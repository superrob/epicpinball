/*
 * autostart.c - --table N (docs/table.md).
 *
 * PINBALL.EXE has to run: it loads the sound drivers and the table's music,
 * then EXECs the table, and when it quits it writes config.pin and
 * hiscore.pin (a table writes neither). So --table drives PINBALL's own menus,
 * a key at a time, each key chosen from where PINBALL is, read from its code
 * and memory through the RECOMP_ENTER seam, never from a clock:
 *
 *   the intro, the main menu   Enter (Up first if the cursor is not on BEGIN)
 *   the table list             Down or Up to table N's place, then Enter
 *   table N's screen           Enter on PLAY GAME: the game starts by itself
 *   table N                    the player's, from the ball's first step
 *   after it                   a high score's initials: the player's
 *     the player quit it       Esc on its screen, Esc on the main menu: PINBALL quits
 *     the game ended           its screen is the player's: PLAY GAME plays again,
 *                              and from the main menu (EXIT, Esc) PINBALL quits
 *
 * Each menu calls one function a frame while it waits for a key (frame_fn),
 * and the intro another (intro_fn); a key is pressed on such a frame and let
 * go some 50 ms of emulated time later, as a finger would. While PINBALL is
 * driven, emulated time runs as fast as it goes and nothing is shown or
 * heard (machine_fast), and the player's own keys are not the game's.
 *
 * Where these are, in your own PINBALL.EXE: work/feedback/pin.map, written
 * by tools/feedback_map.py from the lifted code.
 */
#include "autostart.h"
#include "machine.h"
#include "watch.h"
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_auto_on;
static int want;                      /* table N, 1-13 */
static MenusMap M;
static uint32_t CS;                   /* linear: PINBALL's first code segment */

/* BOOT..TABLE: to the game, driven. PLAY: the player's. AFTER: the table
 * has ended; the player's while initials are typed, or (the game over) on
 * its screen. LEAVE: driven out. */
enum { OFF, BOOT, LIST, SCREEN, EXEC, TABLE, PLAY, AFTER, LEAVE };
static int phase = OFF;
static int quitting;                  /* AFTER: the player quit the table, so out once its initials are in */
static int on_screen;                 /* AFTER: the table's screen is the one open (not rules, options) */

#define HOLD_MS 50.0                  /* a key is down this long */
#define GAP_FRAMES 2                  /* and up this many frames before the next */
#define RETRY_FRAMES 70               /* a key that changes the screen: about a second for it to */

static uint8_t held;                  /* the key down, 0 none */
static double held_until;             /* emulated ms; 0: until the run ends */
static int gap;                       /* menu frames to wait before the next key */
static uint16_t last_frames;          /* the main menu's frame count at the last menu frame */

static double now_ms(void) { return pit_now() * 1000.0 / PIT_HZ; }
static uint8_t at8(uint16_t off) { return g_cpu.mem[CS + off]; }
static uint16_t at16(uint16_t off) { return (uint16_t)(at8(off) | at8((uint16_t)(off + 1)) << 8); }

enum { SC_ESC = 0x01, SC_ENTER = 0x1C, SC_SPACE = 0x39, SC_UP = 0x48, SC_DOWN = 0x50 };

static const char *const phase_name[] = { "off", "boot", "list", "screen", "exec", "table", "play", "after", "leave" };

static void to(int p)
{
    trace("[table] %.0f ms: %s\n", now_ms(), phase_name[p]);
    phase = p;
    gap = GAP_FRAMES;
}

static void scan(uint8_t sc)
{
    if ((sc & 0x7F) == SC_UP || (sc & 0x7F) == SC_DOWN) kbd_scancode(0xE0);
    kbd_scancode(sc);
}

static void press(uint8_t sc, double for_ms)
{
    trace("[table] %.0f ms, %s: key %02X\n", now_ms(), phase_name[phase], sc);
    scan(sc);
    held = sc;
    held_until = for_ms > 0 ? now_ms() + for_ms : 0;
}

static void let_go(void)
{
    scan((uint8_t)(held | 0x80));
    held = 0;
    if (gap < GAP_FRAMES) gap = GAP_FRAMES;
}

/* Enter or Esc that leads to another screen: none again until it comes,
 * or a second's frames later the screen is still this one */
static void press_to_leave(uint8_t sc)
{
    press(sc, HOLD_MS);
    gap = RETRY_FRAMES;
}

/* ---- the map -------------------------------------------------------------------- */

#define FN(f) { #f, offsetof(MenusMap, f), 1 }
#define AT(f) { #f, offsetof(MenusMap, f), 0 }
static const struct { const char *key; size_t at; int fn; } fields[] = {
    AT(key), AT(cursor), AT(scroll), AT(selected), AT(present), AT(screen_cursor), AT(initials), AT(menu_frames),
    FN(frame_fn), FN(intro_fn), FN(menus_fn), FN(screen_fn), FN(exec_fn), FN(end_fn),
};
#define NFIELDS (sizeof fields / sizeof fields[0])

