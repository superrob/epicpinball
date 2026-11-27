/*
 * pad.c - an Xbox (XInput) controller, mapped by controller.ini in the save
 * folder, as in the Gunman Chronicles recomp. Nothing in the game changes:
 * each pad input presses a key the game already reads. The file is written
 * with the defaults on first run -- GOG's DOSBox mapping (its controls.txt):
 * triggers flip, bumpers nudge, A plunges -- and read at start and whenever
 * the settings menu says so. The pad only acts while the window is in front;
 * what it held is let go when that ends, or the pad is unplugged.
 * The file's [plunger] section names a cabinet's plunger sensor, which is
 * read here as a joystick axis (Linux) and played in by plunger.c.
 * On Linux the pad is SDL2's GameController, which names any pad's buttons
 * the Xbox way, so the same file maps it. A cabinet's board is a pad for its
 * buttons only: its axes are sensors, and SDL takes a centred one for a half
 * pulled trigger, which would hold a flipper, and its solenoid, on.
 */
#include "machine.h"
#include "plunger.h"
#include "nudge.h"
#include "keys.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <SDL.h>
#endif

static const struct { const char *name; uint16_t bit; } k_buttons[] = {
    { "dpad_up", 0x0001 }, { "dpad_down", 0x0002 }, { "dpad_left", 0x0004 }, { "dpad_right", 0x0008 },
    { "start", 0x0010 }, { "back", 0x0020 }, { "left_stick", 0x0040 }, { "right_stick", 0x0080 },
    { "lb", 0x0100 }, { "rb", 0x0200 }, { "a", 0x1000 }, { "b", 0x2000 }, { "x", 0x4000 }, { "y", 0x8000 },
};
#define NBUTTONS (int)(sizeof k_buttons / sizeof k_buttons[0])
enum { IN_LT = NBUTTONS, IN_RT, IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT, IN_COUNT };
static const char *const k_extra[] = { "lt", "rt", "stick_up", "stick_down", "stick_left", "stick_right" };

extern const char k_controller_default[];   /* controller_ini.c */

static int map[IN_COUNT];            /* scan code, | 0x100 for E0-prefixed; 0 = none */
static int trigger = 64, stick = 12000;
static char ini[512];
static uint8_t held[0x200];          /* how many inputs hold each key down (E0 keys apart) */
static int remap[0x200];             /* [keys]: a key that sends an action's key; 0 = its own */
#define BUTTONS_PER 4
static struct {
    char device[128];
    int on, button[KEY_ACTIONS][BUTTONS_PER], n[KEY_ACTIONS], most;
} btn;                               /* [buttons] */
static uint32_t was;
static struct {
    char device[128];
    int on, axis, speed_axis, rest, pulled;
} plg;
static struct {
    char device[128];
    int on, x_axis, y_axis, button[3];   /* left, right, up: -1 none */
    int indicator;                       /* the ring on the playfield (hud.c) */
    int indicator_range;                 /* the reading at its edge */
} ndg;
static const int k_nudge_key[3] = { 0x2C, 0x35, 0x39 };   /* Z, /, Space: the game's own nudge */

int host_key_by_name(const char *s);  /* host.c */
#include "ini.h"
#include "autostart.h"

const char *pad_ini(void) { return ini; }

/* A key down or up, counted: two inputs on one key press it once */
static void key_hold(int sc, int down)
{
    int k = sc & 0x1FF;
    if (!down && !held[k]) return;                 /* let go of what was never pressed */
    if (down ? held[k]++ : --held[k]) return;
    /* --table: while PINBALL is driven, counted and not sent, so a button held
     * across is not pressed again when the player has the game back; between
     * games in a cabinet, the initials and Start */
    if (g_auto_on && autostart_busy()) return;
    if (down && g_auto_on && autostart_key(k)) return;
    if (sc & 0x100) kbd_scancode(0xE0);
    kbd_scancode((uint8_t)((k & 0xFF) | (down ? 0 : 0x80)));
}

/* A key of the keyboard, down or up: the action's key if [keys] lists it,
 * else its own. Counted with the pad's and the buttons', so a flipper held
 * by two of them is let go when both are. */
void pad_key(int sc, int up)
{
    sc &= 0x1FF;
    key_hold(remap[sc] ? remap[sc] : sc, !up);
}

