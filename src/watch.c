/*
 * watch.c - reads a running table for cabinet feedback, and changes nothing.
 *
 * Three seams, none of which needs a re-lift:
 *
 *   - RECOMP_ENTER, at the top of every lifted function (src/lifted.h): each
 *     physics step, and the moment the kicker is entered, when ES:BX still
 *     points at the pixel of the collision map the ball touched;
 *   - the memory write hook (vga.c): the instructions that can only mean one
 *     thing. A hit is the kicker storing its strength, not being entered (it
 *     turns back on tilt and the like); a launch is the release block zeroing
 *     the pull and then changing ball 0's velocity. What the host writes
 *     itself does not come through here, so cannot pass for either;
 *   - sampling, on every host_frame and physics step, for levels: flippers.
 *
 * Where each of these lives is in work/feedback/epN.map, generated from the
 * player's own copy by tools/feedback_map.py. docs/cabinet-feedback.md.
 */
#include "watch.h"
#include "feedback.h"
#include <stddef.h>

int g_watch_on;
uint32_t g_watch_ds;
uint8_t g_watched[0x10000];

/* The game runs three physics steps a frame, about 5 ms apart. Silent this
 * long, it is paused, at a prompt or between screens, with stale flippers. */
#define IDLE_MS 50.0

/* Enigma's bumpers are stamped into its map as it runs: by where they stand */
#define STAMPED_LEFT  150
#define STAMPED_RIGHT 180

enum { W_NONE, W_KICK, W_PULL, W_PULL_HI, W_VY, W_ACTIVE, W_COUNT, W_EXTRA, W_METER, W_TILT, W_OVER };

static int wanted;
static WatchMap M;
static uint32_t DS, CS;              /* linear */
static int kicking, kick_x, kick_y, kick_di;
static int pull_old, launched;
static int release, started;
static int flipper[2], known, idle;
static double last_step;
static uint16_t ss0, sp0;            /* the table's stack */

static uint16_t rd16(uint32_t a) { return (uint16_t)(g_cpu.mem[a] | g_cpu.mem[a + 1] << 8); }
static double now_ms(void) { return pit_now() * 1000.0 / PIT_HZ; }

/* A table's stack can leave its segment: a ball off the left edge of EP13
 * overruns a sprite buffer into the timer chain, and from then on every tick
 * leaks six bytes of stack, until pushes land in the variables watched here.
 * The game limps on; what its memory says is no longer what it means, and
 * must not fire a coil, nor be written to. */
static int sound(void)
{
    if (g_cpu.ss != ss0 || g_cpu.sp <= sp0) return 1;      /* a driver's stack, or within the table's */
    fprintf(stderr, "[feedback] %s: the table's stack has left its bounds (a fault in the game):"
                    " no feedback until it is started again\n", M.name);
    g_watch_on = 0;
    feedback_all_off();
    for (int i = 0; i < 2; i++)
        if (flipper[i]) feedback_event(now_ms(), (FbEvent)(FB_FLIPPER_LEFT + i), 0, (FbDetail){ 0 });
    feedback_event(now_ms(), FB_TABLE_LOST, 1, (FbDetail){ g_cpu.sp, sp0, 0, 0 });
    feedback_table(now_ms(), NULL);
    return 0;
}

static void emit(FbEvent e, int on, int x, int y, int a, int b)
{
    feedback_mute(g_cpu.mem[DS + M.demo] == 1);
    feedback_event(now_ms(), e, on, (FbDetail){ x, y, a, b });
}

/* ---- the map ------------------------------------------------------------- */

