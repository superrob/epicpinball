/*
 * audio.c - what Epic Pinball can make noise with:
 *
 *   Sound Blaster  DSP at 220h, 8-bit DMA on channel 1, IRQ 7 -- the BLASTER
 *               line dos.c puts in the environment. The MASI driver mixes the
 *               music (.PSM modules) and the table's effects itself and feeds
 *               the DSP a block at a time; each finished block raises the IRQ.
 *   PC speaker  PIT channel 2 gated through port 61h.
 *
 * There is no FM synthesis to model: MASI plays samples. Everything is
 * rendered only as far as emulated time has advanced (machine.c calls
 * audio_advance before each timer IRQ), so a DSP command lands in the stream
 * where the guest made it.
 */
#include "machine.h"
#include "ssf.h"

static uint64_t rendered;             /* samples so far */

/* ---- 8237 DMA ------------------------------------------------------------ */

/* Channels 0-3 move bytes; 4-7, the second controller (C0h-DFh), words. A
 * Gravis Ultrasound's default is channel 5. */
static struct { uint16_t base_addr, base_count, addr, count; uint8_t page, mode, masked; } dma[8];
static int dma_ff, dma_ff2;
static const uint16_t dma_page_port[8] = { 0x87, 0x83, 0x81, 0x82, 0x8F, 0x8B, 0x89, 0x8A };

static int dma2_port_out(uint16_t port, uint8_t v)
{
    if (port < 0xC0 || port > 0xDF || (port & 1)) return 0;
    unsigned r = (port - 0xC0) >> 1;                   /* the controller's registers, one per even port */
    if (r < 8) {
        int ch = 4 + (r >> 1);
        uint16_t *b = (r & 1) ? &dma[ch].base_count : &dma[ch].base_addr;
        *b = dma_ff2 ? (uint16_t)((*b & 0xFF) | v << 8) : (uint16_t)((*b & 0xFF00) | v);
        dma_ff2 ^= 1;
        if (r & 1) dma[ch].count = dma[ch].base_count; else dma[ch].addr = dma[ch].base_addr;
        return 1;
    }
    switch (r) {
    case 0xA: dma[4 + (v & 3)].masked = (v >> 2) & 1; return 1;
    case 0xB: dma[4 + (v & 3)].mode = v; return 1;
    case 0xC: dma_ff2 = 0; return 1;
    case 0xD: dma_ff2 = 0; for (int i = 4; i < 8; i++) dma[i].masked = 1; return 1;
    case 0xF: for (int i = 0; i < 4; i++) dma[4 + i].masked = (v >> i) & 1; return 1;
    }
    return 1;
}

static int dma2_port_in(uint16_t port, uint8_t *v)
{
    if (port < 0xC0 || port > 0xDF || (port & 1)) return 0;
    unsigned r = (port - 0xC0) >> 1;
    if (r < 8) {
        int ch = 4 + (r >> 1);
        uint16_t w = (r & 1) ? dma[ch].count : dma[ch].addr;
        *v = dma_ff2 ? (uint8_t)(w >> 8) : (uint8_t)w;
        dma_ff2 ^= 1;
        return 1;
    }
    *v = 0;
    return 1;
}

static int dma_port_out(uint16_t port, uint8_t v)
{
    if (dma2_port_out(port, v)) return 1;
    if (port < 8) {
        int ch = port >> 1;
        uint16_t *b = (port & 1) ? &dma[ch].base_count : &dma[ch].base_addr;
        *b = dma_ff ? (uint16_t)((*b & 0xFF) | v << 8) : (uint16_t)((*b & 0xFF00) | v);
        dma_ff ^= 1;
        if (port & 1) dma[ch].count = dma[ch].base_count; else dma[ch].addr = dma[ch].base_addr;
        return 1;
    }
    switch (port) {
    case 0x08: case 0x09: case 0x0E: return 1;
    case 0x0A: dma[v & 3].masked = (v >> 2) & 1; return 1;
    case 0x0B:
        dma[v & 3].mode = v;
        trace("[dma] ch%d mode %02X addr %04X count %04X page %02X\n", v & 3, v,
              dma[v & 3].base_addr, dma[v & 3].base_count, dma[v & 3].page);
        return 1;
    case 0x0C: dma_ff = 0; return 1;
    case 0x0D: dma_ff = 0; for (int i = 0; i < 4; i++) dma[i].masked = 1; return 1;
    case 0x0F: for (int i = 0; i < 4; i++) dma[i].masked = (v >> i) & 1; return 1;
    }
    for (int i = 0; i < 8; i++) if (port == dma_page_port[i]) { dma[i].page = v; return 1; }
    return 0;
}

