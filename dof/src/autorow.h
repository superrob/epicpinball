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
#include <cstdio>
#include <cstdlib>
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

// A table's own features, numbered as its events, with the side of the
// cabinet each is on: "hole:LLCRR ramp:LRR kickback:L target:LLL" (--sides)
struct Sides {
    std::string hole, ramp, kickback, target;
    bool empty() const { return hole.empty() && ramp.empty() && kickback.empty() && target.empty(); }
    // a bank of targets resets where most of them are
    char bank() const
    {
        if (target.empty()) return 0;
        char best = target[0];
        for (char c : std::string("LCR"))
            if (std::count(target.begin(), target.end(), c) > std::count(target.begin(), target.end(), best)) best = c;
        return best;
    }
};

inline std::optional<Sides> sides(const std::string &text)
{
    Sides s;
    size_t at = 0;
    while (at < text.size()) {
        const size_t end = std::min(text.find(' ', at), text.size()), colon = text.find(':', at);
        if (colon == std::string::npos || colon > end) return std::nullopt;
        const std::string key = text.substr(at, colon - at), v = text.substr(colon + 1, end - colon - 1);
        std::string *to = key == "hole" ? &s.hole : key == "ramp" ? &s.ramp : key == "kickback" ? &s.kickback
                        : key == "target" ? &s.target : nullptr;
        const size_t most = key == "hole" ? kHoles : key == "ramp" ? kRamps : key == "kickback" ? 1 : kTargets;
        if (!to || !to->empty() || v.empty() || v.size() > most || v.find_first_not_of("LCR") != std::string::npos)
            return std::nullopt;
        *to = v;
        at = end + 1;
    }
    return s;
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

// ---- flashers and the MXs --------------------------------------------------
//
// Five spots across the back: a row of five RGB flashers, three (the outer two
// spots each share one), or the Back MX, as the Config Tool's own rows place
// them. Each gets, bottom layer first, in the table's colours (palette):
//   multiball   glow, blinking while it lasts      every spot
//   bumper      main, 80 ms                        the spots on its side
//   hole kick-out, kickback   feature, 120 ms      the spots on its side
//   ramp made   ramp, 150 ms                       the spots on its side
//   skill shot  ramp, three flashes                every spot
//   award       feature, three flashes             every spot
//   lanes lit   glow, three flashes                every spot
//   jackpot     jackpot, three flashes             every spot
//   super jackpot  jackpot, six flashes            every spot
//   extra ball  glow, three flashes                every spot
//   game start  start, three flashes               every spot
//   game over   over, three flashes                every spot
//   drain       danger, 300 ms                     the centre
//   tilt        danger, 600 ms                     every spot
// Measured on libdof 0.5.4: Blink 200 blinks while the level lasts and stops
// with it; 450 3 is three flashes; a later setting draws over an earlier one.
//
// On the Back MX a spot is a band, a fifth of its width, or, where the DOF
// folder has DirectOutputShapes.xml (libdof draws no shape without it), the
// Config Tool's own flasher circle for it ([Variables DOF] flshemulo to
// flshemuro), and the awards and the extra ball are its animated shapes. A
// hole, the kickback or a ramp leaves the Back MX for a side MX on its side.
inline constexpr int kSpots = 5;

// The spots a table side's bumpers flash: left two, centre, right two
inline std::vector<int> side_spots(char side)
{
    return side == 'L' ? std::vector<int>{ 0, 1 } : side == 'C' ? std::vector<int>{ 2 } : std::vector<int>{ 3, 4 };
}

// A side's bumpers share its spots from the left: one bumper has them all,
// two on two spots one each, more than there are spots share
inline std::vector<int> bumper_spots(const std::string &bumpers, size_t i)
{
    const auto s = side_spots(bumpers[i]);
    size_t k = 0, n = 0;
    for (size_t j = 0; j < bumpers.size() && j < (size_t)kBumpers; j++)
        if (bumpers[j] == bumpers[i]) {
            if (j < i) k++;
            n++;
        }
    const size_t a = k * s.size() / n, b = (k + 1) * s.size() / n;
    return a < b ? std::vector<int>(s.begin() + (long)a, s.begin() + (long)b) : std::vector<int>{ s[a] };
}

// A strip's area for some neighbouring spots, a fifth of its width each
inline std::string area(const std::vector<int> &spots)
{
    const int lo = *std::min_element(spots.begin(), spots.end()), hi = *std::max_element(spots.begin(), spots.end());
    return " AL" + std::to_string(lo * 20) + " AT0 AW" + std::to_string((hi - lo + 1) * 20) + " AH100";
}

// The Config Tool's flasher circle for a spot, or another shape in its place
inline std::string circle(int spot, const std::string &shape = "Circle3")
{
    return " AL" + std::to_string(spot * 20) + " AT0 AW19 AH100 SHP" + shape;
}

inline bool named(const std::vector<std::string> &names, const std::string &n)
{
    for (auto &x : names)
        if (lower(x) == n) return true;
    return false;
}

// Whether a feature on this side of the table shows on a side MX: the left
// one, the right one, or for the centre either
inline bool on_side_mx(const std::vector<std::string> &names, char side)
{
    const bool l = named(names, "left mx"), r = named(names, "right mx");
    return side == 'L' ? l : side == 'R' ? r : l || r;
}

inline void flashers(std::map<std::string, Rule> &r, const std::vector<std::string> &names, const std::string &bumpers,
                     const Sides &sides, bool shapes)
{
    struct Device { const char *name; std::vector<int> spots; bool mx; };
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
        if (!named(names, d.name)) continue;
        const bool round = d.mx && shapes;
        auto overlap = [&](const std::vector<int> &s) {
            std::vector<int> o;
            for (int x : s)
                if (std::find(d.spots.begin(), d.spots.end(), x) != d.spots.end()) o.push_back(x);
            return o;
        };
        std::vector<std::string> settings;
        std::vector<int> events;
        auto put = [&](int ev, const std::string &s) {
            settings.push_back(e(ev) + " " + s);
            if (std::find(events.begin(), events.end(), ev) == events.end()) events.push_back(ev);
        };
        // a flash on some spots; on the Back MX as their band or circles
        auto add = [&](int ev, const std::string &how, const std::vector<int> &spots) {
            const auto o = overlap(spots);
            if (o.empty()) return;
            if (round)
                for (int s : o) put(ev, how + circle(s));
            else
                put(ev, how + (d.mx ? area(o) : ""));
        };
        // an award: flashes, or on the Back MX with shapes an animated shape on every spot
        auto award = [&](int ev, const std::string &how, const std::string &colour_ms, const std::string &shape) {
            if (!round) return add(ev, how, all);
            for (int s : all) put(ev, colour_ms + circle(s, shape));
        };
        // a feature's own flash, unless a side MX shows it in the Back MX's place
        auto feature = [&](int ev, const std::string &how, char side) {
            if (!(d.mx && on_side_mx(names, side))) add(ev, how, side_spots(side));
        };
        add(kMultiball, "EpGlow Blink 200", all);
        for (size_t i = 0; i < bumpers.size() && i < (size_t)kBumpers; i++)
            add(bumper_event((int)i), "EpMain 80", round ? bumper_spots(bumpers, i) : side_spots(bumpers[i]));
        for (size_t i = 0; i < sides.hole.size(); i++) feature(kHole1 + (int)i, "EpFeature 120", sides.hole[i]);
        if (!sides.kickback.empty()) feature(kKickback, "EpFeature 120", sides.kickback[0]);
        for (size_t i = 0; i < sides.ramp.size(); i++) feature(kRamp1 + (int)i, "EpRamp 150", sides.ramp[i]);
        if (!sides.empty()) {        // a table with features of its own: its awards
            award(kSkillShot, "EpRamp 450 3", "EpRamp 600", "CirclePulse");
            award(kAward, "EpFeature 450 3", "EpFeature 1000", "DiamondBoxPulse");
            if (round) put(kLanesLit, "EpGlow 600 AL0 AT0 AW100 AH100 SHPFillLeftRight");
            else add(kLanesLit, "EpGlow 450 3", all);
            award(kJackpot, "EpJackpot 450 3", "EpJackpot 1200", "RoundAndRound");
            award(kSuperJackpot, "EpJackpot 900 6", "EpJackpot 2400", "RoundAndRound");
            if (round)               // and the strobes, strblft and strbrgt
                for (const char *at : { "AL0", "AL91" })
                    put(kSuperJackpot, std::string("EpGlow 900 6 ") + at + " AT0 AW9 AH30 SHPCircle3");
        }
        award(kKnocker, "EpGlow 450 3", "EpGlow 900", "CrossPulse");
        add(kGameStart, "EpStart 450 3", all);
        add(kGameOver, "EpOver 450 3", all);
        add(kDrain, "EpDanger 300", { 2 });
        add(kTilt, "EpDanger 600", all);
        std::string cell;
        for (auto &s : settings) cell += (cell.empty() ? "" : "/") + s;
        r[d.name] = { cell, true, events };
    }
}

