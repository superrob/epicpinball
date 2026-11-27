/*
 * gus.c - a Gravis Ultrasound: its GF1 wavetable chip, as the MASI Gravis
 * driver (MDRV003R.MUS) uses it, written from Gravis's SDK documentation.
 *
 * The card plays samples out of its own memory (1 MB here), each of up to 32
 * voices with its own position, rate, volume and pan; the driver uploads the
 * module's samples by DMA, then starts and steers voices note by note, and
 * keeps time with the GF1's timers. So unlike the Sound Blaster, where the
 * driver mixes and the card only plays a stream, the mixing happens here.
 *
 * Ports, from the base (220h-260h, SETUP's choice, in the driver's
 * settings): +0 mix control, +6 IRQ status, +8/+9 the AdLib-style timer
 * control, +102h voice select, +103h register select, +104h/+105h data low
 * and high (a register is written when its high byte is: an 8-bit register
 * takes that byte), +107h a byte of card memory at the address in 43h/44h.
 *
 * Positions are 20.9 fixed point (a 20-bit sample address and 9 bits of
 * fraction), as the GF1 keeps them; a voice steps by its frequency control
 * once per output frame, and the card makes 617,400 / active voices frames a
 * second (44,100 with 14).
 */
#include "machine.h"
#include <math.h>

#define RAM (1u << 20)
#define QUARTER_TURN 1.5707963267948966      /* pi / 2: MSVC has no M_PI */

typedef struct {
    uint8_t ctrl;           /* 00h: 0 stopped, 1 stop, 2 16-bit, 3 loop, 4 both ways, 5 IRQ, 6 backwards, 7 IRQ pending */
    uint8_t vctrl;          /* 0Dh: the same bits, for the volume ramp */
    uint16_t fc;            /* 01h: step per frame in 1/1024 (bits 15-1 in 1/512) */
    uint32_t start, end, cur;   /* 20.9 */
    uint8_t rate, rstart, rend; /* 06h-08h: ramp rate (6 bits of step, 2 of how often), its ends (vol >> 4 >> 4) */
    uint16_t vol;           /* 09h: bits 15-4, a 12-bit logarithmic volume */
    uint8_t pan;            /* 0Ch: 0 left .. 15 right */
    int ramp_wait;          /* frames to the next ramp step */
} Voice;

static struct {
    int on;                 /* the loaded driver is MASIULTRASND: the card answers */
    uint16_t base;
    int irq, dma;
    uint8_t *ram;
    Voice v[32];
    int active;             /* 14..32 */
    uint8_t voice, reg, readback, mix;
    uint16_t data;
    uint8_t reset;          /* 4Ch: 0 reset, 1 run, 2 DAC on, 4 IRQs on */
    uint8_t dma_ctrl, dma_tc;   /* 41h (0 go, 2 from the card, 5 IRQ, 6 16-bit data, 7 invert), a finished transfer not yet read */
    uint16_t dma_addr;      /* 42h: card address >> 4 */
    uint32_t peek;          /* 43h/44h */
    uint8_t timer_ctrl;     /* 45h: 2 timer 1's IRQ, 3 timer 2's */
    uint8_t t_count[2];     /* 46h/47h */
    uint8_t adlib_sel, adlib;   /* +8 selects, +9 writes: 0 start T1, 1 start T2, 5 mask T2, 6 mask T1, 7 reset */
    int t_run[2], t_fired[2];
    double t_us[2];
    int last_line, edge;
    double frames;          /* card frames owed to the host's */
    int l, r, pl, pr;       /* the last frame and the one before */
} g = { .base = 0x220, .irq = 11, .dma = 5, .active = 14 };

/* ---- registers -------------------------------------------------------------- */

static uint32_t addr_hi(uint32_t a, uint16_t v) { return (a & 0xFFFF) | (uint32_t)(v & 0x1FFF) << 16; }
static uint32_t addr_lo(uint32_t a, uint16_t v) { return (a & 0x1FFF0000u) | v; }

