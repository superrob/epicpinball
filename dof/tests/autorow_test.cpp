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
    assert(row(n) == "t,$ExtraBall,$FlipperRight,$FlipperLeft,ON EpMain,$Bumper3,0,$Bumper1,$SlingshotLeft,ON,0,$Multiball EpGlow Blink 200/$Bumper2 EpMain 80/$ExtraBall EpGlow 450 3/$GameStart EpStart 450 3/$GameOver EpOver 450 3/$Drain EpDanger 300/$Tilt EpDanger 600,$Bumper2,$SlingshotRight,ON,0,0,0,0,0,$ExtraBall 2000,ON,"
                     "$Nudge 300 I32/$Tilt 600 I32");
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
    assert(row(ten, "LCR") == "t,$Bumper1,$Bumper2,$Bumper3,0,0,$Launch");
    assert(row(ten, "LLCCRR") == "t,$Bumper1/$Bumper2,$Bumper3/$Bumper4,$Bumper5/$Bumper6,0,0,$Launch");          // table 5
    assert(row(ten, "LR") == "t,$Bumper1,0,$Bumper2,0,0,$Launch");                                   // tables 1, 10, 12, 13
    assert(row(ten, "C") == "t,0,$Bumper1,0,0,0,$Launch");                                       // table 4
    assert(row(ten, "LLLLLL") == "t,$Bumper1/$Bumper2/$Bumper3/$Bumper4/$Bumper5/$Bumper6,0,0,0,0,$Launch");
    assert(row(ten, "LCRLCRLCR") == "t,$Bumper1/$Bumper4,$Bumper2/$Bumper5,$Bumper3/$Bumper6,0,0,$Launch");         // six at most
    assert(row({ "10 Bumper Middle Left", "10 Bumper Middle Center", "10 Bumper Middle Right" }) == "t,$Bumper1,$Bumper2,$Launch/$Bumper3");
    assert(row({ "10 Bumper Middle Left", "10 Bumper Back Left" }) == "t,0,$Bumper1");
    const Names eight = { "8 Bumper Left", "8 Bumper Center", "8 Bumper Right", "8 Bumper Back" };
    assert(row(eight, "LCR") == "t,$Bumper1,$Bumper2,$Launch/$Bumper3");
    assert(row(eight, "LLCCRR") == "t,$Bumper1/$Bumper2,$Bumper3/$Bumper4,$Launch/$Bumper5/$Bumper6");
    assert(row(eight, "LR") == "t,$Bumper1,0,$Launch/$Bumper2");
    assert(row({ "8 Bumper Back", "10 Bumper Middle Right", "8 Bumper Right" }) == "t,0,$Launch/$Bumper3,$Launch");
    assert(row({ "Slingshot Right", "slingshot right", "FLIPPER LEFT" }) == "t,$SlingshotRight,$SlingshotRight,$FlipperLeft");    // every one, any case
    assert(row({ "Launch Button", "Knocker" }) == "t,$BallReady,$ExtraBall");
    for (int e : { kBumper1, kBumper2, kBumper3, kBumper4, kBumper5, kBumper6 })
        assert(autorow::build(ten, "LLCCRR").fires[e]);
}

void lights()
{
    assert(row({ "RGB Flippers" }) == "t,ON EpMain");                   // its blanks cut from the line's end
    assert(row({ "RGB Flippers", "" }) == "t,ON EpMain");
    assert(row({ "Exit", "RGB Flippers", "", "", "Coin" }) == "t,ON,ON EpMain,ON");
    auto r = autorow::build({ "RGB Flippers", "Knocker", "", "Coin" });  // no room for three: off, and said
    assert(autorow::line("t", r) == "t,0,$ExtraBall,0,ON" && r.said.size() == 1);
    assert(row({ "RGB Undercab Complex MX", "", "", "Knocker" }) == "t,0,0,0,$ExtraBall");     // unknown RGB: a zero each
    assert(row({ "Shaker", "Gear", "Fan", "Strobe", "Bell" }) == "t,$Nudge 300 I32/$Tilt 600 I32");
}