// The MXs down the sides of the cabinet, top (the back) to bottom (the
// player), as the Config Tool's rows use them, bottom layer first:
//   ball ready  the Right MX's bottom breathes main while a ball waits
//   slingshot   main, rising up its side
//   hole kick-out, kickback   feature, down its side
//   ramp made   ramp, down its side, the ball coming back off it
// A centre feature runs down both.
inline void side_mx(std::map<std::string, Rule> &r, const std::vector<std::string> &names, const Sides &sides)
{
    for (const char *name : { "left mx", "right mx" }) {
        if (!named(names, name)) continue;
        const char me = name[0] == 'l' ? 'L' : 'R';
        auto mine = [&](char side) { return side == me || side == 'C'; };
        std::vector<std::string> settings;
        std::vector<int> events;
        auto put = [&](int ev, const std::string &s) {
            settings.push_back(e(ev) + " " + s);
            events.push_back(ev);
        };
        if (me == 'R') put(kBallReady, "EpMain AL0 AW100 AT85 AH15 F500 BLINK 2000");
        put(me == 'L' ? kSlingLeft : kSlingRight, "EpMain M50 MAX50 AT0 AH100 ADU AS300");
        for (size_t i = 0; i < sides.hole.size(); i++)
            if (mine(sides.hole[i])) put(kHole1 + (int)i, "EpFeature 75 AT0 AH100 ADD AS300");
        if (!sides.kickback.empty() && mine(sides.kickback[0])) put(kKickback, "EpFeature 75 AT0 AH100 ADD AS300");
        for (size_t i = 0; i < sides.ramp.size(); i++)
            if (mine(sides.ramp[i])) put(kRamp1 + (int)i, "EpRamp 75 AT0 AH100 ADD AS300");
        std::string cell;
        for (auto &s : settings) cell += (cell.empty() ? "" : "/") + s;
        r[name] = { cell, true, events };
    }
}

