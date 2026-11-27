/*
 * selftest.c - the runtime's own pieces, checked without any game data, so CI
 * has something real to run (the conformance harness needs your copy of the
 * game and skips without it).
 *
 * One check per behaviour the game is known to depend on; each comment says
 * which. Built by CMake as `selftest`, with no lifted code at all.
 */
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE               /* mkdtemp, usleep */
#endif
#undef NDEBUG                     /* the asserts ARE the test, in every build */
#include "machine.h"
#include "feedback.h"
#include "plunger.h"
#include "dof_pipe.h"
#include "dmd.h"
#include "ssf.h"
#include "autostart.h"
#include "arcade.h"
#include "guard.h"
#include "nudge.h"
#include "ini.h"
#include "keys.h"
#include "hud.h"
#include "vpx_import.h"
#include <assert.h>
#ifndef _WIN32
#include <time.h>
#include <unistd.h>
#endif

CPU g_cpu;
Options g_opt;
static int real_clock;            /* the pipe test's time is the helper's */
uint64_t host_us(void)
{
#ifndef _WIN32
    if (real_clock) {
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
    }
#endif
    return 0;
}
void trace(const char *fmt, ...) { (void)fmt; }
void fatal(const char *fmt, ...) { (void)fmt; abort(); }
static int16_t heard[4096 * 2];
static int nheard;
void host_audio(const int16_t *s, int frames)
{
    for (int i = 0; i < frames && nheard < 4096; i++, nheard++) { heard[nheard * 2] = s[i * 2]; heard[nheard * 2 + 1] = s[i * 2 + 1]; }
}
void host_audio_ssf(const int16_t *front, const int16_t *exciters, int frames) { (void)front; (void)exciters; (void)frames; }
static uint8_t keys_sent[64];          /* what --table pressed (src/autostart.c) */
static int nkeys_sent, fast_now;
void kbd_scancode(uint8_t sc) { if (nkeys_sent < 64) keys_sent[nkeys_sent++] = sc; }
void machine_fast(int on) { fast_now = on; }
int machine_is_fast(void) { return fast_now; }
void host_frame(void) {}
void host_backtrace(void) {}
static uint64_t pit_at;                                   /* moved by the feedback tests */
uint64_t pit_now(void) { return pit_at; }
static uint64_t now_us;
uint64_t emu_us(void) { return now_us; }
void machine_port_wait(unsigned us) { (void)us; }
void recomp_tick(CPU *c) { (void)c; }
int machine_port_in(uint16_t p, uint8_t *v) { (void)p; (void)v; return 0; }
void call_vector(CPU *c, unsigned v) { (void)c; (void)v; }
int machine_busy(void) { return 0; }
void machine_unwound(void) {}

/* the image list tools/lift.py would generate: one fake image, two functions */
static int ran;
static void f10(CPU *c) { (void)c; ran = 0x10; }
static void f2345(CPU *c) { (void)c; ran = 0x2345; }
static const RecompFunc fake_funcs[] = { {0x10, f10}, {0x2345, f2345} };
static const LiftedImage fake = { "ep1", "EP1.EXE", 0x4000, 16, 0x69691905u /* FNV-1a of 16 zeros */, 2, fake_funcs };
const LiftedImage *const g_lifted[] = { &fake, NULL };

static void out(uint16_t port, uint8_t v) { assert(vga_port_out(port, v) || audio_port_out(port, v)); }

static void test_split_screen(void)
{
    /* the tables' scoreboard: CRTC line compare restarts scanout at address 0 */
    static uint32_t frame[640 * 480]; int w, h;
    vga_init();
    vga_set_mode(0x13);
    out(0x3C4, 4); out(0x3C5, 0x06);                     /* Mode X */
    out(0x3D4, 0x12); out(0x3D5, 0xDF);                  /* 480 lines ... */
    out(0x3D4, 0x07); out(0x3D5, 0x3E);                  /* ... with the overflow bits */
    out(0x3D4, 0x09); out(0x3D5, 0x01);                  /* double scan */
    out(0x3D4, 0x18); out(0x3D5, 0xB9);                  /* line compare 1B9h: scanline 441 */
    out(0x3D4, 0x0C); out(0x3D5, 0x06);                  /* start 0640h: playfield below the DMD */
    out(0x3D4, 0x0D); out(0x3D5, 0x40);
    out(0x3C4, 2); out(0x3C5, 0x01);                     /* plane 0: pixel 0 of each group */
    mem_write8(&g_cpu, 0xA000, 0x0000, 3);               /* the scoreboard's first pixel */
    mem_write8(&g_cpu, 0xA000, 0x0640, 5);               /* the playfield's first pixel */
    vga_dac_set(3, 63, 0, 0); vga_dac_set(5, 0, 63, 0);
    vga_compose(frame, &w, &h);
    assert(w == 320 && h == 240);
    assert(frame[0] == 0xFF00FF00u);                     /* row 0: the start address */
    assert(frame[221 * 320] == 0xFFFF0000u);             /* row 221: address 0 */
}

static void test_whole_table(void)
{
    /* the remaster's whole table: the front page from its first row (1600h
     * past the 19 display rows the split shows, as tables 1-8 keep 20), then
     * the display under it -- not only the 221 rows the CRTC scans out */
    static uint32_t frame[320 * 480]; int w, h;
    out(0x3D4, 0x0C); out(0x3D5, 0x84);                  /* start 8480h: page 2 (8340h), 40 rows down */
    out(0x3D4, 0x0D); out(0x3D5, 0x80);
    out(0x3C4, 2); out(0x3C5, 0x01);
    mem_write8(&g_cpu, 0xA000, 0x8340, 7);               /* page 2's first pixel; a row early is black */
    vga_dac_set(7, 0, 0, 63);
    assert(vga_compose_table(frame, &w, &h));
    assert(w == 320 && h == 399 + 19);                   /* page ends at 64K; 19 display rows */
    assert(frame[0] == 0xFF0000FFu);                     /* row 0: the page's first row */
    assert(frame[399 * 320] == 0xFFFF0000u);             /* then address 0: the display */
    vga_set_mode(0x13);                                  /* not a table: nothing */
    assert(!vga_compose_table(frame, &w, &h));
}

static void start_at(unsigned a)
{
    out(0x3D4, 0x0C); out(0x3D5, (uint8_t)(a >> 8));
    out(0x3D4, 0x0D); out(0x3D5, (uint8_t)a);
}

static void test_table_layouts(void)
{
    /* tables 1-8 and 9-13 lay their two pages out differently, and Enter
     * slides either scoreboard away two scanlines a frame: both pages must
     * put their first row on top, at either end of the scroll (and the fifth
     * row: 400 apart would call 9-13's first five page 1) and every height */
    static const struct { unsigned lc, page0, page1; int rows; } t[] = {
        { 0x1B9, 1600, 33600, 399 },                     /* tables 1-8 */
        { 0x1A5, 2400, 34000, 394 },                     /* tables 9-13 */
    };
    static uint32_t frame[320 * 480]; int w, h;
    for (int i = 0; i < 2; i++) {
        vga_set_mode(0x13);
        out(0x3C4, 4); out(0x3C5, 0x06);
        out(0x3D4, 0x12); out(0x3D5, 0xDF);
        out(0x3D4, 0x07); out(0x3D5, 0x3E);
        out(0x3D4, 0x09); out(0x3D5, 0x01);
        out(0x3C4, 2); out(0x3C5, 0x01);
        mem_write8(&g_cpu, 0xA000, (uint16_t)t[i].page0, 5);
        mem_write8(&g_cpu, 0xA000, (uint16_t)t[i].page1, 7);
        vga_dac_set(5, 0, 63, 0); vga_dac_set(7, 0, 0, 63);
        for (unsigned lc = t[i].lc; lc < 479; lc += 2) { /* shown, then sliding away */
            int split = (int)(lc + 1) / 2;
            int ys[] = { 0, 4, t[i].rows - split };
            out(0x3D4, 0x18); out(0x3D5, (uint8_t)lc);
            for (int k = 0; k < 3; k++)
                for (int p = 0; p < 2; p++) {
                    start_at((p ? t[i].page1 : t[i].page0) + ys[k] * 80);
                    assert(vga_compose_table(frame, &w, &h));
                    assert(h == t[i].rows + 240 - split);
                    assert(frame[0] == (p ? 0xFF0000FFu : 0xFF00FF00u));
                }
        }
    }
    vga_set_mode(0x13);
}

static void test_cabinet_views(void)
{
    /* --cabinet's two screens: the front page without a row of the scoreboard,
     * and the scoreboard at its full height, whatever Enter has done with the
     * split -- all the way to gone (480), where tables 9-13 spend most of a
     * ball, and half programmed, as each move of the split leaves it */
    static const struct { unsigned lc, page0, page1; int rows, dmd; } t[] = {
        { 0x1B9, 1600, 33600, 399, 19 },
        { 0x1A5, 2400, 34000, 394, 29 },
    };
    static uint32_t frame[FRAME_PIXELS]; int w, h;
    const uint32_t red = 0xFFFF0000u, green = 0xFF00FF00u, blue = 0xFF0000FFu;
    for (int i = 0; i < 2; i++) {
        vga_set_mode(0x13);
        assert(!vga_compose_score(frame, &w, &h));       /* a menu: no scoreboard */
        assert(!vga_compose_playfield(frame, &w, &h));   /* and shown as it is */
        assert(w == 320 && h == 200);
        out(0x3C4, 4); out(0x3C5, 0x06);
        out(0x3D4, 0x12); out(0x3D5, 0xDF);
        out(0x3D4, 0x07); out(0x3D5, 0x3E);
        out(0x3D4, 0x09); out(0x3D5, 0x01);
        out(0x3C4, 2); out(0x3C5, 0x0F);
        for (int a = 0; a < t[i].dmd * 80; a++) mem_write8(&g_cpu, 0xA000, (uint16_t)a, 3);
        for (int a = 0; a < t[i].rows * 80; a++) {
            mem_write8(&g_cpu, 0xA000, (uint16_t)(t[i].page0 + a), 5);
            mem_write8(&g_cpu, 0xA000, (uint16_t)(t[i].page1 + a), 7);
        }
        vga_dac_set(3, 63, 0, 0); vga_dac_set(5, 0, 63, 0); vga_dac_set(7, 0, 0, 63);
        out(0x3D4, 0x18); out(0x3D5, (uint8_t)t[i].lc);
        vga_compose_playfield(frame, &w, &h);            /* loading, no flip yet: black, */
        for (int k = 0; k < w * h; k++) assert(frame[k] == 0xFF000000u);   /* not the scoreboard */

        start_at(t[i].page0);
        for (unsigned lc = t[i].lc;; lc = lc + 2 < 480 ? lc + 2 : 480) {
            out(0x3D4, 0x18); out(0x3D5, (uint8_t)lc);
            for (int p = 0; p < 2; p++) {
                unsigned page = p ? t[i].page1 : t[i].page0;
                unsigned starts[] = { page, page + 4 * 80, page + 183 * 80, page - 80 };   /* the last: one Up too many */
                for (int k = 0; k < 4; k++) {
                    start_at(starts[k]);
                    for (int half = 0; half < 2; half++) {
                        out(0x3D4, 0x09); out(0x3D5, half ? 0x41 : 0x01);
                        assert(vga_compose_playfield(frame, &w, &h));
                        assert(w == 320 && h == t[i].rows);
                        for (int n = 0; n < w * h; n++) assert(frame[n] == (p ? blue : green));
                        assert(vga_compose_score(frame, &w, &h));
                        assert(w == 320 && h == t[i].dmd);
                        for (int n = 0; n < w * h; n++) assert(frame[n] == red);
                    }
                    out(0x3D4, 0x09); out(0x3D5, 0x01);
                }
            }
            if (lc == 480) break;
        }
    }
    vga_set_mode(0x13);
    assert(!vga_compose_score(frame, &w, &h));           /* back at the menu */
}

static int retrace_at(uint64_t us)
{
    uint8_t v;
    now_us = us;
    assert(vga_port_in(0x3DA, &v));
    return (v & 8) != 0;
}

static void flip_at(uint64_t us)
{
    now_us = us;
    out(0x3D4, 0x0D); out(0x3D5, 0x00);
}

static void test_flip_wait(void)
{
    /* PINBALL's page flip sets the start address, then waits for "in retrace"
     * without first waiting for it to end: one retrace may end only one such
     * wait, or the menu runs as fast as the host. 14286 us a refresh,
     * retrace from 12900. */
    const uint64_t f = 14286;
    now_us = 0;
    vga_start();
    flip_at(1000);
    assert(!retrace_at(1001));                           /* not before the retrace */
    assert(retrace_at(12900));                           /* the retrace ends the wait */
    assert(retrace_at(12901));                           /* no new flip: the plain status */
    flip_at(12950);                                      /* the next frame, done at once */
    assert(!retrace_at(12951));                          /* not in the same retrace */
    assert(!retrace_at(f - 1));
    assert(!retrace_at(f + 12899));
    assert(retrace_at(f + 12900));                       /* but in the next one */
    flip_at(3 * f + 13000);                              /* a late frame, in a retrace no flip used */
    assert(retrace_at(3 * f + 13001));                   /* is not held back a refresh */
    now_us = 0;
}

/* 8 bytes at 12000h played by the DSP at 22050 Hz, auto-init, on DMA channel 1:
 * mode 20h is 8-bit stereo unsigned, 00h mono. 2 ms of it into heard[]. */
static void sb_play(const uint8_t *buf, uint8_t mode)
{
    memcpy(g_cpu.mem + 0x12000, buf, 8);
    out(0x226, 1); out(0x226, 0);
    out(0x22C, 0xD1);                                    /* speaker on */
    out(0x0A, 0x05);                                     /* mask ch1 */
    out(0x0C, 0); out(0x02, 0x00); out(0x02, 0x20);      /* address 2000h */
    out(0x0C, 0); out(0x03, 7); out(0x03, 0);            /* 8 bytes */
    out(0x83, 0x01);                                     /* page 1 */
    out(0x0B, 0x59);                                     /* auto-init, read, ch1 */
    out(0x0A, 0x01);                                     /* unmask */
    out(0x22C, 0x41); out(0x22C, 0x56); out(0x22C, 0x22);
    out(0x22C, 0xC6); out(0x22C, mode); out(0x22C, 7); out(0x22C, 0);
    static uint64_t now;                                 /* audio_advance takes the time, not a step */
    nheard = 0;
    now += (uint64_t)(PIT_HZ / 500);
    audio_advance(now);
}

