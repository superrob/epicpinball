// SPDX-License-Identifier: GPL-3.0-or-later
// autorow_test - rows made from a DOF file's header (src/autorow.h). No libdof.
// The headers here are made up: no one's port list is in this repository.
#undef NDEBUG
#include "../src/autorow.h"
#include <cassert>
#include <cstring>
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
    assert(row(n) == "t,E113,E102,E101,ON EpMain,E107,0,E105,E103,ON,0,E115 EpGlow Blink 200/E106 EpMain 80/E113 EpGlow 450 3/E116 EpStart 450 3/E117 EpOver 450 3/E110 EpDanger 300/E112 EpDanger 600,E106,E104,ON,0,0,0,0,0,E113 2000,ON,"
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
    for (auto &c : r.cells) outputs += c.find("Ep") != std::string::npos ? 3 : 1;     // every colour cell names one
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
    assert(row({ "Launch Button", "Knocker" }) == "t,E114,E113");
    for (int e : { kBumper1, kBumper2, kBumper3, kBumper4, kBumper5, kBumper6 })
        assert(autorow::build(ten, "LLCCRR").fires[e]);
}

void lights()
{
    assert(row({ "RGB Flippers" }) == "t,ON EpMain");                   // its blanks cut from the line's end
    assert(row({ "RGB Flippers", "" }) == "t,ON EpMain");
    assert(row({ "Exit", "RGB Flippers", "", "", "Coin" }) == "t,ON,ON EpMain,ON");
    auto r = autorow::build({ "RGB Flippers", "Knocker", "", "Coin" });  // no room for three: off, and said
    assert(autorow::line("t", r) == "t,0,E113,0,ON" && r.said.size() == 1);
    assert(row({ "RGB Undercab Complex MX", "", "", "Knocker" }) == "t,0,0,0,E113");     // unknown RGB: a zero each
    assert(row({ "Shaker", "Gear", "Fan", "Strobe", "Bell" }) == "t,E111 300 I32/E112 600 I32");
}

void flashers()
{
    // five spots across the back; a bumper flashes its side's, drain the centre
    const Names five = { "5 Flasher Outside Left", "", "", "5 Flasher Left", "", "", "5 Flasher Center", "", "",
                         "5 Flasher Right", "", "", "5 Flasher Outside Right", "", "" };
    const auto r = autorow::build(five, "LLCCRR");                     // table 5
    assert(r.cells.size() == 5 && r.outputs == 15 && r.said.empty());
    const std::string all = "E115 EpGlow Blink 200/", end = "E113 EpGlow 450 3/E116 EpStart 450 3/E117 EpOver 450 3/";
    assert(r.cells[0] == all + "E105 EpMain 80/E106 EpMain 80/" + end + "E112 EpDanger 600");
    assert(r.cells[1] == r.cells[0]);
    assert(r.cells[2] == all + "E107 EpMain 80/E118 EpMain 80/" + end + "E110 EpDanger 300/E112 EpDanger 600");
    assert(r.cells[3] == all + "E119 EpMain 80/E120 EpMain 80/" + end + "E112 EpDanger 600");
    assert(autorow::build(five, "C").cells[0] == all + end + "E112 EpDanger 600");    // table 4: the outer spots, no bumper

    const Names three = { "3 Flasher Left", "", "", "3 Flasher Center", "", "", "3 Flasher Right", "", "" };
    const auto t = autorow::build(three, "LR");                        // tables 1, 10, 12, 13
    assert(t.cells[0] == all + "E105 EpMain 80/" + end + "E112 EpDanger 600");
    assert(t.cells[1] == all + end + "E110 EpDanger 300/E112 EpDanger 600");
    assert(t.cells[2] == all + "E106 EpMain 80/" + end + "E112 EpDanger 600");

    // a strip: the same, as areas of it
    const auto m = autorow::build({ "Back MX", "", "", "Undercab MX", "", "" }, "LCR");
    assert(m.cells.size() == 1 && m.outputs == 3);                     // its own three; the undercab is not a flasher
    assert(m.cells[0] == "E115 EpGlow Blink 200 AL0 AT0 AW100 AH100/E105 EpMain 80 AL0 AT0 AW40 AH100/"
                         "E106 EpMain 80 AL40 AT0 AW20 AH100/E107 EpMain 80 AL60 AT0 AW40 AH100/"
                         "E113 EpGlow 450 3 AL0 AT0 AW100 AH100/E116 EpStart 450 3 AL0 AT0 AW100 AH100/"
                         "E117 EpOver 450 3 AL0 AT0 AW100 AH100/E110 EpDanger 300 AL40 AT0 AW20 AH100/E112 EpDanger 600 AL0 AT0 AW100 AH100");

    // a name it does not know is said, and left off; no flasher has a colour it does not define
    const auto u = autorow::build({ "5 Flashers Center", "", "", "Flasher", "Knocker" });
    assert(u.said.size() == 2 && autorow::line("t", u) == "t,0,0,0,0,E113");
    const std::string f = autorow::file("x", r, "y");
    for (const char *c : { "EpGlow=", "EpMain=", "EpDanger=", "EpStart=", "EpOver=", "EpFeature=", "EpRamp=", "EpJackpot=" })
        assert(f.find(c) != std::string::npos);
    assert(f.find('@') == std::string::npos && f.find("SHP") == std::string::npos);
}