void flashers()
{
    // five spots across the back; a bumper flashes its side's, drain the centre
    const Names five = { "5 Flasher Outside Left", "", "", "5 Flasher Left", "", "", "5 Flasher Center", "", "",
                         "5 Flasher Right", "", "", "5 Flasher Outside Right", "", "" };
    const auto r = autorow::build(five, "LLCCRR");                     // table 5
    assert(r.cells.size() == 5 && r.outputs == 15 && r.said.empty());
    const std::string all = "$Multiball EpGlow Blink 200/", end = "$ExtraBall EpGlow 450 3/$GameStart EpStart 450 3/$GameOver EpOver 450 3/";
    assert(r.cells[0] == all + "$Bumper1 EpMain 80/$Bumper2 EpMain 80/" + end + "$Tilt EpDanger 600");
    assert(r.cells[1] == r.cells[0]);
    assert(r.cells[2] == all + "$Bumper3 EpMain 80/$Bumper4 EpMain 80/" + end + "$Drain EpDanger 300/$Tilt EpDanger 600");
    assert(r.cells[3] == all + "$Bumper5 EpMain 80/$Bumper6 EpMain 80/" + end + "$Tilt EpDanger 600");
    assert(autorow::build(five, "C").cells[0] == all + end + "$Tilt EpDanger 600");    // table 4: the outer spots, no bumper

    const Names three = { "3 Flasher Left", "", "", "3 Flasher Center", "", "", "3 Flasher Right", "", "" };
    const auto t = autorow::build(three, "LR");                        // tables 1, 10, 12, 13
    assert(t.cells[0] == all + "$Bumper1 EpMain 80/" + end + "$Tilt EpDanger 600");
    assert(t.cells[1] == all + end + "$Drain EpDanger 300/$Tilt EpDanger 600");
    assert(t.cells[2] == all + "$Bumper2 EpMain 80/" + end + "$Tilt EpDanger 600");

    // a strip: the same, as areas of it
    const auto m = autorow::build({ "Back MX", "", "", "Undercab MX", "", "" }, "LCR");
    assert(m.cells.size() == 1 && m.outputs == 3);                     // its own three; the undercab is not a flasher
    assert(m.cells[0] == "$Multiball EpGlow Blink 200 AL0 AT0 AW100 AH100/$Bumper1 EpMain 80 AL0 AT0 AW40 AH100/"
                         "$Bumper2 EpMain 80 AL40 AT0 AW20 AH100/$Bumper3 EpMain 80 AL60 AT0 AW40 AH100/"
                         "$ExtraBall EpGlow 450 3 AL0 AT0 AW100 AH100/$GameStart EpStart 450 3 AL0 AT0 AW100 AH100/"
                         "$GameOver EpOver 450 3 AL0 AT0 AW100 AH100/$Drain EpDanger 300 AL40 AT0 AW20 AH100/$Tilt EpDanger 600 AL0 AT0 AW100 AH100");

    // a name it does not know is said, and left off; no flasher has a colour it does not define
    const auto u = autorow::build({ "5 Flashers Center", "", "", "Flasher", "Knocker" });
    assert(u.said.size() == 2 && autorow::line("t", u) == "t,0,0,0,0,$ExtraBall");
    const std::string f = autorow::file("x", r, "y");
    for (const char *c : { "EpGlow=", "EpMain=", "EpDanger=", "EpStart=", "EpOver=", "EpFeature=", "EpRamp=", "EpJackpot=" })
        assert(f.find(c) != std::string::npos);
    assert(f.find('@') == std::string::npos && f.find("SHP") == std::string::npos);
}