static void keys_load(void)
{
    char v[256];
    memset(remap, 0, sizeof remap);
    for (int a = 0; a < KEY_ACTIONS; a++) {
        int k[8], n = key_list(ini_get(ini, "keys", k_actions[a].name, "none", v, sizeof v), k, 8);
        if (n < 0) { fprintf(stderr, "[keys] %s = %s: a key is not one I know (keys.c)\n", k_actions[a].name, v); continue; }
        for (int i = 0; i < n; i++) remap[k[i] & 0x1FF] = k_actions[a].sc;
    }
}

static void buttons_load(void)
{
    char v[64];
    memset(&btn, 0, sizeof btn);
    ini_get(ini, "buttons", "device", "none", btn.device, sizeof btn.device);
    int any = 0;
    for (int a = 0; a < KEY_ACTIONS; a++) {
        const char *s = ini_get(ini, "buttons", k_actions[a].name, "none", v, sizeof v);
        while (*s && btn.n[a] < BUTTONS_PER) {
            char *e;
            while (*s == ' ' || *s == '|' || *s == ',') s++;
            if (!*s || !_strnicmp(s, "none", 4)) break;
            long b = strtol(s, &e, 10);
            if (e == s || b < 0 || b > 127) {
                fprintf(stderr, "[buttons] %s = %s: button numbers from --joysticks\n", k_actions[a].name, v);
                btn.n[a] = 0;
                break;
            }
            btn.button[a][btn.n[a]++] = (int)b;
            if (b + 1 > btn.most) btn.most = (int)b + 1;
            s = e;
        }
        any |= btn.n[a] > 0;
    }
    if (!any) return;
    if (!*btn.device || !_stricmp(btn.device, "none")) {
        fprintf(stderr, "[buttons] %s: buttons are set, but no device. The buttons are off.\n", ini);
        return;
    }
#ifdef _WIN32
    fprintf(stderr, "[buttons] not supported on Windows yet (Linux only for now)\n");
#else
    btn.on = 1;
#endif
}

/* The keys follow the inputs that are on in `now` */
static void press(uint32_t now)
{
    /* two inputs on one key (the d-pad and the stick) press it once */
    for (int i = 0; i < IN_COUNT; i++) {
        int sc = map[i];
        if (!sc || !((now ^ was) & (1u << i))) continue;
        key_hold(sc, (now >> i) & 1);
    }
    was = now;
}

#ifndef _WIN32
static void plunger_forget(void);
static void nudge_forget(void);
static void buttons_forget(void);
#endif

/* -1 none, -2 not an axis. The names are the order SDL gives a board that has all six. */
static int axis_by_name(const char *s)
{
    static const char *const k[] = { "X", "Y", "Z", "RX", "RY", "RZ" };
    if (!_stricmp(s, "none")) return -1;
    for (int i = 0; i < 6; i++) if (!_stricmp(s, k[i])) return i;
    if (*s && strlen(s) < 3 && !s[strspn(s, "0123456789")]) return atoi(s);
    return -2;
}

static void plunger_load(void)
{
    char v[32];
    memset(&plg, 0, sizeof plg);
    ini_get(ini, "plunger", "device", "none", plg.device, sizeof plg.device);
    plg.axis = axis_by_name(ini_get(ini, "plunger", "axis", "none", v, sizeof v));
    plg.speed_axis = axis_by_name(ini_get(ini, "plunger", "speed_axis", "none", v, sizeof v));
    plg.rest = atoi(ini_get(ini, "plunger", "rest", "0", v, sizeof v));
    plg.pulled = atoi(ini_get(ini, "plunger", "pulled", "32767", v, sizeof v));
    int none = !*plg.device || !_stricmp(plg.device, "none");
    if (plg.axis == -1) return;      /* off */
    const char *why = NULL;
    if (plg.axis == -2 || plg.speed_axis == -2) why = "an axis is a number, or X Y Z RX RY RZ";
    else if (none) why = "axis is set, but no device";
    else if (plg.rest < -32768 || plg.rest > 32767 || plg.pulled < -32768 || plg.pulled > 32767)
        why = "rest and pulled are -32768 to 32767";
    else if (abs(plg.pulled - plg.rest) < 1000) why = "rest and pulled are too close together";
#ifdef _WIN32
    else why = "not supported on Windows yet (Linux only for now)";
#endif
    if (why) fprintf(stderr, "[plunger] %s: %s. The plunger is off.\n", ini, why);
    plg.on = !why;
}

int pad_plunger_on(void) { return plg.on; }

