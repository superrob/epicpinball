/*
 * plunger.c - a real plunger's position, played into the table.
 *
 * Each table has the same block in its main loop: while Space or Ctrl is held
 * with the ball in the lane, the pull grows by a step a frame and the sprite
 * is drawn there; let go, a pull that is not 0 launches with that strength.
 * So this writes two values and nothing else, and only on a table whose file
 * matched its map (watch.c), with a plunger configured:
 *
 *   follow   pull = shown - step, Ctrl's flag = 1; the game adds the step and
 *            draws `shown`. Its addition is also the frame clock.
 *   release  pull = the strength, Ctrl's flag = 0, both at once. The game
 *            launches on its next frame, and nothing is written until it has
 *            zeroed the pull itself: it reads it part way through that block.
 *   cancel   pull = 0 and the flag = 0, both at once.
 *
 * Never a pull without the flag but that one release, and never Space, which
 * also nudges. The sprite moves a pixel a frame at most, either way, as the
 * keyboard moves it: further and it leaves copies of itself. The launch has
 * the sensor's own strength, however far the sprite has got. Enigma (EP8) has
 * no pull: a release past a quarter fires its fixed
 * launch. docs/cabinet-feedback.md.
 */
#include "plunger.h"

enum { IDLE, HELD, FIRED };

#define BAND        100              /* at rest, per PLUNGER_FULL */
#define PULL_MIN    150              /* the least that can be let fly */
#define EP8_MIN     250
#define WINDOW_MS   150.0            /* a release is judged on this much past */
#define FLIGHT_MS    90.0            /* from nine tenths of the peak to rest, if let go */
#define FAST        250              /* or this much on the speed axis */
#define QUIET_MS    250.0            /* at rest, before a pull counts again */
#define AWAY_MS     300.0            /* or, after a launch, held out: no bounce does that */
#define GAP_MS      100.0            /* unseen this long, the past says nothing */
#define SETTLE_MS    80.0            /* the ball, still in the lane */
#define NO_FRAME_MS 150.0            /* the game is not drawing the plunger */
#define RETRY_MS    500.0
#define LAUNCH_MS   250.0            /* a release the game did not take */
#define NPAST (int)(sizeof ((Plunger *)0)->past / sizeof(PlungerSample))

static uint16_t get16(const uint8_t *m, uint16_t o) { return (uint16_t)(m[o] | m[(uint16_t)(o + 1)] << 8); }

static void set16(uint8_t *m, uint16_t o, uint16_t v)
{
    m[o] = (uint8_t)v;
    m[(uint16_t)(o + 1)] = (uint8_t)(v >> 8);
}

void plunger_init(Plunger *p)
{
    memset(p, 0, sizeof *p);
    p->state = IDLE;
    p->sampled = -1e9;
    p->quiet = p->away = p->still = -1;
    p->wary = 1;
}

/* ---- the sensor ----------------------------------------------------------- */

static void forget(Plunger *p)
{
    p->first = p->n = 0;
    p->quiet = p->away = -1;
}

/* One sample a millisecond. The strength let go at, or 0. */
static int sense(Plunger *p, double ms, const PlungerInput *in)
{
    if (!in->have) {
        forget(p);
        p->pos = 0;
        p->wary = 1;
        p->bouncing = 0;
        return 0;
    }
    if (ms - p->sampled < 1.0) return 0;
    if (ms - p->sampled > GAP_MS) forget(p);
    p->sampled = ms;
    int pos = in->pos < 0 ? 0 : in->pos > PLUNGER_FULL ? PLUNGER_FULL : in->pos;
    int before = p->n ? p->past[(p->first + p->n - 1) % NPAST].pos : 0, let_go = 0;
    while (p->n && (p->n == NPAST || ms - p->past[p->first].t > WINDOW_MS)) {
        p->first = (p->first + 1) % NPAST;
        p->n--;
    }
    if (p->n && before > BAND && pos <= BAND && !p->wary) {
        int peak = 0, fast = in->speed;
        double high = -1e9;
        for (int i = 0; i < p->n; i++) {
            const PlungerSample *s = &p->past[(p->first + i) % NPAST];
            if (s->pos > peak) peak = s->pos;
            if (s->speed > fast) fast = s->speed;
        }
        for (int i = 0; i < p->n; i++) {
            const PlungerSample *s = &p->past[(p->first + i) % NPAST];
            if (s->pos * 10 >= peak * 9) high = s->t;
        }
        if (peak >= PULL_MIN && (ms - high <= FLIGHT_MS || fast >= FAST)) {
            let_go = peak;
            p->wary = p->bouncing = 1;             /* it bounces */
            p->away = -1;
            p->first = p->n = 0;
        }
    }
    p->past[(p->first + p->n) % NPAST] = (PlungerSample){ ms, pos, in->speed };
    p->n++;
    if (pos > BAND) {
        p->quiet = -1;
        if (p->away < 0) p->away = ms;
        if (p->bouncing && ms - p->away >= AWAY_MS) p->wary = p->bouncing = 0;
    } else {
        p->away = -1;
        if (p->quiet < 0) p->quiet = ms;
        if (ms - p->quiet >= QUIET_MS) p->wary = p->bouncing = 0;
    }
    p->pos = p->wary || pos <= BAND ? 0 : pos;
    return let_go;
}

