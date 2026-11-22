// SPDX-License-Identifier: GPL-3.0-or-later
// events.h - what the game reports (src/feedback.h), and the DOF numbers it goes out as.
#pragma once
#include <string>

// In the game's order, E101 on. People's rows are written in these numbers:
// never renumber them, only add at the end.
inline constexpr const char *kEvent[] = {
    "flipper_left", "flipper_right", "sling_left", "sling_right",
    "bumper_left", "bumper_center", "bumper_right",
    "launch", "ball_release", "drain", "nudge", "tilt", "knocker",
};
inline constexpr int kEvents = sizeof kEvent / sizeof *kEvent;
inline constexpr int kFlippers = 2;             // levels; the rest only happen
inline constexpr int kFirstE = 101;

inline int event_by_name(const std::string &s)
{
    for (int i = 0; i < kEvents; i++)
        if (s == kEvent[i]) return i;
    return -1;
}
