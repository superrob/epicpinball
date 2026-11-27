/*
 * vga.c - the VGA, as Epic Pinball programs it.
 *
 * The tables run in "Mode X" at 320x240: BIOS mode 13h, chain-4 switched off
 * and the CRTC reprogrammed for 480 double-scanned lines, so one byte address
 * at A000 covers four pixels, one per plane, and the Map Mask picks which
 * planes a write lands in. On top of that they lean on the parts of the card a
 * flat framebuffer does not have:
 *
 *   - latches: a read loads one byte from every plane, and write mode 1 stores
 *     those four bytes back -- the fast way to copy video memory to itself;
 *   - the CRTC start address, which is how the playfield scrolls and pages
 *     flip (nothing is copied);
 *   - line compare, the split screen the dot-matrix scoreboard sits in;
 *   - the Graphics Controller's set/reset, bit mask and logical ops.
 *
 * VRAM is stored interleaved, vram[offset*4 + plane], which also makes chain-4
 * mode a plain linear store (plane = addr & 3, offset = addr >> 2).
 */
#include "machine.h"
#include "watch.h"
#include "recomp16/platform/font8x8.h"
#ifdef EPIC_DEBUG
#include <intrin.h>
#endif

static uint8_t vram[0x40000];
static uint8_t latch[4];
static uint8_t seq[8], gc[16], crtc[32], attr[32];
static uint8_t seq_i, gc_i, crtc_i, attr_i, attr_flip;
static uint8_t dac[256][3], dac_wi, dac_ri, dac_wc, dac_rc, dac_mask = 0xFF;
static int mode = 3;                 /* BIOS mode: 3 text, 0x13 graphics */
static int chain4 = 0;
static uint8_t misc_out = 0x63;
static int table_dmd;                /* the table's scoreboard at full height, in rows */

uint8_t *vga_text_mem(void) { return g_cpu.mem + 0xB8000; }

/* The text screen's font: pcrecomp's code page 437, with its blank 80h-AFh
 * filled in -- SETUP's languages are Fran\x87ais and Espa\xA4ol, and its German,
 * French and Spanish help is full of accents. An accented letter is its base
 * letter with the accent drawn above it (a capital is squeezed down a row to
 * make room) or the cedilla below; the rest of that range is drawn here. */
static uint8_t font[256][8];