/* the volume a 16-bit register value means, as a gain 0..1: 4 bits of
 * exponent and 8 of mantissa, each exponent step 6 dB */
static double gain(uint16_t vol)
{
    unsigned v = vol >> 4, e = v >> 8, m = v & 0xFF;
    return v ? ldexp(1.0 + m / 256.0, (int)e - 16) : 0.0;
}

static int line(void)
{
    if (!(g.reset & 4)) return 0;
    if (g.dma_tc) return 1;
    for (int t = 0; t < 2; t++) if (g.t_fired[t]) return 1;
    for (int i = 0; i < g.active; i++) if ((g.v[i].ctrl | g.v[i].vctrl) & 0x80) return 1;
    return 0;
}

static void update_irq(void)
{
    int l = line();
    if (l && !g.last_line) g.edge = 1;      /* the PIC sees an edge */
    g.last_line = l;
}

int gus_irq_pending(void) { update_irq(); return g.on && g.edge; }
int gus_irq_line(void) { return g.irq; }
void gus_irq_ack(void) { g.edge = 0; }

static uint8_t irq_status(void)
{
    uint8_t s = 0;
    if (g.t_fired[0]) s |= 0x04;
    if (g.t_fired[1]) s |= 0x08;
    for (int i = 0; i < g.active; i++) {
        if (g.v[i].ctrl & 0x80) s |= 0x20;
        if (g.v[i].vctrl & 0x80) s |= 0x40;
    }
    if (g.dma_tc) s |= 0x80;
    return s;
}

/* a whole DMA transfer into card memory at once, from the channel's address
 * and count; the transfer-complete IRQ if asked for */
static void dma_upload(void)
{
    uint32_t to = (uint32_t)g.dma_addr << 4;
    int wide = g.dma >= 4, n = 0;
    if (wide) to = (to & 0xC0000) | ((to & 0x1FFFF) << 1);   /* the GF1's 16-bit DMA addressing */
    /* 41h bit 7 inverts each sample's top bit (unsigned data); bit 6 says
     * the samples are 16-bit, so on an 8-bit channel only every other byte */
    int invert = (g.dma_ctrl & 0x80) != 0, data16 = (g.dma_ctrl & 0x40) != 0;
    if (wide) {
        uint16_t w;
        while (audio_dma_read16(g.dma, &w)) {
            if (invert) w ^= data16 ? 0x8000 : 0x8080;
            g.ram[to++ & (RAM - 1)] = (uint8_t)w;
            g.ram[to++ & (RAM - 1)] = (uint8_t)(w >> 8);
            if (++n > (1 << 17)) break;
        }
    } else {
        uint8_t b;
        while (audio_dma_read8(g.dma, &b)) {
            if (invert && (!data16 || (n & 1))) b ^= 0x80;
            g.ram[to++ & (RAM - 1)] = b;
            if (++n > (1 << 17)) break;
        }
    }
    trace("[gus] DMA %d: %d %s to %05X\n", g.dma, n, wide ? "words" : "bytes", (unsigned)(g.dma_addr << 4));
    g.dma_ctrl &= ~1;
    if (g.dma_ctrl & 0x20) g.dma_tc = 1;
    update_irq();
}

