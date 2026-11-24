/*
 * plunger.h - a cabinet's plunger sensor, played into the table (plunger.c).
 * No SDL and no clock of its own: tests/selftest.c drives it.
 */
#pragma once
#include "watch.h"

/* The sensor, from pad.c or a trace: 0 at rest to PLUNGER_FULL pulled back */
#define PLUNGER_FULL 1000

typedef struct {
    int have;                        /* 0: none, unplugged, or the window is not in front */
    int pos;
    int speed;                       /* 0 to PLUNGER_FULL, either way; 0 if the board reports none */
} PlungerInput;

/* A sensor's axis as the plunger's position: rest 0, pulled PLUNGER_FULL,
 * held to that range (pad.c, and the settings tool's drawn plunger) */
static inline int plunger_map(int axis, int rest, int pulled)
{
    if (pulled == rest) return 0;
    const long pos = ((long)axis - rest) * PLUNGER_FULL / (pulled - rest);
    return pos < 0 ? 0 : pos > PLUNGER_FULL ? PLUNGER_FULL : (int)pos;
}

typedef struct {
    double t;
    int pos, speed;
} PlungerSample;

typedef struct {
    int state;
    /* the sensor */
    PlungerSample past[256];
    int first, n;
    double sampled;
    int pos, wary;                   /* wary: no pull counts until it has been quiet at rest */
    int bouncing;                    /* wary only since it was let fly */
    double quiet, away;
    /* the table */
    double still, acked, retry, fired;
    int still_y;
    uint16_t wrote;                  /* what pull was last set to */
    int drawn;                       /* the pull the sprite shows */
} Plunger;

void plunger_init(Plunger *p);
int  plunger_lane(const WatchMap *m, const uint8_t *ds, int running);  /* a ball waits in the lane */
/* Each host_frame. m NULL: no table, and nothing is written. running: its
 * physics is stepping (not paused, not at a prompt). */
void plunger_step(Plunger *p, const WatchMap *m, uint8_t *ds, uint8_t *cs, int running,
                  double ms, const PlungerInput *in);
/* Let go without firing: the game is about to stand still, or the sensor is off */
void plunger_cancel(Plunger *p, const WatchMap *m, uint8_t *ds, uint8_t *cs);

/* --plunger-trace "ms:percent,...": straight lines between, "-" is no sensor */
int  plunger_trace(const char *list);          /* 0: cannot read it */
int  plunger_traced(void);
void plunger_trace_at(double ms, PlungerInput *in);
