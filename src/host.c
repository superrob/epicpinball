/*
 * host.c - the modern machine's side: a window drawn by the presenter
 * (present.c: Direct3D 11 on Windows, GDI if that fails; SDL2 on Linux),
 * waveOut or SDL audio, keyboard as raw PC scancodes, an Xbox pad (pad.c) --
 * or, with --headless, none of that: frames are composed offscreen and, with
 * --record, piped to ffmpeg with the audio muxed in at the end. Headless
 * never touches the desktop, so it works over RDP and from CI (repo rules,
 * section 13).
 *
 * The remaster's settings (scaling, colour, the whole table, the bars beside
 * the picture, fullscreen, the window's place, smooth sound) are kept in
 * epicpinball.ini in the save folder, changed with F7, F9-F12 or the menu on
 * right-click / F10. Headless runs ignore the file: what they draw is set on
 * the command line only, so a test never depends on someone's preferences.
 *
 * --cabinet is two borderless windows instead: the playfield, filling
 * a display and turned with F6, and the scoreboard, placed anywhere. Its
 * settings are the ini's [cabinet], apart from the ordinary window's.
 *
 * Scripted input (--keys "ms:KEY,...") presses and releases keys at fixed
 * points in emulated time, which is how the harness walks the menus and
 * plays the tables.
 */
#include "machine.h"
#include "feedback.h"
#include "dof_pipe.h"
#include "plunger.h"
#include "watch.h"
#include "dmd.h"
#include "dmd_pipe.h"
#include "ssf.h"
#include "autostart.h"
#include "nudge.h"
#include "ini.h"
#include "keys.h"
#include "hud.h"
#include <ctype.h>
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <shellapi.h>
#include <direct.h>
#define popen _popen
#define pclose _pclose
#else
#include <SDL.h>
#include <unistd.h>
#endif

#define FRAME_MAX FRAME_PIXELS
static uint32_t frame[FRAME_MAX];
static uint32_t big[FRAME_MAX];
static int fw = 320, fh = 200;
static double frame_aspect = 4.0 / 3;
static int table_up;                   /* --cabinet: the playfield is a table, not a menu */

static uint32_t score[FRAME_MAX];      /* --cabinet's second screen */
static int sw = 320, sh = 29;

/* --dmd: the 128x32 picture, and as a DMD shows it (round dots), for its window */
#define DMD_S 8
static uint32_t dmd_px[DMD_W * DMD_H], dmd_big[DMD_W * DMD_S * DMD_H * DMD_S];
static int dmd_new = 1;              /* the window has not shown this one yet */

static Present S = { PRESENT_SHARP, 0, 1, 0, 0 };
static const char *const mode_names[] = { "sharp", "smooth", "crt", "nearest", "integer" };

/* The game's picture as the monitor showed it: what --record and the
 * conformance shots see. */
static void compose(void)
{
    vga_compose(frame, &fw, &fh);
    frame_aspect = 4.0 / 3;
}

static int dmd_on(void) { return g_opt.dmd || g_opt.dmd_device; }
static int dmd_showing(void);

/* The DMD picture now: to the helper when it changed, and for the window. 1 if it changed */
static int compose_dmd(void)
{
    static uint32_t bar[320 * 32];
    static int was_ours;
    const int ours = g_auto_on ? autostart_dmd(dmd_px) : -1;   /* between games in a cabinet */
    const int back = was_ours != (ours >= 0);       /* from one to the other: shown whatever it is */
    was_ours = ours >= 0;
    if (ours >= 0) {
        if (!ours && !back) return 0;
    } else {
        int bw = 0, bh = 0;
        if (!vga_compose_score(bar, &bw, &bh)) bw = bh = 0;
        if (!dmd_frame(bw ? bar : NULL, bw, bh, dmd_px) && !back) return 0;
    }
    dmd_new = 1;
    dmd_pipe_frame(dmd_px);
    if (g_opt.dmd || g_opt.cabinet) dmd_dots(dmd_px, dmd_big, DMD_S);
    return 1;
}

/* In a cabinet with --dmd the second window is the DMD's, never the game's scoreboard: dark when no table is up */
static int board_is_dmd(void) { return g_opt.cabinet && g_opt.dmd; }

/* What the player sees: the whole table when that is on and a table is up.
 * In a cabinet, the table without its scoreboard, and the scoreboard (black
 * when no table is up), both from video memory as it is at this moment. With
 * --dmd the messages go to the DMD, not the playfield, and the DMD takes the
 * scoreboard's window in a cabinet. */
static int holding;                  /* --table in a cabinet, the game over: the playfield keeps its picture */
static int between(void) { return g_auto_on && autostart_between(); }   /* the DMD is autostart's */
/* Sound: not while --table drives PINBALL fast, nor between games in a cabinet */
static int heard(void) { return !machine_is_fast() && !between(); }

static void compose_score(void)
{
    if ((board_is_dmd() || between()) && sw == DMD_W * DMD_S) return;   /* the DMD has the scoreboard's window */
    if (vga_compose_score(score, &sw, &sh)) dmd_bar_hide(score, sw, sh);
    else {
        sw = 320; sh = 29;
        for (int i = 0; i < sw * sh; i++) score[i] = 0xFF000000u;
    }
}

static void compose_view(void)
{
    static int inside;
    if ((dmd_on() || g_opt.cabinet) && !inside) {
        /* every message off the playfield while a DMD shows them; in a
         * cabinet anyway those it does not show (keyboard help) */
        if (!holding) {
            dmd_hide(dmd_showing() ? DMD_HIDE_ALL : DMD_HIDE_LISTED);
            inside = 1;
            compose_view();
            inside = 0;
            dmd_hide(0);
        } else if (g_opt.cabinet) compose_score();
        const int ours = between();                    /* between games: on the scoreboard's window too */
        if (!dmd_on() && !ours) return;
        const int changed = compose_dmd();
        if ((board_is_dmd() || (g_opt.cabinet && ours)) && (changed || sw != DMD_W * DMD_S)) {
            memcpy(score, dmd_big, sizeof dmd_big);
            sw = DMD_W * DMD_S; sh = DMD_H * DMD_S;
        }
        return;
    }
    if (g_opt.cabinet) {
        table_up = vga_compose_playfield(frame, &fw, &fh);
        frame_aspect = table_up ? (double)fw / fh : 4.0 / 3;   /* square pixels; the menus' 4:3 */
        compose_score();
        return;
    }
    if (holding) return;
    if (S.whole && !g_opt.classic && vga_compose_table(frame, &fw, &fh)) frame_aspect = (double)fw / fh;
    else compose();
    if (dmd_on() && vga_bar_row() >= 0)                /* with a DMD: hidden ones off the bar too (9-13) */
        dmd_bar_hide(frame + vga_bar_row() * fw, fw, fh - vga_bar_row());
}

/* Over the picture the player sees, after compose_view (shots and
 * recordings compose their own, without it): the nudge indicator while an
 * accelerometer is read, on the playfield's picture, so it turns with it */
int pad_nudge_indicator(void);
int pad_nudge_range(void);

static void compose_hud(void)
{
    int x, y;
    if (holding || fw != 320 || !vga_table_up() || !nudge_now(&x, &y)) return;   /* a table: not the menus, nor the text screen */
    if (!nudge_traced() && !pad_nudge_indicator()) return;
    hud_nudge(frame, fw, fh, frame_aspect * fh / fw, x, y, nudge_get_settings()->dead, pad_nudge_range());
}

/* How the playfield is drawn: stretched (F5) only while a table is up, the
 * menus always in their own shape */
static Present playfield_how(void)
{
    Present p = S;
    p.stretch = S.stretch && table_up;
    return p;
}

void present_rect(int cw, int ch, int w, int h, double aspect, int mode, int fill, RECT *r);
#ifdef _WIN32
int  present_window(HWND hw, const uint32_t *f, int w, int h, double aspect, const Present *s);
void present_forget(HWND hw);
void present_close(void);
#else
int  present_window(SDL_Window *wnd, const uint32_t *f, int w, int h, double aspect, const Present *s);
int  present_prepare(SDL_Window *wnd);
void present_close(void);
#endif
int  present_offscreen(const uint32_t *f, int w, int h, double aspect, const Present *s,
                       int ow, int oh, uint32_t *out);
void pad_load(void);
void pad_poll(int front);
void pad_key(int sc, int up);
const char *pad_ini(void);
int pad_plunger_on(void);
int pad_plunger(int front, PlungerInput *in);
int pad_nudge_on(void);
int pad_nudge_indicator(void);
void pad_nudge(int front);
static void plunger_let_go(void);

/* ---- recording ------------------------------------------------------------ */

#define REC_FPS 35
static FILE *rec_video, *rec_audio;
static uint64_t rec_frames, rec_samples;
static char rec_vpath[512], rec_apath[512];

/* 16-bit PCM: stereo, or with --ssf 7.1 (WAVE_FORMAT_EXTENSIBLE, the channels
 * FL FR FC LFE BL BR SL SR) */
static void wav_header(FILE *f, uint32_t frames, int channels)
{
    const int ext = channels > 2;
    uint16_t ch = (uint16_t)channels, ba = (uint16_t)(channels * 2), bits = 16, pcm = ext ? 0xFFFE : 1;
    uint32_t data = frames * ba, fmt = ext ? 40 : 16, riff = 20 + fmt + data, rate = AUDIO_RATE, br = AUDIO_RATE * ba;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f);
    fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    if (ext) {
        static const uint8_t pcm_guid[16] = { 1, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71 };
        uint16_t cb = 22, valid = 16;
        uint32_t mask = 0x63F;                     /* FL FR FC LFE BL BR SL SR */
        fwrite(&cb, 2, 1, f); fwrite(&valid, 2, 1, f); fwrite(&mask, 4, 1, f); fwrite(pcm_guid, 1, 16, f);
    }
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
}

static int out_channels(void) { return g_opt.ssf ? 8 : 2; }   /* --ssf: 7.1 */

static int rec_w = 640, rec_h = 400;

static void rec_open(void)
{
    snprintf(rec_vpath, sizeof rec_vpath, "%s.video.mp4", g_opt.record);
    snprintf(rec_apath, sizeof rec_apath, "%s.audio.wav", g_opt.record);
    char cmd[1200];
    snprintf(cmd, sizeof cmd,
             "ffmpeg -y -loglevel error -f rawvideo -pixel_format bgra -video_size %dx%d "
             "-framerate %d -i - -vf scale=%d:%d:flags=neighbor -c:v libx264 -pix_fmt yuv420p "
             "-crf 18 \"%s\"", rec_w, rec_h, REC_FPS, rec_w < 1280 ? 1280 : rec_w,
             (rec_w < 1280 ? 1280 : rec_w) * 3 / 4, rec_vpath);
#ifdef _WIN32
    rec_video = popen(cmd, "wb");
#else
    rec_video = popen(cmd, "w");
#endif
    if (!rec_video) fatal("cannot start ffmpeg (is it on PATH?)");
    rec_audio = fopen(rec_apath, "wb");
    if (rec_audio) wav_header(rec_audio, 0, 2);
}

