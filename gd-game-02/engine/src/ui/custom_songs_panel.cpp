// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The "Custom songs" overlay (owner §7). See custom_songs_panel.h.
#include "ui/custom_songs_panel.h"

#include <algorithm>
#include <cctype>
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
constexpr float kRowH = 32;
using Color = std::array<float, 4>;

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

// Instrument markers for a song (GH2 has no drums/vocals; a foreign GH1 song is
// guitar only). Drawn as coloured letters (the menu layer has no icon images).
std::vector<std::pair<char, Color>> instrumentsOf(const SongInfo& s) {
    std::vector<std::pair<char, Color>> out;
    const Color g{0.3f, 1.f, 0.45f, 1}, b{0.45f, 0.75f, 1.f, 1};
    out.emplace_back('G', g);  // every song has a guitar part
    if (s.audio.tracks.count("bass")) out.emplace_back('B', b);
    if (s.audio.tracks.count("rhythm")) out.emplace_back('R', b);
    return out;
}
}  // namespace

class CustomSongsPanelObj : public PanelObj {
public:
    CustomSongsPanelObj(UiManager& ui, const std::string& name) : PanelObj(ui, name, "CustomSongsPanel"), d_(ui) {}

    std::string fileName() override { return ""; }
    bool drawsSomething() const override { return true; }
    bool claimsBack() const override { return false; }  // Back returns to Quick Play

    void enter() override {
        view_ = View::Games;
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
        if (rows_.empty() && button != kPad_L1) return false;
        switch (button) {
        case kPad_DUp: move(-1); return true;
        case kPad_DDown: move(1); return true;
        case kPad_X: activate(); return true;         // green: open folder / play
        case kPad_Square: nextFolder(); return true;  // blue: next folder
        case kPad_L1: cycleView(1); return true;       // orange: change view
        case kPad_Tri: return false;                   // red/back: screen goes back to Quick Play
        default: return false;
        }
    }

