/*
 * ssf.h - --ssf: the board's sounds on a cabinet's exciters (docs/ssf.md).
 *
 * 7.1, as vpinball wires a cabinet: front left and right keep the game's own
 * mix (music and everything else), the side pair drives the exciters at the
 * top of the table and the rear pair those at the bottom. Bumpers,
 * slingshots, flippers, drop targets and the like leave the front: as one
 * starts, ssf.c takes its sample, plays it on the exciters where on the
 * table it happened, and sets its voice in the game's sound driver silent.
 * A rolling sound follows each ball. Nothing else of the game is changed.
 */
#pragma once
#include <stdint.h>
#include "lifted.h"

extern int g_ssf_on;                  /* a table is followed: RECOMP_ENTER calls ssf_enter */

void ssf_wanted(void);                /* --ssf: before the game starts */
void ssf_table_begin(void);           /* after watch_table_begin: SSF if its map has it */
void ssf_table_end(void);
void ssf_enter(CPU *cpu, const char *fn);

/* Gains: the exciters' effects, and the rolling (0: none). 1 is as loud as
 * the game's own mix; SSF_GAIN the default, for exciters that need driving. */
#define SSF_GAIN 4.0
void ssf_volumes(double effects, double rolling);
/* A table's board effects, replacing its map's: "0,1,2,B" (hex slots) */
int  ssf_board(const char *table, const char *list);

/* The exciters for `frames` frames of 44.1 kHz, after the front's: four
 * int16 a frame, rear left, rear right, side left, side right (bottom left,
 * bottom right, top left, top right of the table). Silence when not on. */
void ssf_render(int16_t *out, int frames);

/* Where a sound is heard, as vpinball places it: x across the table's 320,
 * y down its 400. Gains for rear left, rear right, side left, side right. */
void ssf_gains(double x, double y, float g[4]);
