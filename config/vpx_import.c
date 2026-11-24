/*
 * vpx_import.c - VPinball's settings in the game's terms (vpx_import.h).
 *
 * The formats are VPinball's own, read from its source: InputManager.cpp,
 * InputAction.cpp and PhysicsSensor.cpp (10.8.1), pininput.cpp and
 * KeysConfigDialog.cpp (10.8.0).
 */
#include "vpx_import.h"
#include "ini.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#define strtok_r strtok_s
#else
#include <dirent.h>
#endif

static void note(VpxImport *o, const char *fmt, ...)
{
    size_t n = strlen(o->notes);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(o->notes + n, sizeof o->notes - n, fmt, ap);
    va_end(ap);
    n = strlen(o->notes);
    if (n + 1 < sizeof o->notes) { o->notes[n] = '\n'; o->notes[n + 1] = 0; }
}

static void add(int *list, int *n, int v)
{
    for (int i = 0; i < *n; i++) if (list[i] == v) return;
    if (*n < VPX_PER) list[(*n)++] = v;
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    for (char *e = s + strlen(s); e > s && (e[-1] == ' ' || e[-1] == '\t'); ) *--e = 0;
    return s;
}

/* ---- finding it ------------------------------------------------------------------ */

static int exists(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (f) fclose(f);
    return f != NULL;
}

/* "10.8" over "10.10"? by number, not text */
static int newer(const char *a, const char *b)
{
    int a1 = 0, a2 = 0, b1 = 0, b2 = 0;
    sscanf(a, "%d.%d", &a1, &a2);
    sscanf(b, "%d.%d", &b1, &b2);
    return a1 != b1 ? a1 > b1 : a2 > b2;
}

/* dir/<newest major.minor>/VPinballX.ini, else dir/VPinballX.ini */
static int find_in(const char *dir, char *path, int n)
{
    char best[64] = "";
#ifdef _WIN32
    char pat[600];
    WIN32_FIND_DATAA fd;
    snprintf(pat, sizeof pat, "%s\\*", dir);
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const char *v = fd.cFileName;
            char p[700];
            int a, b;
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || sscanf(v, "%d.%d", &a, &b) != 2) continue;
            snprintf(p, sizeof p, "%s\\%s\\VPinballX.ini", dir, v);
            if (exists(p) && (!*best || newer(v, best))) snprintf(best, sizeof best, "%s", v);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    const char sep = '\\';
#else
    DIR *d = opendir(dir);
    for (struct dirent *e; d && (e = readdir(d)) != NULL; ) {
        char p[700];
        int a, b;
        if (sscanf(e->d_name, "%d.%d", &a, &b) != 2) continue;
        snprintf(p, sizeof p, "%s/%s/VPinballX.ini", dir, e->d_name);
        if (exists(p) && (!*best || newer(e->d_name, best))) snprintf(best, sizeof best, "%s", e->d_name);
    }
    if (d) closedir(d);
    const char sep = '/';
#endif
    if (*best) snprintf(path, n, "%s%c%s%cVPinballX.ini", dir, sep, best, sep);
    else snprintf(path, n, "%s%cVPinballX.ini", dir, sep);
    return exists(path);
}

int vpx_find(char *path, int n)
{
    char dir[600];
#ifdef _WIN32
    const char *app = getenv("APPDATA");
    if (app) {
        snprintf(dir, sizeof dir, "%s\\VPinballX", app);
        if (find_in(dir, path, n)) return 1;
    }
#else
    const char *home = getenv("HOME"), *xdg = getenv("XDG_DATA_HOME");
    if (xdg && *xdg == '/') {
        snprintf(dir, sizeof dir, "%s/VPinballX", xdg);
        if (find_in(dir, path, n)) return 1;
    }
    if (home) {
        snprintf(dir, sizeof dir, "%s/.local/share/VPinballX", home);
        if (find_in(dir, path, n)) return 1;
        snprintf(dir, sizeof dir, "%s/Library/Application Support/VPinballX", home);
        if (find_in(dir, path, n)) return 1;
        snprintf(path, n, "%s/.vpinball/VPinballX.ini", home);     /* 10.8.0 */
        if (exists(path)) return 1;
    }
#endif
    *path = 0;
    return 0;
}