#define FIELD(f) { #f, offsetof(WatchMap, f) }
static const struct { const char *key; size_t at; } fields[] = {
    FIELD(ds), FIELD(cs), FIELD(active), FIELD(x), FIELD(y), FIELD(vx), FIELD(vy), FIELD(slots),
    FIELD(flip_l), FIELD(flip_r), FIELD(up_l), FIELD(up_r),
    FIELD(tilt), FIELD(meter), FIELD(cooldown), FIELD(demo), FIELD(extra), FIELD(over), FIELD(count),
    FIELD(drain_y), FIELD(kick), FIELD(weak_y), FIELD(sling_line), FIELD(map),
    FIELD(pull), FIELD(pull_inc), FIELD(pull_max), FIELD(pull_shift), FIELD(ctrl), FIELD(space),
    FIELD(lane_off), FIELD(lane_x), FIELD(lane_y), FIELD(over_pull),
};
#undef FIELD
#define NFIELDS (int)(sizeof fields / sizeof fields[0])

int watch_map_parse(const char *text, WatchMap *m, const char **why)
{
    uint64_t got = 0;
    int version = 0, image = 0, ended = 0;
    memset(m, 0, sizeof *m);
    for (const char *p = text; *p && !ended; ) {
        char line[128], key[24], val[24];
        size_t n = strcspn(p, "\n");
        if (n >= sizeof line) { *why = "a line too long"; return 0; }
        memcpy(line, p, n); line[n] = 0;
        p += n;
        if (*p) p++;
        unsigned a, b, x, y, slot;
        if (!strcmp(line, "end")) ended = 1;
        else if (sscanf(line, "epicpinball-feedback %u", &a) == 1) version = (int)a;
        else if (sscanf(line, "image %7s %u %x", m->name, &a, &b) == 3) { m->size = a; m->fnv = b; image = 1; }
        else if (sscanf(line, "bumper %u %u %u", &x, &y, &slot) == 3) {
            if (m->nbumpers == WATCH_BUMPERS || x >= 320 || y >= 400 || slot > 2) { *why = "a bad bumper"; return 0; }
            m->bumper[m->nbumpers].x = (int)x;
            m->bumper[m->nbumpers].y = (int)y;
            m->bumper[m->nbumpers++].slot = (int)slot;
        } else if (sscanf(line, "%23s %23s", key, val) != 2) { *why = "a line it cannot read"; return 0; }
        else if (!strcmp(key, "step_fn")) snprintf(m->step_fn, sizeof m->step_fn, "%s", val);
        else if (!strcmp(key, "kicker_fn")) snprintf(m->kicker_fn, sizeof m->kicker_fn, "%s", val);
        else {
            int i = 0;
            char *e;
            unsigned long v = strtoul(val, &e, 16);
            while (i < NFIELDS && strcmp(fields[i].key, key)) i++;
            if (i == NFIELDS || *e || v > 0xFFFF) { *why = "a value it does not know"; return 0; }
            uint16_t v16 = (uint16_t)v;
            memcpy((char *)m + fields[i].at, &v16, 2);
            got |= 1ull << i;
        }
    }
    if (version != 1) *why = "another version's map";
    else if (!ended || !image || !*m->step_fn || !*m->kicker_fn || got != (1ull << NFIELDS) - 1) *why = "incomplete";
    else if (m->slots < 1 || m->slots > 8) *why = "a bad ball count";
    else return 1;
    return 0;
}

int watch_kick_event(const WatchMap *m, int x, int y)
{
    if (y >= m->sling_line) return x < 160 ? FB_SLING_LEFT : FB_SLING_RIGHT;
    int slot = x < STAMPED_LEFT ? 0 : x < STAMPED_RIGHT ? 1 : 2, best = 1 << 30;
    for (int i = 0; i < m->nbumpers; i++) {
        int dx = x - m->bumper[i].x, dy = y - m->bumper[i].y;
        if (dx * dx + dy * dy < best) { best = dx * dx + dy * dy; slot = m->bumper[i].slot; }
    }
    return FB_BUMPER_LEFT + slot;
}

/* ---- a table's life ------------------------------------------------------- */

void watch_wanted(void) { wanted = 1; }

