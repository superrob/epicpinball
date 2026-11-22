// SPDX-License-Identifier: GPL-3.0-or-later
// epicpinball-dof - tells libdof which table is up and what happened on it.
//
//   epicpinball-dof --table epN [--save saves]
//   epicpinball-dof --table epN --test EVENT      that one event, for trying a row
//   epicpinball-dof --all-off                     connect, switch everything off, leave
//
// The DOF files say what an event does (locate.h finds them), and the
// controller keeps any limits: this passes events on as they come and holds
// nothing back, shortens nothing, times nothing.
//
// A process of its own because libdof can crash, throw or hang, carries queued
// events from one table into the next, and is GPL. The game talks to it in
// lines, on stdin:
//
//   e NAME 0|1      an event
//   o               the game stands still: what is held is let go
//   q               finish (so does end of file)
//
// "ready" on stdout means libdof is up. Exit: 0 finished, 2 cannot start this
// table (nothing was switched on), 3 failed, 4 hung, 5 no table has a row.
#include "locate.h"

#include <DOF/Config.h>
#include <DOF/DOF.h>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstring>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

enum { DONE = 0, CANNOT_START = 2, FAILED = 3, HUNG = 4, NOTHING_TO_DO = 5 };

constexpr const char *kRom = "epicpinball";

// The game's names for what it reports (src/feedback.h), in its order: E101 on.
// The numbers are what people's rows are written in, so never renumber them.
constexpr const char *kEvent[] = {
    "flipper_left", "flipper_right", "sling_left", "sling_right",
    "bumper_left", "bumper_center", "bumper_right",
    "launch", "ball_release", "drain", "nudge", "tilt", "knocker",
};
constexpr int kEvents = sizeof kEvent / sizeof *kEvent;
constexpr int kFlippers = 2;                    // levels; the rest only happen
constexpr int kFirstE = 101;

// Only a hung libdof meets these; the game's own timeouts come first. They are
// for a helper whose game has already gone.
constexpr unsigned kStartS = 20, kFinishS = 10;

std::string g_save;
bool g_all_off;
volatile sig_atomic_t g_quit;
std::atomic<bool> g_pico_found{false};

void say(const std::string &s)
{
    std::fprintf(stderr, "[dof] %s\n", s.c_str());
}

[[noreturn]] void leave(int code)
{
    std::fflush(stderr);
    _exit(code);                                // libdof's destructors can block or crash
}

// libdof hung starting or finishing: only a fresh process can still switch off.
void hung(int)
{
    static const char gave_up[] = "[dof] libdof hung while switching off: check the board\n";
    static const char again[] = "[dof] libdof hung: starting again only to switch everything off\n";
    if (g_all_off) {
        if (write(STDERR_FILENO, gave_up, sizeof gave_up - 1) < 0) {}
        _exit(HUNG);
    }
    if (write(STDERR_FILENO, again, sizeof again - 1) < 0) {}
    char me[] = "epicpinball-dof", off[] = "--all-off", save[] = "--save";
    char *argv[] = { me, off, save, g_save.data(), nullptr };
    execv("/proc/self/exe", argv);
    _exit(HUNG);
}

// libdof reports trouble only in its log, and not all of it as errors. It is
// passed on, never acted on.
void dof_log(DOF_LogLevel level, const char *fmt, va_list ap)
{
    char line[1024];
    std::vsnprintf(line, sizeof line, fmt, ap);
    if (std::strstr(line, "Pinscape Pico discovery found unit #")) g_pico_found = true;
    static const char *const trouble[] = {
        "not all bytes sent", "could not connect to device", "Could not send update", "Failed to send ArtNet",
    };
    static std::atomic<bool> told[std::size(trouble)];
    for (size_t i = 0; i < std::size(trouble); i++)
        if (std::strstr(line, trouble[i])) {
            if (!told[i].exchange(true)) say(std::string("libdof (said once): ") + line);
            return;
        }
    // no GlobalConfig is normal: libdof then finds the boards itself
    if (level == DOF_LogLevel_ERROR && !std::strstr(line, "no global config loaded"))
        say(std::string("libdof: ") + line);
}

int event_by_name(const std::string &s)
{
    for (int i = 0; i < kEvents; i++)
        if (s == kEvent[i]) return i;
    return -1;
}

// "ep7" is epicpinball_ep07. The zero matters: libdof also takes a row whose
// name the table's begins with, so ep1 would be ep13's too.
std::string rom_of(const std::string &table)
{
    int n = 0, used = 0;
    if (std::sscanf(table.c_str(), "ep%2d%n", &n, &used) != 1 || used != (int)table.size() || n < 1 || n > 13)
        return "";
    char rom[32];
    std::snprintf(rom, sizeof rom, "%s_ep%02d", kRom, n);
    return rom;
}

class Sender {
public:
    explicit Sender(DOF::DOF *dof) : dof_(dof) {}

    void event(int e, bool on)
    {
        if (e < kFlippers) {
            held_[e] = on;
            send(e, on);
        } else if (on) {                        // the row says how long it lasts
            send(e, true);
            send(e, false);
        }
    }

    void release()
    {
        for (int e = 0; e < kFlippers; e++)
            if (held_[e]) event(e, false);
    }

private:
    void send(int e, bool on) { dof_->DataReceive('E', kFirstE + e, on ? 1 : 0); }

    DOF::DOF *dof_;
    bool held_[kFlippers] = {};
};

int usage()
{
    say("usage: epicpinball-dof (--table epN [--test EVENT] | --all-off) [--save DIR]");
    std::string names;
    for (auto *e : kEvent) names += std::string(" ") + e;
    say("events:" + names);
    return CANNOT_START;
}

