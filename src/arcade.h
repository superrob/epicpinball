/*
 * arcade.h - between games in a cabinet (--cabinet --table N, docs/table.md):
 * a high score's initials chosen on the DMD with the flipper buttons, then
 * GAME OVER and the table's three high scores until Start. The pictures and the
 * choosing; src/autostart.c types the initials to PINBALL and starts the
 * next game.
 */
#pragma once
#include <stdint.h>
#include "dmd.h"

typedef struct { char initials[4]; uint32_t score; } ArcadeScore;

/* A new high score, `rank` 1-5. `letters`: the ones offered, in order (A-Z,
 * 0-9, space), those PINBALL can type; a back arrow is added after them. */
void arcade_entry(int rank, uint32_t score, const char *letters);

/* The player: */
enum { ARC_PREV = 1, ARC_NEXT, ARC_TAKE, ARC_BACK, ARC_DONE };
/* the left flipper, the right one, Start or the launch button, Backspace, Esc;
 * or `letter` typed on a keyboard (0 none), taken at once. 1 once the
 * initials are complete: three, or ARC_DONE */
int  arcade_entry_input(int what, int letter);
const char *arcade_initials(void);   /* those chosen, up to 3 */

/* The 128x32 pictures, at emulated time ms (the blinking, the pages) */
void arcade_draw_entry(uint32_t out[DMD_W * DMD_H], double ms);
void arcade_draw_ready(uint32_t out[DMD_W * DMD_H], double ms, const ArcadeScore top[3]);   /* the table's three */

/* The font: 5x7, a column apart, `scale` dots a pixel; unknown characters are blank */
int  arcade_text_w(const char *s, int scale);
void arcade_text(uint32_t out[DMD_W * DMD_H], int x, int y, const char *s, int scale, uint32_t argb);
void arcade_commas(uint32_t n, char out[16]);   /* 4470000 -> "4,470,000" */
