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
    "epicpinball-feedback 3\nimage ep1 16 69691905\n"
    "ds 10\ncs 2000\nactive 100\nx 110\ny 120\nvx 130\nvy 140\nslots 5\n"
    "flip_l 150\nflip_r 152\nup_l 154\nup_r 155\ntilt 160\nmeter 161\ncooldown 162\ndemo 163\n"
    "extra 164\nover 165\ncount 166\ndrain_y 18F\nkick 167\nweak_y 180\nsling_line C7\nmap 170\n"
    "pull 180\npull_inc C\npull_max 2C4\npull_shift 5\nctrl 297\nspace 296\n"
    "lane_off 0\nlane_x 118\nlane_y DC\nover_pull 0\nscan_x 190\nscan_y 192\n"
    "step_fn ep1_step\nkicker_fn ep1_kick\nswitch_fn ep1_sw\nbumper 100 60 0\nbumper 200 60 2\n"
    "msg_fn ep1_msg\nlayer 240\non_layer F9 F8 F1\ntilt_pass F0 F6\n"
    "zone hole 1 F5 0 0 319 399 L\nzone hole 2 F6 0 0 219 399 C\nzone hole 3 F6 220 0 319 399 R\n"
    "zone kickback 1 F8 0 321 50 399 L\n"
    "zone rollover 1 FA 0 0 179 399 C\nzone rollover 2 FA 180 0 319 399 R\n"
    "zone ramp 1 F9 0 0 74 399 L\nzone ramp 2 F6 220 0 319 399 R\n"
    "timer 200\ntimer 201\ntarget 210 1 L\ntarget 211 2 L\ntarget 212 3 L\nlanes 220 4\nramps 230\n"
    "msg 300 jackpot\nmsg 310 award\nend\n";

static void test_feedback_map(void)
{
    /* a map is read strictly: half of one would name arbitrary guest memory */
    static char cut[sizeof k_map];
    const char *why = "";
    WatchMap m;
    assert(watch_map_parse(k_map, &m, &why));
    assert(!strcmp(m.name, "ep1") && m.size == 16 && m.fnv == 0x69691905u && m.pull_max == 708 && m.nbumpers == 2);
    assert(!strcmp(m.layout, "LR"));
    /* a table's own features: the zone a switch is, and every side for made rows */
    assert(!strcmp(m.sides, "hole:LCR ramp:LR kickback:L target:LLL"));
    assert(watch_zone(&m, WZ_HOLE, 0xF6, 219, 50) == 1 && watch_zone(&m, WZ_HOLE, 0xF6, 220, 50) == 2);
    assert(watch_zone(&m, WZ_RAMP, 0xF6, 220, 50) == 7 && watch_zone(&m, WZ_RAMP, 0xF6, 219, 50) < 0);
    assert(watch_zone(&m, WZ_KICKBACK, 0xF8, 30, 320) < 0 && watch_zone(&m, WZ_KICKBACK, 0xF8, 30, 321) == 3);
    /* each bumper its own event, left to right; slings by side */
    assert(watch_kick_event(&m, 90, 70) == FB_BUMPER_1 && watch_kick_event(&m, 210, 70) == FB_BUMPER_2);
    assert(watch_kick_event(&m, 40, 300) == FB_SLING_LEFT && watch_kick_event(&m, 280, 300) == FB_SLING_RIGHT);
    {
        WatchMap six = m;                                 /* table 5's six: E105-E107, then E118-E120 */
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
    strstr(cut, "feedback 3")[9] = '2';                   /* another version */
    assert(!watch_map_parse(cut, &m, &why));
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
}

static int seen[FB_OUTPUTS];
static void seen_table(const char *name, const char *bumpers, const char *sides) { (void)name; (void)bumpers; (void)sides; }
static int level[FB_OUTPUTS];
static void seen_event(FbEvent e, int on) { seen[e] += on; level[e] = on; }
static void seen_off(void) { }

static void store8(uint32_t a, uint8_t v)
{
    watch_write_byte(a, v);                               /* as vga.c's hook: before the store */
    g_cpu.mem[a] = v;
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

#ifndef _WIN32
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
    feedback_table(0, "ep1", "LR", "hole:LLCRR kickback:L");
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
    assert(strstr(got, "--table ep1 --bumpers LR --sides hole:LLCRR kickback:L") && strstr(got, "--save") && !strstr(got, "knocker"));
    assert(strstr(got, "--table ep2 --bumpers LCR\n"));           /* no features: no --sides */
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
#ifndef _WIN32
    test_dof_pipe();
#endif
    printf("selftest: ok\n");
    return 0;
}
