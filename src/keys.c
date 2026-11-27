/*
 * keys.c - keys by name, from SDL and DirectInput, and the game's actions (keys.h).
 */
#include "keys.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct { const char *n; int sc; } k_names[] = {
    {"ESC",0x01},{"1",0x02},{"2",0x03},{"3",0x04},{"4",0x05},{"5",0x06},{"6",0x07},{"7",0x08},{"8",0x09},{"9",0x0A},
    {"0",0x0B},{"MINUS",0x0C},{"EQUALS",0x0D},{"BKSP",0x0E},{"TAB",0x0F},
    {"Q",0x10},{"W",0x11},{"E",0x12},{"R",0x13},{"T",0x14},{"Y",0x15},{"U",0x16},{"I",0x17},{"O",0x18},{"P",0x19},
    {"LBRACKET",0x1A},{"RBRACKET",0x1B},{"ENTER",0x1C},{"LCTRL",0x1D},
    {"A",0x1E},{"S",0x1F},{"D",0x20},{"F",0x21},{"G",0x22},{"H",0x23},{"J",0x24},{"K",0x25},{"L",0x26},
    {"SEMICOLON",0x27},{"APOSTROPHE",0x28},{"GRAVE",0x29},{"LSHIFT",0x2A},{"BACKSLASH",0x2B},
    {"Z",0x2C},{"X",0x2D},{"C",0x2E},{"V",0x2F},{"B",0x30},{"N",0x31},{"M",0x32},
    {"COMMA",0x33},{"PERIOD",0x34},{"SLASH",0x35},{"RSHIFT",0x36},{"KP_MULTIPLY",0x37},{"LALT",0x38},
    {"SPACE",0x39},{"CAPSLOCK",0x3A},
    {"F1",0x3B},{"F2",0x3C},{"F3",0x3D},{"F4",0x3E},{"F5",0x3F},{"F6",0x40},{"F7",0x41},{"F8",0x42},{"F9",0x43},
    {"F10",0x44},{"NUMLOCK",0x45},{"SCROLLLOCK",0x46},
    {"KP_7",0x47},{"KP_8",0x48},{"KP_9",0x49},{"KP_MINUS",0x4A},{"KP_4",0x4B},{"KP_5",0x4C},{"KP_6",0x4D},
    {"KP_PLUS",0x4E},{"KP_1",0x4F},{"KP_2",0x50},{"KP_3",0x51},{"KP_0",0x52},{"KP_PERIOD",0x53},
    {"NONUS_BACKSLASH",0x56},{"F11",0x57},{"F12",0x58},
    {"KP_ENTER",KEY_E0|0x1C},{"RCTRL",KEY_E0|0x1D},{"KP_DIVIDE",KEY_E0|0x35},{"RALT",KEY_E0|0x38},
    {"HOME",KEY_E0|0x47},{"UP",KEY_E0|0x48},{"PAGEUP",KEY_E0|0x49},{"LEFT",KEY_E0|0x4B},{"RIGHT",KEY_E0|0x4D},
    {"END",KEY_E0|0x4F},{"DOWN",KEY_E0|0x50},{"PAGEDOWN",KEY_E0|0x51},{"INSERT",KEY_E0|0x52},{"DELETE",KEY_E0|0x53},
    {"LWIN",KEY_E0|0x5B},{"RWIN",KEY_E0|0x5C},{"MENU",KEY_E0|0x5D},
    /* other names for the same keys; the first of each above is the one written */
    {"CTRL",0x1D},{"ALT",0x38},{"SHIFT",0x2A},{"ESCAPE",0x01},{"RETURN",0x1C},{"BACKSPACE",0x0E},
};
#define NNAMES (int)(sizeof k_names / sizeof k_names[0])

static int same(const char *a, const char *b)
{
    while (*a && *b && toupper((unsigned char)*a) == toupper((unsigned char)*b)) a++, b++;
    return !*a && !*b;
}

int key_by_name(const char *s)
{
    for (int i = 0; i < NNAMES; i++) if (same(s, k_names[i].n)) return k_names[i].sc;
    char *e;
    long v = strtol(s, &e, 16);                     /* a scan code in hex ("0x" or not), | 100 for E0 */
    return *s && !*e && v > 0 && v < 0x200 ? (int)v : 0;
}

