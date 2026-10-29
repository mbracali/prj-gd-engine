// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "render/hud2d.h"

class Globals;
class Match;

// GH3's two-player HUDs, built from their tables the way create_2d_hud_elements
// builds them (setup_hud picks the table by game_mode: co-op ->
// coop_career_hud_2d_elements, face-off / pro face-off ->
// faceoff_hud_2d_elements, battle / boss -> battle_hud_2d_elements), and
// driven by UpdateScoreFastPerFrame's two-player branches
// (0x821167B0) [C data, C XEX; docs/modes.md "The two-player HUDs"].
// One instance draws both players' elements.
class Hud2P {
public:
    // false when the mode's table is missing.
    bool init(const Globals& g, const Match& m);
    void setText(Hud2D::TextFn text, Hud2D::WidthFn width) { text_ = std::move(text), width_ = std::move(width); }
    void update(const Match& m, double nowMs);
    void draw(Canvas& c, const TextureCache& tex, const Match& m, double nowMs) const;

    struct Counters {
        uint64_t needleChanges = 0, scoreChanges = 0, leadChanges = 0, cardsShown = 0, attackTexts = 0, raiseAxe = 0, missingTextures = 0;
    };
    const Counters& counters() const { return n_; }

private:
    struct El {
        uint32_t id = 0, parent = 0, tex = 0;
        float px = 0, py = 0, rot = 0;         // pos_off, rot (player 2: _p2 when given)
        bool hasP2Pos = false, hasP2Rot = false;
        float p2x = 0, p2y = 0, rot2 = 0;
        float z = 0, alpha = 1, jx = -1, jy = -1;
        float w = 0, h = 0, scale = 1;         // element_dims / dims, scale
        float bulb = 1;                        // small_bulb / big_bulb
        bool container = false, parentContainer = false, createOnce = false, noteBar = false, battlePos = false, tube = false;
        uint32_t posType = 0;
        bool flipH[2] = {false, false}, flipV[2] = {false, false};
        float tubeFillOff = 40, tubeFullAlpha = 0;
        bool big = false;
    };
    struct Inst {
        const El* el = nullptr;
        int player = 0;
        int parent = -1;  // index into inst_
    };
    struct PlayerState {
        int64_t score = -1;
        int run = -1, mult = 1, lights = 0;
        bool sp = false;
        float sp100 = 0;
        float needle = 0;
        int light = 1;  // 0 red, 1 yellow, 2 green
        float bgGreen = 0, bgYellow = 1;
        bool barShown = false;
        double barAt = -1e9;
        std::string scoreText;
    };
    bool lookup(const std::string& name, int player, float& x, float& y) const;
    // Numbered elements by id: 1 nixie (n = multiplier, sub 'a' / 'b'), 2..4
    // lights unlit / halflit / allwaylit (n 1..5), 5 tubes (n 1..6).
    struct Numbered {
        int kind = 0, n = 0;
        char sub = 0;
    };
    std::unordered_map<uint32_t, Numbered> numbered_;
    std::unordered_map<uint32_t, std::string> posNames_;
    float tableNumber(const char* name, float fallback) const;

    const Globals* g_ = nullptr;
    uint32_t table_ = 0;
    int mode_ = 0;  // 0 co-op, 1 face-off, 2 battle
    float scale_ = 0.75f, smallBulb_ = 0.7f, bigBulb_ = 1, frameWidth_ = 175;
    std::vector<El> els_;
    std::vector<Inst> inst_;
    std::array<PlayerState, 2> ps_;
    int lead_ = 0;  // face-off: -1 P1 ahead, 1 P2 ahead, 0 even
    float t1_ = 14, t2_ = 31.5f;
    Hud2D::TextFn text_;
    Hud2D::WidthFn width_;
    Counters n_;
    bool started_ = false;
};
