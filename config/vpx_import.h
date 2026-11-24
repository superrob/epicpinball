/*
 * vpx_import.h - what VPinball's settings (VPinballX.ini) say about the
 * cabinet's keys, buttons, plunger, nudging and sound, in the game's terms,
 * for the settings tool to show and, when told to, write (docs/config.md).
 *
 * Three formats are read, as VPinball has written them:
 *   10.8.0      [Player]: keys as DirectInput codes, buttons from 1, axes 1=X..6=RZ
 *   10.8.1      [Input] Mapping.<action> = dev;id | ..., keys as SDL scancodes,
 *               joysticks SDLJoy_<GUID>_<n> with buttons from 0, axes 512+n, and
 *               sensors dev;axis;P|V|A;deadzone;scale;limit
 *   nightlies   between them: Mapping.PlungerPos, NudgeX1 ... and NudgeOrientation0
 * An empty value is VPinball's default, which is what its keys then do.
 */
#pragma once
#include "keys.h"

#define VPX_PER 4

typedef struct {
    int format;                              /* 0 not read, 1 = 10.8.0, 2 = 10.8.1 */
    /* per action (keys.h): keys (set 1 | KEY_E0) and buttons (from 0) */
    int key[KEY_ACTIONS][VPX_PER], nkey[KEY_ACTIONS];
    int button[KEY_ACTIONS][VPX_PER], nbutton[KEY_ACTIONS];
    char button_guid[40];                    /* the buttons' joystick (SDL GUID); "" when VPinball does not say */
    /* the plunger: an axis from 0 (SDL's order), -1 none */
    int plunger_axis, plunger_speed_axis, plunger_reversed;
    char plunger_guid[40];
    /* nudging */
    int nudge_x, nudge_y, invert_x, invert_y, is_velocity, dead;   /* dead: -1 unknown */
    char nudge_guid[40];
    /* sound: SSF from Sound3D 4 or 5; the playfield volume 0-100, -1 unknown */
    int ssf, sound_volume;
    char notes[2048];                        /* what was there but is not imported, a line each */
} VpxImport;

/* The newest VPinballX.ini where VPinball keeps it, into path; 0 if none */
int vpx_find(char *path, int n);
/* Reads it; 0 if it is not a VPinball settings file */
int vpx_read(const char *path, VpxImport *out);