/* ---- 10.8.1: [Input] ----------------------------------------------------------- */

/* VPinball's action, the game's, and its default key (SDL scancode) */
static const struct { const char *vpx; const char *ours; int def; } k_map81[] = {
    { "LeftFlipper", "left_flipper", 225 }, { "RightFlipper", "right_flipper", 229 },
    { "LaunchBall", "launch", 40 }, { "LeftNudge", "nudge_left", 29 }, { "RightNudge", "nudge_right", 56 },
    { "CenterNudge", "nudge_up", 44 }, { "Start", "start", 30 }, { "ExitGame", "exit", 41 },
    { "UIUp", "up", 0 }, { "UIDown", "down", 0 }, { "UILeft", "left", 0 }, { "UIRight", "right", 0 },
};

/* The GUID in SDLJoy_<guid>_<n>, or NULL when dev is not a joystick */
static const char *joy_guid(const char *dev, char *out, int n)
{
    if (strncmp(dev, "SDLJoy_", 7)) return NULL;
    const char *g = dev + 7, *u = strrchr(g, '_');
    snprintf(out, n, "%.*s", (int)(u && u > g ? u - g : (int)strlen(g)), g);
    return out;
}

static void binding81(VpxImport *o, int a, const char *what, const char *value)
{
    char buf[512];
    snprintf(buf, sizeof buf, "%s", value);
    for (char *save = NULL, *b = strtok_r(buf, "|", &save); b; b = strtok_r(NULL, "|", &save)) {
        b = trim(b);
        if (!*b) continue;
        if (strchr(b, '&')) { note(o, "%s: %s is keys held together, which the game has no use for", what, b); continue; }
        char *f[4] = { 0 };
        int nf = 0;
        for (char *s2 = NULL, *t = strtok_r(b, ";", &s2); t && nf < 4; t = strtok_r(NULL, ";", &s2)) f[nf++] = trim(t);
        if (nf < 2) continue;
        const int id = atoi(f[1]);
        char g[40];
        if (nf > 2) { note(o, "%s: an axis used as a button (%s;%s) is not imported", what, f[0], f[1]); continue; }
        if (!strcmp(f[0], "Key")) {
            const int sc = key_from_sdl(id);
            if (!sc) note(o, "%s: key %d is not one the game has", what, id);
            else if (sc != k_actions[a].sc) add(o->key[a], &o->nkey[a], sc);
        } else if (joy_guid(f[0], g, sizeof g)) {
            if (id >= 256) { note(o, "%s: a hat or axis (%d) is not imported", what, id); continue; }
            if (*o->button_guid && strcmp(o->button_guid, g)) {
                note(o, "%s: button %d is on a second joystick; the game reads one", what, id);
                continue;
            }
            snprintf(o->button_guid, sizeof o->button_guid, "%s", g);
            add(o->button[a], &o->nbutton[a], id);
        } else note(o, "%s: %s is not a keyboard or joystick", what, f[0]);
    }
}

/* dev;axis;P|V|A;deadzone;scale;limit: 1 if it is an axis of a joystick */
static int sensor81(const char *value, char *guid, int gn, int *axis, char *type, double *dz, double *scale)
{
    char buf[256];
    snprintf(buf, sizeof buf, "%s", value);
    char *f[6] = { 0 };
    int nf = 0;
    for (char *s2 = NULL, *t = strtok_r(buf, ";", &s2); t && nf < 6; t = strtok_r(NULL, ";", &s2)) f[nf++] = trim(t);
    if (nf < 2 || !joy_guid(f[0], guid, gn)) return 0;
    const int id = atoi(f[1]);
    if (id < 512 || id >= 768) return 0;
    *axis = id - 512;
    *type = nf > 2 ? f[2][0] : 'A';
    *dz = nf > 3 ? atof(f[3]) : 0;
    *scale = nf > 4 ? atof(f[4]) : 1;
    return 1;
}