/* ---- the table ------------------------------------------------------------ */

/* A pull the keyboard can make */
static uint16_t strength(const WatchMap *m, int pos)
{
    int step = m->pull_inc, v = (pos * m->pull_max + step * PLUNGER_FULL / 2) / (step * PLUNGER_FULL) * step;
    return (uint16_t)(v < step ? step : v > m->pull_max ? m->pull_max : v);
}

/* The states in which the game runs its plunger block, less those it must not see a pull in */
int plunger_lane(const WatchMap *m, const uint8_t *ds, int running)
{
    if (!running || ds[m->demo] == 1 || ds[m->over] == 1 || ds[m->count]) return 0;
    if (m->lane_off && ds[m->lane_off]) return 0; /* not every table has it */
    if (!m->pull_inc) {                            /* Enigma: while no ball is up */
        for (int i = 0; i < 3 && i < m->slots; i++)
            if (get16(ds, (uint16_t)(m->active + 2 * i)) == 1) return 0;
        return 1;
    }
    return get16(ds, m->active) && get16(ds, m->x) >= m->lane_x && get16(ds, m->y) >= m->lane_y;
}

static void hold(Plunger *p, const WatchMap *m, uint8_t *ds, uint8_t *cs, uint16_t pull)
{
    set16(ds, m->pull, pull);
    cs[m->ctrl] = 1;
    p->wrote = pull;
}

static void fire(Plunger *p, const WatchMap *m, uint8_t *ds, uint8_t *cs, uint16_t pull, double ms)
{
    set16(ds, m->pull, pull);
    cs[m->ctrl] = 0;
    p->fired = ms;
    p->state = FIRED;
}

/* A pixel from what is drawn, towards `want`. First at rest, as the keyboard
 * starts: the table's own art is under it until then. */
static void follow(Plunger *p, const WatchMap *m, uint8_t *ds, uint8_t *cs, int want)
{
    const int px = 1 << m->pull_shift;
    int show = p->drawn ? want : m->pull_inc;
    if (show > p->drawn + px) show = p->drawn + px;
    if (show < p->drawn - px) show = p->drawn - px;
    if (show < m->pull_inc) show = m->pull_inc;
    if (show > m->pull_max) show = m->pull_max;
    hold(p, m, ds, cs, (uint16_t)(show - m->pull_inc));
}

static void let_go_quietly(Plunger *p, const WatchMap *m, uint8_t *ds, uint8_t *cs)
{
    set16(ds, m->pull, 0);
    cs[m->ctrl] = 0;
    p->state = IDLE;
}

void plunger_cancel(Plunger *p, const WatchMap *m, uint8_t *ds, uint8_t *cs)
{
    forget(p);
    p->pos = 0;
    p->wary = 1;
    p->bouncing = 0;
    if (!m) p->drawn = 0;
    else if (p->state == HELD || (p->state == FIRED && get16(ds, m->pull) && cs[m->space] != 1))
        let_go_quietly(p, m, ds, cs);
    p->state = IDLE;
    p->still = -1;
}