static void font_build(void)
{
    memcpy(font, font8x8_cp437, sizeof font);
    /* accents: two rows over a small letter, one (ring: two) over a capital */
    enum { GRAVE, ACUTE, CIRC, DIAER, RING, TILDE, CEDIL };
    static const uint8_t mark[][2] = {
        [GRAVE] = { 0x30, 0x18 }, [ACUTE] = { 0x0C, 0x18 }, [CIRC] = { 0x38, 0x6C },
        [DIAER] = { 0x6C, 0x00 }, [RING] = { 0x38, 0x28 }, [TILDE] = { 0x76, 0xDC } };
    static const struct { uint8_t code; char base; uint8_t accent; } acc[] = {
        {0x80,'C',CEDIL},{0x81,'u',DIAER},{0x82,'e',ACUTE},{0x83,'a',CIRC},{0x84,'a',DIAER},
        {0x85,'a',GRAVE},{0x86,'a',RING},{0x87,'c',CEDIL},{0x88,'e',CIRC},{0x89,'e',DIAER},
        {0x8A,'e',GRAVE},{0x8B,'i',DIAER},{0x8C,'i',CIRC},{0x8D,'i',GRAVE},{0x8E,'A',DIAER},
        {0x8F,'A',RING},{0x90,'E',ACUTE},{0x93,'o',CIRC},{0x94,'o',DIAER},{0x95,'o',GRAVE},
        {0x96,'u',CIRC},{0x97,'u',GRAVE},{0x98,'y',DIAER},{0x99,'O',DIAER},{0x9A,'U',DIAER},
        {0xA0,'a',ACUTE},{0xA1,'i',ACUTE},{0xA2,'o',ACUTE},{0xA3,'u',ACUTE},{0xA4,'n',TILDE},
        {0xA5,'N',TILDE}};
    for (size_t i = 0; i < sizeof acc / sizeof acc[0]; i++) {
        const uint8_t *b = font8x8_cp437[(uint8_t)acc[i].base];
        uint8_t *g = font[acc[i].code];
        int cap = acc[i].base < 'a';
        if (acc[i].accent == CEDIL) {
            memcpy(g, b, 8);
            g[7] = 0x18;
        } else if (cap) {
            static const int keep[6] = { 0, 1, 2, 4, 5, 6 };   /* the middle row goes */
            g[0] = mark[acc[i].accent][0];
            g[1] = acc[i].accent == RING ? mark[RING][1] : 0;
            for (int r = 0; r < 6; r++) g[2 + r] = b[keep[r]];
        } else {
            memcpy(g, b, 8);
            g[0] = mark[acc[i].accent][0];
            g[1] = mark[acc[i].accent][1];
        }
    }
    static const struct { uint8_t code, g[8]; } own[] = {
        {0x91,{0x00,0x00,0x7E,0x1B,0x7F,0xD8,0x7E,0x00}},   /* ae */
        {0x92,{0x3E,0x6C,0xCC,0xFE,0xCC,0xCC,0xCE,0x00}},   /* AE */
        {0x9B,{0x18,0x18,0x7E,0xC0,0xC0,0x7E,0x18,0x18}},   /* cent */
        {0x9C,{0x38,0x6C,0x64,0xF0,0x60,0xE6,0xFC,0x00}},   /* pound */
        {0x9D,{0xCC,0xCC,0x78,0xFC,0x30,0xFC,0x30,0x30}},   /* yen */
        {0x9E,{0xF8,0xCC,0xCC,0xFA,0xC6,0xCF,0xC6,0xC7}},   /* peseta */
        {0x9F,{0x0E,0x1B,0x18,0x3C,0x18,0x18,0xD8,0x70}},   /* florin */
        {0xA6,{0x3C,0x6C,0x6C,0x3E,0x00,0x7E,0x00,0x00}},   /* feminine ordinal */
        {0xA7,{0x38,0x6C,0x6C,0x38,0x00,0x7C,0x00,0x00}},   /* masculine ordinal */
        {0xA8,{0x30,0x00,0x30,0x60,0xC0,0xCC,0x78,0x00}},   /* inverted question mark */
        {0xA9,{0x00,0x00,0x00,0xFC,0xC0,0xC0,0x00,0x00}},   /* reversed not */
        {0xAA,{0x00,0x00,0x00,0xFC,0x0C,0x0C,0x00,0x00}},   /* not */
        {0xAB,{0xC3,0xC6,0xCC,0xDE,0x33,0x66,0xCC,0x0F}},   /* one half */
        {0xAC,{0xC3,0xC6,0xCC,0xDB,0x37,0x6F,0xCF,0x03}},   /* one quarter */
        {0xAD,{0x18,0x18,0x00,0x18,0x18,0x18,0x18,0x00}},   /* inverted exclamation mark */
        {0xAE,{0x00,0x33,0x66,0xCC,0x66,0x33,0x00,0x00}},   /* << */
        {0xAF,{0x00,0xCC,0x66,0x33,0x66,0xCC,0x00,0x00}},   /* >> */
        {0xB2,{0xDD,0x77,0xDD,0x77,0xDD,0x77,0xDD,0x77}}};  /* dark shade (pcrecomp's is solid) */
    for (size_t i = 0; i < sizeof own / sizeof own[0]; i++) memcpy(font[own[i].code], own[i].g, 8);
}

static inline int chained(void) { return (seq[4] & 8) != 0; }

void vga_set_mode(int m)
{
    mode = m & 0x7F;
    memset(seq, 0, sizeof seq); memset(gc, 0, sizeof gc); memset(crtc, 0, sizeof crtc);
    seq[2] = 0x0F;
    gc[8] = 0xFF;
    gc[6] = 0x05;
    if (mode == 0x13) {
        seq[4] = 0x0E;               /* chain-4, odd/even off, extended memory */
        crtc[0x09] = 0x41;           /* double scan: 200 rows on 400 lines */
        crtc[0x12] = 0x8F;           /* vertical display end 399 (with overflow bit) */
        crtc[0x07] = 0x1F;
        crtc[0x13] = 40;             /* 320 bytes per row in dword mode */
        crtc[0x14] = 0x40;           /* dword mode */
        crtc[0x17] = 0xA3;
        gc[5] = 0x40;
        if (!(m & 0x80)) memset(vram, 0, sizeof vram);
    } else {
        crtc[0x09] = 0x0F;
        if (!(m & 0x80)) {
            uint8_t *t = vga_text_mem();
            for (int i = 0; i < 80 * 25; i++) { t[i * 2] = ' '; t[i * 2 + 1] = 7; }
        }
    }
    for (int i = 0; i < 16; i++) attr[i] = (uint8_t)i;
    attr[0x10] = (mode == 0x13) ? 0x41 : 0x0C;
    table_dmd = 0;
    g_cpu.mem[0x449] = (uint8_t)mode;
    chain4 = chained();
}

