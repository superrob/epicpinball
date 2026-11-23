/*
 * ssf.c - --ssf: the board's sounds on a cabinet's exciters. See ssf.h and
 * docs/ssf.md.
 *
 * A table plays an effect through one function (AL the effect, AH a pan
 * nibble), which asks the Sound Blaster driver for a voice: the driver's
 * setup gets the sample's header in ECX and the class in DL (1 for
 * effects), then sets the voice's volume (AL the voice, CL the volume). For
 * an effect that is the board's, the setup starts a voice here from the same
 * header, and the volume CL becomes 0: the driver mixes it silently, and stops
 * it when it would have, and so do we. Where it is heard comes from the table:
 * a fixed pan (flippers, plunger) at the bottom, else the kicker hit or the
 * switch just handled (watch.c), else the ball.
 */
#include "ssf.h"
#include "machine.h"
#include "watch.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_ssf_on;
static int wanted;
static const WatchMap *M;
static uint32_t DS;
static uint32_t board;                /* this table's board effects: a bit a slot */
static double vol_effects = SSF_GAIN, vol_rolling = SSF_GAIN;

#define PLACE_MS 300.0                /* a kicker hit or switch this recent placed the effect */
#define EFFECT_FULL 16000.0           /* a full-scale sample at the driver's full volume: as loud as the game's own mix */
#define TABLE_W 320.0
#define TABLE_H 400.0

static double now_ms(void) { return pit_now() * 1000.0 / PIT_HZ; }
static uint16_t rd16(uint32_t a) { return (uint16_t)(g_cpu.mem[a] | g_cpu.mem[a + 1] << 8); }
static uint32_t rd32(uint32_t a) { return (uint32_t)rd16(a) | (uint32_t)rd16(a + 2) << 16; }

/* ---- where ------------------------------------------------------------------- */

void ssf_gains(double x, double y, float g[4])
{
    /* vpinball's (src/audio/MixNode.cpp): linear across, linear down */
    double pan = x * 2 / TABLE_W - 1, fade = y * 2 / TABLE_H - 1;
    if (pan < -1) pan = -1; if (pan > 1) pan = 1;
    if (fade < -1) fade = -1; if (fade > 1) fade = 1;
    const double left = 0.5 * (1 - pan), right = 0.5 * (1 + pan), bottom = 0.5 * (1 + fade), top = 1 - bottom;
    g[0] = (float)(bottom * left); g[1] = (float)(bottom * right);
    g[2] = (float)(top * left);    g[3] = (float)(top * right);
}

/* ---- the board's effects ------------------------------------------------------ */

#define VOICES 16
typedef struct {
    int on, masi;                     /* the driver's voice it shadows; -1 until its volume is set */
    uint32_t at, len;                 /* the PCM: unsigned 8-bit, centred on 80h */
    int loop;
    uint32_t loop_from, loop_to;
    double pos, step;
    uint8_t vol;                      /* the sample's, 0-255 */
    float g[4];
    uint32_t hdr;
} Voice;

static Voice voice[VOICES];
static struct { int on, effect, x, y; double t; } pending;   /* the effect function, before the driver */
static uint32_t muting;               /* the header whose voice's volume is to be 0 */
static uint32_t driver;               /* the sound driver's data: its master volume for effects at 2C4h */

static const char *placed_by;         /* EPIC_SSF_CHECK: why there */

/* Where an effect is heard. Bumpers and slingshots (slots 0 and 2) are the
 * kicker's: its last hit, though a switch was handled since; the thud (3)
 * is the ball's, against a wall; the others are a switch's, or else the
 * kicker's. A fixed pan is the flippers' or the plunger's, at the bottom. */
static void place(int effect, int nibble, int *x, int *y)
{
    if (nibble) {
        *x = (int)(160 + (nibble * 16 - 128) * 160 / 128);
        *y = 380;
        placed_by = "its pan";
        return;
    }
    if (effect == 0 || effect == 2) {
        if (watch_place(1, PLACE_MS, x, y)) { placed_by = "the kicker"; return; }
    } else if (effect != 3) {
        if (watch_place(0, PLACE_MS, x, y)) { placed_by = "a switch"; return; }
        if (watch_place(1, PLACE_MS, x, y)) { placed_by = "the kicker"; return; }
    }
    *x = rd16(DS + M->x); *y = rd16(DS + M->y);   /* the ball, as the game pans by it */
    placed_by = "the ball";
}