// 0: there is a row for this table. Else the exit code, said.
int check_rows(DOF::DOF *dof, const locate::Files &at, const std::string &rom)
{
    bool exact = false, every = false, any = false;
    for (auto &name : dof->GetLedControlRomNames(at.table.c_str())) {
        const std::string n = locate::lower(name);
        exact |= n == rom;
        every |= n == kRom;
        any |= n.starts_with(kRom);
    }
    if (exact || every) return 0;
    if (any) {
        say("no " + rom + " and no " + kRom + " row: no feedback on this table");
        return CANNOT_START;
    }
    say(std::string("no ") + kRom + " row in " + at.rows.string()
        + ", so nothing would fire (docs/cabinet-feedback.md)");
    return NOTHING_TO_DO;
}

// The game's lines, until it says q or goes
int serve(Sender &send, int out, pid_t parent)
{
    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
    if (write(out, "ready\n", 6) < 0) {}        // a game that is gone shows as end of file
    std::string in;
    while (!g_quit) {
        if (getppid() != parent) break;         // the game died and someone else holds the pipe
        pollfd p{ STDIN_FILENO, POLLIN, 0 };
        if (poll(&p, 1, 250) <= 0) continue;
        char buf[2048];
        const ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) break;
        if (n > 0) in.append(buf, (size_t)n);
        for (size_t nl; (nl = in.find('\n')) != std::string::npos;) {
            const std::string line = in.substr(0, nl);
            in.erase(0, nl + 1);
            std::istringstream w(line);
            std::string op, name, extra;
            int on;
            w >> op;
            if (op == "e" && (w >> name >> on) && (on == 0 || on == 1) && event_by_name(name) >= 0
                && !(w >> extra)) {
                send.event(event_by_name(name), on != 0);
            } else if (line == "o") {
                send.release();
            } else if (line == "q") {
                return DONE;
            } else {
                say("a line it does not understand");
                return FAILED;
            }
        }
        if (in.size() > 4096) {
            say("a line without an end");
            return FAILED;
        }
    }
    return DONE;
}

int run(int argc, char **argv)
{
    std::string table, test, rom;
    g_save = "saves";
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        const bool v = i + 1 < argc;
        if (a == "--all-off") g_all_off = true;
        else if (a == "--save" && v) g_save = argv[++i];
        else if (a == "--table" && v) table = argv[++i];
        else if (a == "--test" && v) test = argv[++i];
        else return usage();
    }
    if (g_all_off) {
        if (!table.empty() || !test.empty()) return usage();
    } else {
        rom = rom_of(table);
        if (rom.empty() || (!test.empty() && event_by_name(test) < 0)) return usage();
    }

    const pid_t parent = getppid();
    std::error_code e;
    const fs::path save = fs::absolute(g_save, e).lexically_normal();
    g_save = save.string();
    const locate::Files at = locate::find(save, rom);
    if (!g_all_off)
        for (auto &s : at.said) say(s);

    // libdof also reads, and by default logs, in the working directory
    const fs::path work = save / "dof";
    fs::create_directories(work, e);
    if (e || chdir(work.c_str())) {
        say("cannot use " + work.string());
        return CANNOT_START;
    }

    const int out = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 10);
    dup2(STDERR_FILENO, STDOUT_FILENO);         // whatever libdof prints is not protocol
    std::signal(SIGPIPE, SIG_IGN);
    struct sigaction sa {};
    sa.sa_handler = hung;
    sa.sa_flags = SA_NODEFER;                   // or the restarted process inherits it blocked
    sigaction(SIGALRM, &sa, nullptr);

    auto *cfg = DOF::Config::GetInstance();
    cfg->SetLogCallback(dof_log);
    cfg->SetLogLevel(DOF_LogLevel_ERROR);       // info and warnings come below this
    cfg->SetBasePath((at.base.string() + "/").c_str());

    alarm(kStartS);
    auto *dof = new DOF::DOF();                 // this already looks for Pinscapes over HID
    if (g_all_off) {
        dof->Init("", "");                      // no table, so no row can switch anything on
        std::this_thread::sleep_for(150ms);
        alarm(kFinishS);
        dof->Finish();
        say("all outputs switched off");
        return DONE;
    }

    if (const int no = check_rows(dof, at, rom)) return no;
    dof->Init(at.table.c_str(), rom.c_str());
    alarm(0);
    if (!g_pico_found && std::any_of(at.numbers.begin(), at.numbers.end(),
                                     [](int n) { return n >= 120 && n <= 135; }))
        say("there are Pinscape Pico files, and no Pico answers (plugged in? may this user open its hidraw device?)");

    sa.sa_handler = [](int) { g_quit = 1; };
    sa.sa_flags = 0;
    for (int s : { SIGTERM, SIGINT, SIGHUP }) sigaction(s, &sa, nullptr);

    Sender send(dof);
    int code = DONE;
    if (test.empty()) {
        code = serve(send, out, parent);
    } else {
        std::this_thread::sleep_for(300ms);     // libdof's controllers connect on threads of their own
        const int ev = event_by_name(test);
        say(rom + ": " + test + " (E" + std::to_string(kFirstE + ev) + ")");
        send.event(ev, true);
        std::this_thread::sleep_for(500ms);     // long enough to see a flipper held
        send.release();
        for (int i = 0; i < 30 && !g_quit; i++) std::this_thread::sleep_for(100ms);
    }

    alarm(kFinishS);
    send.release();
    dof->Finish();
    return code;
}

} // namespace

int main(int argc, char **argv)
{
    int code;
    try {
        code = run(argc, argv);
    } catch (const std::exception &e) {
        say(std::string("libdof gave up: ") + e.what());
        code = FAILED;
    } catch (...) {
        say("libdof gave up");
        code = FAILED;
    }
    leave(code);
}
