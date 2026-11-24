/*
 * epicpinball-config - the settings tool (docs/config.md): the modes a run
 * starts with, the keys and a cabinet's buttons, its plunger and nudging
 * (with calibration), SSF's volumes and a speaker test, the DMD and DOF, and
 * an import of VPinball's settings. Dear ImGui on SDL2 (Linux), or on Win32
 * and Direct3D 11 with DirectInput's joysticks as the game reads them
 * (Windows).
 *
 * It writes epicpinball.ini and controller.ini in the save folder through
 * src/ini.c, key by key, so comments and anything it does not know stay. A
 * running game reads controller.ini again when its window comes to the
 * front; the modes are read when it starts.
 *
 *   epicpinball-config [--save DIR]      (default: saves, as the game's)
 *       --page N --shot FILE.bmp         page N (0-6) drawn, saved as a picture, and out:
 *                                        for the docs and for checking it with no one there
 */
extern "C" {
#include "ini.h"
#include "keys.h"
#include "vpx_import.h"
extern const char k_controller_default[];
}
#include "sim.h"
#include "imgui.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <dbt.h>
#include <d3d11.h>
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
extern "C" {
#include "dinput_joy.h"
}
#else
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <SDL.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
static void make_dir(const char *d) { _mkdir(d); }
#else
#include <strings.h>
static void make_dir(const char *d) { mkdir(d, 0777); }
#endif

static std::string g_save = "saves", g_ini, g_ctl, g_dmd_ini;

static bool exists(const std::string &p)
{
    FILE *f = fopen(p.c_str(), "rb");
    if (f) fclose(f);
    return f != nullptr;
}

#ifdef _WIN32
static uint32_t ticks() { return (uint32_t)GetTickCount64(); }
#else
static uint32_t ticks() { return SDL_GetTicks(); }
#endif
static bool ticks_passed(uint32_t now, uint32_t at) { return (int32_t)(at - now) <= 0; }

/* A program's output and stderr, line by line, and its exit code (-1 if it did not start) */
static int run_lines(const std::string &cmd, const std::function<void(const char *)> &line)
{
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa = { sizeof sa, nullptr, TRUE };
    HANDLE rd, wr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si = {};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = si.hStdError = wr;
    PROCESS_INFORMATION pi = {};
    std::string c = cmd;
    const BOOL ok = CreateProcessA(nullptr, &c[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); return -1; }
    std::string part;
    char b[512];
    DWORD n;
    while (ReadFile(rd, b, sizeof b, &n, nullptr) && n) {
        part.append(b, n);
        for (size_t e; (e = part.find('\n')) != std::string::npos; part.erase(0, e + 1)) line(part.substr(0, e + 1).c_str());
    }
    if (!part.empty()) line(part.c_str());
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD rc = 1;
    GetExitCodeProcess(pi.hProcess, &rc);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)rc;
#else
    FILE *p = popen((cmd + " 2>&1").c_str(), "r");
    if (!p) return -1;
    char b[512];
    while (fgets(b, sizeof b, p)) line(b);
    return pclose(p);
#endif
}

/* The game, beside this program (as the builds put it), else where a build leaves it */
static std::string game_exe()
{
#ifdef _WIN32
    char me[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, me, sizeof me);
    char *slash = n && n < sizeof me ? strrchr(me, '\\') : nullptr;
    if (slash) {
        strcpy(slash + 1, "epicpinball.exe");
        if (exists(me)) return me;
    }
    for (const char *c : { "build\\Release\\epicpinball.exe", "build-msvc\\epicpinball.exe" })
        if (exists(c)) return c;
#else
    if (exists("build-linux/epicpinball")) return "build-linux/epicpinball";
#endif
    return "";
}

/* ---- joysticks ------------------------------------------------------------------ */

/* As the game reads them (pad.c): SDL's on Linux, DirectInput's on Windows
 * (dinput_joy.c, the game's own), so names, GUIDs and axis numbers match */
struct Joy {
#ifdef _WIN32
    DiJoy *j;
#else
    SDL_Joystick *j;
#endif
    std::string name, guid;
    int axes, buttons;
    uint16_t vendor, product;
    std::vector<uint8_t> was;                        /* its buttons when last looked: "+ press" takes the next one down */
};
static std::vector<Joy> g_joys;

static void joys_close()
{
#ifdef _WIN32
    dij_close_all();
#else
    for (auto &j : g_joys) SDL_JoystickClose(j.j);
#endif
    g_joys.clear();
}

static void joys_refresh()
{
    joys_close();
#ifdef _WIN32
    for (int i = 0, count = dij_count(); i < count; i++) {
        DiJoy *j = dij_open(i);
        if (!j) continue;
        char g[48];
        dij_id(i, g, sizeof g);
        const char *n = dij_name(i);
        uint16_t v, p;
        dij_usb(i, &v, &p);
        g_joys.push_back({ j, n ? n : "?", g, dij_axes(j), dij_buttons(j), v, p, {} });
    }
#else
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        SDL_Joystick *j = SDL_JoystickOpen(i);
        if (!j) continue;
        char g[33];
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(j), g, sizeof g);
        const char *n = SDL_JoystickName(j);
        g_joys.push_back({ j, n ? n : "?", g, SDL_JoystickNumAxes(j), SDL_JoystickNumButtons(j), SDL_JoystickGetVendor(j),
                           SDL_JoystickGetProduct(j), {} });
    }
#endif
    for (auto &j : g_joys) j.was.assign(j.buttons, 0);
}

static void joys_update()
{
#ifdef _WIN32
    dij_update();
#else
    SDL_JoystickUpdate();
#endif
}

static bool button_value(Joy *j, int b)
{
    if (!j || b < 0 || b >= j->buttons) return false;
#ifdef _WIN32
    return dij_button(j->j, b) != 0;
#else
    return SDL_JoystickGetButton(j->j, b) != 0;
#endif
}

/* The game's rule (pad.c's joy_find): by name or GUID, ignoring case */
static Joy *joy_of(const std::string &dev)
{
    if (dev.empty() || !strcasecmp(dev.c_str(), "none")) return nullptr;
    for (auto &j : g_joys)
        if (!strcasecmp(dev.c_str(), j.name.c_str()) || !strcasecmp(dev.c_str(), j.guid.c_str())) return &j;
    return nullptr;
}

static int axis_value(Joy *j, int a)
{
    if (!j || a < 0 || a >= j->axes) return 0;
#ifdef _WIN32
    return dij_axis(j->j, a);
#else
    return SDL_JoystickGetAxis(j->j, a);
#endif
}

/* A VPinball device (an SDL GUID, or "" when it does not say) among those plugged in */
static Joy *joy_for_guid(const std::string &g)
{
    if (!g.empty()) {
        for (auto &j : g_joys) if (!strcasecmp(j.guid.c_str(), g.c_str())) return &j;
        /* the same board by vendor and product: SDL2's and SDL3's GUIDs differ, and
         * DirectInput's (Windows) are not SDL's. An SDL GUID has them little-endian
         * at bytes 4 and 8. */
        auto le16 = [&](size_t at) { return (uint16_t)strtoul((g.substr(at + 2, 2) + g.substr(at, 2)).c_str(), nullptr, 16); };
        if (g.size() == 32 && g.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos) {
            const uint16_t v = le16(8), p = le16(16);
            if (v)
                for (auto &j : g_joys)
                    if (j.vendor == v && j.product == p) return &j;
        }
    }
    if (g_joys.size() == 1) return &g_joys[0];
    for (auto &j : g_joys)                           /* a Pinscape */
        if (j.vendor == 0xFAFA || (j.vendor == 0x1209 && (j.product == 0xEAEA || j.product == 0xEAEB))) return &j;
    return nullptr;
}

/* ---- displays, numbered as the game numbers them ------------------------------------ */

struct Disp { std::string name; int x, y, w, h; };

#ifdef _WIN32
struct DispFound { Disp d; bool primary; };

static BOOL CALLBACK display_found(HMONITOR m, HDC, LPRECT, LPARAM lp)
{
    MONITORINFOEXA mi;
    mi.cbSize = sizeof mi;
    if (GetMonitorInfoA(m, (MONITORINFO *)&mi)) {
        const RECT &r = mi.rcMonitor;
        ((std::vector<DispFound> *)lp)->push_back({ { mi.szDevice, r.left, r.top, r.right - r.left, r.bottom - r.top },
                                                     (mi.dwFlags & MONITORINFOF_PRIMARY) != 0 });
    }
    return TRUE;
}

/* host.c's displays_list: the main one first, then left to right, top to bottom */
static std::vector<Disp> displays()
{
    std::vector<DispFound> f;
    EnumDisplayMonitors(nullptr, nullptr, display_found, (LPARAM)&f);
    if (f.size() > 16) f.resize(16);
    std::stable_sort(f.begin(), f.end(), [](const DispFound &a, const DispFound &b) {
        if (a.primary != b.primary) return a.primary;
        return a.d.x != b.d.x ? a.d.x < b.d.x : a.d.y < b.d.y;
    });
    std::vector<Disp> ds;
    for (auto &x : f) ds.push_back(x.d);
    return ds;
}
#else
static std::vector<Disp> displays()
{
    std::vector<Disp> ds;
    for (int i = 0; i < SDL_GetNumVideoDisplays(); i++) {
        SDL_Rect r = { 0 };
        SDL_GetDisplayBounds(i, &r);
        const char *n = SDL_GetDisplayName(i);
        ds.push_back({ n ? n : "?", r.x, r.y, r.w, r.h });
    }
    return ds;
}
#endif

/* ---- the settings ----------------------------------------------------------------- */

static const char *const k_modes[5] = { "cabinet", "dmd", "dmd_device", "ssf", "dof" };
static const char *const k_mode_labels[5] = { "Cabinet (--cabinet)", "DMD window (--dmd)", "DMD device (--dmd-device)",
                                              "SSF exciters (--ssf)", "DOF (--dof)" };
static const char *const k_scales[5] = { "sharp", "smooth", "crt", "nearest", "integer" };
static const char *const k_axes[6] = { "X", "Y", "Z", "RX", "RY", "RZ" };

