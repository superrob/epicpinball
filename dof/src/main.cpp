// SPDX-License-Identifier: GPL-3.0-or-later
// epicpinball-dof - tells libdof which table is up and what happened on it.
//
//   epicpinball-dof --table epN [--bumpers LCR] [--features "hole1=TestHole:L ..."] [--save saves]
//   epicpinball-dof --table epN --test EVENT      that one event, for trying a row
//
// Events go to libdof by name, as a table without a ROM names them: a row
// says $FlipperLeft, $Bumper2, and for a table's own features the table's
// number and the feature's name, $t1PowerUpHole (events.h, autorow.h).
//
// --bumpers is the table's bumpers from the left, the side of each: L, C or R.
// --features names the table's own holes, kickback, rollovers, ramps and drop
// targets, each with its side. The game passes both; they say what a made row
// puts where, and what a feature is called.
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
// With no epicpinball row anywhere, one is made from the header line of the
// DOF files (autorow.h), into a folder of the helper's own.
//
// "ready" on stdout means libdof is up. Exit: 0 finished, 2 cannot start this
// table (nothing was switched on), 3 failed, 4 hung, 5 no table has a row.
//
// Linux and Windows alike; where they differ: stdin is read by poll() on
// Linux, by a thread of its own on Windows; a hung libdof is caught by alarm()
// and the process becomes an --all-off by exec on Linux, by a watchdog thread
// that runs an --all-off and passes on its exit code on Windows.
#include "autorow.h"
#include "events.h"
#include "locate.h"

#include <DOF/Config.h>
#include <DOF/DOF.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#include <condition_variable>
#include <mutex>
#else
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstring>
#include <iterator>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

enum { DONE = 0, CANNOT_START = 2, FAILED = 3, HUNG = 4, NOTHING_TO_DO = 5 };

constexpr const char *kRom = "epicpinball";

// Only a hung libdof meets these; the game's own timeouts come first. They are
// for a helper whose game has already gone.
constexpr unsigned kStartS = 20, kFinishS = 10;

std::string g_save;
bool g_all_off;
volatile sig_atomic_t g_quit;
std::atomic<bool> g_pico_found{false};
// The boards libdof's updater threads started on, those they got through to,
// and how many gave up: a serial LED controller takes a second or more
std::mutex g_boards_lock;
std::set<std::string> g_started, g_connected;
int g_gave_up;
std::string g_bumpers = "LCR";        // the table's bumpers' sides, from the left
autorow::Table g_table;               // its number, its own features' names and sides
std::atomic<int> g_unread{0};          // settings libdof could not read, and skips
int g_unread_once;                      // in one reading of the files
std::atomic<bool> g_show_unread{true};  // the game's own files: shown, not counted

void say(const std::string &s)
{
    std::fprintf(stderr, "[dof] %s\n", s.c_str());
}

#ifdef _WIN32
// Ends the process here and now: ExitProcess (and so _exit) would still run
// the DLLs' destructors, libdof's among them
[[noreturn]] void end_now(int code)
{
    TerminateProcess(GetCurrentProcess(), (UINT)code);
    for (;;) Sleep(INFINITE);
}

void raw_say(const char *s, size_t n)
{
    DWORD w;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), s, (DWORD)n, &w, nullptr);
}

// One argument onto a command line, as the C runtime will split it again
void quote(std::wstring &line, const std::wstring &a)
{
    if (!line.empty()) line += L' ';
    line += L'"';
    for (size_t i = 0;; i++) {
        size_t slashes = 0;
        while (i < a.size() && a[i] == L'\\') { i++; slashes++; }
        if (i == a.size()) {
            line.append(slashes * 2, L'\\');
            break;
        }
        if (a[i] == L'"') line.append(slashes * 2 + 1, L'\\');
        else line.append(slashes, L'\\');
        line += a[i];
    }
    line += L'"';
}