int autostart_map_parse(const char *text, MenusMap *m, const char **why)
{
    memset(m, 0, sizeof *m);
    int version = 0, seen = 0, image = 0, ended = 0;
    unsigned got = 0;
    for (const char *p = text; *p; ) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        char line[128];
        if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, p, n); line[n] = 0;
        p = e ? e + 1 : p + n;
        char k[32], v[64];
        if (!*line) continue;
        if (!seen++) {
            if (sscanf(line, "epicpinball-menus %d", &version) != 1) { *why = "not a menus map"; return 0; }
            if (version != 1) { *why = "a menus map of another version"; return 0; }
            continue;
        }
        if (!strcmp(line, "end")) { ended = 1; break; }
        if (!strncmp(line, "image ", 6)) {
            char name[16];
            if (sscanf(line, "image %15s %u %x", name, &m->size, &m->fnv) != 3 || strcmp(name, "pin")) {
                *why = "its image line"; return 0;
            }
            image = 1;
            continue;
        }
        if (sscanf(line, "%31s %63s", k, v) != 2) { *why = "a line it cannot read"; return 0; }
        if (!strcmp(k, "sub_fn")) {                      /* the table screen's rules, options: any number */
            if (m->nsub == SUB_FNS || strlen(v) >= sizeof m->sub_fn[0]) { *why = "too many sub_fn"; return 0; }
            strcpy(m->sub_fn[m->nsub++], v);
            continue;
        }
        unsigned i = 0;
        while (i < NFIELDS && strcmp(fields[i].key, k)) i++;
        if (i == NFIELDS) continue;                      /* not one this needs */
        if (fields[i].fn) {
            if (strlen(v) >= sizeof m->frame_fn) { *why = "a function name too long"; return 0; }
            strcpy((char *)m + fields[i].at, v);
        } else {
            *(uint16_t *)((char *)m + fields[i].at) = (uint16_t)strtoul(v, NULL, 16);
        }
        got |= 1u << i;
    }
    if (!image || !ended || got != (1u << NFIELDS) - 1 || !m->nsub) { *why = "it is incomplete"; return 0; }
    return 1;
}

int autostart_list_position(const uint8_t *present, int table)
{
    if (table < 1 || table > 25 || (present[2 * (table - 1)] | present[2 * (table - 1) + 1] << 8) != 1) return -1;
    int at = 0;
    for (int i = 0; i < table - 1; i++) at += (present[2 * i] | present[2 * i + 1] << 8) == 1;
    return at;
}

/* ---- a run ------------------------------------------------------------------------ */

void autostart_wanted(int table) { want = table; }

void autostart_begin_map(const MenusMap *m, uint16_t load_seg)
{
    M = *m;
    CS = (uint32_t)load_seg * 16;
    held = 0; gap = 0; quitting = 0; on_screen = 0;
    last_frames = 0;
    g_auto_on = 1;
    to(BOOT);
    machine_fast(1);
}

void autostart_begin(const LiftedImage *img, uint16_t load_seg)
{
    if (!want) return;
    static char text[2048];
    MenusMap m;
    const char *why = "";
    FILE *f = fopen("work/feedback/pin.map", "rb");
    if (!f) fatal("--table: no work/feedback/pin.map. Run tools/feedback_map.py");
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    if (!autostart_map_parse(text, &m, &why))
        fatal("--table: work/feedback/pin.map: %s. Run tools/feedback_map.py", why);
    if (!img || strcmp(img->name, "pin") || img->size != m.size || img->fnv != m.fnv)
        fatal("--table: work/feedback/pin.map is of another PINBALL.EXE. Run tools/feedback_map.py");
    autostart_begin_map(&m, load_seg);
}

static void stop(const char *why)
{
    machine_fast(0);
    fatal("--table %d: %s", want, why);
}

static void leave(void)
{
    to(LEAVE);
    machine_fast(1);
}