static void start(uint32_t hdr)
{
    Voice *v = NULL;
    for (int i = 0; i < VOICES && !v; i++) if (!voice[i].on) v = &voice[i];
    if (!v) {                         /* all busy: the oldest goes */
        v = &voice[0];
        for (int i = 1; i < VOICES; i++) if (voice[i].pos / (voice[i].len + 1) > v->pos / (v->len + 1)) v = &voice[i];
    }
    memset(v, 0, sizeof *v);
    v->at = rd32(hdr + 0x2D);
    v->len = rd32(hdr + 0x36);
    uint32_t rate = rd32(hdr + 0x45);
    if (v->at == 0xFFFFFFFFu || !v->len || v->at + v->len >= MEM_SIZE || rate < 1000 || rate > 64000) return;
    v->loop = (g_cpu.mem[hdr] & 0x80) != 0;
    v->loop_from = rd32(hdr + 0x3A);
    v->loop_to = rd32(hdr + 0x3E);
    if (!v->loop_to || v->loop_to > v->len) v->loop_to = v->len;
    if (v->loop_from >= v->loop_to) v->loop = 0;
    uint8_t vol = g_cpu.mem[hdr + 0x44];
    v->vol = vol == 0xFF ? 0 : vol;
    v->step = rate / (double)AUDIO_RATE;
    v->hdr = hdr;
    v->masi = -1;
    ssf_gains(pending.x, pending.y, v->g);
    v->on = 1;
}

static void stop_masi(int m)
{
    for (int i = 0; i < VOICES; i++) if (voice[i].on && voice[i].masi == m) voice[i].on = 0;
}

void ssf_enter(CPU *cpu, const char *fn)
{
    if (!strcmp(fn, M->effect_fn)) {
        pending.on = 1;
        pending.effect = cpu->al;
        pending.t = now_ms();
        place(cpu->al, cpu->ah >> 4, &pending.x, &pending.y);
    } else if (!strcmp(fn, M->sfx_setup_fn)) {
        /* the effect function asks for its voice at once; anything else (music,
         * PINBALL's own sounds) is not the board's */
        if (!pending.on || cpu->dl != 1 || now_ms() - pending.t > 50) { pending.on = 0; return; }
        pending.on = 0;
        if (pending.effect > 31 || !(board >> pending.effect & 1)) return;
        uint32_t hdr = (cpu->ecx >> 16) * 16u + (cpu->ecx & 0xFFFF);
        if (hdr + 0x60 >= MEM_SIZE) return;
        driver = (uint32_t)cpu->ds * 16;
        start(hdr);
        muting = hdr;
        if (getenv("EPIC_SSF_CHECK"))
            fprintf(stderr, "[ssf] check %.1f effect %X at %d,%d by %s\n", now_ms(), pending.effect, pending.x, pending.y, placed_by);
    } else if (!strcmp(fn, M->sfx_volume_fn)) {
        if (!muting || g_cpu.mem[muting + 0x33] != cpu->al) return;
        cpu->cl = 0;                  /* the driver mixes it silent; it plays here */
        if (getenv("EPIC_SSF_CHECK")) fprintf(stderr, "[ssf] check %.1f voice %d silent\n", now_ms(), cpu->al);
        stop_masi(cpu->al);           /* a voice it reuses was ours before */
        for (int i = 0; i < VOICES; i++)
            if (voice[i].on && voice[i].hdr == muting && voice[i].masi < 0) voice[i].masi = cpu->al;
        muting = 0;
    } else if (!strcmp(fn, M->sfx_stop_fn)) {
        if (cpu->al != 0xFF) stop_masi(cpu->al);
    } else if (!strcmp(fn, M->sfx_stop_class_fn)) {
        if (cpu->ah == 1) for (int i = 0; i < VOICES; i++) voice[i].on = 0;
    }
}

/* ---- rolling ------------------------------------------------------------------ */

/* A rolling sound for each ball: noise through two band passes in a row (a
 * narrow band, steep sides, no hiss) whose centre rises with the ball's speed
 * (ROLL_LOW_HZ slow, ROLL_HIGH_HZ at full), wobbling at the ball's turning
 * (ROLL_TURN_LOW to ROLL_TURN_HIGH times a second), its gain the speed
 * squared, eased so it never clicks */
