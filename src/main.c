/*
 * main.c - boot PINBALL.EXE, lifted, on the DOS machine. It EXECs the tables
 * itself, exactly as it would under DOS (src/dos.c, src/images.c).
 *
 *   epicpinball [options] [-- arguments for PINBALL.EXE]
 *
 *   --data DIR        the game's files (default: original)
 *   --save DIR        CONFIG.PIN, high scores, sound setup (default: saves)
 *   --headless        no window, no audio device; time is deterministic
 *   --realtime        headless, but run on the wall clock
 *   --record OUT.mp4  record video + audio through ffmpeg (combine with
 *                     --headless for an unattended capture)
 *   --seconds N       quit after N seconds of emulated time
 *   --keys LIST       scripted input: "ms:KEY[+holdms],..." e.g. "4000:SPACE,6000:LSHIFT+300"
 *   --mute            silence
 *   --window N        window size, multiples of 320x240 (default 3, or where it was)
 *   --fullscreen      start fullscreen (F11, Alt+Enter toggle)
 *   --scale MODE      sharp, smooth, crt, nearest or integer (F12 cycles)
 *   --look MODE       natural or vivid (F9)
 *   --whole-table     the whole table, no scrolling (F7)
 *   --bars MODE       beside the picture: blur or black
 *   --classic         the game's own picture: nearest, no extras, GDI
 *   --cabinet         a virtual pinball machine (Linux): the whole table on
 *                     one display (F6 turns it), the scoreboard in a window
 *                     of its own, neither with a frame
 *   --rotate DEG      with --cabinet, the playfield turned clockwise: 0, 90, 180, 270
 *   --stretch         with --cabinet, the playfield fills its display, out of shape
 *   --playfield-display N  with --cabinet, the playfield's display (it lists them)
 *   --score-window X,Y,W,H  with --cabinet, the scoreboard's place
 *   --present WxH     --shot-at frames through the presenter at that size
 * Settings changed in the window are kept in epicpinball.ini in the save
 * folder; the options above win over it, and headless runs read only them.
 *   --feedback-log FILE  what a cabinet would feel, as a list: flippers, slings,
 *                     bumpers, launch, drain ... (docs/cabinet-feedback.md)
 *   --dmd             a 128x32 DMD of the table: with --cabinet in the
 *                     scoreboard's window, else a window of its own; the
 *                     messages of tables 1-8 leave the playfield for it
 *                     (docs/dmd.md)
 *   --table N         straight to table N (1-13) and a one-player game; quitting
 *                     the table quits the program, PINBALL saving as it does
 *                     (docs/table.md)
 *   --ssf             7.1 for a cabinet with exciters: front left and right
 *                     the game's sound, the side and rear pairs the board's
 *                     sounds where on the table they happen (docs/ssf.md)
 *   --dmd-device      the DMD on a real one (a ZeDMD) through libdmdutil;
 *                     build it with dmd/build.sh (Linux)
 *   --dof             table events to libdof, with VPinball's DOF files or
 *                     directoutputconfig/ in the save folder; no limits, they
 *                     are the controller's. Build it with dof/build.sh
 *                     (Linux; docs/cabinet-feedback.md first)
 *   --joysticks       list joysticks and their axes as they move, to set up
 *                     [plunger] in controller.ini (Linux)
 *   --plunger-trace LIST  a scripted plunger sensor: "ms:percent,..." pulled,
 *                     straight lines between, "-" for unplugged
 *   --trace           log DOS calls to stderr
 *   --shot FILE.bmp   write the last frame at exit
 *   --shot-at LIST    frames at emulated times: "ms:file.bmp,..."
 *   --wav OUT.wav     write the audio (no ffmpeg needed)
 */
#include "machine.h"
#include "feedback.h"
#include "dof_pipe.h"
#include "plunger.h"
#include "watch.h"
#include "dmd.h"
#include "dmd_pipe.h"
#include "ssf.h"
#include "autostart.h"
#include <setjmp.h>
#include <stdarg.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <sys/resource.h>
#endif

Options g_opt;
extern jmp_buf g_exit_jmp;
extern int g_exit_code;
void host_snapshot(const char *path);
int host_parse_turn(const char *s);
int pad_list(double seconds);
int host_parse_place(const char *s, RECT *r);