struct Settings {
    int mode[5] = {};
    int scale = 0, vivid = 0, black_bars = 0, whole = 0, fullscreen = 0;
    int rotation = 0, stretch = 0, display = 0;
    float fx = 4, roll = 4;
    std::vector<int> keys[KEY_ACTIONS];
    std::string bdev;
    std::vector<int> buttons[KEY_ACTIONS];
    std::string pdev;
    int paxis = -1, pspeed = -1, rest = 0, pulled = 32767;
    std::string ndev;
    int nx = -1, ny = -1, dead = 1500, ix = 0, iy = 0, vel = 0, indicator = 1, range = 8000;
    float strength = 1, tilt = 1;
    int z_enabled = 1, z_bright = 6;
    std::string z_dev;
    bool z_set = false;                              /* DMDUtil.ini exists or was touched */
};

struct Entry { const std::string *file; std::string sec, key, value; };

static std::string num(double v)
{
    char b[32];
    snprintf(b, sizeof b, "%g", v);
    return b;
}

static std::string axis_text(int a) { return a < 0 ? "none" : a < 6 ? k_axes[a] : std::to_string(a); }

static int axis_parse(const char *s)
{
    if (!strcasecmp(s, "none") || !*s) return -1;
    for (int i = 0; i < 6; i++) if (!strcasecmp(s, k_axes[i])) return i;
    return isdigit((unsigned char)*s) ? atoi(s) : -1;
}

static std::string list_text(const std::vector<int> &v, bool keys)
{
    if (v.empty()) return "none";
    std::string s;
    for (int x : v) s += (s.empty() ? "" : " | ") + (keys ? std::string(key_name(x)) : std::to_string(x));
    return s;
}

/* Every setting as the file holds it: what Save compares and writes */
static std::vector<Entry> entries(const Settings &s)
{
    std::vector<Entry> e;
    auto E = [&](const std::string &f, const char *sec, const char *k, const std::string &v) { e.push_back({ &f, sec, k, v }); };
    for (int i = 0; i < 5; i++) E(g_ini, "modes", k_modes[i], s.mode[i] ? "1" : "0");
    E(g_ini, "display", "scale", k_scales[s.scale]);
    E(g_ini, "display", "look", s.vivid ? "vivid" : "natural");
    E(g_ini, "display", "bars", s.black_bars ? "black" : "blur");
    E(g_ini, "display", "whole_table", s.whole ? "1" : "0");
    E(g_ini, "display", "fullscreen", s.fullscreen ? "1" : "0");
    E(g_ini, "cabinet", "rotation", std::to_string(s.rotation * 90));
    E(g_ini, "cabinet", "stretch", s.stretch ? "1" : "0");
    E(g_ini, "cabinet", "playfield_display", std::to_string(s.display));
    E(g_ini, "ssf", "effects_volume", num(s.fx));
    E(g_ini, "ssf", "rolling_volume", num(s.roll));
    for (int a = 0; a < KEY_ACTIONS; a++) E(g_ctl, "keys", k_actions[a].name, list_text(s.keys[a], true));
    E(g_ctl, "buttons", "device", s.bdev.empty() ? "none" : s.bdev);
    for (int a = 0; a < KEY_ACTIONS; a++) E(g_ctl, "buttons", k_actions[a].name, list_text(s.buttons[a], false));
    E(g_ctl, "plunger", "device", s.pdev.empty() ? "none" : s.pdev);
    E(g_ctl, "plunger", "axis", axis_text(s.paxis));
    E(g_ctl, "plunger", "speed_axis", axis_text(s.pspeed));
    E(g_ctl, "plunger", "rest", std::to_string(s.rest));
    E(g_ctl, "plunger", "pulled", std::to_string(s.pulled));
    E(g_ctl, "nudge", "device", s.ndev.empty() ? "none" : s.ndev);
    E(g_ctl, "nudge", "x_axis", axis_text(s.nx));
    E(g_ctl, "nudge", "y_axis", axis_text(s.ny));
    E(g_ctl, "nudge", "dead", std::to_string(s.dead));
    E(g_ctl, "nudge", "strength", num(s.strength));
    E(g_ctl, "nudge", "tilt", num(s.tilt));
    E(g_ctl, "nudge", "invert_x", s.ix ? "1" : "0");
    E(g_ctl, "nudge", "invert_y", s.iy ? "1" : "0");
    E(g_ctl, "nudge", "is_velocity", s.vel ? "1" : "0");
    E(g_ctl, "nudge", "indicator", s.indicator ? "1" : "0");
    E(g_ctl, "nudge", "indicator_range", std::to_string(s.range));
    if (s.z_set) {
        E(g_dmd_ini, "ZeDMD", "Enabled", s.z_enabled ? "1" : "0");
        E(g_dmd_ini, "ZeDMD", "Device", s.z_dev);
        E(g_dmd_ini, "ZeDMD", "Brightness", std::to_string(s.z_bright));
    }
    return e;
}

static std::string get(const std::string &f, const char *sec, const char *key, const char *def)
{
    char v[512];
    return ini_get(f.c_str(), sec, key, def, v, sizeof v);
}

static Settings load()
{
    Settings s;
    for (int i = 0; i < 5; i++) s.mode[i] = atoi(get(g_ini, "modes", k_modes[i], "0").c_str()) != 0;
    const std::string sc = get(g_ini, "display", "scale", "sharp");
    for (int i = 0; i < 5; i++) if (!strcasecmp(sc.c_str(), k_scales[i])) s.scale = i;
    s.vivid = !strcasecmp(get(g_ini, "display", "look", "natural").c_str(), "vivid");
    s.black_bars = !strcasecmp(get(g_ini, "display", "bars", "blur").c_str(), "black");
    s.whole = atoi(get(g_ini, "display", "whole_table", "0").c_str()) != 0;
    s.fullscreen = atoi(get(g_ini, "display", "fullscreen", "0").c_str()) != 0;
    s.rotation = (atoi(get(g_ini, "cabinet", "rotation", "0").c_str()) / 90) & 3;
    s.stretch = atoi(get(g_ini, "cabinet", "stretch", "0").c_str()) != 0;
    s.display = atoi(get(g_ini, "cabinet", "playfield_display", "0").c_str());
    s.fx = (float)atof(get(g_ini, "ssf", "effects_volume", "4").c_str());
    s.roll = (float)atof(get(g_ini, "ssf", "rolling_volume", "4").c_str());
    for (int a = 0; a < KEY_ACTIONS; a++) {
        int k[8];
        const int n = key_list(get(g_ctl, "keys", k_actions[a].name, "none").c_str(), k, 8);
        for (int i = 0; i < n; i++) s.keys[a].push_back(k[i]);
        const std::string b = get(g_ctl, "buttons", k_actions[a].name, "none");
        for (const char *p = b.c_str(); *p; ) {
            while (*p == ' ' || *p == '|' || *p == ',') p++;
            if (!isdigit((unsigned char)*p)) break;
            char *e;
            s.buttons[a].push_back((int)strtol(p, &e, 10));
            p = e;
        }
    }
    auto dev = [](const std::string &d) { return strcasecmp(d.c_str(), "none") ? d : std::string(); };
    s.bdev = dev(get(g_ctl, "buttons", "device", "none"));
    s.pdev = dev(get(g_ctl, "plunger", "device", "none"));
    s.paxis = axis_parse(get(g_ctl, "plunger", "axis", "none").c_str());
    s.pspeed = axis_parse(get(g_ctl, "plunger", "speed_axis", "none").c_str());
    s.rest = atoi(get(g_ctl, "plunger", "rest", "0").c_str());
    s.pulled = atoi(get(g_ctl, "plunger", "pulled", "32767").c_str());
    s.ndev = dev(get(g_ctl, "nudge", "device", "none"));
    s.nx = axis_parse(get(g_ctl, "nudge", "x_axis", "none").c_str());
    s.ny = axis_parse(get(g_ctl, "nudge", "y_axis", "none").c_str());
    s.dead = atoi(get(g_ctl, "nudge", "dead", "1500").c_str());
    s.strength = (float)atof(get(g_ctl, "nudge", "strength", "1").c_str());
    s.tilt = (float)atof(get(g_ctl, "nudge", "tilt", "1").c_str());
    s.ix = atoi(get(g_ctl, "nudge", "invert_x", "0").c_str()) != 0;
    s.iy = atoi(get(g_ctl, "nudge", "invert_y", "0").c_str()) != 0;
    s.vel = atoi(get(g_ctl, "nudge", "is_velocity", "0").c_str()) != 0;
    s.indicator = atoi(get(g_ctl, "nudge", "indicator", "1").c_str()) != 0;
    s.range = std::clamp(atoi(get(g_ctl, "nudge", "indicator_range", "8000").c_str()), 100, 32767);
    if ((s.z_set = exists(g_dmd_ini))) {
        s.z_enabled = atoi(get(g_dmd_ini, "ZeDMD", "Enabled", "1").c_str()) != 0;
        s.z_dev = get(g_dmd_ini, "ZeDMD", "Device", "");
        s.z_bright = atoi(get(g_dmd_ini, "ZeDMD", "Brightness", "6").c_str());
    }
    return s;
}

static Settings g_s, g_saved;
static std::string g_status;

static bool dirty()
{
    const auto a = entries(g_s), b = entries(g_saved);
    if (a.size() != b.size()) return true;
    for (size_t i = 0; i < a.size(); i++) if (a[i].value != b[i].value) return true;
    return false;
}

/* What changed, key by key: everything else in the files stays as it is */
static void save()
{
    const auto now = entries(g_s), was = entries(g_saved);
    int n = 0, bad = 0;
    if (g_s.z_set) make_dir((g_save + "/dmd").c_str());
    for (size_t i = 0; i < now.size(); i++) {
        if (i < was.size() && now[i].value == was[i].value && now[i].key == was[i].key) continue;
        if (ini_set(now[i].file->c_str(), now[i].sec.c_str(), now[i].key.c_str(), now[i].value.c_str())) n++;
        else bad++;
    }
    g_saved = g_s;
    g_status = bad ? "Could not write " + std::to_string(bad) + " settings to " + g_save
                   : "Saved " + std::to_string(n) + " settings. A running game takes keys, buttons, plunger and nudging "
                     "when its window next comes to the front; modes at its next start.";
}

/* ---- small drawing helpers ------------------------------------------------------- */

static void help(const char *t)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30);
        ImGui::TextUnformatted(t);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

/* A device picker: "none" and every joystick plugged in, and the one set if it is not */
static bool device_combo(const char *label, std::string &dev)
{
    bool changed = false;
    if (ImGui::BeginCombo(label, dev.empty() ? "none" : dev.c_str())) {
        if (ImGui::Selectable("none", dev.empty())) { dev.clear(); changed = true; }
        for (auto &j : g_joys) {
            char l[200];
            snprintf(l, sizeof l, "%s  (%d axes, %d buttons)##%s", j.name.c_str(), j.axes, j.buttons, j.guid.c_str());
            if (ImGui::Selectable(l, joy_of(dev) == &j)) { dev = j.name; changed = true; }
        }
        ImGui::EndCombo();
    }
    if (!dev.empty() && !joy_of(dev)) { ImGui::SameLine(); ImGui::TextColored({ 1, 0.6f, 0.2f, 1 }, "not plugged in"); }
    return changed;
}