static void test_smooth_sound(void)
{
    /* 22 kHz into 44.1: smooth puts a sample halfway between each two of the
     * DSP's; original holds each, so only the two levels are ever heard */
    static const uint8_t saw[8] = { 0x80, 0xC0, 0x80, 0xC0, 0x80, 0xC0, 0x80, 0xC0 };
    for (int smooth = 1; smooth >= 0; smooth--) {
        int mid = 0;
        g_audio_smooth = smooth;
        sb_play(saw, 0x00);
        for (int i = 0; i < nheard; i++) mid += heard[i * 2] > 1000 && heard[i * 2] < 5000;
        assert(smooth ? mid > 20 : mid == 0);
    }
    g_audio_smooth = 1;
}

static void test_sb16_stereo(void)
{
    /* MASI on a Sound Blaster 16: DSP 4.05, rate 22050 by 41h, then C6h mode
     * 20h -- 8-bit stereo unsigned auto-init. Every frame must have the left
     * channel high and the right low -- played as mono, the two would
     * alternate in one channel instead. */
    static const uint8_t buf[8] = { 0x80 + 64, 0x80 - 64, 0x80 + 64, 0x80 - 64, 0x80 + 64, 0x80 - 64, 0x80 + 64, 0x80 - 64 };
    audio_init();
    sb_play(buf, 0x20);
    int got = 0;
    for (int i = 0; i < nheard; i++)
        if (heard[i * 2] || heard[i * 2 + 1]) { assert(heard[i * 2] > 0 && heard[i * 2 + 1] < 0); got++; }
    assert(got > 50);
}

static void test_images(void)
{
    /* placed at its lifted segment, live, found by address; a table loaded
     * over it later is a different image (the tables share segments) */
    uint8_t raw[16] = {0};
    images_placed("EP1.EXE", raw, 16, 0x4000, 0x3000);
    void (*fn)(CPU *) = images_lookup(0x4000 + 0x234, 0x5);  /* linear 0x2345 inside */
    assert(fn == f2345);
    fn(&g_cpu); assert(ran == 0x2345);
    assert(images_lookup(0x4001, 0) == f10);
    images_gone(0x4000);
    assert(images_lookup(0x4001, 0) == NULL);
}

/* ---- cabinet feedback and the plunger ---- */

static const char k_map[] =
    "epicpinball-feedback 8\nimage ep1 16 69691905\n"
    "ds 10\ncs 2000\nactive 100\nx 110\ny 120\nvx 130\nvy 140\nslots 5\n"
    "flip_l 150\nflip_r 152\nup_l 154\nup_r 155\ntilt 160\nmeter 161\ncooldown 162\ndemo 163\n"
    "extra 164\nover 165\ncount 166\ndrain_y 18F\nkick 167\nweak_y 180\nsling_line C7\nmap 170\n"
    "pull 180\npull_inc C\npull_max 2C4\npull_shift 5\nctrl 297\nspace 296\n"
    "lane_off 0\nlane_x 118\nlane_y DC\nover_pull 0\nscan_x 190\nscan_y 192\nnudge_gate 16A\ngate_lo 4\ngate_hi 2A\n"
    "step_fn ep1_step\nkicker_fn ep1_kick\nswitch_fn ep1_sw\nguard_fn ep1_guard\nx_min 1\nbumper 100 60 0\nbumper 200 60 2\n"
    "lamp_list_fn ep1_lamps\nlamp_fn ep1_lamp\nlamp_seg 3000\nlamp_table 10\nlamps 3\n"
    "msg_fn ep1_msg\nlayer 240\non_layer F9 F8 F1\ntilt_pass F0 F6\n"
    "zone hole 1 F5 0 0 319 399 L TestHole\nzone hole 2 F6 0 0 219 399 C PowerUpHole\n"
    "zone hole 3 F6 220 0 319 399 R RightHole\nzone kickback 1 F8 0 321 50 399 L Kickback\n"
    "zone rollover 1 FA 0 0 179 399 C TopLane1\nzone rollover 2 FA 180 0 319 399 R TopLane2\n"
    "zone ramp 1 F9 0 0 74 399 L LeftRamp\nzone ramp 2 F6 220 0 319 399 R CentreRamp\n"
    "feature switch 1 C BallLock\nzone switch 2 FB 0 0 319 399 R LeftLoop\n"
    "timer 200\ntimer 201\ntarget 210 1 L DropTarget1\ntarget 211 2 L DropTarget2\ntarget 212 3 L DropTarget3\n"
    "bank DropTargets\nlanes 220 4\nramps 230\n"
    "msg 300 jackpot\nmsg 310 award\nmsg 320 switch_1\nend\n";

static void test_feedback_map(void)
{
    /* a map is read strictly: half of one would name arbitrary guest memory */
    static char cut[sizeof k_map];
    const char *why = "";
    WatchMap m;
    /* the names the log and the helper use: a lamp's made, the log's own last */
    assert(!strcmp(feedback_name(FB_LAMP_1), "lamp_1") && feedback_by_name("lamp_96") == FB_LAMP_96);
    assert(!strcmp(feedback_name(FB_SWITCH_24), "switch_24") && !strcmp(feedback_name(FB_TABLE_LOST), "table_lost"));
    assert(feedback_by_name("table_start") < 0 && feedback_by_name("lamp_97") < 0);
    assert(watch_map_parse(k_map, &m, &why));
    assert(!strcmp(m.name, "ep1") && m.size == 16 && m.fnv == 0x69691905u && m.pull_max == 708 && m.nbumpers == 2);
    assert(!strcmp(m.layout, "LR"));
    /* a table's own features: the zone a switch is, and each one's name and side for DOF */
    assert(!strcmp(m.features, "hole1=TestHole:L hole2=PowerUpHole:C hole3=RightHole:R kickback1=Kickback:L "
                               "rollover1=TopLane1:C rollover2=TopLane2:R ramp1=LeftRamp:L ramp2=CentreRamp:R "
                               "switch1=BallLock:C switch2=LeftLoop:R "
                               "target1=DropTarget1:L target2=DropTarget2:L target3=DropTarget3:L bank=DropTargets"));
    /* a switch zone is a feature of its own; a named one is never a switch's */
    assert(watch_zone(&m, WZ_SWITCH, 0xFB, 10, 10) >= 0 && watch_zone(&m, WZ_SWITCH, 0, 0, 0) < 0);
    {                                                     /* written on Windows: CRLF reads the same */
        static char crlf[2 * sizeof k_map];
        char *o = crlf;
        for (const char *p = k_map; *p; p++) { if (*p == '\n') *o++ = '\r'; *o++ = *p; }
        *o = 0;
        WatchMap w;
        assert(watch_map_parse(crlf, &w, &why) && !memcmp(&w, &m, sizeof w));
    }
    assert(watch_zone(&m, WZ_HOLE, 0xF6, 219, 50) == 1 && watch_zone(&m, WZ_HOLE, 0xF6, 220, 50) == 2);
    assert(watch_zone(&m, WZ_RAMP, 0xF6, 220, 50) == 7 && watch_zone(&m, WZ_RAMP, 0xF6, 219, 50) < 0);
    assert(watch_zone(&m, WZ_KICKBACK, 0xF8, 30, 320) < 0 && watch_zone(&m, WZ_KICKBACK, 0xF8, 30, 321) == 3);
    /* each bumper its own event, left to right; slings by side */
    assert(watch_kick_event(&m, 90, 70) == FB_BUMPER_1 && watch_kick_event(&m, 210, 70) == FB_BUMPER_2);
    assert(watch_kick_event(&m, 40, 300) == FB_SLING_LEFT && watch_kick_event(&m, 280, 300) == FB_SLING_RIGHT);
    {
        WatchMap six = m;                                 /* table 5's six: bumper_1-6 */
        const int xs[6] = { 28, 47, 100, 188, 240, 266 }, sides[6] = { 0, 0, 1, 1, 2, 2 };
        six.nbumpers = 6;
        for (int i = 0; i < 6; i++) { six.bumper[i].x = xs[i]; six.bumper[i].y = 150; six.bumper[i].slot = sides[i]; }
        const FbEvent want[6] = { FB_BUMPER_1, FB_BUMPER_2, FB_BUMPER_3, FB_BUMPER_4, FB_BUMPER_5, FB_BUMPER_6 };
        for (int i = 0; i < 6; i++) assert(watch_kick_event(&six, xs[i] + 1, 151) == (int)want[i]);
        WatchMap none = m;                                /* Enigma: stamped as it runs, by where they stand */
        none.nbumpers = 0;
        assert(watch_kick_event(&none, 100, 60) == FB_BUMPER_1 && watch_kick_event(&none, 160, 60) == FB_BUMPER_2
               && watch_kick_event(&none, 250, 60) == FB_BUMPER_3);
    }
    for (size_t n = 0; n < sizeof k_map - 5; n++) {       /* cut short anywhere */
        memcpy(cut, k_map, n);
        cut[n] = 0;
        assert(!watch_map_parse(cut, &m, &why));
    }
    memcpy(cut, k_map, sizeof k_map);
    strstr(cut, "feedback 8")[9] = '7';                   /* another version */
    assert(!watch_map_parse(cut, &m, &why));
    {
        /* names: letters and digits, each once on a table, one for a feature,
         * and drop targets with their bank */
        static const char *const bad[][2] = {
            { "C PowerUpHole", "C Power-Up" }, { "L LeftRamp", "L 1Ramp" }, { "R RightHole", "R TestHole" },
            { "R CentreRamp", "R righthole" }, { "L DropTarget2", "L DropTargetsDown" }, { "bank DropTargets\n", "" },
            { "L TestHole", "L" }, { "msg 320 switch_1", "msg 320 switch_3" }, { "switch 1 C", "switch 1 X" },
            { "feature switch", "feature hole" }, { "LeftLoop", "BallLock" },
        };
        for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            static char edited[sizeof k_map + 32];
            const char *at = strstr(k_map, bad[i][0]);
            assert(at);
            snprintf(edited, sizeof edited, "%.*s%s%s", (int)(at - k_map), k_map, bad[i][1], at + strlen(bad[i][0]));
            assert(!watch_map_parse(edited, &m, &why));
        }
        /* the guard: as the save under a ball begins (AX its x, DI its slot), an x
         * below 0 is put back where the game's clamp puts it; x = 0, another call: left */
        WatchMap g;
        assert(watch_map_parse(k_map, &g, &why) && !strcmp(g.guard_fn, "ep1_guard") && g.x_min == 1);
        const uint16_t seg = 0x2000;
        const uint32_t gds = (uint32_t)(seg + g.ds) * 16;
        uint8_t *mem = g_cpu.mem;
        CPU c = g_cpu;
        mem[gds + g.x + 2] = 0xFF; mem[gds + g.x + 3] = 0xFF;                       /* ball 1 at -1 */
        assert(guard_use(&g, seg));
        c.ax = 0xFFFF; c.di = 2;
        guard_enter(&c, "ep1_step");
        assert(c.ax == 0xFFFF && mem[gds + g.x + 2] == 0xFF);
        guard_enter(&c, "ep1_guard");
        assert(c.ax == 1 && mem[gds + g.x + 2] == 1 && mem[gds + g.x + 3] == 0);
        c.ax = 0;
        guard_enter(&c, "ep1_guard");
        assert(c.ax == 0);
        guard_table_end();
        assert(!g_guard_on);
        /* table 6: a bank of targets by its flags fires a feature the map has */
        static char banks[sizeof k_map + 64];
        const char *ln = strstr(k_map, "lanes 220 4\n");
        snprintf(banks, sizeof banks, "%.*sbankset 240 3 switch_2\n%s", (int)(ln - k_map), k_map, ln);
        assert(watch_map_parse(banks, &m, &why) && m.nbanksets == 1 && m.bankset[0].at == 0x240
               && m.bankset[0].n == 3 && m.bankset[0].event == FB_SWITCH_1 + 1);
        for (int bad = 0; bad < 3; bad++) {
            static const char *const wrong[3] = { "bankset 240 3 switch_9\n", "bankset 240 1 switch_2\n",
                                                  "bankset 240 3 jackpot\n" };
            snprintf(banks, sizeof banks, "%.*s%s%s", (int)(ln - k_map), k_map, wrong[bad], ln);
            assert(!watch_map_parse(banks, &m, &why));
        }
        /* table 9: a second way to show a text */
        static char two[sizeof k_map + 32];
        const char *mf = strstr(k_map, "msg_fn ep1_msg\n");
        snprintf(two, sizeof two, "%.*smsg_fn ep1_msg2\n%s", (int)(mf - k_map), k_map, mf);
        assert(watch_map_parse(two, &m, &why) && !strcmp(m.msg_fn, "ep1_msg2") && !strcmp(m.msg_fn2, "ep1_msg"));
        /* Enigma: nothing passes on its wires */
        static char wires[sizeof k_map];
        const char *on = strstr(k_map, "on_layer F9 F8 F1");
        snprintf(wires, sizeof wires, "%.*son_layer %s", (int)(on - k_map), k_map, on + strlen("on_layer F9 F8 F1"));
        assert(watch_map_parse(wires, &m, &why) && m.non_layer == 0);
        /* a feature drawn twice: one name */
        static char twice[sizeof k_map + 64];
        const char *at = strstr(k_map, "zone kickback");
        snprintf(twice, sizeof twice, "%.*szone hole 1 F5 0 0 10 10 L TestHole\n%s", (int)(at - k_map), k_map, at);
        assert(watch_map_parse(twice, &m, &why));
        snprintf(twice, sizeof twice, "%.*szone hole 1 F5 0 0 10 10 L OtherHole\n%s", (int)(at - k_map), k_map, at);
        assert(!watch_map_parse(twice, &m, &why));
    }
    memcpy(cut, k_map, sizeof k_map);
    strstr(cut, "bumper 200")[7] = '0';                   /* not left to right */
    assert(!watch_map_parse(cut, &m, &why));
    memcpy(cut, k_map, sizeof k_map);
    strstr(cut, "pull 180")[0] = 'q';                     /* a key it does not know */
    assert(!watch_map_parse(cut, &m, &why));
    memcpy(cut, k_map, sizeof k_map);
    strstr(cut, "hole 3 F6 220 0 319 399 R")[5] = '7';    /* more holes than there are events */
    assert(!watch_map_parse(cut, &m, &why));
    memcpy(cut, k_map, sizeof k_map);
    strstr(cut, "ramp 2 F6 220")[5] = '3';                /* a number left out */
    assert(!watch_map_parse(cut, &m, &why));
    memcpy(cut, k_map, sizeof k_map);
    strstr(cut, "msg 310 award")[8] = 'b';                /* not a message's event */
    assert(!watch_map_parse(cut, &m, &why));
    memcpy(cut, k_map, sizeof k_map);
    strstr(cut, "layer 240")[0] = 'q';                    /* zones need the handler's gate */
    assert(!watch_map_parse(cut, &m, &why));
    memcpy(cut, k_map, sizeof k_map);
    strstr(cut, "on_layer F9")[9] = 'X';                  /* a colour it cannot read */
    assert(!watch_map_parse(cut, &m, &why));
    memcpy(cut, k_map, sizeof k_map);
    strstr(cut, "lamps 3")[6] = '0';                      /* a table has lamps */
    assert(!watch_map_parse(cut, &m, &why));
    memcpy(cut, k_map, sizeof k_map);
    strstr(cut, "lamp_fn")[0] = 'q';                      /* and draws them */
    assert(!watch_map_parse(cut, &m, &why));
    assert(watch_map_parse(k_map, &m, &why) && m.lamps == 3 && m.lamp_seg == 0x3000 && !strcmp(m.lamp_fn, "ep1_lamp"));
}

