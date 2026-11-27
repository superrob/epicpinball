/*
 * dmd.c - a 128x32 DMD picture of the running table. See dmd.h and
 * docs/dmd.md.
 *
 * A message is followed from the calls that make it: the message function
 * (BX the text, AL the effect, AH the font, DI the place) and the appenders
 * that add a line each. Rasterising the text here exactly as the game does
 * gives, for each dot of its list, the glyph pixel it came from, so the text
 * can be set again for 128x32 -- wrapped, centred, doubled when it fits, a
 * page at a time when it is taller -- and each dot still moved as far as
 * the game's own has moved (its effects: explode, wave, slide) and in its
 * colour (DAC FFh, which the effects fade; tables 9-13 keep one per dot).
 */
#include "dmd.h"
#include "machine.h"
#include "watch.h"
#include "ssf.h"
#include "autostart.h"
#include "guard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_dmd_on;
static int wanted;

/* RECOMP_ENTER lands here while either is on */
void seam_enter(CPU *cpu, const char *fn)
{
    if (g_guard_on) guard_enter(cpu, fn);
    if (g_watch_on) watch_enter(cpu, fn);
    if (g_dmd_on) dmd_enter(cpu, fn);
    if (g_ssf_on) ssf_enter(cpu, fn);
    if (g_auto_on) autostart_enter(cpu, fn);
}

/* ---- the map ------------------------------------------------------------------ */

enum { F_SMALL, F_LARGE, F_TINY, FONTS };     /* AH 0, 1, 2 */
typedef struct { int ok; uint16_t seg, off; int stride, rows, cols, adv; } Font;

#define APPENDERS 4
static struct {
    char name[16];
    uint32_t size, fnv;
    uint16_t ds;
    char msg_fn[32], step_fn[32];
    Font font[FONTS];
    int napp;
    char app_fn[APPENDERS][32];
    Font app[APPENDERS];
    uint16_t list, effect, count;
    int playfield;                    /* messages on the playfield (tables 1-8), not in the bar */
    int nsave;
    uint16_t save[2], save_seg[2];
} M;

static uint32_t DS;
static uint16_t LOAD;

static uint16_t rd16(uint32_t a) { return (uint16_t)(g_cpu.mem[a] | g_cpu.mem[a + 1] << 8); }
static double now_ms(void) { return pit_now() * 1000.0 / PIT_HZ; }

static int parse_font(const char *s, Font *f)
{
    unsigned seg, off;
    if (sscanf(s, "%x %x %d %d %d %d", &seg, &off, &f->stride, &f->rows, &f->cols, &f->adv) != 6) return 0;
    f->seg = (uint16_t)seg; f->off = (uint16_t)off;
    f->ok = f->rows > 0 && f->rows <= 8 && f->cols > 0 && f->cols <= 8 && f->adv > 0;
    return f->ok;
}