void features()
{
    // table 1: holes, ramps, a kickback and a bank of targets, each with its side
    const auto one = autorow::features(1, "hole1=TestHole:L hole2=LeftHole:L hole3=PowerUpHole:C hole4=PhysicalSystemsHole:R hole5=RightHole:R "
                                         "ramp1=LeftRamp:L ramp2=CentreRamp:R ramp3=RightRamp:R kickback1=Kickback:L "
                                         "target1=DropTarget1:L target2=DropTarget2:L target3=DropTarget3:L bank=DropTargets");
    assert(one && one->hole == "LLCRR" && one->ramp == "LRR" && one->kickback == "L" && one->bank() == 'L');
    assert(one->number == 1 && one->name[kHole3] == "PowerUpHole" && one->name[kTargetsDown] == "DropTargetsDown");
    const auto lanes = autorow::features(1, "rollover1=Star:L rollover2=LeftLane:L  rollover3=TopLane1:C");
    assert(lanes && lanes->empty() && lanes->dof(kRollover1) == "t1Star" && lanes->dof(kRollover3) == "t1TopLane1");
    assert(autorow::features(1, "") && autorow::features(1, "")->empty());
    for (auto bad : { "hole1=A:L hole2=B:L hole3=C:L hole4=D:L hole5=E:L hole6=F:L", "hole1=A:X", "rubber1=A:L",
                      "hole1=A:L hole1=B:R", "hole1=:L", "hole1=A", "hole1", "hole2=A:L", "hole1=A:L hole3=B:L",
                      "kickback2=A:L", "ramp4=A:L", "target4=A:L", "hole1=A:l", "hole1=Power-Up:L", "hole1=1Up:L",
                      "hole1=A:L ramp1=a:L", "target1=A:L", "bank=Bank", "target1=A:L bank=B bank=C", "hole0=A:L" })
        assert(!autorow::features(1, bad));
    assert(autorow::features(1, "target1=A:L target2=B:R target3=C:R bank=D")->bank() == 'R');
    assert(autorow::features(1, "target1=A:L target2=B:C target3=C:R bank=D")->bank() == 'L');

    // DOF names: the same on every table but for its features; a table with none, none of them
    assert(one->dof(kFlipperLeft) == "FlipperLeft" && one->dof(kKnocker) == "ExtraBall" && one->dof(kBumper6) == "Bumper6");
    assert(one->dof(kHole3) == "t1PowerUpHole" && one->dof(kRamp2) == "t1CentreRamp" && one->dof(kTargetsDown) == "t1DropTargetsDown");
    assert(one->dof(kRollover1).empty() && autorow::Table{}.dof(kHole1).empty() && autorow::Table{}.dof(kJackpot) == "Jackpot");
    assert(autorow::features(12, "ramp1=Loop:C")->dof(kRamp1) == "t12Loop");
    // a table's other features: switch1 to switch16, numbered from 1, kept out of made rows
    const auto two = autorow::features(2, "switch1=BallLock:C switch2=RainbowLane:L hole1=TreasureHole:C");
    assert(two && two->dof(kSwitch1 + 1) == "t2RainbowLane" && two->dof(kSwitch1 + 2).empty());
    assert(!autorow::features(2, "switch2=RainbowLane:L") && !autorow::features(2, "switch25=A:L")
           && !autorow::features(2, "switch01=A:L") && !autorow::features(2, "switch=A:L"));
    std::string sixteen;
    for (int i = 1; i <= 24; i++) sixteen += "switch" + std::to_string(i) + "=S" + std::to_string(i) + ":C ";
    assert(autorow::features(2, sixteen) && autorow::features(2, sixteen)->dof(kSwitch24) == "t2S24");
    assert(!autorow::row_has(kSwitch1, "LCR", *two));
    assert(autorow::build({ "Beacon", "10 Bumper Middle Center" }, "LCR", *two).cells[1] == "$Bumper2/$t2TreasureHole");
    for (int e = 0; e < kEvents; e++) assert(is_feature(e) == !*kDofName[e]);

    // their solenoids: the middle row on their side, after the bumpers there and the launch
    const auto made = [&](const Names &n, const std::string &b) { return autorow::line("t", autorow::build(n, b, *one)); };
    const Names ten = { "10 Bumper Back Left", "10 Bumper Back Center", "10 Bumper Back Right",
                        "10 Bumper Middle Left", "10 Bumper Middle Center", "10 Bumper Middle Right" };
    assert(made(ten, "LR") == "t,$Bumper1,0,$Bumper2,$t1TestHole/$t1LeftHole/$t1Kickback/$t1DropTargetsDown,$t1PowerUpHole,$Launch/$t1PhysicalSystemsHole/$t1RightHole");
    const Names eight = { "8 Bumper Left", "8 Bumper Center", "8 Bumper Right", "8 Bumper Back" };
    assert(made(eight, "LR") == "t,$Bumper1/$t1TestHole/$t1LeftHole/$t1Kickback/$t1DropTargetsDown,$t1PowerUpHole,$Launch/$Bumper2/$t1PhysicalSystemsHole/$t1RightHole");
    assert(made({ "10 Bumper Back Left", "10 Bumper Back Right" }, "LR") == "t,$Bumper1,$Bumper2");     // nowhere for them
    assert(row(ten, "LR") == "t,$Bumper1,0,$Bumper2,0,0,$Launch");                                   // no features: as before
    assert(made({ "Beacon" }, "LR") == "t,$ExtraBall 2000/$Jackpot 2000/$SuperJackpot 2000" && row({ "Beacon" }) == "t,$ExtraBall 2000");

    // flashers: kick-outs and ramps on their side, the awards everywhere
    const Names five = { "5 Flasher Outside Left", "", "", "5 Flasher Left", "", "", "5 Flasher Center", "", "",
                         "5 Flasher Right", "", "", "5 Flasher Outside Right", "", "" };
    const auto r = autorow::build(five, "LR", *one);
    const std::string all = "$Multiball EpGlow Blink 200/",
                      awards = "$SkillShot EpRamp 450 3/$Award EpFeature 450 3/$LanesLit EpGlow 450 3/$Jackpot EpJackpot 450 3/$SuperJackpot EpJackpot 900 6/",
                      end = "$ExtraBall EpGlow 450 3/$GameStart EpStart 450 3/$GameOver EpOver 450 3/";
    assert(r.cells[0] == all + "$Bumper1 EpMain 80/$t1TestHole EpFeature 120/$t1LeftHole EpFeature 120/$t1Kickback EpFeature 120/$t1LeftRamp EpRamp 150/" + awards + end
                         + "$Tilt EpDanger 600");
    assert(r.cells[2] == all + "$t1PowerUpHole EpFeature 120/" + awards + end + "$Drain EpDanger 300/$Tilt EpDanger 600");
    assert(r.cells[4] == all + "$Bumper2 EpMain 80/$t1PhysicalSystemsHole EpFeature 120/$t1RightHole EpFeature 120/$t1CentreRamp EpRamp 150/$t1RightRamp EpRamp 150/" + awards + end
                         + "$Tilt EpDanger 600");
    for (int e : { kHole1, kHole5, kKickback, kTargetsDown, kRamp3, kJackpot, kSuperJackpot, kAward, kSkillShot, kLanesLit })
        assert(autorow::build(five, "LR", *one).fires[e] || e == kTargetsDown);
    assert(autorow::build(ten, "LR", *one).fires[kTargetsDown]);

    // the "no device for" line: what this table has and a made row could carry, runs as one
    using autorow::row_has;
    assert(row_has(kHole5, "LR", *one) && !row_has(kHole5, "LR", *autorow::features(1, "hole1=A:L hole2=B:C")));
    assert(!row_has(kRollover1, "LR", *one) && !row_has(kTarget1, "LR", *one) && !row_has(kBallHeld, "LR", *one));
    assert(row_has(kJackpot, "LR", *one) && !row_has(kJackpot, "LR", {}) && !row_has(kKickback, "LR", {}));
    assert(!row_has(kBumper3, "LR", {}) && row_has(kBallRelease, "LR", {}));
    assert(autorow::listed({ kBallRelease, kNudge, kBallReady }) == "BallRelease, Nudge, BallReady");
    assert(autorow::listed({ kBumper1, kBumper2, kBumper3, kBumper4, kHole1, kHole2, kHole3, kHole4, kHole5, kJackpot }, *one)
           == "Bumper1-3, Bumper4, t1TestHole, t1LeftHole, t1PowerUpHole, t1PhysicalSystemsHole, t1RightHole, Jackpot");
    assert(autorow::listed({ kTarget1, kTarget2, kTarget3, kRamp1, kRamp3 }, *one)
           == "t1DropTarget1-3, t1LeftRamp, t1RightRamp");
}