// Device names as the DOF Config Tool offers them (Pinscape build guide,
// appendix 5), in lower case. Anything not here is 0: the "ZB Launch Ball"
// output above all, which turns a Pinscape plunger into a launch button.
// `bumpers` is the table's: a side, L C or R, for each bumper from the left;
// `sides` its own features'.
inline std::map<std::string, Rule> rules(const std::vector<std::string> &names, const std::string &bumpers,
                                         const Sides &sides = {}, bool shapes = false)
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
        { "knocker", { e(kKnocker), false, { kKnocker } } },                      /* an extra ball only, not game over */
        { "beacon", sides.empty() ? Rule{ e(kKnocker) + " 2000", false, { kKnocker } }
                                  : Rule{ e(kKnocker) + " 2000/" + e(kJackpot) + " 2000/" + e(kSuperJackpot) + " 2000",
                                          false, { kKnocker, kJackpot, kSuperJackpot } } },
        { "shaker", { e(kNudge) + " 300 I32/" + e(kTilt) + " 600 I32", false, { kNudge, kTilt } } },
        { "launch button", { e(kBallReady), false, { kBallReady } } },
        { "start button", { "ON", false, {} } },
        { "exit", { "ON", false, {} } },
        { "coin", { "ON", false, {} } },
        { "rgb flippers", { "ON EpMain", true, {} } },
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
    // A hole's kick-out, the kickback and a bank of drop targets resetting are
    // solenoids too: the middle row on their side, or an 8-device setup's.
    for (int s = 0; s < 3; s++) {
        const char me = "LCR"[s];
        std::vector<int> mine;
        for (size_t i = 0; i < sides.hole.size(); i++)
            if (sides.hole[i] == me) mine.push_back(kHole1 + (int)i);
        if (!sides.kickback.empty() && sides.kickback[0] == me) mine.push_back(kKickback);
        if (sides.bank() == me) mine.push_back(kTargetsDown);
        if (mine.empty()) continue;
        for (std::string n : { std::string("10 bumper middle "), std::string("8 bumper ") }) {
            n += side[s];
            if (has(n)) {
                on[n].insert(on[n].end(), mine.begin(), mine.end());
                break;
            }
        }
    }
    for (const char *n : { "10 bumper middle right", "8 bumper right" })
        if (has(n)) on[n].insert(on[n].begin(), kLaunch);
    for (auto &[name, events] : on) r[name] = { cell_of(events), false, events };
    flashers(r, names, bumpers, sides, shapes);
    side_mx(r, names, sides);
    return r;
}

} // namespace detail