void trace(const char *fmt, ...)
{
    if (!g_opt.trace) return;
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}

/* What is wrong with the cabinet options, or NULL */
static const char *cabinet_check(void)
{
    if (!g_opt.cabinet) {
        if (g_opt.rotate || g_opt.stretch || g_opt.display || g_opt.score_window)
            return "--rotate, --stretch, --playfield-display and --score-window need --cabinet";
        return NULL;
    }
#ifdef _WIN32
    return "--cabinet is not supported on Windows yet (Linux only for now)";
#else
    RECT r;
    if (g_opt.classic) return "--cabinet and --classic do not go together";
    if (g_opt.window) return "--window is the ordinary window's size: with --cabinet use --score-window X,Y,W,H";
    if (g_opt.rotate && host_parse_turn(g_opt.rotate) < 0) return "--rotate takes 0, 90, 180 or 270";
    if (g_opt.display && (!*g_opt.display || g_opt.display[strspn(g_opt.display, "0123456789")]))
        return "--playfield-display takes a display's number, from 0";
    if (g_opt.score_window && !host_parse_place(g_opt.score_window, &r))
        return "--score-window takes X,Y,W,H, at least 32x8, e.g. 1920,0,1280,116";
    return NULL;
#endif
}

void fatal(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "error: "); vfprintf(stderr, fmt, ap); fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

static void mkdirs(const char *p)
{
    char b[512]; snprintf(b, sizeof b, "%s", p);
    for (char *s = b + 1; *s; s++)
        if (*s == '/' || *s == '\\') { char c = *s; *s = 0; _mkdir(b); *s = c; }
    _mkdir(b);
}

