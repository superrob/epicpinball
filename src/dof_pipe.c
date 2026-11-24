/*
 * dof_pipe.c - --dof: table events to dof/build/epicpinball-dof, down a pipe.
 *
 * The helper is a separate GPL program that owns libdof (see dof/src/main.cpp
 * for the protocol); this only starts one a table and feeds it. The DOF files
 * say what an event does, and any limits are the controller's: none are here.
 * The game has one thread and a 1.4 ms retrace to catch, so nothing here
 * waits: not for the write, not for the process to start or to end.
 *
 * A helper that dies, stops reading, or hangs starting or stopping is killed
 * if need be, a last one is run only to switch everything off, and that table
 * goes on without feedback. The next helper starts when the last has exited.
 *
 * Linux: posix_spawn and non-blocking pipes. Windows: CreateProcess and
 * anonymous pipes, ours not inherited, the write end in PIPE_NOWAIT mode (a
 * full pipe takes what fits, which is a helper not reading, as EAGAIN is),
 * the helper's lines read only as far as PeekNamedPipe says there are bytes.
 */
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE                  /* pipe2 */
#endif
#include "dof_pipe.h"
#include "feedback.h"
#include "machine.h"

#ifdef _WIN32
#include "win_spawn.h"
#define BUILD_HINT "dof\\build.ps1"
#else
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#define BUILD_HINT "dof/build.sh"

extern char **environ;
#endif

#define START_MS 15000
#define STOP_MS   8000
#define EXIT_WAIT 1500               /* at the game's own exit only */

/* the helper's exit codes */
enum { EXIT_DONE = 0, EXIT_CANNOT_START = 2, EXIT_NO_ROWS = 5 };

static enum { NONE, STARTING, RUNNING, STOPPING, SWITCHING_OFF } state;
static char program[512], save[512];
static int was_ready;                /* this child could have switched something on */
static int lost;                     /* killed by us, or exited wrongly: switch off after it */
static int no_more;                  /* for the rest of the run */
static char want[16];                /* the table that should have a helper */
static char want_bumpers[8];         /* its bumpers' sides, "LCR" */
static char want_sides[96];          /* its own features' sides, "hole:LLCRR ..." */
static char in[128];
static size_t in_len;
static uint64_t since;

static double age_ms(uint64_t t) { return (host_us() - t) / 1000.0; }

/* ---- the platform's half: a child, a pipe each way -------------------- */

#ifdef _WIN32
static HANDLE child, to, from;       /* NULL: none */
#define HAVE_CHILD (child != NULL)
#define HAVE_FROM (from != NULL)
#define HAVE_TO (to != NULL)

static void shut(HANDLE *h)
{
    if (*h) CloseHandle(*h);
    *h = NULL;
}
static void shut_to(void) { shut(&to); }
static void shut_from(void) { shut(&from); }

static int spawn(char **argv)
{
    HANDLE down_r, down_w, up_r, up_w;
    DWORD err;
    if (!win_pipe(&down_r, &down_w, 65536)) return 0;
    if (!win_pipe(&up_r, &up_w, 65536)) { CloseHandle(down_r); CloseHandle(down_w); return 0; }
    const int ok = win_spawn(program, argv, down_r, up_w, &child, &err);
    CloseHandle(down_r);
    CloseHandle(up_w);
    if (!ok) {
        CloseHandle(down_w);
        CloseHandle(up_r);
        child = NULL;
        fprintf(stderr, "[dof] cannot run %s: %s\n", program, win_error(err));
        return 0;
    }
    win_nowait(down_w);
    to = down_w;
    from = up_r;
    return 1;
}

/* 1 all of it, 0 no room (it is not reading), -1 it has gone */
static int put(const char *s, size_t n)
{
    DWORD w = 0;
    if (!WriteFile(to, s, (DWORD)n, &w, NULL)) return -1;
    return w == n ? 1 : 0;
}

/* What it has said so far, never waiting: 0 when nothing (more) is there */
static long get(char *buf, size_t n)
{
    DWORD avail = 0, got = 0;
    if (!PeekNamedPipe(from, NULL, 0, NULL, &avail, NULL) || !avail) return 0;
    if (!ReadFile(from, buf, avail < n ? avail : (DWORD)n, &got, NULL)) return 0;
    return (long)got;
}

static void kill_child(void) { TerminateProcess(child, 1); }