static int seen[FB_OUTPUTS];
static void seen_table(const char *name, const char *bumpers, const char *features) { (void)name; (void)bumpers; (void)features; }
static int level[FB_OUTPUTS];
static void seen_event(FbEvent e, int on) { seen[e] += on; level[e] = on; }
static void seen_off(void) { }

static void store8(uint32_t a, uint8_t v)
{
    watch_write_byte(a, v);                               /* as vga.c's hook: before the store */
    g_cpu.mem[a] = v;
}

static uint16_t rd(uint32_t a) { return (uint16_t)(g_cpu.mem[a] | g_cpu.mem[a + 1] << 8); }

/* A lamp's blit as the renderer makes it: its picture and its state's place
 * in the list on the stack over the far return. The lamp it is, twice, and 1
 * if lit; 0 if none */
static int lamp_at(uint16_t ds, uint16_t bx, uint16_t si, uint16_t state)
{
    CPU c = g_cpu;
    uint8_t *st = g_cpu.mem + (uint32_t)c.ss * 16 + c.sp;
    c.ds = ds; c.bx = bx; c.si = si;
    st[4] = (uint8_t)si; st[5] = (uint8_t)(si >> 8); st[6] = (uint8_t)state; st[7] = (uint8_t)(state >> 8);
    int lit = -1, k = watch_lamp_at(&c, &lit);
    return k ? k * 2 + lit : 0;
}

static void test_watch(void)
{
    /* a hit is the kicker storing its strength, not being entered: tilted, it
     * turns back, and tables 11-13 only bounce in a low zone */
    static const FbSink sink = { seen_table, seen_event, seen_off };
    const char *why = "";
    WatchMap m;
    assert(watch_map_parse(k_map, &m, &why));
    feedback_sink(&sink);
    g_cpu.ss = 0x9000; g_cpu.sp = 0x0100;
    assert(watch_map_use(&m, 0x1000));
    uint8_t *ds = g_cpu.mem + g_watch_ds;
    uint32_t kick = g_watch_ds + m.kick, vy = g_watch_ds + m.vy, pull = g_watch_ds + m.pull;
    g_cpu.sp = 0x00F0;
    ds[m.map + 1] = 0x50;                                 /* the collision map's segment */
    g_cpu.es = 0x5000; g_cpu.bx = (uint16_t)(60 * 320 + 95); g_cpu.di = 0;
    ds[m.y] = 50;

    store8(kick, 9);                                      /* not from the kicker */
    assert(!seen[FB_BUMPER_1]);
    watch_enter(&g_cpu, "ep1_kick");                      /* entered, and turned back */
    assert(!seen[FB_BUMPER_1]);
    store8(kick, 9);
    store8(kick, 9);                                      /* one entry, one hit */
    assert(seen[FB_BUMPER_1] == 1);
    ds[m.y] = 0x80; ds[m.y + 1] = 1;                      /* at weak_y */
    watch_enter(&g_cpu, "ep1_kick");
    store8(kick, 3);
    assert(seen[FB_BUMPER_1] == 1);

    ds[m.demo] = 1;                                       /* the demo is never felt */
    ds[m.y] = 50; ds[m.y + 1] = 0;
    watch_enter(&g_cpu, "ep1_kick");
    store8(kick, 9);
    assert(seen[FB_BUMPER_1] == 1);
    ds[m.demo] = 0;

    ds[m.pull] = 0; ds[m.pull + 1] = 0;                   /* the host cancelling a pull is no launch */
    store8(vy, 4); store8(vy + 1, 0);
    assert(!seen[FB_LAUNCH]);
    ds[m.pull] = 0xC4; ds[m.pull + 1] = 2;                /* the game: zeroes it, then the velocity */
    store8(pull, 0); store8(pull + 1, 0);
    store8(vy, 0x3C); store8(vy + 1, 0xFD);
    assert(seen[FB_LAUNCH] == 1);

    /* multiball: two balls rolling. Not a ball parked or settling (table 3
     * parks one in its kickback), and not after one is gone */
    #define BALL(i, X, Y, ON) (ds[m.x + 2 * (i)] = (uint8_t)(X), ds[m.x + 2 * (i) + 1] = (uint8_t)((X) >> 8), \
                               ds[m.y + 2 * (i)] = (uint8_t)(Y), ds[m.y + 2 * (i) + 1] = 0, ds[m.active + 2 * (i)] = (ON))
    BALL(0, 100, 100, 1); BALL(1, 200, 200, 1);
    watch_enter(&g_cpu, "ep1_step");
    assert(!seen[FB_MULTIBALL]);
    BALL(0, 150, 100, 1); watch_enter(&g_cpu, "ep1_step");        /* one rolls */
    BALL(1, 202, 201, 1); watch_enter(&g_cpu, "ep1_step");        /* the other settles */
    assert(!seen[FB_MULTIBALL]);
    BALL(1, 240, 200, 1); watch_enter(&g_cpu, "ep1_step");        /* both roll */
    assert(seen[FB_MULTIBALL] == 1 && level[FB_MULTIBALL]);
    BALL(1, 240, 200, 0); watch_enter(&g_cpu, "ep1_step");        /* one drains */
    assert(seen[FB_MULTIBALL] == 1 && !level[FB_MULTIBALL]);
    #undef BALL

    /* a table's own: a switch is the colour in AL where the scanned ball is */
    #define AT(X, Y) (ds[m.scan_x] = (uint8_t)(X), ds[m.scan_x + 1] = (uint8_t)((X) >> 8), \
                      ds[m.scan_y] = (uint8_t)(Y), ds[m.scan_y + 1] = (uint8_t)((Y) >> 8))
    #define SWITCH(C, X, Y) (AT(X, Y), g_cpu.al = (C), watch_enter(&g_cpu, "ep1_sw"))
    pit_at = 0;
    SWITCH(0xFA, 100, 30);                                /* rolled over: once per pass */
    assert(seen[FB_ROLLOVER_1] == 1 && !seen[FB_ROLLOVER_2]);
    pit_at += (uint64_t)(PIT_HZ / 1000 * 75);             /* the game's own latch: handled again */
    SWITCH(0xFA, 101, 31);
    assert(seen[FB_ROLLOVER_1] == 1);
    pit_at += (uint64_t)(PIT_HZ / 1000 * 400);            /* left it, and back */
    SWITCH(0xFA, 101, 31);
    assert(seen[FB_ROLLOVER_1] == 2);
    SWITCH(0xFA, 200, 31);
    assert(seen[FB_ROLLOVER_2] == 1);
    SWITCH(0xF8, 30, 300);                                /* the same colour, not the kickback's place */
    assert(!seen[FB_KICKBACK]);
    SWITCH(0xF8, 30, 390);
    assert(seen[FB_KICKBACK] == 1);

    /* the handler's own gate: tilted it handles only F0 and F6, and with the
     * ball on the ramps only F9, F8 and F1. What it turns back is not felt */
    pit_at += (uint64_t)(PIT_HZ / 1000 * 400);
    ds[m.tilt] = 1;
    SWITCH(0xF8, 30, 390);
    SWITCH(0xFA, 100, 30);
    assert(seen[FB_KICKBACK] == 1 && seen[FB_ROLLOVER_1] == 2);
    ds[m.tilt] = 0;
    ds[0x240] = 1;                                        /* on a ramp's wire, over a lane */
    SWITCH(0xFA, 100, 30);
    assert(seen[FB_ROLLOVER_1] == 2);
    SWITCH(0xF8, 30, 390);                                /* F8 is handled there */
    assert(seen[FB_KICKBACK] == 2);
    ds[0x240] = 0;

    /* holes: caught by the switch the ball is on; kicked out at the timer's end.
     * Two holes share timer 200, as table 1's power-up and left holes do */
    const uint32_t t0 = g_watch_ds + 0x200, t1 = g_watch_ds + 0x201;
    SWITCH(0xF6, 100, 150);
    store8(t0, 0x32);
    watch_enter(&g_cpu, "ep1_step");
    assert(level[FB_BALL_HELD] && seen[FB_BALL_HELD] == 1);
    for (int v = 0x31; v > 0; v--) { SWITCH(0xF6, 100, 150); store8(t0, (uint8_t)v); }
    assert(!seen[FB_HOLE_2]);
    SWITCH(0xF6, 100, 150);
    store8(t0, 0);
    watch_enter(&g_cpu, "ep1_step");
    assert(seen[FB_HOLE_2] == 1 && !seen[FB_HOLE_3] && !level[FB_BALL_HELD]);
    SWITCH(0xF6, 250, 150);                               /* the other hole on that colour, which also counts a ramp */
    store8(t1, 0x50);
    store8(g_watch_ds + 0x230, 1);
    assert(seen[FB_RAMP_2] == 1 && !seen[FB_RAMP_1]);
    watch_enter(&g_cpu, "ep1_step");                      /* a new step: no switch yet */
    store8(t1, 0);                                        /* let go elsewhere (multiball starting) */
    assert(seen[FB_HOLE_3] == 1);
    store8(t0, 9); store8(t0, 0);                         /* set without a hole's switch: no hole known */
    assert(seen[FB_HOLE_1] == 0 && seen[FB_HOLE_2] == 1);
    store8(g_watch_ds + 0x230, 2);                        /* a count with no ramp's switch */
    SWITCH(0xF9, 40, 60);
    store8(g_watch_ds + 0x230, 0);                        /* reset at a new ball */
    store8(g_watch_ds + 0x230, 1);
    assert(seen[FB_RAMP_1] == 1 && seen[FB_RAMP_2] == 1);

    /* drop targets: each down, then the bank resetting; lanes: all lit, then out */
    const uint32_t tg = g_watch_ds + 0x210, ln = g_watch_ds + 0x220;
    SWITCH(0xF2, 49, 220);
    store8(tg + 1, 1);
    store8(tg + 1, 1);                                    /* already down */
    store8(tg, 1);
    assert(seen[FB_TARGET_2] == 1 && seen[FB_TARGET_1] == 1 && !seen[FB_TARGETS_DOWN]);
    store8(tg + 2, 1);
    store8(tg, 0); store8(tg + 1, 0); store8(tg + 2, 0);
    assert(seen[FB_TARGET_3] == 1 && seen[FB_TARGETS_DOWN] == 1);
    for (int i = 0; i < 4; i++) ds[0x220 + i] = 2;
    store8(ln + 1, 1); store8(ln + 2, 1); store8(ln + 3, 1);
    store8(ln, 2); store8(ln, 1);                         /* the lanes changed by a flipper */
    assert(!seen[FB_LANES_LIT]);
    store8(ln + 1, 1);                                    /* the last one... */
    store8(ln, 2); store8(ln + 1, 2); store8(ln + 2, 2); store8(ln + 3, 2);     /* ...and all out */
    assert(seen[FB_LANES_LIT] == 1);

    /* awards: the message function, with the text in BX */
    g_cpu.bx = 0x300; watch_enter(&g_cpu, "ep1_msg");
    g_cpu.bx = 0x301; watch_enter(&g_cpu, "ep1_msg");
    g_cpu.bx = 0x310; watch_enter(&g_cpu, "ep1_msg");
    assert(seen[FB_JACKPOT] == 1 && seen[FB_AWARD] == 1 && !seen[FB_SUPER_JACKPOT]);

    SWITCH(0xF5, 10, 17);                                 /* held, then a tilt stops its count: let go */
    store8(t0, 0x46);
    watch_enter(&g_cpu, "ep1_step");
    assert(level[FB_BALL_HELD]);
    pit_at += (uint64_t)(PIT_HZ / 1000 * 400);
    watch_enter(&g_cpu, "ep1_step");
    assert(!level[FB_BALL_HELD] && seen[FB_BALL_HELD] == 2);
    SWITCH(0xF5, 10, 17);                                 /* back in it: counted again */
    store8(t0, 0x45);
    watch_enter(&g_cpu, "ep1_step");
    assert(level[FB_BALL_HELD]);                          /* held at the table's end: let go */
    #undef SWITCH
    #undef AT
    watch_table_end();
    assert(!level[FB_BALL_HELD]);

    /* lamps: the blitter drawing a lamp's lit or unlit picture (DS:SI, BX its
     * place in the table of them; on the stack over the far return, the
     * picture and the lamp's state in the list the renderer was given), sent
     * as levels once a frame, kept through the demo. Table 5 has lamps lit
     * and unlit alike: by BX, not SI */
    {
        uint8_t *pic = g_cpu.mem + (uint32_t)(0x1000 + 0x3000) * 16 + 0x10;
        for (int i = 0; i < 6; i++) { pic[2 * i] = (uint8_t)(8 * i); pic[2 * i + 1] = 1; }   /* 0x100, 0x108 ... */
        pic[10] = 0x20;                                   /* lamp 3 unlit as lit: 0x120 */
        assert(watch_map_use(&m, 0x1000));
        assert(lamp_at(0x4000, 0, 0x100, 0x501) == 0);    /* the renderer not entered yet */
        g_cpu.si = 0x500;
        watch_enter(&g_cpu, "ep1_lamps");                 /* it draws the list at 500 */
        assert(lamp_at(0x4000, 0, 0x100, 0x501) == 3 && lamp_at(0x4000, 2, 0x108, 0x501) == 2);
        assert(lamp_at(0x4000, 10, 0x120, 0x503) == 6 && lamp_at(0x4000, 8, 0x120, 0x503) == 7);
        assert(!lamp_at(0x4000, 12, 0x130, 0x504) && !lamp_at(0x4000, 0, 0x108, 0x501)
               && !lamp_at(0x3FFF, 0, 0x100, 0x501) && !lamp_at(0x4000, 1, 0x100, 0x501));
        assert(!lamp_at(0x4000, 0, 0x100, 0x502) && !lamp_at(0x4000, 0, 0x100, 0x2));  /* not from the renderer */
        #define BLIT(DS, BX) (lamp_at(DS, BX, rd(0x40010 + (BX)), (uint16_t)(0x500 + (BX) / 4 + 1)), \
                              g_cpu.ds = (DS), g_cpu.bx = (BX), g_cpu.si = rd(0x40010 + (BX)), watch_enter(&g_cpu, "ep1_lamp"))
        BLIT(0x4000, 0); BLIT(0x3FFF, 4);                 /* the second: another segment's picture */
        watch_tick();
        assert(level[FB_LAMP_1] && seen[FB_LAMP_1] == 1 && !seen[FB_LAMP_1 + 1]);
        BLIT(0x4000, 2); BLIT(0x4000, 0);                 /* out and in, one frame: nothing changed */
        watch_tick();
        assert(seen[FB_LAMP_1] == 1 && level[FB_LAMP_1]);
        BLIT(0x4000, 2); BLIT(0x4000, 8);
        watch_tick();
        assert(!level[FB_LAMP_1] && level[FB_LAMP_1 + 2]);
        BLIT(0x4000, 10);                                 /* the same picture, unlit */
        watch_tick();
        assert(!level[FB_LAMP_1 + 2]);
        BLIT(0x4000, 8);
        ds[m.demo] = 1;                                   /* the demo: not felt */
        BLIT(0x4000, 0);
        watch_tick();
        assert(!level[FB_LAMP_1]);
        ds[m.demo] = 0;                                   /* out of it: every lit lamp again */
        watch_tick();
        assert(level[FB_LAMP_1] && level[FB_LAMP_1 + 2] && seen[FB_LAMP_1 + 2] == 2);
        watch_released();                                 /* the host let everything go */
        watch_tick();
        assert(seen[FB_LAMP_1 + 2] == 3);
        watch_table_end();
        assert(!level[FB_LAMP_1] && !level[FB_LAMP_1 + 2]);
        /* tables 3 and 5: lights glowing on the palette past their lamps, by the
         * glow function's CX (which) and BL (its state); lit at half brightness */
        assert(!watch_glow_lit(0xFD, 0) && watch_glow_lit(0xFE, 0) && !watch_glow_lit(0x18, 0) && watch_glow_lit(0x19, 0)
               && watch_glow_lit(0x31, 0) && watch_glow_lit(0x4A, 0) && !watch_glow_lit(0x4B, 0)
               && !watch_glow_lit(0x31, 1) && watch_glow_lit(0x32, 1));
        static char glow[sizeof k_map + 64];
        const char *lf = strstr(k_map, "lamp_seg");
        WatchMap gm;
        snprintf(glow, sizeof glow, "%.*sglow_fn ep1_glow\nglow_from 5\nglows 2\n%s", (int)(lf - k_map), k_map, lf);
        assert(watch_map_parse(glow, &gm, &why) && gm.glow_from == 5 && gm.glows == 2);
        for (int bad = 0; bad < 3; bad++) {
            static const char *const wrong[3] = { "glow_fn ep1_glow\nglow_from 3\nglows 2\n", "glow_from 5\nglows 2\n",
                                                  "glow_fn ep1_glow\nglow_from 5\nglows 5D\n" };
            snprintf(glow, sizeof glow, "%.*s%s%s", (int)(lf - k_map), k_map, wrong[bad], lf);
            assert(!watch_map_parse(glow, &m, &why));
        }
        pic[-0x10 + 4 + 3] = 1;                           /* lamp 6 only falls */
        assert(watch_map_use(&gm, 0x1000));
        #define GLOW(K, BL) (g_cpu.cx = (K), g_cpu.bl = (BL), watch_enter(&g_cpu, "ep1_glow"))
        GLOW(5, 0xFE); GLOW(6, 0x20); GLOW(7, 0xFE);      /* 7: past them, nothing */
        watch_tick();
        assert(level[FB_LAMP_1 + 4] && !level[FB_LAMP_1 + 5] && !seen[FB_LAMP_1 + 6]);
        GLOW(5, 0xFD); GLOW(6, 0x40);
        watch_tick();
        assert(!level[FB_LAMP_1 + 4] && level[FB_LAMP_1 + 5]);
        watch_table_end();
        assert(!level[FB_LAMP_1 + 5]);
        #undef GLOW
        #undef BLIT
    }
    pit_at = 0;
    feedback_sink(NULL);
}