#define BALLS 8
#define ROLL_FULL 350.0               /* a speed this fast rolls at full gain (a launch is about 300) */
#define ROLL_LEAST 40.0               /* slower than this is still */
#define ROLL_LOW_HZ 150.0
#define ROLL_HIGH_HZ 500.0
#define ROLL_Q 2.5
#define ROLL_TURN_LOW 4.0
#define ROLL_TURN_HIGH 25.0
#define ROLL_WOBBLE 0.6               /* how deep: the sound dips to 1 - this once a turn */
#define ROLL_LEVEL 40000.0
static struct { float lo1, bp1, lo2, bp2, gain, g[4]; double turn; } roll[BALLS];
static uint32_t noise = 0x1234567u;

static void roll_targets(float target[BALLS], float g[BALLS][4], int *n)
{
    *n = 0;
    if (!M) return;
    /* a prompt, a pause: the physics stops, so does the rolling; and with the
     * game's effects off (SOUND FX OFF) it plays none either */
    const int playing = watch_running() && (!M->sfx_on || g_cpu.mem[DS + M->sfx_on]);
    for (int i = 0; i < M->slots && i < BALLS; i++) {
        target[i] = 0;
        g[i][0] = g[i][1] = g[i][2] = g[i][3] = 0;
        *n = i + 1;
        if (!playing || rd16(DS + (uint16_t)(M->active + 2 * i)) != 1) continue;
        double vx = (int16_t)rd16(DS + (uint16_t)(M->vx + 2 * i)), vy = (int16_t)rd16(DS + (uint16_t)(M->vy + 2 * i));
        double speed = sqrt(vx * vx + vy * vy);
        if (speed < ROLL_LEAST) continue;
        double s = speed / ROLL_FULL;
        target[i] = (float)(s > 1 ? 1 : s * s);
        ssf_gains(rd16(DS + (uint16_t)(M->x + 2 * i)), rd16(DS + (uint16_t)(M->y + 2 * i)), g[i]);
    }
}

/* ---- the mix ------------------------------------------------------------------- */

void ssf_render(int16_t *out, int frames)
{
    static float acc[4096 * 4];
    if (frames > 4096) frames = 4096;
    memset(acc, 0, (size_t)frames * 4 * sizeof *acc);
    if (g_ssf_on) {
        /* as the driver weighs a voice: its volume times the effects' master,
         * the high byte, 0-127 */
        const uint8_t master = driver ? g_cpu.mem[driver + 0x2C4] : 0;
        for (int k = 0; k < VOICES; k++) {
            Voice *v = &voice[k];
            const float fx = (float)(vol_effects * EFFECT_FULL * (((v->vol * master) >> 8) & 0x7F) / 127.0);
            for (int i = 0; i < frames && v->on; i++) {
                uint32_t p = (uint32_t)v->pos;
                double f = v->pos - p;
                uint32_t q = p + 1 < v->len ? p + 1 : p;
                float s = ((g_cpu.mem[v->at + p] - 128) * (float)(1 - f) + (g_cpu.mem[v->at + q] - 128) * (float)f) / 128.0f;
                s *= fx;
                for (int c = 0; c < 4; c++) acc[i * 4 + c] += s * v->g[c];
                v->pos += v->step;
                if (v->loop && v->pos >= v->loop_to) v->pos -= v->loop_to - v->loop_from;
                else if (v->pos >= v->len) v->on = 0;
            }
        }
        if (vol_rolling > 0) {
            float target[BALLS], g[BALLS][4];
            int n;
            roll_targets(target, g, &n);
            const float ease = 1.0f / 2000.0f;          /* gain moves over about 45 ms */
            const float damp = (float)(1.0 / ROLL_Q);
            const double PI = 3.14159265358979;
            for (int b = 0; b < n; b++) {
                /* the band's centre and the turning follow the speed (the
                 * eased gain is its square), set for this buffer; the band's
                 * level, which grows with its centre, held: sqrt(low / centre),
                 * and its peak, Q a stage, taken out */
                const double speed = sqrt(roll[b].gain);
                const double hz = ROLL_LOW_HZ + (ROLL_HIGH_HZ - ROLL_LOW_HZ) * speed;
                const double turn = (ROLL_TURN_LOW + (ROLL_TURN_HIGH - ROLL_TURN_LOW) * speed) / 44100.0;
                const float f = (float)(2.0 * sin(PI * hz / 44100.0));
                const float full = (float)(vol_rolling * ROLL_LEVEL * sqrt(ROLL_LOW_HZ / hz) / (ROLL_Q * ROLL_Q));
                for (int i = 0; i < frames; i++) {
                    roll[b].gain += (target[b] - roll[b].gain) * ease;
                    if (roll[b].gain < 1e-5f && target[b] == 0) { roll[b].gain = 0; continue; }
                    noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
                    float w = (float)(int32_t)noise / 2147483648.0f;
                    const float h1 = w - roll[b].lo1 - damp * roll[b].bp1;   /* state-variable filters */
                    roll[b].bp1 += f * h1;
                    roll[b].lo1 += f * roll[b].bp1;
                    const float h2 = roll[b].bp1 - roll[b].lo2 - damp * roll[b].bp2;
                    roll[b].bp2 += f * h2;
                    roll[b].lo2 += f * roll[b].bp2;
                    roll[b].turn += turn;
                    if (roll[b].turn >= 1) roll[b].turn -= 1;
                    const float wobble = (float)(1.0 - ROLL_WOBBLE * (0.5 + 0.5 * sin(2 * PI * roll[b].turn)));
                    float s = roll[b].bp2 * roll[b].gain * full * wobble;
                    for (int c = 0; c < 4; c++) acc[i * 4 + c] += s * g[b][c];
                }
            }
        }
    }
    /* a soft knee: linear to KNEE, then rounding off toward full scale, so
     * the exciters can be driven hard without a clipped edge */
    const float KNEE = 24000.0f, room = 32767.0f - KNEE;
    for (int i = 0; i < frames * 4; i++) {
        float s = acc[i], m = fabsf(s);
        if (m > KNEE) m = KNEE + room * tanhf((m - KNEE) / room);
        out[i] = (int16_t)(s < 0 ? -m : m);
    }
}