static const char *raw(const char *path, const char *sec, const char *key, char *v, int n)
{
    return ini_get_raw(path, sec, key, "", v, n);
}

static void read81(const char *path, VpxImport *o)
{
    char v[512], key[64], g[40];
    for (size_t i = 0; i < sizeof k_map81 / sizeof k_map81[0]; i++) {
        const int a = key_action_by_name(k_map81[i].ours);
        snprintf(key, sizeof key, "Mapping.%s", k_map81[i].vpx);
        raw(path, "Input", key, v, sizeof v);
        if (!*v && k_map81[i].def) snprintf(v, sizeof v, "Key;%d", k_map81[i].def);   /* empty: VPinball's default */
        binding81(o, a, k_map81[i].vpx, v);
    }

    int axis;
    char type;
    double dz, scale;
    if ((*raw(path, "Input", "Mapping.Plunger0.Position", v, sizeof v) || *raw(path, "Input", "Mapping.PlungerPos", v, sizeof v))
        && sensor81(v, g, sizeof g, &axis, &type, &dz, &scale)) {
        o->plunger_axis = axis;
        o->plunger_reversed = scale < 0;
        snprintf(o->plunger_guid, sizeof o->plunger_guid, "%s", g);
    }
    if ((*raw(path, "Input", "Mapping.Plunger0.Velocity", v, sizeof v) || *raw(path, "Input", "Mapping.PlungerVel", v, sizeof v))
        && sensor81(v, g, sizeof g, &axis, &type, &dz, &scale))
        o->plunger_speed_axis = axis;

    /* nudging: the first sensor, as acceleration, velocity, or a pad's stick */
    static const char *const pairs[][2] = {
        { "Mapping.Nudge0.AccX", "Mapping.Nudge0.AccY" }, { "Mapping.Nudge0.VelX", "Mapping.Nudge0.VelY" },
        { "Mapping.Nudge0.X", "Mapping.Nudge0.Y" }, { "Mapping.NudgeX1", "Mapping.NudgeY1" },
    };
    for (int p = 0; p < 4 && o->nudge_x < 0 && o->nudge_y < 0; p++) {
        char gx[40] = "", gy[40] = "";
        int ax = -1, ay = -1;
        char tx = 0, ty = 0;
        double dzx = 0, dzy = 0, sx = 1, sy = 1;
        if (*raw(path, "Input", pairs[p][0], v, sizeof v) && !sensor81(v, gx, sizeof gx, &ax, &tx, &dzx, &sx)) ax = -1;
        if (*raw(path, "Input", pairs[p][1], v, sizeof v) && !sensor81(v, gy, sizeof gy, &ay, &ty, &dzy, &sy)) ay = -1;
        if (ax < 0 && ay < 0) continue;
        if (ax >= 0 && ay >= 0 && strcmp(gx, gy)) note(o, "Nudging: X and Y are on two joysticks; the game reads one, X's");
        snprintf(o->nudge_guid, sizeof o->nudge_guid, "%s", ax >= 0 ? gx : gy);
        o->nudge_x = ax;
        o->nudge_y = ay;
        o->invert_x = sx < 0;
        o->invert_y = sy < 0;
        const char t = ax >= 0 ? tx : ty;
        o->is_velocity = p == 1 || t == 'V';
        double d = dzx > dzy ? dzx : dzy;
        if (t == 'P') {
            /* Nudge0.X/Y: VPinball's "Game Controller" type, a stick, which says
             * nothing of what the board reports. The nightlies' NudgeX1/Y1, left
             * in the file, did: velocity or acceleration, on the same axes. */
            char g1[40];
            int a1 = -1;
            char t1 = 0;
            double dz1 = 0, s1 = 1;
            const char *older = ax >= 0 ? "Mapping.NudgeX1" : "Mapping.NudgeY1";
            if (p == 2 && *raw(path, "Input", older, v, sizeof v) && sensor81(v, g1, sizeof g1, &a1, &t1, &dz1, &s1)
                && a1 == (ax >= 0 ? ax : ay) && (t1 == 'V' || t1 == 'A')) {
                o->is_velocity = t1 == 'V';
                if (d == 0) d = dz1;
                note(o, "Nudging: VPinball reads the board as a game controller's stick; its older NudgeX1/Y1 said %s, so imported as that",
                     t1 == 'V' ? "velocity" : "acceleration");
            } else
                note(o, "Nudging: VPinball reads the sensor as a position; imported as acceleration");
        }
        o->dead = (int)(d * 32767 + 0.5);
    }
    if (o->nudge_x >= 0 || o->nudge_y >= 0) {
        const double deg = atof(ini_get_raw(path, "Player", "NudgeOrientation0", "0", v, sizeof v));
        const int q = ((int)((deg + 45) / 90) % 4 + 4) % 4;
        if (q == 1 || q == 3) { int t = o->nudge_x; o->nudge_x = o->nudge_y; o->nudge_y = t; t = o->invert_x; o->invert_x = o->invert_y; o->invert_y = t; }
        if (q == 1) o->invert_y = !o->invert_y;                    /* x' = y, y' = -x */
        if (q == 2) { o->invert_x = !o->invert_x; o->invert_y = !o->invert_y; }
        if (q == 3) o->invert_x = !o->invert_x;                    /* x' = -y, y' = x */
        if (deg != q * 90.0 && !(q == 0 && deg == 360)) note(o, "Nudging: the sensor is turned %.0f degrees; imported as %d", deg, q * 90);
    }
}