static void nudge_load(void)
{
    static const char *const bn[3] = { "left_button", "right_button", "up_button" };
    char v[32];
    memset(&ndg, 0, sizeof ndg);
    ini_get(ini, "nudge", "device", "none", ndg.device, sizeof ndg.device);
    ndg.x_axis = axis_by_name(ini_get(ini, "nudge", "x_axis", "none", v, sizeof v));
    ndg.y_axis = axis_by_name(ini_get(ini, "nudge", "y_axis", "none", v, sizeof v));
    int any = ndg.x_axis != -1 || ndg.y_axis != -1, bad_button = 0;
    for (int i = 0; i < 3; i++) {
        ini_get(ini, "nudge", bn[i], "none", v, sizeof v);
        ndg.button[i] = !_stricmp(v, "none") ? -1 : *v && !v[strspn(v, "0123456789")] && strlen(v) < 4 ? atoi(v) : -2;
        bad_button |= ndg.button[i] == -2;
        any |= ndg.button[i] != -1;
    }
    NudgeSettings st = { 0 };
    st.dead = atoi(ini_get(ini, "nudge", "dead", "1500", v, sizeof v));
    st.strength = atof(ini_get(ini, "nudge", "strength", "1", v, sizeof v));
    st.tilt = atof(ini_get(ini, "nudge", "tilt", "1", v, sizeof v));
    st.invert_x = atoi(ini_get(ini, "nudge", "invert_x", "0", v, sizeof v)) != 0;
    st.invert_y = atoi(ini_get(ini, "nudge", "invert_y", "0", v, sizeof v)) != 0;
    st.is_velocity = atoi(ini_get(ini, "nudge", "is_velocity", "0", v, sizeof v)) != 0;
    ndg.indicator = atoi(ini_get(ini, "nudge", "indicator", "1", v, sizeof v)) != 0;
    ndg.indicator_range = atoi(ini_get(ini, "nudge", "indicator_range", "8000", v, sizeof v));
    if (ndg.indicator_range < 100 || ndg.indicator_range > 32767) ndg.indicator_range = 8000;
    nudge_settings(&st);
    if (!any) return;                /* off */
    const char *why = NULL;
    if (ndg.x_axis == -2 || ndg.y_axis == -2) why = "an axis is a number, or X Y Z RX RY RZ";
    else if (bad_button) why = "a button is a number from --joysticks";
    else if (!*ndg.device || !_stricmp(ndg.device, "none")) why = "an axis or button is set, but no device";
    else if (st.dead < 0 || st.dead > 30000) why = "dead is 0 to 30000";
    else if (!(st.strength >= 0) || !(st.tilt >= 0)) why = "strength and tilt are 0 or more";
#ifdef _WIN32
    else why = "not supported on Windows yet (Linux only for now)";
#endif
    if (why) fprintf(stderr, "[nudge] %s: %s. Nudging from the cabinet is off.\n", ini, why);
    ndg.on = !why;
}

int pad_nudge_on(void) { return ndg.on; }
int pad_nudge_indicator(void) { return ndg.indicator && (ndg.x_axis >= 0 || ndg.y_axis >= 0); }
int pad_nudge_range(void) { return ndg.indicator_range ? ndg.indicator_range : 8000; }

void pad_load(void)
{
    press(0);                        /* by the mapping that pressed them */
    snprintf(ini, sizeof ini, "%s/controller.ini", g_opt.savedir);
    FILE *f = fopen(ini, "rb");
    if (f) fclose(f);
    else if ((f = fopen(ini, "wb")) != NULL) { fputs(k_controller_default, f); fclose(f); }
    char v[32];
    for (int i = 0; i < IN_COUNT; i++) {
        ini_get(ini, "controller", i < NBUTTONS ? k_buttons[i].name : k_extra[i - NBUTTONS], "none", v, sizeof v);
        map[i] = _stricmp(v, "none") ? host_key_by_name(v) : 0;
    }
    trigger = atoi(ini_get(ini, "controller", "trigger", "64", v, sizeof v));
    stick = atoi(ini_get(ini, "controller", "stick", "12000", v, sizeof v));
    keys_load();
    plunger_load();
    nudge_load();
#ifndef _WIN32
    buttons_forget();
    plunger_forget();
    nudge_forget();
#endif
    buttons_load();
}

/* XINPUT_STATE's layout */
typedef struct { uint32_t packet; uint16_t buttons; uint8_t lt, rt; int16_t lx, ly, rx, ry; } XState;