static int dma_port_in(uint16_t port, uint8_t *v)
{
    if (dma2_port_in(port, v)) return 1;
    if (port < 8) {
        int ch = port >> 1;
        uint16_t w = (port & 1) ? dma[ch].count : dma[ch].addr;
        *v = dma_ff ? (uint8_t)(w >> 8) : (uint8_t)w;
        dma_ff ^= 1;
        return 1;
    }
    if (port == 0x08) { *v = 0; return 1; }
    return 0;
}

/* one word through a 16-bit channel (4-7): it addresses words, within
 * 128 KB pages; returns 0 if the channel is not running */
int audio_dma_read16(int ch, uint16_t *w)
{
    if (ch < 4 || ch > 7 || dma[ch].masked) return 0;
    uint32_t a = (((uint32_t)(dma[ch].page & 0xFE) << 16) | (uint32_t)dma[ch].addr << 1) & 0xFFFFE;
    *w = (uint16_t)(g_cpu.mem[a] | g_cpu.mem[a + 1] << 8);
    dma[ch].addr++;
    if (dma[ch].count-- == 0) {
        if (dma[ch].mode & 0x10) { dma[ch].addr = dma[ch].base_addr; dma[ch].count = dma[ch].base_count; }
        else dma[ch].masked = 1;
    }
    return 1;
}

/* one byte through a DMA channel; returns 0 if the channel is not running */
static int dma_read(int ch, uint8_t *b)
{
    if (dma[ch].masked) return 0;
    *b = g_cpu.mem[((uint32_t)dma[ch].page << 16 | dma[ch].addr) & 0xFFFFF];
    dma[ch].addr++;
    if (dma[ch].count-- == 0) {
        if (dma[ch].mode & 0x10) { dma[ch].addr = dma[ch].base_addr; dma[ch].count = dma[ch].base_count; }
        else dma[ch].masked = 1;
    }
    return 1;
}

int audio_dma_read8(int ch, uint8_t *b) { return ch >= 0 && ch < 4 && dma_read(ch, b); }

/* The card the loaded driver is for: only it answers on its ports (a Gravis
 * and a Sound Blaster are both at 220h by default). The Sound Blaster until a
 * driver says otherwise, as GOG's setting has it. */
enum { CARD_SB, CARD_GUS, CARD_PAS, CARD_NONE };
static int card = CARD_SB;

/* ---- Sound Blaster DSP ------------------------------------------------------ */

/* Where the card is: the settings SETUP wrote into the driver PINBALL loaded
 * (audio_driver), GOG's 220h, IRQ 7, DMA 1 until then. */
static uint16_t sb_base = 0x220;
static int sb_irq = 7, sb_dma = 1;
static struct {
    uint8_t q[16]; int qh, qt;            /* bytes for the read-data port */
    int reset;
    uint8_t cmd; int need, got; uint8_t arg[4];
    uint32_t rate; int speaker;           /* rate: frames per second */
    int playing, autoinit, silence;
    int stereo, is_signed;                /* SB16 mode byte (C0h-CFh), or SB Pro mixer 0Eh */
    uint32_t len, pos;                    /* the DSP's block, in bytes */
    double phase; int cur, cur_r, prev, prev_r;
    int irq;
    uint8_t mixer_i, mixer[256];
    uint8_t test;
} sb = { .rate = 11025, .cur = 128, .cur_r = 128, .prev = 128, .prev_r = 128 };

