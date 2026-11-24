/*
 * sim.h - the game's own nudge and plunger rules for the settings tool, in C
 * (sim.c runs src/nudge.c on a pretend table), so the tool's C++ needs none
 * of the machine's headers.
 */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

void sim_nudge_settings(int dead, double strength, double tilt, int invert_x, int invert_y, int is_velocity);
/* One physics step of a pretend table with a ball in play and the gate
 * open, the reading at that moment (on 0: none) */
void sim_nudge_step(int on, int x, int y);
int  sim_meter(void);                 /* the tilt meter, 0-255; it tilts above 0x50 */
int  sim_tilted(void);                /* tilted, for a moment, as the game would */
void sim_ball(double *x, double *y);  /* the ball's speed from shoves, fading: what it feels */
double sim_nudge_push(int reading, int dead, double strength);
int  sim_plunger(int axis, int rest, int pulled);   /* 0-1000, as pad.c reads it */

#ifdef __cplusplus
}
#endif