int vga_mode(void) { return mode; }

void vga_init(void)
{
    /* The default VGA palette's first 16 entries, as text mode shows them. */
    static const uint8_t ega[16][3] = {
        {0,0,0},{0,0,42},{0,42,0},{0,42,42},{42,0,0},{42,0,42},{42,21,0},{42,42,42},
        {21,21,21},{21,21,63},{21,63,21},{21,63,63},{63,21,21},{63,21,63},{63,63,21},{63,63,63}};
    memset(dac, 0, sizeof dac);
    memcpy(dac, ega, sizeof ega);
    font_build();
    /* text mode reads attr -> DAC through the EGA-compatible mapping */
    vga_set_mode(3);
}

/* ---- CPU access to A000-AFFF ------------------------------------------- */

int recomp_mem_write8(CPU *cpu, uint32_t a, uint8_t v)
{
#ifdef EPIC_DEBUG
    { void watch_write(uint32_t, uint8_t); watch_write(a, v); }
#endif
    if (a - 0xA0000u >= 0x10000u) {
        if (g_watch_on && a - g_watch_ds < 0x10000u && g_watched[a - g_watch_ds]) watch_write_byte(a, v);
        return 0;
    }
#ifdef EPIC_DEBUG
    { void vgaprof_note(void *ra); vgaprof_note(_ReturnAddress()); }
#endif
    (void)cpu;
    uint32_t off = a - 0xA0000u;
    if (chain4) { vram[off] = v; return 1; }
    uint8_t mask = seq[2] & 0x0F;
    uint8_t *p = &vram[off * 4];
    int wmode = gc[5] & 3;
    if (wmode == 0 && !(gc[1] & 0x0F) && !(gc[3]) && gc[8] == 0xFF) {
        if (mask & 1) p[0] = v;
        if (mask & 2) p[1] = v;
        if (mask & 4) p[2] = v;
        if (mask & 8) p[3] = v;
        return 1;
    }
    if (wmode == 1) {
        for (int i = 0; i < 4; i++) if (mask & (1 << i)) p[i] = latch[i];
        return 1;
    }
    uint8_t rot = gc[3] & 7, fn = (gc[3] >> 3) & 3, bm = gc[8];
    uint8_t r = (uint8_t)((v >> rot) | (v << (8 - rot)));
    if (wmode == 3) { bm &= r; }
    for (int i = 0; i < 4; i++) {
        if (!(mask & (1 << i))) continue;
        uint8_t d;
        if (wmode == 2)       d = (v & (1 << i)) ? 0xFF : 0;
        else if (wmode == 3)  d = (gc[0] & (1 << i)) ? 0xFF : 0;
        else                  d = (gc[1] & (1 << i)) ? ((gc[0] & (1 << i)) ? 0xFF : 0) : r;
        switch (fn) {
        case 1: d &= latch[i]; break;
        case 2: d |= latch[i]; break;
        case 3: d ^= latch[i]; break;
        }
        p[i] = (uint8_t)((d & bm) | (latch[i] & ~bm));
    }
    return 1;
}

int recomp_mem_read8(CPU *cpu, uint32_t a, uint8_t *out)
{
    if (a - 0xA0000u >= 0x10000u) return 0;
    (void)cpu;
    uint32_t off = a - 0xA0000u;
    if (chain4) { *out = vram[off]; return 1; }
    uint8_t *p = &vram[off * 4];
    latch[0] = p[0]; latch[1] = p[1]; latch[2] = p[2]; latch[3] = p[3];
    if (gc[5] & 8) {                                   /* read mode 1: colour compare */
        uint8_t m = 0xFF;                              /* bits where every cared-about plane matched */
        for (int i = 0; i < 4; i++)
            if (gc[7] & (1 << i)) m &= (uint8_t)~(latch[i] ^ ((gc[2] & (1 << i)) ? 0xFF : 0));
        *out = m;
        return 1;
    }
    *out = latch[gc[4] & 3];
    return 1;
}

/* Mode X 320x240, which only the tables use. The split is no part of it. */
static int table_screen(void)
{
    if (mode != 0x13 || chain4) return 0;
    unsigned vde = crtc[0x12] | ((crtc[0x07] & 2) << 7) | ((crtc[0x07] & 0x40) << 3);
    return (vde + 1) / ((crtc[0x09] & 0x1F) + 1u) == 240;
}