static bool axis_combo(const char *label, int &axis, Joy *j)
{
    bool changed = false;
    if (ImGui::BeginCombo(label, axis_text(axis).c_str())) {
        if (ImGui::Selectable("none", axis < 0)) { axis = -1; changed = true; }
        const int n = j ? j->axes : 6;
        for (int a = 0; a < n; a++) {
            char l[64];
            snprintf(l, sizeof l, "%d  %s%s", a, a < 6 ? k_axes[a] : "", j ? ("   " + std::to_string(axis_value(j, a))).c_str() : "");
            if (ImGui::Selectable(l, axis == a)) { axis = a; changed = true; }
        }
        ImGui::EndCombo();
    }
    return changed;
}

/* Every axis of a joystick as a bar from -32768 to 32767, one highlighted */
static void axis_bars(Joy *j, int mark)
{
    if (!j) return;
    for (int a = 0; a < j->axes && a < 16; a++) {
        const int v = axis_value(j, a);
        char l[64];
        snprintf(l, sizeof l, "%d %-2s %6d", a, a < 6 ? k_axes[a] : "", v);
        if (a == mark) ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(1, 0.75f, 0.1f, 1));
        ImGui::ProgressBar((v + 32768) / 65535.0f, ImVec2(360, 0), l);
        if (a == mark) ImGui::PopStyleColor();
    }
}

/* ---- pages ---------------------------------------------------------------------------- */

/* how a helper is built (docs/dmd.md, docs/cabinet-feedback.md) */
#ifdef _WIN32
#define HELPER_BUILD(dir) dir "\\build.ps1"
#else
#define HELPER_BUILD(dir) dir "/build.sh"
#endif

static void page_modes()
{
    ImGui::SeparatorText("Every run with a window starts with");
    const bool helper[5] = { true, true, exists("dmd/build/epicpinball-dmd") || exists("dmd/build/epicpinball-dmd.exe"), true,
                             exists("dof/build/epicpinball-dof") || exists("dof/build/epicpinball-dof.exe") };
    const char *why[5] = { nullptr, nullptr, "its helper is not built: " HELPER_BUILD("dmd") " (docs/dmd.md)", nullptr,
                           "its helper is not built: " HELPER_BUILD("dof") " (docs/cabinet-feedback.md)" };
    for (int i = 0; i < 5; i++) {
        bool on = g_s.mode[i];
        if (ImGui::Checkbox(k_mode_labels[i], &on)) g_s.mode[i] = on;
        if (!helper[i]) { ImGui::SameLine(); ImGui::TextColored({ 1, 0.6f, 0.2f, 1 }, "%s", why[i]); }
    }
    ImGui::TextDisabled("For one run without one: --no-cabinet, --no-dmd, --no-dmd-device, --no-ssf, --no-dof");

    ImGui::SeparatorText("Picture");
    ImGui::SetNextItemWidth(200);
    ImGui::Combo("Scaling", &g_s.scale, k_scales, 5);
    bool b = g_s.vivid;
    if (ImGui::Checkbox("Vivid colours", &b)) g_s.vivid = b;
    b = g_s.black_bars;
    if (ImGui::Checkbox("Black beside the picture (else blurred)", &b)) g_s.black_bars = b;
    b = g_s.whole;
    if (ImGui::Checkbox("The whole table, no scrolling", &b)) g_s.whole = b;
    b = g_s.fullscreen;
    if (ImGui::Checkbox("Fullscreen", &b)) g_s.fullscreen = b;

    ImGui::SeparatorText("Cabinet");
    static const char *const turns[4] = { "0", "90", "180", "270" };
    ImGui::SetNextItemWidth(200);
    ImGui::Combo("Playfield turned (degrees, F6)", &g_s.rotation, turns, 4);
    b = g_s.stretch;
    if (ImGui::Checkbox("Playfield stretched to its display (F5)", &b)) g_s.stretch = b;
    ImGui::SetNextItemWidth(400);
    char cur[160];
    const std::vector<Disp> ds = displays();
    if (g_s.display >= 0 && g_s.display < (int)ds.size()) {
        const Disp &d = ds[g_s.display];
        snprintf(cur, sizeof cur, "%d: %s %dx%d", g_s.display, d.name.c_str(), d.w, d.h);
    } else snprintf(cur, sizeof cur, "%d: (not connected)", g_s.display);
    if (ImGui::BeginCombo("Playfield display", cur)) {
        for (int i = 0; i < (int)ds.size(); i++) {
            snprintf(cur, sizeof cur, "%d: %s %dx%d at %d,%d", i, ds[i].name.c_str(), ds[i].w, ds[i].h, ds[i].x, ds[i].y);
            if (ImGui::Selectable(cur, g_s.display == i)) g_s.display = i;
        }
        ImGui::EndCombo();
    }
}

static int g_capture_key = -1, g_capture_button = -1;     /* the action waiting for a key or button */

