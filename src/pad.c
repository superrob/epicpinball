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

static const char k_default[] =
"; Epic Pinball recompiled -- Xbox controller mapping.\r\n"
"; Each pad input presses a key. Keys: ESC ENTER SPACE UP DOWN LEFT RIGHT\r\n"
"; LSHIFT RSHIFT Z SLASH CTRL ALT TAB Y N F1 F2 F3 1-6, a scan code in hex,\r\n"
"; or none. Delete this file to get the defaults back.\r\n"
"[controller]\r\n"
"lt = LSHIFT\r\nrt = RSHIFT\r\n"
"lb = Z\r\nrb = SLASH\r\n"
"a = SPACE\r\nb = N\r\nx = ENTER\r\ny = Y\r\n"
"back = ESC\r\nstart = ESC\r\n"
"dpad_up = UP\r\ndpad_down = DOWN\r\ndpad_left = LEFT\r\ndpad_right = RIGHT\r\n"
"stick_up = UP\r\nstick_down = DOWN\r\nstick_left = LEFT\r\nstick_right = RIGHT\r\n"
"left_stick = none\r\nright_stick = none\r\n"
"; how far a trigger (0-255) and the stick (0-32767) go before they count\r\n"
"trigger = 64\r\nstick = 12000\r\n"
"\r\n"
"; A cabinet's plunger sensor (Linux; docs/cabinet-feedback.md), off until an\r\n"
"; axis is set. epicpinball --joysticks lists devices and live axis values.\r\n"
"[plunger]\r\n"
"device = none                ; its name or GUID\r\n"
"axis = none                  ; a number from that list, or X Y Z RX RY RZ\r\n"
"rest = 0                     ; the axis at rest\r\n"
"pulled = 32767               ; and pulled right back\r\n"
"speed_axis = none            ; for boards that report the release speed\r\n";

static int map[IN_COUNT];            /* scan code, | 0x100 for E0-prefixed; 0 = none */
static int trigger = 64, stick = 12000;
static char ini[512];
static uint8_t held[256];            /* how many pad inputs hold each key down */
static uint32_t was;
static struct {
    char device[128];
    int on, axis, speed_axis, rest, pulled;
} plg;

int host_key_by_name(const char *s);  /* host.c */
const char *ini_get(const char *path, const char *sec, const char *key, const char *def, char *out, int n);

const char *pad_ini(void) { return ini; }

/* The keys follow the inputs that are on in `now` */
static void press(uint32_t now)
{
    /* two inputs on one key (the d-pad and the stick) press it once */
    for (int i = 0; i < IN_COUNT; i++) {
        int sc = map[i];
        if (!sc || !((now ^ was) & (1u << i))) continue;
        int down = (now >> i) & 1, k = sc & 0xFF;
        if (down ? held[k]++ : --held[k]) continue;
        if (sc & 0x100) kbd_scancode(0xE0);
        kbd_scancode((uint8_t)(k | (down ? 0 : 0x80)));
    }
    was = now;
}

#ifndef _WIN32
static void plunger_forget(void);
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

void pad_load(void)
{
    press(0);                        /* by the mapping that pressed them */
    snprintf(ini, sizeof ini, "%s/controller.ini", g_opt.savedir);
    FILE *f = fopen(ini, "rb");
    if (f) fclose(f);
    else if ((f = fopen(ini, "wb")) != NULL) { fputs(k_default, f); fclose(f); }
    char v[32];
    for (int i = 0; i < IN_COUNT; i++) {
        ini_get(ini, "controller", i < NBUTTONS ? k_buttons[i].name : k_extra[i - NBUTTONS], "none", v, sizeof v);
        map[i] = _stricmp(v, "none") ? host_key_by_name(v) : 0;
    }
    trigger = atoi(ini_get(ini, "controller", "trigger", "64", v, sizeof v));
    stick = atoi(ini_get(ini, "controller", "stick", "12000", v, sizeof v));
    plunger_load();
#ifndef _WIN32
    plunger_forget();
#endif
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

static void plunger_find(void)
{
    static uint64_t looked;
    static int said;
    if (looked && host_us() - looked < 500000) return;
    looked = host_us();
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        char id[33];
        const char *name = SDL_JoystickNameForIndex(i);
        SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i), id, sizeof id);
        if ((!name || _stricmp(plg.device, name)) && _stricmp(plg.device, id)) continue;
        SDL_Joystick *j = SDL_JoystickOpen(i);
        if (!j) continue;
        int n = SDL_JoystickNumAxes(j);
        if (plg.axis < n && plg.speed_axis < n) {
            pjoy = j;
            said = 0;
            fprintf(stderr, "[plunger] %s, axis %d\n", name ? name : id, plg.axis);
            return;
        }
        if (!said) fprintf(stderr, "[plunger] %s has %d axes, numbered from 0\n", name ? name : id, n);
        SDL_JoystickClose(j);
        said = 1;
        return;
    }
    if (!said) fprintf(stderr, "[plunger] no device called %s (--joysticks lists them); waiting for it\n", plg.device);
    said = 1;
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
            long pos = ((long)SDL_JoystickGetAxis(pjoy, plg.axis) - plg.rest) * PLUNGER_FULL / (plg.pulled - plg.rest);
            last.have = 1;
            last.pos = pos < 0 ? 0 : pos > PLUNGER_FULL ? PLUNGER_FULL : (int)pos;
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
    enum { MOST = 16, AXES = 16 };
    SDL_Joystick *j[MOST];
    int shown[MOST][AXES], n = SDL_NumJoysticks();
    if (n > MOST) n = MOST;
    if (!n) printf("No joysticks. (May this user read /dev/input? docs/cabinet-feedback.md)\n");
    for (int i = 0; i < n; i++) {
        char id[33];
        const char *name = SDL_JoystickNameForIndex(i);
        SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i), id, sizeof id);
        j[i] = SDL_JoystickOpen(i);
        printf("%d: %s\n   GUID %s, %04X:%04X, %d axes\n", i, name ? name : "?", id,
               SDL_JoystickGetDeviceVendor(i), SDL_JoystickGetDeviceProduct(i), j[i] ? SDL_JoystickNumAxes(j[i]) : 0);
        for (int a = 0; a < AXES; a++) shown[i][a] = 1 << 20;
    }
    if (n) printf("Move the plunger to see its axis and range. Ctrl+C ends.\n");
    fflush(stdout);
    for (uint64_t end = host_us() + (uint64_t)(seconds * 1e6); n && (seconds <= 0 || host_us() < end); ) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_QUIT) n = 0;
        SDL_JoystickUpdate();
        for (int i = 0; i < n; i++) {
            if (!j[i]) continue;
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