#ifdef _WIN32
typedef DWORD (WINAPI *XInputGetState_t)(DWORD, XState *);

static int get_state(XState *s)
{
    static XInputGetState_t get;
    static int tried;
    if (!tried) {
        HMODULE m;
        tried = 1;
        m = LoadLibraryA("xinput1_4.dll");
        if (!m) m = LoadLibraryA("xinput9_1_0.dll");
        if (m) get = (XInputGetState_t)GetProcAddress(m, "XInputGetState");
    }
    return get && get(0, s) == ERROR_SUCCESS;
}

int pad_plunger(int front, PlungerInput *in)
{
    (void)front;
    memset(in, 0, sizeof *in);
    return 0;
}

void pad_nudge(int front) { (void)front; nudge_reading(0, 0, 0); }

int pad_list(double seconds)
{
    (void)seconds;
    fprintf(stderr, "--joysticks is not supported on Windows yet (Linux only for now)\n");
    return 2;
}
#else
static SDL_Joystick *pjoy;           /* the plunger's */

static void plunger_forget(void)
{
    if (!pjoy) return;
    SDL_JoystickClose(pjoy);
    pjoy = NULL;
}

/* A board by its name or GUID that has `axes` axes and `buttons` buttons
 * (numbered from 0), looked for twice a second; NULL until it is there.
 * Opened twice (the plunger and the nudge on one board), SDL counts it. */
typedef struct { uint64_t looked; int said; } Looking;

static SDL_Joystick *joy_find(const char *device, int axes, int buttons, const char *tag, Looking *l)
{
    if (l->looked && host_us() - l->looked < 500000) return NULL;
    l->looked = host_us();
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        char id[33];
        const char *name = SDL_JoystickNameForIndex(i);
        SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i), id, sizeof id);
        if ((!name || _stricmp(device, name)) && _stricmp(device, id)) continue;
        SDL_Joystick *j = SDL_JoystickOpen(i);
        if (!j) continue;
        int na = SDL_JoystickNumAxes(j), nb = SDL_JoystickNumButtons(j);
        if (axes <= na && buttons <= nb) {
            l->said = 0;
            fprintf(stderr, "[%s] %s\n", tag, name ? name : id);
            return j;
        }
        if (!l->said) fprintf(stderr, "[%s] %s has %d axes and %d buttons, numbered from 0\n", tag, name ? name : id, na, nb);
        SDL_JoystickClose(j);
        l->said = 1;
        return NULL;
    }
    if (!l->said) fprintf(stderr, "[%s] no device called %s (--joysticks lists them); waiting for it\n", tag, device);
    l->said = 1;
    return NULL;
}

static void plunger_find(void)
{
    static Looking l;
    const int need = (plg.axis > plg.speed_axis ? plg.axis : plg.speed_axis) + 1;
    pjoy = joy_find(plg.device, need, 0, "plunger", &l);
}

/* ---- nudging ---- */

static SDL_Joystick *njoy;
static uint8_t nudge_down[3];

static void nudge_buttons_up(void)
{
    for (int i = 0; i < 3; i++)
        if (nudge_down[i]) { key_hold(k_nudge_key[i], 0); nudge_down[i] = 0; }
}

static void nudge_forget(void)
{
    nudge_buttons_up();
    nudge_reading(0, 0, 0);
    if (!njoy) return;
    SDL_JoystickClose(njoy);
    njoy = NULL;
}

/* Asked on every host_frame, read once a millisecond: the accelerometer for
 * the next physics step (nudge.c), and the buttons as the game's nudge keys.
 * Not in front, or unplugged: no shove, and its keys let go. */
void pad_nudge(int front)
{
    static uint64_t at;
    static Looking l;
    if (!front || !ndg.on) { nudge_buttons_up(); nudge_reading(0, 0, 0); return; }
    if (host_us() - at < 1000) return;
    at = host_us();
    SDL_JoystickUpdate();
    if (njoy && !SDL_JoystickGetAttached(njoy)) {
        fprintf(stderr, "[nudge] unplugged\n");
        nudge_forget();
    }
    if (!njoy) {
        int axes = (ndg.x_axis > ndg.y_axis ? ndg.x_axis : ndg.y_axis) + 1, buttons = 0;
        for (int i = 0; i < 3; i++) if (ndg.button[i] + 1 > buttons) buttons = ndg.button[i] + 1;
        njoy = joy_find(ndg.device, axes, buttons, "nudge", &l);
        if (!njoy) { nudge_reading(0, 0, 0); return; }
    }
    nudge_reading(ndg.x_axis >= 0 || ndg.y_axis >= 0,
                  ndg.x_axis >= 0 ? SDL_JoystickGetAxis(njoy, ndg.x_axis) : 0,
                  ndg.y_axis >= 0 ? SDL_JoystickGetAxis(njoy, ndg.y_axis) : 0);
    for (int i = 0; i < 3; i++) {
        if (ndg.button[i] < 0) continue;
        const uint8_t d = SDL_JoystickGetButton(njoy, ndg.button[i]) != 0;
        if (d != nudge_down[i]) { key_hold(k_nudge_key[i], d); nudge_down[i] = d; }
    }
}

