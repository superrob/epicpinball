/*
 * feedback.h - what happens on a table that a cabinet can make felt.
 *
 * A plain event interface: no CPU, no VGA, no SDL. The recomp's watcher
 * (watch.c) is one source of events; any other version of the game can be
 * another and reach the same log and the same helper. Events only go out:
 * nothing here can change the game.
 */
#pragma once
#include <stdint.h>

typedef enum {
    /* In this order in dof/src/events.h, which gives each its DOF name
     * ($FlipperLeft; a table's own feature $t1PowerUpHole): only add at the end */
    FB_FLIPPER_LEFT, FB_FLIPPER_RIGHT,       /* levels: 1 while the flipper is up */
    FB_SLING_LEFT, FB_SLING_RIGHT,           /* pulses, unless said */
    FB_BUMPER_1, FB_BUMPER_2, FB_BUMPER_3,   /* a table's bumpers, left to right */
    FB_LAUNCH, FB_BALL_RELEASE, FB_DRAIN, FB_NUDGE, FB_TILT, FB_KNOCKER,
    FB_BALL_READY,                           /* level: a ball waits in the shooter lane */
    FB_MULTIBALL,                            /* level: two balls or more in play */
    FB_GAME_START, FB_GAME_OVER,
    FB_BUMPER_4, FB_BUMPER_5, FB_BUMPER_6,   /* only table 5 has more than three */
    /* A table's own features, numbered left to right on it (docs/cabinet-feedback.md) */
    FB_HOLE_1, FB_HOLE_2, FB_HOLE_3, FB_HOLE_4, FB_HOLE_5,  /* a hole kicks its ball out */
    FB_BALL_HELD,                            /* level: a hole holds a ball */
    FB_KICKBACK,
    FB_TARGET_1, FB_TARGET_2, FB_TARGET_3,   /* a drop target goes down */
    FB_TARGETS_DOWN,                         /* a bank's last one: it resets */
    FB_ROLLOVER_1, FB_ROLLOVER_2, FB_ROLLOVER_3, FB_ROLLOVER_4, FB_ROLLOVER_5, FB_ROLLOVER_6,
    FB_LANES_LIT,                            /* a set of lanes complete */
    FB_SKILL_SHOT,
    FB_RAMP_1, FB_RAMP_2, FB_RAMP_3,         /* a ramp made */
    FB_JACKPOT, FB_SUPER_JACKPOT, FB_AWARD,
    /* A table's other features, its own names for each (its lanes, loops,
     * locks, a ramp it counts no other way): on a switch, or a scoreboard text */
    FB_SWITCH_1, FB_SWITCH_24 = FB_SWITCH_1 + 23,
    /* levels: a table's lamps, lit as the game draws them, in its own order
     * (tools/feedback_map.py --lamps): $t1Lamp1 */
    FB_LAMP_1, FB_LAMP_96 = FB_LAMP_1 + 95,
    FB_OUTPUTS,                              /* from here: only for the log */
    FB_TABLE_START = FB_OUTPUTS, FB_TABLE_END, FB_TABLE_LOST,
    FB_EVENTS
} FbEvent;

#define FB_BUMPERS 6
#define FB_HOLES 5
#define FB_ROLLOVERS 6
#define FB_RAMPS 3
#define FB_SWITCHES 24
#define FB_LAMPS 96
static inline FbEvent fb_bumper(int i) { return (FbEvent)(i < 3 ? FB_BUMPER_1 + i : FB_BUMPER_4 + i - 3); }

/* What an event carries for the log: where, and what it was made of */
typedef struct { int x, y, a, b; } FbDetail;

/* Who drives outputs (dof_pipe.c). Muted events never reach it. */
typedef struct {
    void (*table)(const char *name, const char *bumpers, const char *features);
                                             /* a table is up, its bumpers' sides ("LCR") and its
                                              * own features' names and sides ("hole1=TestHole:L ...");
                                              * NULL: none is */
    void (*event)(FbEvent e, int on);
    void (*all_off)(void);
} FbSink;

const char *feedback_name(FbEvent e);        /* "flipper_left": the log's and the helper's name */
int  feedback_by_name(const char *name);     /* -1 if none */

int  feedback_log(const char *path);         /* 0: cannot write it */
void feedback_sink(const FbSink *sink);

void feedback_table(double ms, const char *name, const char *bumpers, const char *features);  /* NULL: ended */
void feedback_mute(int muted);               /* the demo: logged, never felt */
void feedback_event(double ms, FbEvent e, int on, FbDetail d);
void feedback_all_off(void);                 /* the game is standing still */
void feedback_close(void);                   /* idempotent */