static void write_reg(void)
{
    Voice *v = &g.v[g.voice & 31];
    uint16_t d = g.data;
    uint8_t b = (uint8_t)(d >> 8);
    switch (g.reg) {
    case 0x00: v->ctrl = (uint8_t)((v->ctrl & 0x80) | (b & 0x7F)); if (b & 2) v->ctrl |= 1; break;
    case 0x01: v->fc = d; break;
    case 0x02: v->start = addr_hi(v->start, d); break;
    case 0x03: v->start = addr_lo(v->start, d & 0xFFE0); break;
    case 0x04: v->end = addr_hi(v->end, d); break;
    case 0x05: v->end = addr_lo(v->end, d & 0xFFE0); break;
    case 0x06: v->rate = b; break;
    case 0x07: v->rstart = b; break;
    case 0x08: v->rend = b; break;
    case 0x09: v->vol = d; break;
    case 0x0A: v->cur = addr_hi(v->cur, d); break;
    case 0x0B: v->cur = addr_lo(v->cur, d); break;
    case 0x0C: v->pan = b & 15; break;
    case 0x0D: v->vctrl = (uint8_t)((v->vctrl & 0x80) | (b & 0x7F)); if (b & 2) v->vctrl |= 1; v->ramp_wait = 0; break;
    case 0x0E: g.active = (b & 31) + 1; if (g.active < 14) g.active = 14; break;
    case 0x41: g.dma_ctrl = b; if (b & 1) dma_upload(); break;
    case 0x42: g.dma_addr = d; break;
    case 0x43: g.peek = (g.peek & 0xF0000) | d; break;
    case 0x44: g.peek = (g.peek & 0xFFFF) | (uint32_t)(b & 15) << 16; break;
    case 0x45:
        g.timer_ctrl = b;
        if (!(b & 4)) g.t_fired[0] = 0;
        if (!(b & 8)) g.t_fired[1] = 0;
        update_irq();
        break;
    case 0x46: g.t_count[0] = b; break;
    case 0x47: g.t_count[1] = b; break;
    case 0x48: case 0x49: break;        /* recording */
    case 0x4C:
        if (!(b & 1)) {                 /* reset: every voice stopped, every IRQ gone */
            for (int i = 0; i < 32; i++) g.v[i].ctrl = g.v[i].vctrl = 3;
            g.dma_ctrl = g.dma_tc = 0; g.timer_ctrl = 0; g.t_fired[0] = g.t_fired[1] = 0;
            g.active = 14;
        }
        g.reset = b;
        update_irq();
        break;
    default: trace("[gus] register %02X = %04X\n", g.reg, d);
    }
}

static uint16_t read_reg(void)
{
    Voice *v = &g.v[g.voice & 31];
    switch (g.reg) {
    case 0x80: return (uint16_t)(v->ctrl << 8);
    case 0x81: return v->fc;
    case 0x82: return (uint16_t)(v->start >> 16);
    case 0x83: return (uint16_t)v->start;
    case 0x84: return (uint16_t)(v->end >> 16);
    case 0x85: return (uint16_t)v->end;
    case 0x86: return (uint16_t)(v->rate << 8);
    case 0x87: return (uint16_t)(v->rstart << 8);
    case 0x88: return (uint16_t)(v->rend << 8);
    case 0x89: return v->vol;
    case 0x8A: return (uint16_t)(v->cur >> 16);
    case 0x8B: return (uint16_t)v->cur;
    case 0x8C: return (uint16_t)(v->pan << 8);
    case 0x8D: return (uint16_t)(v->vctrl << 8);
    case 0x8E: return (uint16_t)((g.active - 1) << 8);
    case 0x8F: {                        /* which voice wants servicing: bit 7 clear for its end, 6 for its ramp */
        for (int i = 0; i < g.active; i++) {
            Voice *w = &g.v[i];
            if ((w->ctrl | w->vctrl) & 0x80) {
                uint8_t r = (uint8_t)(i | 0xE0);
                if (w->ctrl & 0x80) r &= (uint8_t)~0x80;
                if (w->vctrl & 0x80) r &= (uint8_t)~0x40;
                w->ctrl &= 0x7F; w->vctrl &= 0x7F;
                update_irq();
                return (uint16_t)(r << 8);
            }
        }
        return 0xE000;
    }
    case 0x41: { uint8_t r = (uint8_t)(g.dma_ctrl | (g.dma_tc ? 0x40 : 0)); g.dma_tc = 0; update_irq(); return (uint16_t)(r << 8); }
    case 0x45: return (uint16_t)(g.timer_ctrl << 8);
    case 0x49: return 0;
    case 0x4C: return (uint16_t)(g.reset << 8);
    }
    return 0;
}