int vga_table_up(void) { return table_screen(); }

/* The rows under the line-compare split: a table's scoreboard, as much of it
 * as shows right now. 0 when the screen is not Mode X 240 with a split. */
static int display_rows(void)
{
    if (!table_screen()) return 0;
    unsigned scan = (crtc[0x09] & 0x1F) + 1;
    unsigned lc = crtc[0x18] | ((crtc[0x07] & 0x10) << 4) | ((crtc[0x09] & 0x40) << 3);
    int split = (int)((lc + 1) / scan);
    return split > 0 && split < 240 ? 240 - split : 0;
}

/* ---- ports 3C0-3DA --------------------------------------------------------- */

static uint64_t t0_us;

/* A new start address shows from the next retrace, so one retrace answers
 * one page flip. PINBALL flips, then waits for "in retrace" without first
 * waiting for the last one to end: a PC of the day was still drawing when
 * that ended, this host is back within microseconds, and its menus ran a
 * hundred frames in one retrace (docs/architecture.md, "Time"). */
static int flip_pending;              /* start address set, no retrace seen since */
static uint64_t flip_refresh = UINT64_MAX;   /* whose retrace answered the last flip */

/* 70 Hz refresh, the last ~10% of it in vertical retrace. Emulated time,
 * because the tables lock their timer to retrace (docs/architecture.md, "Time").
 * ponytail: 70 Hz whatever the mode; 320x240 is 60 Hz on real hardware. Derive
 * it from the CRTC's vertical total if a table's timing ever looks off. */
static uint8_t input_status(void)
{
    static uint8_t de;
    uint64_t us = emu_us() - t0_us;
    uint64_t ph = us % 14286, refresh = us / 14286;
    int blank = ph >= 12900, retrace = blank;
    if (retrace && flip_pending) {
        if (refresh == flip_refresh) retrace = 0;      /* used up: wait for the next */
        else { flip_refresh = refresh; flip_pending = 0; }
    }
    de ^= 1;                                           /* display enable flickers per line */
    return (uint8_t)((retrace ? 0x08 : 0) | (blank ? 1 : de));
}

int vga_port_in(uint16_t port, uint8_t *v)
{
    switch (port) {
    case 0x3C0: *v = attr_i; return 1;
    case 0x3C1: *v = attr[attr_i & 0x1F]; return 1;
    case 0x3C2: *v = 0x10; return 1;
    case 0x3C4: *v = seq_i; return 1;
    case 0x3C5: *v = seq[seq_i & 7]; return 1;
    case 0x3C6: *v = dac_mask; return 1;
    case 0x3C7: *v = 3; return 1;
    case 0x3C8: *v = dac_wi; return 1;
    case 0x3C9: *v = dac[dac_ri][dac_rc]; if (++dac_rc == 3) { dac_rc = 0; dac_ri++; } return 1;
    case 0x3CC: *v = misc_out; return 1;
    case 0x3CE: *v = gc_i; return 1;
    case 0x3CF: *v = gc[gc_i & 15]; return 1;
    case 0x3D4: *v = crtc_i; return 1;
    case 0x3D5: *v = crtc[crtc_i & 31]; return 1;
    case 0x3DA: attr_flip = 0; machine_port_wait(1); *v = input_status(); return 1;
    }
    return 0;
}

int vga_port_out(uint16_t port, uint8_t v)
{
    switch (port) {
    case 0x3C0:
        if (!attr_flip) attr_i = v; else attr[attr_i & 0x1F] = v;
        attr_flip ^= 1; return 1;
    case 0x3C2: misc_out = v; return 1;
    case 0x3C4: seq_i = v; return 1;
    case 0x3C5: seq[seq_i & 7] = v; if ((seq_i & 7) == 4) chain4 = chained(); return 1;
    case 0x3C6: dac_mask = v; return 1;
    case 0x3C7: dac_ri = v; dac_rc = 0; return 1;
    case 0x3C8: dac_wi = v; dac_wc = 0; return 1;
    case 0x3C9: dac[dac_wi][dac_wc] = v & 0x3F; if (++dac_wc == 3) { dac_wc = 0; dac_wi++; } return 1;
    case 0x3CE: gc_i = v; return 1;
    case 0x3CF: gc[gc_i & 15] = v; return 1;
    case 0x3D4: crtc_i = v; return 1;
    case 0x3D5:
        if ((crtc_i & 31) != 0x0C && (crtc_i & 31) != 0x0D && crtc[crtc_i & 31] != v)
            trace("[vga] CRTC %02X = %02X\n", crtc_i & 31, v);
        crtc[crtc_i & 31] = v;
        if ((crtc_i & 31) == 0x0D) {                   /* the game writes 0Ch, then 0Dh */
            flip_pending = 1;
            /* Enter slides the scoreboard away by moving the split; the pages
             * stay where its full height put them. Taken at a flip, when the
             * CRTC is whole: half programmed, it shows splits no table has. */
            int dmd = display_rows();
            if (dmd > table_dmd) table_dmd = dmd;
        }
        return 1;
    }
    return 0;
}