const char *key_name_at(int i) { return i >= 0 && i < NNAMES ? k_names[i].n : NULL; }

const char *key_name(int sc)
{
    static char hex[8];
    for (int i = 0; i < NNAMES; i++) if (k_names[i].sc == sc) return k_names[i].n;
    snprintf(hex, sizeof hex, "0x%X", sc);          /* "F1" would be the key */
    return hex;
}

int key_from_sdl(int s)
{
    static const unsigned char t[101] = {
        [4]  = 0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,   /* A-M */
               0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C,   /* N-Z */
        [30] = 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,                     /* 1-0 */
        [40] = 0x1C, 0x01, 0x0E, 0x0F, 0x39, 0x0C, 0x0D, 0x1A, 0x1B, 0x2B, 0x2B, 0x27, 0x28,   /* Enter... */
               0x29, 0x33, 0x34, 0x35, 0x3A,                                                   /* ...Caps */
        [58] = 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x57, 0x58,         /* F1-F12 */
        [71] = 0x46, 0, 0x52, 0x47, 0x49, 0x53, 0x4F, 0x51, 0x4D, 0x4B, 0x50, 0x48,           /* ScrLk; Ins-Up */
        [83] = 0x45, 0x35, 0x37, 0x4A, 0x4E, 0x1C, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D,         /* keypad */
               0x47, 0x48, 0x49, 0x52, 0x53, 0x56,
    };
    switch (s) {
    case 101: return KEY_E0 | 0x5D;                 /* Application (menu) */
    case 224: return 0x1D;                          /* LCtrl, LShift, LAlt, LGUI */
    case 225: return 0x2A;
    case 226: return 0x38;
    case 227: return KEY_E0 | 0x5B;
    case 228: return KEY_E0 | 0x1D;                 /* RCtrl, RShift, RAlt, RGUI */
    case 229: return 0x36;
    case 230: return KEY_E0 | 0x38;
    case 231: return KEY_E0 | 0x5C;
    }
    if (s < 0 || s > 100 || !t[s]) return 0;
    return t[s] | ((s >= 73 && s <= 82) || s == 84 || s == 88 ? KEY_E0 : 0);   /* Ins..Up, KP /, KP Enter */
}

int key_from_dik(int dik)
{
    if (dik <= 0 || dik > 0xFF) return 0;
    return dik < 0x80 ? dik : KEY_E0 | (dik & 0x7F);
}

const KeyAction k_actions[KEY_ACTIONS] = {
    { "left_flipper",  "Left flipper",                0x2A },
    { "right_flipper", "Right flipper",               0x36 },
    { "launch",        "Launch (the plunger)",        0x39 },
    { "nudge_left",    "Nudge from the left",         0x2C },
    { "nudge_right",   "Nudge from the right",        0x35 },
    { "nudge_up",      "Nudge from the front",        0x39 },
    { "start",         "Start / scoreboard (Enter)",  0x1C },
    { "exit",          "Back / quit (Esc)",           0x01 },
    { "up",            "Up",                          KEY_E0 | 0x48 },
    { "down",          "Down",                        KEY_E0 | 0x50 },
    { "left",          "Left",                        KEY_E0 | 0x4B },
    { "right",         "Right",                       KEY_E0 | 0x4D },
    { "yes",           "Yes (Y)",                     0x15 },
    { "no",            "No (N)",                      0x31 },
};

int key_action_by_name(const char *s)
{
    for (int i = 0; i < KEY_ACTIONS; i++) if (same(s, k_actions[i].name)) return i;
    return -1;
}

int key_list(const char *s, int *out, int n)
{
    int got = 0;
    while (*s) {
        char one[32];
        int len = 0;
        while (*s == ' ' || *s == '|' || *s == ',' || *s == '\t') s++;
        while (*s && *s != '|' && *s != ',' && *s != ' ' && *s != '\t' && len < 31) one[len++] = *s++;
        one[len] = 0;
        if (!len) break;
        if (same(one, "none")) continue;
        int sc = key_by_name(one);
        if (!sc) return -1;
        if (got < n) out[got++] = sc;
    }
    return got;
}