static void watch(uint16_t off, int what) { g_watched[off] = (uint8_t)what; }

void watch_table_begin(const LiftedImage *img, uint16_t load_seg)
{
    if (!wanted || !img || strncmp(img->name, "ep", 2)) return;
    static char text[8192];
    char path[96];
    const char *why = "";
    snprintf(path, sizeof path, "work/feedback/%s.map", img->name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[feedback] no %s: run tools/feedback_map.py. Off for %s.\n", path, img->name);
        return;
    }
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    if (!watch_map_parse(text, &M, &why)) {
        fprintf(stderr, "[feedback] %s: %s. Run tools/feedback_map.py. Off for %s.\n", path, why, img->name);
        return;
    }
    /* A map of another file would name arbitrary guest memory */
    if (strcmp(M.name, img->name) || M.size != img->size || M.fnv != img->fnv) {
        fprintf(stderr, "[feedback] %s is of another copy of %s. Run tools/feedback_map.py. Off for it.\n",
                path, img->file);
        return;
    }
    watch_map_use(&M, load_seg);
}

int watch_map_use(const WatchMap *m, uint16_t load_seg)
{
    if (m != &M) M = *m;
    DS = (uint32_t)(uint16_t)(load_seg + M.ds) * 16;
    CS = (uint32_t)(uint16_t)(load_seg + M.cs) * 16;
    if (DS + 0x10000 > MEM_SIZE || CS + 0x10000 > MEM_SIZE) return 0;

    memset(g_watched, 0, sizeof g_watched);
    watch(M.kick, W_KICK);
    watch(M.pull, W_PULL);
    watch((uint16_t)(M.pull + 1), W_PULL_HI);
    watch(M.vy, W_VY);
    for (int i = 0; i < M.slots; i++) watch((uint16_t)(M.active + 2 * i), W_ACTIVE);
    watch(M.count, W_COUNT);
    watch(M.extra, W_EXTRA);
    watch(M.meter, W_METER);
    watch(M.tilt, W_TILT);
    watch(M.over, W_OVER);
    kicking = launched = release = started = known = idle = 0;
    pull_old = 0;
    flipper[0] = flipper[1] = 0;
    last_step = now_ms();
    ss0 = g_cpu.ss; sp0 = g_cpu.sp;                /* as load_exe left them */
    feedback_table(now_ms(), M.name);
    g_watch_ds = DS;
    g_watch_on = 1;
    return 1;
}

void watch_table_end(void)
{
    if (!g_watch_on) return;
    g_watch_on = 0;
    for (int i = 0; i < 2; i++)
        if (flipper[i]) feedback_event(now_ms(), (FbEvent)(FB_FLIPPER_LEFT + i), 0, (FbDetail){ 0 });
    feedback_table(now_ms(), NULL);
}

const WatchMap *watch_table(uint32_t *ds, uint32_t *cs)
{
    if (!g_watch_on) return NULL;
    *ds = DS; *cs = CS;
    return &M;
}

int watch_running(void)
{
    return g_watch_on && started && now_ms() - last_step <= IDLE_MS;
}

/* ---- levels ----------------------------------------------------------------- */

/* The coil: from the first step of the upstroke to the first of the fall. The
 * key would be wrong in every state where the game ignores it. */
static void flippers(void)
{
    for (int i = 0; i < 2; i++) {
        uint32_t pos = DS + (i ? M.flip_r : M.flip_l), up = DS + (i ? M.up_r : M.up_l);
        int on = g_cpu.mem[up] == 1 || rd16(pos) == 0;
        if (known & (1 << i)) {
            if (on != flipper[i]) emit((FbEvent)(FB_FLIPPER_LEFT + i), on, 0, 0, rd16(pos), 0);
            flipper[i] = on;
        } else if (!on) {
            known |= 1 << i;         /* after a standstill: not felt until its next flip */
            flipper[i] = 0;
        }
    }
}

