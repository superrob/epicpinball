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
};
inline constexpr int kEvents = sizeof kEvent / sizeof *kEvent;
inline constexpr int kFirstE = 101;
enum : int {
    kFlipperLeft, kFlipperRight, kSlingLeft, kSlingRight, kBumper1, kBumper2, kBumper3,
    kLaunch, kBallRelease, kDrain, kNudge, kTilt, kKnocker,
    kBallReady, kMultiball, kGameStart, kGameOver, kBumper4, kBumper5, kBumper6,
};
inline constexpr int kBumpers = 6;

// Levels: on while it lasts. The rest only happen.
inline constexpr bool is_level(int e) { return e == kFlipperLeft || e == kFlipperRight || e == kBallReady || e == kMultiball; }

// A table's n-th bumper from the left, 0 on
inline constexpr int bumper_event(int i) { return i < 3 ? kBumper1 + i : kBumper4 + i - 3; }

inline int event_by_name(const std::string &s)
{
    for (int i = 0; i < kEvents; i++)
        if (s == kEvent[i]) return i;
    return -1;
}