static int parse_map(char *text, const char **why)
{
    memset(&M, 0, sizeof M);
    int version = 0, have = 0;
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        char key[24]; int n = 0;
        if (sscanf(line, "%23s %n", key, &n) != 1) continue;
        const char *v = line + n;
        unsigned x, y;
        if (!strcmp(key, "epicpinball-dmd")) version = atoi(v);
        else if (!strcmp(key, "image")) sscanf(v, "%15s %u %x", M.name, &M.size, &M.fnv);
        else if (!strcmp(key, "ds") && sscanf(v, "%x", &x) == 1) M.ds = (uint16_t)x;
        else if (!strcmp(key, "msg_fn")) sscanf(v, "%31s", M.msg_fn);
        else if (!strcmp(key, "step_fn")) sscanf(v, "%31s", M.step_fn);
        else if (!strcmp(key, "font")) {
            char which[8]; int k = 0;
            if (sscanf(v, "%7s %n", which, &k) != 1) continue;
            int i = !strcmp(which, "small") ? F_SMALL : !strcmp(which, "large") ? F_LARGE : !strcmp(which, "tiny") ? F_TINY : -1;
            if (i >= 0 && !parse_font(v + k, &M.font[i])) { *why = "a font it cannot read"; return 0; }
        } else if (!strcmp(key, "append") && M.napp < APPENDERS) {
            int k = 0;
            if (sscanf(v, "%31s %n", M.app_fn[M.napp], &k) != 1 || !parse_font(v + k, &M.app[M.napp])) {
                *why = "an appender it cannot read"; return 0;
            }
            M.napp++;
        } else if (!strcmp(key, "list") && sscanf(v, "%x", &x) == 1) { M.list = (uint16_t)x; have |= 1; }
        else if (!strcmp(key, "effect") && sscanf(v, "%x", &x) == 1) { M.effect = (uint16_t)x; have |= 2; }
        else if (!strcmp(key, "count") && sscanf(v, "%x", &x) == 1) { M.count = (uint16_t)x; have |= 4; }
        else if (!strcmp(key, "family")) M.playfield = !strncmp(v, "playfield", 9);
        else if (!strcmp(key, "save") && M.nsave < 2 && sscanf(v, "%x %x", &x, &y) == 2) {
            M.save[M.nsave] = (uint16_t)x; M.save_seg[M.nsave] = (uint16_t)y; M.nsave++;
        }
    }
    if (version != 1) { *why = "not a version 1 DMD map"; return 0; }
    if (!*M.msg_fn || !*M.step_fn || have != 7 || !M.font[F_LARGE].ok || !M.font[F_SMALL].ok) { *why = "incomplete"; return 0; }
    if (M.playfield && M.nsave != 2) { *why = "no save lists for a table with messages on its playfield"; return 0; }
    return 1;
}

/* ---- the message, as the game builds it --------------------------------------- */

#define MAXDOTS 4096                  /* the game's list is smaller; the 9-13 bar's is 1,200 */
#define MAXLINES 24
#define MAXCHARS 40

typedef struct {
    const Font *f;
    char text[MAXCHARS + 1];
    int len;
    uint16_t di;                      /* where the game put its first glyph: y*320 + x */
    int first, n;                     /* its dots in the list */
    int char_first[MAXCHARS + 1];     /* the first dot of each character; [len] the end */
    int hidden;                       /* in the list of messages a cabinet does not show */
} Line;

/* A message as the game builds it, and where its characters go on the DMD */
typedef struct { int x, y, page, s; } Place;       /* top left, page, scale */
typedef struct {
    Line ln[MAXLINES];
    int nlines, ndots;
    uint16_t orig[MAXDOTS];           /* each dot where the game first put it */
    uint8_t dot_col[MAXDOTS], dot_row[MAXDOTS], dot_char[MAXDOTS], dot_line[MAXDOTS];
    uint8_t colour[MAXDOTS];          /* a kept board's own (9-13: a colour a dot) */
    Place at_char[MAXLINES][MAXCHARS];
    int npages, layout_dirty;
    int lift;                         /* rows a waving message is raised by, to stay on the DMD */
} Model;

static Model shown_, kept_;           /* the game's message now; the last board (9-13) */
static Model *md = &shown_;
static int have_kept;
static double started, started_play;
static int unchecked;                 /* EPIC_DMD_CHECK: a message built, not yet compared */
static double play_ms, last_step;     /* time with the physics running: a ball in play */


/* ---- messages a cabinet does not show ------------------------------------------
 *
 * Keyboard help ("USE UP/DOWN ARROW KEYS TO ...") and prompts for keys a
 * cabinet has not got. A message whose first line is listed goes with all
 * its lines; a listed appended line ("PRESS ENTER" under the bonus) alone.
 * [cabinet] hide_messages in epicpinball.ini replaces the list. */
static char hide_list[1024] =
    "USE UP/DOWN ARROW KEYS TO|USE SHIFT KEYS TO CONTROL|PRESS ENTER TO REMOVE|PRESS SPACE TO BEGIN|PRESS ENTER";

static int listed(const char *text);

void dmd_set_hidden(const char *list)
{
    snprintf(hide_list, sizeof hide_list, "%s", list ? list : "");
    for (int i = 0; i < md->nlines; i++)              /* the message up now, again */
        md->ln[i].hidden = (i && md->ln[0].hidden) || listed(md->ln[i].text);
    md->layout_dirty = 1;
}