void features()
{
    // table 1: holes, ramps, a kickback and a bank of targets, each with its side
    const auto one = autorow::sides("hole:LLCRR ramp:LRR kickback:L target:LLL");
    assert(one && one->hole == "LLCRR" && one->ramp == "LRR" && one->kickback == "L" && one->bank() == 'L');
    assert(autorow::sides("") && autorow::sides("")->empty());
    for (auto bad : { "hole:LLCRRL", "hole:X", "rubber:L", "hole:L hole:R", "hole:", "hole:L  ramp:R", "hole",
                      "kickback:LR", "ramp:LRRL", "target:LLLL", "hole:l" })
        assert(!autorow::sides(bad));
    assert(autorow::sides("target:LRR")->bank() == 'R' && autorow::sides("target:LCR")->bank() == 'L');

    // their solenoids: the middle row on their side, after the bumpers there and the launch
    const auto made = [&](const Names &n, const std::string &b) { return autorow::line("t", autorow::build(n, b, *one)); };
    const Names ten = { "10 Bumper Back Left", "10 Bumper Back Center", "10 Bumper Back Right",
                        "10 Bumper Middle Left", "10 Bumper Middle Center", "10 Bumper Middle Right" };
    assert(made(ten, "LR") == "t,E105,0,E106,E121/E122/E127/E131,E123,E108/E124/E125");
    const Names eight = { "8 Bumper Left", "8 Bumper Center", "8 Bumper Right", "8 Bumper Back" };
    assert(made(eight, "LR") == "t,E105/E121/E122/E127/E131,E123,E108/E106/E124/E125");
    assert(made({ "10 Bumper Back Left", "10 Bumper Back Right" }, "LR") == "t,E105,E106");     // nowhere for them
    assert(row(ten, "LR") == "t,E105,0,E106,0,0,E108");                                   // no features: as before
    assert(made({ "Beacon" }, "LR") == "t,E113 2000/E143 2000/E144 2000" && row({ "Beacon" }) == "t,E113 2000");

    // flashers: kick-outs and ramps on their side, the awards everywhere
    const Names five = { "5 Flasher Outside Left", "", "", "5 Flasher Left", "", "", "5 Flasher Center", "", "",
                         "5 Flasher Right", "", "", "5 Flasher Outside Right", "", "" };
    const auto r = autorow::build(five, "LR", *one);
    const std::string all = "E115 EpGlow Blink 200/",
                      awards = "E139 EpRamp 450 3/E145 EpFeature 450 3/E138 EpGlow 450 3/E143 EpJackpot 450 3/E144 EpJackpot 900 6/",
                      end = "E113 EpGlow 450 3/E116 EpStart 450 3/E117 EpOver 450 3/";
    assert(r.cells[0] == all + "E105 EpMain 80/E121 EpFeature 120/E122 EpFeature 120/E127 EpFeature 120/E140 EpRamp 150/" + awards + end
                         + "E112 EpDanger 600");
    assert(r.cells[2] == all + "E123 EpFeature 120/" + awards + end + "E110 EpDanger 300/E112 EpDanger 600");
    assert(r.cells[4] == all + "E106 EpMain 80/E124 EpFeature 120/E125 EpFeature 120/E141 EpRamp 150/E142 EpRamp 150/" + awards + end
                         + "E112 EpDanger 600");
    for (int e : { kHole1, kHole5, kKickback, kTargetsDown, kRamp3, kJackpot, kSuperJackpot, kAward, kSkillShot, kLanesLit })
        assert(autorow::build(five, "LR", *one).fires[e] || e == kTargetsDown);
    assert(autorow::build(ten, "LR", *one).fires[kTargetsDown]);

    // the "no device for" line: what this table has and a made row could carry, runs as one
    using autorow::row_has;
    assert(row_has(kHole5, "LR", *one) && !row_has(kHole5, "LR", *autorow::sides("hole:LC")));
    assert(!row_has(kRollover1, "LR", *one) && !row_has(kTarget1, "LR", *one) && !row_has(kBallHeld, "LR", *one));
    assert(row_has(kJackpot, "LR", *one) && !row_has(kJackpot, "LR", {}) && !row_has(kKickback, "LR", {}));
    assert(!row_has(kBumper3, "LR", {}) && row_has(kBallRelease, "LR", {}));
    assert(autorow::listed({ kBallRelease, kNudge, kBallReady }) == "ball_release, nudge, ball_ready");
    assert(autorow::listed({ kBumper1, kBumper2, kBumper3, kBumper4, kHole1, kHole2, kHole3, kHole4, kHole5, kJackpot })
           == "bumper_1-3, bumper_4, hole_1-5, jackpot");
    assert(autorow::listed({ kRamp1, kRamp3 }) == "ramp_1, ramp_3");
}

