/*
 * hud.h - what the remaster draws over the game's picture, never part of it
 * (shots and recordings are composed without it).
 */
#pragma once
#include <stdint.h>

/* The nudge indicator: a ring at the top right of the playfield, a fainter
 * one at the dead zone, and a dot where the accelerometer reads (x, y:
 * -32768..32767; the cabinet moved left is x < 0, towards the player y > 0),
 * with a short fading trail. range is the reading at the ring (more stays on
 * it). px_aspect is a pixel's width over its height as shown, so the ring is
 * round. */
void hud_nudge(uint32_t *f, int w, int h, double px_aspect, int x, int y, int dead, int range);