// `shapes`: the DOF folder has DirectOutputShapes.xml, so the Back MX may use them
inline Row build(const std::vector<std::string> &names, const std::string &bumpers = "LCR", const Sides &sides = {},
                 bool shapes = false)
{
    Row row;
    const auto rules = detail::rules(names, bumpers, sides, shapes);
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

// Whether a made row could carry an event on this table: a bumper or feature
// it does not have is no loss, and neither are a single drop target, a
// rollover or a ball held, which made rows leave to the player's own
inline bool row_has(int ev, const std::string &bumpers, const Sides &sides)
{
    for (int b = (int)bumpers.size(); b < kBumpers; b++)
        if (ev == bumper_event(b)) return false;
    if (ev >= kHole1 && ev <= kHole5) return ev - kHole1 < (int)sides.hole.size();
    if (ev >= kRamp1 && ev <= kRamp3) return ev - kRamp1 < (int)sides.ramp.size();
    if (ev == kKickback) return !sides.kickback.empty();
    if (ev == kTargetsDown) return !sides.target.empty();
    if ((ev >= kTarget1 && ev <= kTarget3) || (ev >= kRollover1 && ev <= kRollover6) || ev == kBallHeld) return false;
    if (ev >= kHole1) return !sides.empty();        // ball held, lanes, skill shot, jackpots, awards
    return true;
}

// Event names for a message, a run of numbered ones as one: "hole_1-5"
inline std::string listed(const std::vector<int> &events)
{
    auto base = [](int ev) {
        const std::string n = kEvent[ev];
        const size_t u = n.rfind('_');
        return u != std::string::npos && std::isdigit((unsigned char)n[u + 1]) ? n.substr(0, u + 1) : std::string();
    };
    std::string out;
    for (size_t i = 0; i < events.size(); ) {
        size_t j = i;
        const std::string b = base(events[i]);
        while (!b.empty() && j + 1 < events.size() && base(events[j + 1]) == b && events[j + 1] == events[j] + 1) j++;
        out += (out.empty() ? "" : ", ") + std::string(kEvent[events[i]]);
        if (j > i) out += "-" + std::string(kEvent[events[j]]).substr(b.size());
        i = j + 1;
    }
    return out;
}

inline std::string line(const std::string &rom, const Row &row)
{
    std::string s = rom;
    for (auto &c : row.cells) s += "," + c;
    return s;
}

// ---- colours ---------------------------------------------------------------
//
// A made row names its colours by what they are for, and each table's file
// says what they are there: the table's own, from its playfield art, made
// bright enough for LEDs. Danger stays red on every table: a drain and a tilt
// read as a warning.
enum Role { kGlow, kMain, kFeature, kRamp, kJackpot, kStart, kOver, kDanger, kRoles };
inline constexpr const char *kRoleName[kRoles] = {
    "EpGlow", "EpMain", "EpFeature", "EpRamp", "EpJackpot", "EpStart", "EpOver", "EpDanger",
};
// RRGGBB by table, 1 to 13; 0, any other name: plain colours
inline constexpr unsigned kPalette[14][kRoles] = {
    //  glow      main      feature   ramp      jackpot   start     over      danger
    { 0xFFFFFF, 0xFFFF00, 0xFF8000, 0x00FFFF, 0xFF00FF, 0x00FF00, 0x0000FF, 0xFF0000 },
    { 0xFFD27A, 0xFF9A00, 0xFF4000, 0x2848FF, 0x6A3CFF, 0x2848FF, 0xFF9A00, 0xFF0000 },  // Super Android: amber, the android's blue
    { 0xFFE680, 0xFFC000, 0xFF5A00, 0x1E7BFF, 0x00E040, 0x00E040, 0x1E7BFF, 0xFF0000 },  // Pot of Gold: gold, blue, the clover's green
    { 0xFFE0A0, 0xFFA030, 0xFF6A10, 0xB8C8FF, 0x9A30FF, 0xFFA030, 0x9A30FF, 0xFF0000 },  // Excalibur: gold, bronze, steel, royal purple
    { 0xFFFFFF, 0xFF6A00, 0xFFD000, 0x1048FF, 0xFF0040, 0xFFFFFF, 0x1048FF, 0xFF0000 },  // Crash and Burn: flames, blue, the chequered flag
    { 0xFFF0D0, 0xFFB830, 0xFF1E1E, 0x2040FF, 0x20C040, 0xFFB830, 0x2040FF, 0xFF0000 },  // Magic: gold, the cards' red, the felt's green
    { 0xC0FF80, 0x40E000, 0xFF2000, 0x2060FF, 0xFFC000, 0x40E000, 0xFF6000, 0xFF0000 },  // Jungle Pinball: green, the apples' red
    { 0xA0F0FF, 0x00C8FF, 0xFF8040, 0x0040FF, 0x00FFB0, 0x00C8FF, 0x0040FF, 0xFF0000 },  // Deep Sea: aqua, coral, the deep's blue
    { 0xFFC8A0, 0xFF4A10, 0xFF8A30, 0xFFC060, 0xFF2000, 0xFF8A30, 0xB01800, 0xFF0000 },  // Enigma: the rust of its marble
    { 0xE0E8FF, 0xFF3A20, 0xFFB040, 0x40A0FF, 0xFF4080, 0x40A0FF, 0xFF3A20, 0xFF0000 },  // Cyborgirl: her red, gold, steel blue
    { 0xFFC080, 0xFF6A00, 0x20E020, 0x6020FF, 0xFF1040, 0x20E020, 0xFF6A00, 0xFF0000 },  // Pangaea: orange, the dinosaur's green, purple
    { 0xD8C8FF, 0x7A3CFF, 0xFF1020, 0x2040FF, 0xFF30D0, 0x2040FF, 0x7A3CFF, 0xFF0000 },  // Space Journey: violet, red, blue
    { 0xFFD0F8, 0xFF40E0, 0x80D020, 0x40A0FF, 0x8030FF, 0x80D020, 0x8030FF, 0xFF0000 },  // Toy Factory: pink, green, light blue, purple
    { 0xFFE0A0, 0xFF8000, 0xFFD800, 0x00C000, 0xFF4000, 0x00C000, 0xFF8000, 0xFF0000 },  // African Safari: orange, yellow, green
};

// The table in "epicpinball_ep07", 7; 0 for any other name
inline int table_of(const std::string &rom)
{
    const size_t u = rom.rfind("_ep");
    if (u == std::string::npos || rom.size() != u + 5) return 0;
    const int n = std::atoi(rom.c_str() + u + 3);
    return n >= 1 && n <= 13 && std::isdigit((unsigned char)rom[u + 3]) && std::isdigit((unsigned char)rom[u + 4]) ? n : 0;
}

// The [Colors DOF] section for the table in rom
inline std::string colours(const std::string &rom)
{
    const unsigned *p = kPalette[table_of(rom)];
    std::string s = "[Colors DOF]\n";
    char hex[16];
    for (int i = 0; i < kRoles; i++) {
        std::snprintf(hex, sizeof hex, "=#%06XFF\n", p[i]);
        s += kRoleName[i] + std::string(hex);
    }
    return s;
}

// The whole file libdof reads in place of the one the header came from
inline std::string file(const std::string &rom, const Row &row, const std::string &from)
{
    return "# Written by epicpinball-dof from the header of " + from + ",\n"
           "# because no epicpinball row was found. Made again at every table start.\n"
           + colours(rom) + "\n"
           "[Config DOF]\n" + line(rom, row) + "\n";
}

} // namespace autorow