int g_audio_smooth = 1;

int  sb_irq_pending(void) { return sb.irq; }
int  sb_irq_line(void) { return sb_irq; }
void sb_irq_ack(void) { sb.irq = 0; }

static void sb_put(uint8_t v) { sb.q[sb.qt] = v; sb.qt = (sb.qt + 1) & 15; }

static void sb_command(void)
{
    uint8_t *a = sb.arg;
    if (sb.cmd != 0x14) trace("[sb] DSP %02X\n", sb.cmd);
    switch (sb.cmd) {
    case 0x10: sb.cur = a[0]; break;
    case 0x14: case 0x91:                              /* 91h: block size from 48h */
        if (sb.cmd == 0x14) sb.len = (uint32_t)(a[0] | a[1] << 8) + 1;
        sb.pos = 0; sb.playing = 1; sb.autoinit = 0; sb.silence = 0;
        trace("[sb] play %u bytes at %u Hz from %X\n", sb.len, sb.rate, (unsigned)dma[sb_dma].page << 16 | dma[sb_dma].addr);
        break;
    case 0x1C: case 0x90:
        /* SB Pro stereo: the mixer's 0Eh bit 1, at half the time-constant rate */
        sb.stereo = (sb.mixer[0x0E] & 2) != 0; sb.is_signed = 0;
        sb.pos = 0; sb.playing = 1; sb.autoinit = 1; sb.silence = 0; break;
    case 0x48: sb.len = (uint32_t)(a[0] | a[1] << 8) + 1; break;
    case 0x40: sb.rate = 1000000u / (256u - a[0]); break;
    case 0x41: case 0x42: sb.rate = (uint32_t)(a[0] << 8 | a[1]); break;   /* SB16: rate in Hz */
    case 0xC0: case 0xC2: case 0xC4: case 0xC6:                         /* SB16 8-bit output */
        /* MASI's SB16 path: C6h (auto-init, FIFO), mode 20h = stereo unsigned,
         * then the block length; 8-bit DMA on channel 1 like the older cards */
        sb.stereo = (a[0] >> 5) & 1; sb.is_signed = (a[0] >> 4) & 1;
        sb.len = (uint32_t)(a[1] | a[2] << 8) + 1;
        sb.pos = 0; sb.playing = 1; sb.silence = 0; sb.autoinit = (sb.cmd & 4) != 0;
        trace("[sb] SB16 %s %s at %u Hz, %u-byte blocks\n", sb.stereo ? "stereo" : "mono",
              sb.autoinit ? "auto-init" : "single", sb.rate, sb.len);
        break;
    case 0xD9: case 0xDA: sb.autoinit = 0; break;
    case 0x80: sb.len = (uint32_t)(a[0] | a[1] << 8) + 1; sb.pos = 0; sb.playing = 1; sb.silence = 1; sb.autoinit = 0; break;
    case 0xD0: sb.playing = 0; break;
    case 0xD4: sb.playing = 1; break;
    case 0xD1: sb.speaker = 1; break;
    case 0xD3: sb.speaker = 0; break;
    case 0xD8: sb_put(sb.speaker ? 0xFF : 0); break;
    case 0xE0: sb_put((uint8_t)~a[0]); break;
    case 0xE1: sb_put(4); sb_put(5); break;       /* SB16, DSP 4.05 */
    case 0xE4: sb.test = a[0]; break;
    case 0xE8: sb_put(sb.test); break;
    case 0xF2: sb.irq = 1; break;
    case 0x20: sb_put(0x80); break;
    default: trace("[sb] DSP command %02X\n", sb.cmd);
    }
}