static uint8_t pds[0x10000], pcs[0x10000];
static Plunger plung;
static int launched, shown, jumped;
static double pms;

static uint16_t word(const uint8_t *m, uint16_t o) { return (uint16_t)(m[o] | m[o + 1] << 8); }
static void put(uint8_t *m, uint16_t o, uint16_t v) { m[o] = (uint8_t)v; m[o + 1] = (uint8_t)(v >> 8); }

/* The table's plunger block, a 14 ms frame at a time, under a sensor going
 * straight from `from` to `to` in `ms` */
static void sensor(const WatchMap *m, int ms, int from, int to, int running)
{
    for (int i = 1; i <= ms; i++) {
        pms += 1;
        PlungerInput in = { from >= 0, from + (to - from) * i / ms, 0 };
        plunger_step(&plung, m, pds, pcs, running, pms, &in);
        uint16_t pull = word(pds, m->pull);
        assert(!pull || pcs[m->ctrl] == 1 || plung.state);          /* never a pull without the flag */
        assert(!pcs[m->space]);                                     /* never Space */
        assert(pull <= m->pull_max);
        if (!running || (int)pms % 14) continue;
        if (pcs[m->ctrl] == 1) {
            if (pull < m->pull_max) pull = (uint16_t)(pull + m->pull_inc);
            put(pds, m->pull, pull);
            int px = pull >> m->pull_shift;
            if (abs(px - shown) > 1) jumped++;                      /* further leaves a trail */
            shown = px;
        } else if (pull) {
            launched = pull;
            shown = 0;
            put(pds, m->pull, 0);
        }
    }
}

static void lane(const WatchMap *m)
{
    memset(pds, 0, sizeof pds); memset(pcs, 0, sizeof pcs);
    put(pds, m->active, 1); put(pds, m->x, 284); put(pds, m->y, 336);
    plunger_init(&plung);
    launched = shown = jumped = 0;
    pms = 0;
    sensor(m, 400, 0, 0, 1);
}

static void test_plunger(void)
{
    /* the analog plunger writes the game's memory: only as the keyboard could */
    const char *why = "";
    WatchMap m;
    assert(watch_map_parse(k_map, &m, &why));

    lane(&m);                                             /* let fly: the strength pulled to, once */
    sensor(&m, 200, 0, 1000, 1);
    sensor(&m, 900, 1000, 1000, 1);
    assert(!launched && shown == 708 >> 5 && !jumped);
    sensor(&m, 35, 1000, 0, 1);
    sensor(&m, 40, 0, 0, 1);
    assert(launched == 708);
    launched = 0;
    sensor(&m, 30, 0, 380, 1); sensor(&m, 30, 380, 0, 1); /* its bounce */
    sensor(&m, 500, 0, 0, 1);
    assert(!launched && !word(pds, m.pull) && !pcs[m.ctrl]);

    lane(&m);                                             /* half: a strength the keyboard can make */
    sensor(&m, 100, 0, 500, 1); sensor(&m, 300, 500, 500, 1); sensor(&m, 30, 500, 0, 1); sensor(&m, 40, 0, 0, 1);
    assert(launched == 360 && launched % m.pull_inc == 0);

    lane(&m);                                             /* eased home: no launch, no trail */
    sensor(&m, 200, 0, 1000, 1); sensor(&m, 600, 1000, 1000, 1);
    sensor(&m, 700, 1000, 0, 1); sensor(&m, 900, 0, 0, 1);
    assert(!launched && !shown && !jumped && !pcs[m.ctrl] && !word(pds, m.pull));

    lane(&m);                                             /* unplugged: axes centre, which is no release */
    sensor(&m, 200, 0, 1000, 1); sensor(&m, 400, 1000, 1000, 1);
    sensor(&m, 900, -1, -1, 1);
    assert(!launched && !shown && !pcs[m.ctrl] && !word(pds, m.pull));

    lane(&m);                                             /* paused: let go, and nothing on resuming */
    sensor(&m, 200, 0, 1000, 1); sensor(&m, 100, 1000, 1000, 1);
    sensor(&m, 100, 1000, 1000, 0);
    assert(!word(pds, m.pull) && !pcs[m.ctrl]);
    sensor(&m, 35, 1000, 0, 0); sensor(&m, 400, 0, 0, 1);
    assert(!launched);

    for (int no = 0; no < 3; no++) {                      /* demo, game over, no ball: nothing written */
        lane(&m);
        if (no == 0) pds[m.demo] = 1;
        if (no == 1) pds[m.over] = 1;
        if (no == 2) put(pds, m.x, 100);
        sensor(&m, 200, 0, 1000, 1); sensor(&m, 400, 1000, 1000, 1);
        assert(!word(pds, m.pull) && !pcs[m.ctrl]);
        sensor(&m, 30, 1000, 0, 1); sensor(&m, 60, 0, 0, 1);
        assert(!launched && !word(pds, m.pull) && !pcs[m.ctrl]);
    }

    WatchMap e = m;                                       /* Enigma: one strength, past a quarter */
    e.pull_inc = 0; e.pull_shift = 0; e.pull_max = 700;
    lane(&e);
    put(pds, e.active, 0);
    sensor(&e, 100, 0, 200, 1); sensor(&e, 300, 200, 200, 1); sensor(&e, 20, 200, 0, 1); sensor(&e, 400, 0, 0, 1);
    assert(!launched);
    sensor(&e, 100, 0, 400, 1); sensor(&e, 300, 400, 400, 1); sensor(&e, 30, 400, 0, 1); sensor(&e, 40, 0, 0, 1);
    assert(launched == 700);

    assert(plunger_trace("0:0,100:100,140:0,200:-"));
    PlungerInput in;
    plunger_trace_at(50, &in); assert(in.have && in.pos == 500);
    plunger_trace_at(250, &in); assert(!in.have);
    assert(!plunger_trace("100:0,50:0") && !plunger_trace("10:101") && !plunger_trace("x") && !plunger_trace(""));
}

/* --dmd: a message set again for 128x32, moved as the game moves its dots;
 * a lasting one gives way to the board; the playfield gets back what the
 * game saved from under its dots while it is composed (src/dmd.c) */
#define LIT(f, x, y) ((f)[(y) * DMD_W + (x)] & 0xFFFFFF)
static int lit_in(const uint32_t *f, int x0, int y0, int x1, int y1)
{
    int n = 0;
    for (int y = 0; y < DMD_H; y++)
        for (int x = 0; x < DMD_W; x++)
            if (f[y * DMD_W + x] & 0xFFFFFF) { assert(x >= x0 && x < x1 && y >= y0 && y < y1); n++; }
    return n;
}