/* ---- 10.8.0: [Player] ---------------------------------------------------------- */

static const struct { const char *key, *joy, *ours; int def; } k_map80[] = {
    { "LFlipKey", "JoyLFlipKey", "left_flipper", 0x2A }, { "RFlipKey", "JoyRFlipKey", "right_flipper", 0x36 },
    { "PlungerKey", "JoyPlungerKey", "launch", 0x1C }, { "LTiltKey", "JoyLTiltKey", "nudge_left", 0x2C },
    { "RTiltKey", "JoyRTiltKey", "nudge_right", 0x35 }, { "CTiltKey", "JoyCTiltKey", "nudge_up", 0x39 },
    { "StartGameKey", "JoyStartGameKey", "start", 0x02 }, { "ExitGameKey", "JoyExitGameKey", "exit", 0x10 },
    { "EscapeKey", NULL, "exit", 0x01 },
};

/* VPinball 10.8.0's axis numbers: 0 none, 1 X .. 6 RZ, 7 and 8 the sliders; -1 none */
static int axis80(VpxImport *o, const char *what, int v)
{
    if (v >= 1 && v <= 8) {
        if (v >= 7) note(o, "%s: slider %d is taken as axis %d; check it on the page", what, v - 6, v - 1);
        return v - 1;
    }
    if (v == 9) note(o, "%s: an Open Pinball Device is not read by the game", what);
    return -1;
}