/* 1 if it has ended: *code its exit code, *sig 0 */
static int ended(int *code, int *sig)
{
    DWORD c = 0;
    if (WaitForSingleObject(child, 0) != WAIT_OBJECT_0) return 0;
    if (!GetExitCodeProcess(child, &c)) c = 255;
    CloseHandle(child);
    child = NULL;
    *code = (int)c;
    *sig = 0;
    return 1;
}

static int runnable(const char *path) { return win_file(path); }
static void nap_ms(int ms) { Sleep(ms); }
#else
static pid_t child;
static int to = -1, from = -1;
#define HAVE_CHILD (child != 0)
#define HAVE_FROM (from >= 0)
#define HAVE_TO (to >= 0)

static void shut(int *fd)
{
    if (*fd >= 0) close(*fd);
    *fd = -1;
}
static void shut_to(void) { shut(&to); }
static void shut_from(void) { shut(&from); }

static int spawn(char **argv)
{
    int down[2], up[2];
    if (pipe2(down, O_CLOEXEC)) return 0;
    if (pipe2(up, O_CLOEXEC)) { close(down[0]); close(down[1]); return 0; }
    posix_spawn_file_actions_t fa;
    posix_spawnattr_t at;
    sigset_t s;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, down[0], 0);
    posix_spawn_file_actions_adddup2(&fa, up[1], 1);
    posix_spawnattr_init(&at);
    sigemptyset(&s);
    sigaddset(&s, SIGPIPE);          /* ignored here, not to be inherited */
    posix_spawnattr_setsigdefault(&at, &s);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSIGDEF);
    int err = posix_spawn(&child, program, &fa, &at, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    close(down[0]);
    close(up[1]);
    if (err) {
        close(down[1]);
        close(up[0]);
        child = 0;
        fprintf(stderr, "[dof] cannot run %s: %s\n", program, strerror(err));
        return 0;
    }
    to = down[1];
    from = up[0];
    fcntl(to, F_SETFL, O_NONBLOCK);
    fcntl(from, F_SETFL, O_NONBLOCK);
    return 1;
}

static int put(const char *s, size_t n)
{
    /* far under PIPE_BUF: all of it or none */
    if (write(to, s, n) == (ssize_t)n) return 1;
    return errno == EAGAIN ? 0 : -1;
}

static long get(char *buf, size_t n)
{
    const ssize_t r = read(from, buf, n);
    return r > 0 ? (long)r : 0;
}

static void kill_child(void) { kill(child, SIGKILL); }

static int ended(int *code, int *sig)
{
    int status;
    pid_t r = waitpid(child, &status, WNOHANG);
    if (r == child) {
        *code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        *sig = WIFEXITED(status) ? 0 : WTERMSIG(status);
    } else if (r < 0 && errno == ECHILD) {
        *code = 255;
        *sig = 0;
    } else {
        return 0;
    }
    child = 0;
    return 1;
}

static int runnable(const char *path) { return access(path, X_OK) == 0; }
static void nap_ms(int ms) { usleep(ms * 1000); }
#endif

/* ---- the same everywhere --------------------------------------------- */

static int start(const char *arg, const char *table)
{
    char *argv[] = { program, (char *)"--save", save, (char *)arg, (char *)table, NULL, NULL, NULL, NULL, NULL };
    int n = 5;
    if (table && *want_bumpers) { argv[n++] = (char *)"--bumpers"; argv[n++] = want_bumpers; }
    if (table && *want_sides) { argv[n++] = (char *)"--sides"; argv[n++] = want_sides; }
    if (!spawn(argv)) return 0;
    since = host_us();
    in_len = 0;
    was_ready = lost = 0;
    return 1;
}

static void give_up(const char *why)
{
    if (state == NONE) return;
    fprintf(stderr, "[dof] the helper %s: killing it%s\n", why,
            state == SWITCHING_OFF ? ". Check the board: an output may still be on"
                                   : ", no feedback for this table");
    if (state != SWITCHING_OFF) {
        lost = 1;
        if (state != STOPPING) *want = 0;
    }
    kill_child();
    shut_to();
    shut_from();
}

static void say(const char *line)
{
    if (!HAVE_TO) return;
    const int r = put(line, strlen(line));
    if (r > 0) return;
    if (r == 0) give_up("is not reading");
    else shut_to();                  /* it has gone: its exit says the rest */
}

static void stop(void)
{
    if (state != STARTING && state != RUNNING) return;
    say("q\n");
    shut_to();
    state = STOPPING;
    since = host_us();
}