static void test_dmd(void)
{
    char map[] = "epicpinball-dmd 1\nimage ep1 1 0\nds 100\nmsg_fn T_msg\nstep_fn T_step\n"
                 "font large 200 0 8 7 7 16\nfont small 200 300 5 5 5 11\nappend T_app 200 300 5 5 5 11\n"
                 "list 10\neffect 2\ncount 4\nfamily playfield\nsave 2000 3000\nsave 2400 3002\n";
    const char *why = "";
    assert(dmd_map_use(map, 0x1000, &why));
    const uint32_t DS = 0x11000, FONT = 0x12000;
    memset(g_cpu.mem + DS, 0, 0x10000);
    for (int r = 0; r < 7; r++) g_cpu.mem[FONT + ('A' - 32) * 8 + r] = 0xFE;      /* A: a 7x7 block */
    for (int r = 0; r < 5; r++) g_cpu.mem[FONT + 0x300 + ('B' - 32) * 5 + r] = 0xF8;   /* small B: 5x5 */
    memcpy(g_cpu.mem + DS + 0x500, "AA", 3);
    memcpy(g_cpu.mem + DS + 0x510, "B", 2);
    vga_dac_set(0xFF, 63, 63, 63);

    CPU c = g_cpu;                                                   /* its memory */
    c.ds = 0x1100; c.bx = 0x500; c.ax = 0x0101; c.di = 320 * 10;   /* large, effect 1: the game ends it */
    pit_at = 0;
    dmd_enter(&c, "T_msg");
    /* the game's own list: centred across 320 (145 = (320 - 2*16)/2 + 1), column by column */
    uint16_t at = 320 * 10 + 145;
    int n = 0;
    for (int ch = 0; ch < 2; ch++, at += 2)
        for (int col = 0; col < 7; col++, at += 2)
            for (int row = 0; row < 7; row++) put(g_cpu.mem + DS, (uint16_t)(0x10 + 2 * n++), (uint16_t)(at + row * 640));
    put(g_cpu.mem + DS, 4, 1);                                       /* its frame counter: up */

    uint32_t f[DMD_W * DMD_H];
    assert(dmd_frame(NULL, 0, 0, f));
    /* "AA" is 15 dots wide and 7 high: doubled, centred */
    assert(lit_in(f, 49, 9, 79, 23) == 2 * 49 * 4);
    assert(f[9 * DMD_W + 49] == 0xFFFFFFFFu);                        /* DAC FFh: white */
    assert(!dmd_frame(NULL, 0, 0, f));                                /* nothing new */
    /* an effect moves a dot 4 screen pixels right: 2 font pixels, 4 DMD dots doubled */
    put(g_cpu.mem + DS, 0x10, (uint16_t)(320 * 10 + 145 + 4));
    assert(dmd_frame(NULL, 0, 0, f) && LIT(f, 53, 9) && !LIT(f, 49, 9));
    put(g_cpu.mem + DS, 0x10, 1);                                    /* gone: not drawn; it was on */
    assert(!dmd_frame(NULL, 0, 0, f) && LIT(f, 53, 9) && !LIT(f, 49, 9));       /* column 2's dot: no change */

    /* a line appended under it: two rows, not doubled; 7 + 2 + 5 high, centred */
    c.bx = 0x510; c.di = 320 * 30;
    dmd_enter(&c, "T_app");
    for (int k = 0; k < 25; k++) put(g_cpu.mem + DS, (uint16_t)(0x10 + 2 * (98 + k)), (uint16_t)(320 * 30 + (k / 5) * 2 + (k % 5) * 640));
    put(g_cpu.mem + DS, 0x10, (uint16_t)(320 * 10 + 145));
    assert(dmd_frame(NULL, 0, 0, f));
    assert(lit_in(f, 56, 9, 72, 23) == 2 * 49 + 25);
    assert(LIT(f, 61, 18) && !LIT(f, 61, 17));               /* B's top row, under a gap */

    /* effect 3 never ends by itself: after 4 s of play the board (here none:
     * dark). Waiting on the player (no physics steps), it stays */
    c.bx = 0x500; c.ax = 0x0103; c.di = 320 * 10;
    g_cpu.mem[DS + 2] = 3;                                           /* the game's effect byte */
    dmd_enter(&c, "T_msg");
    assert(dmd_frame(NULL, 0, 0, f) && lit_in(f, 0, 0, DMD_W, DMD_H));
    pit_at = (uint64_t)(10.0 * PIT_HZ);                              /* 10 s, nothing played */
    dmd_enter(&c, "T_step");
    assert(!dmd_frame(NULL, 0, 0, f) && lit_in(f, 0, 0, DMD_W, DMD_H));
    for (int ms = 0; ms < 3900; ms += 5) { pit_at += (uint64_t)(0.005 * PIT_HZ); dmd_enter(&c, "T_step"); }
    assert(!dmd_frame(NULL, 0, 0, f));                               /* 3.9 s played: still up */
    for (int ms = 0; ms < 200; ms += 5) { pit_at += (uint64_t)(0.005 * PIT_HZ); dmd_enter(&c, "T_step"); }
    assert(dmd_frame(NULL, 0, 0, f) && !lit_in(f, 0, 0, DMD_W, DMD_H));
    g_cpu.mem[DS + 2] = 1;                                           /* the game made it explode: it ends itself */
    assert(dmd_frame(NULL, 0, 0, f) && lit_in(f, 0, 0, DMD_W, DMD_H));
    put(g_cpu.mem + DS, 4, 0);                                       /* the game ended it */
    assert(dmd_frame(NULL, 0, 0, f) && !lit_in(f, 0, 0, DMD_W, DMD_H));

    /* taller than the DMD: a page at a time, 2 s each. "AA" and six lines of
     * B, 7 + 6 * (2 + 5) = 49 high: rows that fit on the first page, the rest
     * on the second */
    c.bx = 0x500; c.ax = 0x0100; c.di = 320 * 10;
    dmd_enter(&c, "T_msg");
    c.bx = 0x510;
    for (int k = 0; k < 6; k++) { c.di = (uint16_t)(320 * (30 + 16 * k)); dmd_enter(&c, "T_app"); }
    for (int i = 0; i < 98; i++) put(g_cpu.mem + DS, (uint16_t)(0x10 + 2 * i), 2);   /* moved nowhere: drawn */
    n = 0;
    at = 320 * 10 + 145;
    for (int ch = 0; ch < 2; ch++, at += 2)
        for (int col = 0; col < 7; col++, at += 2)
            for (int row = 0; row < 7; row++) put(g_cpu.mem + DS, (uint16_t)(0x10 + 2 * n++), (uint16_t)(at + row * 640));
    for (int k = 0; k < 6; k++)
        for (int d = 0; d < 25; d++)
            put(g_cpu.mem + DS, (uint16_t)(0x10 + 2 * n++), (uint16_t)(320 * (30 + 16 * k) + (d / 5) * 2 + (d % 5) * 640));
    put(g_cpu.mem + DS, (uint16_t)(0x10 + 2 * n), 0);
    put(g_cpu.mem + DS, 4, 1);
    assert(dmd_frame(NULL, 0, 0, f));
    const int first = lit_in(f, 0, 0, DMD_W, DMD_H);                 /* AA and three lines: 7 + 3 * 7 = 28 */
    assert(first == 2 * 49 + 3 * 25);
    pit_at += (uint64_t)(2.1 * PIT_HZ);
    assert(dmd_frame(NULL, 0, 0, f) && lit_in(f, 0, 0, DMD_W, DMD_H) == 3 * 25);
    pit_at += (uint64_t)(2.0 * PIT_HZ);
    assert(dmd_frame(NULL, 0, 0, f) && lit_in(f, 0, 0, DMD_W, DMD_H) == first);   /* round again */
    put(g_cpu.mem + DS, 4, 0);

    /* the playfield: a dot (colour FFh) gets back what was under it, the
     * ball drawn over another is left; both as they were afterwards */
    put(g_cpu.mem + DS, 0x3000, 0xA000);
    put(g_cpu.mem + DS, 0x2000, 0x0010); put(g_cpu.mem + DS, 0x2002, 0x5A01);   /* plane 0, saved 5Ah */
    put(g_cpu.mem + DS, 0x2004, 0x0011); put(g_cpu.mem + DS, 0x2006, 0x3302);   /* plane 1, saved 33h */
    put(g_cpu.mem + DS, 0x2008, 0);
    put(g_cpu.mem + DS, 0x3002, 0xA000); put(g_cpu.mem + DS, 0x2400, 0);
    vga_vram_poke(0x10 << 2, 0xFF);
    vga_vram_poke(0x11 << 2 | 1, 0x77);                              /* the ball, over a dot */
    dmd_hide(1);
    assert(vga_vram_peek(0x10 << 2) == 0x5A && vga_vram_peek(0x11 << 2 | 1) == 0x77);
    dmd_hide(0);
    assert(vga_vram_peek(0x10 << 2) == 0xFF && vga_vram_peek(0x11 << 2 | 1) == 0x77);

    /* a wave (effect 3) moves a dot of "AA", doubled at rows 9-22, 6 font
     * pixels down, past the bottom: the message is raised the 3 rows it went
     * over, and stays so */
    put(g_cpu.mem + DS, 0x2000, 0); put(g_cpu.mem + DS, 0x2400, 0);
    c.bx = 0x500; c.ax = 0x0103; c.di = 320 * 10;
    dmd_enter(&c, "T_msg");
    at = 320 * 10 + 145; n = 0;
    for (int ch = 0; ch < 2; ch++, at += 2)
        for (int col = 0; col < 7; col++, at += 2)
            for (int row = 0; row < 7; row++) put(g_cpu.mem + DS, (uint16_t)(0x10 + 2 * n++), (uint16_t)(at + row * 640));
    put(g_cpu.mem + DS, (uint16_t)(0x10 + 2 * n), 0);
    put(g_cpu.mem + DS, 4, 1);
    g_cpu.mem[DS + 2] = 3;
    assert(dmd_frame(NULL, 0, 0, f) && LIT(f, 49, 9) && LIT(f, 49, 22));      /* still: not raised */
    put(g_cpu.mem + DS, 0x10 + 2 * 6, (uint16_t)(320 * 10 + 145 + 12 * 640));   /* column 0's bottom dot */
    assert(dmd_frame(NULL, 0, 0, f));
    assert(LIT(f, 49, 6) && LIT(f, 49, 31) && !LIT(f, 49, 20) && !LIT(f, 49, 5));
    put(g_cpu.mem + DS, 0x10 + 2 * 6, (uint16_t)(320 * 10 + 145 + 6 * 640));    /* back: it stays raised */
    assert(dmd_frame(NULL, 0, 0, f) && LIT(f, 49, 6) && !LIT(f, 49, 22));
    put(g_cpu.mem + DS, 4, 0);

    /* messages a cabinet does not show. A listed first line hides the whole
     * message: the board instead (here none: dark) */
    put(g_cpu.mem + DS, 0x10, (uint16_t)(320 * 10 + 145));
    for (int i = 0; i < 98; i++) put(g_cpu.mem + DS, (uint16_t)(0x10 + 2 * i), 0);
    at = 320 * 10 + 145; n = 0;
    for (int ch = 0; ch < 2; ch++, at += 2)
        for (int col = 0; col < 7; col++, at += 2)
            for (int row = 0; row < 7; row++) put(g_cpu.mem + DS, (uint16_t)(0x10 + 2 * n++), (uint16_t)(at + row * 640));
    for (int d = 0; d < 25; d++) put(g_cpu.mem + DS, (uint16_t)(0x10 + 2 * n++), (uint16_t)(320 * 30 + (d / 5) * 2 + (d % 5) * 640));
    put(g_cpu.mem + DS, (uint16_t)(0x10 + 2 * n), 0);
    put(g_cpu.mem + DS, 4, 1);
    g_cpu.mem[DS + 2] = 1;
    dmd_set_hidden(" AA | XYZ");                                     /* spaces round them do not count */
    c.bx = 0x500; c.ax = 0x0101; c.di = 320 * 10; dmd_enter(&c, "T_msg");
    c.bx = 0x510; c.di = 320 * 30; dmd_enter(&c, "T_app");
    dmd_frame(NULL, 0, 0, f);
    assert(!lit_in(f, 0, 0, DMD_W, DMD_H));
    /* a listed appended line alone goes; the rest is laid out without it */
    dmd_set_hidden("B");
    c.bx = 0x500; c.di = 320 * 10; dmd_enter(&c, "T_msg");
    c.bx = 0x510; c.di = 320 * 30; dmd_enter(&c, "T_app");
    assert(dmd_frame(NULL, 0, 0, f) && lit_in(f, 49, 9, 79, 23) == 2 * 49 * 4);   /* AA alone: doubled */
    /* the playfield, in a cabinet with no DMD: only the hidden line's dots get
     * back what was under them */
    const uint16_t b_dot = (uint16_t)(320 * 30), a_dot = (uint16_t)(320 * 10 + 145);
    put(g_cpu.mem + DS, 0x2000, (uint16_t)(b_dot >> 2)); put(g_cpu.mem + DS, 0x2002, (uint16_t)(0x4400 | 1 << (b_dot & 3)));
    put(g_cpu.mem + DS, 0x2004, (uint16_t)(a_dot >> 2)); put(g_cpu.mem + DS, 0x2006, (uint16_t)(0x5500 | 1 << (a_dot & 3)));
    put(g_cpu.mem + DS, 0x2008, 0);
    vga_vram_poke(b_dot, 0xFF);
    vga_vram_poke(a_dot, 0xFF);
    dmd_hide(DMD_HIDE_LISTED);
    assert(vga_vram_peek(b_dot) == 0x44 && vga_vram_peek(a_dot) == 0xFF);
    dmd_hide(0);
    assert(vga_vram_peek(b_dot) == 0xFF);
    dmd_hide(DMD_HIDE_ALL);
    assert(vga_vram_peek(b_dot) == 0x44 && vga_vram_peek(a_dot) == 0x55);
    dmd_hide(0);
    dmd_set_hidden("");                                              /* nothing hidden */
    dmd_hide(DMD_HIDE_LISTED);
    assert(vga_vram_peek(b_dot) == 0xFF);
    dmd_hide(0);
    put(g_cpu.mem + DS, 4, 0);
    dmd_table_end();

    /* tables 9-13: hidden dots painted out of a picture of the bar, in its background */
    char bar_map[] = "epicpinball-dmd 1\nimage ep9 1 0\nds 100\nmsg_fn T_msg\nstep_fn T_step\n"
                     "font large 200 0 8 7 7 16\nfont small 200 300 5 5 5 11\nappend T_app 200 300 5 5 5 11\n"
                     "list 10\neffect 2\ncount 4\nfamily bar\n";
    assert(dmd_map_use(bar_map, 0x1000, &why));
    dmd_set_hidden("B");
    c.bx = 0x500; c.ax = 0x0101; c.di = 320 * 2; dmd_enter(&c, "T_msg");
    c.bx = 0x510; c.di = 320 * 20; dmd_enter(&c, "T_app");
    n = 0; at = 320 * 2 + 145;
    for (int ch = 0; ch < 2; ch++, at += 2)
        for (int col = 0; col < 7; col++, at += 2)
            for (int row = 0; row < 7; row++) put(g_cpu.mem + DS, (uint16_t)(0x10 + 2 * n++), (uint16_t)(at + row * 640));
    for (int d = 0; d < 25; d++) put(g_cpu.mem + DS, (uint16_t)(0x10 + 2 * n++), (uint16_t)(320 * 20 + (d / 5) * 2 + (d % 5) * 640));
    put(g_cpu.mem + DS, (uint16_t)(0x10 + 2 * n), 0);
    put(g_cpu.mem + DS, 4, 1);
    static uint32_t bar[320 * 30];
    for (int i = 0; i < 320 * 30; i++) bar[i] = 0xFF101010u;
    bar[320 * 20] = bar[320 * 2 + 145] = 0xFFFF0000u;                /* a dot of B, a dot of AA */
    dmd_bar_hide(bar, 320, 30);
    assert(bar[320 * 20] == 0xFF101010u && bar[320 * 2 + 145] == 0xFFFF0000u);
    put(g_cpu.mem + DS, 4, 0);
    dmd_table_end();
    dmd_set_hidden("USE UP/DOWN ARROW KEYS TO|USE SHIFT KEYS TO CONTROL|PRESS ENTER TO REMOVE|PRESS SPACE TO BEGIN|PRESS ENTER");
    pit_at = 0;
}

/* --ssf: where a board effect is heard (vpinball's linear placing), its
 * voice in the game's driver set silent while it plays here, stopped when
 * the driver's is, and a rumble for a rolling ball (src/ssf.c) */
static double ch_energy(const int16_t *o, int frames, int c)
{
    double e = 0;
    for (int i = 0; i < frames; i++) e += (double)o[i * 4 + c] * o[i * 4 + c];
    return e;
}

