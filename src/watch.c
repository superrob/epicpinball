/*
 * watch.c - reads a running table for cabinet feedback, and changes nothing.
 *
 * Three seams, none of which needs a re-lift:
 *
 *   - RECOMP_ENTER, at the top of every lifted function (src/lifted.h): each
 *     physics step; the moment the kicker is entered, when ES:BX still points
 *     at the pixel of the collision map the ball touched; the switch handler,
 *     with the colour of the switch the ball is on in AL; and the message
 *     function, with the text it shows in BX;
 *   - the memory write hook (vga.c): the instructions that can only mean one
 *     thing. A hit is the kicker storing its strength, not being entered (it
 *     turns back on tilt and the like); a launch is the release block zeroing
 *     the pull and then changing ball 0's velocity. What the host writes
 *     itself does not come through here, so cannot pass for either;
 *   - sampling, on every host_frame and physics step, for levels: flippers;
 *   - the lamps: RECOMP_ENTER of their renderer, with the list of lamp
 *     states it draws in SI, and of the blitter it calls, with a lamp's lit
 *     or unlit picture in SI, its place in the table of them in BX and the
 *     lamp's state in that list on the stack. What it draws is what the
 *     player sees, a light show or a blink included; each host_frame sends
 *     what has changed. Tables 3 and 5 also glow lights on the palette: their
 *     glow function, with the light's place in the list in CX, its state in BL.
 *
 * Where each of these lives is in work/feedback/epN.map, generated from the
 * player's own copy by tools/feedback_map.py. docs/cabinet-feedback.md.
 */
#include "watch.h"
#include "nudge.h"
#include "feedback.h"
#include "plunger.h"
#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>

int g_watch_on;
uint32_t g_watch_ds;
uint8_t g_watched[0x10000];

/* The game runs three physics steps a frame, about 5 ms apart. Silent this
 * long, it is paused, at a prompt or between screens, with stale flippers. */
#define IDLE_MS 50.0

/* Enigma's bumpers are stamped into its map as it runs: by where they stand */
#define STAMPED_LEFT  150
#define STAMPED_RIGHT 180

/* A switch closes when the ball reaches it; the game then leaves it alone a
 * few steps. Not called this long, the ball has left it. A hole's timer
 * counts only while its switch is handled, and a tilt stops that: not
 * counted this long, the ball has left it. */
#define SWITCH_GAP_MS 150.0
#define HOLD_GAP_MS 150.0

enum { W_NONE, W_KICK, W_PULL, W_PULL_HI, W_VY, W_ACTIVE, W_COUNT, W_EXTRA, W_METER, W_TILT, W_OVER,
       W_TIMER, W_TARGET, W_LANES, W_RAMPS, W_BANK };

static int wanted;
static WatchMap M;
static uint32_t DS, CS;              /* linear */
static int kicking, kick_x, kick_y, kick_di;
static double kick_t = -1e18, sw_t = -1e18;     /* when each was last seen: SSF places effects by them */
static int pull_old, launched;
static int release, started, fresh;  /* fresh: the next release begins a game */
static int flipper[2], known, idle;
static int ready, multi, held;       /* the levels sent: ball_ready, multiball, ball_held */
static int sw_on, sw_colour, sw_x, sw_y;                    /* the switch being handled this step */
static double sw_last[WATCH_ZONES];  /* when each zone's switch was last handled */
static int caught[WATCH_TIMERS];     /* the hole each timer holds a ball in, 1 on; 0: none known */
static double counted[WATCH_TIMERS]; /* when each timer last counted */
static struct { uint16_t x, y; double moved; } ball[8];      /* when each ball last moved */
static double last_step;
static uint16_t lamp_ds;             /* the lamps' pictures' segment */
static int lamp_list = -1;           /* the list the renderer draws; -1 none yet */
static uint8_t lamp_drawn[WATCH_LAMPS], lamp_sent[WATCH_LAMPS];  /* lit on the screen; and as sent */
static int lamp_muted;               /* the demo, when lamps were last sent */
static int lamp_check;               /* EPIC_LAMP_CHECK: testing */
static uint32_t lamp_blits[WATCH_LAMPS], lamp_strays, lamp_wrong;
static uint16_t ss0, sp0;            /* the table's stack */

static uint16_t rd16(uint32_t a) { return (uint16_t)(g_cpu.mem[a] | g_cpu.mem[a + 1] << 8); }
static double now_ms(void) { return pit_now() * 1000.0 / PIT_HZ; }

/* Lamp events the table has: its lamps, and past them its glowing lights */
static int nlamps(void) { return M.glows ? M.glow_from - 1 + M.glows : M.lamps; }

