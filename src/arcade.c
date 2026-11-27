/*
 * arcade.c - between games in a cabinet: the DMD's initials entry and its
 * GAME OVER pages (arcade.h). Drawn in a font of its own: the table's fonts
 * went with the table.
 */
#include "arcade.h"
#include <stdio.h>
#include <string.h>

/* ---- the font: 7 rows of 5 dots, the high bit leftmost --------------------------- */

static const struct { char c; uint8_t row[7]; } glyphs[] = {
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } }, { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
    { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } }, { 'D', { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E } },
    { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } }, { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
    { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } }, { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } }, { 'J', { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C } },
    { 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } }, { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } }, { 'N', { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 } },
    { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } }, { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
    { 'Q', { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D } }, { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } }, { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } }, { 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
    { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A } }, { 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
    { 'Y', { 0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04 } }, { 'Z', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F } },
    { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } }, { '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } }, { '3', { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E } },
    { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } }, { '5', { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E } },
    { '6', { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E } }, { '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
    { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } }, { '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C } },
    { '#', { 0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A } }, { ',', { 0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08 } },
    { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C } }, { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
    { '<', { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 } }, { '>', { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 } },
    { '_', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F } }, { '!', { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 } },
    { '?', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 } }, { ':', { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 } },
    { '\x7F', { 0x00, 0x04, 0x08, 0x1F, 0x08, 0x04, 0x00 } },     /* the back arrow */
};

static const uint8_t *glyph(char c)
{
    for (size_t i = 0; i < sizeof glyphs / sizeof glyphs[0]; i++) if (glyphs[i].c == c) return glyphs[i].row;
    return NULL;
}

int arcade_text_w(const char *s, int scale)
{
    const int n = (int)strlen(s);
    return n ? (n * 6 - 1) * scale : 0;
}

void arcade_text(uint32_t *out, int x, int y, const char *s, int scale, uint32_t argb)
{
    for (; *s; s++, x += 6 * scale) {
        const uint8_t *g = glyph(*s);
        if (!g) continue;
        for (int r = 0; r < 7 * scale; r++)
            for (int c = 0; c < 5 * scale; c++) {
                const int px = x + c, py = y + r;
                if (px >= 0 && px < DMD_W && py >= 0 && py < DMD_H && g[r / scale] & 0x10 >> c / scale)
                    out[py * DMD_W + px] = argb;
            }
    }
}

static void centred(uint32_t *out, int y, const char *s, int scale, uint32_t argb)
{
    arcade_text(out, (DMD_W - arcade_text_w(s, scale)) / 2, y, s, scale, argb);
}

void arcade_commas(uint32_t n, char out[16])
{
    char d[12];
    const int len = snprintf(d, sizeof d, "%u", n);
    int o = 0;
    for (int i = 0; i < len; i++) {
        if (i && (len - i) % 3 == 0) out[o++] = ',';
        out[o++] = d[i];
    }
    out[o] = 0;
}

/* A DMD's own amber, and dimmer for what is not the point */
#define LIT 0xFFFF8C1Au
#define DIM 0xFF7A4210u
static void clear(uint32_t *out) { for (int i = 0; i < DMD_W * DMD_H; i++) out[i] = 0xFF000000u; }

/* ---- the initials ------------------------------------------------------------------ */

#define BACK '\x7F'
static char offered[48];             /* the letters, then the back arrow */
static int noffered, at;             /* the letter shown in the next place */
static char chosen[4];
static int nchosen, rank_;
static uint32_t score_;

void arcade_entry(int rank, uint32_t score, const char *letters)
{
    noffered = 0;
    for (const char *p = letters; *p && noffered < (int)sizeof offered - 2; p++) offered[noffered++] = *p;
    offered[noffered++] = BACK;
    offered[noffered] = 0;
    at = 0; nchosen = 0; memset(chosen, 0, sizeof chosen);
    rank_ = rank; score_ = score;
}

static void take(char c)
{
    if (c == BACK) { if (nchosen) chosen[--nchosen] = 0; return; }
    if (nchosen < 3) chosen[nchosen++] = c;
}

int arcade_entry_input(int what, int letter)
{
    if (letter) {                                    /* typed: that letter, if offered */
        if (letter >= 'a' && letter <= 'z') letter -= 32;
        const char *p = letter != BACK ? strchr(offered, letter) : NULL;
        if (!p) return 0;
        at = (int)(p - offered);
        take((char)letter);
        return nchosen == 3;
    }
    switch (what) {
    case ARC_PREV: at = (at + noffered - 1) % noffered; break;
    case ARC_NEXT: at = (at + 1) % noffered; break;
    case ARC_TAKE: take(offered[at]); if (offered[at] == BACK) break; return nchosen == 3;
    case ARC_BACK: take(BACK); break;
    case ARC_DONE: return 1;
    }
    return 0;
}

const char *arcade_initials(void) { return chosen; }

void arcade_draw_entry(uint32_t *out, double ms)
{
    char line[32], n[16];
    clear(out);
    snprintf(line, sizeof line, "HIGH SCORE #%d", rank_);
    centred(out, 0, line, 1, LIT);
    arcade_commas(score_, n);
    centred(out, 8, n, 1, DIM);
    const int x0 = (DMD_W - 3 * 14 + 2) / 2, y = 17;  /* three places, 10 dots wide and 4 apart */
    for (int i = 0; i < 3; i++) {
        char s[2] = { 0, 0 };
        uint32_t c = LIT;
        if (i < nchosen) s[0] = chosen[i];
        else if (i == nchosen) {                       /* the one being chosen blinks */
            s[0] = offered[at] == ' ' ? '_' : offered[at];
            if ((long long)(ms / 250) % 2) c = DIM;
        } else { s[0] = '_'; c = DIM; }
        arcade_text(out, x0 + i * 14, y, s, 2, c);
    }
    if (nchosen < 3) {                                 /* the flippers choose */
        arcade_text(out, x0 - 14, y + 4, "<", 1, DIM);
        arcade_text(out, x0 + 3 * 14 + 4, y + 4, ">", 1, DIM);
    }
}

/* ---- between games: GAME OVER, then the high scores, a page each 3 s ---------------- */

void arcade_draw_ready(uint32_t *out, double ms, const ArcadeScore top[3])
{
    clear(out);
    const int blink = (long long)(ms / 500) % 2 == 0;
    if ((long long)(ms / 3000) % 2 == 0) {
        centred(out, 2, "GAME OVER", 2, LIT);
        if (blink) centred(out, 22, "PRESS START", 1, LIT);
        return;
    }
    centred(out, 0, "HIGH SCORES", 1, LIT);
    for (int i = 0; i < 3; i++) {
        char line[8], n[16];
        snprintf(line, sizeof line, "%d %.3s", i + 1, top[i].initials);
        arcade_text(out, 8, 8 + 8 * i, line, 1, DIM);
        arcade_commas(top[i].score, n);
        arcade_text(out, DMD_W - 8 - arcade_text_w(n, 1), 8 + 8 * i, n, 1, DIM);
    }
}
