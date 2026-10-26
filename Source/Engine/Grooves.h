// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Session Jam contributors
#pragma once
#include <string>
#include <vector>

namespace sj {
/** Authored grooves. One bar = 16 sixteenths; patterns may be 2 bars (32 chars) and loop.
    Drum chars: '-' rest, 'g' ghost, 'x' normal, 'X' accent (hat line: 'o'/'O' open hat, 'p' foot).
    Bass chars: 'R' chord bass note, '5' fifth, '8' octave, '3' third, '7' seventh/sixth, 'a' approach (half-step below next root),
                'g' dead/ghost note, '_' hold, '-' rest.
    Keys chars: 'C' chord, 'c' soft chord, 's' short stab, '_' hold, '-' release, '1'..'4' single voice (arpeggio). */
struct Groove {
    const char* name;
    float swing8, swing16;        // fraction of a 16th step: offbeat-8th / offbeat-16th delay
    float laidBackMs;             // bass/keys behind the beat
    struct Tier { std::string kick, snare, hat, bass, keys; } tiers[3];
    std::vector<std::pair<int,int>> fill;     // (16th step within last beat or two, GM note) ; steps 8..15
    int hatNote = 42;
};

inline const Groove& groove (int style) {
    static const Groove g[] = {
        { "Rock", 0.0f, 0.0f, 0.0f, {
            { "X-------X-------X-------X-x-----", "----X-------X-------X-------X--g",
              "x-x-x-x-x-x-x-x-x-x-x-x-x-x-x-x-", "R_______R_______R_______R_______", "C_______________C_______________" },
            { "X-----x-X-x-----X-----x-X-------", "----X--g----X-------X--g----X---",
              "x-x-x-x-x-x-x-x-x-x-x-x-x-x-o-x-", "R-R-R-R-R-R-R-R-R-R-R-R-R-5-8-R-", "C_______c___c___C_______c___s---" },
            { "X-x---x-X-x---x-X-x---x-X-x-x---", "----X--g----X--g----X--g----X-g-",
              "X-x-X-x-X-x-X-x-X-x-X-x-X-x-O-x-", "RRRRRRRRRRRRRRRRRRRRRRRRRRRR5585", "C___s-C_s-C_C___C___s-C_s-C___s-" } },
          { {12,38},{13,38},{14,45},{15,41}, {8,48},{10,48} }, 42 },
        { "Funk", 0.0f, 0.12f, 0.0f, {
            { "X------x--X-----X------x--X-----", "----X-------X-------X-------X---",
              "x-x-x-x-x-x-x-x-x-x-x-x-x-x-x-x-", "R-----g-R-------R-----g-8-------", "--s-------s-----s-----s---s-----" },
            { "X-----x--xX-----X-----x---X--x--", "-g--X-g--g--X--g-g--X-g--g--X-gg",
              "xxxxxxxxxxxxxxoxxxxxxxxxxxxxxxox", "R--8g-R-g-R-8-g5R--8g-R-g-7-8-5a", "-s--s--s-s--s---s--s--s---s-s--s" },
            { "X-x---x--xX---x-X-x---x--xX-x-x-", "-g-gX-g--g-gX-g--g-gX-g--g-gX-gg",
              "XxxxXxxxXxxxXxoxXxxxXxxxXxxxXxox", "R-8gR8g-R-8g5-7aR-8gR8g-R-8g5-7a", "s-s--s-ss-s--s-ss-s--s-ss-s--ss-" } },
          { {12,38},{13,38},{14,38},{15,38}, {9,37},{11,38} }, 42 },
        { "Blues", 0.333f, 0.0f, 4.0f, {
            { "X-------X-------X-------X-------", "----x-------x-------x-------x---",
              "x---x-x-x---x-x-x---x-x-x---x-x-", "R---5---R---5---R---5---R---5---", "----c_------c_------c_------c_--" },
            { "X-----x-X-------X-----x-X-------", "----X-------X-------X-------X--g",
              "x---x-x-x---x-x-x---x-x-x---x-x-", "R---3---5---7---8---7---5---3---", "--c---c---c---c---c---c---c---c-" },
            { "X---X-x-X---X---X---X-x-X---X-x-", "----X-------X---g---X-------X-g-",
              "X-x-x-x-X-x-x-x-X-x-x-x-X-x-x-o-", "R-R-3-3-5-5-7-7-8-8-7-7-5-5-3-a-", "C-s-C-s-C-s-C-s-C-s-C-s-C-s-C-s-" } },
          { {12,38},{14,38},{15,38}, {8,48},{10,45} }, 42 },
        { "Ballad", 0.0f, 0.05f, 6.0f, {
            { "X---------------X-------X-------", "------------x-------------------",
              "x---x---x---x---x---x---x---x---", "R_______________R_______5_______", "C_______________C_______________" },
            { "X-------X-x-----X-------X-------", "----X-------X-------X-------X---",
              "x-x-x-x-x-x-x-x-x-x-x-x-x-x-x-x-", "R_______5_______R_______5___3___", "1_2_3_4_3_2_1_2_1_2_3_4_3_2_1_2_" },
            { "X-------X-x-----X-x-----X-------", "----X-------X-------X-------X--g",
              "x-x-x-x-x-x-x-o-x-x-x-x-x-x-x-o-", "R_____R_5_______R_____R_5___3_a_", "C_______c_______C_______c___s___" } },
          { {12,41},{14,45},{15,48}, {10,38} }, 42 },
    };
    return g[(style % 4 + 4) % 4];
}
} // namespace sj