// What exec does on Linux: an --all-off in this process's place. Its exit code is ours.
[[noreturn]] void all_off_instead()
{
    wchar_t me[32768];
    const DWORD n = GetModuleFileNameW(nullptr, me, (DWORD)std::size(me));
    if (!n || n >= std::size(me)) end_now(HUNG);
    std::wstring line;
    quote(line, me);
    quote(line, L"--all-off");
    quote(line, L"--save");
    quote(line, fs::path(g_save).wstring());
    SECURITY_ATTRIBUTES sa{ sizeof sa, nullptr, TRUE };
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    HANDLE err = nullptr;
    DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_ERROR_HANDLE), GetCurrentProcess(), &err, 0, TRUE, DUPLICATE_SAME_ACCESS);
    HANDLE list[2];
    DWORD count = 0;
    if (nul != INVALID_HANDLE_VALUE) list[count++] = nul;
    if (err) list[count++] = err;
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<char> attrs(size);
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = nul;
    si.StartupInfo.hStdOutput = err;                // as on Linux: stdout is stderr by now
    si.StartupInfo.hStdError = err;
    si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrs.data());
    PROCESS_INFORMATION pi{};
    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &size)
        || !UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, list, count * sizeof *list, nullptr, nullptr)
        || !CreateProcessW(me, line.data(), nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &si.StartupInfo, &pi))
        end_now(HUNG);
    DWORD code = HUNG;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    end_now((int)code);
}
#endif

[[noreturn]] void leave(int code)
{
    std::fflush(stderr);
#ifdef _WIN32
    end_now(code);
#else
    _exit(code);                                // libdof's destructors can block or crash
#endif
}

// libdof hung starting or finishing: only a fresh process can still switch off.
void hung(int)
{
    static const char gave_up[] = "[dof] libdof hung while switching off: check the board\n";
    static const char again[] = "[dof] libdof hung: starting again only to switch everything off\n";
#ifdef _WIN32
    if (g_all_off) {
        raw_say(gave_up, sizeof gave_up - 1);
        end_now(HUNG);
    }
    raw_say(again, sizeof again - 1);
    all_off_instead();
#else
    if (g_all_off) {
        if (write(STDERR_FILENO, gave_up, sizeof gave_up - 1) < 0) {}
        _exit(HUNG);
    }
    if (write(STDERR_FILENO, again, sizeof again - 1) < 0) {}
    char me[] = "epicpinball-dof", off[] = "--all-off", save[] = "--save";
    char *argv[] = { me, off, save, g_save.data(), nullptr };
    execv("/proc/self/exe", argv);
    _exit(HUNG);
#endif
}

// hung() in this many seconds, unless armed again first; 0 disarms
void arm(unsigned seconds)
{
#ifdef _WIN32
    using clock = std::chrono::steady_clock;
    static std::atomic<long long> deadline{0};  // ms on the steady clock, 0: none
    static std::once_flag watchdog;
    const auto now = [] {
        return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count();
    };
    deadline = seconds ? now() + 1000LL * seconds : 0;
    std::call_once(watchdog, [now] {
        std::thread([now] {
            for (;;) {
                Sleep(100);
                const long long d = deadline;
                if (d && now() >= d) hung(0);
            }
        }).detach();
    });
#else
    alarm(seconds);
#endif
}

