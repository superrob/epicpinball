/*
 * watch.h - the recomp's eyes on a running table (watch.c).
 */
#pragma once
#include "machine.h"

#define WATCH_BUMPERS 6              /* E105-E107, E118-E120 */
#define WATCH_ZONES 24
#define WATCH_TIMERS 8
#define WATCH_TARGETS 3
#define WATCH_MSGS 64

/* A table's own features (FEATURES in tools/feedback_map.py) */
enum { WZ_HOLE, WZ_KICKBACK, WZ_ROLLOVER, WZ_RAMP, WZ_KINDS };
typedef struct { int kind, n, colour, x0, y0, x1, y1; char side; } WatchZone;  /* the scanned ball's x, y */
typedef struct { uint16_t at; int n; char side; } WatchTarget;
typedef struct { uint16_t at; int event; } WatchMsg;

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
    uint16_t scan_x, scan_y;         /* the ball the switch handler is looking at */
    char step_fn[24], kicker_fn[24], switch_fn[24], msg_fn[24];
    /* SSF (ssf.c): the table's effect function, the sound driver's voice
     * setup, volume, stop and stop of a class, and the board's effects */
    char effect_fn[24], sfx_setup_fn[24], sfx_volume_fn[24], sfx_stop_fn[24], sfx_stop_class_fn[24];
    uint32_t board;                  /* a bit for each effect slot that is the board's */
    uint16_t sfx_on;                 /* the game's effects switch (SOUND FX OFF: 0); 0: not known */
    int nbumpers;
    struct { int x, y, slot; } bumper[WATCH_BUMPERS];        /* left to right; slot: its side, 0-2 */
    char layout[WATCH_BUMPERS + 1];                         /* the sides, "LCR" */
    /* the table's own: switch colours where the ball is, and stores */
    int nzones, ntimers, ntargets, nmsgs, nlanes;
    WatchZone zone[WATCH_ZONES];
    uint16_t timer[WATCH_TIMERS];                           /* holes' hold timers */
    WatchTarget target[WATCH_TARGETS];                      /* drop target flags */
    uint16_t lanes, ramps;                                  /* lane lights, ramp counter; 0: none */
    uint16_t layer;                  /* 1 while the ball is on the ramps; 0: no gate known */
    int non_layer, ntilt_pass;
    uint8_t on_layer[8], tilt_pass[8];                      /* the colours handled there, and tilted */
    WatchMsg msg[WATCH_MSGS];        /* texts the message function shows */
    char sides[96];                  /* for made rows: "hole:LLCRR ramp:LRR kickback:L target:LLL" */
} WatchMap;

int  watch_map_parse(const char *text, WatchMap *m, const char **why);
int  watch_kick_event(const WatchMap *m, int x, int y);      /* which FbEvent a contact there is */
int  watch_zone(const WatchMap *m, int kind, int colour, int x, int y);   /* its index, or -1 */

void watch_wanted(void);             /* something needs tables watched: before the game starts */
void watch_table_begin(const LiftedImage *img, uint16_t load_seg);
void watch_table_end(void);
int  watch_map_use(const WatchMap *m, uint16_t load_seg);    /* the second half of begin: tests/selftest.c */
void watch_tick(void);               /* each host_frame */
void watch_enter(CPU *cpu, const char *fn);   /* RECOMP_ENTER, through seam_enter (dmd.c) */
void watch_released(void);           /* the host let everything go (a dialog): send levels again */
int  watch_running(void);            /* the table's physics is stepping */
void watch_write_byte(uint32_t a, uint8_t v);                /* vga.c's write hook */
const WatchMap *watch_table(uint32_t *ds, uint32_t *cs);     /* the live table, or NULL */
/* Where the last kicker hit (kicker 1) or switch (0) was, if that recent; or
 * the more recent of the two (-1) */
int  watch_place(int kicker, double within_ms, int *x, int *y);

extern uint32_t g_watch_ds;          /* linear */
extern uint8_t g_watched[0x10000];   /* which bytes of that segment */