static void page_keys()
{
    ImGui::TextWrapped("Each action's own key always works. Keys added here do the same (a cabinet's buttons that "
                       "type keys, or VPinball's layout). Click a key to remove it.");
    if (ImGui::BeginTable("keys", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 260);
        ImGui::TableSetupColumn("Its own key", ImGuiTableColumnFlags_WidthFixed, 110);
        ImGui::TableSetupColumn("Added keys");
        ImGui::TableHeadersRow();
        for (int a = 0; a < KEY_ACTIONS; a++) {
            ImGui::PushID(a);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(k_actions[a].label);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", key_name(k_actions[a].sc));
            ImGui::TableNextColumn();
            for (size_t i = 0; i < g_s.keys[a].size(); i++) {
                ImGui::PushID((int)i);
                if (ImGui::SmallButton(key_name(g_s.keys[a][i]))) { g_s.keys[a].erase(g_s.keys[a].begin() + i); ImGui::PopID(); break; }
                ImGui::PopID();
                ImGui::SameLine();
            }
            if (g_capture_key == a) {
                ImGui::TextColored({ 1, 0.85f, 0.2f, 1 }, "press a key...");
                ImGui::SameLine();
                if (ImGui::SmallButton("cancel")) g_capture_key = -1;
            } else if (ImGui::SmallButton("+ add")) g_capture_key = a;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Remove all added keys")) for (auto &k : g_s.keys) k.clear();
}

static void page_buttons()
{
    ImGui::TextWrapped("A cabinet's buttons on a joystick board (a Pinscape): each held button holds its action's key. "
                       "\"+ press\" takes the next button pressed, on any board, and picks that board.");
    device_combo("Board##buttons", g_s.bdev);
    Joy *j = joy_of(g_s.bdev);
    if (j) {
        ImGui::Text("Buttons now:");
        for (int b = 0; b < j->buttons && b < 64; b++) {
            if (b % 16) ImGui::SameLine();
            const bool d = button_value(j, b);
            ImGui::PushStyleColor(ImGuiCol_Button, d ? ImVec4(1, 0.75f, 0.1f, 1) : ImVec4(0.2f, 0.22f, 0.27f, 1));
            char l[8];
            snprintf(l, sizeof l, "%d", b);
            ImGui::Button(l, ImVec2(34, 0));
            ImGui::PopStyleColor();
        }
    }
    if (ImGui::BeginTable("buttons", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 260);
        ImGui::TableSetupColumn("Buttons");
        ImGui::TableHeadersRow();
        for (int a = 0; a < KEY_ACTIONS; a++) {
            ImGui::PushID(a);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(k_actions[a].label);
            ImGui::TableNextColumn();
            for (size_t i = 0; i < g_s.buttons[a].size(); i++) {
                ImGui::PushID((int)i);
                if (ImGui::SmallButton(("button " + std::to_string(g_s.buttons[a][i])).c_str())) {
                    g_s.buttons[a].erase(g_s.buttons[a].begin() + i);
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
                ImGui::SameLine();
            }
            if (g_capture_button == a) {
                ImGui::TextColored({ 1, 0.85f, 0.2f, 1 }, "press a button...");
                ImGui::SameLine();
                if (ImGui::SmallButton("cancel")) g_capture_button = -1;
            } else if (ImGui::SmallButton("+ press")) g_capture_button = a;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

/* The plunger as it stands in its lane: the rod's tip, the spring behind it, the knob */
static void draw_plunger(int pos)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const float w = 90, h = 300, cx = o.x + w / 2, top = o.y + 10, travel = 110;
    const float tip = top + 20 + travel * pos / 1000.0f, base = top + 180;
    dl->AddRectFilled({ o.x + 15, top }, { o.x + w - 15, base + 10 }, IM_COL32(40, 44, 52, 255), 6);   /* the lane */
    dl->AddRectFilled({ cx - 16, base }, { cx + 16, base + 10 }, IM_COL32(120, 120, 130, 255));        /* the housing */
    const int coils = 9;                                                                                 /* the spring */
    for (int i = 0; i < coils; i++) {
        const float y0 = tip + 14 + (base - tip - 14) * i / coils, y1 = tip + 14 + (base - tip - 14) * (i + 1) / coils;
        dl->AddLine({ cx - 13, y0 }, { cx + 13, (y0 + y1) / 2 }, IM_COL32(200, 200, 210, 255), 2);
        dl->AddLine({ cx + 13, (y0 + y1) / 2 }, { cx - 13, y1 }, IM_COL32(200, 200, 210, 255), 2);
    }
    dl->AddRectFilled({ cx - 4, tip }, { cx + 4, base + 60 + travel * pos / 1000.0f }, IM_COL32(220, 220, 225, 255));   /* the rod */
    dl->AddRectFilled({ cx - 14, tip }, { cx + 14, tip + 14 }, IM_COL32(230, 180, 40, 255), 3);        /* its tip */
    dl->AddCircleFilled({ cx, base + 70 + travel * pos / 1000.0f }, 14, IM_COL32(200, 40, 40, 255));   /* the knob */
    ImGui::Dummy(ImVec2(w, h + 30));
}

struct Learn { int phase = 0; uint32_t until = 0; int lo[16], hi[16], start[16]; };
static Learn g_find;

static void page_plunger()
{
    ImGui::TextWrapped("A plunger sensor read as a joystick's axis (a Pinscape's Z). Pick the board, then \"Find the "
                       "axis\" and pull the plunger: the axis that moves is taken, with where it rests and how far it goes.");
    device_combo("Board##plunger", g_s.pdev);
    Joy *j = joy_of(g_s.pdev);
    ImGui::SetNextItemWidth(200);
    axis_combo("Axis", g_s.paxis, j);
    ImGui::SameLine();
    if (g_find.phase == 0 && ImGui::Button("Find the axis") && j) {
        g_find.phase = 1;
        g_find.until = ticks() + 5000;
        for (int a = 0; a < j->axes && a < 16; a++) g_find.lo[a] = g_find.hi[a] = g_find.start[a] = axis_value(j, a);
    }
    if (g_find.phase == 1 && j) {
        ImGui::TextColored({ 1, 0.85f, 0.2f, 1 }, "Pull the plunger right back and let it go... (%.0f s)",
                           (g_find.until - ticks()) / 1000.0);
        int best = -1, range = 0;
        for (int a = 0; a < j->axes && a < 16; a++) {
            const int v = axis_value(j, a);
            g_find.lo[a] = std::min(g_find.lo[a], v);
            g_find.hi[a] = std::max(g_find.hi[a], v);
            if (g_find.hi[a] - g_find.lo[a] > range) { range = g_find.hi[a] - g_find.lo[a]; best = a; }
        }
        if (ticks_passed(ticks(), g_find.until)) {
            g_find.phase = 0;
            if (best >= 0 && range >= 4000) {
                g_s.paxis = best;
                g_s.rest = g_find.start[best];
                g_s.pulled = abs(g_find.hi[best] - g_s.rest) > abs(g_find.lo[best] - g_s.rest) ? g_find.hi[best] : g_find.lo[best];
                g_status = "The plunger is axis " + axis_text(best) + ": rest " + std::to_string(g_s.rest) + ", pulled "
                           + std::to_string(g_s.pulled) + ". Check them below, then Save.";
            } else g_status = "No axis moved far enough: is it the right board?";
        }
    }
    ImGui::SetNextItemWidth(200);
    axis_combo("Speed axis (boards that report it)", g_s.pspeed, j);

    ImGui::SeparatorText("Calibration");
    ImGui::SetNextItemWidth(200);
    ImGui::InputInt("At rest", &g_s.rest, 100, 1000);
    ImGui::SameLine();
    if (ImGui::Button("Set: it is at rest now") && j) g_s.rest = axis_value(j, g_s.paxis);
    ImGui::SetNextItemWidth(200);
    ImGui::InputInt("Pulled right back", &g_s.pulled, 100, 1000);
    ImGui::SameLine();
    if (ImGui::Button("Set: it is pulled back now") && j) g_s.pulled = axis_value(j, g_s.paxis);
    g_s.rest = std::clamp(g_s.rest, -32768, 32767);
    g_s.pulled = std::clamp(g_s.pulled, -32768, 32767);
    if (g_s.paxis >= 0 && abs(g_s.pulled - g_s.rest) < 1000)
        ImGui::TextColored({ 1, 0.4f, 0.3f, 1 }, "At rest and pulled are under 1000 apart: the game turns the plunger off.");

    ImGui::Separator();
    ImGui::BeginGroup();
    const int raw = axis_value(j, g_s.paxis), pos = j && g_s.paxis >= 0 ? sim_plunger(raw, g_s.rest, g_s.pulled) : 0;
    draw_plunger(pos);
    ImGui::Text("%d%% pulled", pos / 10);
    ImGui::EndGroup();
    ImGui::SameLine(0, 40);
    ImGui::BeginGroup();
    if (j) axis_bars(j, g_s.paxis);
    else ImGui::TextDisabled("No board picked, or it is not plugged in.");
    ImGui::EndGroup();
}

/* The indicator as the game draws it, bigger, with the dead zone and a trail */
static void draw_indicator(float r, double nx, double ny, double dz)
{
    static ImVec2 trail[12];
    static int tn;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetCursorScreenPos(), c = { o.x + r + 4, o.y + r + 4 };
    const double len = sqrt(nx * nx + ny * ny);
    if (len > 1) { nx /= len; ny /= len; }
    for (int i = 11; i > 0; i--) trail[i] = trail[i - 1];
    trail[0] = { (float)nx, (float)ny };
    if (tn < 12) tn++;
    dl->AddCircleFilled(c, r, IM_COL32(20, 22, 28, 255), 64);
    dl->AddCircle(c, r, IM_COL32(240, 240, 240, 220), 64, 2);
    if (dz > 0 && dz < 1) dl->AddCircle(c, (float)(r * dz), IM_COL32(150, 150, 150, 160), 48, 1.5f);
    dl->AddLine({ c.x - r, c.y }, { c.x + r, c.y }, IM_COL32(80, 80, 90, 255));
    dl->AddLine({ c.x, c.y - r }, { c.x, c.y + r }, IM_COL32(80, 80, 90, 255));
    for (int i = tn - 1; i >= 0; i--)
        dl->AddCircleFilled({ c.x + trail[i].x * r, c.y + trail[i].y * r }, i ? 4.0f : 8.0f,
                            IM_COL32(255, 192, 32, i ? 160 * (12 - i) / 12 : 255));
    ImGui::Dummy(ImVec2(2 * r + 8, 2 * r + 8));
}

struct Calib {
    int step = 0;                         /* 0 idle, 1 still, 2 front, 3 left, 4 hard */
    uint32_t until = 0;
    int peak[16], first[16], noise[16];
    long sum[16];
    int samples = 0;
};
static Calib g_cal;

static void calib_start(int step, Joy *j, uint32_t ms)
{
    g_cal.step = step;
    g_cal.until = ticks() + ms;
    g_cal.samples = 0;
    for (int a = 0; a < 16; a++) g_cal.peak[a] = g_cal.first[a] = g_cal.noise[a] = 0, g_cal.sum[a] = 0;
    (void)j;
}

static void calib_run(Joy *j)
{
    if (!g_cal.step || !j) return;
    const int axes = std::min(j->axes, 16);
    for (int a = 0; a < axes; a++) {
        const int v = axis_value(j, a);
        g_cal.sum[a] += v;
        g_cal.noise[a] = std::max(g_cal.noise[a], abs(v));
        if (abs(v) > abs(g_cal.peak[a])) g_cal.peak[a] = v;
        if (!g_cal.first[a] && abs(v) > 6000) g_cal.first[a] = v;   /* which way it went first */
    }
    g_cal.samples++;
    if (!ticks_passed(ticks(), g_cal.until)) return;

    auto strongest = [&](int skip) {
        int best = -1;
        for (int a = 0; a < axes; a++)
            if (a != skip && g_cal.first[a] && (best < 0 || abs(g_cal.peak[a]) > abs(g_cal.peak[best]))) best = a;
        return best;
    };
    const int step = g_cal.step;
    g_cal.step = 0;
    if (step == 1) {                                  /* still: the noise is the dead zone */
        int worst = 0, off = 0;
        for (int a = 0; a < axes; a++) {
            if ((g_s.nx >= 0 && a != g_s.nx && a != g_s.ny) || (g_s.nx < 0 && g_s.ny >= 0 && a != g_s.ny)) continue;
            worst = std::max(worst, g_cal.noise[a]);
            off = std::max(off, (int)labs(g_cal.sum[a] / std::max(1, g_cal.samples)));
        }
        g_s.dead = std::min(30000, worst * 3 / 2 + 300);
        g_status = "Dead zone " + std::to_string(g_s.dead) + " (the noise at rest was up to " + std::to_string(worst) + ").";
        if (off > 2000) g_status += " The reading rests at " + std::to_string(off) + ", not 0: re-centre the board.";
        calib_start(2, j, 8000);
    } else if (step == 2) {                           /* from the front: the cabinet goes away, y < 0 */
        const int a = strongest(-1);
        if (a < 0) { g_status = "No shove seen: try again, harder."; return; }
        g_s.ny = a;
        g_s.iy = g_cal.first[a] > 0;
        g_status = "Along the cabinet: axis " + axis_text(a) + (g_s.iy ? ", inverted." : ".");
        calib_start(3, j, 8000);
    } else if (step == 3) {                           /* from the left: the cabinet goes right, x > 0 */
        const int a = strongest(g_s.ny);
        if (a < 0) { g_status = "No shove seen: try again, harder."; return; }
        g_s.nx = a;
        g_s.ix = g_cal.first[a] < 0;
        g_status = "Across the cabinet: axis " + axis_text(a) + (g_s.ix ? ", inverted." : ".");
        calib_start(4, j, 6000);
    } else if (step == 4) {                           /* a hard shove: full scale is the game's own nudge */
        int m = std::max(abs(g_cal.peak[std::max(g_s.nx, 0)]), abs(g_cal.peak[std::max(g_s.ny, 0)]));
        if (m < 3000) { g_status = "No hard shove seen; strength left as it is."; return; }
        g_s.strength = (float)std::clamp(32767.0 / m, 0.5, 10.0);
        g_s.range = std::clamp(m, 1000, 32767);
        g_status = "Done. A hard shove read " + std::to_string(m) + ": strength " + num(g_s.strength)
                   + ", so it pushes about as the game's own nudge, and the indicator's range " + std::to_string(g_s.range)
                   + ", so it reaches the ring. Try it below, then Save.";
    }
}

static void page_nudge()
{
    ImGui::TextWrapped("A cabinet's accelerometer (a Pinscape's X and Y) pushes the ball by how hard the cabinet is "
                       "shoved. Some boards report the cabinet's velocity instead: tick Velocity input for those.");
    device_combo("Board##nudge", g_s.ndev);
    Joy *j = joy_of(g_s.ndev);
    ImGui::SetNextItemWidth(200);
    axis_combo("Across (X)", g_s.nx, j);
    ImGui::SameLine();
    bool b = g_s.ix;
    if (ImGui::Checkbox("Invert X", &b)) g_s.ix = b;
    ImGui::SetNextItemWidth(200);
    axis_combo("Along (Y)", g_s.ny, j);
    ImGui::SameLine();
    b = g_s.iy;
    if (ImGui::Checkbox("Invert Y", &b)) g_s.iy = b;
    b = g_s.vel;
    if (ImGui::Checkbox("Velocity input", &b)) g_s.vel = b;
    help("is_velocity: the board reports the cabinet's velocity, not its acceleration. The push is then how much the "
         "velocity changed since the step before.");
    b = g_s.indicator;
    if (ImGui::Checkbox("Show the indicator on the playfield", &b)) g_s.indicator = b;
    ImGui::SetNextItemWidth(300);
    ImGui::SliderInt("Indicator range", &g_s.range, 1000, 32767);
    help("indicator_range: the reading that takes the dot to the ring, here and in the game. Lower shows small "
         "shoves bigger; more than it stays on the ring. Only the indicator: the push is set by Strength.");
    ImGui::SetNextItemWidth(300);
    ImGui::SliderInt("Dead zone", &g_s.dead, 0, 30000);
    ImGui::SetNextItemWidth(300);
    ImGui::SliderFloat("Strength", &g_s.strength, 0, 10, "%.2f");
    help("1: a full reading pushes as the game's own nudge (with velocity input: a full-scale change does).");
    ImGui::SetNextItemWidth(300);
    ImGui::SliderFloat("Tilt", &g_s.tilt, 0, 10, "%.2f");
    help("How fast shoves fill the game's tilt meter; 0 never tilts.");

    ImGui::SeparatorText("Calibration");
    static const char *const say[5] = { "", "Keep the cabinet still...", "Shove the cabinet from the front, towards the backbox...",
                                        "Shove the cabinet from the left side...", "Give it one hard shove, any way..." };
    calib_run(j);
    if (!j) ImGui::TextDisabled("Pick the board first.");
    else if (!g_cal.step) {
        if (ImGui::Button("Calibrate")) calib_start(1, j, 3000);
        help("Four steps: still for 3 s (the dead zone), a shove from the front (which axis is along the cabinet, "
             "and which way), one from the left (across), and a hard one (the strength and the indicator's range).");
    } else {
        ImGui::TextColored({ 1, 0.85f, 0.2f, 1 }, "%d/4  %s  (%.0f s)", g_cal.step, say[g_cal.step],
                           (g_cal.until - ticks()) / 1000.0);
        ImGui::SameLine();
        if (ImGui::Button("Stop")) g_cal.step = 0;
    }

    ImGui::SeparatorText("Try it");
    int x = axis_value(j, g_s.nx), y = axis_value(j, g_s.ny);
    if (g_s.ix) x = -x;
    if (g_s.iy) y = -y;
    const bool on = j && (g_s.nx >= 0 || g_s.ny >= 0);
    sim_nudge_settings(g_s.dead, g_s.strength, g_s.tilt, g_s.ix, g_s.iy, g_s.vel);
    static uint32_t last;
    const uint32_t now = ticks();
    for (int n = 0; last && n < 20 && ticks_passed(now, last + 5); n++, last += 5)   /* 200 physics steps a second */
        sim_nudge_step(on, axis_value(j, g_s.nx), axis_value(j, g_s.ny));
    if (!last || now - last > 100) last = now;
    ImGui::BeginGroup();
    draw_indicator(110, (double)x / g_s.range, (double)y / g_s.range, (double)g_s.dead / g_s.range);
    ImGui::Text("%s %6d, %6d", g_s.vel ? "velocity" : "reading ", x, y);
    ImGui::EndGroup();
    ImGui::SameLine(0, 30);
    ImGui::BeginGroup();
    double bx, by;
    sim_ball(&bx, &by);
    ImGui::Text("What the ball feels");
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 o = ImGui::GetCursorScreenPos(), c = { o.x + 110, o.y + 110 };
        dl->AddRectFilled(o, { o.x + 220, o.y + 220 }, IM_COL32(28, 60, 36, 255), 8);
        const float k = 110.0f / 2000;
        dl->AddLine(c, { c.x + std::clamp((float)bx * k, -105.0f, 105.0f), c.y + std::clamp((float)by * k, -105.0f, 105.0f) },
                    IM_COL32(255, 255, 255, 200), 3);
        dl->AddCircleFilled({ c.x + std::clamp((float)bx * k, -105.0f, 105.0f), c.y + std::clamp((float)by * k, -105.0f, 105.0f) },
                            9, IM_COL32(210, 210, 220, 255));
        ImGui::Dummy(ImVec2(220, 220));
    }
    const int meter = sim_meter();
    ImGui::Text("Tilt meter");
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, sim_tilted() ? ImVec4(1, 0.25f, 0.2f, 1) : ImVec4(0.3f, 0.7f, 1, 1));
    ImGui::ProgressBar(std::min(1.0f, meter / 80.0f), ImVec2(220, 0), sim_tilted() ? "TILT" : "");
    ImGui::PopStyleColor();
    ImGui::EndGroup();
    if (j) {
        ImGui::SameLine(0, 30);
        ImGui::BeginGroup();
        axis_bars(j, -1);
        ImGui::EndGroup();
    }
}

/* ---- SSF: the speaker test ---- */

/* The game's own sounds, recorded once: the game run headless on Super
 * Android with --ssf, written as 7.1 (--wav), for a piece of its music (the
 * front pair, before the ball is in play) and the board's effects (the side
 * and rear pairs, each cut out where it starts after quiet). Rolling is off
 * and the effects at 1, so they play at the effects volume as set here. */
#define TEST_RATE 44100
#ifdef _WIN32
static HWAVEOUT g_wout;
static WAVEHDR g_whdr;
static std::vector<int16_t> g_wbuf;
#else
static SDL_AudioDeviceID g_audio;
#endif
static bool g_audio_ok;
static int g_audio_channels;
static std::mutex g_rec_lock;
static int g_rec_state;                              /* 0 not yet, 1 recording, 2 ready, 3 failed */
static std::string g_rec_error;
static std::vector<int16_t> g_music;                 /* mono */
static std::vector<std::vector<int16_t>> g_clips;    /* mono, at the game's level for an effects volume of 1 */

static bool wav_read(const std::string &path, int &channels, std::vector<int16_t> &pcm)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    unsigned char h[12], ck[8];
    bool ok = fread(h, 1, 12, f) == 12 && !memcmp(h, "RIFF", 4) && !memcmp(h + 8, "WAVE", 4);
    int rate = 0, bits = 0;
    channels = 0;
    while (ok && fread(ck, 1, 8, f) == 8) {
        const uint32_t n = ck[4] | ck[5] << 8 | ck[6] << 16 | (uint32_t)ck[7] << 24;
        if (!memcmp(ck, "fmt ", 4)) {
            unsigned char fm[16];
            if (n < 16 || fread(fm, 1, 16, f) != 16) { ok = false; break; }
            channels = fm[2] | fm[3] << 8;
            rate = fm[4] | fm[5] << 8 | fm[6] << 16 | fm[7] << 24;
            bits = fm[14] | fm[15] << 8;
            fseek(f, (long)(n - 16 + (n & 1)), SEEK_CUR);
        } else if (!memcmp(ck, "data", 4)) {
            if (!channels || rate != TEST_RATE || bits != 16) { ok = false; break; }
            pcm.resize(n / 2);
            pcm.resize(fread(pcm.data(), 2, pcm.size(), f));   /* the game's own order: little-endian, as written */
            break;
        } else fseek(f, (long)(n + (n & 1)), SEEK_CUR);
    }
    fclose(f);
    return ok && !pcm.empty();
}

static void fade(std::vector<int16_t> &v, int in, int out)
{
    const int n = (int)v.size();
    for (int i = 0; i < in && i < n; i++) v[i] = (int16_t)(v[i] * i / in);
    for (int i = 0; i < out && i < n; i++) v[n - 1 - i] = (int16_t)(v[n - 1 - i] * i / out);
}

/* Cut the recording up: music from the front, each board effect from the exciters */
static std::string ssf_cut(const std::vector<int16_t> &pcm, int c, std::vector<int16_t> &music,
                           std::vector<std::vector<int16_t>> &clips)
{
    if (c != 8) return "the game wrote " + std::to_string(c) + " channels, not 8";
    const size_t frames = pcm.size() / 8;
    const size_t m0 = TEST_RATE * 3, m1 = TEST_RATE * 8;            /* the table's music, before the ball is in play */
    if (frames < m1) return "the recording is too short";
    for (size_t i = m0; i < m1; i++) music.push_back((int16_t)((pcm[i * 8] + pcm[i * 8 + 1]) / 2));
    fade(music, TEST_RATE / 50, TEST_RATE / 5);

    const size_t win = TEST_RATE / 100;                              /* 10 ms */
    std::vector<int> env(frames / win);
    for (size_t w = 0; w < env.size(); w++)
        for (size_t i = w * win; i < (w + 1) * win; i++)
            for (int ch = 4; ch < 8; ch++) env[w] = std::max(env[w], abs(pcm[i * 8 + ch]));
    std::vector<std::pair<size_t, int>> seen;                        /* length and peak: one of each sound */
    for (size_t w = 1; w < env.size(); ) {
        if (!(env[w] > 200 && env[w - 1] < 100)) { w++; continue; }   /* at an effects volume of 1 */
        size_t e = w;
        int quiet = 0, peak = 0;
        while (e < env.size() && quiet < 5 && e - w < 100) { quiet = env[e] < 75 ? quiet + 1 : 0; peak = std::max(peak, env[e]); e++; }
        const size_t len = e - w;
        bool dup = false;
        for (auto &sn : seen) dup |= (sn.first + 2 >= len && len + 2 >= sn.first && abs(sn.second - peak) * 20 <= peak);
        if (!dup && len >= 5) {
            seen.push_back({ len, peak });
            std::vector<int16_t> cl;
            for (size_t i = w * win; i < e * win; i++) {             /* the four exciters together: the effect, as loud as placed */
                int v = pcm[i * 8 + 4] + pcm[i * 8 + 5] + pcm[i * 8 + 6] + pcm[i * 8 + 7];
                cl.push_back((int16_t)std::clamp(v, -32768, 32767));
            }
            fade(cl, 0, TEST_RATE / 50);
            clips.push_back(std::move(cl));
        }
        w = e;
    }
    return clips.empty() ? "no playfield sounds were heard" : "";
}

static void ssf_record()
{
    {
        std::lock_guard<std::mutex> l(g_rec_lock);
        if (g_rec_state) return;
        g_rec_state = 1;
    }
    std::thread([] {
        std::string err;
        std::vector<int16_t> music;
        std::vector<std::vector<int16_t>> clips;
        const std::string exe = game_exe();
        std::error_code ec;
        const auto dir = std::filesystem::temp_directory_path(ec) / ("epicpinball-ssf-" + std::to_string(ticks()) + "-"
                                                                     + std::to_string(rand()));
#ifdef _WIN32
        if (exe.empty()) err = "the game is not built (Setup.cmd)";
#else
        if (exe.empty()) err = "the game is not built (build-linux.sh)";
#endif
        else if (!exists("original/EP1.DAT")) err = "the game's files are not in original/";
        else if (!std::filesystem::create_directories(dir, ec)) err = "cannot make " + dir.string();
        else {
            std::string keys;                                        /* a ball now and then, a flip or two */
            for (int t = 26000; t <= 100000; t += 9000)
                keys += (keys.empty() ? "" : ",") + std::to_string(t) + ":SPACE+1500," + std::to_string(t + 4000) + ":LSHIFT+200,"
                        + std::to_string(t + 6000) + ":RSHIFT+200";
            const std::string wav = (dir / "ssf.wav").string();
            const std::string cmd = "\"" + exe + "\" --headless --ssf --ssf-volumes 1,0 --table 1 --seconds 105 --save \""
                                    + dir.string() + "\" --wav \"" + wav + "\" --keys \"" + keys + "\"";
            std::string out;
            const int rc = run_lines(cmd, [&](const char *l) { out += l; });
            int c = 0;
            std::vector<int16_t> pcm;
            if (rc != 0 || !wav_read(wav, c, pcm)) err = "the game did not record (" + out.substr(0, 200) + ")";
            else err = ssf_cut(pcm, c, music, clips);
            std::filesystem::remove_all(dir, ec);
        }
        std::lock_guard<std::mutex> l(g_rec_lock);
        g_rec_error = err;
        if (err.empty()) { g_music = std::move(music); g_clips = std::move(clips); }
        g_rec_state = err.empty() ? 2 : 3;
    }).detach();
}

#ifdef _WIN32
/* As the game opens it (host.c's wave_try): 7.1, FL FR FC LFE BL BR SL SR */
static bool wave_try(int ch)
{
    static const GUID pcm = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
    WAVEFORMATEXTENSIBLE x = {};
    x.Format.wFormatTag = ch > 2 ? WAVE_FORMAT_EXTENSIBLE : WAVE_FORMAT_PCM;
    x.Format.nChannels = (WORD)ch;
    x.Format.nSamplesPerSec = TEST_RATE;
    x.Format.wBitsPerSample = 16;
    x.Format.nBlockAlign = (WORD)(ch * 2);
    x.Format.nAvgBytesPerSec = TEST_RATE * ch * 2;
    x.Format.cbSize = ch > 2 ? (WORD)(sizeof x - sizeof x.Format) : 0;
    x.Samples.wValidBitsPerSample = 16;
    x.dwChannelMask = 0x63F;
    x.SubFormat = pcm;
    return waveOutOpen(&g_wout, WAVE_MAPPER, &x.Format, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR;
}

static bool audio_open()
{
    if (g_audio_ok) return true;
    g_audio_channels = 8;
    if (!wave_try(8)) {
        g_audio_channels = 2;
        if (!wave_try(2)) { g_wout = nullptr; g_status = "No sound device."; return false; }
    }
    return g_audio_ok = true;
}

/* One sound at a time: what plays stops */
static void audio_play(std::vector<int16_t> &&buf)
{
    waveOutReset(g_wout);
    if (g_whdr.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(g_wout, &g_whdr, sizeof g_whdr);
    g_wbuf = std::move(buf);
    g_whdr = {};
    g_whdr.lpData = (LPSTR)g_wbuf.data();
    g_whdr.dwBufferLength = (DWORD)(g_wbuf.size() * sizeof g_wbuf[0]);
    if (waveOutPrepareHeader(g_wout, &g_whdr, sizeof g_whdr) == MMSYSERR_NOERROR) waveOutWrite(g_wout, &g_whdr, sizeof g_whdr);
}

static void audio_close()
{
    if (!g_wout) return;
    waveOutReset(g_wout);
    if (g_whdr.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(g_wout, &g_whdr, sizeof g_whdr);
    waveOutClose(g_wout);
}
#else
static bool audio_open()
{
    if (g_audio_ok) return true;
    SDL_AudioSpec want = {}, got = {};
    want.freq = TEST_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 8;
    want.samples = 1024;
    g_audio = SDL_OpenAudioDevice(nullptr, 0, &want, &got, SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
    if (!g_audio) { g_status = std::string("No sound device: ") + SDL_GetError(); return false; }
    g_audio_channels = got.channels;
    SDL_PauseAudioDevice(g_audio, 0);
    return g_audio_ok = true;
}

static void audio_play(std::vector<int16_t> &&buf)
{
    SDL_ClearQueuedAudio(g_audio);                       /* one at a time */
    SDL_QueueAudio(g_audio, buf.data(), (Uint32)(buf.size() * sizeof buf[0]));
}

static void audio_close() { if (g_audio) SDL_CloseAudioDevice(g_audio); }
#endif

/* On one channel: the music for the front pair, a playfield sound for the
 * others (at the effects volume); a low knock if the recording failed */
static void speaker_test(int ch)
{
    if (!audio_open()) return;
    if (ch >= g_audio_channels) {
        g_status = "The sound device has " + std::to_string(g_audio_channels) + " channels, not 8: SSF needs 7.1.";
        return;
    }
    std::vector<int16_t> mono;
    double gain = 1;
    {
        std::lock_guard<std::mutex> l(g_rec_lock);
        if (g_rec_state == 2) {
            if (ch < 2) mono = g_music;
            else {
                static size_t last = (size_t)-1;
                size_t k = (size_t)rand() % g_clips.size();
                if (k == last && g_clips.size() > 1) k = (k + 1) % g_clips.size();
                last = k;
                mono = g_clips[k];
                gain = g_s.fx;
            }
        }
    }
    if (mono.empty()) {
        const int n = TEST_RATE * 30 / 100;
        for (int i = 0; i < n; i++) {                    /* a low knock, as a ball hits something */
            const double t = (double)i / TEST_RATE, env = exp(-t * 18) * (1 - exp(-t * 900));
            mono.push_back((int16_t)(20000 * env * sin(2 * 3.14159265358979 * 70 * t)));
        }
    }
    const int c = g_audio_channels;
    std::vector<int16_t> buf(mono.size() * c);
    for (size_t i = 0; i < mono.size(); i++) buf[i * c + ch] = (int16_t)std::clamp((int)lround(mono[i] * gain), -32768, 32767);
    audio_play(std::move(buf));
}

static void page_ssf()
{
    ImGui::TextWrapped("SSF plays the board's sounds on a cabinet's exciters, where on the table they happen: the game's "
                       "own sound on the front pair, the board on the side and rear pairs (docs/ssf.md). It needs a 7.1 "
                       "sound device.");
    ImGui::SetNextItemWidth(300);
    ImGui::SliderFloat("Effects volume", &g_s.fx, 0, 10, "%.1f");
    ImGui::SetNextItemWidth(300);
    ImGui::SliderFloat("Rolling volume", &g_s.roll, 0, 10, "%.1f");
    help("4 is the default for both.");
    ImGui::SeparatorText("Speaker test");
    ssf_record();
    int state;
    std::string err;
    size_t nclips;
    {
        std::lock_guard<std::mutex> l(g_rec_lock);
        state = g_rec_state;
        err = g_rec_error;
        nclips = g_clips.size();
    }
    if (state == 1) ImGui::TextColored({ 1, 0.85f, 0.2f, 1 }, "Recording the game's sounds from Super Android...");
    else if (state == 2)
        ImGui::TextWrapped("The front pair plays a piece of Super Android's music; the side and rear pairs one of its "
                           "playfield sounds, picked at random from %d, at the effects volume.", (int)nclips);
    else if (state == 3) ImGui::TextColored({ 1, 0.6f, 0.2f, 1 }, "No game sounds (%s): a low knock instead.", err.c_str());
    static const struct { const char *n; int ch; } sp[6] = {
        { "Front left (the game)", 0 }, { "Front right (the game)", 1 }, { "Side left", 6 }, { "Side right", 7 },
        { "Rear left", 4 }, { "Rear right", 5 } };
    ImGui::BeginDisabled(state == 1);
    for (int i = 0; i < 6; i++) {
        if (i % 2) ImGui::SameLine();
        if (ImGui::Button(sp[i].n, ImVec2(220, 0))) speaker_test(sp[i].ch);
    }
    ImGui::EndDisabled();
    if (g_audio_ok) ImGui::TextDisabled("The default sound device, %d channels.", g_audio_channels);
}

/* ---- DMD and DOF ---- */

static std::mutex g_log_lock;
static std::string g_dof_log;
static bool g_dof_running;

static void dof_test(const std::string &event)
{
    if (g_dof_running) return;
    g_dof_running = true;
    std::thread([event] {
        const std::string exe = exists("dof/build/epicpinball-dof.exe") ? "dof\\build\\epicpinball-dof.exe" : "dof/build/epicpinball-dof";
        const std::string cmd = exe + " --table ep1 --test " + event + " --save \"" + g_save + "\"";
        { std::lock_guard<std::mutex> l(g_log_lock); g_dof_log = "$ " + cmd + "\n"; }
        if (run_lines(cmd, [](const char *line) { std::lock_guard<std::mutex> l(g_log_lock); g_dof_log += line; }) == -1) {
            std::lock_guard<std::mutex> l(g_log_lock);
            g_dof_log += "(it did not start)\n";
        }
        g_dof_running = false;
    }).detach();
}

static void page_dmd_dof()
{
    ImGui::SeparatorText("DMD device (ZeDMD)");
    bool b = g_s.z_set;
    if (ImGui::Checkbox("Set the ZeDMD here (saves/dmd/DMDUtil.ini)", &b)) g_s.z_set = b;
    help("Without it, libdmdutil looks for a ZeDMD on every serial port (docs/dmd.md).");
    if (g_s.z_set) {
        b = g_s.z_enabled;
        if (ImGui::Checkbox("Enabled", &b)) g_s.z_enabled = b;
        char dev[128];
        snprintf(dev, sizeof dev, "%s", g_s.z_dev.c_str());
        ImGui::SetNextItemWidth(300);
        if (ImGui::InputText("Serial port (empty: look on all)", dev, sizeof dev)) g_s.z_dev = dev;
        ImGui::SetNextItemWidth(300);
        ImGui::SliderInt("Brightness", &g_s.z_bright, 0, 15);
    }
    ImGui::SeparatorText("DOF test");
    const bool built = exists("dof/build/epicpinball-dof") || exists("dof/build/epicpinball-dof.exe");
    if (!built) { ImGui::TextColored({ 1, 0.6f, 0.2f, 1 }, "The DOF helper is not built: " HELPER_BUILD("dof") " (docs/cabinet-feedback.md)."); return; }
    ImGui::TextWrapped("Each fires one event through table 1's row for a moment, with the DOF files the game uses "
                       "(the first line of the output says where they are).");
    static const char *const ev[] = { "flipper_left", "flipper_right", "sling_left", "sling_right", "bumper_1", "bumper_2",
                                      "bumper_3", "launch", "ball_release", "knocker", "drain", "nudge", "tilt", "game_start" };
    for (size_t i = 0; i < sizeof ev / sizeof ev[0]; i++) {
        if (i % 4) ImGui::SameLine();
        ImGui::BeginDisabled(g_dof_running);
        if (ImGui::Button(ev[i], ImVec2(150, 0))) dof_test(ev[i]);
        ImGui::EndDisabled();
    }
    std::lock_guard<std::mutex> l(g_log_lock);
    ImGui::InputTextMultiline("##doflog", (char *)g_dof_log.c_str(), g_dof_log.size() + 1, ImVec2(-1, 220), ImGuiInputTextFlags_ReadOnly);
}

/* ---- the VPinball import ------------------------------------------------------------ */

static VpxImport g_imp;
static char g_imp_path[1024];
static bool g_imp_open, g_ask_open;
static bool g_take[5] = { true, true, true, true, true };    /* keys, buttons, plunger, nudging, sound */

static std::string vpx_device(const char *guid)
{
    Joy *j = joy_for_guid(guid);
    return j ? j->name : std::string();
}

static void import_read()
{
    if (!vpx_read(g_imp_path, &g_imp)) { g_status = std::string(g_imp_path) + " is not VPinball's settings."; return; }
    g_imp_open = true;
}

static void import_apply()
{
    const VpxImport &o = g_imp;
    if (g_take[0])
        for (int a = 0; a < KEY_ACTIONS; a++)
            for (int i = 0; i < o.nkey[a]; i++)
                if (std::find(g_s.keys[a].begin(), g_s.keys[a].end(), o.key[a][i]) == g_s.keys[a].end()) g_s.keys[a].push_back(o.key[a][i]);
    bool any_button = false;
    for (int a = 0; a < KEY_ACTIONS; a++) any_button |= o.nbutton[a] > 0;
    if (g_take[1] && any_button) {
        const std::string d = vpx_device(o.button_guid);
        if (d != g_s.bdev) for (auto &b : g_s.buttons) b.clear();     /* another board's numbers mean nothing here */
        g_s.bdev = d;
        for (int a = 0; a < KEY_ACTIONS; a++)
            if (o.nbutton[a]) g_s.buttons[a].assign(o.button[a], o.button[a] + o.nbutton[a]);
    }
    if (g_take[2] && o.plunger_axis >= 0) {
        g_s.pdev = vpx_device(o.plunger_guid);
        g_s.paxis = o.plunger_axis;
        g_s.pspeed = o.plunger_speed_axis;
        g_s.rest = 0;
        g_s.pulled = o.plunger_reversed ? -32767 : 32767;
    }
    if (g_take[3] && (o.nudge_x >= 0 || o.nudge_y >= 0)) {
        g_s.ndev = vpx_device(o.nudge_guid);
        g_s.nx = o.nudge_x;
        g_s.ny = o.nudge_y;
        g_s.ix = o.invert_x;
        g_s.iy = o.invert_y;
        g_s.vel = o.is_velocity;
        if (o.dead >= 0) g_s.dead = std::min(o.dead, 30000);
    }
    if (g_take[4]) {
        g_s.mode[3] = o.ssf;
        if (o.ssf && o.sound_volume >= 0) g_s.fx = g_s.roll = 4.0f * o.sound_volume / 100;
    }
    ini_set(g_ini.c_str(), "config", "asked_import", "1");
    g_status = "Imported from VPinball: look through the pages (calibrate the plunger and nudging), then Save.";
}

static void import_window()
{
    if (g_ask_open) {
        ImGui::OpenPopup("Import from VPinball?");
        g_ask_open = false;
    }
    if (ImGui::BeginPopupModal("Import from VPinball?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("VPinball's settings are at\n  %s", g_imp_path);
        ImGui::Text("Take its keys, buttons, plunger, nudging and sound from there?");
        if (ImGui::Button("Yes, show me what", ImVec2(200, 0))) { import_read(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(120, 0))) {
            ini_set(g_ini.c_str(), "config", "asked_import", "1");
            ImGui::CloseCurrentPopup();
        }
        ImGui::TextDisabled("The Import button on the first page does this at any time.");
        ImGui::EndPopup();
    }
    if (!g_imp_open) return;
    ImGui::SetNextWindowSize(ImVec2(760, 600), ImGuiCond_Appearing);
    if (!ImGui::Begin("From VPinball", &g_imp_open)) { ImGui::End(); return; }
    const VpxImport &o = g_imp;
    ImGui::Text("%s  (VPinball %s)", g_imp_path, o.format == 2 ? "10.8.1 or newer" : "10.8.0");
    ImGui::Separator();
    ImGui::Checkbox("Keys", &g_take[0]);
    for (int a = 0; a < KEY_ACTIONS; a++)
        if (o.nkey[a]) {
            std::string s;
            for (int i = 0; i < o.nkey[a]; i++) {
                s += (i ? ", " : "") + std::string(key_name(o.key[a][i]));
                for (int b = 0; b < KEY_ACTIONS; b++)
                    if (b != a && k_actions[b].sc == o.key[a][i]) s += std::string(" (instead of ") + k_actions[b].label + ")";
            }
            ImGui::BulletText("%s: %s", k_actions[a].label, s.c_str());
        }
    ImGui::Checkbox("Buttons", &g_take[1]);
    bool any = false;
    for (int a = 0; a < KEY_ACTIONS; a++)
        if (o.nbutton[a]) {
            std::string s;
            for (int i = 0; i < o.nbutton[a]; i++) s += (i ? ", " : "") + std::to_string(o.button[a][i]);
            ImGui::BulletText("%s: button %s", k_actions[a].label, s.c_str());
            any = true;
        }
    if (any) {
        const std::string d = vpx_device(o.button_guid);
        ImGui::BulletText("on %s", d.empty() ? "(the board is not plugged in: pick it on the Buttons page)" : d.c_str());
    }
    ImGui::Checkbox("Plunger", &g_take[2]);
    if (o.plunger_axis >= 0) {
        const std::string d = vpx_device(o.plunger_guid);
        ImGui::BulletText("axis %s%s on %s; calibrate it on the Plunger page", axis_text(o.plunger_axis).c_str(),
                          o.plunger_reversed ? ", reversed" : "", d.empty() ? "(not plugged in)" : d.c_str());
    } else ImGui::BulletText("none");
    ImGui::Checkbox("Nudging", &g_take[3]);
    if (o.nudge_x >= 0 || o.nudge_y >= 0) {
        const std::string d = vpx_device(o.nudge_guid);
        ImGui::BulletText("X axis %s%s, Y axis %s%s, %s, dead zone %d, on %s", axis_text(o.nudge_x).c_str(), o.invert_x ? " inverted" : "",
                          axis_text(o.nudge_y).c_str(), o.invert_y ? " inverted" : "", o.is_velocity ? "velocity" : "acceleration",
                          o.dead, d.empty() ? "(not plugged in)" : d.c_str());
    } else ImGui::BulletText("none");
    ImGui::Checkbox("Sound", &g_take[4]);
    ImGui::BulletText("SSF %s%s", o.ssf ? "on" : "off",
                      o.sound_volume >= 0 ? (", playfield volume " + std::to_string(o.sound_volume) + "%").c_str() : "");
    if (*o.notes) {
        ImGui::SeparatorText("Not imported");
        ImGui::TextWrapped("%s", o.notes);
    }
    ImGui::Separator();
    if (ImGui::Button("Apply", ImVec2(160, 0))) { import_apply(); g_imp_open = false; }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) g_imp_open = false;
    ImGui::TextDisabled("Nothing is written until you Save.");
    ImGui::End();
}

/* ---- the window ---------------------------------------------------------------------- */

static const char *const k_pages[] = { "Modes and picture", "Keys", "Buttons", "Plunger", "Nudging", "SSF", "DMD and DOF" };
static int g_page;
static bool g_quit, g_ask_quit;
static const char *g_shot;                           /* --shot: the picture, then out */

/* A key pressed for "+ add" (a set-1 scan code, | KEY_E0) */
static void key_captured(int sc)
{
    if (g_capture_key < 0) return;
    auto &k = g_s.keys[g_capture_key];
    if (sc && sc != k_actions[g_capture_key].sc && std::find(k.begin(), k.end(), sc) == k.end()) k.push_back(sc);
    if (!sc) g_status = "The game has no such key.";
    g_capture_key = -1;
}

/* The boards read, and for "+ press" the next button that goes down, on any board */
static void joys_poll()
{
    joys_update();
    for (auto &j : g_joys)
        for (int b = 0; b < j.buttons; b++) {
            const bool d = button_value(&j, b);
            if (d && !j.was[b] && g_capture_button >= 0) {
                if (joy_of(g_s.bdev) != &j) {
                    if (!g_s.bdev.empty()) for (auto &x : g_s.buttons) x.clear();    /* another board: its numbers */
                    g_s.bdev = j.name;
                }
                auto &bs = g_s.buttons[g_capture_button];
                if (std::find(bs.begin(), bs.end(), b) == bs.end()) bs.push_back(b);
                g_capture_button = -1;
            }
            j.was[b] = d;
        }
}

static void close_asked()
{
    if (dirty()) g_ask_quit = true;
    else g_quit = true;
}

/* Everything but the window's own work: the settings files, ImGui, the boards */
static int start(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--save") && i + 1 < argc) g_save = argv[++i];
        else if (!strcmp(argv[i], "--page") && i + 1 < argc) g_page = atoi(argv[++i]) % 7;
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) g_shot = argv[++i];
        else { fprintf(stderr, "usage: epicpinball-config [--save DIR] [--page N --shot FILE.bmp]\n"); return 2; }
    }
    make_dir(g_save.c_str());
    g_ini = g_save + "/epicpinball.ini";
    g_ctl = g_save + "/controller.ini";
    g_dmd_ini = g_save + "/dmd/DMDUtil.ini";
    if (!exists(g_ctl)) {                            /* as the game first writes it */
        FILE *f = fopen(g_ctl.c_str(), "wb");
        if (f) { fputs(k_controller_default, f); fclose(f); }
    }
    return 0;
}

static void imgui_start(float scale)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;                        /* no imgui.ini beside the game */
    io.FontGlobalScale = 1.25f * scale;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().FrameRounding = 4;
    ImGui::GetStyle().ScaleAllSizes(scale);
}

static void settings_start()
{
    joys_refresh();
    g_s = g_saved = load();
    char v[16];
    if (vpx_find(g_imp_path, sizeof g_imp_path) && !atoi(ini_get(g_ini.c_str(), "config", "asked_import", "0", v, sizeof v))
        && !g_shot)
        g_ask_open = true;
}

/* One frame of the pages, up to ImGui::Render */
static void draw()
{
    ImGuiIO &io = ImGui::GetIO();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::BeginChild("nav", ImVec2(220, -60), ImGuiChildFlags_Borders);
    for (int i = 0; i < (int)(sizeof k_pages / sizeof k_pages[0]); i++)
        if (ImGui::Selectable(k_pages[i], g_page == i)) g_page = i;
    ImGui::Separator();
    if (ImGui::Button("Import from VPinball", ImVec2(-1, 0))) {
        if (!*g_imp_path) vpx_find(g_imp_path, sizeof g_imp_path);
        if (*g_imp_path) import_read();
        else g_status = "No VPinballX.ini where VPinball keeps it.";
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("page", ImVec2(0, -60), ImGuiChildFlags_Borders);
    switch (g_page) {
    case 0: page_modes(); break;
    case 1: page_keys(); break;
    case 2: page_buttons(); break;
    case 3: page_plunger(); break;
    case 4: page_nudge(); break;
    case 5: page_ssf(); break;
    case 6: page_dmd_dof(); break;
    }
    ImGui::EndChild();
    const bool d = dirty();
    ImGui::BeginDisabled(!d);
    if (ImGui::Button("Save", ImVec2(120, 0))) save();
    ImGui::SameLine();
    if (ImGui::Button("Undo changes", ImVec2(160, 0))) g_s = g_saved;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextWrapped("%s", d ? "Unsaved changes." : g_status.c_str());
    import_window();
    if (g_ask_quit) { ImGui::OpenPopup("Unsaved changes"); g_ask_quit = false; }
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Save the changes first?");
        if (ImGui::Button("Save", ImVec2(120, 0))) { save(); g_quit = true; }
        ImGui::SameLine();
        if (ImGui::Button("Don't save", ImVec2(120, 0))) g_quit = true;
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
    ImGui::Render();
}

/* --shot: true once the page has settled (the SSF page: recorded) */
static bool shot_due()
{
    static int frames, since_recorded;               /* frames drawn since the SSF page's recording was not under way */
    { std::lock_guard<std::mutex> l(g_rec_lock); since_recorded = g_rec_state == 1 ? 0 : since_recorded + 1; }
    return g_shot && ++frames >= 20 && since_recorded >= 3;
}

/* 32-bit BGRA rows, top first, as a BMP */
static void save_bmp(const char *path, const uint8_t *px, int w, int h, int pitch)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    const uint32_t size = 54 + (uint32_t)w * h * 4;
    uint8_t hd[54] = { 'B', 'M' };
    auto put32 = [&](int at, uint32_t v) { for (int i = 0; i < 4; i++) hd[at + i] = (uint8_t)(v >> (8 * i)); };
    put32(2, size); put32(10, 54); put32(14, 40); put32(18, (uint32_t)w); put32(22, (uint32_t)-h);
    hd[26] = 1; hd[28] = 32;
    put32(34, (uint32_t)w * h * 4);
    fwrite(hd, 1, 54, f);
    for (int y = 0; y < h; y++) fwrite(px + (size_t)y * pitch, 4, (size_t)w, f);
    fclose(f);
}

#ifdef _WIN32
/* Win32 and Direct3D 11, as the game's own window on Windows */
static ID3D11Device *g_dev;
static ID3D11DeviceContext *g_ctx;
static IDXGISwapChain *g_chain;
static ID3D11RenderTargetView *g_target;
static UINT g_resize_w, g_resize_h;
static bool g_devices_changed;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static void target_make()
{
    ID3D11Texture2D *back = nullptr;
    if (SUCCEEDED(g_chain->GetBuffer(0, IID_PPV_ARGS(&back)))) {
        g_dev->CreateRenderTargetView(back, nullptr, &g_target);
        back->Release();
    }
}

static void target_drop()
{
    if (g_target) { g_target->Release(); g_target = nullptr; }
}

static bool d3d_start(HWND w)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = w;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                               &sd, &g_chain, &g_dev, &got, &g_ctx);
    if (FAILED(hr))                                  /* no GPU driver: Windows' own software one */
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd,
                                           &g_chain, &g_dev, &got, &g_ctx);
    if (FAILED(hr)) { fprintf(stderr, "Direct3D 11: error %08lX\n", (unsigned long)hr); return false; }
    target_make();
    return true;
}