static void test_ssf(void)
{
    float g[4];
    ssf_gains(0, 0, g);                                  /* top left: the side left exciter */
    assert(g[2] == 1 && g[0] == 0 && g[1] == 0 && g[3] == 0);
    ssf_gains(320, 400, g);                              /* bottom right: the rear right */
    assert(g[1] == 1 && g[0] == 0 && g[2] == 0 && g[3] == 0);
    ssf_gains(160, 200, g);                              /* the middle: a quarter each */
    for (int i = 0; i < 4; i++) assert(g[i] == 0.25f);

    static char text[sizeof k_map + 256];
    snprintf(text, sizeof text, "%.*s%s", (int)(sizeof k_map - 5), k_map,
             "effect_fn ep1_fx\nsfx_setup_fn sb_setup\nsfx_volume_fn sb_vol\nsfx_stop_fn sb_stop\n"
             "sfx_stop_class_fn sb_class\nboard 0 4 11\nend\n");
    static const FbSink sink = { seen_table, seen_event, seen_off };
    const char *why = "";
    WatchMap m;
    assert(watch_map_parse(text, &m, &why));
    assert(!strcmp(m.effect_fn, "ep1_fx") && !strcmp(m.sfx_stop_class_fn, "sb_class"));
    assert(m.board == (1u << 0 | 1u << 4 | 1u << 0x11));
    feedback_sink(&sink);
    g_cpu.ss = 0x9000; g_cpu.sp = 0x0100;
    assert(watch_map_use(&m, 0x1000));
    uint8_t *ds = g_cpu.mem + g_watch_ds;
    for (int i = 0; i < 5; i++) put(ds, (uint16_t)(m.active + 2 * i), 0);   /* no ball yet */
    ssf_wanted();
    ssf_table_begin();
    assert(g_ssf_on);

    /* a sample: 100 ms of a square wave, at 44.1 kHz, volume 100; the
     * driver's master volume for effects at full */
    const uint32_t H = 0x60000, D = 0x61000;
    memset(g_cpu.mem + H, 0, 0x60);
    g_cpu.mem[H + 0x2D] = (uint8_t)D; g_cpu.mem[H + 0x2E] = (uint8_t)(D >> 8); g_cpu.mem[H + 0x2F] = (uint8_t)(D >> 16);
    put(g_cpu.mem + H, 0x36, 4410);
    put(g_cpu.mem + H, 0x45, 44100 & 0xFFFF); g_cpu.mem[H + 0x47] = 0;
    g_cpu.mem[H + 0x44] = 100;
    for (int i = 0; i < 4410; i++) g_cpu.mem[D + i] = (uint8_t)(i / 20 % 2 ? 0x80 + 100 : 0x80 - 100);
    g_cpu.mem[0x70000 + 0x2C4] = 0xFF;

    /* the left flipper: a fixed pan at the left, at the bottom */
    CPU c = g_cpu;
    c.al = 4; c.ah = 0x10; ssf_enter(&c, "ep1_fx");
    c.dl = 1; c.ds = 0x7000; c.ecx = 0x6000u << 16; ssf_enter(&c, "sb_setup");
    g_cpu.mem[H + 0x33] = 3;                             /* the driver gave it voice 3 */
    c.al = 3; c.cl = 100; ssf_enter(&c, "sb_vol");
    assert(c.cl == 0);                                   /* silent in the game's mix */
    static int16_t o[441 * 4];
    ssf_render(o, 441);
    const double bl = ch_energy(o, 441, 0), br = ch_energy(o, 441, 1), sl = ch_energy(o, 441, 2), sr = ch_energy(o, 441, 3);
    assert(bl > 0 && bl > 100 * sr && bl > 10 * br && bl > 10 * sl);
    /* the driver stops its voice: ours stops */
    c.al = 3; ssf_enter(&c, "sb_stop");
    ssf_render(o, 441);
    assert(ch_energy(o, 441, 0) == 0);

    /* not the board's (slot 2): left to the game, volume untouched */
    c.al = 2; c.ah = 0; ssf_enter(&c, "ep1_fx");
    c.dl = 1; c.ecx = 0x6000u << 16; ssf_enter(&c, "sb_setup");
    g_cpu.mem[H + 0x33] = 5;
    c.al = 5; c.cl = 100; ssf_enter(&c, "sb_vol");
    assert(c.cl == 100);
    ssf_render(o, 441);
    assert(ch_energy(o, 441, 0) == 0 && ch_energy(o, 441, 3) == 0);
    /* music (class 2) never is, even straight after a board effect's call */
    c.al = 0; ssf_enter(&c, "ep1_fx");
    c.dl = 2; c.ecx = 0x6000u << 16; ssf_enter(&c, "sb_setup");
    g_cpu.mem[H + 0x33] = 6;
    c.al = 6; c.cl = 100; ssf_enter(&c, "sb_vol");
    assert(c.cl == 100);

    /* a board effect, then every effect stopped at once (class 1) */
    c.al = 0; c.ah = 0; ssf_enter(&c, "ep1_fx");
    c.dl = 1; c.ecx = 0x6000u << 16; ssf_enter(&c, "sb_setup");    /* CL is ECX's low byte */
    g_cpu.mem[H + 0x33] = 7;
    c.al = 7; c.cl = 100; ssf_enter(&c, "sb_vol");
    assert(c.cl == 0);
    c.ah = 1; ssf_enter(&c, "sb_class");
    ssf_render(o, 441);
    for (int k = 0; k < 4; k++) assert(ch_energy(o, 441, k) == 0);

    /* a rolling ball at the top right, the physics stepping: a rumble there;
     * still, none; and none while the game waits (a prompt: no steps) */
    put(ds, m.active, 1); put(ds, m.x, 300); put(ds, m.y, 10); put(ds, m.vx, 300); put(ds, m.vy, 0);
    watch_enter(&g_cpu, "ep1_step");
    ssf_render(o, 441);
    assert(ch_energy(o, 441, 3) > 0 && ch_energy(o, 441, 3) > 20 * ch_energy(o, 441, 0));
    put(ds, m.vx, 0);
    for (int i = 0; i < 40; i++) ssf_render(o, 441);
    assert(ch_energy(o, 441, 3) == 0);
    put(ds, m.vx, 300);
    pit_at += (uint64_t)(0.2 * PIT_HZ);                  /* 200 ms without a step */
    for (int i = 0; i < 40; i++) ssf_render(o, 441);
    assert(ch_energy(o, 441, 3) == 0);
    watch_enter(&g_cpu, "ep1_step");
    put(ds, m.vx, 0);
    ssf_volumes(1, 0);                                   /* rolling off */
    put(ds, m.vx, 300);
    ssf_render(o, 441);
    assert(ch_energy(o, 441, 3) == 0);
    ssf_volumes(1, 1);
    put(ds, m.active, 0);
    ssf_table_end();
    assert(!g_ssf_on);
    watch_table_end();
}

#ifndef _WIN32
static void pump(int ms)
{
    for (uint64_t end = host_us() + (uint64_t)ms * 1000u; host_us() < end; ) { dof_tick(); usleep(1000); }
}

static void test_dof_pipe(void)
{
    /* the pipe protocol, with a shell script for the helper */
    char dir[256], path[320], got[65536];
    const char *t = getenv("TMPDIR");
    snprintf(dir, sizeof dir, "%s/epicpinball-selftest.XXXXXX", t && *t ? t : "/tmp");
    assert(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/helper", dir);
    FILE *f = fopen(path, "w");
    assert(f);
    fprintf(f, "#!/bin/sh\necho \"run $*\" >> '%s/got'\n"
               "case \"$*\" in *--all-off*) exit 0;; *ep3[!0-9]*) exit 2;; *ep5[!0-9]*) exit 5;; esac\necho ready\n"
               "while read -r op a b; do echo \"$op $a $b\" >> '%s/got'\n"
               "case \"$op $a\" in q*) exit 0;; 'e tilt') exit 3;; esac; done\n",
            dir, dir);
    fclose(f);
    assert(chmod(path, 0700) == 0);

    real_clock = 1;
    assert(dof_open(path, dir));
    feedback_table(0, "ep1", "LR", "hole1=TestHole:L kickback1=Kickback:L");
    pump(300);
    feedback_event(1, FB_FLIPPER_LEFT, 1, (FbDetail){ 0 });
    pump(1200);                                           /* held, and nothing said: no fault, and no off */
    feedback_event(1, FB_DRAIN, 1, (FbDetail){ 0 });
    feedback_mute(1);                                     /* the demo: never sent */
    feedback_event(2, FB_KNOCKER, 1, (FbDetail){ 0 });
    feedback_mute(0);
    feedback_table(3, NULL, NULL, NULL);                              /* the table's end: q */
    pump(300);
    feedback_table(4, "ep2", "LCR", "");
    pump(300);
    feedback_event(5, FB_TILT, 1, (FbDetail){ 0 });       /* it dies: all off, and none till the next table */
    pump(500);
    feedback_table(6, "ep3", "LCR", "");                             /* no row for this table: only this one goes without */
    pump(300);
    feedback_table(7, "ep4", "LCR", "");
    pump(300);
    feedback_table(8, "ep5", "LCR", "");                             /* no row at all: none for the rest of the run */
    pump(300);
    feedback_table(9, "ep6", "LCR", "");
    pump(300);
    dof_close();
    real_clock = 0;

    char log[320];
    snprintf(log, sizeof log, "%s/got", dir);
    f = fopen(log, "r");
    assert(f);
    size_t n = fread(got, 1, sizeof got - 1, f);
    fclose(f);
    got[n] = 0;
    assert(strstr(got, "--table ep1 --bumpers LR --features hole1=TestHole:L kickback1=Kickback:L") && strstr(got, "--save")
           && !strstr(got, "knocker"));
    assert(strstr(got, "--table ep2 --bumpers LCR\n"));           /* no features: no --features */
    const char *up = strstr(got, "e flipper_left 1"), *drain = strstr(got, "e drain 1");
    assert(up && drain && up < drain);
    const char *off = strstr(drain, "o  \n");             /* the mute's, after the drain */
    assert(off && !strstr(got, "\nh "));                  /* and no heartbeat */
    for (const char *p = up; (p = strchr(p, '\n')) != NULL && ++p < drain; )
        assert(strncmp(p, "o ", 2) && strncmp(p, "e flipper_left 0", 16));
    const char *q = strstr(got, "q  \n"), *two = strstr(got, "--table ep2"), *all = strstr(got, "--all-off");
    assert(q && two && all && q < two && two < all);      /* one a table, the next after the last */
    assert(!strstr(all + 1, "--all-off"));                /* a table without a row switched nothing on */
    assert(strstr(got, "--table ep4") && strstr(got, "--table ep5") && !strstr(got, "--table ep6"));

    remove(log);
    remove(path);
    assert(rmdir(dir) == 0);                              /* the game writes no configuration */
}
#endif

/* --table (src/autostart.c): the menus map; the list's place of a table among
 * those installed; and a drive through fake PINBALL memory, a key a frame:
 * the intro, the main menu, the list to table 3, its screen, the EXEC (one
 * player), its end, out, and initials handed to the player */
static int sent(int sc) { for (int i = 0; i < nkeys_sent; i++) if (keys_sent[i] == sc) return 1; return 0; }

static void test_autostart(void)
{
    static const char map[] =
        "epicpinball-menus 2\nimage pin 37461 E8DF0C9E\ncursor 6C5E\nend_fn pin_E\nentry 1D67\nexec_fn pin_X\n"
        "frame_fn pin_F\ninitials 1D65\nintro_fn pin_I\nkey 3B4B\nletters 1D6B\nmenu_frames 2E\nmenus_fn pin_M\n"
        "present 6969\nscores 6581\nscreen_cursor 6C61\nscreen_fn pin_S\nscroll 1B8A\nselected 6C5C\n"
        "sub_fn pin_O\nsub_fn pin_R\nend\n";
    MenusMap m;
    const char *why = "";
    assert(autostart_map_parse(map, &m, &why));
    assert(m.size == 37461 && m.fnv == 0xE8DF0C9Eu && m.cursor == 0x6C5E && m.nsub == 2);
    assert(!strcmp(m.frame_fn, "pin_F") && !strcmp(m.sub_fn[1], "pin_R"));
    char bad[sizeof map];
    strcpy(bad, map);
    *strstr(bad, "initials") = 'X';                                   /* a field missing */
    assert(!autostart_map_parse(bad, &m, &why));
    strcpy(bad, map);
    bad[18] = '1';                                                    /* another version */
    assert(!autostart_map_parse(bad, &m, &why));
    {                                                                 /* written on Windows: CRLF */
        char crlf[2 * sizeof map], *o = crlf;
        for (const char *p = map; *p; p++) { if (*p == '\n') *o++ = '\r'; *o++ = *p; }
        *o = 0;
        assert(autostart_map_parse(crlf, &m, &why) && m.nsub == 2 && !strcmp(m.sub_fn[1], "pin_R"));
    }
    assert(autostart_map_parse(map, &m, &why));

    uint8_t present[26] = { 0 };
    for (int i = 0; i < 13; i++) present[2 * i] = 1;
    assert(autostart_list_position(present, 1) == 0 && autostart_list_position(present, 13) == 12);
    present[2] = 0;                                                   /* table 2 not installed */
    assert(autostart_list_position(present, 2) == -1 && autostart_list_position(present, 3) == 1);
    present[2] = 1;

    const uint32_t cs = 0x2000 * 16;
    uint8_t *M = g_cpu.mem + cs;
    memset(M, 0, 0x10000);
    memcpy(M + 0x6969, present, sizeof present);
    const int watching = g_watch_on;
    g_watch_on = 0;                                                   /* no feedback map: shown at its start */
    pit_at = 0;
    nkeys_sent = 0;
    autostart_wanted(3);
    autostart_begin_map(&m, 0x2000);
    assert(g_auto_on && fast_now && autostart_busy() == 1);
    CPU c = { 0 };
    #define FRAMES(fn, n) for (int f_ = 0; f_ < (n); f_++) { autostart_enter(&c, fn); \
                                                         pit_at += (uint64_t)(PIT_HZ / 70); autostart_poll(); }
    FRAMES("pin_I", 8);                                                /* the intro: Enter, let go */
    assert(sent(0x1C) && sent(0x9C));
    nkeys_sent = 0;
    M[0x6C5E] = 2;                                                    /* the main menu, cursor not on BEGIN */
    FRAMES("pin_F", 10);
    assert(sent(0xE0) && sent(0x48) && !sent(0x1C));                  /* Up */
    M[0x6C5E] = 0;
    nkeys_sent = 0;
    FRAMES("pin_F", 10);
    assert(sent(0x1C));                                               /* Enter on BEGIN */
    autostart_enter(&c, "pin_M");                                     /* the list */
    nkeys_sent = 0;
    for (int i = 0; i < 40 && M[0x6C5E] + M[0x1B8A] < 2; i++) {       /* Down to table 3's place */
        int n = nkeys_sent;
        FRAMES("pin_F", 1);
        if (nkeys_sent > n && keys_sent[nkeys_sent - 1] == 0x50) M[0x6C5E]++;
    }
    assert(M[0x6C5E] == 2);
    nkeys_sent = 0;
    FRAMES("pin_F", 10);
    assert(sent(0x1C) && !sent(0x50));                                /* Enter on it */
    M[0x6C5C] = 2;
    autostart_enter(&c, "pin_S");                                     /* its screen */
    nkeys_sent = 0;
    FRAMES("pin_F", 10);
    assert(sent(0x1C));                                               /* PLAY GAME */
    autostart_enter(&c, "pin_X");
    uint8_t tail[16] = { 3, ' ', '2', '<', 0x0D };
    autostart_exec("C:\\EP3.EXE", tail);
    assert(tail[2] == '1');                                           /* one player */
    autostart_enter(&c, "ep3_step");                                  /* the table runs: the player's */
    assert(!fast_now && autostart_busy() == 0);
    autostart_exec_done();                                            /* quit (no map: as if) */
    assert(fast_now && autostart_busy() == 2);
    nkeys_sent = 0;
    FRAMES("pin_F", 10);                                              /* its screen: Esc, out */
    assert(sent(0x01) && autostart_busy() == 2);
    M[0x1D65] = 1;                                                    /* a high score: initials */
    FRAMES("pin_F", 2);
    assert(!fast_now && autostart_busy() == 0);
    nkeys_sent = 0;
    FRAMES("pin_F", 30);
    assert(nkeys_sent == 0);                                          /* none pressed while typed */
    M[0x1D65] = 0;
    FRAMES("pin_F", 10);
    assert(fast_now && autostart_busy() == 2 && sent(0x01));          /* then out again */

    /* Between games in a cabinet: the game over holds the playfield; PINBALL
     * fades in unseen and asks initials, chosen with the flipper keys and a
     * typed letter, typed to it; hiscore.pin written; GAME OVER; Start plays
     * table 3 again */
    autostart_arcade(1);
    const uint16_t seg = 0x6000;
    uint8_t *A = g_cpu.mem + seg * 16;
    memset(A, 0, 0x10000);
    memcpy(A + 0x6969, present, sizeof present);
    memcpy(A + 0x1D6B, " 1234567890-=  QWERTYUIOP[]  ASDFGHJKL;\"~ \\ZXCVBNM,./    ", 57);   /* a letter per scan code - 1 */
    for (int i = 0; i < 125; i++) memcpy(A + 0x6581 + 8 * i, "JDS \x10\x27\0\0", 8);       /* 25 tables of 5 */
    char saves[256] = "", hs[300] = "";
#ifndef _WIN32
    const char *tmp = getenv("TMPDIR");
    snprintf(saves, sizeof saves, "%s/epicpinball-selftest.XXXXXX", tmp && *tmp ? tmp : "/tmp");
    assert(mkdtemp(saves));
    snprintf(hs, sizeof hs, "%s/hiscore.pin", saves);
    FILE *hf = fopen(hs, "wb");
    assert(hf && fwrite(A + 0x6581, 1, 1000, hf) == 1000 && fclose(hf) == 0);   /* as PINBALL read it */
    g_opt.savedir = saves;
#endif
    autostart_begin_map(&m, seg);
    FRAMES("pin_I", 8);
    FRAMES("pin_F", 10);                                              /* BEGIN */
    autostart_enter(&c, "pin_M");
    for (int i = 0; i < 40 && A[0x6C5E] < 2; i++) {
        int n = nkeys_sent;
        FRAMES("pin_F", 1);
        if (nkeys_sent > n && keys_sent[nkeys_sent - 1] == 0x50) A[0x6C5E]++;
    }
    FRAMES("pin_F", 10);
    A[0x6C5C] = 2;
    autostart_enter(&c, "pin_S");
    FRAMES("pin_F", 10);
    autostart_enter(&c, "pin_X");
    tail[2] = '1';
    autostart_exec("C:\\EP3.EXE", tail);
    autostart_enter(&c, "ep3_step");
    assert(!fast_now && autostart_busy() == 0 && !autostart_hold());
    WatchMap w;
    assert(watch_map_parse(k_map, &w, &why) && watch_map_use(&w, 0x1000));
    g_cpu.mem[g_watch_ds + w.over] = 1;                               /* game over */
    assert(autostart_hold() && !autostart_between());                 /* the playfield held, the DMD the table's */
    autostart_exec_done();
    watch_table_end();
    g_watch_on = 0;
    uint32_t dmd[DMD_W * DMD_H];
    assert(autostart_between() && autostart_hold() && fast_now && autostart_busy() == 2 && autostart_dmd(dmd) >= 0);
    FRAMES("pin_I", 20);                                              /* PINBALL fades in */
    assert(autostart_key(0x36) == 0);                                 /* nothing asked yet */
    A[0x1D65] = 1;                                                    /* a high score: second of table 3 */
    A[0x1D67] = 2 * 40 + 8;
    memcpy(A + 0x6581 + 2 * 40 + 8, "???\x20\x48\xDC\x0B\0", 8);    /* 777,000 */
    autostart_enter(&c, "pin_S");
    FRAMES("pin_F", 1);
    assert(!fast_now && autostart_busy() == 0);                       /* the player's, on the DMD */
    assert(autostart_key(0x36) && autostart_key(0x36) && autostart_key(0x1C));   /* right flipper twice: C, Start */
    assert(autostart_key(0x10));                                      /* Q typed */
    assert(autostart_key(0x2A) && autostart_key(0x39));               /* left flipper: P, the launch button */
    assert(fast_now && autostart_busy() == 2);
    nkeys_sent = 0;
    FRAMES("pin_F", 40);
    assert(sent(0x2E) && sent(0x10) && sent(0x19) && sent(0x1C) && !sent(0x36));   /* C Q P Enter */
    memcpy(A + 0x6581 + 2 * 40 + 8, "CQP", 3);                       /* PINBALL took them */
    A[0x1D65] = 0;
    FRAMES("pin_F", 80);
    assert(!fast_now && autostart_busy() == 0 && autostart_between());   /* GAME OVER, PRESS START */
#ifndef _WIN32
    char back[1000];
    hf = fopen(hs, "rb");
    assert(hf && fread(back, 1, 1000, hf) == 1000);
    fclose(hf);
    assert(!memcmp(back, A + 0x6581, 1000) && !memcmp(back + 2 * 40 + 8, "CQP", 3));   /* written between games */
    remove(hs);
    rmdir(saves);
    g_opt.savedir = NULL;
#endif
    nkeys_sent = 0;
    FRAMES("pin_F", 10);
    assert(nkeys_sent == 0);                                          /* waits for Start */
    assert(autostart_key(0x1C));
    FRAMES("pin_F", 10);
    assert(fast_now && sent(0x1C));                                   /* PLAY GAME, unseen */
    autostart_enter(&c, "pin_X");
    tail[2] = '1';
    autostart_exec("C:\\EP3.EXE", tail);
    autostart_enter(&c, "ep3_step");
    assert(!fast_now && !autostart_between() && !autostart_hold());    /* the second game */
    autostart_arcade(0);
    #undef FRAMES
    g_watch_on = watching;
    g_auto_on = 0;
    pit_at = 0;
}