void plunger_step(Plunger *p, const WatchMap *m, uint8_t *ds, uint8_t *cs, int running,
                  double ms, const PlungerInput *in)
{
    const int released = sense(p, ms, in);
    if (!m) {                                      /* gone, its memory with it */
        p->state = IDLE;
        p->drawn = 0;
        p->still = -1;
        p->retry = 0;
        return;
    }
    const int fixed = !m->pull_inc;
    const int lane = plunger_lane(m, ds, running);
    const uint16_t pull = get16(ds, m->pull);

    if (p->state == FIRED) {
        if (!pull) {                               /* launched */
            p->state = IDLE;
            p->drawn = 0;
            p->still = -1;
        } else if (cs[m->space] == 1) {
            p->state = IDLE;                       /* the keyboard's pull now */
        } else if (!lane || ms - p->fired > LAUNCH_MS) {
            let_go_quietly(p, m, ds, cs);
        }
        return;
    }

    const int want = p->pos && !fixed ? strength(m, p->pos) : 0;

    if (p->state == HELD) {
        if (pull != p->wrote) {                    /* a frame took it */
            p->drawn = pull;
            p->acked = ms;
        }
        if (!lane || ms - p->acked > NO_FRAME_MS) {
            let_go_quietly(p, m, ds, cs);
            p->retry = ms + (lane ? RETRY_MS : 0);
        } else if (released) {
            fire(p, m, ds, cs, strength(m, released), ms);
        } else if (!want && !(p->drawn >> m->pull_shift)) {
            let_go_quietly(p, m, ds, cs);
            p->drawn = 0;
        } else {
            follow(p, m, ds, cs, want);
        }
        return;
    }

    if (!lane) {
        p->still = -1;
        return;
    }
    const int y = fixed ? 0 : get16(ds, m->y);
    if (p->still < 0 || y < p->still_y - 1 || y > p->still_y + 1) {
        p->still = ms;
        p->still_y = y;
    }
    if (pull || cs[m->ctrl] == 1 || cs[m->space] == 1) {   /* the keyboard has it, and draws it */
        p->drawn = 0;
        return;
    }
    if (ms - p->still < SETTLE_MS) return;
    if (released) {
        if (!fixed) fire(p, m, ds, cs, strength(m, released), ms);
        else if (released >= EP8_MIN) fire(p, m, ds, cs, m->pull_max, ms);
    } else if (!fixed && ms >= p->retry && (want || p->drawn >> m->pull_shift)) {
        p->state = HELD;
        p->acked = ms;
        follow(p, m, ds, cs, want);
    }
}

/* ---- --plunger-trace ------------------------------------------------------ */

static struct { double ms, pos; } trace_pt[4096];     /* pos < 0: no sensor */
static int trace_n, trace_i;

int plunger_trace(const char *s)
{
    trace_n = trace_i = 0;
    while (*s) {
        if (trace_n == (int)(sizeof trace_pt / sizeof trace_pt[0])) return 0;
        char *e;
        double ms = strtod(s, &e), pos = -1;
        if (e == s || *e != ':' || !(ms >= 0) || (trace_n && ms < trace_pt[trace_n - 1].ms)) return 0;
        s = e + 1;
        if (*s == '-') s++;
        else {
            pos = strtod(s, &e);
            if (e == s || !(pos >= 0 && pos <= 100)) return 0;
            s = e;
        }
        if (*s == ',') s++;
        else if (*s) return 0;
        trace_pt[trace_n].ms = ms;
        trace_pt[trace_n++].pos = pos;
    }
    return trace_n > 0;
}

int plunger_traced(void) { return trace_n > 0; }

void plunger_trace_at(double ms, PlungerInput *in)
{
    if (trace_i && trace_pt[trace_i].ms > ms) trace_i = 0;
    while (trace_i + 1 < trace_n && trace_pt[trace_i + 1].ms <= ms) trace_i++;
    double pos = ms < trace_pt[0].ms ? 0 : trace_pt[trace_i].pos;
    if (pos >= 0 && ms >= trace_pt[0].ms && trace_i + 1 < trace_n && trace_pt[trace_i + 1].pos >= 0)
        pos += (trace_pt[trace_i + 1].pos - pos) * (ms - trace_pt[trace_i].ms)
               / (trace_pt[trace_i + 1].ms - trace_pt[trace_i].ms);
    in->have = pos >= 0;
    in->pos = in->have ? (int)(pos * PLUNGER_FULL / 100 + 0.5) : 0;
    in->speed = 0;
}
