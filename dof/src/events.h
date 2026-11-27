// SPDX-License-Identifier: GPL-3.0-or-later
// events.h - what the game reports (src/feedback.h), and the DOF names it goes out as.
#pragma once
#include <string>

// The game's names, in its order (src/feedback.h): what it sends the helper,
// one line an event. Only add at the end.
inline constexpr const char *kEvent[] = {
    "flipper_left", "flipper_right", "sling_left", "sling_right",
    "bumper_1", "bumper_2", "bumper_3",
    "launch", "ball_release", "drain", "nudge", "tilt", "knocker",
    "ball_ready", "multiball", "game_start", "game_over",
    "bumper_4", "bumper_5", "bumper_6",
    "hole_1", "hole_2", "hole_3", "hole_4", "hole_5", "ball_held", "kickback",
    "target_1", "target_2", "target_3", "targets_down",
    "rollover_1", "rollover_2", "rollover_3", "rollover_4", "rollover_5", "rollover_6",
    "lanes_lit", "skill_shot", "ramp_1", "ramp_2", "ramp_3",
    "jackpot", "super_jackpot", "award",
    "switch_1", "switch_2", "switch_3", "switch_4", "switch_5", "switch_6", "switch_7", "switch_8",
    "switch_9", "switch_10", "switch_11", "switch_12", "switch_13", "switch_14", "switch_15", "switch_16",
    "switch_17", "switch_18", "switch_19", "switch_20", "switch_21", "switch_22", "switch_23", "switch_24",
    "lamp_1", "lamp_2", "lamp_3", "lamp_4", "lamp_5", "lamp_6", "lamp_7", "lamp_8", "lamp_9", "lamp_10", "lamp_11",
    "lamp_12", "lamp_13", "lamp_14", "lamp_15", "lamp_16", "lamp_17", "lamp_18", "lamp_19", "lamp_20", "lamp_21",
    "lamp_22", "lamp_23", "lamp_24", "lamp_25", "lamp_26", "lamp_27", "lamp_28", "lamp_29", "lamp_30", "lamp_31",
    "lamp_32", "lamp_33", "lamp_34", "lamp_35", "lamp_36", "lamp_37", "lamp_38", "lamp_39", "lamp_40", "lamp_41",
    "lamp_42", "lamp_43", "lamp_44", "lamp_45", "lamp_46", "lamp_47", "lamp_48", "lamp_49", "lamp_50", "lamp_51",
    "lamp_52", "lamp_53", "lamp_54", "lamp_55", "lamp_56", "lamp_57", "lamp_58", "lamp_59", "lamp_60", "lamp_61",
    "lamp_62", "lamp_63", "lamp_64", "lamp_65", "lamp_66", "lamp_67", "lamp_68", "lamp_69", "lamp_70", "lamp_71",
    "lamp_72", "lamp_73", "lamp_74", "lamp_75", "lamp_76", "lamp_77", "lamp_78", "lamp_79", "lamp_80", "lamp_81",
    "lamp_82", "lamp_83", "lamp_84", "lamp_85", "lamp_86", "lamp_87", "lamp_88", "lamp_89", "lamp_90", "lamp_91",
    "lamp_92", "lamp_93", "lamp_94", "lamp_95", "lamp_96",
};
inline constexpr int kEvents = sizeof kEvent / sizeof *kEvent;
enum : int {
    kFlipperLeft, kFlipperRight, kSlingLeft, kSlingRight, kBumper1, kBumper2, kBumper3,
    kLaunch, kBallRelease, kDrain, kNudge, kTilt, kKnocker,
    kBallReady, kMultiball, kGameStart, kGameOver, kBumper4, kBumper5, kBumper6,
    kHole1, kHole2, kHole3, kHole4, kHole5, kBallHeld, kKickback,
    kTarget1, kTarget2, kTarget3, kTargetsDown,
    kRollover1, kRollover2, kRollover3, kRollover4, kRollover5, kRollover6,
    kLanesLit, kSkillShot, kRamp1, kRamp2, kRamp3, kJackpot, kSuperJackpot, kAward,
    kSwitch1, kSwitch24 = kSwitch1 + 23,
    kLamp1, kLamp96 = kLamp1 + 95,
};
inline constexpr int kBumpers = 6, kHoles = 5, kTargets = 3, kRollovers = 6, kRamps = 3, kSwitches = 24, kLamps = 96;
static_assert(sizeof kEvent / sizeof *kEvent == kLamp96 + 1, "a name for every event");

// What a DOF row calls them: $FlipperLeft, as a table without a ROM names its
// events. The same on every table. A table's own features (an empty name
// here) have names of its own instead, t1PowerUpHole, and its lamps are
// t1Lamp1 on (autorow::Table).
inline constexpr const char *kDofName[] = {
    "FlipperLeft", "FlipperRight", "SlingshotLeft", "SlingshotRight",
    "Bumper1", "Bumper2", "Bumper3",
    "Launch", "BallRelease", "Drain", "Nudge", "Tilt", "ExtraBall",
    "BallReady", "Multiball", "GameStart", "GameOver",
    "Bumper4", "Bumper5", "Bumper6",
    "", "", "", "", "", "BallHeld", "",
    "", "", "", "",
    "", "", "", "", "", "",
    "LanesLit", "SkillShot", "", "", "",
    "Jackpot", "SuperJackpot", "Award",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
};
static_assert(sizeof kDofName / sizeof *kDofName == kEvents, "a DOF name for every event");

// A table's own feature, named by the table
inline constexpr bool is_feature(int e) { return e >= 0 && e < kEvents && *kDofName[e] == 0; }

// A table's lamp
inline constexpr bool is_lamp(int e) { return e >= kLamp1 && e <= kLamp96; }

// Levels: on while it lasts. The rest only happen.
inline constexpr bool is_level(int e)
{
    return e == kFlipperLeft || e == kFlipperRight || e == kBallReady || e == kMultiball || e == kBallHeld || is_lamp(e);
}

// A table's n-th bumper from the left, 0 on
inline constexpr int bumper_event(int i) { return i < 3 ? kBumper1 + i : kBumper4 + i - 3; }

inline int event_by_name(const std::string &s)
{
    for (int i = 0; i < kEvents; i++)
        if (s == kEvent[i]) return i;
    return -1;
}
