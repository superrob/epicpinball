/*
 * guard.c - a fault of the game itself, kept from happening (guard.h).
 *
 * Each table keeps a ball's x off the left edge in its physics step (below 1
 * it is set to 1; table 5 sets 0), but a collision after that can still push
 * it to -1. Drawn there, it overruns the table's data: on African Safari
 * the table's saved timer vector, and the next tick
 * jumps to FFFF:0202, where DOS has the interrupt table or the HMA. The
 * original would crash or hang there.
 *
 * The overrun itself is in the function that saves the screen under a ball
 * before it is drawn: from x / 4, with x = -1, its byte count wraps to some
 * 49,000, and `rep movsb` copies them over the table's data. So as that
 * function begins, an x below 0 (AX, and the ball's own) is put back where the
 * game's clamp puts it. Nothing else is touched: an x of 0, which the game
 * allows, stays.
 * Always on, whatever the options; it needs the table's feedback map
 * (tools/feedback_map.py), and without one it is off for that table.
 *
 * To try it: EPIC_GUARD_TEST=ms gives the save x = -1 once at that emulated
 * time; with EPIC_GUARD_OFF as well it is left so, and the table breaks.
 */
#include "guard.h"
#include "watch.h"
#include <stdlib.h>
#include <string.h>

int g_guard_on;
static char fn[24];
static uint32_t ds;
static uint16_t x, active, x_min;
static int slots, told;

void guard_table_begin(const LiftedImage *img, uint16_t load_seg)
{
    static WatchMap m;
    static char text[8192];
    char path[96];
    const char *why = "";
    g_guard_on = 0;
    if (!img || strncmp(img->name, "ep", 2)) return;
    snprintf(path, sizeof path, "work/feedback/%s.map", img->name);
    FILE *f = fopen(path, "rb");
    if (!f) return;                  /* the watcher says so when it is wanted */
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    if (!watch_map_parse(text, &m, &why) || strcmp(m.name, img->name) || m.size != img->size || m.fnv != img->fnv)
        return;                      /* another copy's, or old: no guess at guest memory */
    guard_use(&m, load_seg);
}

int guard_use(const WatchMap *m, uint16_t load_seg)
{
    g_guard_on = 0;
    ds = (uint32_t)(uint16_t)(load_seg + m->ds) * 16;
    if (ds + 0x10000 > MEM_SIZE || !*m->guard_fn) return 0;
    snprintf(fn, sizeof fn, "%s", m->guard_fn);
    x = m->x;
    active = m->active;
    x_min = m->x_min;
    slots = m->slots < 8 ? m->slots : 8;
    told = 0;
    g_guard_on = 1;
    return 1;
}

void guard_table_end(void) { g_guard_on = 0; }

static uint16_t rd16(uint32_t a) { return (uint16_t)(g_cpu.mem[a] | g_cpu.mem[a + 1] << 8); }

void guard_enter(CPU *cpu, const char *f)
{
    if (strcmp(f, fn)) return;
    /* AX is the ball's x, DI its slot (twice its number) */
    const uint32_t a = ds + (uint16_t)(x + cpu->di);
    static double poke = -1;         /* EPIC_GUARD_TEST=ms: a ball at -1 then, to try it */
    if (poke < 0) { const char *e = getenv("EPIC_GUARD_TEST"); poke = e ? atof(e) : 1e300; }
    if (pit_now() * 1000.0 / PIT_HZ >= poke) {
        poke = 1e300;
        cpu->ax = 0xFFFF;
        fprintf(stderr, "[guard] test: a ball at x = -1\n");
        if (getenv("EPIC_GUARD_OFF")) return;
    }
    if ((int16_t)cpu->ax >= 0) return;
    if (!told++)
        fprintf(stderr, "[guard] a ball at x = %d, off the left edge (a fault in the game): put back at %d\n",
                (int16_t)cpu->ax, x_min);
    cpu->ax = x_min;
    if (cpu->di < 2 * slots && (int16_t)rd16(a) < 0) {
        g_cpu.mem[a] = (uint8_t)x_min;
        g_cpu.mem[a + 1] = (uint8_t)(x_min >> 8);
    }
}
