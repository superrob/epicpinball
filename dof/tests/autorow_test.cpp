// SPDX-License-Identifier: GPL-3.0-or-later
// autorow_test - rows made from a DOF file's header (src/autorow.h). No libdof.
// The headers here are made up: no one's port list is in this repository.
#undef NDEBUG
#include "../src/autorow.h"
#include <cassert>
#include <cstdio>
#include <sstream>

namespace {

using Names = std::vector<std::string>;

Names parse(const std::string &text)
{
    std::istringstream in(text);
    auto h = autorow::header(in);
    assert(h);
    return *h;
}

bool no_header(const std::string &text)
{
    std::istringstream in(text);
    return !autorow::header(in);
}

std::string row(const Names &n) { return autorow::line("t", autorow::build(n)); }

void headers()
{
    const std::string head = "[Colors DOF]\r\nWhite=#FFFFFFFF\r\n\r\n[Config DOF]\r\n";
    assert(parse(head + "# \"Rom\",\"Flipper Left\",\"A, B\",\"\",\"Say \"\"hi\"\"\"\r\nafm,E101\r\n")
           == (Names{ "Flipper Left", "A, B", "", "Say \"hi\"" }));
    assert(parse("\xEF\xBB\xBF[Config DOF]\n\n#\"rom\",Knocker , \"\"\n") == (Names{ "Knocker", "" }));
    assert(parse("[Config DOF]\n#  \"Rom\",\"Beacon\"") == (Names{ "Beacon" }));
    assert(no_header("[Config DOF]\nafm,E101\n"));                     // a row, not a header
    assert(no_header("[Config DOF]\n# just a remark\n"));
    assert(no_header("[Config DOF]\n# \"Rom\"\n"));                    // no device at all
    assert(no_header("[Colors DOF]\n# \"Rom\",\"Knocker\"\n"));         // another section
    assert(no_header("[config dof]\n# \"Rom\",\"Knocker\"\n"));         // libdof wants it so spelled
    assert(no_header(""));
}

void cabinet()
{
    // a made-up cabinet of 29 outputs, RGB devices among the rest
    const Names n = { "Knocker", "Flipper Right", "Flipper Left", "RGB Flippers", "", "",
                      "10 Bumper Back Right", "10 Bumper Middle Center", "10 Bumper Back Left",
                      "Slingshot Left", "Coin", "ZB Launch Ball", "5 Flasher Center", "", "",
                      "10 Bumper Back Center", "Slingshot Right", "Exit", "Gear", "Custom RGB 1", "", "",
                      "10 Bumper Middle Left", "Beacon", "Start Button", "Shaker", "Extra Ball", "Strobe", "Fan" };
    assert(row(n) == "t,E113,E102,E101,ON White,E107,0,E105,E103,ON,0,0,0,0,E106,E104,ON,0,0,0,0,0,E113 2000,ON,"
                     "E111 300 I32/E112 600 I32");
    const auto r = autorow::build(n);
    assert(r.outputs == 16 && r.width == 29 && r.said.empty());
    assert(autorow::build({ "Knocker", "RGB Flippers" }).width == 4);
    for (int e : { 0, 1, 2, 3, 4, 5, 6, 10, 11, 12 }) assert(r.fires[e]);   // the shaker takes nudge and tilt
    for (int e : { 7, 8, 9 }) assert(!r.fires[e]);                    // launch, release, drain: no device

    // outputs per cell, as libdof counts them: a colour takes three
    int outputs = 0;
    for (auto &c : r.cells) outputs += c.find("White") != std::string::npos ? 3 : 1;
    assert(outputs == (int)n.size() - 3);                              // the last three are zeros, cut
    assert(autorow::build({ "Knocker", "Gear", "Fan" }).cells.size() == 1);       // zeros at the end are cut
}

void bumpers()
{
    // the back row first, then the middle row, then an 8-device setup
    assert(row({ "10 Bumper Middle Left", "10 Bumper Middle Center", "10 Bumper Middle Right" }) == "t,E105,E106,E107");
    assert(row({ "10 Bumper Middle Left", "10 Bumper Back Left" }) == "t,0,E105");
    assert(row({ "8 Bumper Left", "8 Bumper Center", "8 Bumper Right", "8 Bumper Back" }) == "t,E105,E106,E107");
    assert(row({ "8 Bumper Back", "10 Bumper Middle Right", "8 Bumper Right" }) == "t,0,E107");
    assert(row({ "Slingshot Right", "slingshot right", "FLIPPER LEFT" }) == "t,E104,E104,E101");    // every one, any case
}

void lights()
{
    assert(row({ "RGB Flippers" }) == "t,ON White");                   // its blanks cut from the line's end
    assert(row({ "RGB Flippers", "" }) == "t,ON White");
    assert(row({ "Exit", "RGB Flippers", "", "", "Coin" }) == "t,ON,ON White,ON");
    auto r = autorow::build({ "RGB Flippers", "Knocker", "", "Coin" });  // no room for three: off, and said
    assert(autorow::line("t", r) == "t,0,E113,0,ON" && r.said.size() == 1);
    assert(row({ "RGB Undercab Complex MX", "", "", "Knocker" }) == "t,0,0,0,E113");     // unknown RGB: a zero each
    assert(row({ "Shaker", "Gear", "Fan", "Strobe", "Bell" }) == "t,E111 300 I32/E112 600 I32");
}

void never()
{
    // the ZB launch output turns a Pinscape plunger into a launch button
    for (auto n : { "ZB Launch Ball", "zb launch ball", "Launch Button", "Authentic Launch Ball", "Extra Ball",
                    "Custom Output 1", "Hellball Motor", "5 Flasher Center", "Back MX", "", "Rom" })
        assert(autorow::build({ n }).empty());
    assert(autorow::build({}).empty());
}

void file()
{
    const auto r = autorow::build({ "Flipper Left", "RGB Flippers", "", "" });
    const std::string f = autorow::file("epicpinball_ep07", r, "x.ini");
    std::istringstream in(f);
    assert(!autorow::header(in));                                      // the made file has none: never made again from itself
    assert(f.find("[Colors DOF]\nWhite=#FFFFFFFF\n") != std::string::npos);
    assert(f.find("\nepicpinball_ep07,E101,ON White\n") != std::string::npos);
    assert(f.find('@') == std::string::npos);
}

} // namespace

int main()
{
    headers();
    cabinet();
    bumpers();
    lights();
    never();
    file();
    std::puts("autorow_test: ok");
}