/* Every lamp sent lit, out: the table ends or is lost */
static void lamps_off(double ms)
{
    for (int i = 0; i < nlamps(); i++)
        if (lamp_sent[i]) feedback_event(ms, (FbEvent)(FB_LAMP_1 + i), 0, (FbDetail){ 0 });
    memset(lamp_sent, 0, sizeof lamp_sent);
}

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
    if (ready) feedback_event(now_ms(), FB_BALL_READY, 0, (FbDetail){ 0 });
    if (multi) feedback_event(now_ms(), FB_MULTIBALL, 0, (FbDetail){ 0 });
    if (held) feedback_event(now_ms(), FB_BALL_HELD, 0, (FbDetail){ 0 });
    ready = multi = held = 0;
    lamps_off(now_ms());
    feedback_event(now_ms(), FB_TABLE_LOST, 1, (FbDetail){ g_cpu.sp, sp0, 0, 0 });
    feedback_table(now_ms(), NULL, NULL, NULL);
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
    FIELD(lane_off), FIELD(lane_x), FIELD(lane_y), FIELD(over_pull), FIELD(scan_x), FIELD(scan_y),
    FIELD(nudge_gate), FIELD(gate_lo), FIELD(gate_hi), FIELD(x_min),
    FIELD(lamp_seg), FIELD(lamp_table), FIELD(lamps),
};
#undef FIELD
#define NFIELDS (int)(sizeof fields / sizeof fields[0])

/* The zone kind and number of a table's own feature event; 0 if it is not one */
static int feature_of(int e, int *kind, int *n)
{
    static const struct { int first, count, kind; } runs[] = {
        { FB_HOLE_1, FB_HOLES, WZ_HOLE }, { FB_KICKBACK, 1, WZ_KICKBACK }, { FB_ROLLOVER_1, FB_ROLLOVERS, WZ_ROLLOVER },
        { FB_RAMP_1, FB_RAMPS, WZ_RAMP }, { FB_SWITCH_1, FB_SWITCHES, WZ_SWITCH },
    };
    for (size_t i = 0; i < sizeof runs / sizeof runs[0]; i++)
        if (e >= runs[i].first && e < runs[i].first + runs[i].count) {
            *kind = runs[i].kind;
            *n = e - runs[i].first + 1;
            return 1;
        }
    return 0;
}

/* A feature's name: a letter, then letters and digits */
static int good_name(const char *s)
{
    if (!isalpha((unsigned char)*s) || strlen(s) > WATCH_NAME) return 0;
    for (; *s; s++) if (!isalnum((unsigned char)*s)) return 0;
    return 1;
}

static int same_name(const char *a, const char *b)
{
    for (; *a && tolower((unsigned char)*a) == tolower((unsigned char)*b); a++, b++) {}
    return tolower((unsigned char)*a) == tolower((unsigned char)*b);
}

