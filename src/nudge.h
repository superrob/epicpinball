/*
 * nudge.h - a cabinet's accelerometer shoving the table (docs/cabinet-feedback.md).
 *
 * The game's own nudge is keys: Z, / and Space, and nudge buttons press them
 * (pad.c). A shove of the cabinet, read from its accelerometer, pushes the
 * balls by how hard it was instead: at the start of each physics step the
 * reading of that moment changes every ball's speed, and fills the tilt
 * meter, which then tilts by the game's own rule.
 */
#pragma once
#include "watch.h"

#define NUDGE_STEP 0x14               /* a frame of the game's own nudge, sideways */
#define NUDGE_GAME (24 * NUDGE_STEP)  /* the whole of one (8 frames, about 3 steps each), which fills the meter by 23h */

typedef struct {
    int dead;                         /* readings within this of 0 are the cabinet at rest */
    double strength;                  /* 1: a full reading for a frame pushes as a frame of the game's nudge */
    double tilt;                      /* how fast shoves fill the tilt meter; 0 never */
    int invert_x, invert_y;
} NudgeSettings;

void nudge_settings(const NudgeSettings *s);
/* The accelerometer now, -32768..32767 across and along the cabinet: what
 * the next physics step sees. on 0: no accelerometer (or not in front). */
void nudge_reading(int on, int x, int y);
/* watch.c, at the start of a table's physics step */
void nudge_step(const WatchMap *m, uint8_t *ds);

/* One axis's push on a ball for one step, in the game's speed units, by the
 * cabinet's acceleration: the ball goes the other way */
double nudge_push(int reading, int dead, double strength);

/* --nudge-trace "ms:x,y,...": a scripted accelerometer, held between points */
int  nudge_trace(const char *list);    /* 0: cannot read it */
int  nudge_traced(void);
void nudge_trace_at(double ms, int *x, int *y);