/* ---- [buttons] ---- */

static SDL_Joystick *bjoy;
static uint8_t btn_down[KEY_ACTIONS];

static void buttons_up(void)
{
    for (int a = 0; a < KEY_ACTIONS; a++)
        if (btn_down[a]) { key_hold(k_actions[a].sc, 0); btn_down[a] = 0; }
}

static void buttons_forget(void)
{
    buttons_up();
    if (!bjoy) return;
    SDL_JoystickClose(bjoy);
    bjoy = NULL;
}

/* A cabinet's buttons, each its action's key while it is down */
static void buttons_poll(int front)
{
    static Looking l;
    if (!front || !btn.on) { buttons_up(); return; }
    SDL_JoystickUpdate();
    if (bjoy && !SDL_JoystickGetAttached(bjoy)) {
        fprintf(stderr, "[buttons] unplugged\n");
        buttons_forget();
    }
    if (!bjoy && !(bjoy = joy_find(btn.device, 0, btn.most, "buttons", &l))) return;
    for (int a = 0; a < KEY_ACTIONS; a++) {
        uint8_t d = 0;
        for (int i = 0; i < btn.n[a]; i++) d |= SDL_JoystickGetButton(bjoy, btn.button[a][i]) != 0;
        if (d != btn_down[a]) { key_hold(k_actions[a].sc, d); btn_down[a] = d; }
    }
}

/* Asked on every host_frame, looked at once a millisecond. Not in front, or
 * unplugged, is no sensor: SDL centres the axes then, which is a full release. */
int pad_plunger(int front, PlungerInput *in)
{
    static PlungerInput last;
    static uint64_t at;
    if (!front || !plg.on) {
        memset(&last, 0, sizeof last);
    } else if (host_us() - at >= 1000) {
        at = host_us();
        SDL_JoystickUpdate();
        if (pjoy && !SDL_JoystickGetAttached(pjoy)) {
            fprintf(stderr, "[plunger] unplugged\n");
            plunger_forget();
        }
        memset(&last, 0, sizeof last);
        if (!pjoy) plunger_find();
        if (pjoy) {
            last.have = 1;
            last.pos = plunger_map(SDL_JoystickGetAxis(pjoy, plg.axis), plg.rest, plg.pulled);
            if (plg.speed_axis >= 0)
                last.speed = abs(SDL_JoystickGetAxis(pjoy, plg.speed_axis)) * PLUNGER_FULL / 32768;
        }
    }
    *in = last;
    return last.have;
}