/* ---- ports ------------------------------------------------------------------- */

int gus_port_in(uint16_t port, uint8_t *v)
{
    if (!g.on) return 0;
    uint16_t o = (uint16_t)(port - g.base);
    switch (o) {
    case 0x006: *v = irq_status(); return 1;
    case 0x008: *v = (uint8_t)((g.t_fired[0] ? 0x40 : 0) | (g.t_fired[1] ? 0x20 : 0)
                               | (g.t_fired[0] || g.t_fired[1] ? 0x80 : 0)); return 1;
    case 0x009: case 0x00A: case 0x00B: case 0x000: *v = 0xFF; return 1;
    case 0x102: *v = g.voice; return 1;
    case 0x103: *v = g.readback; return 1;
    case 0x104: *v = (uint8_t)read_reg(); return 1;
    case 0x105: *v = (uint8_t)(read_reg() >> 8); return 1;
    case 0x107: *v = g.ram[g.peek & (RAM - 1)]; return 1;
    }
    return o < 0x10 || (o >= 0x100 && o < 0x110);
}

int gus_port_out(uint16_t port, uint8_t v)
{
    if (!g.on) return 0;
    uint16_t o = (uint16_t)(port - g.base);
    switch (o) {
    case 0x000: g.mix = v; return 1;
    case 0x008: g.adlib_sel = v; return 1;
    case 0x009:
        if (g.adlib_sel == 4) {
            if (v & 0x80) { g.t_fired[0] = g.t_fired[1] = 0; update_irq(); return 1; }
            g.adlib = v;
            for (int t = 0; t < 2; t++) {
                int start = (v >> t) & 1;
                if (start && !g.t_run[t]) g.t_us[t] = 0;
                g.t_run[t] = start;
            }
        }
        return 1;
    case 0x102: g.voice = v; return 1;
    case 0x103: g.reg = v; g.readback = v; g.data = 0; return 1;
    case 0x104: g.data = (uint16_t)((g.data & 0xFF00) | v); return 1;
    case 0x105: g.data = (uint16_t)((g.data & 0x00FF) | v << 8); g.readback = v; write_reg(); return 1;
    case 0x107: g.ram[g.peek & (RAM - 1)] = v; return 1;
    }
    return o < 0x10 || (o >= 0x100 && o < 0x110);
}

/* ---- the driver's settings ---------------------------------------------------- */

void gus_configure(int on, uint16_t base, int irq, int dma)
{
    g.on = on;
    if (!on) return;
    if (!g.ram) g.ram = calloc(RAM, 1);
    if (base >= 0x210 && base <= 0x260 && !(base & 0xF)) g.base = base;
    if (irq >= 2 && irq <= 15) g.irq = irq;
    if (dma >= 0 && dma <= 7 && dma != 4) g.dma = dma;
    trace("[gus] port %03X, IRQ %d, DMA %d\n", g.base, g.irq, g.dma);
}

/* ---- the sound ------------------------------------------------------------------- */

static int sample(const Voice *v, uint32_t a)
{
    uint32_t i = a >> 9;
    if (v->ctrl & 4) {                  /* 16-bit: the GF1's address translation */
        uint32_t p = ((i & 0xC0000) | ((i & 0x1FFFF) << 1)) & (RAM - 2);
        return (int16_t)(g.ram[p] | g.ram[p + 1] << 8);
    }
    return (int8_t)g.ram[i & (RAM - 1)] * 256;
}

/* one voice's position past an end: loop (or turn) or stop; its IRQ */
static void wave_end(Voice *v)
{
    if (v->ctrl & 0x20) v->ctrl |= 0x80;
    if (!(v->ctrl & 8)) { v->ctrl |= 1; v->cur = (v->ctrl & 0x40) ? v->start : v->end; return; }
    if (v->ctrl & 0x10) {
        v->ctrl ^= 0x40;
        v->cur = (v->ctrl & 0x40) ? v->end - (v->cur - v->end) : v->start + (v->start - v->cur);
    } else if (v->ctrl & 0x40) v->cur = v->end - (v->start - v->cur);
    else v->cur = v->start + (v->cur - v->end);
}

