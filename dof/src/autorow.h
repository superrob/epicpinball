// SPDX-License-Identifier: GPL-3.0-or-later
// autorow.h - a row for Epic Pinball, made from a DOF file's own header.
//
// The DOF Config Tool writes, right after [Config DOF], which device is on
// each output:
//
//   # "Rom","Flipper Left","Flipper Right", ... ,"RGB Flippers","","", ...
//
// The n-th name is output n, and an RGB device takes three outputs: its name
// and two blanks. So when nobody has written an epicpinball row, one can be
// made: each device that has an event of the game's gets it, everything else
// is 0. No libdof in here.
#pragma once
#include "events.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace autorow {

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

// "a","b,c","" as the Config Tool quotes it; "" inside quotes is a quote
inline std::vector<std::string> fields(const std::string &line)
{
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false, was_quoted = false;
    for (size_t i = 0; i < line.size(); i++) {
        const char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') cur += '"', i++;
            else if (c == '"') quoted = false;
            else cur += c;
        } else if (c == '"') {
            quoted = was_quoted = true;
        } else if (c == ',') {
            out.push_back(was_quoted ? cur : trim(cur));
            cur.clear();
            was_quoted = false;
        } else {
            cur += c;
        }
    }
    out.push_back(was_quoted ? cur : trim(cur));
    return out;
}

// The names after "Rom" in the header of a DOF ini file's text: output 1 first.
// Nothing if the line after [Config DOF] is not such a header.
inline std::optional<std::vector<std::string>> header(std::istream &in)
{
    std::string line;
    bool section = false, first = true;
    while (std::getline(in, line)) {
        if (first && line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3);
        first = false;
        line = trim(line);
        if (!section) {
            section = line == "[Config DOF]";          // as libdof spells it
            continue;
        }
        if (line.empty()) continue;
        if (line[0] != '#') return std::nullopt;
        auto f = fields(trim(line.substr(1)));
        if (f.size() < 2 || lower(trim(f[0])) != "rom") return std::nullopt;
        f.erase(f.begin());
        for (auto &n : f) n = trim(n);
        return f;
    }
    return std::nullopt;
}

inline std::optional<std::vector<std::string>> header_of(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    return header(f);
}

struct Row {
    std::vector<std::string> cells;   // after the table's name
    int outputs = 0;                  // that got something
    int width = 0;                    // outputs the header spans
    bool fires[kEvents] = {};         // which events some output answers
    std::vector<std::string> said;    // what was left out, and why
    bool empty() const { return outputs == 0; }
};

