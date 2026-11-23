// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The "Custom songs" overlay for GH1 (owner §7). See custom_songs_panel.h.
#include "ui/custom_songs_panel.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "game/song_db.h"
#include "render/renderer.h"

namespace {
constexpr float kScreenW = 1280, kScreenH = 720;
constexpr float kRowH = 32;
// GH1 menu buttons (UiSystem::Button): Up 12, Right 13, Down 14, Left 15.
constexpr int kUp = 12, kRight = 13, kDown = 14, kLeft = 15;
using Color = std::array<float, 4>;
constexpr Color kTitle{0.95f, 0.4f, 0.2f, 1}, kGold{1.f, 0.82f, 0.3f, 1}, kFocused{1, 1, 1, 1}, kNormal{0.86f, 0.84f, 0.8f, 1},
    kDim{0.55f, 0.55f, 0.58f, 1}, kBox{0, 0, 0, 0.66f}, kEdge{1, 1, 1, 0.14f}, kBar{0.96f, 0.76f, 0.26f, 0.18f},
    kAccent{0.95f, 0.4f, 0.2f, 1};

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

class CustomSongsPanel : public NativePanel {
public:
    explicit CustomSongsPanel(SelectHost& host) : host_(host) {}

    void enter() override {
        host_.menuBackdrop(true);
        view_ = View::Games;
        cursor_ = 0;
        scroll_ = scrollTarget_ = 0;
        build();
    }
    void exit() override { host_.menuBackdrop(false); }

    void poll(float dtMs) override {
        updateTransition();
        keepVisible();
        scroll_ += (scrollTarget_ - scroll_) * std::min(1.0f, dtMs / 60.f);
    }

    bool buttonDown(int button, int player) override {
        (void)player;
        if (host_.isBackButton(button)) return false;  // let the screen go back to Quick Play
        if (host_.isSelectButton(button)) return activate(), true;
        if (button == kUp) return move(-1), true;
        if (button == kDown) return move(1), true;
        if (button == kLeft) return cycleView(-1), true;
        if (button == kRight) return cycleView(1), true;
        return true;  // the overlay owns the rest while it is open
    }

