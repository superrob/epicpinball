/*
 * feedback.c - table events, to the log and to whoever drives outputs.
 *
 * The log (--feedback-log) has everything, the demo included, a line each:
 *
 *   ms table event on x y a b felt|muted
 *
 * so a scripted run can be checked against what the game did. The sink gets
 * only what a player should feel. An off is never held back.
 */
#include "feedback.h"
#include <stdio.h>
#include <string.h>

static const char *const names[FB_EVENTS] = {
    "flipper_left", "flipper_right", "sling_left", "sling_right",
    "bumper_1", "bumper_2", "bumper_3",
    "launch", "ball_release", "drain", "nudge", "tilt", "knocker",
    "ball_ready", "multiball", "game_start", "game_over",
    "bumper_4", "bumper_5", "bumper_6",
    "hole_1", "hole_2", "hole_3", "hole_4", "hole_5", "ball_held", "kickback",
    "target_1", "target_2", "target_3", "targets_down",
    "rollover_1", "rollover_2", "rollover_3", "rollover_4", "rollover_5", "rollover_6",
    "lanes_lit", "skill_shot", "ramp_1", "ramp_2", "ramp_3",
    "jackpot", "super_jackpot", "award",
    "switch_1", "switch_2", "switch_3", "switch_4", "switch_5", "switch_6", "switch_7", "switch_8",
    "switch_9", "switch_10", "switch_11", "switch_12", "switch_13", "switch_14", "switch_15", "switch_16",
    "switch_17", "switch_18", "switch_19", "switch_20", "switch_21", "switch_22", "switch_23", "switch_24",
    "table_start", "table_end", "table_lost",
};

static FILE *log_file;
static const FbSink *out;
static char table[16];
static int muted;

const char *feedback_name(FbEvent e) { return (unsigned)e < FB_EVENTS ? names[e] : "?"; }

int feedback_by_name(const char *name)
{
    for (int i = 0; i < FB_OUTPUTS; i++) if (!strcmp(name, names[i])) return i;
    return -1;
}

int feedback_log(const char *path)
{
    log_file = fopen(path, "w");
    return log_file != NULL;
}

void feedback_sink(const FbSink *sink) { out = sink; }

void feedback_table(double ms, const char *name, const char *bumpers, const char *features)
{
    muted = 0;
    if (!name && *table) feedback_event(ms, FB_TABLE_END, 1, (FbDetail){ 0, 0, 0, 0 });
    if (out) {
        out->all_off();
        out->table(name, name ? bumpers : NULL, name ? features : NULL);
    }
    snprintf(table, sizeof table, "%s", name ? name : "");
    if (name) feedback_event(ms, FB_TABLE_START, 1, (FbDetail){ 0, 0, 0, 0 });
    if (log_file) fflush(log_file);
}

void feedback_mute(int on)
{
    if (on == muted) return;
    muted = on;
    if (on && out) out->all_off();
}

void feedback_event(double ms, FbEvent e, int on, FbDetail d)
{
    if (log_file)
        fprintf(log_file, "%.2f %s %s %d %d %d %d %d %s\n", ms, *table ? table : "-",
                feedback_name(e), on, d.x, d.y, d.a, d.b, muted ? "muted" : "felt");
    if (out && e < FB_OUTPUTS && (!on || !muted)) out->event(e, on);
}

void feedback_all_off(void)
{
    if (out) out->all_off();
}

void feedback_close(void)
{
    if (out) {
        out->all_off();
        out->table(NULL, NULL, NULL);
        out = NULL;
    }
    if (log_file) { fclose(log_file); log_file = NULL; }
}
