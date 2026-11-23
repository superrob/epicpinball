/*
 * autostart.h - --table N: PINBALL's own menus driven to table N and a
 * one-player game, unseen; when the table is quit, PINBALL is quit by its own
 * way out, so it saves the high scores (docs/table.md).
 */
#pragma once
#include "lifted.h"

extern int g_auto_on;                 /* PINBALL is driven: RECOMP_ENTER calls autostart_enter */

void autostart_wanted(int table);     /* --table N, 1-13: before the game starts */
/* PINBALL is loaded: its menus map (work/feedback/pin.map) is read and
 * checked against it, or the run stops with why */
void autostart_begin(const LiftedImage *img, uint16_t load_seg);
void autostart_enter(CPU *cpu, const char *fn);
void autostart_poll(void);            /* every host frame: keys held long enough are let go */

/* dos_exec: PINBALL runs `name` with this command tail (tail[0] its length).
 * Table N gets one player; anything else while driving stops the run. */
void autostart_exec(const char *name, uint8_t *tail);
void autostart_exec_done(void);       /* a program PINBALL ran has ended: before the watcher lets go */

/* While driving, the player's own keys, pad and mouse are not the game's:
 * 1 starting (Esc cancels the run), 2 leaving (nothing does); 0 the player's */
int  autostart_busy(void);

/* The pure parts, for the self-test */
typedef struct {
    uint32_t size, fnv;
    uint16_t key, cursor, scroll, selected, present, screen_cursor, initials, menu_frames;
    char frame_fn[24], intro_fn[24], menus_fn[24], screen_fn[24], exec_fn[24], end_fn[24];
#define SUB_FNS 4
    int nsub;
    char sub_fn[SUB_FNS][24];         /* the table screen's rules, options */
} MenusMap;
int  autostart_map_parse(const char *text, MenusMap *m, const char **why);
void autostart_begin_map(const MenusMap *m, uint16_t load_seg);
/* Where table `table` (1-13) is in the list: how many installed tables come
 * before it, or -1 when it is not installed. present: a word a table, 1 if so */
int  autostart_list_position(const uint8_t *present, int table);