namespace detail {

inline std::string e(int ev) { return "E" + std::to_string(kFirstE + ev); }

struct Rule {
    std::string cell;
    bool rgb;
    std::vector<int> events;
};

// The cell for several events on one device
inline std::string cell_of(const std::vector<int> &events)
{
    std::string c;
    for (int ev : events) c += (c.empty() ? "" : "/") + e(ev);
    return c;
}

// ---- flashers -------------------------------------------------------------
//
// Five spots across the back, left to right: a row of five RGB flashers, three
// (the outer two spots each share one), or an LED strip's five areas, as the
// Config Tool's own rows place them. Each gets, bottom layer first:
//   multiball   white, blinking while it lasts      every spot
//   bumper      yellow, 80 ms                      the spots on its side
//   extra ball  white, three flashes               every spot
//   game start  green, three flashes               every spot
//   game over   blue, three flashes                every spot
//   drain       red, 300 ms                        the centre
//   tilt        red, 600 ms                        every spot
// Measured on libdof 0.5.4: Blink 200 blinks while the level lasts and stops
// with it; 450 3 is three flashes; a later setting draws over an earlier one.
inline constexpr int kSpots = 5;

// The spots a table side's bumpers flash: left two, centre, right two
inline std::vector<int> side_spots(char side)
{
    return side == 'L' ? std::vector<int>{ 0, 1 } : side == 'C' ? std::vector<int>{ 2 } : std::vector<int>{ 3, 4 };
}

// A strip's area for some neighbouring spots, a fifth of its width each
inline std::string area(const std::vector<int> &spots)
{
    const int lo = *std::min_element(spots.begin(), spots.end()), hi = *std::max_element(spots.begin(), spots.end());
    return " AL" + std::to_string(lo * 20) + " AT0 AW" + std::to_string((hi - lo + 1) * 20) + " AH100";
}

inline void flashers(std::map<std::string, Rule> &r, const std::vector<std::string> &names, const std::string &bumpers)
{
    struct Device { const char *name; std::vector<int> spots; bool strip; };
    static const std::vector<Device> devices = {
        { "5 flasher outside left", { 0 }, false }, { "5 flasher left", { 1 }, false },
        { "5 flasher center", { 2 }, false }, { "5 flasher right", { 3 }, false },
        { "5 flasher outside right", { 4 }, false },
        { "3 flasher left", { 0, 1 }, false }, { "3 flasher center", { 2 }, false },
        { "3 flasher right", { 3, 4 }, false },
        { "back mx", { 0, 1, 2, 3, 4 }, true },
    };
    static const std::vector<int> all = { 0, 1, 2, 3, 4 };
    for (const Device &d : devices) {
        bool here = false;
        for (auto &x : names) here |= lower(x) == d.name;
        if (!here) continue;
        auto overlap = [&](const std::vector<int> &s) {
            std::vector<int> o;
            for (int x : s)
                if (std::find(d.spots.begin(), d.spots.end(), x) != d.spots.end()) o.push_back(x);
            return o;
        };
        std::vector<std::string> settings;
        std::vector<int> events;
        auto add = [&](int ev, const std::string &how, const std::vector<int> &spots) {
            const auto o = overlap(spots);
            if (o.empty()) return;
            settings.push_back(e(ev) + " " + how + (d.strip ? area(o) : ""));
            events.push_back(ev);
        };
        add(kMultiball, "White Blink 200", all);
        for (size_t i = 0; i < bumpers.size() && i < (size_t)kBumpers; i++)
            add(bumper_event((int)i), "Yellow 80", side_spots(bumpers[i]));
        add(kKnocker, "White 450 3", all);
        add(kGameStart, "Lime 450 3", all);
        add(kGameOver, "Blue 450 3", all);
        add(kDrain, "Red 300", { 2 });
        add(kTilt, "Red 600", all);
        std::string cell;
        for (auto &s : settings) cell += (cell.empty() ? "" : "/") + s;
        r[d.name] = { cell, true, events };
    }
}

// Device names as the DOF Config Tool offers them (Pinscape build guide,
// appendix 5), in lower case. Anything not here is 0: the "ZB Launch Ball"
// output above all, which turns a Pinscape plunger into a launch button.
// `bumpers` is the table's: a side, L C or R, for each bumper from the left.
inline std::map<std::string, Rule> rules(const std::vector<std::string> &names, const std::string &bumpers)
{
    auto has = [&](const std::string &n) {
        for (auto &x : names)
            if (lower(x) == n) return true;
        return false;
    };
    std::map<std::string, Rule> r = {
        { "flipper left", { e(kFlipperLeft), false, { kFlipperLeft } } },
        { "flipper right", { e(kFlipperRight), false, { kFlipperRight } } },
        { "slingshot left", { e(kSlingLeft), false, { kSlingLeft } } },
        { "slingshot right", { e(kSlingRight), false, { kSlingRight } } },
        { "knocker", { e(kKnocker) + "/" + e(kGameOver), false, { kKnocker, kGameOver } } },
        { "beacon", { e(kKnocker) + " 2000", false, { kKnocker } } },
        { "shaker", { e(kNudge) + " 300 I32/" + e(kTilt) + " 600 I32", false, { kNudge, kTilt } } },
        { "launch button", { e(kBallReady), false, { kBallReady } } },
        { "start button", { "ON", false, {} } },
        { "exit", { "ON", false, {} } },
        { "coin", { "ON", false, {} } },
        { "rgb flippers", { "ON White", true, {} } },
    };
    // Each bumper goes to the device for its side: the back row; the middle
    // row, or an 8-device setup, only where the back row has none. Several
    // bumpers on one side share it. The bottom right one also kicks on launch.
    static const char *const side[] = { "left", "center", "right" };
    std::map<std::string, std::vector<int>> on;
    for (int s = 0; s < 3; s++) {
        std::vector<int> mine;
        for (size_t i = 0; i < bumpers.size() && i < (size_t)kBumpers; i++)
            if (bumpers[i] == "LCR"[s]) mine.push_back(bumper_event((int)i));
        if (mine.empty()) continue;
        for (std::string n : { std::string("10 bumper back "), std::string("10 bumper middle "), std::string("8 bumper ") }) {
            n += side[s];
            if (has(n)) {
                on[n] = mine;
                break;
            }
        }
    }
    for (const char *n : { "10 bumper middle right", "8 bumper right" })
        if (has(n)) on[n].insert(on[n].begin(), kLaunch);
    for (auto &[name, events] : on) r[name] = { cell_of(events), false, events };
    flashers(r, names, bumpers);
    return r;
}

} // namespace detail

inline Row build(const std::vector<std::string> &names, const std::string &bumpers = "LCR")
{
    Row row;
    const auto rules = detail::rules(names, bumpers);
    const size_t n = names.size();
    for (size_t i = 0; i < n; i++) {
        const auto it = rules.find(lower(names[i]));
        if (it == rules.end()) {
            if (lower(names[i]).find("flasher") != std::string::npos)
                row.said.push_back("output " + std::to_string(i + 1) + " \"" + names[i]
                                   + "\" looks like a flasher this does not know, so it is left off");
            row.cells.push_back("0");
            continue;
        }
        const detail::Rule &r = it->second;
        if (r.rgb) {
            // its own output and the next two: blanks, or past the end of the line
            const bool room = (i + 1 >= n || names[i + 1].empty()) && (i + 2 >= n || names[i + 2].empty());
            if (!room) {
                row.said.push_back("output " + std::to_string(i + 1) + " \"" + names[i]
                                   + "\" is not followed by two blank names, so it is left off");
                row.cells.push_back("0");
                continue;
            }
            row.cells.push_back(r.cell);
            row.outputs += 3;
            row.width = std::max(row.width, (int)i + 3);
            i += 2;
        } else {
            row.cells.push_back(r.cell);
            row.outputs++;
        }
        for (int ev : r.events) row.fires[ev] = true;
    }
    row.width = std::max(row.width, (int)n);
    while (!row.cells.empty() && row.cells.back() == "0") row.cells.pop_back();
    return row;
}

inline std::string line(const std::string &rom, const Row &row)
{
    std::string s = rom;
    for (auto &c : row.cells) s += "," + c;
    return s;
}

// The whole file libdof reads in place of the one the header came from
inline std::string file(const std::string &rom, const Row &row, const std::string &from)
{
    return "# Written by epicpinball-dof from the header of " + from + ",\n"
           "# because no epicpinball row was found. Made again at every table start.\n"
           "[Colors DOF]\nWhite=#FFFFFFFF\nYellow=#FFFF00FF\nRed=#FF0000FF\nLime=#00FF00FF\nBlue=#0000FFFF\n\n"
           "[Config DOF]\n" + line(rom, row) + "\n";
}

} // namespace autorow