// libdof reports trouble only in its log, and not all of it as errors. It is
// passed on, never acted on.
void dof_log(DOF_LogLevel level, const char *fmt, va_list ap)
{
    char line[1024];
    std::vsnprintf(line, sizeof line, fmt, ap);
    if (std::strstr(line, "Pinscape Pico discovery found unit #")) g_pico_found = true;
    static const char kStarted[] = "Updater thread started for ", kConnected[] = "Updater thread connected to ";
    if (const char *p = std::strstr(line, kStarted)) {
        std::lock_guard<std::mutex> l(g_boards_lock);
        g_started.insert(p + sizeof kStarted - 1);
    } else if (const char *p2 = std::strstr(line, kConnected)) {
        std::lock_guard<std::mutex> l(g_boards_lock);
        g_connected.insert(p2 + sizeof kConnected - 1);
    } else if (std::strstr(line, "Could not connect to controller")) {
        std::lock_guard<std::mutex> l(g_boards_lock);
        g_gave_up++;
    }
    static const char *const trouble[] = {
        "not all bytes sent", "could not connect to device", "Could not send update", "Failed to send ArtNet",
    };
    static std::atomic<bool> told[std::size(trouble)];
    for (size_t i = 0; i < std::size(trouble); i++)
        if (std::strstr(line, trouble[i])) {
            if (!told[i].exchange(true)) say(std::string("libdof (said once): ") + line);
            return;
        }
    // A Config Tool export has some settings libdof cannot read, in other
    // games' rows; it skips them, under VPX too. Counted, not printed.
    if (std::strstr(line, "Could not parse") || std::strstr(line, "Cant parse")
        || std::strstr(line, "Exception(s) occured when parsing") || std::strstr(line, "Previous exceptions occured")) {
        if (g_show_unread) say(std::string("libdof: ") + line);
        else if (std::strstr(line, "Could not parse setting")) g_unread++;
        return;
    }
    // no GlobalConfig is normal: libdof then finds the boards itself
    if (level == DOF_LogLevel_ERROR && !std::strstr(line, "no global config loaded"))
        say(std::string("libdof: ") + line);
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
    Sender(DOF::DOF *dof, const autorow::Table &table) : dof_(dof)
    {
        for (int e = 0; e < kEvents; e++)
            if (!table.dof(e).empty()) name_[e] = "$" + table.dof(e);   // the $ keeps $S1 a name
    }

    void event(int e, bool on)
    {
        if (is_level(e)) {
            held_[e] = on;
            send(e, on);
        } else if (on) {                        // the row says how long it lasts
            send(e, true);
            send(e, false);
        }
    }

    void release()
    {
        for (int e = 0; e < kEvents; e++)
            if (held_[e]) event(e, false);
    }

private:
    // an event this table has no name for (a feature it does not have) is not sent
    void send(int e, bool on)
    {
        if (!name_[e].empty()) dof_->DataReceive(name_[e].c_str(), on ? 1 : 0);
    }

    DOF::DOF *dof_;
    std::string name_[kEvents];
    bool held_[kEvents] = {};
};

int usage()
{
    say("usage: epicpinball-dof (--table epN [--bumpers LCR] [--features \"hole1=TestHole:L ramp1=LeftRamp:L"
        " target1=DropTarget1:L bank=DropTargets ...\"] [--test EVENT] | --all-off) [--save DIR]");
    std::string names;
    for (auto *e : kDofName)
        if (*e) names += std::string(" ") + e;
    say("events:" + names);
    say("and a table's own features with its --features: t1PowerUpHole, t1LeftRamp ...;"
        " and its lamps, t1Lamp1 on (tools/feedback_map.py --lamps)");
    return CANNOT_START;
}

enum Rows { NONE, OTHER_TABLES, THIS_TABLE };

// What libdof would find for this table, looking where it looks
Rows rows(DOF::DOF *dof, const fs::path &table, const std::string &rom)
{
    bool exact = false, every = false, any = false;
    for (auto &name : dof->GetLedControlRomNames(table.string().c_str())) {
        const std::string n = locate::lower(name);
        exact |= n == rom;
        every |= n == kRom;
        any |= n.starts_with(kRom);
    }
    return exact || every ? THIS_TABLE : any ? OTHER_TABLES : NONE;
}