/* Appended to m->features, a space between; 0 once it is full */
static int add(WatchMap *m, const char *fmt, ...)
{
    const size_t at = strlen(m->features);
    if (at && at + 1 < sizeof m->features) m->features[at] = ' ', m->features[at + 1] = 0;
    const size_t from = strlen(m->features);
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(m->features + from, sizeof m->features - from, fmt, ap);
    va_end(ap);
    return n >= 0 && from + (size_t)n < sizeof m->features;
}

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
        unsigned a, b, x, y, slot, c, x1, y1;
        char kind[16], s, name[WATCH_NAME + 1];
        if (!strcmp(line, "end")) ended = 1;
        else if (sscanf(line, "epicpinball-feedback %u", &a) == 1) version = (int)a;
        else if (sscanf(line, "image %7s %u %x", m->name, &a, &b) == 3) { m->size = a; m->fnv = b; image = 1; }
        else if (sscanf(line, "bumper %u %u %u", &x, &y, &slot) == 3) {
            if (m->nbumpers == WATCH_BUMPERS || x >= 320 || y >= 400 || slot > 2) { *why = "a bad bumper"; return 0; }
            m->bumper[m->nbumpers].x = (int)x;
            m->bumper[m->nbumpers].y = (int)y;
            m->bumper[m->nbumpers++].slot = (int)slot;
        } else if (sscanf(line, "zone %15s %u %x %u %u %u %u %c %40s", kind, &a, &c, &x, &y, &x1, &y1, &s, name) == 9) {
            static const char *const kinds[WZ_KINDS] = { "hole", "kickback", "rollover", "ramp", "switch" };
            static const int most[WZ_KINDS] = { FB_HOLES, 1, FB_ROLLOVERS, FB_RAMPS, FB_SWITCHES };
            int k = 0;
            while (k < WZ_KINDS && strcmp(kinds[k], kind)) k++;
            if (m->nzones == WATCH_ZONES || k == WZ_KINDS || a < 1 || (int)a > most[k] || c < 0xAA || c > 0xFF
                || x > x1 || y > y1 || x1 >= 320 || y1 >= 400 || !strchr("LCR", s) || !good_name(name)) { *why = "a bad zone"; return 0; }
            m->zone[m->nzones] = (WatchZone){ k, (int)a, (int)c, (int)x, (int)y, (int)x1, (int)y1, s, "" };
            strcpy(m->zone[m->nzones++].name, name);
        } else if (sscanf(line, "feature %15s %u %c %40s", kind, &a, &s, name) == 4) {
            static const char *const kinds[WZ_KINDS] = { "hole", "kickback", "rollover", "ramp", "switch" };
            static const int most[WZ_KINDS] = { FB_HOLES, 1, FB_ROLLOVERS, FB_RAMPS, FB_SWITCHES };
            int k = 0;
            while (k < WZ_KINDS && strcmp(kinds[k], kind)) k++;
            if (m->nzones == WATCH_ZONES || k == WZ_KINDS || k == WZ_HOLE || a < 1 || (int)a > most[k]
                || !strchr("LCR", s) || !good_name(name)) { *why = "a bad feature"; return 0; }
            m->zone[m->nzones] = (WatchZone){ k, (int)a, 0, 0, 0, 0, 0, s, "" };     /* colour 0: no switch */
            strcpy(m->zone[m->nzones++].name, name);
        } else if (sscanf(line, "timer %x", &a) == 1 && !strncmp(line, "timer ", 6)) {
            if (m->ntimers == WATCH_TIMERS || a > 0xFFFF) { *why = "a bad timer"; return 0; }
            m->timer[m->ntimers++] = (uint16_t)a;
        } else if (sscanf(line, "target %x %u %c %40s", &a, &b, &s, name) == 4) {
            if (m->ntargets == WATCH_TARGETS || a > 0xFFFF || b < 1 || b > WATCH_TARGETS || !strchr("LCR", s) || !good_name(name)) {
                *why = "a bad target"; return 0;
            }
            m->target[m->ntargets] = (WatchTarget){ (uint16_t)a, (int)b, s, "" };
            strcpy(m->target[m->ntargets++].name, name);
        } else if (sscanf(line, "bank %40s", name) == 1 && !strncmp(line, "bank ", 5)) {
            if (*m->bank || !good_name(name)) { *why = "a bad bank"; return 0; }
            strcpy(m->bank, name);
        } else if (sscanf(line, "bankset %x %u %15s", &a, &b, kind) == 3) {
            int e = feedback_by_name(kind), k, n;
            if (m->nbanksets == WATCH_BANKS || !a || a + b > 0xFFFF || b < 2 || b > 8 || !feature_of(e, &k, &n)) {
                *why = "a bad bank of targets"; return 0;
            }
            m->bankset[m->nbanksets].at = (uint16_t)a;
            m->bankset[m->nbanksets].n = (int)b;
            m->bankset[m->nbanksets++].event = e;
        } else if (sscanf(line, "lanes %x %u", &a, &b) == 2) {
            if (!a || a > 0xFFF0 || b < 2 || b > 8) { *why = "bad lanes"; return 0; }
            m->lanes = (uint16_t)a; m->nlanes = (int)b;
        } else if (sscanf(line, "ramps %x", &a) == 1 && !strncmp(line, "ramps ", 6)) {
            if (!a || a > 0xFFFF) { *why = "a bad ramp counter"; return 0; }
            m->ramps = (uint16_t)a;
        } else if (!strncmp(line, "layer ", 6) && sscanf(line, "layer %x", &a) == 1) {
            if (!a || a > 0xFFFF) { *why = "a bad layer flag"; return 0; }
            m->layer = (uint16_t)a;
        } else if (!strncmp(line, "on_layer ", 9) || !strncmp(line, "tilt_pass ", 10)) {
            const int tilt = line[0] == 't';
            uint8_t *to = tilt ? m->tilt_pass : m->on_layer;
            int *n = tilt ? &m->ntilt_pass : &m->non_layer;
            char *p = line + (tilt ? 10 : 9), *e;
            while (*p) {
                unsigned long v = strtoul(p, &e, 16);
                if (e == p || v < 0xAA || v > 0xFF || *n == 8 || (*e && *e != ' ')) { *why = "a bad gate"; return 0; }
                to[(*n)++] = (uint8_t)v;
                p = *e ? e + 1 : e;
            }
        } else if (sscanf(line, "msg %x %15s", &a, kind) == 2) {
            int e = feedback_by_name(kind), k, n;
            if (m->nmsgs == WATCH_MSGS || a > 0xFFFF
                || (e != FB_JACKPOT && e != FB_SUPER_JACKPOT && e != FB_SKILL_SHOT && e != FB_AWARD
                    && !feature_of(e, &k, &n))) {
                *why = "a bad message"; return 0;
            }
            m->msg[m->nmsgs++] = (WatchMsg){ (uint16_t)a, e };
        } else if (!strncmp(line, "board ", 6)) {          /* SSF: the board's effect slots, hex */
            for (const char *p = line + 6; *p; ) {
                char *e;
                unsigned long v = strtoul(p, &e, 16);
                if (e == p || v > 31 || (*e && *e != ' ')) { *why = "a bad board list"; return 0; }
                m->board |= 1u << v;
                p = *e ? e + 1 : e;
            }
        } else if (sscanf(line, "%23s %23s", key, val) != 2) { *why = "a line it cannot read"; return 0; }
        else if (!strcmp(key, "step_fn")) snprintf(m->step_fn, sizeof m->step_fn, "%s", val);
        else if (!strcmp(key, "kicker_fn")) snprintf(m->kicker_fn, sizeof m->kicker_fn, "%s", val);
        else if (!strcmp(key, "switch_fn")) snprintf(m->switch_fn, sizeof m->switch_fn, "%s", val);
        else if (!strcmp(key, "msg_fn") && !*m->msg_fn) snprintf(m->msg_fn, sizeof m->msg_fn, "%s", val);
        else if (!strcmp(key, "msg_fn")) snprintf(m->msg_fn2, sizeof m->msg_fn2, "%s", val);
        else if (!strcmp(key, "effect_fn")) snprintf(m->effect_fn, sizeof m->effect_fn, "%s", val);
        else if (!strcmp(key, "guard_fn")) snprintf(m->guard_fn, sizeof m->guard_fn, "%s", val);
        else if (!strcmp(key, "lamp_fn")) snprintf(m->lamp_fn, sizeof m->lamp_fn, "%s", val);
        else if (!strcmp(key, "lamp_list_fn")) snprintf(m->lamp_list_fn, sizeof m->lamp_list_fn, "%s", val);
        else if (!strcmp(key, "glow_fn")) snprintf(m->glow_fn, sizeof m->glow_fn, "%s", val);
        else if (!strcmp(key, "glow_from") || !strcmp(key, "glows")) {
            char *e;
            unsigned long v = strtoul(val, &e, 16);
            if (*e || !v || v > WATCH_LAMPS) { *why = "a bad glowing light"; return 0; }
            *(key[4] == '_' ? &m->glow_from : &m->glows) = (int)v;
        }
        else if (!strcmp(key, "sfx_on")) {
            char *e;
            unsigned long v = strtoul(val, &e, 16);
            if (*e || !v || v > 0xFFFF) { *why = "a bad effects switch"; return 0; }
            m->sfx_on = (uint16_t)v;
        }
        else if (!strcmp(key, "sfx_setup_fn")) snprintf(m->sfx_setup_fn, sizeof m->sfx_setup_fn, "%s", val);
        else if (!strcmp(key, "sfx_volume_fn")) snprintf(m->sfx_volume_fn, sizeof m->sfx_volume_fn, "%s", val);
        else if (!strcmp(key, "sfx_stop_fn")) snprintf(m->sfx_stop_fn, sizeof m->sfx_stop_fn, "%s", val);
        else if (!strcmp(key, "sfx_stop_class_fn")) snprintf(m->sfx_stop_class_fn, sizeof m->sfx_stop_class_fn, "%s", val);
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
    for (int i = 0; i < m->nbumpers; i++) m->layout[i] = "LCR"[m->bumper[i].slot];
    m->layout[m->nbumpers] = 0;
    if (!m->nbumpers) strcpy(m->layout, "LCR");    /* Enigma's three, stamped as it runs */
    for (int i = 1; i < m->nbumpers; i++)
        if (m->bumper[i].x < m->bumper[i - 1].x) { *why = "bumpers not left to right"; return 0; }
    /* for the DOF helper: each feature's name and side, by kind and number */
    static const char *const kinds[WZ_KINDS] = { "hole", "kickback", "rollover", "ramp", "switch" };
    int fits = 1;
    const char *names[WATCH_ZONES + WATCH_TARGETS + 2];
    int nnames = 0;
    for (int k = 0; k < WZ_KINDS; k++) {
        int top = 0;
        for (int i = 0; i < m->nzones; i++)
            if (m->zone[i].kind == k && m->zone[i].n > top) top = m->zone[i].n;
        for (int n = 1; n <= top; n++) {
            const WatchZone *first = NULL;
            for (int i = 0; i < m->nzones; i++) {
                const WatchZone *z = &m->zone[i];
                if (z->kind != k || z->n != n) continue;
                if (!first) first = z;
                else if (z->side != first->side) { *why = "a feature on two sides"; return 0; }
                else if (strcmp(z->name, first->name)) { *why = "a feature with two names"; return 0; }
            }
            if (!first) { *why = "a feature's number missing"; return 0; }
            names[nnames++] = first->name;
            fits &= add(m, "%s%d=%s:%c", kinds[k], n, first->name, first->side);
        }
    }
    for (int n = 1; n <= m->ntargets; n++) {
        const WatchTarget *t = NULL;
        for (int i = 0; i < m->ntargets; i++)
            if (m->target[i].n == n) t = &m->target[i];
        if (!t) { *why = "a target's number missing"; return 0; }
        names[nnames++] = t->name;
        fits &= add(m, "target%d=%s:%c", n, t->name, t->side);
    }
    if (!*m->bank != !m->ntargets) { *why = "drop targets and their bank: both or neither"; return 0; }
    char bank_down[WATCH_NAME + 8];                  /* what the helper sends for it */
    if (*m->bank) {
        snprintf(bank_down, sizeof bank_down, "%sDown", m->bank);
        names[nnames++] = bank_down;
        fits &= add(m, "bank=%s", m->bank);
    }
    if (!fits) { *why = "features too long"; return 0; }
    for (int i = 0; i < m->nmsgs + m->nbanksets; i++) {   /* a text, a bank, fires a feature the map has */
        int k, n, found = 0;
        if (!feature_of(i < m->nmsgs ? m->msg[i].event : m->bankset[i - m->nmsgs].event, &k, &n)) continue;
        for (int j = 0; j < m->nzones && !found; j++) found = m->zone[j].kind == k && m->zone[j].n == n;
        if (!found) { *why = "a message or bank for a feature the map has not"; return 0; }
    }
    for (int i = 0; i < nnames; i++)
        for (int j = i + 1; j < nnames; j++)
            if (same_name(names[i], names[j])) { *why = "a feature's name used twice"; return 0; }
    if (version != 8) *why = "another version's map";
    else if (!ended || !image || !*m->step_fn || !*m->kicker_fn || !*m->switch_fn || !*m->guard_fn || !*m->lamp_fn || !*m->lamp_list_fn
             || got != (1ull << NFIELDS) - 1)
        *why = "incomplete";
    else if (!m->lamps || m->lamps > WATCH_LAMPS || m->lamp_table + 4u * m->lamps > 0x10000u) *why = "bad lamps";
    else if (!*m->glow_fn != !m->glows || !m->glows != !m->glow_from
             || (m->glows && (m->glow_from <= m->lamps || m->glow_from - 1 + m->glows > WATCH_LAMPS)))
        *why = "bad glowing lights";
    else if (m->nmsgs && !*m->msg_fn) *why = "messages, but no message function";
    else if (m->nzones && (!m->layer || !m->ntilt_pass)) *why = "zones, but no gate";   /* none may pass on the layer */
    else if (m->slots < 1 || m->slots > 8) *why = "a bad ball count";
    else return 1;
    return 0;
}