static int sb_args(uint8_t c)
{
    switch (c) {
    case 0x10: case 0x40: case 0xE0: case 0xE4: return 1;
    case 0x14: case 0x16: case 0x17: case 0x48: case 0x80: case 0x41: case 0x42: return 2;
    }
    if (c >= 0xB0 && c <= 0xCF) return 3;              /* SB16: mode, length lo, hi */
    return 0;
}

/* one byte of the block; 0 when the block (and a single-cycle transfer) ends */
static int sb_byte(int *v)
{
    uint8_t b = 128;
    if (!sb.silence && !dma_read(sb_dma, &b)) return 0;
    *v = sb.is_signed ? (int)(b ^ 0x80) : b;
    if (++sb.pos >= sb.len) {
        sb.irq = 1;
        if (sb.autoinit) sb.pos = 0;
        else { sb.playing = 0; return 0; }
    }
    return 1;
}

/* the DSP's output for one host frame: left and right. The DSP runs at 22 kHz
 * and the host at 44.1: smooth draws a line between its samples, otherwise
 * each is held -- steps the card's analogue output filtered away. */
static void sb_sample(int *l, int *r)
{
    *l = *r = 0;
    if (!sb.playing && sb.cur == 128 && sb.cur_r == 128) return;
    sb.phase += (double)sb.rate / AUDIO_RATE;
    while (sb.phase >= 1.0 && sb.playing) {
        sb.phase -= 1.0;
        sb.prev = sb.cur; sb.prev_r = sb.cur_r;
        if (!sb_byte(&sb.cur)) break;
        sb.cur_r = sb.cur;
        if (sb.stereo && !sb_byte(&sb.cur_r)) break;
    }
    if (!sb.speaker) return;
    double f = g_audio_smooth && sb.playing ? sb.phase : 1.0;
    *l = (int)((sb.prev + (sb.cur - sb.prev) * f - 128) * 96);
    *r = (int)((sb.prev_r + (sb.cur_r - sb.prev_r) * f - 128) * 96);
}

/* ---- PC speaker --------------------------------------------------------- */

static int spk_on;
static double spk_phase, spk_step;

void audio_speaker(int gate, uint16_t divisor)
{
    spk_on = gate && divisor > 16;
    spk_step = spk_on ? (PIT_HZ / divisor) / AUDIO_RATE : 0;
}

/* ---- ports ---------------------------------------------------------------- */

int audio_port_in(uint16_t port, uint8_t *v)
{
    if (dma_port_in(port, v)) return 1;
    if (port == 0x388 || port == 0x389) { *v = 0xFF; return 1; }     /* no FM chip */
    if (card == CARD_GUS) return gus_port_in(port, v);
    if (card == CARD_PAS) return pas_port_in(port, v);
    if (card != CARD_SB || port - sb_base >= 0x10u) return 0;
    switch (port - sb_base) {
    case 0x8: case 0x9: *v = 0xFF; return 1;
    case 0x5: *v = sb.mixer[sb.mixer_i]; return 1;
    case 0xA:
        if (sb.qh != sb.qt) { *v = sb.q[sb.qh]; sb.qh = (sb.qh + 1) & 15; } else *v = 0xFF;
        return 1;
    case 0xC: *v = 0x00; return 1;                     /* ready for a command */
    case 0xE: *v = (sb.qh != sb.qt) ? 0x80 : 0x00; sb.irq = 0; return 1;
    }
    return 0;
}

int audio_port_out(uint16_t port, uint8_t v)
{
    if (dma_port_out(port, v)) return 1;
    if (port == 0x388 || port == 0x389) return 1;
    if (card == CARD_GUS) return gus_port_out(port, v);
    if (card == CARD_PAS) return pas_port_out(port, v);
    if (card != CARD_SB || port - sb_base >= 0x10u) return 0;
    switch (port - sb_base) {
    case 0x8: case 0x9: return 1;
    case 0x4: sb.mixer_i = v; return 1;
    case 0x5: trace("[sb] mixer %02X = %02X\n", sb.mixer_i, v); sb.mixer[sb.mixer_i] = v; return 1;
    case 0x6:
        if (v & 1) sb.reset = 1;
        else if (sb.reset) { sb.reset = 0; sb.qh = sb.qt = 0; sb.playing = 0; sb.need = 0; sb_put(0xAA); }
        return 1;
    case 0xC:
        if (sb.need) { sb.arg[sb.got++] = v; if (--sb.need == 0) sb_command(); }
        else { sb.cmd = v; sb.got = 0; sb.need = sb_args(v); if (!sb.need) sb_command(); }
        return 1;
    }
    return 0;
}