static int listed(const char *text)
{
    const char *a = text;
    while (*a == ' ') a++;
    size_t n = strlen(a);
    while (n && a[n - 1] == ' ') n--;
    for (const char *p = hide_list; *p; ) {
        const char *e = strchr(p, '|');
        size_t m = e ? (size_t)(e - p) : strlen(p);
        const char *q = p;
        while (m && *q == ' ') { q++; m--; }
        while (m && q[m - 1] == ' ') m--;
        if (m && m == n && !strncmp(q, a, n)) return 1;
        if (!e) break;
        p = e + 1;
    }
    return 0;
}

static uint8_t glyph_byte(const Font *f, int idx, int row)
{
    uint32_t a = (uint32_t)(uint16_t)(LOAD + f->seg) * 16 + (uint16_t)(f->off + idx * f->stride + row);
    return a < MEM_SIZE ? g_cpu.mem[a] : 0;
}

/* The game's rasteriser: character by character, column by column, top to
 * bottom, a dot for each lit pixel, 2 pixels apart */
static void add_line(CPU *cpu, const Font *f, uint16_t bx, uint16_t di)
{
    if (md->nlines >= MAXLINES) return;
    Line *l = &md->ln[md->nlines];
    memset(l, 0, sizeof *l);
    l->f = f; l->di = di; l->first = md->ndots;
    for (int i = 0; i < MAXCHARS; i++) {
        uint8_t c = mem_read8(cpu, cpu->ds, (uint16_t)(bx + i));
        if (!c) break;
        l->text[l->len++] = (char)c;
    }
    l->hidden = (md->nlines && md->ln[0].hidden) || listed(l->text);
    uint16_t at = di;
    for (int i = 0; i < l->len; i++) {
        l->char_first[i] = md->ndots;
        uint8_t c = (uint8_t)l->text[i];
        if (c > 0x60) c -= 0x20;
        int idx = c - 0x20;
        for (int col = 0; col < f->cols; col++, at += 2)
            for (int row = 0; row < f->rows; row++)
                if ((glyph_byte(f, idx, row) >> (7 - col)) & 1 && md->ndots < MAXDOTS) {
                    md->orig[md->ndots] = (uint16_t)(at + row * 640);
                    md->dot_col[md->ndots] = (uint8_t)col; md->dot_row[md->ndots] = (uint8_t)row;
                    md->dot_char[md->ndots] = (uint8_t)i; md->dot_line[md->ndots] = (uint8_t)md->nlines;
                    md->ndots++;
                }
        at = (uint16_t)(at + f->adv - f->cols * 2);
    }
    l->char_first[l->len] = md->ndots;
    l->n = md->ndots - l->first;
    md->nlines++;
    md->layout_dirty = 1;
}

/* Tables 9-13 build their idle board as a message: BALL/PLAYER in the tiny
 * font, the score appended in the large one */
static int same_font(const Font *a, const Font *b) { return a->seg == b->seg && a->off == b->off; }
static int is_board(const Model *m)
{
    return !M.playfield && m->nlines && !m->ln[0].hidden && same_font(m->ln[0].f, &M.font[F_TINY])
        && !strncmp(m->ln[0].text, "BALL ", 5);
}

/* EPIC_DMD_CHECK=1: does the game's list hold the dots rasterised here?
 * Where it ends, and how many are where they were predicted (an effect that
 * has begun moves some). On stderr, a line a message. */
static void check(void)
{
    unchecked = 0;
    int n = 0, same = 0;
    while (n < MAXDOTS && rd16(DS + M.list + 2 * n)) n++;
    for (int i = 0; i < md->ndots && i < n; i++) same += rd16(DS + M.list + 2 * i) == md->orig[i];
    fprintf(stderr, "[dmd] check \"%s\"%s: %d dots here, the game's list ends at %d, %d in place\n",
            md->nlines ? md->ln[0].text : "", md->nlines > 1 ? " and more lines" : "", md->ndots, n, same);
}

