/*
 * keys.h - keys by name, and the game's actions (controller.ini's [keys] and
 * [buttons], the settings tool, --keys).
 *
 * A key is its PC set-1 scan code, | 0x100 when it comes after E0 (the
 * arrows, right Ctrl and Alt, keypad Enter ...): what the game's keyboard
 * handler sees.
 */
#pragma once

#define KEY_E0 0x100

/* "LSHIFT", "A", "F1", "KP_ENTER" ... (ignoring case), or a scan code in
 * hex ("0x2A", or "2A" as older files have it); 0 when it is none of them */
int key_by_name(const char *s);
/* A key's name: "LSHIFT", or "0x54" when it has none */
const char *key_name(int sc);
const char *key_name_at(int i);      /* the i-th name keys know, NULL past the last */

/* From SDL's scancodes (USB HID usage ids, as SDL2 and SDL3 number them, and
 * VPinball 10.8.1 writes them); 0 when the game has no such key */
int key_from_sdl(int sdl);
/* From DirectInput's DIK_ codes (VPinball 10.8.0): set 1, with E0 as 0x80 */
int key_from_dik(int dik);

/* What a key, a cabinet button or a pad does in the game: the key it sends */
typedef struct { const char *name, *label; int sc; } KeyAction;
enum { KEY_ACTIONS = 14 };
extern const KeyAction k_actions[KEY_ACTIONS];
int key_action_by_name(const char *s);     /* -1 if none */

/* "LCTRL | A": up to n keys into out; how many, or -1 if one is not a key */
int key_list(const char *s, int *out, int n);
