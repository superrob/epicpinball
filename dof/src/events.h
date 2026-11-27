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
};
inline constexpr int kBumpers = 6, kHoles = 5, kTargets = 3, kRollovers = 6, kRamps = 3, kSwitches = 24;
static_assert(sizeof kEvent / sizeof *kEvent == kSwitch24 + 1, "a name for every event");

// What a DOF row calls them: $FlipperLeft, as a table without a ROM names its
// events. The same on every table. A table's own features (an empty name
// here) have names of its own instead, t1PowerUpHole (autorow::Table).
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
};
static_assert(sizeof kDofName / sizeof *kDofName == kEvents, "a DOF name for every event");

// A table's own feature, named by the table
inline constexpr bool is_feature(int e) { return e >= 0 && e < kEvents && *kDofName[e] == 0; }

// Levels: on while it lasts. The rest only happen.
inline constexpr bool is_level(int e)
{
    return e == kFlipperLeft || e == kFlipperRight || e == kBallReady || e == kMultiball || e == kBallHeld;
}

// A table's n-th bumper from the left, 0 on
inline constexpr int bumper_event(int i) { return i < 3 ? kBumper1 + i : kBumper4 + i - 3; }

inline int event_by_name(const std::string &s)
{
    for (int i = 0; i < kEvents; i++)
        if (s == kEvent[i]) return i;
    return -1;
}