    bool mouse(float x, float y, bool click) override {
        for (int v = 0; v < 3; ++v)
            if (x >= tabX_[v] && x < tabX_[v + 1] - 8 && y >= tabsY_ && y < tabsY_ + 28) {
                if (click && int(view_) != v) setView(View(v));
                return true;
            }
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

    void drawBefore() override {
        updateTransition();
        Renderer& r = host_.renderer();
        float fx = 0, fy = 0;
        if (!host_.drawMenuBackdrop(fx, fy))  // the still venue + band behind the overlay (black when none)
            r.fillRect(offset_, 0, kScreenW, kScreenH, {0, 0, 0, uint8_t(255 * fade_)});
        else
            r.fillRect(0, 0, kScreenW, kScreenH, {0, 0, 0, uint8_t(255 * (1 - fade_))});  // the transition fade only
    }
    void drawAfter() override { drawAll(); }

private:
    enum class View { Games = 0, Alphabetical = 1, Artist = 2 };
    struct Row {
        bool folder = false;
        std::string label;
        int song = -1;
    };

    void setView(View v) {
        view_ = v, cursor_ = 0, scroll_ = scrollTarget_ = 0;
        build();
    }
    void cycleView(int d) { setView(View((int(view_) + d + 3) % 3)); }

    void build() {
        rows_.clear();
        const auto& songs = host_.menuSongs();
        if (view_ == View::Games) {
            std::vector<std::string> order;
            std::map<std::string, std::vector<int>> groups;
            auto add = [&](bool foreign) {
                for (int i = 0; i < int(songs.size()); ++i)
                    if (songs[size_t(i)].foreign() == foreign) {
                        const std::string g = foreign ? songs[size_t(i)].foreignLabel : "Guitar Hero";
                        if (!groups.count(g)) order.push_back(g);
                        groups[g].push_back(i);
                    }
            };
            add(true);
            add(false);
            for (const std::string& g : order) {
                expanded_.try_emplace(g, false);
                rows_.push_back({true, g, -1});
                if (expanded_[g])
                    for (int si : groups[g]) rows_.push_back({false, g, si});
            }
        } else {
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

    void move(int d) { cursor_ = std::clamp(cursor_ + d, 0, int(rows_.size()) - 1); }
    void activate() {
        if (rows_.empty()) return;
        const Row& r = rows_[size_t(cursor_)];
        if (r.folder) {
            expanded_[r.label] = !expanded_[r.label];
            const std::string keep = r.label;
            build();
            for (int i = 0; i < int(rows_.size()); ++i)
                if (rows_[size_t(i)].folder && rows_[size_t(i)].label == keep) {
                    cursor_ = i;
                    break;
                }
            return;
        }
        // A song: choose it by engine index and go to the difficulty screen.
        host_.sendTo("game", "set_engine_song", {Value::integer(r.song)});
        host_.gotoScreen("qp_diff");
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
        for (const SongInfo& s : host_.menuSongs())
            if ((label == "Guitar Hero") ? !s.foreign() : (s.foreign() && s.foreignLabel == label)) ++n;
        return n;
    }

    void drawAll() {
        const auto& songs = host_.menuSongs();
        const float o = offset_;
        const float bx0 = boxX0_ + o, bx1 = boxX1_ + o;
        quad(bx0, boxY0_, bx1, boxY1_, kBox);
        frame(bx0, boxY0_, bx1, boxY1_, 1.5f, kEdge);
        quad(bx0, boxY0_, bx1, boxY0_ + 5, kAccent);
        text("rokk.font", "CUSTOM SONGS", (bx0 + bx1) / 2, boxY0_ + 28, 30, kTitle, 0x22);
        quad(bx0 + 24, searchY_, bx1 - 24, searchY_ + 30, {0.02f, 0.02f, 0.03f, 0.9f});
        frame(bx0 + 24, searchY_, bx1 - 24, searchY_ + 30, 1.2f, kEdge);
        text("impactor.font", "Search  (coming soon)", bx0 + 36, searchY_ + 15, 16, kDim, 0x21);
        static const char* kTabs[3] = {"GAME + FOLDERS", "ALPHABETICAL", "ARTIST"};
        for (int v = 0; v < 3; ++v) {
            const float tx0 = tabX_[v] + o, tx1 = tabX_[v + 1] - 8 + o;
            const bool on = int(view_) == v;
            quad(tx0, tabsY_, tx1, tabsY_ + 28, on ? kBar : Color{0.04f, 0.04f, 0.05f, 0.8f});
            frame(tx0, tabsY_, tx1, tabsY_ + 28, 1.2f, on ? kAccent : kEdge);
            text("impactor.font", kTabs[v], (tx0 + tx1) / 2, tabsY_ + 14, 15, on ? kFocused : kDim, 0x22);
        }
        const float nameX = boxX0_ + 54 + o;
        const float markRight = scrollX_ - 16 + o;
        for (int r = 0; r < int(rows_.size()); ++r) {
            const float ry = listTop_ + float(r) * kRowH - scroll_;
            if (ry + kRowH < listTop_ || ry > listBottom_) continue;
            const Row& row = rows_[size_t(r)];
            const bool cur = r == cursor_;
            if (cur) quad(bx0 + 10, ry + 1, scrollX_ - 6 + o, ry + kRowH - 1, kBar);
            const float midY = ry + kRowH / 2;  // middle-aligned text (0x21/0x24)
            if (row.folder) {
                text("impactor.font", expanded_[row.label] ? "v" : ">", bx0 + 30, midY, 18, kGold, 0x21);
                text("impactor.font", row.label, bx0 + 54, midY, 22, cur ? kFocused : kGold, 0x21);
                text("impactor.font", std::to_string(folderCount(row.label)) + " songs", markRight, midY, 15, kDim, 0x24);
            } else {
                const SongInfo& s = songs[size_t(row.song)];
                text("impactor.font", "G", markRight, midY, 15, {0.3f, 1.f, 0.45f, 1}, 0x24);
                const float artX = markRight - 30;
                const std::string name = fit("impactor.font", s.name, 19, artX - 220 - nameX);
                text("impactor.font", name, nameX, midY, 19, cur ? kFocused : kNormal, 0x21);
                if (!s.artist.empty()) text("impactor.font", fit("impactor.font", s.artist, 15, 210), artX, midY, 15, kDim, 0x24);
            }
        }
        // Scrollbar.
        const float view = listBottom_ - listTop_, content = float(rows_.size()) * kRowH;
        quad(scrollX_ + o, listTop_, scrollX_ + o + 8, listBottom_, {1, 1, 1, 0.06f});
        if (content > view) {
            const float th = std::max(30.f, view * view / content);
            const float ty = listTop_ + (view - th) * (scroll_ / (content - view));
            quad(scrollX_ + o, ty, scrollX_ + o + 8, ty + th, {0.8f, 0.72f, 0.5f, 0.6f});
        }
        text("impactor.font", "GREEN  select / open      RED  back      LEFT/RIGHT  change view", (bx0 + bx1) / 2, boxY1_ - 26, 15,
             kDim, 0x22);
    }

    // ---- GH1 port-draw helpers (as gamepad_panel.cpp) ----
    void updateTransition() {
        int phase = 0;
        float t = 1;
        bool back = false;
        host_.transition(phase, t, back);
        const float e = t * t * (3 - 2 * t);
        offset_ = 0, fade_ = 1;
        if (phase == 2) offset_ = (back ? -1.f : 1.f) * (1 - e) * kScreenW * 0.6f, fade_ = e;
        if (phase == 1) offset_ = (back ? 1.f : -1.f) * e * kScreenW * 0.6f, fade_ = 1 - e;
    }
    Color faded(Color c) const { return c[3] *= fade_, c; }
    void text(const char* font, const std::string& s, float x, float y, float h, const Color& c, uint32_t align) {
        host_.drawText(font, s, x, y, h, faded(c), align);
    }
    std::string fit(const char* font, std::string s, float h, float width) {
        if (host_.textWidth(font, s, h) <= width) return s;
        while (s.size() > 1 && host_.textWidth(font, s + "...", h) > width) s.pop_back();
        return s + "...";
    }
    void quad(float x0, float y0, float x1, float y1, const Color& c) {
        const Color f = faded(c);
        const uint8_t r = uint8_t(f[0] * 255), g = uint8_t(f[1] * 255), b = uint8_t(f[2] * 255), a = uint8_t(f[3] * 255);
        const Vertex v[6] = {{x0, y0, 0, 0, 0, r, g, b, a}, {x1, y0, 0, 0, 0, r, g, b, a}, {x1, y1, 0, 0, 0, r, g, b, a},
                             {x0, y0, 0, 0, 0, r, g, b, a}, {x1, y1, 0, 0, 0, r, g, b, a}, {x0, y1, 0, 0, 0, r, g, b, a}};
        host_.renderer().draw(v, 6, 0, Blend::Alpha);
    }
    void frame(float x0, float y0, float x1, float y1, float t, const Color& c) {
        quad(x0, y0, x1, y0 + t, c), quad(x0, y1 - t, x1, y1, c), quad(x0, y0 + t, x0 + t, y1 - t, c), quad(x1 - t, y0 + t, x1, y1 - t, c);
    }

    SelectHost& host_;
    View view_ = View::Games;
    std::vector<Row> rows_;
    std::map<std::string, bool> expanded_;
    int cursor_ = 0;
    float scroll_ = 0, scrollTarget_ = 0, offset_ = 0, fade_ = 1;
    static constexpr float boxX0_ = 180, boxX1_ = 1100, boxY0_ = 70, boxY1_ = 660;
    static constexpr float searchY_ = 118, tabsY_ = 160, listTop_ = 206, listBottom_ = 614, scrollX_ = 1066;
    static constexpr float tabX_[4] = {204, 500, 760, 1076};
};
constexpr float CustomSongsPanel::tabX_[4];

}  // namespace

std::unique_ptr<NativePanel> makeCustomSongsPanel(SelectHost& host) { return std::make_unique<CustomSongsPanel>(host); }