int main(int argc, char **argv)
{
    static char tail[256];
    int list_joysticks = 0;
    const char *volatile shot = NULL;    /* read after the longjmp */
#ifndef _WIN32
    /* Windows links a 16 MB stack (CMakeLists.txt). Linux grows the main
     * thread's on demand up to RLIMIT_STACK, so raising that is enough. */
    struct rlimit rl;
    if (getrlimit(RLIMIT_STACK, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur < (16u << 20)) {
        rl.rlim_cur = rl.rlim_max == RLIM_INFINITY || rl.rlim_max >= (16u << 20) ? 16u << 20 : rl.rlim_max;
        setrlimit(RLIMIT_STACK, &rl);
    }
#endif
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (!strcmp(a, "--data") && v) { g_opt.datadir = v; i++; }
        else if (!strcmp(a, "--save") && v) { g_opt.savedir = v; i++; }
        else if (!strcmp(a, "--headless")) g_opt.headless = 1;
        else if (!strcmp(a, "--record") && v) { g_opt.record = v; i++; }
        else if (!strcmp(a, "--seconds") && v) { g_opt.seconds = atof(v); i++; }
        else if (!strcmp(a, "--keys") && v) { g_opt.keys = v; i++; }
        else if (!strcmp(a, "--shot-at") && v) { g_opt.shots = v; i++; }
        else if (!strcmp(a, "--wav") && v) { g_opt.wav = v; i++; }
        else if (!strcmp(a, "--mute")) g_opt.mute = 1;
        else if (!strcmp(a, "--trace")) g_opt.trace = 1;
        else if (!strcmp(a, "--feedback-log") && v) { g_opt.feedback_log = v; i++; }
        else if (!strcmp(a, "--dof")) g_opt.dof = 1;
        else if (!strcmp(a, "--dmd")) g_opt.dmd = 1;
        else if (!strcmp(a, "--dmd-device")) g_opt.dmd_device = 1;
        else if (!strcmp(a, "--ssf")) g_opt.ssf = 1;
        else if (!strcmp(a, "--table") && v) {
            char *e; long n = strtol(v, &e, 10);
            if (*e || n < 1 || n > 13) { fprintf(stderr, "--table: a table from 1 to 13, not %s\n", v); return 2; }
            g_opt.table = (int)n; i++;
        }
        else if (!strcmp(a, "--joysticks")) list_joysticks = 1;
        else if (!strcmp(a, "--plunger-trace") && v) { g_opt.plunger_trace = v; i++; }
        else if (!strcmp(a, "--window") && v) { g_opt.window = atoi(v); i++; }
        else if (!strcmp(a, "--scale") && v) { g_opt.scale = v; i++; }
        else if (!strcmp(a, "--look") && v) { g_opt.look = v; i++; }
        else if (!strcmp(a, "--bars") && v) { g_opt.bars = v; i++; }
        else if (!strcmp(a, "--whole-table")) g_opt.whole = 1;
        else if (!strcmp(a, "--classic")) g_opt.classic = 1;
        else if (!strcmp(a, "--cabinet")) g_opt.cabinet = 1;
        else if (!strcmp(a, "--rotate") && v) { g_opt.rotate = v; i++; }
        else if (!strcmp(a, "--stretch")) g_opt.stretch = 1;
        else if (!strcmp(a, "--playfield-display") && v) { g_opt.display = v; i++; }
        else if (!strcmp(a, "--score-window") && v) { g_opt.score_window = v; i++; }
        else if (!strcmp(a, "--present") && v) { sscanf(v, "%dx%d", &g_opt.present_w, &g_opt.present_h); i++; }
        else if (!strcmp(a, "--fullscreen")) g_opt.fullscreen = 1;
        else if (!strcmp(a, "--realtime")) g_opt.realtime = 1;
        else if (!strcmp(a, "--shot") && v) { shot = v; i++; }
        else if (!strcmp(a, "--")) {
            for (i++; i < argc; i++) {
                if (tail[0]) strncat(tail, " ", sizeof tail - strlen(tail) - 1);
                strncat(tail, argv[i], sizeof tail - strlen(tail) - 1);
            }
        } else { fprintf(stderr, "unknown option %s (see the top of src/main.c)\n", a); return 2; }
    }
    if (list_joysticks) return pad_list(g_opt.seconds);
    const char *wrong = cabinet_check();
    if (!wrong && g_opt.plunger_trace && !plunger_trace(g_opt.plunger_trace))
        wrong = "--plunger-trace takes ms:percent,... in order of time, e.g. 20000:0,20500:100,20540:0";
#ifdef _WIN32
    if (!wrong && g_opt.ssf) wrong = "--ssf is not supported on Windows yet (Linux only for now)";
#endif
    if (!wrong && g_opt.dof && g_opt.headless && !g_opt.realtime)
        wrong = "--dof needs a window or --realtime: libdof times pulses by the wall clock";
    if (wrong) { fprintf(stderr, "%s\n", wrong); return 2; }
    if (!g_opt.datadir) g_opt.datadir = "original";
    if (!g_opt.savedir) g_opt.savedir = "saves";
    g_opt.args = tail;
    mkdirs(g_opt.savedir);
    if (g_opt.feedback_log) {
        if (!feedback_log(g_opt.feedback_log)) fatal("cannot write %s", g_opt.feedback_log);
        watch_wanted();
    }
    if (g_opt.dof) {
        if (!dof_open(DOF_HELPER, g_opt.savedir)) return 2;
        watch_wanted();
    }
    if (g_opt.plunger_trace) watch_wanted();
    if (g_opt.dmd || g_opt.dmd_device || g_opt.cabinet) dmd_wanted();   /* a cabinet hides keyboard messages */
    if (g_opt.ssf) { ssf_wanted(); watch_wanted(); }              /* SSF places sounds by what watch.c follows */
    if (g_opt.table) { autostart_wanted(g_opt.table); watch_wanted(); }   /* the watcher says when its game runs */
    if (g_opt.dmd_device && !dmd_pipe_open(DMD_HELPER, g_opt.savedir)) return 2;
    atexit(feedback_close);
    atexit(dmd_pipe_close);            /* fatal() does not come through host_shutdown */
    atexit(dof_close);

    if (cpu_alloc_mem(&g_cpu) != 0) return 1;
    vga_init();
    audio_init();
    machine_init();
    host_init();
    dos_boot(&g_cpu);
    vga_start();

    if (!setjmp(g_exit_jmp)) {
        void (*entry)(CPU *) = images_lookup(g_cpu.cs, g_cpu.ip);
        if (!entry) fatal("PINBALL.EXE is not lifted -- run tools/lift.py, then build");
        entry(&g_cpu);
        fprintf(stderr, "[dos] the program returned without exiting\n");
    }
    machine_report();
    { void vgaprof_report(void); vgaprof_report(); }
    if (shot) host_snapshot(shot);
    host_shutdown();
    return g_exit_code;
}