/* INT 10h palette services land here too. */
void vga_dac_set(int i, uint8_t r, uint8_t g, uint8_t b) { dac[i & 255][0] = r & 63; dac[i & 255][1] = g & 63; dac[i & 255][2] = b & 63; }
void vga_dac_get(int i, uint8_t *r, uint8_t *g, uint8_t *b) { *r = dac[i & 255][0]; *g = dac[i & 255][1]; *b = dac[i & 255][2]; }

/* One byte of video memory, as vram[] holds it: (address << 2) | plane. For
 * the DMD (dmd.c), which takes a message's dots out of a picture of the
 * playfield and puts them back before the game runs again. */
uint8_t vga_vram_peek(uint32_t i) { return vram[i & 0x3FFFF]; }
void vga_vram_poke(uint32_t i, uint8_t v) { vram[i & 0x3FFFF] = v; }

/* ---- scanout --------------------------------------------------------------- */

static inline uint32_t rgb(int i)
{
    i &= dac_mask;
    return 0xFF000000u | ((uint32_t)(dac[i][0] * 255 / 63) << 16)
         | ((uint32_t)(dac[i][1] * 255 / 63) << 8) | (uint32_t)(dac[i][2] * 255 / 63);
}

/* What the monitor shows right now, as 0xAARRGGBB. Graphics: 320 x rows, rows
 * from the CRTC (200 for PINBALL's screens, 240 for the tables). Text: 640x400. */
static int bar_row = -1;             /* in the last picture composed: where video address 0 starts */
int vga_bar_row(void) { return bar_row; }

void vga_compose(uint32_t *out, int *w, int *h)
{
    bar_row = -1;
    if (mode != 0x13) {
        static uint32_t blink;
        const uint8_t *t = vga_text_mem();
        int cx = g_cpu.mem[0x450], cy = g_cpu.mem[0x451];
        blink++;
        *w = 640; *h = 400;
        for (int r = 0; r < 25; r++)
            for (int c = 0; c < 80; c++) {
                uint8_t ch = t[(r * 80 + c) * 2], at = t[(r * 80 + c) * 2 + 1];
                uint32_t fg = rgb(attr[at & 15] & 0x3F), bg = rgb(attr[(at >> 4) & 7] & 0x3F);
                if ((at & 0x80) && (blink & 32)) fg = bg;
                for (int y = 0; y < 16; y++) {
                    uint8_t bits = font[ch][y >> 1];
                    if (r == cy && c == cx && y >= 13 && (blink & 16)) bits = 0xFF;
                    uint32_t *o = out + (r * 16 + y) * 640 + c * 8;
                    for (int x = 0; x < 8; x++) o[x] = (bits & (0x80 >> x)) ? fg : bg;
                }
            }
        return;
    }
    unsigned start = ((unsigned)crtc[0x0C] << 8) | crtc[0x0D];
    unsigned vde = crtc[0x12] | ((crtc[0x07] & 2) << 7) | ((crtc[0x07] & 0x40) << 3);
    unsigned scan = (crtc[0x09] & 0x1F) + 1;
    int rows = (int)((vde + 1) / scan);
    if (rows < 200 || rows > 480) rows = 200;
    *w = 320; *h = rows;
    if (chain4) {
        /* dword mode: the start address and the row offset count 4-byte units */
        unsigned row = crtc[0x13] ? crtc[0x13] * 8u : 320u;
        for (int y = 0; y < rows; y++)
            for (int x = 0; x < 320; x++)
                out[y * 320 + x] = rgb(vram[(start * 4 + y * row + x) & 0x3FFFF]);
        return;
    }
    unsigned row = crtc[0x13] ? crtc[0x13] * 2u : 80u;
    /* Line compare: at that scanline the CRTC starts over from address 0.
     * Epic Pinball keeps its dot-matrix score display there, below a
     * playfield it scrolls by moving the start address. It counts scanlines,
     * two per row in the double-scanned modes. */
    unsigned lc = crtc[0x18] | ((crtc[0x07] & 0x10) << 4) | ((crtc[0x09] & 0x40) << 3);
    int split = (int)((lc + 1) / scan);
    if (getenv("EPIC_VRAM0")) { start = (unsigned)atoi(getenv("EPIC_VRAM0")); split = rows; }   /* debug: show VRAM from here */
    if (split < rows) bar_row = split;
    for (int y = 0; y < rows; y++) {
        unsigned base = y < split ? start + y * row : (unsigned)(y - split) * row;
        for (int x = 0; x < 320; x++)
            out[y * 320 + x] = rgb(vram[(((base + (x >> 2)) & 0xFFFF) << 2) | (x & 3)]);
    }
}

