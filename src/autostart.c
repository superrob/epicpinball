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
 * In a cabinet (autostart_arcade), PINBALL stays unseen after the table too:
 * the playfield keeps its last picture, dimmed, and the DMD is ours
 * (arcade.c). A high score's initials are chosen there with the flipper
 * buttons and typed to PINBALL as keys; then GAME OVER and the high scores
 * until Start, which is PLAY GAME, driven as the first one was.
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
#include "arcade.h"
#include "ini.h"
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
enum { OFF, BOOT, LIST, SCREEN, EXEC, TABLE, PLAY, AFTER, LEAVE, ENTRY, SEND, READY };
static int phase = OFF;
static int quitting;                  /* AFTER: the player quit the table, so out once its initials are in */
static int on_screen;                 /* AFTER: the table's screen is the one open (not rules, options) */

/* A cabinet's, between games. ENTRY: the initials chosen on the DMD; SEND:
 * typed to PINBALL; READY: GAME OVER until Start */
static int arcade;                    /* --cabinet: between games on the DMD */
static int between;                   /* the table has ended: the DMD and the buttons are ours, PINBALL unseen */
static int start_req, leave_req;      /* READY: Start, Esc (or the window closed) */
static int quit_after;                /* ENTRY: then out, not READY */
static uint8_t queue[8];              /* SEND: the keys of the initials, then Enter */
static int nqueue, iqueue;
static double ready_ms;               /* READY began: its pages */
static int saves_same;                /* hiscore.pin was the high scores in memory, byte for byte */

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

static const char *const phase_name[] = { "off", "boot", "list", "screen", "exec", "table", "play", "after", "leave",
                                         "entry", "send", "ready" };

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
    AT(entry), AT(scores), AT(letters),
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
            if (version != 2) { *why = "a menus map of another version"; return 0; }
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
void autostart_arcade(int on) { arcade = on; }