void mx()
{
    const auto one = *autorow::sides("hole:LLCRR ramp:LRR kickback:L target:LLL");
    const Names mxs = { "Back MX", "", "", "Left MX", "", "", "Right MX", "", "", "RGB Undercab Complex MX", "", "" };

    // the side MXs: ball ready breathing at the bottom of the right one, the
    // slings rising, kick-outs and ramps down their side; centre on both
    const auto r = autorow::build(mxs, "LR", one);
    assert(r.cells.size() == 3 && r.outputs == 9 && r.width == 12 && r.said.empty());
    const std::string sling = " EpMain M50 MAX50 AT0 AH100 ADU AS300", out = " EpFeature 75 AT0 AH100 ADD AS300",
                      ramp = " EpRamp 75 AT0 AH100 ADD AS300";
    assert(r.cells[1] == "E103" + sling + "/E121" + out + "/E122" + out + "/E123" + out + "/E127" + out + "/E140" + ramp);
    assert(r.cells[2] == "E114 EpMain AL0 AW100 AT85 AH15 F500 BLINK 2000/E104" + sling + "/E123" + out + "/E124" + out
                         + "/E125" + out + "/E141" + ramp + "/E142" + ramp);
    for (int e : { kBallReady, kSlingLeft, kSlingRight, kHole3, kKickback, kRamp2 }) assert(r.fires[e]);
    // which leave the Back MX: all of them, so it keeps no hole, kickback or ramp
    for (const char *gone : { "E121", "E123", "E125", "E127", "E140", "E142" })
        assert(r.cells[0].find(gone) == std::string::npos);
    // no table features: slings and ball ready only
    assert(row({ "Left MX", "", "", "Right MX", "", "" }) == "t,E103" + sling + ",E114 EpMain AL0 AW100 AT85 AH15 F500 BLINK 2000/E104" + sling);
    // with only a left MX, the right side's stay on the Back MX, the centre hole goes left
    const auto l = autorow::build({ "Back MX", "", "", "Left MX", "", "" }, "LR", one);
    assert(l.cells[1] == "E103" + sling + "/E121" + out + "/E122" + out + "/E123" + out + "/E127" + out + "/E140" + ramp);
    assert(l.cells[0].find("E121") == std::string::npos && l.cells[0].find("E123") == std::string::npos
           && l.cells[0].find("E124 EpFeature 120 AL60 AT0 AW40 AH100") != std::string::npos
           && l.cells[0].find("E142 EpRamp 150 AL60 AT0 AW40 AH100") != std::string::npos);
    // a Back MX alone keeps them all, as before
    const auto b = autorow::build({ "Back MX", "", "" }, "LR", one);
    assert(b.cells[0].find("E121 EpFeature 120 AL0 AT0 AW40 AH100") != std::string::npos
           && b.cells[0].find("E140 EpRamp 150 AL0 AT0 AW40 AH100") != std::string::npos);

    // shapes: the Config Tool's flasher circles, a bumper's on its own spots
    using autorow::detail::bumper_spots;
    assert(bumper_spots("LR", 0) == std::vector<int>({ 0, 1 }) && bumper_spots("LR", 1) == std::vector<int>({ 3, 4 }));
    assert(bumper_spots("LLCCRR", 0) == std::vector<int>{ 0 } && bumper_spots("LLCCRR", 1) == std::vector<int>{ 1 });
    assert(bumper_spots("LLCCRR", 2) == std::vector<int>{ 2 } && bumper_spots("LLCCRR", 3) == std::vector<int>{ 2 });
    assert(bumper_spots("LLLR", 0) == std::vector<int>{ 0 } && bumper_spots("LLLR", 1) == std::vector<int>{ 0 }
           && bumper_spots("LLLR", 2) == std::vector<int>{ 1 } && bumper_spots("LCR", 1) == std::vector<int>{ 2 });
    const auto s = autorow::build(mxs, "LLCCRR", one, true);
    const std::string &c = s.cells[0];
    const auto has = [&](const std::string &x) { return c.find(x) != std::string::npos; };
    assert(c.starts_with("E115 EpGlow Blink 200 AL0 AT0 AW19 AH100 SHPCircle3/E115 EpGlow Blink 200 AL20 AT0 AW19 AH100 SHPCircle3/"));
    assert(has("/E105 EpMain 80 AL0 AT0 AW19 AH100 SHPCircle3/E106 EpMain 80 AL20 AT0 AW19 AH100 SHPCircle3/"
               "E107 EpMain 80 AL40 AT0 AW19 AH100 SHPCircle3/E118 EpMain 80 AL40 AT0 AW19 AH100 SHPCircle3/"
               "E119 EpMain 80 AL60 AT0 AW19 AH100 SHPCircle3/E120 EpMain 80 AL80 AT0 AW19 AH100 SHPCircle3/"));
    assert(has("/E139 EpRamp 600 AL80 AT0 AW19 AH100 SHPCirclePulse/") && has("/E145 EpFeature 1000 AL40 AT0 AW19 AH100 SHPDiamondBoxPulse/"));
    assert(has("/E138 EpGlow 600 AL0 AT0 AW100 AH100 SHPFillLeftRight/") && has("/E143 EpJackpot 1200 AL0 AT0 AW19 AH100 SHPRoundAndRound/"));
    assert(has("/E144 EpJackpot 2400 AL80 AT0 AW19 AH100 SHPRoundAndRound/E144 EpGlow 900 6 AL0 AT0 AW9 AH30 SHPCircle3/"
               "E144 EpGlow 900 6 AL91 AT0 AW9 AH30 SHPCircle3/E113 EpGlow 900 AL0 AT0 AW19 AH100 SHPCrossPulse/"));
    assert(has("/E110 EpDanger 300 AL40 AT0 AW19 AH100 SHPCircle3/E112 EpDanger 600 AL0 AT0 AW19 AH100 SHPCircle3/"));
    assert(c.ends_with("/E112 EpDanger 600 AL80 AT0 AW19 AH100 SHPCircle3"));
    assert(!has("E121") && !has("E140") && !has("Blink 200 AL0 AT0 AW100"));
    // no features: no awards, the extra ball still a shape; shapes never reach other devices
    const auto p = autorow::build({ "Back MX", "", "", "5 Flasher Center", "", "", "Right MX", "", "" }, "LR", {}, true);
    assert(p.cells[0].find("E139") == std::string::npos && p.cells[0].find("E113 EpGlow 900 AL40 AT0 AW19 AH100 SHPCrossPulse") != std::string::npos);
    assert(p.cells[1].find("SHP") == std::string::npos && p.cells[2].find("SHP") == std::string::npos);
    assert(p.cells[1] == autorow::build({ "5 Flasher Center", "", "" }, "LR").cells[0]);
    const std::string f = autorow::file("x", s, "y");
    for (const char *k : { "EpGlow=", "EpMain=", "EpDanger=", "EpStart=", "EpOver=", "EpFeature=", "EpRamp=", "EpJackpot=" })
        assert(f.find(k) != std::string::npos);
    assert(f.find('@') == std::string::npos);
}

