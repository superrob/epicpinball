/*
 * sim.c - src/nudge.c on a pretend table for the settings tool (sim.h).
 */
#include "sim.h"
#include "nudge.h"
#include "plunger.h"
#include <math.h>
#include <stdarg.h>

/* what nudge.c asks of the machine */
void trace(const char *fmt, ...) { (void)fmt; }
static uint64_t steps;
uint64_t pit_now(void) { return (uint64_t)(steps * (PIT_HZ / 200)); }

static uint8_t ds[0x200];
static WatchMap m;
static int ready, tilted_for;
static double bx, by;

enum { ACTIVE = 0x100, VX = 0x110, VY = 0x120, TILT = 0x130, METER = 0x131, DEMO = 0x132, GATE = 0x140 };

static void table(void)
{
    if (ready) return;
    ready = 1;
    m.active = ACTIVE; m.vx = VX; m.vy = VY; m.slots = 1;
    m.tilt = TILT; m.meter = METER; m.demo = DEMO;
    m.nudge_gate = GATE; m.gate_lo = 4; m.gate_hi = 0x2A;
    ds[ACTIVE] = 1;
    ds[GATE] = 0x10;
}

void sim_nudge_settings(int dead, double strength, double tilt, int invert_x, int invert_y, int is_velocity)
{
    NudgeSettings s = { dead, strength, tilt, invert_x, invert_y, is_velocity };
    nudge_settings(&s);
}

static int16_t rd(int a) { return (int16_t)(ds[a] | ds[a + 1] << 8); }

void sim_nudge_step(int on, int x, int y)
{
    table();
    steps++;
    ds[VX] = ds[VX + 1] = ds[VY] = ds[VY + 1] = 0;
    nudge_reading(on, x, y);
    nudge_step(&m, ds);
    bx = bx * 0.97 + rd(VX);                          /* the ball's speed from shoves, fading */
    by = by * 0.97 + rd(VY);
    if (steps % 3 == 0 && ds[METER]) ds[METER]--;     /* the game drains a frame at a time */
    if (ds[METER] > 0x50 && !ds[TILT]) { ds[TILT] = 1; tilted_for = 400; }
    if (ds[TILT] && --tilted_for <= 0) { ds[TILT] = 0; ds[METER] = 0; }
}

int sim_meter(void) { return ds[METER]; }
int sim_tilted(void) { return ds[TILT]; }
void sim_ball(double *x, double *y) { *x = bx; *y = by; }
double sim_nudge_push(int reading, int dead, double strength) { return nudge_push(reading, dead, strength); }
int sim_plunger(int axis, int rest, int pulled) { return plunger_map(axis, rest, pulled); }