void dmd_enter(CPU *cpu, const char *fn)
{
    if (!strcmp(fn, M.step_fn)) {                      /* hundreds a second while a ball is played */
        double t = now_ms();
        if (t - last_step < 50) play_ms += t - last_step;
        last_step = t;
        if (unchecked) check();
        return;
    }
    if (!strcmp(fn, M.msg_fn)) {
        int len = 0;                                   /* over 30 characters: dropped */
        while (len < 31 && mem_read8(cpu, cpu->ds, (uint16_t)(cpu->bx + len))) len++;
        if (len >= 31) return;
        int ah = cpu->ah;
        uint16_t di = cpu->di;
        if (ah <= 2) {                                 /* centred across 320, or dropped */
            int adv = ah == 2 ? 8 : ah == 0 ? 11 : 16;
            int cx = 320 - adv * len;
            if (cx < 0) return;
            di = (uint16_t)(di + cx / 2 + 1);
        } else ah -= 3;
        if (unchecked) check();                        /* the last one, before it goes */
        if (is_board(&shown_)) {                       /* 9-13: kept, for while a hidden message is up */
            kept_ = shown_;
            for (int i = 0; i < kept_.ndots; i++) kept_.colour[i] = g_cpu.mem[DS + (uint16_t)(M.list + 2 * i + 0x960)];
            kept_.layout_dirty = have_kept = 1;
        }
        md->nlines = md->ndots = md->lift = 0;
        started = now_ms();
        started_play = play_ms;
        unchecked = getenv("EPIC_DMD_CHECK") != NULL;
        md->layout_dirty = 1;
        if (ah > 2 || !M.font[ah].ok) return;          /* a stub font: nothing to follow */
        add_line(cpu, &M.font[ah], cpu->bx, di);
        return;
    }
    for (int i = 0; i < M.napp; i++)
        if (!strcmp(fn, M.app_fn[i])) {
            if (md->nlines) add_line(cpu, &M.app[i], cpu->bx, cpu->di);
            return;
        }
}

/* ---- the table ---------------------------------------------------------------- */

static int use(uint16_t load_seg);

void dmd_wanted(void) { wanted = 1; }
int dmd_active(void) { return g_dmd_on; }

void dmd_table_begin(const LiftedImage *img, uint16_t load_seg)
{
    if (!wanted || !img || strncmp(img->name, "ep", 2)) return;
    static char text[4096];
    char path[96];
    const char *why = "";
    snprintf(path, sizeof path, "work/dmd/%s.map", img->name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[dmd] no %s: run tools/dmd_survey.py. No DMD for %s.\n", path, img->name);
        return;
    }
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    if (!parse_map(text, &why)) {
        fprintf(stderr, "[dmd] %s: %s. Run tools/dmd_survey.py. No DMD for %s.\n", path, why, img->name);
        return;
    }
    if (strcmp(M.name, img->name) || M.size != img->size || M.fnv != img->fnv) {
        fprintf(stderr, "[dmd] %s is of another copy of %s. Run tools/dmd_survey.py. No DMD for it.\n", path, img->file);
        return;
    }
    use(load_seg);
}

static int use(uint16_t load_seg)
{
    LOAD = load_seg;
    DS = (uint32_t)(uint16_t)(load_seg + M.ds) * 16;
    if (DS + 0x10000 > MEM_SIZE) return 0;
    md->nlines = md->ndots = 0;
    play_ms = started_play = 0;
    last_step = -1e18;
    md->layout_dirty = 1;
    g_dmd_on = 1;
    return 1;
}

int dmd_map_use(char *text, uint16_t load_seg, const char **why)
{
    return parse_map(text, why) && use(load_seg);
}

void dmd_table_end(void)
{
    g_dmd_on = 0;
    md->nlines = md->ndots = 0;
}

/* ---- laying a message out on 128x32 ------------------------------------------- */

#define GAP 2                         /* dots between rows */
#define PAGE_MS 2000.0                /* a page of a message taller than the DMD */
#define LASTING_MS 4000.0             /* a message the game never ends: then the board */


static int char_w(const Font *f) { return f->cols + 1; }       /* a column apart */


typedef struct { int line, from, to; } Word;   /* characters [from, to) of a line */