static void rec_close(void)
{
    if (!rec_video) return;
    pclose(rec_video);
    rec_video = NULL;
    if (rec_audio) { wav_header(rec_audio, (uint32_t)rec_samples, 2); fclose(rec_audio); rec_audio = NULL; }
    char cmd[1600];
    snprintf(cmd, sizeof cmd, "ffmpeg -y -loglevel error -i \"%s\" -i \"%s\" -c:v copy -c:a aac -b:a 192k -shortest \"%s\"",
             rec_vpath, rec_apath, g_opt.record);
    if (system(cmd) == 0) { remove(rec_vpath); remove(rec_apath); }
    fprintf(stderr, "[rec] %s: %llu frames\n", g_opt.record, (unsigned long long)rec_frames);
}

/* Every frame to the encoder at one size, whatever mode the game is in */
static const uint32_t *as_rec(void)
{
    if (fw == rec_w && fh == rec_h) return frame;
    for (int y = 0; y < rec_h; y++)
        for (int x = 0; x < rec_w; x++)
            big[y * rec_w + x] = frame[(y * fh / rec_h) * fw + x * fw / rec_w];
    return big;
}

static FILE *wav_out;
static uint64_t wav_samples;

void host_snapshot(const char *path);

/* --shot-at "ms:file,...": frames at fixed points in emulated time, for the
 * conformance harness (tools/conformance.py) */
static void shots_due(uint64_t now)
{
    static const char *p;
    static int init;
    if (!init) { init = 1; p = g_opt.shots; }
    while (p && *p) {
        double ms; char path[260]; int n = 0;
        if (sscanf(p, "%lf:%259[^,]%n", &ms, path, &n) < 2) { p = NULL; return; }
        if (now < (uint64_t)(ms * PIT_HZ / 1000.0)) return;
        host_snapshot(path);
        p += n; if (*p == ',') p++;
    }
}

/* ---- scripted keys ----------------------------------------------------------- */

typedef struct { uint64_t at; uint16_t sc; uint8_t up; } KeyEv;
static KeyEv kev[512];
static int nkev, kev_i;

/* A key's scan code by name, | 0x100 when it is E0-prefixed (keys.c) */
int host_key_by_name(const char *s) { return key_by_name(s); }

static void keys_parse(void)
{
    const char *p = g_opt.keys;
    while (p && *p && nkev < 500) {
        char name[32]; double ms; int hold = 120, n = 0;
        if (sscanf(p, "%lf:%31[^,]%n", &ms, name, &n) < 2) break;
        char *h = strchr(name, '+');            /* KEY+holdms */
        if (h) { *h = 0; hold = atoi(h + 1); }
        int sc = host_key_by_name(name);
        uint64_t at = (uint64_t)(ms * PIT_HZ / 1000.0);
        kev[nkev++] = (KeyEv){ at, (uint16_t)sc, 0 };
        kev[nkev++] = (KeyEv){ at + (uint64_t)(hold * PIT_HZ / 1000.0), (uint16_t)sc, 1 };
        p += n; if (*p == ',') p++;
    }
    /* sort by time (insertion; small) */
    for (int i = 1; i < nkev; i++)
        for (int j = i; j > 0 && kev[j].at < kev[j - 1].at; j--) { KeyEv t = kev[j]; kev[j] = kev[j - 1]; kev[j - 1] = t; }
}


/* --table: while PINBALL is driven to the table, and out after it, the
 * player's keys and pad are not the game's; Esc while starting cancels */
static int driven(void) { return g_auto_on ? autostart_busy() : 0; }

static void keys_due(void)
{
    if (driven()) return;                          /* --keys counts from the table's game */
    while (kev_i < nkev && kev[kev_i].at <= pit_now()) {    /* as the keyboard's, through [keys] */
        pad_key(kev[kev_i].sc, kev[kev_i].up);
        kev_i++;
    }
}

/* --autoplay, for testing a table's feedback: a player that flips when a ball
 * comes down near the flippers, launches a ball waiting in the lane, and starts
 * the next game. Its keys go the way the keyboard's do. */
#define AUTO_FLIP_MS 140.0           /* a flip held */
#define AUTO_LAUNCH_MS 1200.0        /* the plunger pulled */
static void autoplay(void)
{
    static double until[5], again;   /* left, right, Space, a menu key, a nudge: down until then, resting after */
    static uint8_t sc[5] = { 0x2A, 0x36, 0x39, 0x1C, 0x2C };
    static double still;             /* since when no ball has moved: then a nudge, Z and / in turn */
    static int last_xy, nudges;
    static int menu, gone;           /* initials ABC, then Enter: PLAY GAME is first on the table's screen */
    static const double hold[5] = { AUTO_FLIP_MS, AUTO_FLIP_MS, AUTO_LAUNCH_MS, 100.0, 100.0 };
    if (!g_opt.autoplay || driven()) return;
    const double t = pit_now() * 1000.0 / PIT_HZ;
    uint32_t ds, cs;
    const WatchMap *m = watch_table(&ds, &cs);
    int want[5] = { 0, 0, 0, 0, 0 };
    if (!m) {                                        /* PINBALL's screen: initials, then PLAY GAME */
        if (!gone) { gone = 1; again = t + 3000.0; }    /* once it has come */
        if (t >= again && until[3] == 0) {
            static const uint8_t keys[4] = { 0x1E, 0x30, 0x2E, 0x1C };    /* A B C Enter */
            sc[3] = keys[menu++ % 4];
            want[3] = 1;
            again = t + 600.0;
        }
    } else if ((menu = gone = 0), watch_game_over() == 1) {   /* over: the scores, until Space */
        if (t >= again) { want[2] = 1; again = t + 2500.0; }
    } else {
        again = t + 8000.0;
        const uint8_t *d = g_cpu.mem + ds;
        int xy = 0;
        for (int i = 0; i < m->slots && i < 8; i++) {
            const uint16_t o = (uint16_t)(2 * i);
            if ((d[(uint16_t)(m->active + o)] | d[(uint16_t)(m->active + o + 1)] << 8) != 1) continue;
            const int x = d[(uint16_t)(m->x + o)] | d[(uint16_t)(m->x + o + 1)] << 8;
            const int y = d[(uint16_t)(m->y + o)] | d[(uint16_t)(m->y + o + 1)] << 8;
            const int vy = (int16_t)(d[(uint16_t)(m->vy + o)] | d[(uint16_t)(m->vy + o + 1)] << 8);
            xy = xy * 31 + x * 401 + y;
            if (y > m->drain_y - 75 && y < m->drain_y - 15 && vy > 0 && (m->lane_x == 0 || x < m->lane_x))
                want[x < 160 ? 0 : 1] = 1;
        }
        want[2] = plunger_lane(m, d, 1);
        if (xy != last_xy || want[2]) { last_xy = xy; still = t; }
        else if (t - still > 10000.0) {          /* stuck somewhere: shake it loose */
            sc[4] = nudges++ % 2 ? 0x35 : 0x2C;
            want[4] = 1;
            still = t;
        }
    }
    for (int k = 0; k < 5; k++) {
        if (want[k] && until[k] == 0) { pad_key(sc[k], 0); until[k] = t + hold[k]; }
        else if (until[k] > 0 && t >= until[k]) { pad_key(sc[k], 1); until[k] = -(t + hold[k]); }   /* let go, then rest */
        else if (until[k] < 0 && t >= -until[k]) until[k] = 0;
    }
}

/* ---- settings ------------------------------------------------------------------ */

static int mode_by_name(const char *s)
{
    for (int i = 0; i < PRESENT_MODES; i++) if (!_stricmp(s, mode_names[i])) return i;
    return PRESENT_SHARP;
}

static char ini[512];
static Present saved;                /* what the ini says: the command line never reaches it */
static int saved_full;
static RECT win_rect;                /* the window's place when not fullscreen */

/* --cabinet's own: what the ini says, and what this run uses */
static int saved_display, cab_display;
static RECT saved_score, cab_score;  /* empty: the default place */
static RECT saved_dmdw;              /* the DMD window's, in a cabinet with --dmd: kept apart, another shape */
static char saved_hide[1024];        /* [cabinet] hide_messages as written, kept when the file is written again */
static char saved_ssf[15][128];      /* [ssf]: effects_volume, rolling_volume, board_ep1..13 as written; "" unset */
static const char *const ssf_keys[15] = { "effects_volume", "rolling_volume", "board_ep1", "board_ep2", "board_ep3",
    "board_ep4", "board_ep5", "board_ep6", "board_ep7", "board_ep8", "board_ep9", "board_ep10", "board_ep11", "board_ep12", "board_ep13" };
static int saved_hide_set;

/* --score-window's and the ini's X,Y,W,H */
int host_parse_place(const char *s, RECT *r)
{
    long x, y, w, h;
    char over;
    if (sscanf(s, "%ld,%ld,%ld,%ld%c", &x, &y, &w, &h, &over) != 4) return 0;
    if (w < 32 || h < 8 || w > 32767 || h > 32767 || x < -65536 || x > 65536 || y < -65536 || y > 65536) return 0;
    *r = (RECT){ x, y, x + w, y + h };
    return 1;
}

/* Degrees to quarter turns, or -1 */
int host_parse_turn(const char *s)
{
    static const char *const deg[] = { "0", "90", "180", "270" };
    for (int i = 0; i < 4; i++) if (!strcmp(s, deg[i])) return i;
    return -1;
}

/* Each setting the window changes, set in place: the rest of the file (its
 * comments, [ssf], hide_messages, [modes] and whatever the settings tool
 * wrote) is left as it is */
