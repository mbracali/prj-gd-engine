// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/gamepads_screen.h"

#include <algorithm>

#include "platform/paths.h"
#include "render/canvas.h"

namespace {

using C = Input::Control;

constexpr Rgba kBg{18, 16, 22}, kPanel{30, 27, 36}, kRow{48, 42, 58}, kAccent{240, 140, 40}, kText{236, 228, 214},
    kDim{150, 142, 132}, kMuted{90, 84, 80}, kLit{120, 220, 120};
// The player boxes' colours (universal-menus.md: blue, green, red, orange).
constexpr Rgba kPlayerColor[4] = {{70, 130, 230}, {70, 190, 90}, {220, 70, 70}, {240, 150, 40}};
// The buttons' colours: Back red, Export yellow, Import green.
constexpr Rgba kBackColor{220, 70, 70}, kExportColor{230, 200, 60}, kImportColor{70, 190, 90};

constexpr float kBoxX = 16, kBoxW = 520, kBoxY = 52, kBoxH = 98, kMapX = 552, kMapW = 392;
constexpr int kMapRows = 13;  // visible mapping rows

const char* const kSections[] = {"General control bindings", "PSP buttons", "5 fret guitar bindings"};

}  // namespace

void GamepadsScreen::enter() {
    focus_ = 0, rfocus_ = 0, slot_ = 0, scroll_ = 0;
    editId_.clear();
    status_.clear();
}

void GamepadsScreen::exit() {
    input_.cancelCapture();
    input_.cancelDetect();
    if (input_.editing()) input_.discardEdit();  // leaving without Save discards
    editId_.clear();
}

std::vector<Input::Control> GamepadsScreen::mappingRows() const {
    std::vector<C> rows;
    if (editId_.empty()) return rows;
    const bool guitar = input_.instrument(editId_) == Input::Instrument::Guitar;
    for (int c = 0; c < int(C::Count); ++c) {
        const int sec = Input::controlSection(C(c));
        if (sec == 0 || (sec == 1 && !guitar) || (sec == 2 && guitar)) rows.push_back(C(c));
    }
    return rows;
}

void GamepadsScreen::finishEdit(bool save) {
    input_.cancelCapture();
    if (save) input_.commitEdit();
    else input_.discardEdit();
    status_ = save ? "Saved to controllers.json" : "Changes discarded";
    editId_.clear();
    focus_ = kCustomize;
}

bool GamepadsScreen::activateLeft(int f, int dir) {
    const std::string dev = input_.playerDevice(0);
    switch (f) {
    case kInstrument:
        if (dev == "keyboard") {
            status_ = "The keyboard plays the PSP layout";
        } else {
            const auto now = input_.instrument(dev);
            input_.setInstrument(dev, now == Input::Instrument::Guitar ? Input::Instrument::Gamepad : Input::Instrument::Guitar);
        }
        return true;
    case kBind:
        if (dir == 0) {
            input_.beginDetect(0);
            status_ = "Press a button on the device for Player 1 (Esc cancels)";
        } else {
            const auto devs = input_.devices();
            size_t i = 0;
            for (size_t k = 0; k < devs.size(); ++k)
                if (devs[k].id == dev) i = k;
            i = (i + devs.size() + size_t(dir)) % devs.size();
            input_.setPlayer(devs[i].id, 0);
        }
        return true;
    case kCustomize:
        editId_ = dev;
        input_.beginEdit(dev);
        rfocus_ = 0, slot_ = 0, scroll_ = 0;
        status_.clear();
        return true;
    case kBack:
        return false;
    case kExport: {
        const std::string file = prefDir() + "controllers-export.json";
        status_ = input_.exportTo(file) ? "Exported to " + file : "Could not write " + file;
        return true;
    }
    case kImport: {
        const std::string file = prefDir() + "controllers-export.json";
        status_ = input_.importFrom(file) ? "Imported " + file : "No valid " + file + " to import";
        return true;
    }
    default:
        return true;
    }
}

bool GamepadsScreen::activateRight(int f, int dir) {
    const auto rows = mappingRows();
    if (f < int(rows.size())) {
        if (dir) slot_ = std::clamp(slot_ + dir, 0, int(Input::kSlots) - 1);
        else input_.beginCapture(editId_, rows[size_t(f)], size_t(slot_));
        return true;
    }
    const int b = f - int(rows.size());
    if (dir) return true;
    if (b == 0) input_.resetBindings(editId_), status_ = "Defaults restored (Save keeps them)";
    else finishEdit(b == 1);
    return true;
}