static void d3d_stop()
{
    target_drop();
    if (g_chain) g_chain->Release();
    if (g_ctx) g_ctx->Release();
    if (g_dev) g_dev->Release();
}

/* The back buffer, for --shot */
static void d3d_shot(const char *path)
{
    ID3D11Texture2D *back = nullptr, *copy = nullptr;
    if (FAILED(g_chain->GetBuffer(0, IID_PPV_ARGS(&back)))) return;
    D3D11_TEXTURE2D_DESC td;
    back->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags = 0;
    if (SUCCEEDED(g_dev->CreateTexture2D(&td, nullptr, &copy))) {
        g_ctx->CopyResource(copy, back);
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(g_ctx->Map(copy, 0, D3D11_MAP_READ, 0, &m))) {
            save_bmp(path, (const uint8_t *)m.pData, (int)td.Width, (int)td.Height, (int)m.RowPitch);
            g_ctx->Unmap(copy, 0);
        }
        copy->Release();
    }
    back->Release();
}

static LRESULT WINAPI wndproc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (ImGui_ImplWin32_WndProcHandler(h, m, wp, lp)) return 1;
    switch (m) {
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) { g_resize_w = LOWORD(lp); g_resize_h = HIWORD(lp); }
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_KEYMENU) return 0;   /* Alt is a key here, not the menu */
        break;
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        if (!(lp & (1 << 30)))                       /* not a repeat: the key's set-1 code, as the game reads it */
            key_captured((int)((lp >> 16) & 0xFF) | (lp & (1 << 24) ? KEY_E0 : 0));
        break;
    case WM_DEVICECHANGE:
        if (wp == DBT_DEVNODES_CHANGED) g_devices_changed = true;   /* a board plugged in or out */
        break;
    case WM_CLOSE:
        close_asked();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