static void settings_save(void)
{
    if (!*ini) return;
    char v[96];
    ini_set(ini, "display", "scale", mode_names[saved.mode]);
    ini_set(ini, "display", "look", saved.look ? "vivid" : "natural");
    ini_set(ini, "display", "bars", saved.bars ? "blur" : "black");
    ini_set(ini, "display", "whole_table", saved.whole ? "1" : "0");
    ini_set(ini, "display", "fullscreen", saved_full ? "1" : "0");
    if (win_rect.right > win_rect.left) {
        snprintf(v, sizeof v, "%ld,%ld,%ld,%ld", (long)win_rect.left, (long)win_rect.top,
                 (long)(win_rect.right - win_rect.left), (long)(win_rect.bottom - win_rect.top));
        ini_set(ini, "display", "window", v);
    }
    ini_set(ini, "sound", "output", g_audio_smooth ? "smooth" : "original");
    int placed = saved_score.right > saved_score.left, placed_dmd = saved_dmdw.right > saved_dmdw.left;
    if (g_opt.cabinet || placed || placed_dmd || saved.rot || saved.stretch || saved_display) {
        snprintf(v, sizeof v, "%d", saved.rot * 90);
        ini_set(ini, "cabinet", "rotation", v);
        ini_set(ini, "cabinet", "stretch", saved.stretch ? "1" : "0");
        snprintf(v, sizeof v, "%d", saved_display);
        ini_set(ini, "cabinet", "playfield_display", v);
        if (placed) {
            snprintf(v, sizeof v, "%ld,%ld,%ld,%ld", (long)saved_score.left, (long)saved_score.top,
                     (long)(saved_score.right - saved_score.left), (long)(saved_score.bottom - saved_score.top));
            ini_set(ini, "cabinet", "score_window", v);
        }
        if (placed_dmd) {
            snprintf(v, sizeof v, "%ld,%ld,%ld,%ld", (long)saved_dmdw.left, (long)saved_dmdw.top,
                     (long)(saved_dmdw.right - saved_dmdw.left), (long)(saved_dmdw.bottom - saved_dmdw.top));
            ini_set(ini, "cabinet", "dmd_window", v);
        }
    }
}

static void settings_load(void)
{
    char v[64];
    snprintf(ini, sizeof ini, "%s/epicpinball.ini", g_opt.savedir);
    S.mode = mode_by_name(ini_get(ini, "display", "scale", "sharp", v, sizeof v));
    S.look = !_stricmp(ini_get(ini, "display", "look", "natural", v, sizeof v), "vivid");
    S.bars = _stricmp(ini_get(ini, "display", "bars", "blur", v, sizeof v), "black") != 0;
    S.whole = atoi(ini_get(ini, "display", "whole_table", "0", v, sizeof v));
    saved = S;
    int turn = host_parse_turn(ini_get(ini, "cabinet", "rotation", "0", v, sizeof v));
    saved.rot = turn < 0 ? 0 : turn;
    saved.stretch = atoi(ini_get(ini, "cabinet", "stretch", "0", v, sizeof v)) != 0;
    saved_display = atoi(ini_get(ini, "cabinet", "playfield_display", "0", v, sizeof v));
    {                                              /* messages a cabinet does not show: the built-in list unless set */
        const char *h = ini_get(ini, "cabinet", "hide_messages", "\x01", saved_hide, sizeof saved_hide);
        saved_hide_set = h[0] != 1;
        if (saved_hide_set) { if (h != saved_hide) snprintf(saved_hide, sizeof saved_hide, "%s", h); dmd_set_hidden(saved_hide); }
    }
    {                                              /* [ssf]: volumes, and tables' board effects */
        for (int i = 0; i < 15; i++) {
            const char *x = ini_get(ini, "ssf", ssf_keys[i], "", saved_ssf[i], sizeof saved_ssf[i]);
            if (x != saved_ssf[i]) snprintf(saved_ssf[i], sizeof saved_ssf[i], "%s", x);
        }
        double fx = saved_ssf[0][0] ? atof(saved_ssf[0]) : SSF_GAIN, roll = saved_ssf[1][0] ? atof(saved_ssf[1]) : SSF_GAIN;
        ssf_volumes(fx < 0 ? 0 : fx, roll < 0 ? 0 : roll);
        for (int i = 2; i < 15; i++)
            if (saved_ssf[i][0] && !ssf_board(ssf_keys[i] + 6, saved_ssf[i]))
                fprintf(stderr, "[ssf] %s=%s: not a list of effect slots (hex, comma between)\n", ssf_keys[i], saved_ssf[i]);
    }
    if (!host_parse_place(ini_get(ini, "cabinet", "score_window", "", v, sizeof v), &saved_score))
        memset(&saved_score, 0, sizeof saved_score);
    if (!host_parse_place(ini_get(ini, "cabinet", "dmd_window", "", v, sizeof v), &saved_dmdw))
        memset(&saved_dmdw, 0, sizeof saved_dmdw);
    if (g_opt.cabinet) {
        S.rot = saved.rot; S.stretch = saved.stretch; cab_display = saved_display;
        cab_score = g_opt.dmd ? saved_dmdw : saved_score;      /* the second window: the DMD's or the scoreboard's */
    }
    saved_full = atoi(ini_get(ini, "display", "fullscreen", "0", v, sizeof v));
    if (saved_full) g_opt.fullscreen = 1;
    long x, y, w, h;
    if (sscanf(ini_get(ini, "display", "window", "", v, sizeof v), "%ld,%ld,%ld,%ld", &x, &y, &w, &h) == 4
        && w > 100 && h > 100) {
        RECT r = { x, y, x + w, y + h };
#ifdef _WIN32
        if (MonitorFromRect(&r, MONITOR_DEFAULTTONULL))   /* still on a screen */
#endif
            win_rect = r;
    }
    g_audio_smooth = _stricmp(ini_get(ini, "sound", "output", "smooth", v, sizeof v), "original") != 0;
}

/* The command line wins over the ini, and is all a headless run reads */
static void settings_args(void)
{
    if (g_opt.scale) S.mode = mode_by_name(g_opt.scale);
    if (g_opt.look) S.look = !_stricmp(g_opt.look, "vivid");
    if (g_opt.bars) S.bars = _stricmp(g_opt.bars, "black") != 0;
    if (g_opt.whole) S.whole = 1;
    if (g_opt.classic) { S.mode = PRESENT_NEAREST; S.look = S.bars = S.whole = 0; }
    if (g_opt.rotate) S.rot = host_parse_turn(g_opt.rotate);
    if (g_opt.stretch) S.stretch = 1;
    if (g_opt.display) cab_display = atoi(g_opt.display);
    if (g_opt.score_window) host_parse_place(g_opt.score_window, &cab_score);
    if (g_opt.ssf_volumes) {
        double fx = SSF_GAIN, roll = SSF_GAIN;
        if (sscanf(g_opt.ssf_volumes, "%lf,%lf", &fx, &roll) < 1) fprintf(stderr, "--ssf-volumes takes FX,ROLL, e.g. 4,0\n");
        else ssf_volumes(fx < 0 ? 0 : fx, roll < 0 ? 0 : roll);
    }
}

/* ---- window ---------------------------------------------------------------- */

static int quit_req, is_full;
static void set_fullscreen(int on);
static void open_file(const char *path);

/* PrtSc: what is on screen, to screenshots/epic-NNN.bmp */
static void screenshot_key(void)
{
    char p[256];
    _mkdir("screenshots");
    for (int n = 0; n < 1000; n++) {
        snprintf(p, sizeof p, "screenshots/epic-%03d.bmp", n);
        FILE *f = fopen(p, "rb");
        if (!f) { host_snapshot(p); return; }
        fclose(f);
    }
}

enum { ID_MODE = 100, ID_LOOK = 120, ID_WHOLE = 130, ID_BARS_BLUR, ID_BARS_BLACK, ID_FULL,
       ID_SOUND_SMOOTH, ID_SOUND_ORIG, ID_SHOT, ID_TURN, ID_STRETCH, ID_SETTINGS };
static void settings_tool(void);

/* A change made in the window: shown, and remembered */
static void command(int id)
{
    if (g_opt.cabinet && (id == ID_WHOLE || id == ID_FULL)) return;     /* it is always both */
    if (g_opt.cabinet && id == ID_TURN) S.rot = saved.rot = (S.rot + 1) % 4;
    else if (g_opt.cabinet && id == ID_STRETCH) S.stretch = saved.stretch = !S.stretch;
    else if (id >= ID_MODE && id < ID_MODE + PRESENT_MODES) S.mode = saved.mode = id - ID_MODE;
    else if (id == ID_LOOK || id == ID_LOOK + 1) S.look = saved.look = id - ID_LOOK;
    else if (id == ID_WHOLE) S.whole = saved.whole = !S.whole;
    else if (id == ID_BARS_BLUR || id == ID_BARS_BLACK) S.bars = saved.bars = id == ID_BARS_BLUR;
    else if (id == ID_FULL) { set_fullscreen(!is_full); saved_full = is_full; }
    else if (id == ID_SOUND_SMOOTH || id == ID_SOUND_ORIG) g_audio_smooth = id == ID_SOUND_SMOOTH;
    else if (id == ID_SHOT) screenshot_key();
    else if (id == ID_SETTINGS) settings_tool();
    else return;
    settings_save();
}

#ifdef _WIN32
static HWND wnd;                       /* with --cabinet, the playfield */
static BITMAPINFO bmi;

/* --cabinet's scoreboard window (the DMD's, with --dmd), or on the desktop
 * --dmd's window of its own */
static struct {
    HWND wnd;
    int dirty;
    uint64_t settled;                /* from here a move is the player's */
    RECT at;                         /* where it opened, then where it was last */
    uint32_t shown[FRAME_MAX];       /* what is in it */
    int shown_w, shown_h;
    Present how;
} board;

/* With a real DMD (--dmd-device) the scoreboard is on it: no window for it,
 * unless --dmd asks for the DMD in a window too */
static int board_wanted(void) { return !g_opt.dmd_device || g_opt.dmd; }

static void open_file(const char *path) { ShellExecuteA(wnd, "open", path, NULL, NULL, SW_SHOWNORMAL); }
/* The settings tool (config/, docs/config.md), beside this program; without
 * it, controller.ini in an editor. What it saves the game reads when its
 * window next comes to the front (and the modes at its next start). */
static void settings_tool(void)
{
    char me[MAX_PATH], args[600];
    const DWORD n = GetModuleFileNameA(NULL, me, sizeof me);
    char *slash = n && n < sizeof me ? strrchr(me, '\\') : NULL;
    if (slash && (size_t)(slash + 1 - me) + sizeof "epicpinball-config.exe" <= sizeof me) {
        strcpy(slash + 1, "epicpinball-config.exe");
        if (GetFileAttributesA(me) != INVALID_FILE_ATTRIBUTES) {
            snprintf(args, sizeof args, "--save \"%s\"", g_opt.savedir);
            if ((INT_PTR)ShellExecuteA(wnd, "open", me, args, NULL, SW_SHOWNORMAL) > 32) return;
        }
    }
    fprintf(stderr, "[settings] no epicpinball-config.exe beside the game (cmake -DEPIC_CONFIG=ON): controller.ini instead\n");
    open_file(pad_ini());
}