static void layout(void)
{
    md->layout_dirty = 0;
    md->npages = 1;
    if (!md->nlines) return;
    const int board = is_board(md);                    /* its lines one under another, the score doubled */

    /* the game's lines a cabinet shows, top to bottom; those beside each other
     * on screen (a label and its number) are one row, read left to right */
    int order[MAXLINES], group[MAXLINES], nl = 0;
    for (int i = 0; i < md->nlines; i++)
        if (!md->ln[i].hidden) order[nl++] = i;
    for (int i = 1; i < nl; i++)
        for (int j = i; j > 0 && md->ln[order[j - 1]].di / 320 > md->ln[order[j]].di / 320; j--) {
            int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
        }
    for (int k = 0, g = -1, bottom = -1; k < nl; k++) {
        const Line *l = &md->ln[order[k]];
        if (l->di / 320 >= bottom || board) g++;
        group[k] = g;
        if (l->di / 320 + 2 * l->f->rows > bottom) bottom = l->di / 320 + 2 * l->f->rows;
    }
    for (int i = 1; i < nl; i++)
        for (int j = i; j > 0 && group[j - 1] == group[j] && md->ln[order[j - 1]].di % 320 > md->ln[order[j]].di % 320; j--) {
            int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
        }

    /* the words of each screen row, wrapped to the DMD's width: into as few
     * rows as filling them in needs, and those as even as they go */
    typedef struct { int w, h, s, large; int nw; Word w_[64]; } Row;
    static Row row[48];
    static struct { Word w; int width, cw, rows; } word[160];
    int nrows = 0;
    for (int k = 0; k < nl && nrows < 48; ) {
        int nw = 0, widest = 1;
        for (const int g = group[k]; k < nl && group[k] == g; k++) {
            const Line *l = &md->ln[order[k]];
            for (int i = 0; i < l->len && nw < 160; ) {
                while (i < l->len && l->text[i] == ' ') i++;
                int j = i;
                while (j < l->len && l->text[j] != ' ') j++;
                if (j == i) break;
                word[nw].w = (Word){ order[k], i, j };
                word[nw].cw = char_w(l->f);
                word[nw].width = (j - i) * word[nw].cw - 1;
                word[nw].rows = l->f->rows;
                if (word[nw].width > widest) widest = word[nw].width;
                nw++;
                i = j;
            }
        }
        if (!nw) continue;
        int limit = DMD_W, need = 0;
        for (int pass = 0; pass < 2; pass++) {          /* how many rows, then the narrowest that keeps them */
            for (int lim = pass ? widest : DMD_W; lim <= DMD_W; lim++) {
                int rows = 1, cur = 0;
                for (int i = 0; i < nw; i++) {
                    int gap = cur ? word[i].cw : 0;
                    if (cur && cur + gap + word[i].width > lim) { rows++; cur = word[i].width; }
                    else cur += gap + word[i].width;
                }
                if (!pass) { need = rows; break; }
                if (rows == need) { limit = lim; break; }
            }
        }
        Row *r = NULL;
        for (int i = 0; i < nw; i++) {
            int gap = r && r->nw ? word[i].cw : 0;
            if (!r || (r->nw && r->w + gap + word[i].width > limit)) {
                if (nrows == 48) break;
                r = &row[nrows++];
                memset(r, 0, sizeof *r);
                gap = 0;
            }
            if (r->nw < 64) {
                r->w_[r->nw++] = word[i].w;
                r->w += gap + word[i].width;
                if (word[i].rows > r->h) r->h = word[i].rows;
                r->large |= same_font(md->ln[word[i].w.line].f, &M.font[F_LARGE]);
                r->s = 1;
            }
        }
    }

    /* one short row doubled, if it fits; on the board, the score */
    if (nrows == 1 && row[0].w * 2 <= DMD_W && row[0].h * 2 <= DMD_H) row[0].s = 2;
    if (board) {
        int h = 0;
        for (int r = 0; r < nrows; r++) h += (r ? GAP : 0) + row[r].h * (row[r].large ? 2 : 1);
        for (int r = 0; r < nrows; r++)
            if (row[r].large && row[r].w * 2 <= DMD_W && h <= DMD_H) row[r].s = 2;
    }

    /* pages of rows that fit; each page centred */
    int page = 0, first = 0;
    while (first < nrows) {
        int h = 0, last = first;
        while (last < nrows && h + (last > first ? GAP : 0) + row[last].h * row[last].s <= DMD_H) {
            h += (last > first ? GAP : 0) + row[last].h * row[last].s;
            last++;
        }
        if (last == first) last++;                    /* taller than the DMD alone: cut */
        int y = (DMD_H - h) / 2;
        for (int r = first; r < last; r++) {
            const int scale = row[r].s;
            int x = (DMD_W - row[r].w * scale) / 2;
            for (int k = 0; k < row[r].nw; k++) {
                const Word *w = &row[r].w_[k];
                const Line *l = &md->ln[w->line];
                int cw = char_w(l->f) * scale;
                if (k) x += cw;                         /* the space before it */
                int yy = y + (row[r].h - l->f->rows) * scale;   /* on the row's baseline */
                for (int c = w->from; c < w->to; c++) {
                    md->at_char[w->line][c].x = x;
                    md->at_char[w->line][c].y = yy;
                    md->at_char[w->line][c].page = page;
                    md->at_char[w->line][c].s = scale;
                    x += cw;
                }
                x -= scale;                             /* the last character's gap column */
            }
            y += row[r].h * scale + GAP;
        }
        page++;
        first = last;
    }
    md->npages = page;
}

