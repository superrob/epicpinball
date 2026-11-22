// SPDX-License-Identifier: GPL-3.0-or-later
// locate.h - whose DOF files: VPinball's if there is an install, else the game's.
//
// VPX hands libdof its preference folder, or the DOFPath of its [Plugin.DOF]
// (newer builds), and libdof reads directoutputconfig/ in that. The game's own
// ini files, in directoutputconfig/ of the save folder, take the place of
// VPX's for this game only: libdof looks beside the "table file" first.
#pragma once
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace locate {

namespace fs = std::filesystem;

inline constexpr const char *kDir = "directoutputconfig";

inline std::string lower(std::string s)
{
    for (auto &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

inline std::string trim(const std::string &s)
{
    auto a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// The numbers of a folder's directoutputconfigN.ini, named as libdof accepts
inline std::vector<int> ini_numbers(const fs::path &dir)
{
    std::vector<int> found;
    std::error_code e;
    const std::string head = kDir;
    for (fs::directory_iterator it(dir, e), end; !e && it != end; it.increment(e)) {
        const std::string f = lower(it->path().filename().string());
        if (f.size() < head.size() + 4 || !f.starts_with(head) || !f.ends_with(".ini")) continue;
        const std::string n = f.substr(head.size(), f.size() - head.size() - 4);
        if (n.size() > 4 || n.find_first_not_of("0123456789") != std::string::npos) continue;
        std::error_code kind;
        if (it->is_regular_file(kind)) found.push_back(n.empty() ? 1 : std::stoi(n));
    }
    return found;
}

// DOFPath in [Plugin.DOF] of VPinballX.ini, "" if none
inline std::string dof_path(const fs::path &ini)
{
    std::ifstream f(ini);
    std::string line, section;
    while (std::getline(f, line)) {
        if (line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3);
        line = trim(line);
        if (line.empty() || line[0] == ';') continue;
        if (line.front() == '[') {
            section = lower(line);
            continue;
        }
        const auto eq = line.find('=');
        if (eq != std::string::npos && section == "[plugin.dof]" && lower(trim(line.substr(0, eq))) == "dofpath")
            return trim(line.substr(eq + 1));
    }
    return "";
}

// VPX keeps a preference folder for each major.minor. The newest one set up, or "".
inline fs::path vpx_preferences()
{
    fs::path data;
    const char *xdg = std::getenv("XDG_DATA_HOME"), *home = std::getenv("HOME");
    if (xdg && *xdg == '/') data = xdg;
    else if (home && *home) data = fs::path(home) / ".local/share";
    else return {};
    fs::path best;
    int major = -1, minor = -1;
    std::error_code e;
    for (fs::directory_iterator it(data / "VPinballX", e), end; !e && it != end; it.increment(e)) {
        int a, b, used = 0;
        const std::string name = it->path().filename().string();
        if (std::sscanf(name.c_str(), "%d.%d%n", &a, &b, &used) != 2 || used != (int)name.size()) continue;
        std::error_code kind;
        if (!fs::is_regular_file(it->path() / "VPinballX.ini", kind)) continue;
        if (a > major || (a == major && b > minor)) {
            best = it->path();
            major = a;
            minor = b;
        }
    }
    return best;
}

struct Files {
    fs::path base;                  // for libdof: directoutputconfig/ is in it
    fs::path rows;                  // where the table rows are looked for
    fs::path table;                 // the "table file", when the game's own rows win; or empty
    std::vector<int> numbers;       // of the ini files there
    std::vector<std::string> said;  // how it came to that
};

inline Files find(const fs::path &save, const std::string &rom)
{
    Files r;
    r.base = save;
    const fs::path vpx = vpx_preferences(), own = save / kDir;
    if (!vpx.empty()) {
        fs::path base = vpx;
        const std::string set = dof_path(vpx / "VPinballX.ini");
        if (!set.empty() && fs::path(set).is_absolute()) base = set;
        else if (!set.empty()) r.said.push_back("ignoring VPinball's DOFPath, not an absolute path: " + set);
        std::error_code e;
        if (fs::is_directory(base / kDir, e)) r.base = base;
        else r.said.push_back("VPinball has no " + (base / kDir).string() + ", so the game's own folder is used");
    }
    r.rows = r.base / kDir;
    r.said.push_back("DOF files: " + r.rows.string() + (r.base == save ? "" : " (VPinball's)"));
    r.numbers = ini_numbers(r.rows);
    if (r.base != save) {
        auto mine = ini_numbers(own);
        if (!mine.empty()) {
            r.rows = own;
            r.numbers = std::move(mine);
            r.table = own / rom;
            r.said.push_back("this game's own rows, instead of VPinball's: " + own.string());
        }
    }
    return r;
}

} // namespace locate
