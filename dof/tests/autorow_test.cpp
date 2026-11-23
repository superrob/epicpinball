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

std::string row(const Names &n, const std::string &bumpers = "LCR") { return autorow::line("t", autorow::build(n, bumpers)); }

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
    assert(row(n) == "t,E113/E117,E102,E101,ON White,E107,0,E105,E103,ON,0,E115 White Blink 200/E106 Yellow 80/E113 White 450 3/E116 Lime 450 3/E117 Blue 450 3/E110 Red 300/E112 Red 600,E106,E104,ON,0,0,0,0,0,E113 2000,ON,"
                     "E111 300 I32/E112 600 I32");
    const auto r = autorow::build(n);
    assert(r.outputs == 19 && r.width == 29 && r.said.empty());
    assert(autorow::build({ "Knocker", "RGB Flippers" }).width == 4);
    for (int e : { kFlipperLeft, kFlipperRight, kSlingLeft, kSlingRight, kBumper1, kBumper2, kBumper3, kNudge, kTilt,
                   kKnocker, kGameOver, kMultiball, kGameStart, kDrain })
        assert(r.fires[e]);                                            // the shaker takes nudge and tilt
    for (int e : { kLaunch, kBallRelease, kBallReady, kBumper4 })
        assert(!r.fires[e]);                                           // no device here, or never in a made row

    // outputs per cell, as libdof counts them: a colour takes three
    int outputs = 0;
    for (auto &c : r.cells) outputs += c.find("White") != std::string::npos ? 3 : 1;     // every colour cell has White
    assert(outputs == (int)n.size() - 3);                              // the last three are zeros, cut
    assert(autorow::build({ "Knocker", "Gear", "Fan" }).cells.size() == 1);       // zeros at the end are cut
}

void bumpers()
{
    // each bumper to its side's device: the back row first, then the middle
    // row, then an 8-device setup; the bottom right one also kicks on launch
    const Names ten = { "10 Bumper Back Left", "10 Bumper Back Center", "10 Bumper Back Right",
                        "10 Bumper Middle Left", "10 Bumper Middle Center", "10 Bumper Middle Right" };
    assert(row(ten, "LCR") == "t,E105,E106,E107,0,0,E108");
    assert(row(ten, "LLCCRR") == "t,E105/E106,E107/E118,E119/E120,0,0,E108");          // table 5
    assert(row(ten, "LR") == "t,E105,0,E106,0,0,E108");                                   // tables 1, 10, 12, 13
    assert(row(ten, "C") == "t,0,E105,0,0,0,E108");                                       // table 4
    assert(row(ten, "LLLLLL") == "t,E105/E106/E107/E118/E119/E120,0,0,0,0,E108");
    assert(row(ten, "LCRLCRLCR") == "t,E105/E118,E106/E119,E107/E120,0,0,E108");         // six at most
    assert(row({ "10 Bumper Middle Left", "10 Bumper Middle Center", "10 Bumper Middle Right" }) == "t,E105,E106,E108/E107");
    assert(row({ "10 Bumper Middle Left", "10 Bumper Back Left" }) == "t,0,E105");
    const Names eight = { "8 Bumper Left", "8 Bumper Center", "8 Bumper Right", "8 Bumper Back" };
    assert(row(eight, "LCR") == "t,E105,E106,E108/E107");
    assert(row(eight, "LLCCRR") == "t,E105/E106,E107/E118,E108/E119/E120");
    assert(row(eight, "LR") == "t,E105,0,E108/E106");
    assert(row({ "8 Bumper Back", "10 Bumper Middle Right", "8 Bumper Right" }) == "t,0,E108/E107,E108");
    assert(row({ "Slingshot Right", "slingshot right", "FLIPPER LEFT" }) == "t,E104,E104,E101");    // every one, any case
    assert(row({ "Launch Button", "Knocker" }) == "t,E114,E113/E117");
    for (int e : { kBumper1, kBumper2, kBumper3, kBumper4, kBumper5, kBumper6 })
        assert(autorow::build(ten, "LLCCRR").fires[e]);
}