int main(int argc, char **argv)
{
    if (const int rc = start(argc, argv)) return rc;
    ImGui_ImplWin32_EnableDpiAwareness();
    const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT { 0, 0 }, MONITOR_DEFAULTTOPRIMARY));
    WNDCLASSEXW wc = { sizeof wc, CS_CLASSDC, wndproc, 0, 0, GetModuleHandleW(nullptr), LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION),
                       LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW), nullptr, nullptr, L"EpicPinballSettings", nullptr };
    RegisterClassExW(&wc);
    const int ww = (int)(1180 * scale), wh = (int)(820 * scale);
    HWND w = CreateWindowW(wc.lpszClassName, L"Epic Pinball settings", WS_OVERLAPPEDWINDOW,
                           (GetSystemMetrics(SM_CXSCREEN) - ww) / 2, (GetSystemMetrics(SM_CYSCREEN) - wh) / 2, ww, wh,
                           nullptr, nullptr, wc.hInstance, nullptr);
    if (!w || !d3d_start(w)) {
        MessageBoxA(nullptr, "Direct3D 11 did not start.", "Epic Pinball settings", MB_ICONERROR);
        return 1;
    }
    ShowWindow(w, SW_SHOWDEFAULT);
    UpdateWindow(w);
    imgui_start(scale);
    ImGui_ImplWin32_Init(w);
    ImGui_ImplDX11_Init(g_dev, g_ctx);
    dij_init();
    settings_start();

    while (!g_quit) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) g_quit = true;
        }
        if (g_quit) break;
        if (g_devices_changed) { g_devices_changed = false; joys_refresh(); }
        joys_poll();
        if (g_resize_w && g_resize_h) {
            target_drop();
            g_chain->ResizeBuffers(0, g_resize_w, g_resize_h, DXGI_FORMAT_UNKNOWN, 0);
            g_resize_w = g_resize_h = 0;
            target_make();
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        draw();
        const float bg[4] = { 20 / 255.0f, 20 / 255.0f, 24 / 255.0f, 1 };
        g_ctx->OMSetRenderTargets(1, &g_target, nullptr);
        g_ctx->ClearRenderTargetView(g_target, bg);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        if (shot_due()) { d3d_shot(g_shot); g_quit = true; }
        g_chain->Present(1, 0);
    }
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    audio_close();
    joys_close();
    d3d_stop();
    DestroyWindow(w);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}