// No row: one from the header of the files libdof reads, or of VPinball's if
// the game's own have none. Written to work/auto, where it takes the place of
// those files for this game only. Empty path: none could be made, and
// `headers` says whether there was a header to make one from.
fs::path construct(const locate::Files &at, const fs::path &work, const std::string &rom, bool &headers)
{
    const fs::path out = work / "auto";
    std::error_code e;
    fs::create_directories(out, e);
    for (auto &f : locate::ini_files(out)) fs::remove(f.second, e);    // one table's, from last time
    std::vector<fs::path> dirs{ at.rows };
    if (!at.table.empty()) dirs.push_back(at.base / locate::kDir);
    headers = false;
    // libdof draws shapes from the DirectOutputShapes.xml beside its GlobalConfig, which is in at.rows,
    // and the .png of the same name beside that: without either, a shape draws nothing
    const bool shapes = fs::exists(at.rows / "DirectOutputShapes.xml", e) && fs::exists(at.rows / "DirectOutputShapes.png", e);
    for (auto &dir : dirs) {
        bool made = false, fires[kEvents] = {};
        for (auto &[number, path] : locate::ini_files(dir)) {
            const auto names = autorow::header_of(path.string());
            if (!names) continue;
            headers = true;
            const autorow::Row row = autorow::build(*names, g_bumpers, g_table, shapes);
            if (!shapes && autorow::detail::named(*names, "back mx"))
                say("no DirectOutputShapes.xml and .png in " + at.rows.string() + ": plain bands on the Back MX");
            for (auto &s : row.said) say(path.filename().string() + ": " + s);
            if (row.empty()) continue;
            for (int i = 0; i < kEvents; i++) fires[i] |= row.fires[i];
            say("no " + rom + " or " + kRom + " row: made one for bumpers " + g_bumpers + " from the header of " + path.string() + ", "
                + std::to_string(row.outputs) + " of " + std::to_string(row.width) + " outputs");
            const fs::path name = out / ("directoutputconfig" + (number == 1 ? std::string() : std::to_string(number)) + ".ini");
            std::ofstream f(name, std::ios::binary);
            f << autorow::file(rom, row, path.string());
            if (!f.flush()) {
                say("cannot write " + name.string());
                return {};
            }
            made = true;
        }
        if (made) {
            std::vector<int> none;                       // over all the files: a strip's alone has no solenoids
            for (int i = 0; i < kEvents; i++)
                if (!fires[i] && autorow::row_has(i, g_bumpers, g_table)) none.push_back(i);
            if (!none.empty()) say("no device for " + autorow::listed(none, g_table));
            return out / rom;
        }
        if (headers) {
            say("the headers in " + dir.string() + " name no device Epic Pinball has an event for");
            return {};
        }
    }
    return {};
}

// 0: there is a row for this table, and `table` is what libdof is told. Else the exit code, said.
int find_rows(DOF::DOF *dof, const locate::Files &at, const fs::path &work, const std::string &rom, fs::path &table)
{
    table = at.table;
    const Rows found = rows(dof, table, rom);
    g_unread_once = g_unread;
    if (found == THIS_TABLE) return 0;
    bool headers = false;
    const fs::path made = construct(at, work, rom, headers);
    if (!made.empty()) {
        if (rows(dof, made, rom) == THIS_TABLE) {
            table = made;
            return 0;
        }
        say("libdof does not see the row it was given: an IniFilesPath in the GlobalConfig comes first");
    }
    if (found == OTHER_TABLES) {
        say("no " + rom + " and no " + kRom + " row: no feedback on this table");
        return CANNOT_START;
    }
    say(std::string("no ") + kRom + " row in " + at.rows.string()
        + (headers ? std::string(", so nothing would fire (docs/cabinet-feedback.md)")
                   : ", and no header line to make one from, so nothing would fire (docs/cabinet-feedback.md)"));
    return NOTHING_TO_DO;
}

// Its lines so far, in `in`: -1 to go on, else how to end
int lines(std::string &in, Sender &send)
{
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
    return -1;
}

// libdof's controllers connect on threads of their own, and an event sent
// before a board is through is lost on it: a serial LED controller takes a
// second or more. So "ready" (and --test's event) wait for them, 10 s at most.
void wait_for_boards()
{
    std::this_thread::sleep_for(300ms);
    auto waiting = [] {
        std::lock_guard<std::mutex> l(g_boards_lock);
        std::string w;
        if (g_connected.size() + g_gave_up < g_started.size())
            for (auto &b : g_started)
                if (!g_connected.count(b)) w += (w.empty() ? "" : ", ") + b;
        return w;
    };
    for (int i = 0; i < 100 && !waiting().empty() && !g_quit; i++) std::this_thread::sleep_for(100ms);
    if (const std::string w = waiting(); !w.empty()) say("not connected after 10 s: " + w);
}