static void read80(const char *path, VpxImport *o)
{
    char v[64];
    for (size_t i = 0; i < sizeof k_map80 / sizeof k_map80[0]; i++) {
        const int a = key_action_by_name(k_map80[i].ours);
        int dik = atoi(raw(path, "Player", k_map80[i].key, v, sizeof v));
        if (!*v || dik <= 0 || dik > 0xDD) dik = k_map80[i].def;
        const int sc = key_from_dik(dik);
        if (sc && sc != k_actions[a].sc) add(o->key[a], &o->nkey[a], sc);
        if (!k_map80[i].joy) continue;
        const int b = atoi(raw(path, "Player", k_map80[i].joy, v, sizeof v));
        if (b >= 25 && b <= 27) note(o, "%s: a mouse button is not imported", k_map80[i].joy);
        else if (b >= 1 && b <= 32) add(o->button[a], &o->nbutton[a], b - 1);
    }
    if (*raw(path, "Player", "PlungerAxis", v, sizeof v)) {
        o->plunger_axis = axis80(o, "PlungerAxis", atoi(v));
        o->plunger_reversed = atoi(raw(path, "Player", "ReversePlungerAxis", v, sizeof v)) != 0;
    }
    if (*raw(path, "Player", "PlungerSpeedAxis", v, sizeof v)) o->plunger_speed_axis = axis80(o, "PlungerSpeedAxis", atoi(v));
    if (*raw(path, "Player", "PBWEnabled", v, sizeof v) && !atoi(v)) {
        note(o, "Nudging: off in VPinball (PBWEnabled = 0)");
        return;
    }
    if (*raw(path, "Player", "LRAxis", v, sizeof v)) o->nudge_x = axis80(o, "LRAxis", atoi(v));
    if (*raw(path, "Player", "UDAxis", v, sizeof v)) o->nudge_y = axis80(o, "UDAxis", atoi(v));
    if (o->nudge_x < 0 && o->nudge_y < 0) return;
    o->invert_x = atoi(raw(path, "Player", "LRAxisFlip", v, sizeof v)) != 0;
    o->invert_y = atoi(raw(path, "Player", "UDAxisFlip", v, sizeof v)) != 0;
    o->is_velocity = atoi(raw(path, "Player", "AccelVelocityInput", v, sizeof v)) != 0;
    if (*raw(path, "Player", "DeadZone", v, sizeof v)) o->dead = (int)(atof(v) * 32767 / 100 + 0.5);
    if (atoi(raw(path, "Player", "PBWRotationCB", v, sizeof v)))
        note(o, "Nudging: VPinball turns the sensor %s degrees; set the axes on the page", raw(path, "Player", "PBWRotationValue", v, sizeof v));
    if (*raw(path, "Player", "PBWNormalMount", v, sizeof v) && !atoi(v))
        note(o, "Nudging: the sensor is not mounted the usual way in VPinball; calibrate it on the page");
    if (*raw(path, "Player", "PBWAccelGainX", v, sizeof v)) note(o, "Nudging: VPinball's gain is not imported; strength stays 1");
}

/* ---- both ----------------------------------------------------------------------- */

int vpx_read(const char *path, VpxImport *o)
{
    char v[256];
    memset(o, 0, sizeof *o);
    o->plunger_axis = o->plunger_speed_axis = o->nudge_x = o->nudge_y = -1;
    o->dead = o->sound_volume = -1;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    const int has_input = *raw(path, "Input", "Devices", v, sizeof v) != 0;
    const int has_player = *ini_get_raw(path, "Player", "LFlipKey", "", v, sizeof v)
                           || *raw(path, "Version", "VPinball", v, sizeof v)
                           || *raw(path, "Player", "Sound3D", v, sizeof v);
    if (has_input) { o->format = 2; read81(path, o); }
    else if (has_player) { o->format = 1; read80(path, o); }
    else return 0;
    if (*raw(path, "Player", "Sound3D", v, sizeof v)) {
        const int m = atoi(v);
        o->ssf = m == 4 || m == 5;
        if (!o->ssf && m) note(o, "Sound: VPinball's speaker mode %d is not an exciter setup; SSF is left off", m);
    }
    if (*raw(path, "Player", "SoundVolume", v, sizeof v)) o->sound_volume = atoi(v);
    if (*raw(path, "Player", "SoundDevice", v, sizeof v))
        note(o, "Sound: VPinball plays to \"%s\"; the game plays to the default device", v);
    return 1;
}
