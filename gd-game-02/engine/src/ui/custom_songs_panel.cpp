// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The "Custom songs" overlay (owner §7). See custom_songs_panel.h.
#include "ui/custom_songs_panel.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "game/song_db.h"
#include "ui/natives.h"
#include "ui/port_draw.h"
#include "ui/ui_host.h"
#include "ui/ui_manager.h"

namespace ui {

namespace {
constexpr float kScreenW = 1280, kScreenH = 720;
constexpr float kRowH = 34;
using Color = std::array<float, 4>;

// Instrument markers for a song's SongInfo (GH2 has no drums/vocals; a foreign
// GH1 song is guitar only).
std::vector<std::pair<char, Color>> instrumentsOf(const SongInfo& s) {
    std::vector<std::pair<char, Color>> out;
    const Color g{0.3f, 1.f, 0.45f, 1}, b{0.45f, 0.75f, 1.f, 1}, d{1.f, 0.6f, 0.3f, 1}, v{0.85f, 0.55f, 1.f, 1};
    out.emplace_back('G', g);  // every song has a guitar part
    if (s.audio.tracks.count("bass")) out.emplace_back('B', b);
    if (s.audio.tracks.count("rhythm")) out.emplace_back('R', b);
    (void)d;
    (void)v;
    return out;
}
}  // namespace

class CustomSongsPanelObj : public PanelObj {
public:
    CustomSongsPanelObj(UiManager& ui, const std::string& name) : PanelObj(ui, name, "CustomSongsPanel"), d_(ui) {}

    std::string fileName() override { return ""; }
    bool drawsSomething() const override { return true; }
    bool claimsBack() const override { return false; }  // Back closes the screen (returns to Quick Play)

    void enter() override {
        build();
        cursor_ = 0;
        scroll_ = scrollTarget_ = 0;
        ui.host().menuBackdrop(true);
        PanelObj::enter();
    }
    void exit() override {
        ui.host().menuBackdrop(false);
        PanelObj::exit();
    }

    bool buttonDown(int button, int player) override {
        (void)player;
        if (visible_.empty()) return false;
        switch (button) {
        case kPad_DUp: move(-1); return true;
        case kPad_DDown: move(1); return true;
        case kPad_X: activate(); return true;        // green: expand/collapse or play
        case kPad_Square: nextFolder(); return true;  // blue: next folder
        case kPad_L1: ui.playSfx("button_error"); return true;  // orange: sort (not available)
        case kPad_Tri: return false;                  // red/back: let the screen go back to Quick Play
        default: return false;
        }
    }

    bool mouse(float x, float y, bool click) override {
        for (size_t v = 0; v < visible_.size(); ++v) {
            const float ry = listTop_ + float(v) * kRowH - scroll_;
            if (ry < listTop_ - kRowH || ry > listBottom_) continue;
            if (y >= ry && y < ry + kRowH && x >= boxX0_ && x <= boxX1_) {
                cursor_ = int(v);
                if (click) activate();
                return true;
            }
        }
        return false;
    }

    void poll(double dtMs) override {
        PanelObj::poll(dtMs);
        d_.updateTransition();
        keepVisible();
        scroll_ += (scrollTarget_ - scroll_) * std::min(1.0f, float(dtMs) / 60.f);
    }

    void draw() override {
        if (ui.headless()) return;
        const bool still = ui.host().drawMenuBackdrop();
        const float o = d_.offset();
        for (int pass : {0, 1}) {
            d_.begin(pass);
            if (pass == 0) d_.quad(o, 0, kScreenW + o, kScreenH, {0, 0, 0, still ? 0.72f : 1.f});
            drawAll(o);
        }
    }

private:
    struct Node {
        bool folder = false;
        std::string label;   // folder label
        int song = -1;       // index into host().songs()
        bool* expanded = nullptr;  // for a folder: points at expandedState_[label]
    };

    void build() {
        nodes_.clear();
        const auto& songs = ui.host().songs();
        // Group: shared (foreign) games first, by label; then this game.
        std::vector<std::string> order;      // group labels in display order
        std::map<std::string, std::vector<int>> groups;
        for (int i = 0; i < int(songs.size()); ++i) {
            if (!songs[size_t(i)].foreign()) continue;
            const std::string& g = songs[size_t(i)].foreignLabel;
            if (!groups.count(g)) order.push_back(g);
            groups[g].push_back(i);
        }
        const std::string self = "Guitar Hero II";  // this game, shown last
        for (int i = 0; i < int(songs.size()); ++i)
            if (!songs[size_t(i)].foreign()) {
                if (!groups.count(self)) order.push_back(self);
                groups[self].push_back(i);
            }
        for (const std::string& g : order) {
            expandedState_.try_emplace(g, false);  // folders start collapsed (spec)
            Node f;
            f.folder = true, f.label = g, f.expanded = &expandedState_[g];
            nodes_.push_back(f);
            for (int si : groups[g]) {
                Node s;
                s.folder = false, s.song = si, f.label.clear();
                s.label = g;  // its parent group (for visibility)
                nodes_.push_back(s);
            }
        }
        rebuildVisible();
    }

    void rebuildVisible() {
        visible_.clear();
        for (int i = 0; i < int(nodes_.size()); ++i) {
            const Node& n = nodes_[size_t(i)];
            if (n.folder) {
                visible_.push_back(i);
            } else if (expandedState_[n.label]) {
                visible_.push_back(i);
            }
        }
        cursor_ = std::clamp(cursor_, 0, std::max(0, int(visible_.size()) - 1));
    }

    const Node& node(int visIdx) const { return nodes_[size_t(visible_[size_t(visIdx)])]; }