void mx()
{
    const auto one = *autorow::features(1, "hole1=TestHole:L hole2=LeftHole:L hole3=PowerUpHole:C hole4=PhysicalSystemsHole:R hole5=RightHole:R "
                                         "ramp1=LeftRamp:L ramp2=CentreRamp:R ramp3=RightRamp:R kickback1=Kickback:L "
                                         "target1=DropTarget1:L target2=DropTarget2:L target3=DropTarget3:L bank=DropTargets");
    const Names mxs = { "Back MX", "", "", "Left MX", "", "", "Right MX", "", "", "RGB Undercab Complex MX", "", "" };

    // the side MXs: ball ready breathing at the bottom of the right one, the
    // slings rising, kick-outs and ramps down their side; centre on both
    const auto r = autorow::build(mxs, "LR", one);
    assert(r.cells.size() == 3 && r.outputs == 9 && r.width == 12 && r.said.empty());
    const std::string sling = " EpMain M50 MAX50 AT0 AH100 ADU AS300", out = " EpFeature 75 AT0 AH100 ADD AS300",
                      ramp = " EpRamp 75 AT0 AH100 ADD AS300";
    assert(r.cells[1] == "$SlingshotLeft" + sling + "/$t1TestHole" + out + "/$t1LeftHole" + out + "/$t1PowerUpHole" + out + "/$t1Kickback" + out + "/$t1LeftRamp" + ramp);
    assert(r.cells[2] == "$BallReady EpMain AL0 AW100 AT85 AH15 F500 BLINK 2000/$SlingshotRight" + sling + "/$t1PowerUpHole" + out + "/$t1PhysicalSystemsHole" + out
                         + "/$t1RightHole" + out + "/$t1CentreRamp" + ramp + "/$t1RightRamp" + ramp);
    for (int e : { kBallReady, kSlingLeft, kSlingRight, kHole3, kKickback, kRamp2 }) assert(r.fires[e]);
    // which leave the Back MX: all of them, so it keeps no hole, kickback or ramp
    for (const char *gone : { "$t1TestHole", "$t1PowerUpHole", "$t1RightHole", "$t1Kickback", "$t1LeftRamp", "$t1RightRamp" })
        assert(r.cells[0].find(gone) == std::string::npos);
    // no table features: slings and ball ready only
    assert(row({ "Left MX", "", "", "Right MX", "", "" }) == "t,$SlingshotLeft" + sling + ",$BallReady EpMain AL0 AW100 AT85 AH15 F500 BLINK 2000/$SlingshotRight" + sling);
    // with only a left MX, the right side's stay on the Back MX, the centre hole goes left
    const auto l = autorow::build({ "Back MX", "", "", "Left MX", "", "" }, "LR", one);
    assert(l.cells[1] == "$SlingshotLeft" + sling + "/$t1TestHole" + out + "/$t1LeftHole" + out + "/$t1PowerUpHole" + out + "/$t1Kickback" + out + "/$t1LeftRamp" + ramp);
    assert(l.cells[0].find("$t1TestHole") == std::string::npos && l.cells[0].find("$t1PowerUpHole") == std::string::npos
           && l.cells[0].find("$t1PhysicalSystemsHole EpFeature 120 AL60 AT0 AW40 AH100") != std::string::npos
           && l.cells[0].find("$t1RightRamp EpRamp 150 AL60 AT0 AW40 AH100") != std::string::npos);
    // a Back MX alone keeps them all, as before
    const auto b = autorow::build({ "Back MX", "", "" }, "LR", one);
    assert(b.cells[0].find("$t1TestHole EpFeature 120 AL0 AT0 AW40 AH100") != std::string::npos
           && b.cells[0].find("$t1LeftRamp EpRamp 150 AL0 AT0 AW40 AH100") != std::string::npos);

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
    assert(c.starts_with("$Multiball EpGlow Blink 200 AL0 AT0 AW19 AH100 SHPCircle3/$Multiball EpGlow Blink 200 AL20 AT0 AW19 AH100 SHPCircle3/"));
    assert(has("/$Bumper1 EpMain 80 AL0 AT0 AW19 AH100 SHPCircle3/$Bumper2 EpMain 80 AL20 AT0 AW19 AH100 SHPCircle3/"
               "$Bumper3 EpMain 80 AL40 AT0 AW19 AH100 SHPCircle3/$Bumper4 EpMain 80 AL40 AT0 AW19 AH100 SHPCircle3/"
               "$Bumper5 EpMain 80 AL60 AT0 AW19 AH100 SHPCircle3/$Bumper6 EpMain 80 AL80 AT0 AW19 AH100 SHPCircle3/"));
    assert(has("/$SkillShot EpRamp 600 AL80 AT0 AW19 AH100 SHPCirclePulse/") && has("/$Award EpFeature 1000 AL40 AT0 AW19 AH100 SHPDiamondBoxPulse/"));
    assert(has("/$LanesLit EpGlow 600 AL0 AT0 AW100 AH100 SHPFillLeftRight/") && has("/$Jackpot EpJackpot 1200 AL0 AT0 AW19 AH100 SHPRoundAndRound/"));
    assert(has("/$SuperJackpot EpJackpot 2400 AL80 AT0 AW19 AH100 SHPRoundAndRound/$SuperJackpot EpGlow 900 6 AL0 AT0 AW9 AH30 SHPCircle3/"
               "$SuperJackpot EpGlow 900 6 AL91 AT0 AW9 AH30 SHPCircle3/$ExtraBall EpGlow 900 AL0 AT0 AW19 AH100 SHPCrossPulse/"));
    assert(has("/$Drain EpDanger 300 AL40 AT0 AW19 AH100 SHPCircle3/$Tilt EpDanger 600 AL0 AT0 AW19 AH100 SHPCircle3/"));
    assert(c.ends_with("/$Tilt EpDanger 600 AL80 AT0 AW19 AH100 SHPCircle3"));
    assert(!has("$t1TestHole") && !has("$t1LeftRamp") && !has("Blink 200 AL0 AT0 AW100"));
    // no features: no awards, the extra ball still a shape; shapes never reach other devices
    const auto p = autorow::build({ "Back MX", "", "", "5 Flasher Center", "", "", "Right MX", "", "" }, "LR", {}, true);
    assert(p.cells[0].find("$SkillShot") == std::string::npos && p.cells[0].find("$ExtraBall EpGlow 900 AL40 AT0 AW19 AH100 SHPCrossPulse") != std::string::npos);
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
    assert(f.find("\nepicpinball_ep07,$FlipperLeft,ON EpMain\n") != std::string::npos);
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
    const auto sides = autorow::features(7, "hole1=A:L hole2=B:C hole3=C:R ramp1=D:L ramp2=E:R kickback1=F:L");
    const std::string f = autorow::file("epicpinball_ep07", autorow::build(all, "LCR", *sides, true), "y");
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
