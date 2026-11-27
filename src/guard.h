/*
 * guard.h - a fault of the game itself, kept from happening: a ball pushed
 * off the left edge (x = -1) put back before it is drawn (guard.c).
 */
#pragma once
#include "machine.h"
#include "watch.h"

extern int g_guard_on;                /* a table is guarded: RECOMP_ENTER calls guard_enter */
void guard_table_begin(const LiftedImage *img, uint16_t load_seg);
void guard_table_end(void);
int  guard_use(const WatchMap *m, uint16_t load_seg);   /* the second half of begin: tests/selftest.c */
void guard_enter(CPU *cpu, const char *fn);
