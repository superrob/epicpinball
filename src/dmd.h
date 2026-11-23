/*
 * dmd.h - a 128x32 DMD picture of the running table (docs/dmd.md).
 *
 * Every table has one message engine: a function that turns a text into a
 * list of dots, appenders that add lines to it, and an effect that moves the
 * dots each frame. dmd.c follows those calls through RECOMP_ENTER, lays the
 * text out again for 128x32 in the table's own fonts, and moves each of its
 * dots as far as the game moved its own. Between messages it shows the
 * table's scoreboard, rearranged. It reads the game and puts back what it
 * swaps for a picture; it never changes what the game does.
 *
 * The addresses come from work/dmd/epN.map (tools/dmd_survey.py).
 */
#pragma once
#include <stdint.h>
#include "lifted.h"

#define DMD_W 128
#define DMD_H 32

extern int g_dmd_on;                  /* a table is followed: RECOMP_ENTER calls dmd_enter */

void dmd_wanted(void);                /* --dmd: before the game starts */
int  dmd_active(void);                /* wanted, and a table with a map is up */
void dmd_table_begin(const LiftedImage *img, uint16_t load_seg);
void dmd_table_end(void);
int  dmd_map_use(char *text, uint16_t load_seg, const char **why);   /* a map's text, unchecked: for tests */
void dmd_enter(CPU *cpu, const char *fn);

/* The picture now, from the scoreboard as vga_compose_score gives it.
 * 1 when it differs from the last one. Black when no table is followed. */
int  dmd_frame(const uint32_t *bar, int bw, int bh, uint32_t out[DMD_W * DMD_H]);

/* Tables 1-8 plot their messages on the playfield. While the playfield is
 * composed for the screen, dmd_hide() puts back what the game saved from
 * under each message dot (or only the hidden messages' dots), and
 * dmd_hide(0) puts the dots back. */
enum { DMD_HIDE_ALL = 1, DMD_HIDE_LISTED = 2 };
void dmd_hide(int how);               /* DMD_HIDE_ALL, DMD_HIDE_LISTED (only hidden messages), or 0: back */

/* Messages a cabinet does not show (keyboard help, key prompts): a list of
 * first lines, '|' between them; NULL or "" hides nothing. dmd_bar_hide
 * takes them out of a picture of the 9-13 bar (vga_compose_score's). */
void dmd_set_hidden(const char *list);
void dmd_bar_hide(uint32_t *bar, int bw, int bh);

/* The picture as a DMD shows it: round dots, unlit ones faintly; `scale`
 * pixels a dot. out is DMD_W*scale x DMD_H*scale. */
void dmd_dots(const uint32_t in[DMD_W * DMD_H], uint32_t *out, int scale);
