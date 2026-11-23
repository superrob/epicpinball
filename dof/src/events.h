// SPDX-License-Identifier: GPL-3.0-or-later
// events.h - what the game reports (src/feedback.h), and the DOF numbers it goes out as.
#pragma once
#include <string>

// In the game's order, E101 on. People's rows are written in these numbers:
// only add at the end. E105-E107 were renumbered once, from a cabinet's
// sides (left, centre, right) to a table's bumpers.
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
};
inline constexpr int kEvents = sizeof kEvent / sizeof *kEvent;
inline constexpr int kFirstE = 101;
enum : int {
    kFlipperLeft, kFlipperRight, kSlingLeft, kSlingRight, kBumper1, kBumper2, kBumper3,
    kLaunch, kBallRelease, kDrain, kNudge, kTilt, kKnocker,
    kBallReady, kMultiball, kGameStart, kGameOver, kBumper4, kBumper5, kBumper6,
    kHole1, kHole2, kHole3, kHole4, kHole5, kBallHeld, kKickback,
    kTarget1, kTarget2, kTarget3, kTargetsDown,
    kRollover1, kRollover2, kRollover3, kRollover4, kRollover5, kRollover6,
    kLanesLit, kSkillShot, kRamp1, kRamp2, kRamp3, kJackpot, kSuperJackpot, kAward,
};
inline constexpr int kBumpers = 6, kHoles = 5, kTargets = 3, kRollovers = 6, kRamps = 3;
static_assert(sizeof kEvent / sizeof *kEvent == kAward + 1, "a name for every event");

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