static void ramp(Voice *v)
{
    if (v->vctrl & 3) return;
    if (v->ramp_wait > 0) { v->ramp_wait--; return; }
    static const int every[4] = { 1, 8, 64, 512 };
    v->ramp_wait = every[v->rate >> 6] - 1;
    int vol = v->vol >> 4, step = v->rate & 0x3F, lo = v->rstart << 4, hi = v->rend << 4;
    int end = 0;
    if (v->vctrl & 0x40) { vol -= step; if (vol <= lo) { vol = lo; end = 1; } }
    else { vol += step; if (vol >= hi) { vol = hi; end = 1; } }
    v->vol = (uint16_t)(vol << 4);
    if (!end) return;
    if (v->vctrl & 0x20) v->vctrl |= 0x80;
    if (v->vctrl & 8) {
        if (v->vctrl & 0x10) v->vctrl ^= 0x40;
        else v->vol = (uint16_t)(((v->vctrl & 0x40) ? hi : lo) << 4);
    } else v->vctrl |= 1;
}

/* one frame of the card's output */
static void frame(int *l, int *r)
{
    static double pan_l[16], pan_r[16];
    if (pan_l[0] == 0) for (int p = 0; p < 16; p++) {
        pan_l[p] = cos(p / 15.0 * QUARTER_TURN); pan_r[p] = sin(p / 15.0 * QUARTER_TURN);
    }
    double sl = 0, sr = 0;
    for (int i = 0; i < g.active; i++) {
        Voice *v = &g.v[i];
        ramp(v);
        if (v->ctrl & 3) continue;
        int s0 = sample(v, v->cur), s1 = sample(v, v->cur + (1u << 9));
        double s = s0 + (s1 - s0) * ((v->cur & 0x1FF) / 512.0);      /* the GF1 interpolates */
        s *= gain(v->vol);
        sl += s * pan_l[v->pan];
        sr += s * pan_r[v->pan];
        uint32_t step = v->fc >> 1;
        if (v->ctrl & 0x40) {
            v->cur -= step;
            if ((int32_t)v->cur <= (int32_t)v->start) wave_end(v);
        } else {
            v->cur += step;
            if (v->cur >= v->end) wave_end(v);
        }
    }
    *l = (int)sl; *r = (int)sr;
}

static void timers(double us)
{
    static const double unit[2] = { 80.0, 320.0 };
    for (int t = 0; t < 2; t++) {
        if (!g.t_run[t]) continue;
        double period = (256 - g.t_count[t]) * unit[t];
        g.t_us[t] += us;
        while (g.t_us[t] >= period) {
            g.t_us[t] -= period;
            if ((g.timer_ctrl & (4 << t)) && !(g.adlib & (t ? 0x20 : 0x40))) g.t_fired[t] = 1;
        }
    }
    update_irq();
}

/* The card's sound for one host frame (AUDIO_RATE), added to *l and *r */
void gus_sample(int *l, int *r)
{
    if (!g.on) return;
    timers(1e6 / AUDIO_RATE);
    if (!(g.reset & 1)) return;
    g.frames += 617400.0 / g.active / AUDIO_RATE;
    while (g.frames >= 1.0) {
        g.frames -= 1.0;
        g.pl = g.l; g.pr = g.r;
        frame(&g.l, &g.r);
    }
    if (!(g.reset & 2) || (g.mix & 2)) return;      /* DAC off, or line out off */
    /* at about the Sound Blaster's loudness for the same music */
    *l += (int)((g.pl + (g.l - g.pl) * g.frames) / 4);
    *r += (int)((g.pr + (g.r - g.pr) * g.frames) / 4);
}
