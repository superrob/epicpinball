// SPDX-License-Identifier: GPL-3.0-or-later
// epicpinball-dmd - puts the game's 128x32 DMD picture on a real DMD
// through libdmdutil: a ZeDMD over USB or WiFi, a Pixelcade, a PinDMD3.
//
//   epicpinball-dmd --table ep01 [--ini FILE]
//
// The game (src/dmd_pipe.c) starts it with one end of a SOCK_SEQPACKET
// socket pair as stdin, and sends a frame per message: 128*32 RGB24 bytes,
// only when the picture changed. An empty message, or the socket closing,
// ends it: the display is blanked first. It answers nothing; what it has to
// say goes to stderr, which the game passes on.
//
// On Windows stdin is an anonymous pipe instead (no packet socket pair there
// between processes, and the socket is only that: no network): the frames
// come back to back, each whole, and are read by their size. End of file
// ends it, as the socket closing does.
//
// --ini names a DMDUtil.ini-style file (libdmdutil's own keys: [ZeDMD]
// Device, WiFiAddr, Brightness ...); without one libdmdutil looks for a
// ZeDMD on every serial port and on the network, and for the others.
// EPICPINBALL_DMD_DUMP=DIR also writes what libdmdutil was given there, as
// its own RGB888 dump (for testing without a display).
#include <DMDUtil/Config.h>
#include <DMDUtil/DMD.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

constexpr int kW = 128, kH = 32, kFrame = kW * kH * 3;

void say(const char *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    std::fputs("[dmd] ", stderr);
    std::vfprintf(stderr, fmt, a);
    std::fputc('\n', stderr);
    va_end(a);
}

void DMDUTILCALLBACK dmd_log(DMDUtil_LogLevel level, const char *format, va_list args)
{
    char line[512];
    std::vsnprintf(line, sizeof line, format, args);
    size_t n = std::strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
    std::fprintf(stderr, "[dmd] libdmdutil: %s\n", line);
}

}  // namespace

int main(int argc, char **argv)
{
    std::string table = "epicpinball", ini;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--table") && i + 1 < argc) table = std::string("epicpinball_") + argv[++i];
        else if (!std::strcmp(argv[i], "--ini") && i + 1 < argc) ini = argv[++i];
        else { say("usage: epicpinball-dmd --table ep01 [--ini FILE]"); return 2; }
    }

    DMDUtil::Config *cfg = DMDUtil::Config::GetInstance();
    cfg->SetLogCallback(dmd_log);
    cfg->SetLogLevel(DMDUtil_LogLevel_INFO);
    struct stat st;
    if (!ini.empty()) {
        if (stat(ini.c_str(), &st) == 0) {
            cfg->parseConfigFile(ini.c_str());
            say("settings from %s", ini.c_str());
        } else say("no %s: looking for displays everywhere", ini.c_str());
    }
    const char *dump = std::getenv("EPICPINBALL_DMD_DUMP");
    if (dump && *dump) cfg->SetDumpPath(dump);

    auto *dmd = new DMDUtil::DMD();
    dmd->SetRomName(table.c_str());
    if (dump && *dump) dmd->DumpDMDRgb888();
    dmd->FindDisplays();

    // Report what was found, without holding frames up: they queue meanwhile
    std::thread([dmd] {
        auto t0 = std::chrono::steady_clock::now();
        while (DMDUtil::DMD::IsFinding() && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(20))
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        say(dmd->HasDisplay() ? "a display is up" : "no display found (a ZeDMD plugged in? may this user open its serial port?)");
    }).detach();

    static uint8_t frame[kFrame];
    long frames = 0;
#ifdef _WIN32
    const HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    for (;;) {
        DWORD got = 0, n = 0;
        while (got < (DWORD)kFrame && ReadFile(in, frame + got, kFrame - got, &n, nullptr) && n) got += n;
        if (got < (DWORD)kFrame) break;          // the end, or the game gone (half a frame: dropped)
        dmd->UpdateRGB24Data(frame, kW, kH);
        frames++;
    }
#else
    for (;;) {
        ssize_t n = recv(STDIN_FILENO, frame, sizeof frame, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;                       // the end, or the game gone
        if (n != kFrame) {
            if (n == 1) break;                   // asked to finish
            say("a message of %zd bytes, not a frame: ignored", n);
            continue;
        }
        dmd->UpdateRGB24Data(frame, kW, kH);
        frames++;
    }
#endif
    std::memset(frame, 0, sizeof frame);         // dark, as a table ends
    dmd->UpdateRGB24Data(frame, kW, kH);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    say("%ld frames", frames);
    delete dmd;
    return 0;
}