int watch_zone(const WatchMap *m, int kind, int colour, int x, int y)
{
    for (int i = 0; i < m->nzones; i++)
        if (m->zone[i].kind == kind && m->zone[i].colour && m->zone[i].colour == colour && x >= m->zone[i].x0 && x <= m->zone[i].x1
            && y >= m->zone[i].y0 && y <= m->zone[i].y1) return i;
    return -1;
}

int watch_kick_event(const WatchMap *m, int x, int y)
{
    if (y >= m->sling_line) return x < 160 ? FB_SLING_LEFT : FB_SLING_RIGHT;
    if (!m->nbumpers)                              /* Enigma's, by where they stand */
        return fb_bumper(x < STAMPED_LEFT ? 0 : x < STAMPED_RIGHT ? 1 : 2);
    int event = FB_BUMPER_1, best = 1 << 30;
    for (int i = 0; i < m->nbumpers; i++) {
        int dx = x - m->bumper[i].x, dy = y - m->bumper[i].y;
        if (dx * dx + dy * dy < best) { best = dx * dx + dy * dy; event = fb_bumper(i); }
    }
    return event;
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
    for (int i = 0; i < M.ntimers; i++) watch(M.timer[i], W_TIMER);
    for (int i = 0; i < M.ntargets; i++) watch(M.target[i].at, W_TARGET);
    if (M.lanes) watch(M.lanes, W_LANES);
    for (int i = 0; i < M.nbanksets; i++)
        for (int j = 0; j < M.bankset[i].n; j++) watch((uint16_t)(M.bankset[i].at + j), W_BANK);
    if (M.ramps) watch(M.ramps, W_RAMPS);
    kicking = launched = release = started = known = idle = ready = multi = held = sw_on = 0;
    kick_t = sw_t = -1e18;
    for (int i = 0; i < WATCH_ZONES; i++) sw_last[i] = -1e18;
    for (int i = 0; i < WATCH_TIMERS; i++) { caught[i] = 0; counted[i] = -1e18; }
    for (int i = 0; i < 8; i++) { ball[i].x = ball[i].y = 0; ball[i].moved = -1e18; }
    lamp_ds = (uint16_t)(load_seg + M.lamp_seg);
    lamp_list = -1;
    memset(lamp_drawn, 0, sizeof lamp_drawn);
    memset(lamp_sent, 0, sizeof lamp_sent);
    memset(lamp_blits, 0, sizeof lamp_blits);
    lamp_muted = lamp_strays = lamp_wrong = 0;
    lamp_check = getenv("EPIC_LAMP_CHECK") != NULL;
    fresh = 1;
    pull_old = 0;
    flipper[0] = flipper[1] = 0;
    last_step = now_ms();
    ss0 = g_cpu.ss; sp0 = g_cpu.sp;                /* as load_exe left them */
    feedback_table(now_ms(), M.name, M.layout, M.features);
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
    if (ready) feedback_event(now_ms(), FB_BALL_READY, 0, (FbDetail){ 0 });
    if (multi) feedback_event(now_ms(), FB_MULTIBALL, 0, (FbDetail){ 0 });
    if (held) feedback_event(now_ms(), FB_BALL_HELD, 0, (FbDetail){ 0 });
    ready = multi = held = 0;
    lamps_off(now_ms());
    if (lamp_check) {                /* which lamps were drawn at all, and what else was */
        char never[WATCH_LAMPS * 4 + 1] = "";
        int drawn = 0;
        for (int i = 0; i < nlamps(); i++) {
            if (i >= M.lamps && i < M.glow_from - 1) continue;   /* between lamps and glows: none */
            if (lamp_blits[i]) drawn++;
            else snprintf(never + strlen(never), sizeof never - strlen(never), " %d", i + 1);
        }
        fprintf(stderr, "[lamps] check %s: %d of %d lamps drawn, never:%s; %u pictures there no lamp's;"
                " %u lamps drawn other than their list says\n",
                M.name, drawn, M.lamps + M.glows, *never ? never : " none", lamp_strays, lamp_wrong);
    }
    feedback_table(now_ms(), NULL, NULL, NULL);
}

