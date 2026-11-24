/*
 * hud.c - the nudge indicator (hud.h), as VPinball shows it: where the
 * cabinet's accelerometer (or velocity) reads, inside a ring that is full scale.
 */
#include "hud.h"
#include <math.h>

#define TRAIL 6

static uint32_t blend(uint32_t under, uint32_t over, double a)
{
    if (a <= 0) return under;
    if (a > 1) a = 1;
    uint32_t r = 0xFF000000u;
    for (int s = 0; s < 24; s += 8) {
        const double u = (under >> s) & 0xFF, o = (over >> s) & 0xFF;
        r |= (uint32_t)(u + (o - u) * a + 0.5) << s;
    }
    return r;
}

/* How much of a pixel at distance d (in pixels) a line of half-width hw at radius rad covers */
static double cover(double d, double rad, double hw)
{
    const double e = fabs(d - rad) - hw;
    return e <= 0 ? 1 : e >= 1 ? 0 : 1 - e;
}

void hud_nudge(uint32_t *f, int w, int h, double px_aspect, int x, int y, int dead, int range)
{
    static double tx[TRAIL], ty[TRAIL];
    static int tn;
    if (!f || w < 64 || h < 64 || !(px_aspect > 0)) return;

    const double s = w / 320.0, rx = 12 * s, ry = rx * px_aspect;     /* ry: rows for the same height on screen */
    const double cx = w - 4 * s - rx - 0.5, cy = 4 * s * px_aspect + ry - 0.5;
    if (range < 1) range = 32767;
    double nx = (double)x / range, ny = (double)y / range;
    const double len = sqrt(nx * nx + ny * ny);
    if (len > 1) { nx /= len; ny /= len; }                          /* past full scale: on the ring */
    const double dz = dead > 0 && dead < range ? (double)dead / range : 0;

    for (int i = TRAIL - 1; i > 0; i--) { tx[i] = tx[i - 1]; ty[i] = ty[i - 1]; }
    tx[0] = nx; ty[0] = ny;
    if (tn < TRAIL) tn++;

    const int x0 = (int)(cx - rx - 2), x1 = (int)(cx + rx + 3), y0 = (int)(cy - ry - 2), y1 = (int)(cy + ry + 3);
    for (int py = y0 < 0 ? 0 : y0; py < y1 && py < h; py++)
        for (int px = x0 < 0 ? 0 : x0; px < x1 && px < w; px++) {
            const double dx = px - cx, dy = (py - cy) / px_aspect;      /* in the ring's own round space */
            const double d = sqrt(dx * dx + dy * dy);
            uint32_t *p = &f[py * w + px];
            if (d < rx) *p = blend(*p, 0xFF000000u, 0.45);              /* darker inside, so it reads on any table */
            *p = blend(*p, 0xFFFFFFFFu, 0.75 * cover(d, rx, 0.5 * s));
            if (dz > 0) *p = blend(*p, 0xFFA0A0A0u, 0.5 * cover(d, rx * dz, 0.35 * s));
            for (int i = tn - 1; i >= 0; i--) {                          /* the trail, oldest first; the dot on top */
                const double ex = dx - tx[i] * rx, ey = dy - ty[i] * rx;
                const double dd = sqrt(ex * ex + ey * ey), r = (i ? 1.4 : 2.2) * s;
                const double a = (dd <= r ? 1 : dd >= r + 1 ? 0 : r + 1 - dd) * (i ? 0.5 * (TRAIL - i) / TRAIL : 1);
                *p = blend(*p, 0xFFFFC020u, a);
            }
        }
}