/* arcade.c: the initials chosen with flippers, Start, typed letters and the
 * back arrow; the pictures; numbers with commas */
static void test_arcade(void)
{
    char n[16];
    arcade_commas(4470000, n);
    assert(!strcmp(n, "4,470,000"));
    arcade_commas(999, n);
    assert(!strcmp(n, "999"));
    arcade_entry(2, 777000, "ABC ");
    assert(!arcade_entry_input(ARC_NEXT, 0) && !arcade_entry_input(ARC_TAKE, 0));          /* B */
    assert(!strcmp(arcade_initials(), "B"));
    for (int i = 0; i < 3; i++) arcade_entry_input(ARC_PREV, 0);      /* A, the back arrow, space */
    arcade_entry_input(ARC_NEXT, 0);                                  /* the back arrow */
    assert(!arcade_entry_input(ARC_TAKE, 0) && !strcmp(arcade_initials(), ""));
    assert(!arcade_entry_input(0, 'z') && !strcmp(arcade_initials(), ""));   /* not offered */
    assert(!arcade_entry_input(0, 'c') && !strcmp(arcade_initials(), "C"));
    assert(!arcade_entry_input(ARC_TAKE, 0) && !strcmp(arcade_initials(), "CC"));
    assert(!arcade_entry_input(ARC_BACK, 0) && !strcmp(arcade_initials(), "C"));
    assert(arcade_entry_input(ARC_DONE, 0) && !strcmp(arcade_initials(), "C"));
    arcade_entry_input(ARC_TAKE, 0);
    assert(arcade_entry_input(ARC_TAKE, 0) && !strcmp(arcade_initials(), "CCC"));         /* three: done */
    static uint32_t a[DMD_W * DMD_H], b[DMD_W * DMD_H];
    arcade_draw_entry(a, 0);
    int lit = 0;
    for (int i = 0; i < DMD_W * DMD_H; i++) lit += a[i] != 0xFF000000u;
    assert(lit > 100);
    const ArcadeScore top[3] = { { "CQP", 777000 }, { "ROB", 500 }, { "JDS", 1 } };
    arcade_draw_ready(a, 0, top);                                     /* GAME OVER */
    arcade_draw_ready(b, 3000, top);                                  /* the high scores */
    assert(memcmp(a, b, sizeof a));
    assert(arcade_text_w("ABC", 1) == 17 && arcade_text_w("ABC", 2) == 34);
}

/* An accelerometer's shove (src/nudge.c): the push against the shove, the
 * dead zone, small pushes adding up, the game's gate, nothing tilted or in
 * the demo, and the tilt meter filling by how hard */
/* ini.c: a key set in place keeps the file's comments, other keys, line ends
 * and the comment after the value; a missing key joins its section, a
 * missing section the end; NULL removes; VPinball's values keep their ';' */
static char *slurp(const char *path)
{
    static char buf[4096];
    FILE *f = fopen(path, "rb");
    size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
    if (f) fclose(f);
    buf[n] = 0;
    return buf;
}

static void test_ini(void)
{
    const char *p = "selftest-ini.tmp";
    FILE *f = fopen(p, "wb");
    fputs("\xEF\xBB\xBF; top comment\r\n[plunger]\r\ndevice = none                ; its name\r\naxis = none\r\n\r\n"
          "[nudge]\r\nDead=1500\r\n", f);
    fclose(f);
    char v[64];
    assert(!strcmp(ini_get(p, "Plunger", "DEVICE", "x", v, sizeof v), "none"));
    assert(ini_set(p, "plunger", "device", "Pinscape Controller"));
    assert(ini_set(p, "plunger", "rest", "120"));
    assert(ini_set(p, "nudge", "dead", "900"));
    assert(ini_set(p, "modes", "cabinet", "1"));
    assert(!ini_set(p, "modes", "bad", "a;b"));
    assert(!strcmp(slurp(p), "\xEF\xBB\xBF; top comment\r\n[plunger]\r\ndevice = Pinscape Controller ; its name\r\naxis = none\r\n"
                             "rest = 120\r\n\r\n[nudge]\r\nDead = 900\r\n\r\n[modes]\r\ncabinet = 1\r\n"));
    assert(!strcmp(ini_get(p, "plunger", "device", "x", v, sizeof v), "Pinscape Controller"));
    assert(!strcmp(ini_get(p, "modes", "cabinet", "0", v, sizeof v), "1"));
    assert(ini_set(p, "plunger", "rest", NULL));
    assert(!strstr(slurp(p), "rest"));
    remove(p);

    f = fopen(p, "wb");                                   /* a new file; VPinball's ';' in values */
    fclose(f);
    remove(p);
    assert(ini_set(p, "keys", "left_flipper", "LCTRL | A"));
    assert(!strcmp(slurp(p), "[keys]\nleft_flipper = LCTRL | A\n"));
    f = fopen(p, "wb");
    fputs("[Input]\n; Devices: a comment\nMapping.LeftFlipper = Key;225 | SDLJoy_03000000_1;6\n", f);
    fclose(f);
    assert(!strcmp(ini_get_raw(p, "input", "mapping.leftflipper", "", v, sizeof v), "Key;225 | SDLJoy_03000000_1;6"));
    assert(!strcmp(ini_get(p, "input", "mapping.leftflipper", "", v, sizeof v), "Key"));
    remove(p);
}

/* keys.c: names both ways, SDL's and DirectInput's codes, actions, lists */
static void test_keys(void)
{
    assert(key_by_name("lshift") == 0x2A && key_by_name("UP") == (KEY_E0 | 0x48) && key_by_name("RCTRL") == (KEY_E0 | 0x1D));
    assert(key_by_name("CTRL") == 0x1D && key_by_name("1D") == 0x1D && key_by_name("1") == 0x02 && !key_by_name("nope"));
    for (int sc = 1; sc < 0x200; sc++) {                  /* every named key reads back as itself */
        const char *n = key_name(sc);
        assert(key_by_name(n) == sc);
    }
    assert(!strcmp(key_name(0x2A), "LSHIFT") && !strcmp(key_name(KEY_E0 | 0x1C), "KP_ENTER"));
    assert(key_from_sdl(4) == 0x1E && key_from_sdl(225) == 0x2A && key_from_sdl(229) == 0x36);      /* A, LShift, RShift */
    assert(key_from_sdl(40) == 0x1C && key_from_sdl(88) == (KEY_E0 | 0x1C) && key_from_sdl(82) == (KEY_E0 | 0x48));
    assert(key_from_sdl(228) == (KEY_E0 | 0x1D) && key_from_sdl(44) == 0x39 && key_from_sdl(56) == 0x35 && !key_from_sdl(72));
    assert(key_from_dik(0x2A) == 0x2A && key_from_dik(0x9D) == (KEY_E0 | 0x1D) && key_from_dik(0xC8) == (KEY_E0 | 0x48));
    assert(key_action_by_name("Left_Flipper") == 0 && k_actions[key_action_by_name("launch")].sc == 0x39);
    assert(key_action_by_name("tilt") < 0);
    int k[4];
    assert(key_list("LCTRL | a", k, 4) == 2 && k[0] == 0x1D && k[1] == 0x1E);
    assert(key_list("none", k, 4) == 0 && key_list("", k, 4) == 0 && key_list("LCTRL | bogus", k, 4) == -1);
}

/* hud.c: the nudge indicator's ring at the top right, its dot where the reading is */
static void test_hud(void)
{
    static uint32_t f[320 * 240];
    for (int i = 0; i < 320 * 240; i++) f[i] = 0xFF000000u;
    hud_nudge(f, 320, 240, 1.0, 0, 0, 1500, 32767);
    const int cx = 320 - 4 - 12, cy = 4 + 12;                         /* the ring's middle */
    #define AT(x, y) f[(y) * 320 + (x)]
    assert((AT(cx, cy) & 0xFF0000) > 0xC00000);                       /* the dot, at rest in the middle */
    assert((AT(cx + 12, cy) & 0xFF) > 0x80 && (AT(cx, cy - 12) & 0xFF) > 0x80);   /* the ring */
    assert(AT(10, 200) == 0xFF000000u && AT(cx - 6, cy + 6) == 0xFF000000u);      /* nothing else */
    for (int i = 0; i < 8; i++) hud_nudge(f, 320, 240, 1.0, 32767, 0, 1500, 32767);   /* the cabinet moved right */
    assert((AT(cx + 11, cy) & 0xFF0000) > 0xC00000);
    for (int i = 0; i < 320 * 240; i++) f[i] = 0xFF000000u;
    for (int i = 0; i < 8; i++) hud_nudge(f, 320, 240, 1.0, -4000, 0, 1500, 8000);   /* half its range: half way */
    assert((AT(cx - 6, cy) & 0xFF0000) > 0xC00000 && (AT(cx - 11, cy) & 0xFF0000) < 0x800000);
    for (int i = 0; i < 8; i++) hud_nudge(f, 320, 240, 1.0, 0, -9000, 1500, 8000);    /* past it: on the ring */
    assert((AT(cx, cy - 11) & 0xFF0000) > 0xC00000);
    for (int i = 0; i < 320 * 240; i++) f[i] = 0xFF000000u;
    for (int i = 0; i < 8; i++) hud_nudge(f, 320, 200, 1.2, 0, 0, 0, 32767);       /* tall pixels: as round on screen */
    assert((AT(cx, 4 + 14) & 0xFF0000) > 0xC00000 && (AT(cx, 4 + 14 - 14) & 0xFF) > 0x80);
    #undef AT
}

