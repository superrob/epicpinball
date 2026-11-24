/*
 * ini.c - reading and writing the settings files (ini.h).
 *
 * The files are small and read whole on each call: settings change while the
 * game runs (the settings tool writes them; the game reads controller.ini
 * again when its window comes to the front), so nothing is cached.
 */
#include "ini.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

#define LINE 2048

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    for (char *e = s + strlen(s); e > s && isspace((unsigned char)e[-1]); ) *--e = 0;
    return s;
}

static const char *get(const char *path, const char *sec, const char *key, const char *def, char *out, int n, int inline_comments)
{
    FILE *f = fopen(path, "r");
    char line[LINE], cur[128] = "";
    snprintf(out, n, "%s", def);
    for (int first = 1; f && fgets(line, sizeof line, f); first = 0) {
        char *s = line, *e;
        if (first && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s += 3;
        if (inline_comments && (e = strchr(s, ';')) != NULL) *e = 0;
        s = trim(s);
        if (*s == ';' || *s == '#' || !*s) continue;
        if (*s == '[') {
            if ((e = strchr(s, ']')) != NULL) *e = 0;
            snprintf(cur, sizeof cur, "%s", s + 1);
            continue;
        }
        if (strcasecmp(cur, sec) || (e = strchr(s, '=')) == NULL) continue;
        *e = 0;
        if (!strcasecmp(trim(s), key)) { snprintf(out, n, "%s", trim(e + 1)); break; }
    }
    if (f) fclose(f);
    return out;
}

const char *ini_get(const char *path, const char *sec, const char *key, const char *def, char *out, int n)
{
    return get(path, sec, key, def, out, n, 1);
}

const char *ini_get_raw(const char *path, const char *sec, const char *key, const char *def, char *out, int n)
{
    return get(path, sec, key, def, out, n, 0);
}

/* ---- writing ------------------------------------------------------------------- */

typedef struct { char **l; int n, cap; } Lines;

static void push(Lines *L, const char *s)
{
    if (L->n == L->cap) {
        L->cap = L->cap ? L->cap * 2 : 64;
        L->l = (char **)realloc(L->l, sizeof *L->l * L->cap);
    }
    L->l[L->n++] = strdup(s);
}

static void insert(Lines *L, int at, const char *s)
{
    push(L, s);                                    /* room at the end, then moved into place */
    char *x = L->l[L->n - 1];
    memmove(L->l + at + 1, L->l + at, sizeof *L->l * (L->n - 1 - at));
    L->l[at] = x;
}

static int ncase(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int x = tolower((unsigned char)a[i]), y = tolower((unsigned char)b[i]);
        if (x != y || !x) return x - y;
    }
    return 0;
}

/* The section a line opens, into name; 0 if it opens none */
static int section_of(const char *line, char *name, int n)
{
    while (isspace((unsigned char)*line)) line++;
    if (*line != '[') return 0;
    const char *e = strchr(line, ']');
    if (!e) return 0;
    snprintf(name, n, "%.*s", (int)(e - line - 1), line + 1);
    return 1;
}

/* Whether a line sets key: "key =" with any spaces, not a comment */
static int sets(const char *line, const char *key)
{
    while (isspace((unsigned char)*line)) line++;
    if (*line == ';' || *line == '#') return 0;
    const char *e = strchr(line, '=');
    if (!e) return 0;
    const char *k = e;
    while (k > line && isspace((unsigned char)k[-1])) k--;
    return (size_t)(k - line) == strlen(key) && !ncase(line, key, strlen(key));
}

int ini_set(const char *path, const char *sec, const char *key, const char *value)
{
    if (value && strpbrk(value, ";\r\n")) return 0;
    Lines L = { 0 };
    char line[LINE];
    int crlf = 0;
    FILE *f = fopen(path, "rb");
    while (f && fgets(line, sizeof line, f)) {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\n') { line[--len] = 0; if (len && line[len - 1] == '\r') { line[--len] = 0; crlf = 1; } }
        push(&L, line);
    }
    if (f) fclose(f);

    int in = 0, found = -1, last = -1;            /* last: the section's last line that is not blank */
    char cur[128];
    for (int i = 0; i < L.n; i++) {
        if (section_of(L.l[i], cur, sizeof cur)) {
            if (in) break;
            in = !strcasecmp(cur, sec);
            if (in) last = i;
            continue;
        }
        if (!in) continue;
        if (*trim(strcpy(line, L.l[i]))) last = i;
        if (sets(L.l[i], key)) { found = i; break; }
    }

    if (found >= 0) {
        if (!value) {
            free(L.l[found]);
            memmove(L.l + found, L.l + found + 1, sizeof *L.l * (L.n - found - 1));
            L.n--;
        } else {
            /* "key = value", the key as written, and the comment after the value where it was */
            const char *old = L.l[found], *eq = strchr(old, '='), *c = strchr(eq, ';');
            char nl[LINE];
            int w = snprintf(nl, sizeof nl, "%.*s", (int)(eq - old), old);
            while (w > 0 && isspace((unsigned char)nl[w - 1])) nl[--w] = 0;
            w += snprintf(nl + w, sizeof nl - w, " = %s", value);
            if (c) {
                int col = (int)(c - old);
                if (w < col) { memset(nl + w, ' ', col - w); w = col; nl[w] = 0; }
                else w += snprintf(nl + w, sizeof nl - w, " ");
                snprintf(nl + w, sizeof nl - w, "%s", c);
            }
            free(L.l[found]);
            L.l[found] = strdup(nl);
        }
    } else if (value) {
        snprintf(line, sizeof line, "%s = %s", key, value);
        if (last >= 0) insert(&L, last + 1, line);
        else {
            if (L.n && *L.l[L.n - 1]) push(&L, "");
            char s[160];
            snprintf(s, sizeof s, "[%s]", sec);
            push(&L, s);
            push(&L, line);
        }
    }

    /* a whole new file, then in its place: never half a file */
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s.new", path);
    int ok = 0;
    if ((f = fopen(tmp, "wb")) != NULL) {
        ok = 1;
        for (int i = 0; i < L.n; i++) ok &= fprintf(f, "%s%s", L.l[i], crlf ? "\r\n" : "\n") >= 0;
        ok &= fclose(f) == 0;
#ifdef _WIN32
        ok = ok && MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING);
#else
        ok = ok && rename(tmp, path) == 0;
#endif
        if (!ok) remove(tmp);
    }
    for (int i = 0; i < L.n; i++) free(L.l[i]);
    free(L.l);
    return ok;
}