#ifdef _WIN32
// stdin, read by a thread of its own: a ReadFile waits for the game's next
// line, and the main thread must still see g_quit. It lives until the process
// ends, still waiting in ReadFile.
struct Input {
    std::mutex m;
    std::condition_variable cv;
    std::string data;
    bool end = false;
};
Input g_input;

int serve(Sender &send, HANDLE out)
{
    DWORD w;
    WriteFile(out, "ready\n", 6, &w, nullptr);  // a game that is gone shows as end of file
    std::thread([] {
        HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
        char buf[2048];
        DWORD n = 0;
        while (ReadFile(h, buf, sizeof buf, &n, nullptr) && n) {
            std::lock_guard l(g_input.m);
            g_input.data.append(buf, n);
            g_input.cv.notify_one();
        }
        std::lock_guard l(g_input.m);           // end of file: the game closed it, or has gone
        g_input.end = true;
        g_input.cv.notify_one();
    }).detach();
    std::string in;
    while (!g_quit) {
        bool end;
        {
            std::unique_lock l(g_input.m);
            g_input.cv.wait_for(l, 250ms, [] { return !g_input.data.empty() || g_input.end; });
            in += g_input.data;
            end = g_input.end && g_input.data.empty();
            g_input.data.clear();
        }
        if (const int code = lines(in, send); code >= 0) return code;
        if (end) break;
    }
    return DONE;
}
#else
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
        if (const int code = lines(in, send); code >= 0) return code;
    }
    return DONE;
}
#endif

int run(int argc, char **argv)
{
    std::string table, test, rom, features;
    bool layout = false;
    g_save = "saves";
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        const bool v = i + 1 < argc;
        if (a == "--all-off") g_all_off = true;
        else if (a == "--save" && v) g_save = argv[++i];
        else if (a == "--table" && v) table = argv[++i];
        else if (a == "--test" && v) test = argv[++i];
        else if (a == "--bumpers" && v) { g_bumpers = argv[++i]; layout = true; }
        else if (a == "--features" && v) features = argv[++i];
        else return usage();
    }
    if (g_bumpers.empty() || g_bumpers.size() > (size_t)kBumpers
        || g_bumpers.find_first_not_of("LCR") != std::string::npos) return usage();
    int test_event = -1;
    if (g_all_off) {
        if (!table.empty() || !test.empty() || !features.empty()) return usage();
    } else {
        rom = rom_of(table);
        if (rom.empty()) return usage();
        const auto t = autorow::features(autorow::table_of(rom), features);
        if (!t) {
            say("--features: not a table's features");
            return usage();
        }
        g_table = *t;
        if (!test.empty()) {
            for (int ev = 0; ev < kEvents; ev++)
                if (!g_table.dof(ev).empty() && autorow::lower(g_table.dof(ev)) == autorow::lower(test)) test_event = ev;
            if (test_event < 0) {
                say("no event " + test + (g_table.empty() ? " (a table's own feature needs its --features)" : ""));
                return usage();
            }
        }
        if (!layout && !test.empty())
            say("bumpers LCR assumed: give the table's with --bumpers (docs/cabinet-feedback.md)");
    }

#ifndef _WIN32
    const pid_t parent = getppid();
#endif
    std::error_code e;
    const fs::path save = fs::absolute(g_save, e).lexically_normal();
    g_save = save.string();
    const locate::Files at = locate::find(save, rom);
    g_show_unread = at.rows == save / locate::kDir;           // the game's own: shown; an export: only counted
    if (!g_all_off)
        for (auto &s : at.said) say(s);

    // libdof also reads, and by default logs, in the working directory
    const fs::path work = save / "dof";
    fs::create_directories(work, e);
    if (!e) fs::current_path(work, e);
    if (e) {
        say("cannot use " + work.string());
        return CANNOT_START;
    }