/* A menu waits for a key: the next one, from where PINBALL is */
static void menu_frame(int intro)
{
    int main_menu = 0;
    if (!intro) {                                /* only the main menu counts its frames */
        const uint16_t f = at16(M.menu_frames);
        main_menu = f == (uint16_t)(last_frames + 1);
        last_frames = f;
    }
    if ((phase == AFTER || phase == LEAVE) && at16(M.initials)) {
        if (phase == LEAVE) to(AFTER);           /* a high score: the player types the initials, */
        if (held) let_go();                      /* shown, then out as before */
        if (machine_is_fast()) { trace("[table] initials\n"); machine_fast(0); }
        return;
    }
    if (phase == AFTER) {
        /* the key the table's screen reads right after this call: Esc, or
         * Enter or Space on EXIT, leaves it for the list */
        const uint8_t k = at8(M.key);
        const int exiting = on_screen && (k == SC_ESC || ((k == SC_ENTER || k == SC_SPACE) && at8(M.screen_cursor) >= 3));
        if (quitting || exiting || main_menu) leave();
        else return;                             /* its screen, rules, options: the player's */
    }
    if (held) return;
    if (gap) { gap--; return; }
    switch (phase) {
    case BOOT:                                   /* the intro, then the main menu on BEGIN */
        if (intro) press(SC_ENTER, HOLD_MS);
        else if (at8(M.cursor)) press(SC_UP, HOLD_MS);
        else press_to_leave(SC_ENTER);
        break;
    case LIST: {
        int to = autostart_list_position(g_cpu.mem + CS + M.present, want);
        if (to < 0) stop("that table is not installed");
        int at = at8(M.cursor) + at16(M.scroll);
        if (at != to) press(at < to ? SC_DOWN : SC_UP, HOLD_MS);
        else press_to_leave(SC_ENTER);
        break;
    }
    case SCREEN:                                 /* PLAY GAME is the first */
        if (at8(M.screen_cursor)) press(SC_UP, HOLD_MS);
        else press_to_leave(SC_ENTER);
        break;
    case LEAVE:                                  /* its screen's EXIT, then the main menu's */
        press_to_leave(SC_ESC);
        break;
    }
}

/* In the table: PLAY GAME started a game, shown once the ball is in play
 * (the table's physics steps); then it is the player's */
static void table_step(void)
{
    if (!g_watch_on)                             /* no feedback map: no telling, so shown from its start */
        fprintf(stderr, "[table] no feedback map for table %d: shown from its start\n", want);
    else if (!watch_running()) return;
    to(PLAY);
    machine_fast(0);
}

void autostart_enter(CPU *cpu, const char *fn)
{
    (void)cpu;
    if (phase == TABLE) {
        if (strncmp(fn, "pin_", 4)) table_step();
        return;
    }
    if (strncmp(fn, "pin_", 4)) return;
    if (!strcmp(fn, M.frame_fn)) menu_frame(0);
    else if (!strcmp(fn, M.intro_fn)) menu_frame(1);
    else if (!strcmp(fn, M.menus_fn)) { if (phase == BOOT) to(LIST); }
    else if (!strcmp(fn, M.screen_fn)) {
        on_screen = 1;
        if (phase == LIST) {
            if (at8(M.selected) != want - 1) stop("the table list chose another table");
            to(SCREEN);
        }
    }
    else if (!strcmp(fn, M.exec_fn)) { if (phase == SCREEN || phase == AFTER) to(EXEC); }
    else if (!strcmp(fn, M.end_fn)) { if (phase == LEAVE) { if (held) let_go(); press(SC_ESC, 0); } }
    else for (int i = 0; i < M.nsub; i++) if (!strcmp(fn, M.sub_fn[i])) on_screen = 0;
}

void autostart_poll(void)
{
    if (held && held_until > 0 && now_ms() >= held_until) let_go();
}

void autostart_exec(const char *name, uint8_t *tail)
{
    if (phase != EXEC) return;
    const char *b = name;
    for (const char *p = name; *p; p++) if (*p == '\\' || *p == '/' || *p == ':') b = p + 1;
    char expect[16];
    snprintf(expect, sizeof expect, "EP%d.EXE", want);
    int same = strlen(b) == strlen(expect);
    for (size_t i = 0; same && b[i]; i++) same = toupper((unsigned char)b[i]) == expect[i];
    if (!same) stop("PINBALL ran another program than that table");
    if (tail[0] >= 2 && tail[1] == ' ' && tail[2] >= '1' && tail[2] <= '4') tail[2] = '1';   /* one player */
    if (held) let_go();
    to(TABLE);
}

/* Before the watcher lets go of the table: was a game still running? Then
 * the player quit it; otherwise the game ended. */
void autostart_exec_done(void)
{
    if (phase != TABLE && phase != PLAY) return;
    quitting = watch_game_over() != 1;           /* no feedback map: as if quit */
    if (held) let_go();
    to(AFTER);
    if (quitting) machine_fast(1);               /* unseen, unless initials are wanted */
}

int autostart_busy(void)
{
    switch (phase) {
    case BOOT: case LIST: case SCREEN: case EXEC: case TABLE: return 1;
    case AFTER: return quitting && machine_is_fast() ? 2 : 0;
    case LEAVE: return 2;
    default: return 0;
    }
}