void never()
{
    // the ZB launch output turns a Pinscape plunger into a launch button
    for (auto n : { "ZB Launch Ball", "zb launch ball", "Authentic Launch Ball", "Extra Ball",
                    "Custom Output 1", "Hellball Motor", "RGB Undercab Complex MX", "Strobe", "", "Rom" })
        assert(autorow::build({ n }).empty());
    assert(autorow::build({}).empty());
}

void file()
{
    const auto r = autorow::build({ "Flipper Left", "RGB Flippers", "", "" });
    const std::string f = autorow::file("epicpinball_ep07", r, "x.ini");
    std::istringstream in(f);
    assert(!autorow::header(in));                                      // the made file has none: never made again from itself
    assert(f.find("[Colors DOF]\nEpGlow=#A0F0FFFF\n") != std::string::npos);    // Deep Sea's
    assert(f.find("\nepicpinball_ep07,E101,ON EpMain\n") != std::string::npos);
    assert(f.find('@') == std::string::npos);
}

// Each table's colours (autorow::colours): every name a row uses is in its
// file, a name libdof takes for a colour, and the table's own
void palette()
{
    assert(autorow::table_of("epicpinball_ep07") == 7 && autorow::table_of("epicpinball_ep13") == 13);
    assert(autorow::table_of("epicpinball_ep14") == 0 && autorow::table_of("epicpinball") == 0 && autorow::table_of("x") == 0);
    for (const char *n : autorow::kRoleName) {
        const std::string s = n;       // libdof: letters only, three or more, and no keyword's start
        assert(s.size() > 2 && s.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") == std::string::npos);
        assert(s.rfind("APC", 0) && s.rfind("SHP", 0) && s != "BLINK" && s != "INVERT" && s != "NOBOOL");
    }
    const Names all = { "Flipper Left", "RGB Flippers", "", "", "Left MX", "", "", "Right MX", "", "", "Back MX", "", "" };
    autorow::Sides sides;
    sides.hole = "LCR";
    sides.ramp = "LR";
    sides.kickback = "L";
    const std::string f = autorow::file("epicpinball_ep07", autorow::build(all, "LCR", sides, true), "y");
    for (size_t at = 0; (at = f.find(" Ep", at)) != std::string::npos; at++) {
        const std::string name = f.substr(at + 1, f.find_first_of(" /,\n", at + 1) - at - 1);
        assert(f.find("\n" + name + "=#") != std::string::npos);
    }
    assert(f.find("\nEpMain=#00C8FFFF\n") != std::string::npos);        // Deep Sea's aqua
    assert(autorow::file("x", autorow::build(all), "y").find("\nEpMain=#FFFF00FF\n") != std::string::npos);
    for (int t = 1; t <= 13; t++) {
        assert(autorow::kPalette[t][autorow::kDanger] == 0xFF0000);
        for (int u = 1; u < t; u++)
            assert(std::memcmp(autorow::kPalette[t], autorow::kPalette[u], sizeof autorow::kPalette[t]));
    }
}

} // namespace

int main()
{
    headers();
    cabinet();
    bumpers();
    lights();
    flashers();
    features();
    mx();
    never();
    file();
    palette();
    std::puts("autorow_test: ok");
}
