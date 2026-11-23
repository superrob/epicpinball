/*
 * dmd_pipe.c - --dmd-device: the DMD picture to dmd/build/epicpinball-dmd.
 *
 * One helper for the whole run, so the display is looked for once. Frames go
 * down a socket pair of packets: one frame a packet, never half of one, and
 * never waited for. A frame the socket has no room for waits for the next
 * host frame, and is replaced by a newer one meanwhile. A helper that goes
 * leaves the game running, without a DMD.
 */
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "dmd_pipe.h"
#include "dmd.h"
#include "machine.h"

#ifdef _WIN32
int dmd_pipe_open(const char *helper, const char *savedir)
{
    (void)helper; (void)savedir;
    fprintf(stderr, "--dmd-device is not supported on Windows yet (Linux only for now)\n");
    return 0;
}
void dmd_pipe_frame(const uint32_t *px) { (void)px; }
void dmd_pipe_tick(void) { }
void dmd_pipe_close(void) { }
int dmd_pipe_alive(void) { return 0; }
#else
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static int sock = -1;
static pid_t child;
static uint8_t pending[DMD_W * DMD_H * 3];
static int waiting;                  /* a frame the socket had no room for */
static int have;                     /* a frame to send again: a display found late gets it */
static uint64_t sent_at;

int dmd_pipe_alive(void) { return sock >= 0; }

static void gone(const char *why)
{
    fprintf(stderr, "[dmd] the helper %s: no DMD for the rest of the run\n", why);
    if (sock >= 0) close(sock);
    sock = -1;
    waiting = 0;
}

int dmd_pipe_open(const char *helper, const char *savedir)
{
    struct stat st;
    if (stat(helper, &st) != 0) {
        fprintf(stderr, "--dmd-device needs %s: run dmd/build.sh (docs/dmd.md)\n", helper);
        return 0;
    }
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sv)) {
        fprintf(stderr, "[dmd] no socket pair: %s\n", strerror(errno));
        return 0;
    }
    static char program[512], ini[600];
    snprintf(program, sizeof program, "%s", helper);
    snprintf(ini, sizeof ini, "%s/dmd/DMDUtil.ini", savedir);
    posix_spawn_file_actions_t fa;
    posix_spawnattr_t at;
    sigset_t s;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, sv[1], 0);
    posix_spawnattr_init(&at);
    sigemptyset(&s);
    sigaddset(&s, SIGPIPE);
    posix_spawnattr_setsigdefault(&at, &s);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSIGDEF);
    char *argv[] = { program, (char *)"--ini", ini, NULL };
    int err = posix_spawn(&child, program, &fa, &at, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    close(sv[1]);
    if (err) {
        close(sv[0]);
        fprintf(stderr, "[dmd] cannot run %s: %s\n", program, strerror(err));
        return 0;
    }
    sock = sv[0];
    signal(SIGPIPE, SIG_IGN);
    return 1;
}

static void send_pending(void)
{
    if (sock < 0 || !waiting) return;
    ssize_t n = send(sock, pending, sizeof pending, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (n == (ssize_t)sizeof pending) { waiting = 0; sent_at = host_us(); }
    else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return;   /* next frame */
    else gone(n < 0 ? strerror(errno) : "took part of a frame");
}

void dmd_pipe_frame(const uint32_t *px)
{
    if (sock < 0) return;
    for (int i = 0; i < DMD_W * DMD_H; i++) {
        pending[3 * i] = (uint8_t)(px[i] >> 16);
        pending[3 * i + 1] = (uint8_t)(px[i] >> 8);
        pending[3 * i + 2] = (uint8_t)px[i];
    }
    waiting = have = 1;
    send_pending();
}

void dmd_pipe_tick(void)
{
    if (have && !waiting && host_us() - sent_at > 1000000) waiting = 1;   /* once a second, unchanged */
    send_pending();
    if (child > 0) {
        int status;
        if (waitpid(child, &status, WNOHANG) == child) {
            child = 0;
            if (sock >= 0) gone("ended");
        }
    }
}

void dmd_pipe_close(void)
{
    if (sock >= 0) { close(sock); sock = -1; }      /* the helper blanks the display and ends */
    if (child > 0) {
        for (int i = 0; i < 40; i++) {               /* up to 2 s */
            int status;
            if (waitpid(child, &status, WNOHANG) == child) { child = 0; return; }
            usleep(50000);
        }
        kill(child, SIGKILL);
        waitpid(child, NULL, 0);
        child = 0;
    }
}
#endif