#ifdef _WIN32
    HANDLE out = nullptr;                       // not inheritable: an --all-off run must not hold it
    DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_OUTPUT_HANDLE), GetCurrentProcess(), &out, 0, FALSE,
                    DUPLICATE_SAME_ACCESS);
    std::fflush(stdout);
    _dup2(_fileno(stderr), _fileno(stdout));    // whatever libdof prints is not protocol
    SetStdHandle(STD_OUTPUT_HANDLE, GetStdHandle(STD_ERROR_HANDLE));
#else
    const int out = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 10);
    dup2(STDERR_FILENO, STDOUT_FILENO);         // whatever libdof prints is not protocol
    std::signal(SIGPIPE, SIG_IGN);
    struct sigaction sa {};
    sa.sa_handler = hung;
    sa.sa_flags = SA_NODEFER;                   // or the restarted process inherits it blocked
    sigaction(SIGALRM, &sa, nullptr);
#endif

    auto *cfg = DOF::Config::GetInstance();
    cfg->SetLogCallback(dof_log);
    cfg->SetLogLevel(DOF_LogLevel_ERROR);       // info and warnings come below this
    // DirectOutput's GlobalConfig, and with it its cabinet.xml (LED strip
    // controllers and other boards libdof does not find itself), as a copy
    // where libdof reads one
    fs::path base = at.base;
    if (!at.global.empty()) {
        std::ifstream in(at.global, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        const fs::path shapes = at.rows / "DirectOutputShapes.xml", copy = work / "directoutput";
        fs::create_directories(copy / locate::kDir, e);
        std::ofstream o(copy / locate::kDir / "GlobalConfig_B2SServer.xml", std::ios::binary);
        o << locate::global_for(text.str(), at.global.parent_path().string(),
                                fs::is_regular_file(shapes, e) ? shapes.string() : "");
        if (in && o.flush()) base = copy;
        else say("cannot hand libdof " + at.global.string() + ": it finds the boards itself");
    }
    cfg->SetBasePath((base.string() + "/").c_str());

    arm(kStartS);
    auto *dof = new DOF::DOF();                 // this already looks for Pinscapes over HID
    if (g_all_off) {
        dof->Init("", "");                      // no table, so no row can switch anything on
        std::this_thread::sleep_for(150ms);
        arm(kFinishS);
        dof->Finish();
        say("all outputs switched off");
        return DONE;
    }

    fs::path table_file;
    if (const int no = find_rows(dof, at, work, rom, table_file)) return no;
    dof->Init(table_file.string().c_str(), rom.c_str());
    arm(0);
    if (!g_pico_found && std::any_of(at.numbers.begin(), at.numbers.end(),
                                     [](int n) { return n >= 120 && n <= 135; }))
#ifdef _WIN32
        say("there are Pinscape Pico files, and no Pico answers (plugged in? held by another program?)");
#else
        say("there are Pinscape Pico files, and no Pico answers (plugged in? may this user open its hidraw device?)");
#endif
    if (g_unread_once)
        say("libdof could not read " + std::to_string(g_unread_once) + " settings in the DOF files and skips them,"
            " as it does under VPinball (saves/dof/DirectOutput.log names them, if logging is on)");

#ifdef _WIN32
    SetConsoleCtrlHandler([](DWORD) -> BOOL { g_quit = 1; return TRUE; }, TRUE);
#else
    sa.sa_handler = [](int) { g_quit = 1; };
    sa.sa_flags = 0;
    for (int s : { SIGTERM, SIGINT, SIGHUP }) sigaction(s, &sa, nullptr);
#endif

    wait_for_boards();
    Sender send(dof, g_table);
    int code = DONE;
    if (test.empty()) {
#ifdef _WIN32
        code = serve(send, out);
#else
        code = serve(send, out, parent);
#endif
    } else {
        const int ev = test_event;
        say(rom + ": $" + g_table.dof(ev) + ", bumpers " + g_bumpers);
        send.event(ev, true);
        std::this_thread::sleep_for(500ms);     // long enough to see a flipper held
        send.release();
        for (int i = 0; i < 30 && !g_quit; i++) std::this_thread::sleep_for(100ms);
    }

    arm(kFinishS);
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