static void listen_to_it(void)
{
    char buf[512];
    long n;
    while (HAVE_FROM && (n = get(buf, sizeof buf)) > 0) {
        for (long i = 0; i < n; i++) {
            if (buf[i] != '\n') {
                if (in_len < sizeof in - 1) in[in_len++] = buf[i];
                continue;
            }
            in[in_len] = 0;
            in_len = 0;
            if (!strcmp(in, "ready")) {
                was_ready = 1;
                if (state == STARTING) state = RUNNING;
            }
        }
    }
}

/* code: its exit code; sig: the signal that ended it, or 0 */
static void exited(int code, int sig)
{
    const int was = state;
    if (sig) code = -1;
    shut_to();
    shut_from();
    state = NONE;
    if (was == SWITCHING_OFF) {
        if (code != EXIT_DONE) fprintf(stderr, "[dof] could not switch the outputs off: check the board\n");
        return;
    }
    if (!lost && !was_ready && (code == EXIT_CANNOT_START || code == EXIT_NO_ROWS)) {
        if (code == EXIT_NO_ROWS) no_more = 1;
        if (was != STOPPING) *want = 0;          /* it has said why */
        return;
    }
    if (!lost) {
        if (was == STOPPING && code == EXIT_DONE) return;
        if (sig || (unsigned)code <= 255)
            fprintf(stderr, "[dof] the helper stopped (%s %d)%s\n", sig ? "signal" : "exit",
                    sig ? sig : code, was == STOPPING ? "" : ": no feedback for this table");
        else                                     /* Windows: a crash's NTSTATUS */
            fprintf(stderr, "[dof] the helper stopped (exit 0x%08X)%s\n",
                    (unsigned)code, was == STOPPING ? "" : ": no feedback for this table");
        if (was != STOPPING) *want = 0;
    }
    fprintf(stderr, "[dof] switching all outputs off\n");
    if (start("--all-off", NULL)) state = SWITCHING_OFF;
}

static void reap(void)
{
    int code, sig;
    if (HAVE_CHILD && ended(&code, &sig)) exited(code, sig);
}

void dof_tick(void)
{
    if (!*program) return;
    listen_to_it();                  /* before judging: the game may be what stood still */
    reap();
    switch (state) {
    case NONE:
        if (!*want || no_more) break;
        if (start("--table", want)) state = STARTING;
        else *want = 0;
        break;
    case STARTING:
        if (HAVE_FROM && age_ms(since) > START_MS) give_up("did not start");
        break;
    case RUNNING:                    /* however long nothing is said: that is no fault */
        break;
    case STOPPING:
        if (HAVE_FROM && age_ms(since) > STOP_MS) give_up("did not finish");
        break;
    case SWITCHING_OFF:
        if (HAVE_FROM && age_ms(since) > START_MS) give_up("did not finish switching off");
        break;
    }
}

static void on_table(const char *name, const char *bumpers, const char *sides)
{
    stop();
    snprintf(want, sizeof want, "%s", name ? name : "");
    snprintf(want_bumpers, sizeof want_bumpers, "%s", name && bumpers ? bumpers : "");
    snprintf(want_sides, sizeof want_sides, "%s", name && sides ? sides : "");
}

static void on_event(FbEvent e, int on)
{
    if (state != RUNNING) return;    /* late is wrong: not queued */
    char line[40];
    snprintf(line, sizeof line, "e %s %d\n", feedback_name(e), on ? 1 : 0);
    say(line);
}

static void on_all_off(void)
{
    if (state == RUNNING) say("o\n");
}

static const FbSink k_sink = { on_table, on_event, on_all_off };

int dof_open(const char *helper, const char *savedir)
{
    if (!runnable(helper)) {
        fprintf(stderr, "--dof needs %s: run " BUILD_HINT " (docs/cabinet-feedback.md)\n", helper);
        return 0;
    }
    snprintf(save, sizeof save, "%s", savedir);
    snprintf(program, sizeof program, "%s", helper);
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);        /* a dead helper is an error from write */
#endif
    state = NONE;
    child = 0;
    no_more = 0;
    *want = 0;
    feedback_sink(&k_sink);
    return 1;
}

void dof_close(void)
{
    if (!*program) return;
    feedback_close();
    *want = 0;
    stop();
    const uint64_t t = host_us();    /* the one place that waits: the game is over */
    while (HAVE_CHILD && age_ms(t) < EXIT_WAIT) {
        listen_to_it();
        reap();
        if (HAVE_CHILD) nap_ms(5);
    }
    if (HAVE_CHILD) fprintf(stderr, "[dof] the helper is still switching off; it ends by itself\n");
    shut_to();
    shut_from();
    *program = 0;
}
