/*
 * pas.c - a Media Vision Pro Audio Spectrum, as the MASI PAS driver
 * (MDRV005R.MUS) uses it, and the part of MVSOUND.SYS, Media Vision's
 * driver, that it asks.
 *
 * The driver mixes, as the Sound Blaster one does, and the card plays the
 * stream: 8-bit samples by DMA at the rate of the card's own 8254 timer 0
 * (1,193,180 / its count: a sample of each channel in turn, in stereo). It
 * never takes the card's interrupt (its handler is an iret); it mixes when
 * PINBALL polls it, as far as the DMA channel's count says the card has got.
 *
 * Its registers are where the driver's settings put the card (388h, 384h,
 * 38Ch or 288h, SETUP's choice): each at `register ^ 388h ^ base`. Those it
 * touches: B89h interrupt status, B8Ah audio filter (bit 6: the sample rate
 * timer runs), B8Bh interrupt control, F8Ah cross-channel control (bit 5
 * mono, bit 7 PCM on), 1388h timer 0's count, 138Bh the 8254's control.
 *
 * MVSOUND.SYS answers INT 2Fh: BC00h is it there (BX ^ CX ^ DX is "MV"),
 * BC02h where its table of the card's register values is, BC04h the card's
 * DMA channel (BL) and IRQ (CL). Those are its, not SETUP's.
 */
#include "machine.h"

#define PAS_DMA 1
#define PAS_IRQ 10
#define TABLE_SEG 0x0050             /* MVSOUND's table: in DOS's own area, 0500h-05FFh, at 0520h */
#define TABLE_OFF 0x0020

static struct {
    int on;
    uint16_t base;
    uint8_t filter, int_ctrl, cross, timer_ctrl;
    uint16_t count;
    int count_hi;                    /* the next byte of 1388h is the count's high one */
    double phase;
    int cur, cur_r, prev, prev_r;
} p = { .base = 0x388, .count = 1193180 / 22050, .cur = 128, .cur_r = 128, .prev = 128, .prev_r = 128 };

void pas_configure(int on, uint16_t base)
{
    p.on = on;
    if (!on) return;
    if (base == 0x388 || base == 0x384 || base == 0x38C || base == 0x288) p.base = base;
    trace("[pas] port %03X, DMA %d, IRQ %d\n", p.base, PAS_DMA, PAS_IRQ);
}

/* the register a port is, or 0 */
static uint16_t reg(uint16_t port)
{
    uint16_t r = (uint16_t)(port ^ p.base ^ 0x388);
    switch (r) {
    case 0x0B89: case 0x0B8A: case 0x0B8B: case 0x0F8A: case 0x1388: case 0x1389: case 0x138A: case 0x138B:
    case 0x8389: case 0x0F88: case 0x0F8B: case 0x0B88: case 0x2789: case 0x7F89:
        return r;
    }
    return 0;
}

int pas_port_in(uint16_t port, uint8_t *v)
{
    if (!p.on) return 0;
    switch (reg(port)) {
    case 0: return 0;
    case 0x0B89: *v = 0; return 1;
    case 0x0B8A: *v = p.filter; return 1;
    case 0x0B8B: *v = p.int_ctrl; return 1;
    case 0x0F8A: *v = p.cross; return 1;
    case 0x138B: *v = p.timer_ctrl; return 1;
    default: *v = 0; return 1;
    }
}

int pas_port_out(uint16_t port, uint8_t v)
{
    if (!p.on) return 0;
    switch (reg(port)) {
    case 0: return 0;
    case 0x0B8A: p.filter = v; return 1;
    case 0x0B8B: p.int_ctrl = v; return 1;
    case 0x0F8A:
        if ((v & 0x80) && !(p.cross & 0x80))
            trace("[pas] PCM %s at %u Hz\n", (v & 0x20) ? "mono" : "stereo", 1193180u / (p.count ? p.count : 1));
        p.cross = v;
        return 1;
    case 0x138B: p.timer_ctrl = v; if (((v >> 6) & 3) == 0) p.count_hi = 0; return 1;
    case 0x1388:
        if (!p.count_hi) p.count = (uint16_t)((p.count & 0xFF00) | v);
        else p.count = (uint16_t)((p.count & 0x00FF) | v << 8);
        p.count_hi ^= 1;
        return 1;
    default: return 1;               /* B89h, a write clears the interrupt status; the rest unused */
    }
}

/* MVSOUND.SYS on INT 2Fh: 1 if the call was its */
int pas_mvsound(CPU *c)
{
    switch (c->ax) {
    case 0xBC00: c->bx = 0x4D56; c->cx = 0; c->dx = 0; return 1;             /* "MV" */
    case 0xBC02: c->ax = 0x4D56; c->bx = TABLE_OFF; c->dx = TABLE_SEG; return 1;
    case 0xBC04: c->bx = (uint16_t)((c->bx & 0xFF00) | PAS_DMA); c->cx = (uint16_t)((c->cx & 0xFF00) | PAS_IRQ); return 1;
    }
    return 0;
}

static int pcm_byte(int *v)
{
    uint8_t b;
    if (!audio_dma_read8(PAS_DMA, &b)) return 0;
    *v = b;
    return 1;
}

/* The card's output for one host frame, added to *l and *r */
void pas_sample(int *l, int *r)
{
    if (!p.on || !(p.cross & 0x80) || !(p.filter & 0x40) || !p.count) return;
    int stereo = !(p.cross & 0x20);
    /* the timer counts a sample of each channel in stereo: frames at half its rate */
    p.phase += 1193180.0 / p.count / (stereo ? 2 : 1) / AUDIO_RATE;
    while (p.phase >= 1.0) {
        p.phase -= 1.0;
        p.prev = p.cur; p.prev_r = p.cur_r;
        if (!pcm_byte(&p.cur)) break;
        p.cur_r = p.cur;
        if (stereo && !pcm_byte(&p.cur_r)) break;
    }
    double f = g_audio_smooth ? p.phase : 1.0;
    *l += (int)((p.prev + (p.cur - p.prev) * f - 128) * 96);
    *r += (int)((p.prev_r + (p.cur_r - p.prev_r) * f - 128) * 96);
}