/* ---- the picture -------------------------------------------------------------- */

static uint32_t last[DMD_W * DMD_H];

static uint32_t dac_rgb(int i)
{
    uint8_t r, g, b;
    vga_dac_get(i, &r, &g, &b);
    return 0xFF000000u | (uint32_t)(r * 255 / 63) << 16 | (uint32_t)(g * 255 / 63) << 8 | (uint32_t)(b * 255 / 63);
}

/* Effects the game ends itself (docs/dmd.md); the rest stay until the next message */
static int ends_itself(int e) { return e == 1 || e == 2 || e == 4 || e == 5 || e == 6 || (e >= 8 && e <= 0xB); }

/* A message the game never ends gives way to the board after 4 s of play: a
 * screen that waits on the player (QUIT?, the bonus, the scores) stays, as
 * the physics stops meanwhile. The effect is the game's now: it changes one
 * in place (Enter makes the bonus explode). */
static int message_up(void)
{
    if (!md->nlines || !md->ndots || md->ln[0].hidden || !rd16(DS + M.count)) return 0;
    if (M.playfield && !ends_itself(g_cpu.mem[DS + M.effect]) && play_ms - started_play > LASTING_MS) return 0;
    return 1;
}

static int half(int d) { return d >= 0 ? (d + 1) / 2 : -((1 - d) / 2); }   /* screen pixels to font pixels */

/* Its dots on the DMD: how many were drawn. `still`: a kept board, where the
 * game put its dots, in their own colours */
/* The waves (effects 2-5, 7) move dots down from where the text sits, and
 * back: on the DMD a doubled line goes 24 rows down. A waving message is
 * raised as far as its lowest dot has gone past the bottom, never past the
 * top, and stays raised while it is up. Explosions and slides are left to
 * fly off. */
static int waves(int e) { return (e >= 2 && e <= 5) || e == 7; }