static void stepped(void)
{
    last_step = now_ms();
    idle = 0;
    feedback_mute(g_cpu.mem[DS + M.demo] == 1);
    if (!started) {
        started = 1;                 /* the first ball of a game */
        if (M.lane_x && rd16(DS + M.active) && rd16(DS + M.x) >= M.lane_x) release = 1;
    }
    if (release) {
        release = 0;
        if (g_cpu.mem[DS + M.over] != 1) emit(FB_BALL_RELEASE, 1, rd16(DS + M.x), rd16(DS + M.y), 0, 0);
    }
    flippers();
}

void watch_tick(void)
{
    if (!g_watch_on || !sound() || !started || idle) return;
    if (now_ms() - last_step > IDLE_MS) {
        for (int i = 0; i < 2; i++)
            if (flipper[i]) emit((FbEvent)(FB_FLIPPER_LEFT + i), 0, 0, 0, -1, 0);
        flipper[0] = flipper[1] = known = 0;
        idle = 1;
        return;
    }
    flippers();
}

/* ---- functions entered ------------------------------------------------------ */

void watch_enter(CPU *cpu, const char *fn)
{
    if (!strcmp(fn, M.step_fn)) {
        if (sound()) stepped();
    } else if (!strcmp(fn, M.kicker_fn)) {
        if (!sound()) return;
        uint32_t o = (uint32_t)(uint16_t)(cpu->es - rd16(DS + M.map)) * 16 + cpu->bx;
        kicking = o < 320u * 400u;
        kick_x = (int)(o % 320);
        kick_y = (int)(o / 320);
        kick_di = cpu->di;
    }
}

/* ---- instructions that mean one thing --------------------------------------- */

/* Called before the store, so memory still has the old value */
void watch_write_byte(uint32_t a, uint8_t v)
{
    uint16_t off = (uint16_t)(a - DS);
    uint8_t old = g_cpu.mem[a];
    if (!sound()) return;
    switch (g_watched[off]) {
    case W_KICK:
        if (!kicking || !v) break;
        kicking = 0;
        {
            uint16_t bx = rd16(DS + (uint16_t)(M.x + kick_di)), by = rd16(DS + (uint16_t)(M.y + kick_di));
            if (by >= M.weak_y) break;             /* tables 11-13: a bounce, no kick */
            emit((FbEvent)watch_kick_event(&M, kick_x, kick_y), 1, kick_x, kick_y, bx, by);
        }
        break;
    case W_PULL:
        pull_old = rd16(a);                        /* a word: low byte, then high */
        break;
    case W_PULL_HI:
        if (!v && !g_cpu.mem[a - 1] && pull_old) launched = pull_old;
        break;
    case W_VY:
        if (!launched) break;
        emit(FB_LAUNCH, 1, rd16(DS + M.x), rd16(DS + M.y), launched, (int16_t)rd16(a));
        launched = 0;
        break;
    case W_ACTIVE: {
        uint16_t i = (uint16_t)(off - M.active), y = rd16(DS + (uint16_t)(M.y + i));
        if (!v && old == 1 && y >= M.drain_y) emit(FB_DRAIN, 1, rd16(DS + (uint16_t)(M.x + i)), y, i / 2, 0);
        break;
    }
    case W_COUNT:
        if (v == 1 && old == 2) release = 1;       /* the next ball, unless that was the last */
        break;
    case W_EXTRA:
        if (v > old) emit(FB_KNOCKER, 1, 0, 0, v, 0);
        break;
    case W_METER:
        if (v == (uint8_t)(old + 0x23)) emit(FB_NUDGE, 1, 0, 0, v, 0);
        break;
    case W_TILT:
        if (v == 1 && old != 1) emit(FB_TILT, 1, 0, 0, 0, 0);
        break;
    case W_OVER:
        if (v == 1 && old != 1) emit(FB_GAME_OVER, 1, 0, 0, 0, 0);
        break;
    }
}
