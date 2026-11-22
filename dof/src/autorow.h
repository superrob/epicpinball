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

// Device names as the DOF Config Tool offers them (Pinscape build guide,
// appendix 5), in lower case. Anything not here is 0: the "ZB Launch Ball"
// output above all, which turns a Pinscape plunger into a launch button.
inline std::map<std::string, Rule> rules(const std::vector<std::string> &names)
{
    auto has = [&](const std::string &n) {
        for (auto &x : names)
            if (lower(x) == n) return true;
        return false;
    };
    std::map<std::string, Rule> r = {
        { "flipper left", { e(0), false, { 0 } } },
        { "flipper right", { e(1), false, { 1 } } },
        { "slingshot left", { e(2), false, { 2 } } },
        { "slingshot right", { e(3), false, { 3 } } },
        { "knocker", { e(12), false, { 12 } } },
        { "beacon", { e(12) + " 2000", false, { 12 } } },
        { "shaker", { e(10) + " 300 I32/" + e(11) + " 600 I32", false, { 10, 11 } } },
        { "start button", { "ON", false, {} } },
        { "exit", { "ON", false, {} } },
        { "coin", { "ON", false, {} } },
        { "rgb flippers", { "ON White", true, {} } },
    };
    // A bumper's kick goes to the back row; the middle row, or an 8-device
    // setup, only stands in where the back row has no such device.
    static const char *const side[] = { "left", "center", "right" };
    for (int i = 0; i < 3; i++) {
        for (std::string n : { std::string("10 bumper back "), std::string("10 bumper middle "), std::string("8 bumper ") }) {
            n += side[i];
            if (has(n)) {
                r[n] = { e(4 + i), false, { 4 + i } };
                break;
            }
        }
    }
    return r;
}

} // namespace detail

inline Row build(const std::vector<std::string> &names)
{
    Row row;
    const auto rules = detail::rules(names);
    const size_t n = names.size();
    for (size_t i = 0; i < n; i++) {
        const auto it = rules.find(lower(names[i]));
        if (it == rules.end()) {
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
           "[Colors DOF]\nWhite=#FFFFFFFF\n\n[Config DOF]\n" + line(rom, row) + "\n";
}

} // namespace autorow