static int draw_message(uint32_t *out, int still)
{
    if (md->layout_dirty) layout();
    int page = md->npages > 1 ? (int)((now_ms() - started) / PAGE_MS) % md->npages : 0;
    uint32_t colour = dac_rgb(0xFF);
    int drawn = 0;
    if (!still && waves(g_cpu.mem[DS + M.effect])) {
        int top = DMD_H, bottom = -1;
        for (int i = 0; i < md->ndots; i++) {
            uint16_t live = rd16(DS + M.list + 2 * i);
            int l = md->dot_line[i], c = md->dot_char[i];
            if (live <= 1 || md->ln[l].hidden || md->at_char[l][c].page != page) continue;
            const int scale = md->at_char[l][c].s;
            int y0 = md->at_char[l][c].y + md->dot_row[i] * scale;   /* where the text sits */
            int y = y0 + half((int)(live / 320) - (int)(md->orig[i] / 320)) * scale + scale - 1;
            if (y0 < top) top = y0;
            if (y > bottom) bottom = y;
        }
        int need = bottom - (DMD_H - 1);
        if (need > top) need = top;
        if (need > md->lift) md->lift = need;
    }
    for (int i = 0; i < md->ndots; i++) {
        uint16_t live = still ? md->orig[i] : rd16(DS + M.list + 2 * i);
        if (live <= 1) continue;                       /* the list's end, or a dot gone */
        int l = md->dot_line[i], c = md->dot_char[i];
        if (md->ln[l].hidden || md->at_char[l][c].page != page) continue;
        int dx = (int)(live % 320) - (int)(md->orig[i] % 320), dy = (int)(live / 320) - (int)(md->orig[i] / 320);
        const int scale = md->at_char[l][c].s;
        int x = md->at_char[l][c].x + (md->dot_col[i] + half(dx)) * scale;
        int y = md->at_char[l][c].y + (md->dot_row[i] + half(dy)) * scale - md->lift;
        uint32_t col = M.playfield ? colour
                     : dac_rgb(still ? md->colour[i] : g_cpu.mem[DS + (uint16_t)(M.list + 2 * i + 0x960)]);
        for (int yy = y; yy < y + scale; yy++)
            for (int xx = x; xx < x + scale; xx++)
                if (xx >= 0 && xx < DMD_W && yy >= 0 && yy < DMD_H) { out[yy * DMD_W + xx] = col; drawn++; }
    }
    return drawn;
}

/* Tables 1-8: the bar's BALL/PLAYER line over its score digits, pixel for
 * pixel, its background unlit */
static uint32_t commonest(const uint32_t *bar, int bw, int y0, int y1)   /* a bar's background */
{
    uint32_t seen[8] = { 0 }, bg = 0;
    int count[8] = { 0 }, kinds = 0, most = 0;
    for (int y = y0; y < y1; y++)
        for (int x = 0; x < bw; x += 3) {
            uint32_t c = bar[y * bw + x];
            int k = 0;
            while (k < kinds && seen[k] != c) k++;
            if (k == kinds) { if (kinds == 8) continue; seen[kinds++] = c; }
            if (++count[k] > most) { most = count[k]; bg = c; }
        }
    return bg;
}

static void draw_board(const uint32_t *bar, int bw, int bh, uint32_t *out)
{
    if (bw != 320 || bh < 19) return;
    const uint32_t bg = commonest(bar, bw, 1, 19);
    static const struct { int x, y, w, h, to_x, to_y; } part[] = {
        { 0, 10, 128, 7, 0, 2 },                        /* BALL n  PLAYER n */
        { 196, 2, 120, 17, 4, 12 },                     /* the score's ten digits */
    };
    for (unsigned p = 0; p < sizeof part / sizeof *part; p++)
        for (int y = 0; y < part[p].h; y++)
            for (int x = 0; x < part[p].w; x++) {
                uint32_t c = bar[(part[p].y + y) * bw + part[p].x + x];
                if (c != bg) out[(part[p].to_y + y) * DMD_W + part[p].to_x + x] = c;
            }
}

int dmd_frame(const uint32_t *bar, int bw, int bh, uint32_t *out)
{
    for (int i = 0; i < DMD_W * DMD_H; i++) out[i] = 0xFF000000u;
    if (g_dmd_on) {
        /* the board, unless a message has dots on the DMD (an exploded one has
         * none left a while before the game ends it) */
        if (!(message_up() && draw_message(out, 0))) {
            if (M.playfield) { if (bar) draw_board(bar, bw, bh, out); }
            else if (have_kept && md->nlines && md->ln[0].hidden) {   /* 9-13: the board it replaced */
                md = &kept_;
                draw_message(out, 1);
                md = &shown_;
            }
        }
    }
    int changed = memcmp(out, last, sizeof last) != 0;
    memcpy(last, out, sizeof last);
    return changed;
}

/* ---- messages off the playfield ----------------------------------------------- */

#define HIDDEN 8192
static uint32_t hid_at[HIDDEN];
static uint8_t hid_was[HIDDEN];
static int nhid;

