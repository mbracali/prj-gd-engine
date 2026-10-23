// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <algorithm>
#include <bit>
#include <vector>

#include "game/player.h"

// Perfect guitar-mode input for a chart: press a gem's frets just before it,
// strum on time, and hold until the next gem. With `hammer`, HOPO-able gems are
// played as the original's watcher allows (FUN_002639b8): a hammer-on presses a
// fret above all held ones, a pull-off releases down to a fret already held,
// neither strums, and all frets are never let go in between. Used by
// --autoplay and the self-test.
class Autoplay {
public:
    // `player` >= 0: a face-off player, who plays only the gems it owns.
    explicit Autoplay(const Chart& chart, int player = -1, bool hammer = true) {
        const auto& gems = chart.gems;
        float lastStrum = -1e9f, prevHit = -1e9f;
        uint32_t held = 0;
        int prev = -1;
        auto top = [](uint32_t m) { return m ? 31 - std::countl_zero(m) : -1; };
        auto down = [&](float t, int f) { steps_.push_back({t, Step::Down, f}), held |= 1u << f; };
        // Lowest first: letting go of a fret below the top one tries no pull-off.
        auto release = [&](float t, uint32_t m) {
            for (int f = 0; f < 5; ++f)
                if (m & held & (1u << f)) steps_.push_back({t, Step::Up, f}), held &= ~(1u << f), t += 0.25f;
        };
        for (size_t i = 0; i < gems.size(); ++i) {
            const Gem& g = gems[i];
            if (player >= 0 && !g.ownedBy(player)) continue;
            const int f = g.fretCount() == 1 ? std::countr_zero(unsigned(g.frets)) : -1;
            // Inside the HOPO window (gem - 40 .. gem + 160 ms) and after the previous hit.
            const float ht = g.ms + 5;
            const bool chain = hammer && g.hopo && f >= 0 && prev == int(i) - 1 && held && prevHit + 1 < ht;
            if (chain && f > top(held)) {  // hammer-on
                down(ht, f);
                release(ht + 0.25f, held & ~g.frets);
                prevHit = ht;
            } else if (chain && (held & g.frets) && f < top(held)) {  // pull-off
                release(ht, held & ~((2u << f) - 1));  // everything above f, the top last
                release(ht + 2, held & ~g.frets);
                prevHit = ht;
            } else {
                // Let go of everything (no HOPO try), then press the gem's frets and strum.
                float t = std::max(g.ms - 1, prevHit + 0.5f);
                release(t, held);
                for (int k = 0; k < 5; ++k)
                    if (g.frets & (1u << k)) down(t + 1.5f, k);
                // Notes closer than the 52 ms strum debounce are strummed a little
                // late, as a player would, still well inside the hit window.
                lastStrum = std::max(std::max(g.ms, t + 2), lastStrum + 53);
                steps_.push_back({lastStrum, Step::Strum, -1});
                prevHit = lastStrum;
            }
            prev = int(i);
            // A pull-off next: hold its fret below this single note once it is hit.
            if (hammer && f >= 0 && i + 1 < gems.size()) {
                const Gem& n = gems[i + 1];
                if (n.hopo && n.fretCount() == 1 && (player < 0 || n.ownedBy(player)) && n.frets < g.frets &&
                    n.ms + 5 > prevHit + 3)
                    down(prevHit + 0.5f, std::countr_zero(unsigned(n.frets)));
            }
            // Held through the sustain's end (held_note_released 1.0); letting go
            // early would silence the guitar (UnmuteBeatMatchAudio_v18). The next
            // gem lets go itself; the last one is let go here.
            if (i + 1 == gems.size()) release(g.ms + std::max(g.durationMs + 1, 40.0f), held);
        }
        release(1e9f, held);
        std::stable_sort(steps_.begin(), steps_.end(), [](const Step& a, const Step& b) { return a.t < b.t; });
    }

    // Feeds every step up to time `t`, each at its own timestamp.
    void feed(Player& p, float t) {
        while (next_ < steps_.size() && steps_[next_].t <= t) {
            const Step& s = steps_[next_++];
            p.update(s.t);
            switch (s.kind) {
            case Step::Down: p.fretDown(s.fret, s.t); break;
            case Step::Up: p.fretUp(s.fret, s.t); break;
            case Step::Strum: p.strum(s.t); break;
            }
        }
    }

private:
    struct Step {
        float t;
        enum Kind { Down, Up, Strum } kind;
        int fret;
    };
    std::vector<Step> steps_;
    size_t next_ = 0;
};
