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
 */
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE                  /* pipe2 */
#endif
#include "dof_pipe.h"
#include "feedback.h"
#include "machine.h"

#ifdef _WIN32
int dof_open(const char *helper, const char *savedir)
{
    (void)helper; (void)savedir;
    fprintf(stderr, "--dof is not supported on Windows yet (Linux only for now)\n");
    return 0;
}
void dof_tick(void) { }
void dof_close(void) { }
#else
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define START_MS 15000
#define STOP_MS   8000
#define EXIT_WAIT 1500               /* at the game's own exit only */

/* the helper's exit codes */
enum { EXIT_DONE = 0, EXIT_CANNOT_START = 2, EXIT_NO_ROWS = 5 };

static enum { NONE, STARTING, RUNNING, STOPPING, SWITCHING_OFF } state;
static char program[512], save[512];
static pid_t child;
static int to = -1, from = -1;
static int was_ready;                /* this child could have switched something on */
static int lost;                     /* killed by us, or exited wrongly: switch off after it */
static int no_more;                  /* for the rest of the run */
static char want[16];                /* the table that should have a helper */
static char want_bumpers[8];         /* its bumpers' sides, "LCR" */
static char in[128];
static size_t in_len;
static uint64_t since;

static double age_ms(uint64_t t) { return (host_us() - t) / 1000.0; }

static void shut(int *fd)
{
    if (*fd >= 0) close(*fd);
    *fd = -1;
}

static int start(const char *arg, const char *table)
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
    char *argv[] = { program, (char *)"--save", save, (char *)arg, (char *)table, NULL, NULL, NULL };
    if (table && *want_bumpers) { argv[5] = (char *)"--bumpers"; argv[6] = want_bumpers; }
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
    kill(child, SIGKILL);
    shut(&to);
    shut(&from);
}

static void say(const char *line)
{
    if (to < 0) return;
    size_t n = strlen(line);         /* far under PIPE_BUF: all of it or none */
    if (write(to, line, n) == (ssize_t)n) return;
    if (errno == EAGAIN) give_up("is not reading");
    else shut(&to);                  /* it has gone: its exit says the rest */
}

static void stop(void)
{
    if (state != STARTING && state != RUNNING) return;
    say("q\n");
    shut(&to);
    state = STOPPING;
    since = host_us();
}

static void listen_to_it(void)
{
    char buf[512];
    ssize_t n;
    while (from >= 0 && (n = read(from, buf, sizeof buf)) > 0) {
        for (ssize_t i = 0; i < n; i++) {
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

static void exited(int status)
{
    const int was = state;
    const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    shut(&to);
    shut(&from);
    child = 0;
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
        fprintf(stderr, "[dof] the helper stopped (%s %d)%s\n", code < 0 ? "signal" : "exit",
                code < 0 ? WTERMSIG(status) : code, was == STOPPING ? "" : ": no feedback for this table");
        if (was != STOPPING) *want = 0;
    }
    fprintf(stderr, "[dof] switching all outputs off\n");
    if (start("--all-off", NULL)) state = SWITCHING_OFF;
}

static void reap(void)
{
    int status;
    if (!child) return;
    pid_t r = waitpid(child, &status, WNOHANG);
    if (r == child) exited(status);
    else if (r < 0 && errno == ECHILD) exited(0xFF00);
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
        if (from >= 0 && age_ms(since) > START_MS) give_up("did not start");
        break;
    case RUNNING:                    /* however long nothing is said: that is no fault */
        break;
    case STOPPING:
        if (from >= 0 && age_ms(since) > STOP_MS) give_up("did not finish");
        break;
    case SWITCHING_OFF:
        if (from >= 0 && age_ms(since) > START_MS) give_up("did not finish switching off");
        break;
    }
}

static void on_table(const char *name, const char *bumpers)
{
    stop();
    snprintf(want, sizeof want, "%s", name ? name : "");
    snprintf(want_bumpers, sizeof want_bumpers, "%s", name && bumpers ? bumpers : "");
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
    if (access(helper, X_OK)) {
        fprintf(stderr, "--dof needs %s: run dof/build.sh (docs/cabinet-feedback.md)\n", helper);
        return 0;
    }
    snprintf(save, sizeof save, "%s", savedir);
    snprintf(program, sizeof program, "%s", helper);
    signal(SIGPIPE, SIG_IGN);        /* a dead helper is an error from write */
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
    while (child && age_ms(t) < EXIT_WAIT) {
        listen_to_it();
        reap();
        if (child) usleep(5000);
    }
    if (child) fprintf(stderr, "[dof] the helper is still switching off; it ends by itself\n");
    shut(&to);
    shut(&from);
    *program = 0;
}
#endif
