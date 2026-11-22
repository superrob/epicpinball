/*
 * watch.h - the recomp's eyes on a running table (watch.c).
 */
#pragma once
#include "machine.h"

#define WATCH_BUMPERS 16

/* One table, from work/feedback/epN.map (tools/feedback_map.py). Offsets are
 * into its data segment, or its code segment for the key flags. */
typedef struct {
    char name[8];
    uint32_t size, fnv;              /* of the EXE it was made from */
    uint16_t ds, cs;                 /* segments, from the load segment */
    uint16_t active, x, y, vx, vy, slots;      /* the balls: word arrays */
    uint16_t flip_l, flip_r, up_l, up_r;
    uint16_t tilt, meter, cooldown, demo, extra, over, count, drain_y;
    uint16_t kick, weak_y, sling_line, map;
    uint16_t pull, pull_inc, pull_max, pull_shift, ctrl, space;
    uint16_t lane_off, lane_x, lane_y, over_pull;
    char step_fn[24], kicker_fn[24];
    int nbumpers;
    struct { int x, y, slot; } bumper[WATCH_BUMPERS];
} WatchMap;

int  watch_map_parse(const char *text, WatchMap *m, const char **why);
int  watch_kick_event(const WatchMap *m, int x, int y);      /* which FbEvent a contact there is */

void watch_wanted(void);             /* something needs tables watched: before the game starts */
void watch_table_begin(const LiftedImage *img, uint16_t load_seg);
void watch_table_end(void);
int  watch_map_use(const WatchMap *m, uint16_t load_seg);    /* the second half of begin: tests/selftest.c */
void watch_tick(void);               /* each host_frame */
int  watch_running(void);            /* the table's physics is stepping */
void watch_write_byte(uint32_t a, uint8_t v);                /* vga.c's write hook */
const WatchMap *watch_table(uint32_t *ds, uint32_t *cs);     /* the live table, or NULL */

extern uint32_t g_watch_ds;          /* linear */
extern uint8_t g_watched[0x10000];   /* which bytes of that segment */