/* ---- rendering -------------------------------------------------------------- */

void audio_init(void) {}

/* A MASI driver PINBALL has just read into memory. Its settings (SETUP's
 * menus: 40h bytes at the offset its header gives at 30h) say where its card
 * is; the card answers there. */
void audio_driver(const char *file, const uint8_t *d, uint32_t len)
{
    if (len < 0x34 || memcmp(d + 1, "REAL16", 6)) return;
    uint32_t s = (uint32_t)(d[0x30] | d[0x31] << 8 | d[0x32] << 16 | (uint32_t)d[0x33] << 24);
    if (!s || s + 0x40 > len) return;               /* the mixer (MDRV000R) has no settings: not a card */
    const uint8_t *c = d + s;
    /* every MASI card driver keeps them alike: port at +19h, IRQ +1Dh, DMA +2Dh */
    card = !memcmp(d + 8, "MASISBSERIES", 12) ? CARD_SB : !memcmp(d + 8, "MASIULTRASND", 12) ? CARD_GUS
         : !memcmp(d + 8, "MASIPASERIES", 12) ? CARD_PAS : CARD_NONE;
    gus_configure(card == CARD_GUS, (uint16_t)(c[0x19] | c[0x1A] << 8), c[0x1D], c[0x2D]);
    pas_configure(card == CARD_PAS, (uint16_t)(c[0x19] | c[0x1A] << 8));   /* its DMA and IRQ: MVSOUND.SYS's */
    if (card == CARD_SB) {
        uint16_t port = (uint16_t)(c[0x19] | c[0x1A] << 8);
        if (port >= 0x210 && port <= 0x280 && !(port & 0xF)) sb_base = port;
        if (c[0x1D] >= 2 && c[0x1D] <= 15) sb_irq = c[0x1D];
        if (c[0x2D] <= 3) sb_dma = c[0x2D];
        trace("[sb] %s: port %03X, IRQ %d, DMA %d, %u Hz mixing\n", file, sb_base, sb_irq, sb_dma,
              (unsigned)(c[0x31] | c[0x32] << 8));
    }
}

void audio_advance(uint64_t pit_ticks)
{
    static int16_t buf[2048 * 2];
    uint64_t want = (uint64_t)((double)pit_ticks * AUDIO_RATE / PIT_HZ);
    while (rendered < want) {
        int n = (int)((want - rendered) > 2048 ? 2048 : (want - rendered));
        for (int i = 0; i < n; i++) {
            int l, r;
            sb_sample(&l, &r);
            gus_sample(&l, &r);
            pas_sample(&l, &r);
            if (spk_on) {
                spk_phase += spk_step; if (spk_phase >= 1) spk_phase -= 1;
                int s = spk_phase < 0.5 ? 3000 : -3000;
                l += s; r += s;
            }
            if (g_opt.mute) l = r = 0;
            if (l > 32767) l = 32767; if (l < -32768) l = -32768;
            if (r > 32767) r = 32767; if (r < -32768) r = -32768;
            buf[i * 2] = (int16_t)l; buf[i * 2 + 1] = (int16_t)r;
        }
        if (g_opt.ssf) {
            static int16_t ex[2048 * 4];
            ssf_render(ex, n);
            if (g_opt.mute) memset(ex, 0, sizeof ex);
            host_audio_ssf(buf, ex, n);
        } else host_audio(buf, n);
        rendered += n;
    }
}

void audio_shutdown(void) {}