    void move(int d) {
        cursor_ = std::clamp(cursor_ + d, 0, int(visible_.size()) - 1);
        ui.playSfx("button_toggle");
    }
    void nextFolder() {
        for (int v = cursor_ + 1; v < int(visible_.size()); ++v)
            if (node(v).folder) {
                cursor_ = v;
                ui.playSfx("button_toggle");
                return;
            }
    }
    void activate() {
        if (visible_.empty()) return;
        const Node& n = node(cursor_);
        if (n.folder) {
            expandedState_[n.label] = !expandedState_[n.label];
            ui.playSfx("button_select");
            rebuildVisible();
            return;
        }
        // A song: hand off to the difficulty screen, which runs the normal
        // Quick Play flow (loading -> game -> results -> back to Quick Play).
        ui.session().song = ui.host().songs()[size_t(n.song)].id;
        ui.playSfx("button_select");
        ui.gotoScreen("qp_diff_screen");
    }

    void keepVisible() {
        const float view = listBottom_ - listTop_;
        const float cy = float(cursor_) * kRowH;
        if (cy < scrollTarget_) scrollTarget_ = cy;
        if (cy + kRowH > scrollTarget_ + view) scrollTarget_ = cy + kRowH - view;
        const float content = float(visible_.size()) * kRowH;
        scrollTarget_ = std::clamp(scrollTarget_, 0.f, std::max(0.f, content - view));
    }

    void drawAll(float o) {
        const auto& songs = ui.host().songs();
        const float bx0 = boxX0_ + o, bx1 = boxX1_ + o;
        // The box + title.
        if (d_.pass() == 0) {
            d_.quad(bx0, boxY0_, bx1, boxY1_, PortDraw::kBox);
            d_.frame(bx0, boxY0_, bx1, boxY1_, 1.5f, PortDraw::kEdge);
            d_.quad(bx0, boxY0_, bx1, boxY0_ + 5, PortDraw::kAccent);
            d_.quad(bx0 + 24, searchY_, bx1 - 24, searchY_ + 34, {0.02f, 0.02f, 0.03f, 0.9f});
            d_.frame(bx0 + 24, searchY_, bx1 - 24, searchY_ + 34, 1.2f, PortDraw::kEdge);
        } else {
            d_.text("CUSTOM SONGS", (bx0 + bx1) / 2, boxY0_ + 34, 34, PortDraw::kTitle, 0x22);
            d_.text("Search:  (type to search - coming soon)", bx0 + 36, searchY_ + 17, 18, PortDraw::kDim, 0x12);
        }
        // The list, clipped to the viewport.
        for (size_t v = 0; v < visible_.size(); ++v) {
            const float ry = listTop_ + float(v) * kRowH - scroll_;
            if (ry + kRowH < listTop_ || ry > listBottom_) continue;
            const Node& n = node(int(v));
            const bool cur = int(v) == cursor_;
            if (d_.pass() == 0 && cur) d_.quad(bx0 + 10, ry, bx1 - 10, ry + kRowH, PortDraw::kFocusBar);
            if (d_.pass() != 1) continue;
            const float tx = bx0 + 28 + (n.folder ? 0 : 28);
            if (n.folder) {
                d_.text(expandedState_[n.label] ? "\xE2\x96\xBC" : "\xE2\x96\xB6", bx0 + 28, ry + kRowH / 2, 18, PortDraw::kGold, 0x12);
                const int count = folderCount(n.label);
                d_.text(n.label, tx + 24, ry + kRowH / 2, 22, cur ? PortDraw::kFocused : PortDraw::kGold, 0x12);
                d_.text(std::to_string(count) + " songs", bx1 - 28, ry + kRowH / 2, 16, PortDraw::kDim, 0x42);
            } else {
                const SongInfo& s = songs[size_t(n.song)];
                const std::string name = d_.fit(s.name, 20, bx1 - tx - 240);
                d_.text(name, tx, ry + kRowH / 2, 20, cur ? PortDraw::kFocused : PortDraw::kNormal, 0x12);
                if (!s.artist.empty())
                    d_.text(d_.fit(s.artist, 15, 220), bx1 - 150, ry + kRowH / 2, 15, PortDraw::kDim, 0x42);
                // Instrument markers.
                float ix = bx1 - 28;
                auto inst = instrumentsOf(s);
                for (auto it = inst.rbegin(); it != inst.rend(); ++it) {
                    d_.text(std::string(1, it->first), ix, ry + kRowH / 2, 16, it->second, 0x42);
                    ix -= 20;
                }
            }
        }
        // Footer controls.
        if (d_.pass() == 1) {
            const std::string help = "GREEN  select / open folder      RED  back      BLUE  next folder      ORANGE  sort (soon)";
            d_.text(help, (bx0 + bx1) / 2, boxY1_ - 20, 16, PortDraw::kDim, 0x22);
        }
    }

    int folderCount(const std::string& label) const {
        int n = 0;
        for (const Node& nd : nodes_)
            if (!nd.folder && nd.label == label) ++n;
        return n;
    }

    PortDraw d_;
    std::vector<Node> nodes_;
    std::vector<int> visible_;
    std::map<std::string, bool> expandedState_;
    int cursor_ = 0;
    float scroll_ = 0, scrollTarget_ = 0;
    // Layout (menu units).
    static constexpr float boxX0_ = 180, boxX1_ = 1100, boxY0_ = 70, boxY1_ = 660;
    static constexpr float searchY_ = 128;
    static constexpr float listTop_ = 180, listBottom_ = 620;
};

std::unique_ptr<ScriptObject> makeCustomSongsPanel(UiManager& ui, const std::string& name) {
    return std::make_unique<CustomSongsPanelObj>(ui, name);
}

}  // namespace ui