/* vpx_import.c: VPinball's settings in each format it has written */
static void vpx_file(const char *p, const char *text)
{
    FILE *f = fopen(p, "wb");
    fputs(text, f);
    fclose(f);
}

static int has(const int *list, int n, int v)
{
    for (int i = 0; i < n; i++) if (list[i] == v) return 1;
    return 0;
}

static void test_vpx_import(void)
{
    const char *p = "selftest-vpx.tmp";
    VpxImport o;
    const int LF = key_action_by_name("left_flipper"), RF = key_action_by_name("right_flipper"),
              LA = key_action_by_name("launch"), ST = key_action_by_name("start"), EX = key_action_by_name("exit"),
              NL = key_action_by_name("nudge_left");

    /* 10.8.1: SDL scancodes, a joystick's buttons from 0, sensors; empty is VPinball's default */
    vpx_file(p, "[Version]\nVPinball = 10815362\n[Player]\n; Playfield Output Mode\nSound3D = 5\nSoundVolume = 80\n"
                "NudgeOrientation0 = 180\n"
                "[Input]\nDevices = Key;SDLJoy_0300abcdfafa0000f700000001000000_1\n"
                "Mapping.LeftFlipper = Key;225 | SDLJoy_0300abcdfafa0000f700000001000000_1;3 | Key;224\n"
                "Mapping.RightFlipper = SDLJoy_0300abcdfafa0000f700000001000000_1;4\n"
                "Mapping.LaunchBall = \n"
                "Mapping.Start = Key;30 & Key;226 | SDLJoy_0300abcdfafa0000f700000001000000_1;517;o;0.500000\n"
                "Mapping.Plunger0.Position = SDLJoy_0300abcdfafa0000f700000001000000_1;514;P;0.000000;-1.000000;1.000000\n"
                "Mapping.Nudge0.VelX = SDLJoy_0300abcdfafa0000f700000001000000_1;512;V;0.050000;1.000000;1.000000\n"
                "Mapping.Nudge0.VelY = SDLJoy_0300abcdfafa0000f700000001000000_1;513;V;0.000000;-1.000000;1.000000\n");
    assert(vpx_read(p, &o) && o.format == 2);
    assert(o.nkey[LF] == 1 && o.key[LF][0] == 0x1D);                       /* LShift is its own: only LCtrl added */
    assert(o.nbutton[LF] == 1 && o.button[LF][0] == 3 && o.button[RF][0] == 4);
    assert(!strcmp(o.button_guid, "0300abcdfafa0000f700000001000000"));
    assert(o.nkey[RF] == 0);                                                /* no key, only the button */
    assert(o.nkey[LA] == 1 && o.key[LA][0] == 0x1C);                        /* empty: Return, VPinball's */
    assert(o.nkey[ST] == 0 && o.nbutton[ST] == 0 && strstr(o.notes, "held together") && strstr(o.notes, "axis used as a button"));
    assert(o.nkey[EX] == 0);                                                /* Esc, its own */
    assert(o.plunger_axis == 2 && o.plunger_reversed && o.plunger_speed_axis == -1);
    assert(o.nudge_x == 0 && o.nudge_y == 1 && o.is_velocity && o.dead == 1638);
    assert(o.invert_x && !o.invert_y);                                      /* Y's -1 and the half turn cancel */
    assert(o.ssf && o.sound_volume == 80);

    /* the nightlies' NudgeX1 as acceleration, turned a quarter */
    vpx_file(p, "[Player]\nNudgeOrientation0 = 90\n[Input]\nDevices = Key\n"
                "Mapping.NudgeX1 = SDLJoy_1209eaea_1;512;A;0;1;1\nMapping.NudgeY1 = SDLJoy_1209eaea_1;513;A;0;1;1\n");
    assert(vpx_read(p, &o) && o.format == 2 && !o.is_velocity);
    assert(o.nudge_x == 1 && o.nudge_y == 0 && !o.invert_x && o.invert_y);   /* x' = y, y' = -x */
    assert(!strcmp(o.nudge_guid, "1209eaea") && !o.ssf);

    /* a cabinet's: Nudge0 as a game controller's stick (P), the older NudgeX1/Y1 still saying velocity */
    vpx_file(p, "[Input]\nDevices = Key\nMapping.NudgeX1 = SDLJoy_E6614C311B560622.0B.01;512;V;0.099536;0.107216;1.000000\n"
                "Mapping.NudgeY1 = SDLJoy_E6614C311B560622.0B.01;513;V;0.100641;0.107424;1.000000\n"
                "Mapping.Nudge0.Type = 0\n"
                "Mapping.Nudge0.X = SDLJoy_0300252c09120000ebea000011010000_1;512;P;0.000000;1.000000;1.000000\n"
                "Mapping.Nudge0.Y = SDLJoy_0300252c09120000ebea000011010000_1;513;P;0.000000;0.440534;1.000000\n");
    assert(vpx_read(p, &o) && o.nudge_x == 0 && o.nudge_y == 1 && o.is_velocity && o.dead == 3261);
    assert(!strcmp(o.nudge_guid, "0300252c09120000ebea000011010000") && strstr(o.notes, "said velocity"));
    vpx_file(p, "[Input]\nDevices = Key\n"                              /* no older line: a stick, as acceleration */
                "Mapping.Nudge0.X = SDLJoy_0300252c09120000ebea000011010000_1;512;P;0.000000;1.000000;1.000000\n");
    assert(vpx_read(p, &o) && !o.is_velocity && strstr(o.notes, "as a position"));

    /* 10.8.0: DirectInput codes, buttons from 1, axes from 1 */
    vpx_file(p, "[Player]\nLFlipKey = 29\nRFlipKey = 157\nPlungerKey = \nStartGameKey = 2\nExitGameKey = 16\n"
                "JoyLFlipKey = 1\nJoyRFlipKey = 2\nJoyPlungerKey = 26\nLTiltKey = 44\n"
                "PlungerAxis = 3\nReversePlungerAxis = 0\nPlungerSpeedAxis = 0\nLRAxis = 1\nUDAxis = 2\nLRAxisFlip = 1\n"
                "DeadZone = 10\nAccelVelocityInput = 1\nSound3D = 1\n");
    assert(vpx_read(p, &o) && o.format == 1);
    assert(o.nkey[LF] == 1 && o.key[LF][0] == 0x1D && o.key[RF][0] == (KEY_E0 | 0x1D));
    assert(o.key[LA][0] == 0x1C && o.key[ST][0] == 0x02 && has(o.key[EX], o.nkey[EX], 0x10));
    assert(o.nkey[NL] == 0);                                                /* Z: its own */
    assert(o.button[LF][0] == 0 && o.button[RF][0] == 1 && o.nbutton[LA] == 0 && strstr(o.notes, "mouse"));
    assert(o.plunger_axis == 2 && !o.plunger_reversed && o.plunger_speed_axis == -1);
    assert(o.nudge_x == 0 && o.nudge_y == 1 && o.invert_x && !o.invert_y && o.is_velocity && o.dead == 3277);
    assert(!o.ssf && strstr(o.notes, "speaker mode 1"));

    vpx_file(p, "[Display]\nscale = sharp\n");                             /* not VPinball's */
    assert(!vpx_read(p, &o));
    remove(p);
}

static void test_nudge(void)
{
    assert(nudge_push(1000, 1500, 1) == 0 && nudge_push(-1500, 1500, 1) == 0);
    assert(nudge_push(32767, 1500, 1) == NUDGE_STEP && nudge_push(-32767, 1500, 2) == -2 * NUDGE_STEP);
    assert(nudge_push(40000, 0, 1) == NUDGE_STEP);                  /* past full: full */

    WatchMap m;
    memset(&m, 0, sizeof m);
    m.active = 0x100; m.vx = 0x110; m.vy = 0x120; m.slots = 2;
    m.tilt = 0x130; m.meter = 0x131; m.demo = 0x132;
    m.nudge_gate = 0x140; m.gate_lo = 4; m.gate_hi = 0x2A;
    static uint8_t ds[0x10000];
    memset(ds, 0, sizeof ds);
    ds[0x100] = 1;                                                   /* ball 0 in play, ball 1 not */
    ds[0x140] = 0x10;
    NudgeSettings st = { 1500, 1.0, 1.0, 0, 0, 0 };
    nudge_settings(&st);
    #define VX(i) ((int16_t)(ds[0x110 + 2 * (i)] | ds[0x111 + 2 * (i)] << 8))
    #define VY(i) ((int16_t)(ds[0x120 + 2 * (i)] | ds[0x121 + 2 * (i)] << 8))
    nudge_reading(1, -32767, 0);                                     /* the cabinet shoved left */
    nudge_step(&m, ds);
    assert(VX(0) == NUDGE_STEP && VY(0) == 0 && VX(1) == 0);         /* the ball goes right */
    nudge_reading(1, 0, 32767);                                      /* along it */
    nudge_step(&m, ds);
    assert(VY(0) == -NUDGE_STEP);
    memset(ds + 0x110, 0, 0x20);
    nudge_reading(1, 1500 + 79, 0);                                  /* a twentieth of a unit a step */
    for (int i = 0; i < 40; i++) nudge_step(&m, ds);
    assert(VX(0) == -1 || VX(0) == -2);                              /* adds up, not rounded away */
    ds[0x131] = 0;
    memset(ds + 0x110, 0, 0x20);
    nudge_reading(1, -32767, 0);
    for (int i = 0; i < 24; i++) nudge_step(&m, ds);                 /* a whole game nudge's push */
    assert(ds[0x131] >= 0x22 && ds[0x131] <= 0x23);                  /* fills the meter as one */
    ds[0x140] = 0x2C;                                                /* out of the gate: no push */
    int before = VX(0), meter = ds[0x131];
    nudge_step(&m, ds);
    assert(VX(0) == before && ds[0x131] > meter);                    /* but the meter still fills */
    ds[0x140] = 0x10;
    ds[0x130] = 1;                                                   /* tilted: nothing */
    meter = ds[0x131];
    nudge_step(&m, ds);
    assert(VX(0) == before && ds[0x131] == meter);
    ds[0x130] = 0; ds[0x132] = 1;                                    /* the demo: nothing */
    nudge_step(&m, ds);
    assert(VX(0) == before);
    ds[0x132] = 0;
    st.invert_x = 1;                                                 /* mounted the other way round */
    nudge_settings(&st);
    nudge_step(&m, ds);
    assert(VX(0) == before - NUDGE_STEP);
    nudge_reading(0, -32767, 0);                                     /* none: nothing */
    nudge_step(&m, ds);
    assert(VX(0) == before - NUDGE_STEP);
    st.invert_x = 0;

    /* is_velocity: the push is the change of the cabinet's velocity, once */
    st.is_velocity = 1;
    nudge_settings(&st);
    memset(ds + 0x110, 0, 0x20);
    ds[0x131] = 0;
    nudge_reading(1, 0, 0);
    nudge_step(&m, ds);                                              /* at rest: the first step only learns */
    assert(VX(0) == 0 && VY(0) == 0);
    nudge_reading(1, -32767, 0);                                     /* moving left at full speed, all at once */
    nudge_step(&m, ds);
    assert(VX(0) == NUDGE_GAME && ds[0x131] >= 0x22 && ds[0x131] <= 0x23);   /* a whole game nudge, rightwards */
    nudge_step(&m, ds);                                              /* still moving: nothing more */
    nudge_step(&m, ds);
    assert(VX(0) == NUDGE_GAME);
    nudge_reading(1, 0, 0);                                          /* stopped: pushed back as much */
    nudge_step(&m, ds);
    assert(VX(0) == 0);
    nudge_reading(1, 1400, -1400);                                   /* within the dead zone: nothing */
    nudge_step(&m, ds);
    assert(VX(0) == 0 && VY(0) == 0);
    nudge_reading(0, 0, 0);                                          /* lost, then back moving: no jump */
    nudge_step(&m, ds);
    nudge_reading(1, 32767, 0);
    nudge_step(&m, ds);
    assert(VX(0) == 0);
    int nx, ny;
    st.invert_y = 1;
    nudge_settings(&st);
    nudge_reading(1, 100, 200);
    assert(nudge_now(&nx, &ny) && nx == 100 && ny == -200);          /* the indicator's: inverted as set */
    st.invert_y = 0; st.is_velocity = 0;
    nudge_settings(&st);
    #undef VX
    #undef VY

    assert(nudge_trace("1000:-20000,0,1100:0,5000") && nudge_traced());
    int x, y;
    nudge_trace_at(500, &x, &y);  assert(x == 0 && y == 0);
    nudge_trace_at(1050, &x, &y); assert(x == -20000 && y == 0);
    nudge_trace_at(2000, &x, &y); assert(x == 0 && y == 5000);
    assert(!nudge_trace("1000:-20000") && !nudge_trace("1000:1,2,500:0,0") && !nudge_trace("1:40000,0"));
    st.invert_x = 0;
    nudge_settings(&st);
    nudge_reading(0, 0, 0);
}

int main(void)
{
    g_cpu.mem = (uint8_t *)calloc(1, MEM_SIZE);
    test_split_screen();
    test_sb16_stereo();
    test_smooth_sound();
    test_whole_table();
    test_table_layouts();
    test_cabinet_views();
    test_flip_wait();
    test_images();
    test_feedback_map();
    test_watch();
    test_plunger();
    test_dmd();
    test_ssf();
    test_autostart();
    test_arcade();
    test_nudge();
    test_ini();
    test_keys();
    test_hud();
    test_vpx_import();
#ifndef _WIN32
    test_dof_pipe();
#endif
    printf("selftest: ok\n");
    return 0;
}