/* ---- the table ------------------------------------------------------------------ */

static struct { char table[8]; uint32_t mask; } override_[16];
static int noverride;

int ssf_board(const char *table, const char *list)
{
    uint32_t mask = 0;
    for (const char *p = list; *p; ) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;
        char *e;
        unsigned long v = strtoul(p, &e, 16);
        if (e == p || v > 31) return 0;
        mask |= 1u << v;
        p = e;
    }
    for (int i = 0; i < noverride; i++)
        if (!strcmp(override_[i].table, table)) { override_[i].mask = mask; return 1; }
    if (noverride == 16) return 0;
    snprintf(override_[noverride].table, sizeof override_[noverride].table, "%s", table);
    override_[noverride++].mask = mask;
    return 1;
}

void ssf_volumes(double effects, double rolling) { vol_effects = effects; vol_rolling = rolling; }
void ssf_wanted(void) { wanted = 1; }

void ssf_table_begin(void)
{
    uint32_t cs;
    g_ssf_on = 0;
    if (!wanted) return;
    if (!(M = watch_table(&DS, &cs))) { fprintf(stderr, "[ssf] no feedback map for this table (above): no SSF for it\n"); return; }
    if (!*M->effect_fn || !*M->sfx_setup_fn || !*M->sfx_volume_fn || !*M->sfx_stop_fn) {
        fprintf(stderr, "[ssf] %s's map has no sound effects: run tools/feedback_map.py. No SSF for it.\n", M->name);
        return;
    }
    board = M->board;
    for (int i = 0; i < noverride; i++) if (!strcmp(override_[i].table, M->name)) board = override_[i].mask;
    memset(voice, 0, sizeof voice);
    memset(roll, 0, sizeof roll);
    pending.on = 0;
    muting = 0;
    g_ssf_on = 1;
    int n = 0;
    for (int i = 0; i < 32; i++) n += board >> i & 1;
    fprintf(stderr, "[ssf] %s: %d board effects on the exciters, effects x%g, rolling x%g\n",
            M->name, n, vol_effects, vol_rolling);
}

void ssf_table_end(void)
{
    g_ssf_on = 0;
    M = NULL;
    memset(voice, 0, sizeof voice);
    memset(roll, 0, sizeof roll);
}