void autostart_begin_map(const MenusMap *m, uint16_t load_seg)
{
    M = *m;
    CS = (uint32_t)load_seg * 16;
    held = 0; gap = 0; quitting = 0; on_screen = 0;
    between = start_req = leave_req = quit_after = nqueue = iqueue = 0;
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

/* ---- between games, in a cabinet ------------------------------------------------- */

#define SCORES_SIZE 1000              /* 25 tables of 40 bytes: hiscore.pin as it is in memory */
#define TABLE_SCORES 40
#define SHOWN_SCORES 3                /* each table's: three initials, a space, the score (32 bits) */

static uint32_t at32(uint16_t off) { return at16(off) | (uint32_t)at16((uint16_t)(off + 2)) << 16; }

static void top_scores(ArcadeScore top[SHOWN_SCORES])
{
    for (int i = 0; i < SHOWN_SCORES; i++) {
        const uint16_t e = (uint16_t)(M.scores + (want - 1) * TABLE_SCORES + 8 * i);
        for (int k = 0; k < 3; k++) {
            const uint8_t c = at8((uint16_t)(e + k));
            top[i].initials[k] = c >= ' ' && c < 0x7F ? (char)c : ' ';
        }
        top[i].initials[3] = 0;
        top[i].score = at32((uint16_t)(e + 4));
    }
}

/* The scan code that types c in the initials (PINBALL's own table), or 0.
 * Esc, Backspace and Enter are blanks in it, and keys there, not letters. */
static int key_of(char c)
{
    if (c == ' ' && at8((uint16_t)(M.letters + SC_SPACE - 1)) == ' ') return SC_SPACE;   /* not Tab's blank */
    for (int sc = 2; sc <= SC_SPACE; sc++)
        if (sc != 0x0E && sc != SC_ENTER && at8((uint16_t)(M.letters + sc - 1)) == (uint8_t)c) return sc;
    return 0;
}

static int letter_of(int sc)
{
    if (sc < 2 || sc >= SC_SPACE || sc == 0x0E || sc == SC_ENTER) return 0;
    const uint8_t c = at8((uint16_t)(M.letters + sc - 1));
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ? c : 0;
}

/* hiscore.pin in the save folder, as it is spelt there */
static const char *scores_path(void)
{
    static char p[600];
    snprintf(p, sizeof p, "%s/HISCORE.PIN", g_opt.savedir ? g_opt.savedir : ".");
    FILE *f = fopen(p, "rb");
    if (f) { fclose(f); return p; }
    snprintf(p, sizeof p, "%s/hiscore.pin", g_opt.savedir ? g_opt.savedir : ".");
    return p;
}

/* PINBALL writes the high scores only when it quits; between games they are
 * written as it would, so a cabinet switched off keeps them. Only once
 * hiscore.pin has been seen to be that memory exactly (saves_check). */
static void saves_check(void)
{
    static uint8_t file[SCORES_SIZE + 1];
    FILE *f = fopen(scores_path(), "rb");
    const size_t n = f ? fread(file, 1, sizeof file, f) : 0;
    if (f) fclose(f);
    saves_same = n == SCORES_SIZE && !memcmp(file, g_cpu.mem + CS + M.scores, SCORES_SIZE);
    trace("[table] hiscore.pin %s the high scores in memory\n", saves_same ? "is" : "is not");
}

static void saves_write(void)
{
    if (!saves_same) return;
    if (!file_replace(scores_path(), g_cpu.mem + CS + M.scores, SCORES_SIZE))
        fprintf(stderr, "[table] %s could not be written: the high scores are saved when the game quits\n", scores_path());
}

static void entry_begin(void)
{
    static const char order[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ";
    char letters[sizeof order];
    int n = 0;
    for (const char *c = order; *c; c++) if (key_of(*c)) letters[n++] = *c;
    letters[n] = 0;
    const int e = at16(M.entry) - (want - 1) * TABLE_SCORES;
    const int rank = e >= 0 && e < 8 * SHOWN_SCORES ? e / 8 + 1 : 1;
    arcade_entry(rank, at32((uint16_t)(M.scores + at16(M.entry) + 4)), letters);
    if (held) let_go();
    to(ENTRY);
    machine_fast(0);
}

/* The initials chosen: typed to PINBALL, unseen */
static void entry_send(void)
{
    const char *in = arcade_initials();
    nqueue = iqueue = 0;
    for (; *in && nqueue < 3; in++) { const int k = key_of(*in); if (k) queue[nqueue++] = (uint8_t)k; }
    if (!nqueue) queue[nqueue++] = SC_SPACE;     /* none: blank, not PINBALL's ??? */
    queue[nqueue++] = SC_ENTER;
    trace("[table] initials \"%s\"\n", arcade_initials());
    to(SEND);
    machine_fast(1);
}

static void ready(void)
{
    to(READY);
    start_req = leave_req = 0;
    ready_ms = now_ms();
    machine_fast(0);
}

/* A menu frame between games: 1 when it is taken care of here */
static int between_frame(int intro, int main_menu)
{
    switch (phase) {
    case AFTER:                                  /* PINBALL fades in, decides on a high score, draws the table's screen */
        if (intro || (!on_screen && !main_menu)) return 1;
        if (at16(M.initials)) entry_begin();
        else if (quitting || main_menu) leave();
        else ready();
        return 1;
    case ENTRY:                                  /* the player chooses (autostart_key) */
        return 1;
    case SEND:
        if (held) return 1;
        if (gap) { gap--; return 1; }
        if (iqueue < nqueue) {
            if (queue[iqueue] == SC_ENTER) press_to_leave(queue[iqueue++]);
            else press(queue[iqueue++], HOLD_MS);
            return 1;
        }
        if (at16(M.initials)) { press_to_leave(SC_ENTER); return 1; }   /* not taken yet: again */
        saves_write();
        if (quit_after || quitting) leave();
        else ready();
        return 1;
    case READY:
        if (leave_req) leave();
        else if (start_req) {                    /* PLAY GAME, as the first time */
            trace("[table] start\n");
            to(SCREEN);
            machine_fast(1);
        }
        return 1;
    }
    return 0;
}

int autostart_key(int sc)
{
    if (phase == ENTRY) {
        int what = 0;
        switch (sc) {
        case 0x2A: what = ARC_PREV; break;       /* the left flipper's key (LShift) */
        case 0x36: what = ARC_NEXT; break;       /* the right's (RShift) */
        case SC_ENTER: case SC_SPACE: what = ARC_TAKE; break;   /* Start, the launch button */
        case 0x0E: what = ARC_BACK; break;
        case SC_ESC: what = ARC_DONE; quit_after = 1; break;
        }
        const int letter = what ? 0 : letter_of(sc);
        if ((what || letter) && arcade_entry_input(what, letter)) entry_send();
        return 1;
    }
    if (phase == READY) {
        if (sc == SC_ENTER) start_req = 1;
        else if (sc == SC_ESC) leave_req = 1;
        return 1;
    }
    return 0;
}

int autostart_quit(void)
{
    if (phase == ENTRY) { quit_after = 1; entry_send(); return 1; }
    if (phase == READY) { leave_req = 1; return 1; }
    return 0;
}

int autostart_between(void) { return between; }

int autostart_hold(void)
{
    return arcade && (between || (phase == PLAY && watch_game_over() == 1));
}

int autostart_dmd(uint32_t out[DMD_W * DMD_H])
{
    static uint32_t last[DMD_W * DMD_H];
    if (!between) return -1;
    if (phase == ENTRY || phase == SEND) arcade_draw_entry(out, now_ms());
    else {
        ArcadeScore top[SHOWN_SCORES];
        top_scores(top);
        arcade_draw_ready(out, phase == READY ? now_ms() - ready_ms : 0, top);
    }
    const int changed = memcmp(out, last, sizeof last) != 0;
    memcpy(last, out, sizeof last);
    return changed;
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
    if (between && between_frame(intro, main_menu)) return;
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
    static int checked;
    if (arcade && !checked++) saves_check();     /* the first game: the scores as PINBALL read them */
    between = 0;
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
    on_screen = 0;                               /* drawn again after the table */
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
    if (arcade) between = 1;                     /* a cabinet: PINBALL unseen, the DMD ours */
    if (quitting || arcade) machine_fast(1);     /* unseen, unless initials are wanted */
}

int autostart_busy(void)
{
    switch (phase) {
    case BOOT: case LIST: case SCREEN: case EXEC: case TABLE: return 1;
    case AFTER: return (quitting || between) && machine_is_fast() ? 2 : 0;
    case LEAVE: case SEND: return 2;
    default: return 0;                           /* PLAY, ENTRY, READY: the player's */
    }
}