/* The dots of the hidden lines: a bit for each place one is now */
static uint8_t hidden_at[65536 / 8];
static int mark_hidden(void)
{
    memset(hidden_at, 0, sizeof hidden_at);
    int any = 0;
    for (int i = 0; i < shown_.ndots; i++) {
        if (!shown_.ln[shown_.dot_line[i]].hidden) continue;
        uint16_t live = rd16(DS + M.list + 2 * i);
        if (live > 1) { hidden_at[live >> 3] |= (uint8_t)(1 << (live & 7)); any = 1; }
    }
    return any;
}

void dmd_hide(int how)
{
    if (!how) {
        while (nhid > 0) { nhid--; vga_vram_poke(hid_at[nhid], hid_was[nhid]); }
        return;
    }
    nhid = 0;
    if (!g_dmd_on || !M.playfield) return;
    if (how == DMD_HIDE_LISTED && !mark_hidden()) return;
    for (int s = 0; s < 2; s++) {
        /* (offset, plane mask | saved pixel << 8) pairs, 0 ending them, plotted in
         * the segment the word at save_seg names */
        uint16_t seg = rd16(DS + M.save_seg[s]);
        uint32_t p = DS + M.save[s];
        for (int k = 0; k < 4000 && nhid < HIDDEN; k++, p += 4) {
            uint16_t di = rd16(p);
            if (!di) break;
            uint16_t mv = rd16(p + 2);
            int plane = (mv & 1) ? 0 : (mv & 2) ? 1 : (mv & 4) ? 2 : 3;
            uint16_t dot = (uint16_t)(di << 2 | plane);     /* the dot it was saved for */
            if (how == DMD_HIDE_LISTED && !(hidden_at[dot >> 3] >> (dot & 7) & 1)) continue;
            uint32_t i = ((((uint32_t)(uint16_t)(seg - 0xA000) * 16 + di) & 0xFFFF) << 2) | (uint32_t)plane;
            uint8_t now = vga_vram_peek(i);
            if (now != 0xFF) continue;                  /* drawn over since (the ball): left alone */
            hid_at[nhid] = i; hid_was[nhid] = now; nhid++;
            vga_vram_poke(i, (uint8_t)(mv >> 8));
        }
    }
}

/* Tables 9-13 draw their messages into the bar every frame, saving nothing:
 * in a picture of it, the hidden lines' dots are painted the bar's own
 * background. Video memory is not touched. */
void dmd_bar_hide(uint32_t *bar, int bw, int bh)
{
    if (!g_dmd_on || M.playfield || bw != 320 || bh < 2 || !mark_hidden()) return;
    const uint32_t bg = commonest(bar, bw, 1, bh);
    for (int i = 0; i < shown_.ndots; i++) {
        if (!shown_.ln[shown_.dot_line[i]].hidden) continue;
        uint16_t live = rd16(DS + M.list + 2 * i);
        if (live > 1 && live < bw * bh) bar[live] = bg;
    }
}

/* ---- how a DMD looks ---------------------------------------------------------- */

void dmd_dots(const uint32_t *in, uint32_t *out, int s)
{
    static uint8_t round_[16 * 16];                   /* which pixels of a cell are its dot */
    static int made;
    if (s < 1 || s > 16) return;
    if (made != s) {
        for (int y = 0; y < s; y++)
            for (int x = 0; x < s; x++) {
                int cx = x * 8 / s - 4, cy = y * 8 / s - 4;   /* eighths of a cell from its centre */
                round_[y * s + x] = (cx * 2 + 1) * (cx * 2 + 1) + (cy * 2 + 1) * (cy * 2 + 1) <= 52;
            }
        made = s;
    }
    const int W = DMD_W * s;
    for (int cy = 0; cy < DMD_H; cy++)
        for (int cx = 0; cx < DMD_W; cx++) {
            uint32_t c = in[cy * DMD_W + cx];
            if ((c & 0xFFFFFF) == 0) c = 0xFF161412;    /* unlit */
            uint32_t *o = out + (size_t)cy * s * W + cx * s;
            for (int y = 0; y < s; y++, o += W)
                for (int x = 0; x < s; x++) o[x] = round_[y * s + x] ? c : 0xFF000000u;
        }
}