bool GamepadsScreen::key(Key k) {
    if (input_.capturing() || input_.detecting()) {
        if (k == Key::Back) input_.cancelCapture(), input_.cancelDetect(), status_.clear();
        return true;
    }
    if (!editId_.empty()) {
        const int n = rightCount();
        if (k == Key::Up) rfocus_ = (rfocus_ + n - 1) % n;
        else if (k == Key::Down) rfocus_ = (rfocus_ + 1) % n;
        else if (k == Key::Left) activateRight(rfocus_, -1);
        else if (k == Key::Right) activateRight(rfocus_, 1);
        else if (k == Key::Confirm) activateRight(rfocus_, 0);
        else if (k == Key::Back) finishEdit(false);
        const int rows = int(mappingRows().size());
        if (rfocus_ < rows) {
            if (rfocus_ < scroll_) scroll_ = rfocus_;
            if (rfocus_ >= scroll_ + kMapRows) scroll_ = rfocus_ - kMapRows + 1;
        }
        return true;
    }
    if (k == Key::Up) focus_ = (focus_ + kLeftCount - 1) % kLeftCount;
    else if (k == Key::Down) focus_ = (focus_ + 1) % kLeftCount;
    else if (k == Key::Left || k == Key::Right) {
        if (focus_ <= kBind) activateLeft(focus_, k == Key::Left ? -1 : 1);
        else if (focus_ >= kBack) focus_ = std::clamp(focus_ + (k == Key::Left ? -1 : 1), int(kBack), int(kImport));
    } else if (k == Key::Confirm) return activateLeft(focus_, 0);
    else if (k == Key::Back) return false;
    return true;
}

bool GamepadsScreen::click(float x, float y) {
    for (const Hit& h : hits_) {
        if (x < h.x || x >= h.x + h.w || y < h.y || y >= h.y + h.h) continue;
        if (h.focus >= 100) {  // the mapping
            if (editId_.empty()) return true;
            rfocus_ = h.focus - 100;
            if (h.slot >= 0) slot_ = h.slot;
            return activateRight(rfocus_, 0);
        }
        if (!editId_.empty()) return true;  // locked until Save or Discard
        focus_ = h.focus;
        return activateLeft(focus_, focus_ == kInstrument || focus_ == kBind ? 1 : 0);
    }
    return true;
}

