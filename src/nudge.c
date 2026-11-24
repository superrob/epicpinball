/*
 * nudge.c - a cabinet's accelerometer shoving the table (nudge.h).
 *
 * The game's nudge (Z, /, Space) adds 23h to the tilt meter and, for the next
 * eight frames, pushes each ball 14h a frame sideways (Z: to the right) and
 * up, while a value the physics step works out for the ball lies within
 * gate_lo..gate_hi (4..2Ah on every table). Its meter drains by one a frame
 * and the table tilts above 50h.
 *
 * Here a shove pushes in proportion instead: at the start of each step the
 * accelerometer's reading, past the dead zone, changes each ball's speed by
 * up to 14h (times strength), against the cabinet's movement, as a ball does
 * when its cabinet is shoved. The same gate as the game's push, as the step
 * before left it; never while tilted or in the demo. The tilt meter fills by
 * 23h for each game-nudge's worth of push, so the game tilts on hard shaking
 * by its own rule. Small pushes add up rather than round away.
 */
#include "nudge.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static NudgeSettings S = { 1500, 1.0, 1.0, 0, 0, 0 };
static int on, rx, ry;
static double carry_x, carry_y, filled;
static double vel_x, vel_y;          /* is_velocity: the velocity the step before saw */
static int vel_known;

static int traced_velocity;

void nudge_settings(const NudgeSettings *s)
{
    S = *s;
    if (traced_velocity) S.is_velocity = 1;
    vel_known = 0;
}

void nudge_trace_velocity(void) { traced_velocity = 1; S.is_velocity = 1; }
const NudgeSettings *nudge_get_settings(void) { return &S; }

void nudge_reading(int is_on, int x, int y)
{
    on = is_on;
    rx = x; ry = y;
    if (!on) { carry_x = carry_y = 0; vel_known = 0; }
}

int nudge_now(int *x, int *y)
{
    *x = S.invert_x ? -rx : rx;
    *y = S.invert_y ? -ry : ry;
    return on;
}

double nudge_push(int reading, int dead, double strength)
{
    const int a = abs(reading);
    if (a <= dead || dead >= 32767) return 0;
    double f = (double)(a - dead) / (32767 - dead);
    if (f > 1) f = 1;
    return (reading < 0 ? -1 : 1) * f * NUDGE_STEP * strength;
}

static uint16_t rd16(const uint8_t *ds, uint16_t a) { return (uint16_t)(ds[a] | ds[(uint16_t)(a + 1)] << 8); }

static void add16(uint8_t *ds, uint16_t a, int d)
{
    int v = (int16_t)rd16(ds, a) + d;
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    ds[a] = (uint8_t)v;
    ds[(uint16_t)(a + 1)] = (uint8_t)((uint16_t)v >> 8);
}

void nudge_step(const WatchMap *m, uint8_t *ds)
{
    if (!on || !m || !ds) return;
    if (ds[m->demo] == 1 || ds[m->tilt] == 1) { carry_x = carry_y = 0; vel_known = 0; return; }
    const int x = S.invert_x ? -rx : rx, y = S.invert_y ? -ry : ry;
    double px = -nudge_push(x, S.dead, S.strength), py = -nudge_push(y, S.dead, S.strength);
    if (S.is_velocity) {                         /* the push is the velocity's change, the whole of it now */
        const double vx = -px * (NUDGE_GAME / NUDGE_STEP), vy = -py * (NUDGE_GAME / NUDGE_STEP);
        px = vel_known ? -(vx - vel_x) : 0;
        py = vel_known ? -(vy - vel_y) : 0;
        vel_x = vx; vel_y = vy; vel_known = 1;
    }
    if (px == 0 && py == 0) return;

    if (S.tilt > 0) {                            /* the meter: by how hard, whatever the gate */
        filled += 0x23 * S.tilt * hypot(px, py) / NUDGE_GAME;
        int add = (int)filled;
        filled -= add;
        int v = ds[m->meter] + add;
        ds[m->meter] = (uint8_t)(v > 0xFF ? 0xFF : v);
    }

    if (m->nudge_gate) {                         /* where the game's own nudge would not push */
        const uint8_t g = ds[m->nudge_gate];
        if (g < m->gate_lo || g > m->gate_hi) {
            trace("[nudge] %.0f ms: shove %d,%d, not here (gate %02X), meter %02X\n",
                  pit_now() * 1000.0 / PIT_HZ, x, y, g, ds[m->meter]);
            return;
        }
    }
    carry_x += px; carry_y += py;
    const int dx = (int)carry_x, dy = (int)carry_y;
    carry_x -= dx; carry_y -= dy;
    if (!dx && !dy) return;
    trace("[nudge] %.0f ms: shove %d,%d: speed %+d,%+d, meter %02X\n", pit_now() * 1000.0 / PIT_HZ, x, y, dx, dy, ds[m->meter]);
    for (int i = 0; i < m->slots; i++) {
        if (rd16(ds, (uint16_t)(m->active + 2 * i)) != 1) continue;
        add16(ds, (uint16_t)(m->vx + 2 * i), dx);
        add16(ds, (uint16_t)(m->vy + 2 * i), dy);
    }
}

/* ---- --nudge-trace ---------------------------------------------------------- */

static struct { double ms; int x, y; } pt[4096];
static int npt, ipt;

int nudge_trace(const char *s)
{
    npt = ipt = 0;
    while (*s) {
        if (npt == (int)(sizeof pt / sizeof pt[0])) return 0;
        char *e;
        double ms = strtod(s, &e);
        if (e == s || *e != ':' || !(ms >= 0) || (npt && ms < pt[npt - 1].ms)) return 0;
        long x = strtol(e + 1, &e, 10);
        if (*e != ',') return 0;
        const char *b = e + 1;
        long y = strtol(b, &e, 10);
        if (e == b || x < -32768 || x > 32767 || y < -32768 || y > 32767) return 0;
        pt[npt].ms = ms; pt[npt].x = (int)x; pt[npt++].y = (int)y;
        s = e;
        if (*s == ',') s++;
        else if (*s) return 0;
    }
    return npt > 0;
}

int nudge_traced(void) { return npt > 0; }

void nudge_trace_at(double ms, int *x, int *y)
{
    if (ipt && pt[ipt].ms > ms) ipt = 0;
    while (ipt + 1 < npt && pt[ipt + 1].ms <= ms) ipt++;
    const int before = ms < pt[0].ms;
    *x = before ? 0 : pt[ipt].x;
    *y = before ? 0 : pt[ipt].y;
}