    bool mouse(float x, float y, bool click) override {
        // View tabs.
        for (int v = 0; v < 3; ++v) {
            if (x >= tabX_[v] && x < tabX_[v + 1] - 8 && y >= tabsY_ && y < tabsY_ + 28) {
                if (click && int(view_) != v) setView(View(v));
                return true;
            }
        }
        // Rows.
        for (int r = 0; r < int(rows_.size()); ++r) {
            const float ry = listTop_ + float(r) * kRowH - scroll_;
            if (ry < listTop_ - kRowH || ry > listBottom_) continue;
            if (y >= ry && y < ry + kRowH && x >= boxX0_ && x <= scrollX_ - 6) {
                cursor_ = r;
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
        const bool still = ui.host().drawMenuBackdrop();  // the still venue + band behind
        const float o = d_.offset();
        for (int pass : {0, 1}) {
            d_.begin(pass);
            if (pass == 0) d_.quad(o, 0, kScreenW + o, kScreenH, {0, 0, 0, still ? 0.55f : 1.f});
            drawAll(o);
        }
    }

private:
    enum class View { Games = 0, Alphabetical = 1, Artist = 2 };
    struct Row {
        bool folder = false;
        std::string label;  // folder label, or the group a song belongs to
        int song = -1;       // index into host().songs()
    };

    void setView(View v) {
        view_ = v;
        cursor_ = 0;
        scroll_ = scrollTarget_ = 0;
        build();
        ui.playSfx("button_select");
    }
    void cycleView(int d) { setView(View((int(view_) + d + 3) % 3)); }

    void build() {
        rows_.clear();
        const auto& songs = ui.host().songs();
        if (view_ == View::Games) {
            // Per-game collapsible folders: shared games first, then this game.
            std::vector<std::string> order;
            std::map<std::string, std::vector<int>> groups;
            auto add = [&](bool foreign) {
                for (int i = 0; i < int(songs.size()); ++i)
                    if (songs[size_t(i)].foreign() == foreign) {
                        const std::string g = foreign ? songs[size_t(i)].foreignLabel : "Guitar Hero II";
                        if (!groups.count(g)) order.push_back(g);
                        groups[g].push_back(i);
                    }
            };
            add(true);
            add(false);
            for (const std::string& g : order) {
                expanded_.try_emplace(g, false);  // folders start collapsed (spec)
                rows_.push_back({true, g, -1});
                if (expanded_[g])
                    for (int si : groups[g]) rows_.push_back({false, g, si});
            }
        } else {
            // A flat list, sorted by name or by artist.
            std::vector<int> idx;
            for (int i = 0; i < int(songs.size()); ++i) idx.push_back(i);
            const bool byArtist = view_ == View::Artist;
            std::sort(idx.begin(), idx.end(), [&](int a, int b) {
                const SongInfo& x = songs[size_t(a)];
                const SongInfo& y = songs[size_t(b)];
                if (byArtist && lower(x.artist) != lower(y.artist)) return lower(x.artist) < lower(y.artist);
                return lower(x.name) < lower(y.name);
            });
            for (int i : idx) rows_.push_back({false, "", i});
        }
        cursor_ = std::clamp(cursor_, 0, std::max(0, int(rows_.size()) - 1));
    }

    void move(int d) {
        cursor_ = std::clamp(cursor_ + d, 0, int(rows_.size()) - 1);
        ui.playSfx("button_toggle");
    }
    void nextFolder() {
        for (int r = cursor_ + 1; r < int(rows_.size()); ++r)
            if (rows_[size_t(r)].folder) {
                cursor_ = r;
                ui.playSfx("button_toggle");
                return;
            }
    }
    void activate() {
        if (rows_.empty()) return;
        const Row& r = rows_[size_t(cursor_)];
        if (r.folder) {
            expanded_[r.label] = !expanded_[r.label];
            ui.playSfx("button_select");
            const std::string keep = r.label;
            build();
            for (int i = 0; i < int(rows_.size()); ++i)
                if (rows_[size_t(i)].folder && rows_[size_t(i)].label == keep) {
                    cursor_ = i;
                    break;
                }
            return;
        }
        // A song: hand off to the difficulty screen (the normal Quick Play flow).
        ui.session().song = ui.host().songs()[size_t(r.song)].id;
        ui.playSfx("button_select");
        ui.gotoScreen("qp_diff_screen");
    }

    void keepVisible() {
        const float view = listBottom_ - listTop_;
        const float cy = float(cursor_) * kRowH;
        if (cy < scrollTarget_) scrollTarget_ = cy;
        if (cy + kRowH > scrollTarget_ + view) scrollTarget_ = cy + kRowH - view;
        const float content = float(rows_.size()) * kRowH;
        scrollTarget_ = std::clamp(scrollTarget_, 0.f, std::max(0.f, content - view));
    }

    int folderCount(const std::string& label) const {
        int n = 0;
        for (const SongInfo& s : ui.host().songs())
            if ((label == "Guitar Hero II") ? !s.foreign() : (s.foreign() && s.foreignLabel == label)) ++n;
        return n;
    }

    void drawAll(float o) {
        const auto& songs = ui.host().songs();
        const bool p0 = d_.pass() == 0, p1 = d_.pass() == 1;
        const float bx0 = boxX0_ + o, bx1 = boxX1_ + o;
        // Box + title + search bar.
        if (p0) {
            d_.quad(bx0, boxY0_, bx1, boxY1_, PortDraw::kBox);
            d_.frame(bx0, boxY0_, bx1, boxY1_, 1.5f, PortDraw::kEdge);
            d_.quad(bx0, boxY0_, bx1, boxY0_ + 5, PortDraw::kAccent);
            d_.quad(bx0 + 24, searchY_, bx1 - 24, searchY_ + 30, {0.02f, 0.02f, 0.03f, 0.9f});
            d_.frame(bx0 + 24, searchY_, bx1 - 24, searchY_ + 30, 1.2f, PortDraw::kEdge);
        } else if (p1) {
            d_.text("CUSTOM SONGS", (bx0 + bx1) / 2, boxY0_ + 30, 32, PortDraw::kTitle, 0x22);
            d_.text("Search  (coming soon)", bx0 + 36, searchY_ + 15, 16, PortDraw::kDim, 0x21);
        }
        // View tabs below the search bar.
        static const char* kTabs[3] = {"GAME + FOLDERS", "ALPHABETICAL", "ARTIST"};
        for (int v = 0; v < 3; ++v) {
            const float tx0 = tabX_[v] + o, tx1 = tabX_[v + 1] - 8 + o;
            const bool on = int(view_) == v;
            if (p0) {
                d_.quad(tx0, tabsY_, tx1, tabsY_ + 28, on ? PortDraw::kFocusBar : Color{0.04f, 0.04f, 0.05f, 0.8f});
                d_.frame(tx0, tabsY_, tx1, tabsY_ + 28, 1.2f, on ? PortDraw::kAccent : PortDraw::kEdge);
            } else if (p1) {
                d_.text(kTabs[v], (tx0 + tx1) / 2, tabsY_ + 14, 15, on ? PortDraw::kFocused : PortDraw::kDim, 0x22);
            }
        }
        // The list (clipped between listTop_ and listBottom_).
        const float nameX = boxX0_ + 54 + o;         // songs
        const float markRight = scrollX_ - 16 + o;   // instrument markers' right edge
        for (int r = 0; r < int(rows_.size()); ++r) {
            const float ry = listTop_ + float(r) * kRowH - scroll_;
            if (ry + kRowH < listTop_ || ry > listBottom_) continue;
            const Row& row = rows_[size_t(r)];
            const bool cur = r == cursor_;
            const float midY = ry + kRowH / 2;
            if (p0 && cur) d_.quad(bx0 + 10, ry + 1, scrollX_ - 6 + o, ry + kRowH - 1, PortDraw::kFocusBar);
            if (!p1) continue;
            if (row.folder) {
                d_.text(expanded_[row.label] ? "v" : ">", bx0 + 30, midY, 16, PortDraw::kGold, 0x21);
                d_.text(row.label, bx0 + 54, midY, 22, cur ? PortDraw::kFocused : PortDraw::kGold, 0x21);
                d_.text(std::to_string(folderCount(row.label)) + " songs", markRight, midY, 15, PortDraw::kDim, 0x24);
            } else {
                const SongInfo& s = songs[size_t(row.song)];
                // Instrument markers on the right (drawn right to left).
                float ix = markRight;
                auto inst = instrumentsOf(s);
                for (auto it = inst.rbegin(); it != inst.rend(); ++it) {
                    d_.text(std::string(1, it->first), ix, midY, 15, it->second, 0x24);
                    ix -= 18;
                }
                const float artX = ix - 12;  // artist, right-aligned, left of the markers
                const float nameRight = artX - 220;
                const std::string name = d_.fit(s.name, 19, nameRight - nameX);
                d_.text(name, nameX, midY, 19, cur ? PortDraw::kFocused : PortDraw::kNormal, 0x21);
                if (!s.artist.empty())
                    d_.text(d_.fit(s.artist, 15, 210), artX, midY, 15, PortDraw::kDim, 0x24);
            }
        }
        // Vertical scrollbar.
        drawScrollbar(o);
        // Footer controls.
        if (p1)
            d_.text("GREEN  select / open      RED  back      BLUE  next folder      ORANGE  change view", (bx0 + bx1) / 2,
                    boxY1_ - 18, 15, PortDraw::kDim, 0x22);
    }

    void drawScrollbar(float o) {
        const float view = listBottom_ - listTop_;
        const float content = float(rows_.size()) * kRowH;
        const float tx = scrollX_ + o;
        if (d_.pass() == 0) {
            d_.quad(tx, listTop_, tx + 8, listBottom_, {1, 1, 1, 0.06f});
            if (content > view) {
                const float th = std::max(30.f, view * view / content);
                const float ty = listTop_ + (view - th) * (scroll_ / (content - view));
                d_.quad(tx, ty, tx + 8, ty + th, {0.8f, 0.72f, 0.5f, 0.6f});
            }
        }
    }

    PortDraw d_;
    View view_ = View::Games;
    std::vector<Row> rows_;
    std::map<std::string, bool> expanded_;
    int cursor_ = 0;
    float scroll_ = 0, scrollTarget_ = 0;
    // Layout (menu units).
    static constexpr float boxX0_ = 180, boxX1_ = 1100, boxY0_ = 70, boxY1_ = 660;
    static constexpr float searchY_ = 118;
    static constexpr float tabsY_ = 160;
    static constexpr float listTop_ = 206, listBottom_ = 622;
    static constexpr float scrollX_ = 1066;  // scrollbar x (inside the box)
    static constexpr float tabX_[4] = {204, 500, 760, 1076};  // tab column edges
};
constexpr float CustomSongsPanelObj::tabX_[4];

std::unique_ptr<ScriptObject> makeCustomSongsPanel(UiManager& ui, const std::string& name) {
    return std::make_unique<CustomSongsPanelObj>(ui, name);
}

}  // namespace ui
