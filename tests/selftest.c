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
uint64_t pit_now(void) { return 0; }
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
    "epicpinball-feedback 1\nimage ep1 16 69691905\n"
    "ds 10\ncs 2000\nactive 100\nx 110\ny 120\nvx 130\nvy 140\nslots 5\n"
    "flip_l 150\nflip_r 152\nup_l 154\nup_r 155\ntilt 160\nmeter 161\ncooldown 162\ndemo 163\n"
    "extra 164\nover 165\ncount 166\ndrain_y 18F\nkick 167\nweak_y 180\nsling_line C7\nmap 170\n"
    "pull 180\npull_inc C\npull_max 2C4\npull_shift 5\nctrl 297\nspace 296\n"
    "lane_off 0\nlane_x 118\nlane_y DC\nover_pull 0\n"
    "step_fn ep1_step\nkicker_fn ep1_kick\nbumper 100 60 0\nbumper 200 60 2\nend\n";

static void test_feedback_map(void)
{
    /* a map is read strictly: half of one would name arbitrary guest memory */
    static char cut[sizeof k_map];
    const char *why = "";
    WatchMap m;
    assert(watch_map_parse(k_map, &m, &why));
    assert(!strcmp(m.name, "ep1") && m.size == 16 && m.fnv == 0x69691905u && m.pull_max == 708 && m.nbumpers == 2);
    assert(watch_kick_event(&m, 90, 70) == FB_BUMPER_LEFT && watch_kick_event(&m, 210, 70) == FB_BUMPER_RIGHT);
    assert(watch_kick_event(&m, 40, 300) == FB_SLING_LEFT && watch_kick_event(&m, 280, 300) == FB_SLING_RIGHT);
    for (size_t n = 0; n < sizeof k_map - 5; n++) {       /* cut short anywhere */
        memcpy(cut, k_map, n);
        cut[n] = 0;
        assert(!watch_map_parse(cut, &m, &why));
    }
    memcpy(cut, k_map, sizeof k_map);
    *strstr(cut, " 1\n") = '\t';
    strstr(cut, "\t1\n")[1] = '2';                        /* another version */
    assert(!watch_map_parse(cut, &m, &why));
    memcpy(cut, k_map, sizeof k_map);
    strstr(cut, "pull 180")[0] = 'q';                     /* a key it does not know */
    assert(!watch_map_parse(cut, &m, &why));
}

static int seen[FB_OUTPUTS];
static void seen_table(const char *name) { (void)name; }
static void seen_event(FbEvent e, int on) { seen[e] += on; }
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
    assert(!seen[FB_BUMPER_LEFT]);
    watch_enter(&g_cpu, "ep1_kick");                      /* entered, and turned back */
    assert(!seen[FB_BUMPER_LEFT]);
    store8(kick, 9);
    store8(kick, 9);                                      /* one entry, one hit */
    assert(seen[FB_BUMPER_LEFT] == 1);
    ds[m.y] = 0x80; ds[m.y + 1] = 1;                      /* at weak_y */
    watch_enter(&g_cpu, "ep1_kick");
    store8(kick, 3);
    assert(seen[FB_BUMPER_LEFT] == 1);

    ds[m.demo] = 1;                                       /* the demo is never felt */
    ds[m.y] = 50; ds[m.y + 1] = 0;
    watch_enter(&g_cpu, "ep1_kick");
    store8(kick, 9);
    assert(seen[FB_BUMPER_LEFT] == 1);
    ds[m.demo] = 0;

    ds[m.pull] = 0; ds[m.pull + 1] = 0;                   /* the host cancelling a pull is no launch */
    store8(vy, 4); store8(vy + 1, 0);
    assert(!seen[FB_LAUNCH]);
    ds[m.pull] = 0xC4; ds[m.pull + 1] = 2;                /* the game: zeroes it, then the velocity */
    store8(pull, 0); store8(pull + 1, 0);
    store8(vy, 0x3C); store8(vy + 1, 0xFD);
    assert(seen[FB_LAUNCH] == 1);
    watch_table_end();
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
               "case \"$*\" in *--all-off*) exit 0;; *ep3) exit 2;; *ep5) exit 5;; esac\necho ready\n"
               "while read -r op a b; do echo \"$op $a $b\" >> '%s/got'\n"
               "case \"$op $a\" in q*) exit 0;; 'e tilt') exit 3;; esac; done\n",
            dir, dir);
    fclose(f);
    assert(chmod(path, 0700) == 0);

    real_clock = 1;
    assert(dof_open(path, dir));
    feedback_table(0, "ep1");
    pump(300);
    feedback_event(1, FB_FLIPPER_LEFT, 1, (FbDetail){ 0 });
    pump(1200);                                           /* held, and nothing said: no fault, and no off */
    feedback_event(1, FB_DRAIN, 1, (FbDetail){ 0 });
    feedback_mute(1);                                     /* the demo: never sent */
    feedback_event(2, FB_KNOCKER, 1, (FbDetail){ 0 });
    feedback_mute(0);
    feedback_table(3, NULL);                              /* the table's end: q */
    pump(300);
    feedback_table(4, "ep2");
    pump(300);
    feedback_event(5, FB_TILT, 1, (FbDetail){ 0 });       /* it dies: all off, and none till the next table */
    pump(500);
    feedback_table(6, "ep3");                             /* no row for this table: only this one goes without */
    pump(300);
    feedback_table(7, "ep4");
    pump(300);
    feedback_table(8, "ep5");                             /* no row at all: none for the rest of the run */
    pump(300);
    feedback_table(9, "ep6");
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
    assert(strstr(got, "--table ep1") && strstr(got, "--save") && !strstr(got, "knocker"));
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
#ifndef _WIN32
    test_dof_pipe();
#endif
    printf("selftest: ok\n");
    return 0;
}