#else
int main(int argc, char **argv)
{
    if (const int rc = start(argc, argv)) return rc;
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow("Epic Pinball settings", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1180, 820,
                                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED) : nullptr;
    if (!ren && win) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }

    imgui_start(1);
    ImGui_ImplSDL2_InitForSDLRenderer(win, ren);
    /* No gamepads for ImGui: opening a cabinet board as one (SDL maps a
     * Pinscape as a pad, Z and RZ its triggers) holds those axes at -32768
     * until they move, and a plunger at rest would read so. */
    ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_Manual, nullptr, 0);
    ImGui_ImplSDLRenderer2_Init(ren);
    settings_start();

    while (!g_quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT || (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE)) close_asked();
            if (e.type == SDL_JOYDEVICEADDED || e.type == SDL_JOYDEVICEREMOVED) joys_refresh();
            if (e.type == SDL_KEYDOWN && !e.key.repeat) key_captured(key_from_sdl(e.key.keysym.scancode));
        }
        joys_poll();

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        draw();
        SDL_SetRenderDrawColor(ren, 20, 20, 24, 255);
        SDL_RenderClear(ren);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), ren);
        if (shot_due()) {
            int w, h;
            SDL_GetRendererOutputSize(ren, &w, &h);
            std::vector<uint8_t> px((size_t)w * h * 4);
            if (!SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, px.data(), w * 4)) save_bmp(g_shot, px.data(), w, h, w * 4);
            g_quit = true;
        }
        SDL_RenderPresent(ren);
    }
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    audio_close();
    joys_close();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
#endif