/* Where a table's two pages are. They start after the full display's rows,
 * rounded up to ten, and halve what is left of the 64K, the first taking the
 * odd row: tables 1-8 show 19 and keep 20 (pages 400 rows apart, at 1600 and
 * 33600), 9-13 show 29 and keep 30 (395 apart, at 2400 and 34000) -- the
 * addresses in every table's own flip. */
typedef struct { unsigned row, start, base0, base1; int rows; } Pages;

static Pages table_pages(void)
{
    Pages p;
    p.row = crtc[0x13] ? crtc[0x13] * 2u : 80u;
    p.start = ((unsigned)crtc[0x0C] << 8) | crtc[0x0D];
    p.base0 = (table_dmd + 9) / 10 * 10 * p.row;
    unsigned left = (0x10000 - p.base0) / p.row;
    p.base1 = p.base0 + (left + 1) / 2 * p.row;
    p.rows = (int)((0x10000 - p.base1) / p.row);         /* the second page ends at 64K */
    return p;
}

/* n Mode X rows of video memory from an address */
static void copy_rows(uint32_t *out, unsigned base, unsigned row, int n)
{
    for (int y = 0; y < n; y++)
        for (int x = 0; x < 320; x++)
            out[y * 320 + x] = rgb(vram[(((base + y * row + (x >> 2)) & 0xFFFF) << 2) | (x & 3)]);
}

/* The whole table at once, for the remaster's "whole table" view. VRAM holds
 * each table twice -- two pages, flipped by the start address -- below the
 * dot-matrix display at address 0, and the CRTC only ever shows the
 * front page's `split` rows from wherever the start address is. This draws
 * that page from its first row, then the display under it, as on screen:
 * 320 x (page + display). The ball is in the page wherever it is; the game
 * draws it there before flipping. Returns 0 when the screen is not a table
 * (anything but Mode X 240 with a line-compare split). */
int vga_compose_table(uint32_t *out, int *w, int *h)
{
    int dmd = display_rows();
    if (!dmd || !table_dmd) return 0;
    Pages p = table_pages();
    if (p.start < p.base0) return 0;
    *w = 320; *h = p.rows + dmd;
    copy_rows(out, p.start >= p.base1 ? p.base1 : p.base0, p.row, p.rows);
    copy_rows(out + p.rows * 320, 0, p.row, dmd);
    bar_row = p.rows;
    return 1;
}

/* --cabinet's playfield: the front page alone. The split is not asked: Enter
 * takes it to 240, where there is nothing under it, and the page is the same.
 * The page is the nearer one, as a scroll can sit a row above a page's top.
 * A table still loading is black, as its scoreboard is the first thing in
 * video memory. Returns 0, with the screen as it is, when it is not a table. */
int vga_compose_playfield(uint32_t *out, int *w, int *h)
{
    if (!table_screen()) { vga_compose(out, w, h); return 0; }
    *w = 320;
    if (!table_dmd) {
        *h = 240;
        for (int i = 0; i < 320 * 240; i++) out[i] = 0xFF000000u;
        return 1;
    }
    Pages p = table_pages();
    *h = p.rows;
    copy_rows(out, p.start >= (p.base0 + p.base1) / 2 ? p.base1 : p.base0, p.row, p.rows);
    return 1;
}

/* --cabinet's scoreboard: address 0 at the display's full height, which the
 * game keeps drawing while it has it hidden. 0 when no table is up. */
int vga_compose_score(uint32_t *out, int *w, int *h)
{
    if (!table_screen() || !table_dmd) return 0;
    *w = 320; *h = table_dmd;
    copy_rows(out, 0, table_pages().row, table_dmd);
    return 1;
}

void vga_start(void) { t0_us = emu_us(); }

