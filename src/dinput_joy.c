/*
 * dinput_joy.c - Windows: a cabinet board's joystick through DirectInput 8,
 * for the game (pad.c) and the settings tool (config/), so both see the same
 * names, GUIDs and axis numbers.
 *
 * dij_count lists what is plugged in, then dij_name/dij_id/dij_is/dij_open
 * take an index into that list. dij_update reads every open one, after which
 * dij_attached, dij_axis (-32768 to 32767) and dij_button tell.
 */
#ifdef _WIN32
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dinput_joy.h"

/* DirectInput 8. Its axes are numbered X Y Z RX RY RZ as on Linux, then the
 * two sliders; a board without some of them has gaps, read as 0. Background
 * and non-exclusive: read whichever window has the keys, and shared with
 * whatever else reads the board. */
enum { JOY_MOST = 16 };
static LPDIRECTINPUT8A di;
static HWND di_wnd;                  /* SetCooperativeLevel wants a window: one that is never shown */
static int di_failed;
static struct { GUID inst, prod; char name[MAX_PATH]; } dl[JOY_MOST];
static int dn;

struct DiJoy {
    LPDIRECTINPUTDEVICE8A dev;
    DIJOYSTATE2 st;
    int axes, buttons, gone;
    uint8_t has[8];                  /* which of the axes it has */
    struct DiJoy *next;
};
static DiJoy *joys;                  /* the open ones */

static const DWORD k_axis_ofs[8] = {
    DIJOFS_X, DIJOFS_Y, DIJOFS_Z, DIJOFS_RX, DIJOFS_RY, DIJOFS_RZ, DIJOFS_SLIDER(0), DIJOFS_SLIDER(1) };

int dij_init(void)
{
    if (di) return 1;
    if (di_failed) return 0;
    HRESULT hr = DirectInput8Create(GetModuleHandleA(NULL), DIRECTINPUT_VERSION, &IID_IDirectInput8A, (void **)&di, NULL);
    if (FAILED(hr)) {
        di = NULL;
        di_failed = 1;
        fprintf(stderr, "DirectInput: error %08lX, so no joysticks\n", (unsigned long)hr);
        return 0;
    }
    di_wnd = CreateWindowExA(0, "STATIC", "epicpinball joysticks", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
    return 1;
}

static BOOL CALLBACK di_device(LPCDIDEVICEINSTANCEA d, LPVOID ctx)
{
    (void)ctx;
    if (dn >= JOY_MOST) return DIENUM_STOP;
    dl[dn].inst = d->guidInstance;
    dl[dn].prod = d->guidProduct;
    snprintf(dl[dn].name, sizeof dl[dn].name, "%s", d->tszProductName);
    dn++;
    return DIENUM_CONTINUE;
}

int dij_count(void)
{
    dn = 0;
    if (dij_init()) IDirectInput8_EnumDevices(di, DI8DEVCLASS_GAMECTRL, di_device, NULL, DIEDFL_ATTACHEDONLY);
    return dn;
}

const char *dij_name(int i) { return dl[i].name[0] ? dl[i].name : NULL; }

static void guid_text(const GUID *g, char *s, int n)
{
    snprintf(s, n, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", (unsigned long)g->Data1, g->Data2, g->Data3,
             g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

void dij_id(int i, char *id, int n) { guid_text(&dl[i].inst, id, n); }

/* with or without its braces */
static int guid_is(const char *device, const GUID *g)
{
    char s[40];
    guid_text(g, s, sizeof s);
    return !_stricmp(device, s) || (strlen(device) == 36 && !_strnicmp(device, s + 1, 36));
}

/* by its name, its instance GUID (--joysticks), or its product GUID */
int dij_is(int i, const char *device)
{
    return (dl[i].name[0] && !_stricmp(device, dl[i].name)) || guid_is(device, &dl[i].inst) || guid_is(device, &dl[i].prod);
}

/* A USB device's product GUID is its vendor and product IDs, then "PIDVID" */
int dij_usb(int i, uint16_t *vid, uint16_t *pid)
{
    if (memcmp(&dl[i].prod.Data4[2], "PIDVID", 6)) return *vid = *pid = 0;
    *vid = LOWORD(dl[i].prod.Data1);
    *pid = HIWORD(dl[i].prod.Data1);
    return 1;
}

static void dij_read(DiJoy *j)
{
    HRESULT hr = IDirectInputDevice8_Poll(j->dev);
    if (FAILED(hr)) {                /* not acquired yet, or lost: once more */
        IDirectInputDevice8_Acquire(j->dev);
        hr = IDirectInputDevice8_Poll(j->dev);
    }
    if (SUCCEEDED(hr)) hr = IDirectInputDevice8_GetDeviceState(j->dev, sizeof j->st, &j->st);
    if (FAILED(hr)) memset(&j->st, 0, sizeof j->st);
    j->gone = FAILED(hr);
}

void dij_close(DiJoy *j)
{
    for (DiJoy **p = &joys; *p; p = &(*p)->next)
        if (*p == j) { *p = j->next; break; }
    IDirectInputDevice8_Unacquire(j->dev);
    IDirectInputDevice8_Release(j->dev);
    free(j);
}

DiJoy *dij_open(int i)
{
    DiJoy *j = calloc(1, sizeof *j);
    if (!j || !di || FAILED(IDirectInput8_CreateDevice(di, &dl[i].inst, &j->dev, NULL))) { free(j); return NULL; }
    j->next = joys;
    joys = j;
    if (FAILED(IDirectInputDevice8_SetDataFormat(j->dev, &c_dfDIJoystick2))) { dij_close(j); return NULL; }
    if (di_wnd)                      /* without, DirectInput reads in the background anyway */
        IDirectInputDevice8_SetCooperativeLevel(j->dev, di_wnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    for (int a = 0; a < 8; a++) {    /* each axis it has to SDL's range; one it lacks is not found */
        DIPROPRANGE r;
        memset(&r, 0, sizeof r);
        r.diph.dwSize = sizeof r;
        r.diph.dwHeaderSize = sizeof r.diph;
        r.diph.dwObj = k_axis_ofs[a];
        r.diph.dwHow = DIPH_BYOFFSET;
        r.lMin = -32768;
        r.lMax = 32767;
        if (SUCCEEDED(IDirectInputDevice8_SetProperty(j->dev, DIPROP_RANGE, &r.diph))) {
            j->has[a] = 1;
            j->axes = a + 1;
        }
    }
    DIDEVCAPS caps;
    memset(&caps, 0, sizeof caps);
    caps.dwSize = sizeof caps;
    if (SUCCEEDED(IDirectInputDevice8_GetCapabilities(j->dev, &caps)))
        j->buttons = caps.dwButtons > 128 ? 128 : (int)caps.dwButtons;
    dij_read(j);
    if (j->gone) { dij_close(j); return NULL; }
    return j;
}

int dij_axes(DiJoy *j) { return j->axes; }
int dij_buttons(DiJoy *j) { return j->buttons; }
void dij_update(void) { for (DiJoy *j = joys; j; j = j->next) dij_read(j); }
int dij_attached(DiJoy *j) { return !j->gone; }

int dij_axis(DiJoy *j, int a)
{
    if (a < 0 || a >= 8 || !j->has[a]) return 0;
    return (int)*(const LONG *)((const uint8_t *)&j->st + k_axis_ofs[a]);
}

int dij_button(DiJoy *j, int b) { return b >= 0 && b < j->buttons && (j->st.rgbButtons[b] & 0x80); }

void dij_close_all(void) { while (joys) dij_close(joys); }
#endif