void GamepadsScreen::draw(Canvas& c) {
    hits_.clear();
    c.clear(kBg);
    c.text(16, 12, "GAMEPADS", kAccent, 3);
    const std::string dev = input_.playerDevice(0);
    const bool locked = !editId_.empty();
    std::string devName = "Keyboard";
    for (const Input::Device& d : input_.devices())
        if (d.id == dev) devName = d.name;

    // The four player boxes.
    for (int p = 0; p < 4; ++p) {
        const float y = kBoxY + float(p) * (kBoxH + 6);
        const bool on = p < Input::kPlayers;
        const Rgba col = on ? kPlayerColor[p] : kMuted;
        c.fill(kBoxX, y, kBoxW, kBoxH, kPanel);
        c.frame(kBoxX, y, kBoxW, kBoxH, col);
        c.fill(kBoxX + 10, y + 10, 104, 44, on ? col : kRow);
        c.text(kBoxX + 14, y + 26, "Player " + std::to_string(p + 1), on ? kBg : kMuted, 1.5f);
        if (!on) {
            c.text(kBoxX + 130, y + 26, "Unavailable (one player)", kMuted, 1.5f);
            continue;
        }
        auto field = [&](int f, float fy, const std::string& label, const std::string& value) {
            const bool sel = !locked && focus_ == f;
            if (sel) c.fill(kBoxX + 124, fy - 4, kBoxW - 132, 22, kRow);
            c.text(kBoxX + 130, fy + 2, label, locked ? kMuted : kDim, 1.5f);
            c.text(kBoxX + 268, fy + 2, "[" + value + " v]", sel ? kAccent : locked ? kMuted : kText, 1.5f);
            hits_.push_back({kBoxX + 124, fy - 4, kBoxW - 132, 22, f, -1});
        };
        field(kInstrument, y + 12, "Instrument", Input::instrumentName(input_.instrument(dev)));
        std::string bindTo = input_.detecting() ? "press a button..." : devName;
        if (bindTo.size() > 17) bindTo = bindTo.substr(0, 16) + ".";
        field(kBind, y + 38, "Bind to:", bindTo);
        const bool sel = !locked && focus_ == kCustomize;
        c.fill(kBoxX + 10, y + 66, kBoxW - 20, 24, sel ? kRow : kBg);
        c.frame(kBoxX + 10, y + 66, kBoxW - 20, 24, sel ? kAccent : kDim);
        const char* cust = "Customize inputs";
        c.text(kBoxX + kBoxW / 2 - Canvas::textWidth(cust, 2) / 2, y + 71, cust, sel ? kAccent : locked ? kMuted : kText, 2);
        hits_.push_back({kBoxX + 10, y + 66, kBoxW - 20, 24, kCustomize, -1});
    }
    // Back (red), Export (yellow), Import (green).
    const float by = kBoxY + 4 * (kBoxH + 6) + 2;
    const char* const kButtons[] = {"< Back", "Export", "Import"};
    const Rgba kColors[] = {kBackColor, kExportColor, kImportColor};
    for (int b = 0; b < 3; ++b) {
        const float bx = kBoxX + float(b) * 128;
        const bool sel = !locked && focus_ == kBack + b;
        c.fill(bx, by, 120, 28, sel ? kColors[b] : kPanel);
        c.frame(bx, by, 120, 28, locked ? kMuted : kColors[b]);
        c.text(bx + 60 - Canvas::textWidth(kButtons[b], 2) / 2, by + 7, kButtons[b], sel ? kBg : locked ? kMuted : kText, 2);
        hits_.push_back({bx, by, 120, 28, kBack + b, -1});
    }

    // INPUTS MAPPING.
    c.fill(kMapX, kBoxY, kMapW, 4 * (kBoxH + 6) + 30, kPanel);
    c.frame(kMapX, kBoxY, kMapW, 4 * (kBoxH + 6) + 30, kDim);
    const char* title = "INPUTS MAPPING";
    c.text(kMapX + kMapW / 2 - Canvas::textWidth(title, 2) / 2, kBoxY + 8, title, kAccent, 2);
    if (locked) {
        const auto rows = mappingRows();
        const auto& b = input_.bindings(editId_);
        float y = kBoxY + 34;
        int lastSec = -1;
        for (int i = scroll_; i < int(rows.size()) && i < scroll_ + kMapRows; ++i) {
            const C ctl = rows[size_t(i)];
            if (Input::controlSection(ctl) != lastSec) {
                lastSec = Input::controlSection(ctl);
                c.text(kMapX + 8, y + 2, kSections[lastSec], kDim, 1.5f);
                y += 16;
            }
            const bool sel = rfocus_ == i;
            if (sel) c.fill(kMapX + 4, y - 2, kMapW - 8, 22, kRow);
            const bool down = input_.controlDown(editId_, ctl);
            c.text(kMapX + 10, y + 3, Input::controlName(ctl), down ? kLit : sel ? kAccent : kText, 1.5f);
            for (int s = 0; s < int(Input::kSlots); ++s) {
                const float sx = kMapX + 160 + float(s) * 114;
                std::string v = s < int(b[size_t(ctl)].size()) ? input_.sourceName(editId_, b[size_t(ctl)][size_t(s)]) : "--";
                if (input_.capturingSlot(editId_, ctl, size_t(s))) v = "press...";
                if (v.size() > 9) v = v.substr(0, 8) + ".";
                const bool cell = sel && slot_ == s;
                c.frame(sx, y - 1, 110, 20, cell ? kAccent : kMuted);
                c.text(sx + 4, y + 3, v, cell ? kAccent : down ? kLit : kText, 1.5f);
                hits_.push_back({sx, y - 1, 110, 20, 100 + i, s});
            }
            y += 22;
        }
        // Reset, Save, Discard.
        const float fy = kBoxY + 4 * (kBoxH + 6) - 2;
        const char* const kEdit[] = {"Reset", "Save", "Discard"};
        for (int k = 0; k < 3; ++k) {
            const float bx = kMapX + 10 + float(k) * 126;
            const bool sel = rfocus_ == int(rows.size()) + k;
            c.fill(bx, fy, 116, 26, sel ? kAccent : kBg);
            c.frame(bx, fy, 116, 26, kDim);
            c.text(bx + 58 - Canvas::textWidth(kEdit[k], 2) / 2, fy + 6, kEdit[k], sel ? kBg : kText, 2);
            hits_.push_back({bx, fy, 116, 26, 100 + int(rows.size()) + k, -1});
        }
    } else {
        c.text(kMapX + 20, kBoxY + 60, "Customize inputs to see", kMuted, 2);
        c.text(kMapX + 20, kBoxY + 84, "and change the bindings.", kMuted, 2);
    }
    if (!status_.empty()) c.text(kMapX, by + 8, status_.size() > 48 ? status_.substr(0, 47) + "." : status_, kText, 1.5f);
    // The help bar.
    c.fill(0, Canvas::kH - 40, Canvas::kW, 40, kPanel);
    const char* help = locked ? "Up/Down control  Left/Right slot  Confirm bind  Del clear  Back discard"
                              : "Up/Down field  Left/Right change  Confirm select / detect  Back return";
    c.text(16, Canvas::kH - 26, help, kDim, 1.5f);
}