void lights()
{
    assert(row({ "RGB Flippers" }) == "t,ON White");                   // its blanks cut from the line's end
    assert(row({ "RGB Flippers", "" }) == "t,ON White");
    assert(row({ "Exit", "RGB Flippers", "", "", "Coin" }) == "t,ON,ON White,ON");
    auto r = autorow::build({ "RGB Flippers", "Knocker", "", "Coin" });  // no room for three: off, and said
    assert(autorow::line("t", r) == "t,0,E113/E117,0,ON" && r.said.size() == 1);
    assert(row({ "RGB Undercab Complex MX", "", "", "Knocker" }) == "t,0,0,0,E113/E117");     // unknown RGB: a zero each
    assert(row({ "Shaker", "Gear", "Fan", "Strobe", "Bell" }) == "t,E111 300 I32/E112 600 I32");
}

void flashers()
{
    // five spots across the back; a bumper flashes its side's, drain the centre
    const Names five = { "5 Flasher Outside Left", "", "", "5 Flasher Left", "", "", "5 Flasher Center", "", "",
                         "5 Flasher Right", "", "", "5 Flasher Outside Right", "", "" };
    const auto r = autorow::build(five, "LLCCRR");                     // table 5
    assert(r.cells.size() == 5 && r.outputs == 15 && r.said.empty());
    const std::string all = "E115 White Blink 200/", end = "E113 White 450 3/E116 Lime 450 3/E117 Blue 450 3/";
    assert(r.cells[0] == all + "E105 Yellow 80/E106 Yellow 80/" + end + "E112 Red 600");
    assert(r.cells[1] == r.cells[0]);
    assert(r.cells[2] == all + "E107 Yellow 80/E118 Yellow 80/" + end + "E110 Red 300/E112 Red 600");
    assert(r.cells[3] == all + "E119 Yellow 80/E120 Yellow 80/" + end + "E112 Red 600");
    assert(autorow::build(five, "C").cells[0] == all + end + "E112 Red 600");    // table 4: the outer spots, no bumper

    const Names three = { "3 Flasher Left", "", "", "3 Flasher Center", "", "", "3 Flasher Right", "", "" };
    const auto t = autorow::build(three, "LR");                        // tables 1, 10, 12, 13
    assert(t.cells[0] == all + "E105 Yellow 80/" + end + "E112 Red 600");
    assert(t.cells[1] == all + end + "E110 Red 300/E112 Red 600");
    assert(t.cells[2] == all + "E106 Yellow 80/" + end + "E112 Red 600");

    // a strip: the same, as areas of it
    const auto m = autorow::build({ "Back MX", "", "", "Left MX", "", "" }, "LCR");
    assert(m.cells.size() == 1 && m.outputs == 3);                     // its own three; Left MX is not a flasher
    assert(m.cells[0] == "E115 White Blink 200 AL0 AT0 AW100 AH100/E105 Yellow 80 AL0 AT0 AW40 AH100/"
                         "E106 Yellow 80 AL40 AT0 AW20 AH100/E107 Yellow 80 AL60 AT0 AW40 AH100/"
                         "E113 White 450 3 AL0 AT0 AW100 AH100/E116 Lime 450 3 AL0 AT0 AW100 AH100/"
                         "E117 Blue 450 3 AL0 AT0 AW100 AH100/E110 Red 300 AL40 AT0 AW20 AH100/E112 Red 600 AL0 AT0 AW100 AH100");

    // a name it does not know is said, and left off; no flasher has a colour it does not define
    const auto u = autorow::build({ "5 Flashers Center", "", "", "Flasher", "Knocker" });
    assert(u.said.size() == 2 && autorow::line("t", u) == "t,0,0,0,0,E113/E117");
    const std::string f = autorow::file("x", r, "y");
    for (const char *c : { "White=", "Yellow=", "Red=", "Lime=", "Blue=" }) assert(f.find(c) != std::string::npos);
    assert(f.find('@') == std::string::npos && f.find("SHP") == std::string::npos);
}

void never()
{
    // the ZB launch output turns a Pinscape plunger into a launch button
    for (auto n : { "ZB Launch Ball", "zb launch ball", "Authentic Launch Ball", "Extra Ball",
                    "Custom Output 1", "Hellball Motor", "Left MX", "Strobe", "", "Rom" })
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
    flashers();
    never();
    file();
    std::puts("autorow_test: ok");
}