const WatchMap *watch_table(uint32_t *ds, uint32_t *cs)
{
    if (!g_watch_on) return NULL;
    *ds = DS; *cs = CS;
    return &M;
}

int watch_place(int kicker, double within_ms, int *x, int *y)
{
    const double t = now_ms(), k = t - kick_t, s = t - sw_t;
    const int use_kick = kicker == 1 ? k <= within_ms : kicker == 0 ? 0 : k <= within_ms && k <= s;
    if (use_kick) { *x = kick_x; *y = kick_y; return 1; }
    if (kicker == 1 || s > within_ms) return 0;
    *x = sw_x; *y = sw_y;
    return 1;
}

int watch_game_over(void)
{
    return g_watch_on ? g_cpu.mem[DS + M.over] == 1 : -1;
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

/* A ball rolling: one that has gone this far from where it last rested, this
 * recently. A ball in a slot is not always in play: table 3 parks one in its
 * kickback from the start, and a ball waiting in the lane settles a pixel or
 * two and then stands still. A ball cradled on a flipper this long during
 * multiball stops counting until it moves again. */
#define ROLLING_MS 5000.0
#define ROLLING_PX 24

/* A ball waiting in the shooter lane, as the plunger sees it; and how many
 * balls are rolling. Sampled, so a ball that rolls back lights it again. */
static void lane_and_balls(void)
{
    const int r = plunger_lane(&M, g_cpu.mem + DS, 1);
    if (r != ready) emit(FB_BALL_READY, r, rd16(DS + M.x), rd16(DS + M.y), 0, 0);
    ready = r;
    const double t = now_ms();
    int n = 0;
    for (int i = 0; i < M.slots && i < 8; i++) {
        const int x = rd16(DS + (uint16_t)(M.x + 2 * i)), y = rd16(DS + (uint16_t)(M.y + 2 * i));
        if (rd16(DS + (uint16_t)(M.active + 2 * i)) != 1 || (!x && !y)) {
            ball[i].x = ball[i].y = 0;                 /* gone, or not placed yet */
            ball[i].moved = -1e18;
            continue;
        }
        if (!ball[i].x && !ball[i].y) {
            ball[i].x = (uint16_t)x; ball[i].y = (uint16_t)y;    /* where it first appears */
        } else if (abs(x - ball[i].x) > ROLLING_PX || abs(y - ball[i].y) > ROLLING_PX) {
            ball[i].x = (uint16_t)x; ball[i].y = (uint16_t)y;
            ball[i].moved = t;
        }
        n += t - ball[i].moved <= ROLLING_MS;
    }
    if ((n >= 2) != multi) emit(FB_MULTIBALL, n >= 2, 0, 0, n, 0);
    multi = n >= 2;
    int h = 0;                       /* a hole holds a ball while its timer counts */
    for (int i = 0; i < M.ntimers; i++) h |= g_cpu.mem[DS + M.timer[i]] != 0 && t - counted[i] <= HOLD_GAP_MS;
    if (h != held) emit(FB_BALL_HELD, h, 0, 0, 0, 0);
    held = h;
}

static void stepped(void)
{
    last_step = now_ms();
    idle = 0;
    sw_on = 0;                       /* the switches of this step are yet to come */
    feedback_mute(g_cpu.mem[DS + M.demo] == 1);
    if (!started) {
        started = 1;                 /* the first ball of a game */
        if (M.lane_x && rd16(DS + M.active) && rd16(DS + M.x) >= M.lane_x) release = 1;
    }
    if (release) {
        release = 0;
        if (g_cpu.mem[DS + M.over] != 1) {
            if (fresh) emit(FB_GAME_START, 1, 0, 0, 0, 0);
            fresh = 0;
            emit(FB_BALL_RELEASE, 1, rd16(DS + M.x), rd16(DS + M.y), 0, 0);
        }
    }
    flippers();
    lane_and_balls();
}

/* The host switched every output off while the game stood still, with
 * emulated time held, so no standstill shows here: forget what was sent, and
 * the next step sends what is still on. */
void watch_released(void)
{
    flipper[0] = flipper[1] = known = ready = multi = held = 0;
    memset(lamp_sent, 0, sizeof lamp_sent);
}

int watch_lamp_at(const CPU *cpu, int *lit)
{
    /* the renderer's own: not its copy of the last few to the other page
     * first (a call of its own), nor another caller's pictures there */
    const uint32_t sp = (uint32_t)cpu->ss * 16;
    const uint16_t bx = cpu->bx, k = (uint16_t)(bx / 4 + 1);
    if (cpu->ds != lamp_ds || lamp_list < 0 || bx & 1 || bx >= 4u * M.lamps
        || rd16((uint32_t)lamp_ds * 16 + (uint16_t)(M.lamp_table + bx)) != cpu->si
        || rd16(sp + (uint16_t)(cpu->sp + 4)) != cpu->si
        || rd16(sp + (uint16_t)(cpu->sp + 6)) != (uint16_t)(lamp_list + k)) return 0;
    *lit = !(bx & 2);
    return k;
}

int watch_glow_lit(int state, int flag)
{
    if (state == 0xFD) return 0;
    if (state == 0xFE) return 1;
    if (flag == 1 && state < 0x32) return 0;
    return (state < 0x32 ? state : 0x63 - state) >= 0x19;     /* up to 0x31, and down */
}

/* Each frame, the lamps that changed. Kept through a pause between balls, as
 * a table's own lamps are. The demo mutes them; out of it, the lit ones are
 * sent again, since nothing was while it played. */
static void lamps(void)
{
    const int muted = g_cpu.mem[DS + M.demo] == 1;
    if (muted != lamp_muted) {       /* muting puts every output out; out of it, none was sent */
        feedback_mute(muted);
        memset(lamp_sent, 0, sizeof lamp_sent);
    }
    lamp_muted = muted;
    for (int i = 0; i < nlamps(); i++)
        if (lamp_drawn[i] != lamp_sent[i]) {
            emit((FbEvent)(FB_LAMP_1 + i), lamp_drawn[i], 0, 0, i + 1, 0);
            lamp_sent[i] = lamp_drawn[i];
        }
}

void watch_tick(void)
{
    if (!g_watch_on || !sound()) return;
    lamps();
    if (!started || idle) return;
    if (now_ms() - last_step > IDLE_MS) {
        for (int i = 0; i < 2; i++)
            if (flipper[i]) emit((FbEvent)(FB_FLIPPER_LEFT + i), 0, 0, 0, -1, 0);
        if (ready) emit(FB_BALL_READY, 0, 0, 0, -1, 0);
        if (multi) emit(FB_MULTIBALL, 0, 0, 0, -1, 0);
        if (held) emit(FB_BALL_HELD, 0, 0, 0, -1, 0);
        flipper[0] = flipper[1] = known = ready = multi = held = 0;   /* sent again by the next step */
        idle = 1;
        return;
    }
    flippers();
}

/* ---- functions entered ------------------------------------------------------ */

/* Whether the switch handler gets past its own gate with this colour: on the
 * ramps only some colours are handled, and tilted only some others */
static int handled(int colour)
{
    const int on_layer = g_cpu.mem[DS + M.layer] == 1;
    const uint8_t *c = on_layer ? M.on_layer : M.tilt_pass;
    const int n = on_layer ? M.non_layer : M.ntilt_pass;
    for (int i = 0; i < n; i++)
        if (c[i] == colour) return 1;
    return !on_layer && g_cpu.mem[DS + M.tilt] != 1;
}

/* --switch-log, testing */
static void switch_log(const char *fmt, ...)
{
    static FILE *f;
    if (!f && !(f = fopen(g_opt.switch_log, "w"))) { g_opt.switch_log = NULL; return; }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fflush(f);
}

void watch_enter(CPU *cpu, const char *fn)
{
    if (!strcmp(fn, M.lamp_list_fn)) {                       /* the lamps' list, in SI */
        lamp_list = cpu->si;
        return;
    }
    if (*M.glow_fn && !strcmp(fn, M.glow_fn)) {              /* a glowing light: CX which, BL its state */
        const int k = cpu->cx;
        if (k < M.glow_from || k >= M.glow_from + M.glows || !sound()) return;
        const int flag = g_cpu.mem[(uint32_t)lamp_ds * 16 + (uint16_t)((k - M.glow_from) * 4 + 3)];
        lamp_drawn[k - 1] = (uint8_t)watch_glow_lit(cpu->bl, flag);
        if (lamp_check) lamp_blits[k - 1]++;
        return;
    }
    if (!strcmp(fn, M.lamp_fn) && cpu->ds == lamp_ds) {       /* a lamp's picture, in SI */
        int lit, k = watch_lamp_at(cpu, &lit);
        if (k && sound()) lamp_drawn[k - 1] = (uint8_t)lit;
        if (lamp_check && k) {
            /* the lamp's state in the list, just set: 5 and 4 drawn lit, 6 and 3 unlit */
            const uint8_t st = g_cpu.mem[DS + (uint16_t)(lamp_list + k)];
            lamp_blits[k - 1]++;
            if ((lit && st != 5 && st != 4) || (!lit && st != 6 && st != 3)) {
                if (lamp_wrong++ < 8)
                    fprintf(stderr, "[lamps] check %s: lamp %d drawn %s, its state %u in the list at %04X\n",
                            M.name, k, lit ? "lit" : "unlit", st, lamp_list);
            }
        }
        else if (lamp_check && lamp_strays++ < 8)
            fprintf(stderr, "[lamps] check %s: a picture at %04X no lamp's\n", M.name, cpu->si);
        return;
    }
    if (g_opt.switch_log && *M.effect_fn && !strcmp(fn, M.effect_fn) && sound())
        /* an effect the table plays: its slot and pan, the first ball's place */
        switch_log("%.0f %s fx%02X %d %d %d\n", now_ms(), M.name, cpu->al, cpu->ah >> 4,
                   rd16(DS + M.x), rd16(DS + M.y));
    if (!strcmp(fn, M.step_fn)) {
        if (!sound()) return;
        nudge_step(&M, g_cpu.mem + DS);          /* a shove of the cabinet, as this step begins */
        stepped();
    } else if (!strcmp(fn, M.kicker_fn)) {
        if (!sound()) return;
        uint32_t o = (uint32_t)(uint16_t)(cpu->es - rd16(DS + M.map)) * 16 + cpu->bx;
        kicking = o < 320u * 400u;
        kick_x = (int)(o % 320);
        kick_y = (int)(o / 320);
        kick_di = cpu->di;
        if (kicking) kick_t = now_ms();
    } else if (!strcmp(fn, M.switch_fn)) {
        if (!sound()) return;
        sw_on = 0;
        if (M.layer && !handled(cpu->al)) return;          /* the handler turns back: nothing happens */
        sw_on = 1;
        sw_colour = cpu->al;
        sw_x = rd16(DS + M.scan_x);
        sw_y = rd16(DS + M.scan_y);
        sw_t = now_ms();
        const double t = now_ms();
        if (g_opt.switch_log) {                    /* testing: each colour once a gap, with the ball's place */
            static double last[256];
            if (t - last[sw_colour] > SWITCH_GAP_MS)
                switch_log("%.0f %s %02X %d %d %d\n", t, M.name, sw_colour, sw_x, sw_y, g_cpu.mem[DS + M.tilt] == 1);
            last[sw_colour] = t;
        }
        for (int i = 0; i < M.nzones; i++) {
            const WatchZone *z = &M.zone[i];
            if (z->colour != sw_colour || sw_x < z->x0 || sw_x > z->x1 || sw_y < z->y0 || sw_y > z->y1) continue;
            const int fresh_hit = t - sw_last[i] > SWITCH_GAP_MS;
            sw_last[i] = t;
            if (!fresh_hit) continue;
            if (z->kind == WZ_ROLLOVER) emit((FbEvent)(FB_ROLLOVER_1 + z->n - 1), 1, sw_x, sw_y, sw_colour, 0);
            else if (z->kind == WZ_SWITCH) emit((FbEvent)(FB_SWITCH_1 + z->n - 1), 1, sw_x, sw_y, sw_colour, 0);
            else if (z->kind == WZ_KICKBACK) emit(FB_KICKBACK, 1, sw_x, sw_y, sw_colour, 0);
            else if (z->kind == WZ_RAMP && !M.ramps) emit((FbEvent)(FB_RAMP_1 + z->n - 1), 1, sw_x, sw_y, sw_colour, 0);
        }
    } else if ((*M.msg_fn && !strcmp(fn, M.msg_fn)) || (*M.msg_fn2 && !strcmp(fn, M.msg_fn2))) {
        if (!sound()) return;
        for (int i = 0; i < M.nmsgs; i++)
            if (M.msg[i].at == cpu->bx) { emit((FbEvent)M.msg[i].event, 1, 0, 0, cpu->bx, 0); break; }
    }
}

/* The feature of a kind the switch being handled is, by its number; 0: none */
static int switch_is(int kind)
{
    const int z = sw_on ? watch_zone(&M, kind, sw_colour, sw_x, sw_y) : -1;
    return z < 0 ? 0 : M.zone[z].n;
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
        if (fresh && g_cpu.mem[DS + M.over] != 1) {   /* Enigma serves its first ball unseen */
            emit(FB_GAME_START, 1, 0, 0, 0, 0);
            fresh = 0;
        }
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
        if (v == 1 && old != 1) {
            emit(FB_GAME_OVER, 1, 0, 0, 0, 0);
            fresh = 1;
        }
        break;
    case W_TIMER: {                  /* set: a ball caught; down to 0: kicked out */
        int i = 0;
        while (i < M.ntimers && M.timer[i] != off) i++;
        if (i == M.ntimers) break;
        counted[i] = now_ms();
        if (!old && v) caught[i] = switch_is(WZ_HOLE);
        else if (old && !v) {
            if (caught[i]) emit((FbEvent)(FB_HOLE_1 + caught[i] - 1), 1, sw_x, sw_y, caught[i], 0);
            caught[i] = 0;
        }
        break;
    }
    case W_TARGET: {
        int i = 0, all = 1;
        while (i < M.ntargets && M.target[i].at != off) i++;
        if (i == M.ntargets) break;
        for (int j = 0; j < M.ntargets; j++) all &= g_cpu.mem[DS + M.target[j].at] == 1;
        if (v == 1 && old != 1) emit((FbEvent)(FB_TARGET_1 + M.target[i].n - 1), 1, sw_x, sw_y, M.target[i].n, 0);
        else if (!v && all) emit(FB_TARGETS_DOWN, 1, sw_x, sw_y, 0, 0);       /* the bank resets */
        break;
    }
    case W_BANK: {                   /* a bank all down, the first flag cleared: it resets */
        for (int i = 0; i < M.nbanksets; i++) {
            if (off < M.bankset[i].at || off >= M.bankset[i].at + M.bankset[i].n) continue;
            int all = 1;
            for (int j = 0; j < M.bankset[i].n; j++) all &= g_cpu.mem[DS + M.bankset[i].at + j] == 1;
            if (!v && old == 1 && all) emit((FbEvent)M.bankset[i].event, 1, sw_x, sw_y, i, 0);
        }
        break;
    }
    case W_LANES: {                  /* the first light put out with all of them lit */
        int all = 1;
        for (int j = 0; j < M.nlanes; j++) all &= g_cpu.mem[DS + M.lanes + j] == 1;
        if (all && v != 1) emit(FB_LANES_LIT, 1, sw_x, sw_y, v, 0);
        break;
    }
    case W_RAMPS: {                  /* the count goes up: the ramp the switch is on */
        const int n = switch_is(WZ_RAMP);
        if (v == (uint8_t)(old + 1) && n) emit((FbEvent)(FB_RAMP_1 + n - 1), 1, sw_x, sw_y, v, 0);
        break;
    }
    }
}