/* --joysticks: what is plugged in, and each axis as it moves */
int pad_list(double seconds)
{
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");     /* there is no window */
    if (SDL_Init(SDL_INIT_JOYSTICK)) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    enum { MOST = 16, AXES = 16, BUTTONS = 64 };
    SDL_Joystick *j[MOST];
    int shown[MOST][AXES], n = SDL_NumJoysticks();
    static uint8_t pressed[MOST][BUTTONS];
    if (n > MOST) n = MOST;
    if (!n) printf("No joysticks. (May this user read /dev/input? docs/cabinet-feedback.md)\n");
    for (int i = 0; i < n; i++) {
        char id[33];
        const char *name = SDL_JoystickNameForIndex(i);
        SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i), id, sizeof id);
        j[i] = SDL_JoystickOpen(i);
        printf("%d: %s\n   GUID %s, %04X:%04X, %d axes, %d buttons\n", i, name ? name : "?", id,
               SDL_JoystickGetDeviceVendor(i), SDL_JoystickGetDeviceProduct(i), j[i] ? SDL_JoystickNumAxes(j[i]) : 0,
               j[i] ? SDL_JoystickNumButtons(j[i]) : 0);
        for (int a = 0; a < AXES; a++) shown[i][a] = 1 << 20;
    }
    if (n) printf("Move the plunger or shove the cabinet to see the axis and range; press a button to see its number."
                  " Ctrl+C ends.\n");
    fflush(stdout);
    for (uint64_t end = host_us() + (uint64_t)(seconds * 1e6); n && (seconds <= 0 || host_us() < end); ) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_QUIT) n = 0;
        SDL_JoystickUpdate();
        for (int i = 0; i < n; i++) {
            if (!j[i]) continue;
            int nb = SDL_JoystickNumButtons(j[i]);
            for (int b = 0; b < nb && b < BUTTONS; b++) {
                const uint8_t d = SDL_JoystickGetButton(j[i], b) != 0;
                if (d != pressed[i][b]) printf("%d: button %d %s\n", i, b, d ? "down" : "up");
                pressed[i][b] = d;
            }
            int axes = SDL_JoystickNumAxes(j[i]), changed = 0;
            if (axes > AXES) axes = AXES;
            for (int a = 0; a < axes; a++)
                if (abs(SDL_JoystickGetAxis(j[i], a) - shown[i][a]) >= 600) changed = 1;
            if (!changed) continue;
            printf("%d:", i);
            for (int a = 0; a < axes; a++) printf("  %d=%6d", a, shown[i][a] = SDL_JoystickGetAxis(j[i], a));
            printf("\n");
        }
        fflush(stdout);
        SDL_Delay(100);
    }
    SDL_Quit();
    return 0;
}

/* Boards that report a cabinet's plunger and nudge as a pad's axes */
static int cabinet_board(uint16_t vid, uint16_t pid)
{
    static const uint16_t k[][2] = {
        { 0x1209, 0xEAEA }, { 0x1209, 0xEAEB },      /* Pinscape Pico */
        { 0xFAFA, 0x00F7 },                          /* Pinscape KL25Z */
        { 0x2E8A, 0x106F }, { 0x39F5, 0x9208 },      /* other boards libdof knows */
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        if (vid == k[i][0] && pid == k[i][1]) return 1;
    return 0;
}

/* The first pad SDL knows, read into XInput's shape */
static int get_state(XState *s)
{
    static SDL_GameController *gc;
    static const SDL_GameControllerButton b[NBUTTONS] = {     /* in k_buttons' order */
        SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
        SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK,
        SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK, SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
        SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y };
    if (gc && !SDL_GameControllerGetAttached(gc)) { SDL_GameControllerClose(gc); gc = NULL; }
    for (int i = 0; !gc && i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) gc = SDL_GameControllerOpen(i);
    if (!gc) return 0;
    s->buttons = 0;
    for (int i = 0; i < NBUTTONS; i++) if (SDL_GameControllerGetButton(gc, b[i])) s->buttons |= k_buttons[i].bit;
    s->lt = s->rt = 0;
    s->lx = s->ly = 0;
    if (cabinet_board(SDL_GameControllerGetVendor(gc), SDL_GameControllerGetProduct(gc))) return 1;
    if (plg.on) {                    /* whatever it is, its axes are the plunger's */
        SDL_Joystick *j = SDL_GameControllerGetJoystick(gc);
        char id[33];
        const char *name = SDL_JoystickName(j);
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(j), id, sizeof id);
        if ((name && !_stricmp(plg.device, name)) || !_stricmp(plg.device, id)) return 1;
    }
    s->lt = (uint8_t)(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) >> 7);
    s->rt = (uint8_t)(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >> 7);
    s->lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
    int y = -SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);   /* XInput's up is +, SDL's - */
    s->ly = (int16_t)(y > 32767 ? 32767 : y);
    return 1;
}
#endif

/* front: the window has the keys */
void pad_poll(int front)
{
    XState s;

#ifndef _WIN32
    buttons_poll(front);
#endif
    if (!front || !get_state(&s)) {
        press(0);
        return;
    }

    uint32_t now = 0;
    for (int i = 0; i < NBUTTONS; i++) if (s.buttons & k_buttons[i].bit) now |= 1u << i;
    if (s.lt > trigger) now |= 1u << IN_LT;
    if (s.rt > trigger) now |= 1u << IN_RT;
    if (s.ly >  stick) now |= 1u << IN_UP;
    if (s.ly < -stick) now |= 1u << IN_DOWN;
    if (s.lx < -stick) now |= 1u << IN_LEFT;
    if (s.lx >  stick) now |= 1u << IN_RIGHT;

    press(now);
}