static void set_fullscreen(int on)
{
    if (on == is_full) return;
    DWORD st = GetWindowLong(wnd, GWL_STYLE);
    if (on) {
        MONITORINFO mi = { sizeof mi };
        GetWindowRect(wnd, &win_rect);
        GetMonitorInfo(MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLong(wnd, GWL_STYLE, st & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(wnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLong(wnd, GWL_STYLE, st | WS_OVERLAPPEDWINDOW);
        SetWindowPos(wnd, NULL, win_rect.left, win_rect.top, win_rect.right - win_rect.left,
                     win_rect.bottom - win_rect.top, SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    is_full = on;
}

/* GDI, when Direct3D is not there: a stretch, nearest or halftone, no extras,
 * and not turned */
static void paint_gdi(HWND hw, const uint32_t *px, int w, int h, double aspect, int stretch)
{
    HDC dc = GetDC(hw);
    RECT c, r; GetClientRect(hw, &c);
    present_rect(c.right, c.bottom, w, h, aspect, S.mode, stretch, &r);
    PatBlt(dc, 0, 0, c.right, r.top, BLACKNESS); PatBlt(dc, 0, r.bottom, c.right, c.bottom - r.bottom, BLACKNESS);
    PatBlt(dc, 0, 0, r.left, c.bottom, BLACKNESS); PatBlt(dc, r.right, 0, c.right - r.right, c.bottom, BLACKNESS);
    bmi.bmiHeader.biWidth = w; bmi.bmiHeader.biHeight = -h;
    SetStretchBltMode(dc, S.mode == PRESENT_SMOOTH ? HALFTONE : COLORONCOLOR);
    SetBrushOrgEx(dc, 0, 0, NULL);
    StretchDIBits(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, 0, 0, w, h, px, &bmi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(hw, dc);
}

/* A picture into a window: the presenter, else GDI */
static void show(HWND hw, const uint32_t *px, int w, int h, double aspect, const Present *how)
{
    if (!g_opt.classic && present_window(hw, px, w, h, aspect, how)) return;
    paint_gdi(hw, px, w, h, aspect, how->stretch);
}

static void paint(void)
{
    const Present v = playfield_how();
    show(wnd, frame, fw, fh, frame_aspect, &v);
}

static void menu(int x, int y)
{
    static const char *const mode_labels[] = { "Sharp", "Smooth", "CRT", "Nearest (original pixels)", "Integer (whole multiples)" };
    HMENU m = CreatePopupMenu(), sc = CreatePopupMenu(), look = CreatePopupMenu(),
          bars = CreatePopupMenu(), snd = CreatePopupMenu();
    for (int i = 0; i < PRESENT_MODES; i++)
        AppendMenuA(sc, MF_STRING | (S.mode == i ? MF_CHECKED : 0), ID_MODE + i, mode_labels[i]);
    AppendMenuA(look, MF_STRING | (!S.look ? MF_CHECKED : 0), ID_LOOK, "Natural");
    AppendMenuA(look, MF_STRING | (S.look ? MF_CHECKED : 0), ID_LOOK + 1, "Vivid");
    AppendMenuA(bars, MF_STRING | (S.bars ? MF_CHECKED : 0), ID_BARS_BLUR, "Blurred");
    AppendMenuA(bars, MF_STRING | (!S.bars ? MF_CHECKED : 0), ID_BARS_BLACK, "Black");
    AppendMenuA(snd, MF_STRING | (g_audio_smooth ? MF_CHECKED : 0), ID_SOUND_SMOOTH, "Smooth");
    AppendMenuA(snd, MF_STRING | (!g_audio_smooth ? MF_CHECKED : 0), ID_SOUND_ORIG, "Original (stepped)");
    if (g_opt.cabinet) {                           /* always the whole table, always filling its display */
        char turn[48];
        snprintf(turn, sizeof turn, "Turn the playfield (now %d degrees)\tF6", S.rot * 90);
        AppendMenuA(m, MF_STRING, ID_TURN, turn);
        AppendMenuA(m, MF_STRING | (S.stretch ? MF_CHECKED : 0), ID_STRETCH, "Stretch the playfield\tF5");
    } else
        AppendMenuA(m, MF_STRING | (S.whole ? MF_CHECKED : 0), ID_WHOLE, "Whole table\tF7");
    AppendMenuA(m, MF_POPUP, (UINT_PTR)look, "Colour\tF9");
    AppendMenuA(m, MF_POPUP, (UINT_PTR)sc, "Scaling\tF12");
    AppendMenuA(m, MF_POPUP, (UINT_PTR)bars, "Beside the picture");
    if (!g_opt.cabinet) AppendMenuA(m, MF_STRING | (is_full ? MF_CHECKED : 0), ID_FULL, "Fullscreen\tF11");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_POPUP, (UINT_PTR)snd, "Sound");
    AppendMenuA(m, MF_STRING, ID_SETTINGS, "Settings...");     /* the settings tool, else controller.ini */
    AppendMenuA(m, MF_STRING, ID_SHOT, "Screenshot\tPrtSc");
    uint64_t t = host_us();
    feedback_all_off();                            /* the game stands still under it */
    plunger_let_go();
    watch_released();
    int id = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, wnd, NULL);
    machine_paused(host_us() - t);                 /* the game stood still meanwhile */
    DestroyMenu(m);                                /* and its submenus */
    command(id);
    board.dirty = 1;
}

/* Saved only when it has moved since it opened: --score-window alone is for
 * the one run, as the other options are */
static void board_moved(void)
{
    RECT r; GetWindowRect(board.wnd, &r);
    if (host_us() >= board.settled && memcmp(&r, &board.at, sizeof r)) *(g_opt.dmd ? &saved_dmdw : &saved_score) = r;
    board.at = r;
}

/* The cabinet's scoreboard has no frame: dragged by its middle, resized by its edges */
static LRESULT board_hit(HWND h, LPARAM lp)
{
    POINT p = { (short)LOWORD(lp), (short)HIWORD(lp) };
    RECT c; GetClientRect(h, &c);
    ScreenToClient(h, &p);
    int ex = c.right / 4 < 10 ? c.right / 4 : 10, ey = c.bottom / 4 < 10 ? c.bottom / 4 : 10;
    int l = p.x < ex, r = p.x >= c.right - ex, t = p.y < ey, b = p.y >= c.bottom - ey;
    if (t) return l ? HTTOPLEFT : r ? HTTOPRIGHT : HTTOP;
    if (b) return l ? HTBOTTOMLEFT : r ? HTBOTTOMRIGHT : HTBOTTOM;
    return l ? HTLEFT : r ? HTRIGHT : HTCAPTION;
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    const int is_board = board.wnd && h == board.wnd;
    switch (m) {
    case WM_CLOSE:
        if (is_board && !g_opt.cabinet) { ShowWindow(h, SW_HIDE); return 0; }   /* the game goes on */
        quit_req = 1;
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; BeginPaint(h, &ps); EndPaint(h, &ps);
        if (is_board) board.dirty = 1;
        else paint();
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: if (is_board) board.dirty = 1; break;
    case WM_NCCALCSIZE:                            /* the cabinet's scoreboard: sizing edges, no frame drawn */
        if (is_board && g_opt.cabinet && wp) return 0;
        break;
    case WM_NCHITTEST:
        if (is_board && g_opt.cabinet) return board_hit(h, lp);
        break;
    case WM_NCLBUTTONDBLCLK:
        if (is_board && g_opt.cabinet) return 0;   /* no maximising by its "title" */
        break;
    case WM_SETCURSOR:                             /* no pointer over the cabinet's playfield */
        if (g_opt.cabinet && !is_board && LOWORD(lp) == HTCLIENT) { SetCursor(NULL); return TRUE; }
        break;
    case WM_SYSKEYDOWN: case WM_KEYDOWN: case WM_SYSKEYUP: case WM_KEYUP: {
        int up = (m == WM_KEYUP || m == WM_SYSKEYUP);
        if (wp == VK_RETURN && (lp & (1 << 29))) { if (!up) command(ID_FULL); return 0; }
        if (wp == VK_F4 && (lp & (1 << 29))) { quit_req = 1; return 0; }
        if (wp == VK_SNAPSHOT) { if (up) screenshot_key(); return 0; }   /* Windows sends PrtSc's up only */
        if (g_opt.cabinet && (wp == VK_F5 || wp == VK_F6)) {          /* press and release: any key ends the demo */
            if (!up && !(lp & (1 << 30))) command(wp == VK_F5 ? ID_STRETCH : ID_TURN);
            return 0;
        }
        if (wp == VK_F7 || (wp >= VK_F9 && wp <= VK_F12)) {
            if (up) return 0;
            if (wp == VK_F7) command(ID_WHOLE);
            if (wp == VK_F9) command(ID_LOOK + !S.look);
            if (wp == VK_F10) { RECT r; GetWindowRect(h, &r); menu(r.left + 40, r.top + 40); }
            if (wp == VK_F11) command(ID_FULL);
            if (wp == VK_F12) command(ID_MODE + (S.mode + 1) % PRESENT_MODES);
            return 0;
        }
        if (!up && (lp & (1 << 30))) return 0;      /* the game does its own repeat */
        if (!up && wp == VK_ESCAPE && driven() == 1) { quit_req = 1; return 0; }
        pad_key(((lp >> 16) & 0xFF) | (lp & (1 << 24) ? KEY_E0 : 0), up);   /* [keys] */
        return 0;
    }
    case WM_SYSCHAR: return 0;                       /* no menu beep on Alt */
    case WM_CONTEXTMENU: {                           /* the game has no mouse: right-click is ours */
        POINT p = { (short)LOWORD(lp), (short)HIWORD(lp) };
        if (p.x == -1 && p.y == -1) { RECT r; GetWindowRect(h, &r); p.x = r.left + 40; p.y = r.top + 40; }
        menu(p.x, p.y);
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE) pad_load();   /* controller.ini may have been edited */
        break;
    case WM_EXITSIZEMOVE:
        if (is_board) { if (g_opt.cabinet) board_moved(); }
        else if (!g_opt.cabinet && !is_full && !IsZoomed(h)) { GetWindowRect(h, &win_rect); settings_save(); }
        break;
    }
    return DefWindowProcA(h, m, wp, lp);
}

/* waveOut: a ring the main thread fills and a few buffers cycling under it.
 * With --ssf it is opened as 7.1 (FL FR FC LFE BL BR SL SR, as SDL orders
 * them); Windows mixes that to the device as its speakers are set up. */
#define WBUF 8
#define WLEN 1024
static HWAVEOUT wout;
static WAVEHDR whdr[WBUF];
static int16_t wdata[WBUF][WLEN * 8];
static int wfill, wcur, wch = 2;     /* the channels it was opened with */

static int wave_try(int ch)
{
    static const GUID pcm = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
    WAVEFORMATEXTENSIBLE x;
    memset(&x, 0, sizeof x);
    x.Format.wFormatTag = ch > 2 ? WAVE_FORMAT_EXTENSIBLE : WAVE_FORMAT_PCM;
    x.Format.nChannels = (WORD)ch;
    x.Format.nSamplesPerSec = AUDIO_RATE;
    x.Format.wBitsPerSample = 16;
    x.Format.nBlockAlign = (WORD)(ch * 2);
    x.Format.nAvgBytesPerSec = AUDIO_RATE * ch * 2;
    x.Format.cbSize = ch > 2 ? (WORD)(sizeof x - sizeof x.Format) : 0;
    x.Samples.wValidBitsPerSample = 16;
    x.dwChannelMask = 0x63F;
    x.SubFormat = pcm;
    return waveOutOpen(&wout, WAVE_MAPPER, &x.Format, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR;
}

static void wave_open(void)
{
    wch = out_channels();
    if (!wave_try(wch)) {
        wout = NULL;
        if (wch == 2) return;
        fprintf(stderr, "[ssf] the sound device takes no 7.1, so the exciters are silent: set its speakers"
                        " to 7.1 in Windows' sound settings\n");
        wch = 2;
        if (!wave_try(2)) { wout = NULL; return; }
    } else if (g_opt.ssf) fprintf(stderr, "[ssf] sound out: 7.1 to the default device\n");
    for (int i = 0; i < WBUF; i++) {
        whdr[i].lpData = (LPSTR)wdata[i]; whdr[i].dwBufferLength = WLEN * wch * 2;
        waveOutPrepareHeader(wout, &whdr[i], sizeof whdr[i]);
        whdr[i].dwFlags |= WHDR_DONE;
    }
}

/* s: out_channels() a frame; the front pair only, if 7.1 was refused */
static void wave_push(const int16_t *s, int frames)
{
    const int in = out_channels();
    while (frames > 0) {
        WAVEHDR *h = &whdr[wcur];
        if (!(h->dwFlags & WHDR_DONE)) return;       /* all queued: drop rather than block */
        int n = WLEN - wfill; if (n > frames) n = frames;
        int16_t *d = &wdata[wcur][wfill * wch];
        if (in == wch) memcpy(d, s, (size_t)n * wch * 2);
        else for (int i = 0; i < n; i++) { d[i * 2] = s[i * in]; d[i * 2 + 1] = s[i * in + 1]; }
        wfill += n; s += n * in; frames -= n;
        if (wfill == WLEN) {
            h->dwFlags &= ~WHDR_DONE;
            waveOutWrite(wout, h, sizeof *h);
            wfill = 0; wcur = (wcur + 1) % WBUF;
        }
    }
}

/* ---- --cabinet's windows */

typedef struct { RECT r; char name[40]; int primary; } Display;
static Display displays[16];
static int ndisplays;

static BOOL CALLBACK display_found(HMONITOR m, HDC dc, LPRECT rc, LPARAM lp)
{
    MONITORINFOEXA mi;
    (void)dc; (void)rc; (void)lp;
    mi.cbSize = sizeof mi;
    if (ndisplays < 16 && GetMonitorInfoA(m, (MONITORINFO *)&mi)) {
        Display *d = &displays[ndisplays++];
        d->r = mi.rcMonitor;
        d->primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        snprintf(d->name, sizeof d->name, "%s", mi.szDevice);
    }
    return TRUE;
}

/* The displays, the main one first, then left to right, top to bottom */
static void displays_list(void)
{
    ndisplays = 0;
    EnumDisplayMonitors(NULL, NULL, display_found, 0);
    for (int i = 1; i < ndisplays; i++)
        for (int j = i; j > 0; j--) {
            const Display *a = &displays[j - 1], *b = &displays[j];
            int before = b->primary > a->primary
                      || (b->primary == a->primary && (b->r.left < a->r.left || (b->r.left == a->r.left && b->r.top < a->r.top)));
            if (!before) break;
            Display t = displays[j]; displays[j] = displays[j - 1]; displays[j - 1] = t;
        }
}

static void cabinet_open(HINSTANCE hi)
{
    uint64_t t = host_us();
    displays_list();
    if (ndisplays < 1) fatal("no display found");
    for (int i = 0; i < ndisplays; i++) {
        const RECT *b = &displays[i].r;
        fprintf(stderr, "[cabinet] display %d: %s %ldx%ld at %ld,%ld\n", i, displays[i].name,
                (long)(b->right - b->left), (long)(b->bottom - b->top), (long)b->left, (long)b->top);
    }
    int pd = cab_display;
    if (pd < 0 || pd >= ndisplays) {
        fprintf(stderr, "[cabinet] there is no display %d: the playfield goes on display 0\n", pd);
        pd = 0;
    }
    RECT r = cab_score;
    int placed = r.right > r.left;
    if (placed && !MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) {
        fprintf(stderr, "[cabinet] %ld,%ld is on no display: the scoreboard opens in its default place\n",
                (long)r.left, (long)r.top);
        placed = 0;
    }
    int bd = ndisplays > 1 ? (pd ? 0 : 1) : pd;    /* a display of its own, if there is one */
    if (!placed) {
        const RECT b = displays[bd].r;
        int bw = b.right - b.left, bh = b.bottom - b.top;
        int w = bw < 960 ? bw : 960, h = g_opt.dmd ? w / 4 : w * 29 / 320;
        r.left = b.left + (bw - w) / 2;
        r.top = ndisplays > 1 ? b.top + (bh - h) / 2 : b.top;
        r.right = r.left + w; r.bottom = r.top + h;
    }
    board.at = r;
    if (board_wanted()) {
        board.wnd = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, "epicpinball",
                                    g_opt.dmd ? "Epic Pinball DMD" : "Epic Pinball scoreboard", WS_POPUP | WS_THICKFRAME,
                                    r.left, r.top, r.right - r.left, r.bottom - r.top, NULL, NULL, hi, NULL);
        if (!board.wnd) fatal("the scoreboard window: error %lu", (unsigned long)GetLastError());
        SetWindowPos(board.wnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    } else fprintf(stderr, "[cabinet] the scoreboard is on the DMD: no window for it\n");
    const RECT p = displays[pd].r;                 /* the playfield: the whole display, borderless */
    wnd = CreateWindowExA(0, "epicpinball", "Epic Pinball", WS_POPUP, p.left, p.top, p.right - p.left,
                          p.bottom - p.top, NULL, NULL, hi, NULL);
    if (!wnd) fatal("the playfield window: error %lu", (unsigned long)GetLastError());

    /* the scoreboard first, the playfield last, which gets the keys */
    compose_view();
    board.how = S; board.how.rot = board.how.bars = board.how.stretch = 0;
    if (board.wnd) {
        ShowWindow(board.wnd, SW_SHOWNOACTIVATE);
        show(board.wnd, score, sw, sh, (double)sw / sh, &board.how);
    }
    ShowWindow(wnd, SW_SHOW);
    paint();
    SetForegroundWindow(wnd);
    board.dirty = 1;
    board.settled = host_us() + 1000000;
    machine_paused(host_us() - t);
}

/* Both windows; the scoreboard only when there is something new in it */
static void cabinet_present(void)
{
    uint64_t t0 = host_us();
    paint();
    uint64_t t1 = host_us(), t2 = t1;
    Present how = S;
    how.rot = how.bars = how.stretch = 0;            /* upright, black round it */
    size_t bytes = (size_t)sw * sh * 4;
    if (board.wnd && (board.dirty || board.shown_w != sw || board.shown_h != sh
        || memcmp(&how, &board.how, sizeof how) || memcmp(board.shown, score, bytes))) {
        show(board.wnd, score, sw, sh, (double)sw / sh, &how);
        memcpy(board.shown, score, bytes);
        board.shown_w = sw; board.shown_h = sh; board.how = how; board.dirty = 0;
        t2 = host_us();
    }
    if (t2 - t0 > 1000)
        trace("[present] slow at %llu ms: playfield %u us, scoreboard %u us\n",
              (unsigned long long)(emu_us() / 1000), (unsigned)(t1 - t0), (unsigned)(t2 - t1));
}

static void desktop_open(HINSTANCE hi)
{
    int x = win_rect.left, y = win_rect.top, w = win_rect.right - win_rect.left, h = win_rect.bottom - win_rect.top;
    if (g_opt.window || w <= 0) {                  /* --window N, or no place remembered */
        int s = g_opt.window ? g_opt.window : 3;
        RECT r = { 0, 0, 320 * s, 240 * s };
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        x = y = CW_USEDEFAULT; w = r.right - r.left; h = r.bottom - r.top;
    }
    wnd = CreateWindowA("epicpinball", "Epic Pinball", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                        x, y, w, h, NULL, NULL, hi, NULL);
    GetWindowRect(wnd, &win_rect);
    if (g_opt.dmd) {                               /* --dmd: a window of its own, beside */
        RECT r = { 0, 0, DMD_W * 6, DMD_H * 6 };
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        board.wnd = CreateWindowA("epicpinball", "Epic Pinball DMD", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                  r.right - r.left, r.bottom - r.top, NULL, NULL, hi, NULL);
        if (!board.wnd) fatal("the DMD window: error %lu", (unsigned long)GetLastError());
        board.how = S; board.how.rot = board.how.bars = board.how.stretch = 0;
        board.dirty = 1;
        ShowWindow(board.wnd, SW_SHOWNOACTIVATE);
        SetForegroundWindow(wnd);
    }
    if (g_opt.fullscreen) set_fullscreen(1);
}

static void window_open(void)
{
    bmi.bmiHeader.biSize = sizeof bmi.bmiHeader;
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.lpszClassName = "epicpinball";
    wc.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(1));
    RegisterClassA(&wc);
    if (g_opt.cabinet) cabinet_open(wc.hInstance);
    else desktop_open(wc.hInstance);
    if (!g_opt.mute) wave_open();
}

/* In a cabinet either window may have the keys */
static int window_front(void)
{
    HWND f = GetForegroundWindow();
    return f == wnd || (g_opt.cabinet && board.wnd && f == board.wnd);
}

/* Messages and the pad */
static void window_events(void)
{
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    pad_poll(window_front());                      /* while driven too: pad.c counts, sends nothing */
    if (quit_req && driven() != 2) {                /* leaving: PINBALL is saving */
        if (g_auto_on && autostart_quit()) quit_req = 0;   /* between games: the initials in, then out */
        else dos_exit(0);
    }
}

/* Messages, the pad, and the picture */
static void window_frame(void)
{
    window_events();
    compose_view();
    compose_hud();
    if (g_opt.cabinet) cabinet_present();
    else {
        paint();
        if (board.wnd && IsWindowVisible(board.wnd) && (dmd_new || board.dirty)) {
            show(board.wnd, dmd_big, DMD_W * DMD_S, DMD_H * DMD_S, 4.0, &board.how);
            dmd_new = board.dirty = 0;
        }
    }
}

static void window_live(void) {}

static void window_audio(const int16_t *s, int frames) { if (wout) wave_push(s, frames); }

static void window_close(void)
{
    if (wout) { waveOutReset(wout); waveOutClose(wout); }
    if (wnd) {
        settings_save();
        present_close();
        if (board.wnd) DestroyWindow(board.wnd);
        DestroyWindow(wnd);
    }
}

#else /* SDL2: Linux */

static SDL_Window *wnd;                /* with --cabinet, the playfield */
static SDL_AudioDeviceID aud;

static struct {
    SDL_Window *wnd;
    int x11;                         /* else windows cannot be placed */
    int fills;                       /* so then it fills a display of its own */
    int dirty;
    uint64_t settled;                /* from here a move is the player's */
    RECT at;                         /* where it settled, then where it was last */
    uint32_t shown[FRAME_MAX];       /* what is in it */
    int shown_w, shown_h;
    Present how;
} board;                             /* --cabinet's scoreboard window */

static void open_file(const char *path)
{
    char cmd[600];
    snprintf(cmd, sizeof cmd, "xdg-open \"%s\" >/dev/null 2>&1 &", path);
    if (system(cmd)) {}
}

/* The settings tool (config/, docs/config.md), beside this program; without
 * it, controller.ini in an editor. What it saves the game reads when its
 * window next comes to the front (and the modes at its next start). */
static void settings_tool(void)
{
    char me[512], cmd[1200];
    const ssize_t n = readlink("/proc/self/exe", me, sizeof me - 1);
    char *slash = n > 0 ? (me[n] = 0, strrchr(me, '/')) : NULL;
    if (slash) {
        snprintf(slash + 1, sizeof me - (slash + 1 - me), "epicpinball-config");
        if (access(me, X_OK) == 0) {
            snprintf(cmd, sizeof cmd, "\"%s\" --save \"%s\" &", me, g_opt.savedir);
            if (system(cmd)) {}
            return;
        }
    }
    fprintf(stderr, "[settings] no epicpinball-config beside the game (cmake -DEPIC_CONFIG=ON): controller.ini instead\n");
    open_file(pad_ini());
}

/* The window's place, kept for the ini while it is not fullscreen or maximised */
static void remember_place(void)
{
    if (g_opt.cabinet || is_full || (SDL_GetWindowFlags(wnd) & SDL_WINDOW_MAXIMIZED)) return;
    int x, y, w, h;
    SDL_GetWindowPosition(wnd, &x, &y);
    SDL_GetWindowSize(wnd, &w, &h);
    win_rect = (RECT){ x, y, x + w, y + h };
}

static void set_fullscreen(int on)
{
    if (on == is_full) return;
    remember_place();
    SDL_SetWindowFullscreen(wnd, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    is_full = on;
}

static void board_aside(int aside);

/* SDL has no popup menus: the settings and keys, with buttons for what no key does */
static void menu(void)
{
    char msg[360], view[120], full[40] = "";
    if (g_opt.cabinet)
        snprintf(view, sizeof view, "Playfield turned: %d degrees (F6)\nPlayfield stretched: %s (F5)", S.rot * 90,
                 S.stretch ? "on" : "off");
    else {
        snprintf(view, sizeof view, "Whole table: %s (F7)", S.whole ? "on" : "off");
        snprintf(full, sizeof full, "Fullscreen: F11, Alt+Enter\n");
    }
    snprintf(msg, sizeof msg,
             "%s\nColour: %s (F9)\nScaling: %s (F12)\n%s"
             "Beside the picture: %s\nSound: %s\nScreenshot: PrtSc",
             view, S.look ? "vivid" : "natural", mode_names[S.mode], full,
             S.bars ? "blur" : "black", g_audio_smooth ? "smooth" : "original");
    const SDL_MessageBoxButtonData b[] = {
        { 0, S.bars ? ID_BARS_BLACK : ID_BARS_BLUR, S.bars ? "Black bars" : "Blurred bars" },
        { 0, g_audio_smooth ? ID_SOUND_ORIG : ID_SOUND_SMOOTH, g_audio_smooth ? "Original sound" : "Smooth sound" },
        { 0, ID_SETTINGS, "Settings..." },
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Close" },
    };
    SDL_Window *owner = g_opt.cabinet && SDL_GetKeyboardFocus() ? SDL_GetKeyboardFocus() : wnd;
    const SDL_MessageBoxData d = { SDL_MESSAGEBOX_INFORMATION, owner, "Epic Pinball settings", msg, 4, b, NULL };
    int id = 0;
    uint64_t t = host_us();
    int cursor = SDL_ShowCursor(SDL_QUERY);
    SDL_ShowCursor(SDL_ENABLE);
    board_aside(1);                                /* the dialog would open under the scoreboard */
    feedback_all_off();                            /* and the game stands still under it */
    plunger_let_go();
    watch_released();
    SDL_ShowMessageBox(&d, &id);
    board_aside(0);
    SDL_ShowCursor(cursor);
    machine_paused(host_us() - t);                 /* the game stood still meanwhile */
    command(id);
}

/* A key: the remaster's own (F-keys, PrtSc), else the game's, by keys.c's
 * table and controller.ini's [keys] (pad.c) */
static void key(const SDL_KeyboardEvent *k)
{
    int up = k->type == SDL_KEYUP, s = k->keysym.scancode;
    if (s == SDL_SCANCODE_RETURN && (k->keysym.mod & KMOD_ALT)) { if (!up && !k->repeat) command(ID_FULL); return; }
    if (s == SDL_SCANCODE_PRINTSCREEN) { if (up) screenshot_key(); return; }
    if (g_opt.cabinet && (s == SDL_SCANCODE_F5 || s == SDL_SCANCODE_F6)) {    /* press and release: any key ends the demo */
        if (!up && !k->repeat) command(s == SDL_SCANCODE_F5 ? ID_STRETCH : ID_TURN);
        return;
    }
    if (s == SDL_SCANCODE_F7 || (s >= SDL_SCANCODE_F9 && s <= SDL_SCANCODE_F12)) {
        if (up || k->repeat) return;
        if (s == SDL_SCANCODE_F7) command(ID_WHOLE);
        if (s == SDL_SCANCODE_F9) command(ID_LOOK + !S.look);
        if (s == SDL_SCANCODE_F10) menu();
        if (s == SDL_SCANCODE_F11) command(ID_FULL);
        if (s == SDL_SCANCODE_F12) command(ID_MODE + (S.mode + 1) % PRESENT_MODES);
        return;
    }
    if (k->repeat) return;                          /* the game does its own repeat */
    if (!up && s == SDL_SCANCODE_ESCAPE && driven() == 1) { quit_req = 1; return; }
    int sc = key_from_sdl(s);                      /* counted even while driven (pad.c): no key stuck */
    if (sc) pad_key(sc, up);                       /* [keys] */
}

/* ---- --cabinet's windows */

/* Drag the scoreboard by its middle, resize it by its edges: it has no frame */
static SDL_HitTestResult board_hit(SDL_Window *w, const SDL_Point *p, void *data)
{
    int ww, wh;
    (void)data;
    if (board.fills) return SDL_HITTEST_NORMAL;
    SDL_GetWindowSize(w, &ww, &wh);
    int ex = ww / 4 < 10 ? ww / 4 : 10, ey = wh / 4 < 10 ? wh / 4 : 10;
    int l = p->x < ex, r = p->x >= ww - ex, t = p->y < ey, b = p->y >= wh - ey;
    if (t) return l ? SDL_HITTEST_RESIZE_TOPLEFT : r ? SDL_HITTEST_RESIZE_TOPRIGHT : SDL_HITTEST_RESIZE_TOP;
    if (b) return l ? SDL_HITTEST_RESIZE_BOTTOMLEFT : r ? SDL_HITTEST_RESIZE_BOTTOMRIGHT : SDL_HITTEST_RESIZE_BOTTOM;
    return l ? SDL_HITTEST_RESIZE_LEFT : r ? SDL_HITTEST_RESIZE_RIGHT : SDL_HITTEST_DRAGGABLE;
}

static RECT board_place(void)
{
    int x, y, w, h;
    SDL_GetWindowSize(board.wnd, &w, &h);
    SDL_GetWindowPosition(board.wnd, &x, &y);
    return (RECT){ x, y, x + w, y + h };
}

/* Saved only when it has moved since it opened: --score-window alone is for
 * the one run, as the other options are */
static void board_moved(void)
{
    if (!board.x11) return;                        /* it has no place to speak of */
    RECT r = board_place();
    if (host_us() >= board.settled && memcmp(&r, &board.at, sizeof r)) *(g_opt.dmd ? &saved_dmdw : &saved_score) = r;
    board.at = r;
}

/* Off the top while a dialog is up, then back */
static void board_aside(int aside)
{
    if (!board.wnd) return;
#if SDL_VERSION_ATLEAST(2, 0, 16)
    SDL_SetWindowAlwaysOnTop(board.wnd, aside ? SDL_FALSE : SDL_TRUE);
#else
    if (aside) SDL_HideWindow(board.wnd);
    else { SDL_ShowWindow(board.wnd); SDL_RaiseWindow(wnd); }
#endif
    SDL_PumpEvents();                              /* sent before the dialog blocks */
    board.dirty = 1;
}

static int on_a_display(const RECT *r)
{
    const SDL_Rect w = { r->left, r->top, r->right - r->left, r->bottom - r->top };
    for (int i = 0; i < SDL_GetNumVideoDisplays(); i++) {
        SDL_Rect b, both;
        if (SDL_GetDisplayBounds(i, &b) == 0 && SDL_IntersectRect(&b, &w, &both) && both.w >= 16 && both.h >= 8)
            return 1;
    }
    return 0;
}

/* Before SDL_Init. 1 if it chose the video driver. */
static int cabinet_hints(void)
{
    int chose = 0;
    /* X11 (XWayland) first: Wayland cannot place a window. The player's own
     * choice of driver stands. */
    if (!getenv("SDL_VIDEODRIVER") && !getenv("SDL_VIDEO_DRIVER"))
        chose = setenv("SDL_VIDEODRIVER", "x11,wayland", 1) == 0;
    SDL_SetHint("SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR", "0");         /* it draws title bars */
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");          /* a first right-click counts */
    return chose;
}

/* With a real DMD (--dmd-device) the scoreboard is on it: no window for it,
 * unless --dmd asks for the DMD in a window too */
static int board_wanted(void) { return !g_opt.dmd_device || g_opt.dmd; }

static void cabinet_open(void)
{
    uint64_t t = host_us();
    int n = SDL_GetNumVideoDisplays();
    if (n < 1) fatal("SDL displays: %s", SDL_GetError());
    for (int i = 0; i < n; i++) {
        SDL_Rect b = { 0 };
        const char *name = SDL_GetDisplayName(i);
        SDL_GetDisplayBounds(i, &b);
        fprintf(stderr, "[cabinet] display %d: %s %dx%d at %d,%d\n", i, name ? name : "?", b.w, b.h, b.x, b.y);
    }
    int pd = cab_display;
    if (pd < 0 || pd >= n) {
        fprintf(stderr, "[cabinet] there is no display %d: the playfield goes on display 0\n", pd);
        pd = 0;
    }
    const char *driver = SDL_GetCurrentVideoDriver();
    board.x11 = driver && !strcmp(driver, "x11");
    board.fills = !board.x11 && n > 1;
    if (!board.x11)
        fprintf(stderr, "[cabinet] video driver %s: the scoreboard cannot be placed or kept on top (X11 can)%s\n",
                driver ? driver : "?", board.fills ? ": it fills a display of its own" : "");

    RECT r = cab_score;
    int placed = r.right > r.left;
    if (placed && board.x11 && !on_a_display(&r)) {
        fprintf(stderr, "[cabinet] %ld,%ld is on no display: the scoreboard opens in its default place\n",
                (long)r.left, (long)r.top);
        placed = 0;
    }
    int bd = n > 1 ? (pd ? 0 : 1) : pd;              /* a display of its own, if there is one */
    if (!placed) {
        SDL_Rect b = { 0, 0, 960, 87 };
        SDL_GetDisplayBounds(bd, &b);
        int w = b.w < 960 ? b.w : 960, h = g_opt.dmd ? w / 4 : w * 29 / 320;
        r.left = b.x + (b.w - w) / 2;
        r.top = n > 1 ? b.y + (b.h - h) / 2 : b.y;
        r.right = r.left + w; r.bottom = r.top + h;
    }
    board.at = r;

    const uint32_t flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    /* CENTERED_DISPLAY, not UNDEFINED_DISPLAY: sdl2-compat drops the latter's display on Wayland */
    int any = SDL_WINDOWPOS_CENTERED_DISPLAY(bd), there = SDL_WINDOWPOS_CENTERED_DISPLAY(pd);
    if (board_wanted()) {
        board.wnd = SDL_CreateWindow(g_opt.dmd ? "Epic Pinball DMD" : "Epic Pinball scoreboard",
                                     board.x11 ? (int)r.left : any, board.x11 ? (int)r.top : any,
                                     r.right - r.left, r.bottom - r.top, flags | SDL_WINDOW_ALWAYS_ON_TOP);
        if (!board.wnd) fatal("SDL scoreboard window: %s", SDL_GetError());
    } else fprintf(stderr, "[cabinet] the scoreboard is on the DMD: no window for it\n");
    wnd = SDL_CreateWindow("Epic Pinball", there, there, 640, 480, flags);
    if (!wnd) fatal("SDL window: %s", SDL_GetError());
    if (board.wnd) {
        SDL_SetWindowMinimumSize(board.wnd, 32, 8);
        if (SDL_SetWindowHitTest(board.wnd, board_hit, NULL))
            fprintf(stderr, "[cabinet] the scoreboard cannot be dragged here: %s\n", SDL_GetError());
    }
    if (!present_prepare(wnd) || (board.wnd && !present_prepare(board.wnd))) fatal("--cabinet needs two SDL renderers");
    if (SDL_SetWindowFullscreen(wnd, SDL_WINDOW_FULLSCREEN_DESKTOP))
        fprintf(stderr, "[cabinet] fullscreen: %s\n", SDL_GetError());
    if (board.wnd && board.fills && SDL_SetWindowFullscreen(board.wnd, SDL_WINDOW_FULLSCREEN_DESKTOP))
        fprintf(stderr, "[cabinet] scoreboard fullscreen: %s\n", SDL_GetError());

    /* the scoreboard first, the playfield last, which gets the keys. Wayland
     * maps a window at its first picture, so they get one here. */
    compose_view();
    board.how = S; board.how.rot = board.how.bars = board.how.stretch = 0;
    if (board.wnd) {
        SDL_ShowWindow(board.wnd);
        if (board.x11) SDL_SetWindowPosition(board.wnd, r.left, r.top);
        present_window(board.wnd, score, sw, sh, (double)sw / sh, &board.how);
    }
    SDL_ShowWindow(wnd);
    { const Present v = playfield_how(); present_window(wnd, frame, fw, fh, frame_aspect, &v); }
    SDL_RaiseWindow(wnd);
    SDL_ShowCursor(SDL_DISABLE);
    board.dirty = 1;
    board.settled = host_us() + 1000000;             /* the window manager has its say first */
    machine_paused(host_us() - t);
}

static void cabinet_event(const SDL_WindowEvent *e)
{
    int b = board.wnd && e->windowID == SDL_GetWindowID(board.wnd);
    if (e->event == SDL_WINDOWEVENT_CLOSE) quit_req = 1;   /* SDL_QUIT waits for the last window */
    if (e->event == SDL_WINDOWEVENT_ENTER) SDL_ShowCursor(b ? SDL_ENABLE : SDL_DISABLE);
    if (!b) return;
    if (e->event == SDL_WINDOWEVENT_MOVED || e->event == SDL_WINDOWEVENT_SIZE_CHANGED) board_moved();
    if (e->event == SDL_WINDOWEVENT_EXPOSED || e->event == SDL_WINDOWEVENT_SIZE_CHANGED
        || e->event == SDL_WINDOWEVENT_SHOWN || e->event == SDL_WINDOWEVENT_RESTORED)
        board.dirty = 1;
}

/* Both windows; the scoreboard only when there is something new in it */
static void cabinet_present(void)
{
    uint64_t t0 = host_us();
    { const Present v = playfield_how(); present_window(wnd, frame, fw, fh, frame_aspect, &v); }
    uint64_t t1 = host_us(), t2 = t1;
    Present how = S;
    how.rot = how.bars = how.stretch = 0;            /* upright, black round it */
    size_t bytes = (size_t)sw * sh * 4;
    if (board.wnd && (board.dirty || board.shown_w != sw || board.shown_h != sh
        || memcmp(&how, &board.how, sizeof how) || memcmp(board.shown, score, bytes))) {
        present_window(board.wnd, score, sw, sh, (double)sw / sh, &how);
        memcpy(board.shown, score, bytes);
        board.shown_w = sw; board.shown_h = sh; board.how = how; board.dirty = 0;
        t2 = host_us();
    }
    if (t2 - t0 > 1000)
        trace("[present] slow at %llu ms: playfield %u us, scoreboard %u us\n",
              (unsigned long long)(emu_us() / 1000), (unsigned)(t1 - t0), (unsigned)(t2 - t1));
}

static void desktop_open(void)
{
    int x = win_rect.left, y = win_rect.top, w = win_rect.right - win_rect.left, h = win_rect.bottom - win_rect.top;
    if (g_opt.window || w <= 0) {                  /* --window N, or no place remembered */
        int s = g_opt.window ? g_opt.window : 3;
        x = y = SDL_WINDOWPOS_UNDEFINED; w = 320 * s; h = 240 * s;
    }
    wnd = SDL_CreateWindow("Epic Pinball", x, y, w, h, SDL_WINDOW_RESIZABLE);
    if (!wnd) fatal("SDL window: %s", SDL_GetError());
    remember_place();
    if (g_opt.dmd) {                               /* --dmd: a window of its own, beside */
        board.wnd = SDL_CreateWindow("Epic Pinball DMD", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                     DMD_W * 6, DMD_H * 6, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!board.wnd) fatal("SDL DMD window: %s", SDL_GetError());
        if (!present_prepare(wnd) || !present_prepare(board.wnd)) fatal("--dmd needs two SDL renderers");
        board.how = S; board.how.rot = board.how.bars = board.how.stretch = 0;
        board.dirty = 1;
        SDL_RaiseWindow(wnd);
    }
    if (g_opt.fullscreen) set_fullscreen(1);
}

static void ssf_report_device(int opened);

static void window_open(void)
{
    const uint32_t systems = SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER;
    int chose = g_opt.cabinet && cabinet_hints();
    int failed = SDL_Init(systems) != 0;
    if (failed && chose) {                         /* an older SDL2 reads the list as one name */
        unsetenv("SDL_VIDEODRIVER");               /* and tries X11 first by itself */
        failed = SDL_Init(systems) != 0;
    }
    if (failed) fatal("SDL: %s", SDL_GetError());
    if (g_opt.cabinet) cabinet_open();
    else desktop_open();
    if (!g_opt.mute && SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec want = { 0 };
        want.freq = AUDIO_RATE; want.format = AUDIO_S16SYS; want.channels = (Uint8)out_channels(); want.samples = 1024;
        aud = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
        if (aud) SDL_PauseAudioDevice(aud, 0);
        if (g_opt.ssf) ssf_report_device(aud != 0);
    }
}

/* --ssf: what the default output takes, as SDL sees it: with fewer than
 * eight channels it folds the exciters into the front */
static void ssf_report_device(int opened)
{
    if (!opened) { fprintf(stderr, "[ssf] no sound device: %s\n", SDL_GetError()); return; }
#if SDL_VERSION_ATLEAST(2, 24, 0)
    char *name = NULL;
    SDL_AudioSpec have;
    if (SDL_GetDefaultAudioInfo(&name, &have, 0) == 0) {
        fprintf(stderr, "[ssf] sound out: 7.1 to %s, which has %d channels%s\n", name ? name : "the default device",
                have.channels, have.channels < 8 ? ": the exciters are folded into it. Set its profile to 7.1." : "");
        SDL_free(name);
        return;
    }
#endif
    fprintf(stderr, "[ssf] sound out: 7.1 to the default device\n");
}

static int window_front(void)
{
    return g_opt.cabinet ? SDL_GetKeyboardFocus() != NULL : (SDL_GetWindowFlags(wnd) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

/* Events and the pad */
static void window_events(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) quit_req = 1;
        else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) key(&e.key);
        else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_RIGHT) menu();
        else if (e.type == SDL_WINDOWEVENT) {
            if (g_opt.cabinet) cabinet_event(&e.window);
            else if (board.wnd && e.window.windowID == SDL_GetWindowID(board.wnd)) {
                if (e.window.event == SDL_WINDOWEVENT_CLOSE) SDL_HideWindow(board.wnd);   /* the game goes on */
                else board.dirty = 1;
                continue;
            }
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) pad_load();   /* controller.ini may have been edited */
            if (e.window.event == SDL_WINDOWEVENT_MOVED || e.window.event == SDL_WINDOWEVENT_RESIZED) remember_place();
        }
    }
    pad_poll(window_front());                      /* while driven too: pad.c counts, sends nothing */
    if (quit_req && driven() != 2) {                /* leaving: PINBALL is saving */
        if (g_auto_on && autostart_quit()) quit_req = 0;   /* between games: the initials in, then out */
        else dos_exit(0);
    }
}

/* --table: the game is on; what was mixed while driving is not heard */
static void window_live(void) { if (aud) SDL_ClearQueuedAudio(aud); }

/* Events, the pad, and the picture */
static void window_frame(void)
{
    window_events();
    compose_view();
    compose_hud();
    if (g_opt.cabinet) cabinet_present();
    else {
        const Present v = playfield_how();
        present_window(wnd, frame, fw, fh, frame_aspect, &v);
        if (board.wnd && (dmd_new || board.dirty)) {
            present_window(board.wnd, dmd_big, DMD_W * DMD_S, DMD_H * DMD_S, 4.0, &board.how);
            dmd_new = board.dirty = 0;
        }
    }
}

/* A queue the main thread fills, as long as waveOut's ring: drop rather than block */
static void window_audio(const int16_t *s, int frames)
{
    const Uint32 frame = (Uint32)out_channels() * 2;
    if (aud && SDL_GetQueuedAudioSize(aud) < 8 * 1024 * frame) SDL_QueueAudio(aud, s, (Uint32)frames * frame);
}

static void window_close(void)
{
    if (aud) SDL_CloseAudioDevice(aud);
    if (wnd) {
        settings_save();
        present_close();
        SDL_DestroyWindow(wnd);
        if (board.wnd) SDL_DestroyWindow(board.wnd);
    }
    SDL_Quit();
}
#endif

void host_audio(const int16_t *s, int frames)
{
    if (rec_audio) { fwrite(s, 4, frames, rec_audio); rec_samples += frames; }
    if (wav_out) { fwrite(s, 4, frames, wav_out); wav_samples += frames; }
    if (wnd && heard()) window_audio(s, frames);
}

/* --ssf: the front pair and the four exciters as 7.1, FL FR FC LFE BL BR SL
 * SR; a recording keeps the front */
void host_audio_ssf(const int16_t *front, const int16_t *exciters, int frames)
{
    static int16_t out[2048 * 8];
    for (int done = 0; done < frames; ) {
        const int n = frames - done > 2048 ? 2048 : frames - done;
        for (int i = 0; i < n; i++) {
            int16_t *o = out + i * 8;
            const int16_t *f = front + (done + i) * 2, *e = exciters + (done + i) * 4;
            o[0] = f[0]; o[1] = f[1]; o[2] = o[3] = 0;
            o[4] = e[0]; o[5] = e[1]; o[6] = e[2]; o[7] = e[3];
        }
        if (rec_audio) { fwrite(front + done * 2, 4, n, rec_audio); rec_samples += n; }
        if (wav_out) { fwrite(out, 16, n, wav_out); wav_samples += n; }
        if (wnd && heard()) window_audio(out, n);
        done += n;
    }
}

/* ---- the analog plunger (plunger.c) ---------------------------------------- */

static Plunger plunger;
static int plunger_used;             /* only then is the table ever written */

static const WatchMap *plunger_table(uint8_t **ds, uint8_t **cs)
{
    uint32_t d, c;
    const WatchMap *m = watch_table(&d, &c);
    if (m) { *ds = g_cpu.mem + d; *cs = g_cpu.mem + c; }
    return m;
}

static void plunger_let_go(void)
{
    uint8_t *ds = NULL, *cs = NULL;
    if (!plunger_used) return;
    const WatchMap *m = plunger_table(&ds, &cs);
    plunger_cancel(&plunger, m, ds, cs);
}

static void plunger_poll(void)
{
    PlungerInput in;
    const double ms = pit_now() * 1000.0 / PIT_HZ;
    if (plunger_traced()) plunger_trace_at(ms, &in);
    else if (wnd && pad_plunger_on()) pad_plunger(window_front(), &in);
    else {                           /* switched off in the ini while the game runs */
        plunger_let_go();
        plunger_used = 0;
        return;
    }
    if (!plunger_used) {
        plunger_init(&plunger);
        watch_wanted();
        plunger_used = 1;
    }
    uint8_t *ds = NULL, *cs = NULL;
    const WatchMap *m = plunger_table(&ds, &cs);
    plunger_step(&plunger, m, ds, cs, watch_running(), ms, &in);
}

/* The accelerometer (or its trace) for the next physics step, and nudge
 * buttons as the game's keys (pad.c, nudge.c) */
static void nudge_poll(void)
{
    static int wanted;
    if (nudge_traced()) {
        int x, y;
        nudge_trace_at(pit_now() * 1000.0 / PIT_HZ, &x, &y);
        nudge_reading(1, x, y);
        return;
    }
    if (!wnd || !pad_nudge_on()) { nudge_reading(0, 0, 0); return; }
    if (!wanted++) watch_wanted();                 /* the push needs the table's map */
    pad_nudge(window_front() && !driven());
}

/* Messages leave the playfield only for somewhere they show: the DMD's
 * window while it is open (headless, the shots), or a helper taking frames */
static int dmd_showing(void)
{
    if (dmd_pipe_alive()) return 1;
    if (!g_opt.dmd) return 0;
    if (!wnd || g_opt.cabinet) return 1;
#ifdef _WIN32
    return board.wnd && IsWindowVisible(board.wnd);
#else
    return board.wnd && (SDL_GetWindowFlags(board.wnd) & SDL_WINDOW_SHOWN);
#endif
}

/* A cabinet's game is over (--table): the playfield keeps its last picture,
 * dimmed as a table dims its own, until the next game is shown */
static void hold(int on)
{
    if (on) {
        compose_view();
        for (int i = 0; i < fw * fh; i++) {
            const uint32_t p = frame[i];
            frame[i] = 0xFF000000u | ((p >> 16 & 0xFF) * 2 / 5) << 16 | ((p >> 8 & 0xFF) * 2 / 5) << 8 | (p & 0xFF) * 2 / 5;
        }
    }
    holding = on;
}

void host_init(void)
{
    keys_parse();
    if (g_opt.record) rec_open();
    if (g_opt.wav && (wav_out = fopen(g_opt.wav, "wb")) != NULL) wav_header(wav_out, 0, out_channels());
    if (g_opt.headless) { settings_args(); return; }
    settings_load();
    settings_args();
    pad_load();
    window_open();
}

void host_frame(void)
{
    static uint64_t last_present;
    static int was_driven, live;
    if (g_auto_on) {
        autostart_poll();
        const int d = driven();
        if (was_driven == 1 && !d) {               /* a game of the table is on */
            if (!live++)                           /* the first: --keys count from it */
                for (int i = kev_i; i < nkev; i++) kev[i].at += pit_now();
            if (wnd) window_live();
        }
        was_driven = d;
    }
    if (g_auto_on && autostart_hold() != holding) hold(!holding);
    keys_due();
    autoplay();
    dof_tick();                      /* the helper's starts and ends */
    dmd_pipe_tick();
    watch_tick();                    /* first: a table it has lost is not written */
    plunger_poll();
    nudge_poll();
    uint64_t now = pit_now();
    shots_due(now);
    if (g_opt.seconds > 0 && now >= (uint64_t)(g_opt.seconds * PIT_HZ)) dos_exit(0);
    if (rec_video) {
        uint64_t per = (uint64_t)(PIT_HZ / REC_FPS);
        if ((rec_frames + 1) * per <= now) {
            compose();
            const uint32_t *f = as_rec();
            while ((rec_frames + 1) * per <= now) { fwrite(f, 4, (size_t)rec_w * rec_h, rec_video); rec_frames++; }
        }
    }
    if (machine_is_fast()) {                       /* --table: nothing shown, events now and then */
        static uint64_t last_pump;
        if (wnd && host_us() - last_pump >= 15000) { last_pump = host_us(); window_events(); }
        return;
    }
    if (now - last_present < (uint64_t)(PIT_HZ / 70)) return;
    if (!wnd) {
        if (g_opt.dmd_device) { last_present = now; compose_dmd(); }   /* no window: the DMD all the same */
        return;
    }
    last_present = now;
    window_frame();
}

static void write_bmp(const char *path, const uint32_t *px, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t sz = 54 + w * h * 4, off = 54, hs = 40, zero = 0, img = w * h * 4;
    int32_t bw = w, bh = -h; uint16_t planes = 1, bpp = 32;
    fwrite("BM", 1, 2, f); fwrite(&sz, 4, 1, f); fwrite(&zero, 4, 1, f); fwrite(&off, 4, 1, f);
    fwrite(&hs, 4, 1, f); fwrite(&bw, 4, 1, f); fwrite(&bh, 4, 1, f); fwrite(&planes, 2, 1, f);
    fwrite(&bpp, 2, 1, f); fwrite(&zero, 4, 1, f); fwrite(&img, 4, 1, f);
    fwrite(&zero, 4, 1, f); fwrite(&zero, 4, 1, f); fwrite(&zero, 4, 1, f); fwrite(&zero, 4, 1, f);
    fwrite(px, 4, (size_t)w * h, f);
    fclose(f);
}

static void write_view(const char *path, const uint32_t *f, int w, int h, double aspect, const Present *s)
{
    if (g_opt.present_w > 0) {
        size_t n = (size_t)g_opt.present_w * g_opt.present_h;
        uint32_t *out = malloc(n * 4);
        if (out && present_offscreen(f, w, h, aspect, s, g_opt.present_w, g_opt.present_h, out))
            write_bmp(path, out, g_opt.present_w, g_opt.present_h);
        else
            fprintf(stderr, "[present] %s: no presenter, not written\n", path);
        free(out);
        return;
    }
    write_bmp(path, f, w, h);
}

/* The picture as a BMP: the game's own frame, or with --present the
 * presenter's output at that size -- a run nobody watched still says where
 * it got to. In a cabinet, the playfield (upright unless presented) and
 * beside it NAME-score.bmp, the scoreboard. */
void host_snapshot(const char *path)
{
    compose_view();
    const Present v = playfield_how();
    write_view(path, frame, fw, fh, frame_aspect, &v);
    size_t n = strlen(path);
    if (n >= 4 && !_stricmp(path + n - 4, ".bmp")) n -= 4;
    char second[600];
    if (dmd_on()) {                                  /* NAME-dmd.bmp: the 128x32 picture itself */
        snprintf(second, sizeof second, "%.*s-dmd.bmp", (int)n, path);
        write_bmp(second, dmd_px, DMD_W, DMD_H);
    }
    if (!g_opt.cabinet || g_opt.dmd || g_opt.dmd_device) return;   /* the DMD took the scoreboard's place */
    snprintf(second, sizeof second, "%.*s-score.bmp", (int)n, path);
    Present how = S;
    how.rot = how.bars = how.stretch = 0;
    write_view(second, score, sw, sh, (double)sw / sh, &how);
}

void host_shutdown(void)
{
    feedback_close();
    dof_close();
    dmd_pipe_close();
    rec_close();
    if (wav_out) { wav_header(wav_out, (uint32_t)wav_samples, out_channels()); fclose(wav_out); wav_out = NULL; }
    window_close();
}
