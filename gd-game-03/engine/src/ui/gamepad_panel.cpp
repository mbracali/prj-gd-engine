// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The universal Gamepad menu (universal_mechanisms.md 1.2.1 at the
// repository root, its mockup as an ASCII layout there): gd01's
// (gd-game-01/engine/src/ui/gamepad_panel.cpp) with the same structure,
// rows, flow and mouse, drawn with PortDraw (Encore's menu font and colours;
// guidelines/universal_launcher.md).
#include "ui/gamepad_panel.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "input/input.h"
#include "render/canvas.h"
#include "ui/port_draw.h"
#include "ui/ui_host.h"
#include "ui/ui_manager.h"
#include "ui/ui_objects.h"

namespace ui {

namespace {

// [I] Not in the original game. Self-contained as gd01's: it only needs the
// UI manager (sounds, back, the host's file dialogs) and Input.
//
//   Left: four player boxes. "Player N" turns the player on or off (player 1
//   is always on; players 3 and 4 are listed but unavailable, Guitar Hero II
//   has two). Each box has an Instrument dropdown (what Encore supports: 5 fret
//   guitar, gamepad, generic MIDI), a "Bind to:" dropdown with every
//   connected device (the keyboard, each pad or guitar, each MIDI port) and
//   "Detect...", which binds the next device pressed, and "Customize inputs".
//   Below: Back, Export and Import (controllers.json to / from a file).
//
//   Right: INPUTS MAPPING, empty until "Customize inputs": the player's
//   device's bindings in the doc's sections (General control bindings, 5
//   fret guitar bindings; the doc's Rock Band, drum and vocal sections do not
//   apply to Encore), two slots per control, Require Strum, a pad's Controller
//   Type (gd03's: Encore's guitar layouts) and Reset, the touch screen's
//   settings for player 1 when touch is on, and Save / Discard at the
//   bottom. Everything on the left is locked until Save or Discard.
//
// Keys and pads: Up / Down move the focus, Left / Right change a dropdown's
// value (or a binding's slot), Select opens / presses, Back closes a
// dropdown, discards an edit, else leaves the screen. Mouse: every field and
// button clicks; a click outside an open dropdown closes it.
class GamepadPanelObj : public PanelObj {
public:
    GamepadPanelObj(UiManager& ui, const std::string& name) : PanelObj(ui, name, "GamepadPanel"), input_(&ui.input()), d_(ui) {}

    std::string fileName() override { return ""; }
    bool drawsSomething() const override { return true; }

    void enter() override {
        focus_ = 0, rfocus_ = 0, slot_ = 0, scroll_ = 0;
        dropdown_ = -1, editPlayer_ = -1;
        captureMs_ = 0;
        status_ = ui.host().enhancedStatus(), statusMs_ = 0;
        if (input_) input_->writeReport();
        PanelObj::enter();
    }
    void exit() override {
        if (input_) {
            input_->cancelCapture();
            input_->cancelDetect();
            if (input_->editing()) finishEdit(false, true);  // leaving without Save discards
            editPlayer_ = -1;
            input_->writeReport();  // with what was pressed here
        }
        PanelObj::exit();
    }

    bool buttonDown(int button, int player) override {
        (void)player;
        if (!input_) return false;
        if (input_->capturing() || input_->detecting()) return true;  // the devices wait while one is being bound
        const bool select = button == kPad_X, back = button == kPad_Tri;
        if (dropdown_ >= 0) return dropdownButton(button, select, back), true;
        if (editPlayer_ >= 0) return editButton(button, select, back), true;
        const auto items = leftItems();
        const int n = int(items.size());
        focus_ = std::clamp(focus_, 0, n - 1);
        if (button == kUp || button == kDown) {
            focus_ = (focus_ + (button == kDown ? 1 : n - 1)) % n;
            sound("button_toggle");
            return true;
        }
        const Item it = items[size_t(focus_)];
        if ((button == kLeft || button == kRight) && (it.kind == Field::Instrument || it.kind == Field::BindTo)) {
            stepField(it, button == kRight ? 1 : -1);
            return true;
        }
        if (select) return activate(it), true;
        return false;  // back: the screen's auto-back
    }

    bool mouse(float x, float y, bool click) override {
        if (!input_) return false;
        x -= offset_;
        if (input_->capturing() || input_->detecting()) {
            if (click) input_->cancelCapture(), input_->cancelDetect(), sound("button_back");
            return true;
        }
        if (dropdown_ >= 0) {  // its options, or a click anywhere else closes it
            const auto opts = options(dropItem_);
            for (size_t k = 0; k < opts.size(); ++k) {
                float y0 = ddY_ + float(k) * kDdRow;
                if (x < ddX0_ || x > ddX1_ || y < y0 || y > y0 + kDdRow) continue;
                if (int(k) != ddFocus_) ddFocus_ = int(k), sound("button_toggle");
                if (click) choose(dropItem_, opts[k]);
                return true;
            }
            if (click) dropdown_ = -1, sound("button_back");
            return true;
        }
        // The bottom buttons and the player boxes (locked while editing).
        const auto items = leftItems();
        for (size_t i = 0; i < items.size(); ++i) {
            float x0, y0, x1, y1;
            rect(items[i], x0, y0, x1, y1);
            if (x < x0 || x > x1 || y < y0 || y > y1) continue;
            if (editPlayer_ >= 0) {
                if (click) sound("button_error");
                return true;
            }
            if (int(i) != focus_) focus_ = int(i), sound("button_toggle");
            if (click) activate(items[i]);
            return true;
        }
        for (int p = kGamePlayers; p < kBoxes; ++p) {  // players 3 and 4: not in this game
            float bx0, by0, bx1, by1;
            boxRect(p, bx0, by0, bx1, by1);
            if (click && x >= bx0 && x <= bx1 && y >= by0 && y <= by1) return sound("button_error"), true;
        }
        if (editPlayer_ < 0) return false;
        const auto rows = rightRows();
        for (size_t r = 0; r < rows.size(); ++r) {
            const RRow& row = rows[r];
            if (row.kind == RKind::Heading) continue;
            float x0, y0, x1, y1;
            if (!rrect(int(r), x0, y0, x1, y1)) continue;
            if (x < x0 || x > x1 || y < y0 || y > y1) continue;
            int slot = slot_;
            if (row.kind == RKind::Binding) {
                if (x < kSlot1X0) continue;
                slot = x < kSlot2X0 ? 0 : 1;
            }
            if (int(r) != rfocus_ || slot != slot_) rfocus_ = int(r), slot_ = slot, sound("button_toggle");
            if (click) activateRow(row);
            return true;
        }
        return false;
    }

    void poll(double dtMsD) override {
        PanelObj::poll(dtMsD);
        const float dtMs = float(dtMsD);
        if (!input_) return;
        if (input_->devices().size() != reportedDevices_) {  // a controller came or went: a fresh report
            reportedDevices_ = input_->devices().size();
            input_->writeReport();
        }
        if (input_->capturing() || input_->detecting()) {
            captureMs_ += dtMs;
            if (captureMs_ > 10000) input_->cancelCapture(), input_->cancelDetect(), sound("button_back");
        } else {
            captureMs_ = 0;
        }
        if (editPlayer_ >= 0 && !input_->editing()) editPlayer_ = -1;  // e.g. the device went away
        std::string st = ui.host().enhancedStatus();
        if (st != status_) status_ = st, statusMs_ = st.empty() ? 0 : 5000;
        statusMs_ = std::max(0.0f, statusMs_ - dtMs);
    }

    void draw() override {
        if (!input_ || ui.headless()) return;
        d_.updateTransition();
        const PortDraw::FullFrame frame(ui);  // the 16:9 frame in either aspect
        offset_ = d_.offset();
        for (int pass : {0, 1}) {
            d_.begin(pass);
            if (pass == 0) d_.quad(0, 0, kScreenW, kScreenH, {0, 0, 0, 1});
            drawAll();
        }
        if (dropdown_ >= 0)  // over everything (its own quads, then its texts)
            for (int pass : {0, 1}) d_.begin(pass), drawDropdown();
    }

private:
    void drawAll() {
        const float o = offset_;
        text("rokk.font", "GAMEPADS", kLeftX0 + o, 12, 44, kTitle, 0x11);
        if (statusMs_ > 0) {
            auto c = kOn;
            c[3] = std::min(1.0f, statusMs_ / 500);
            text("impactor.font", fit(status_, 16, 300), kLeftX1 + o, 34, 16, c, 0x31);
        }
        const auto items = leftItems();
        for (int p = 0; p < kBoxes; ++p) drawBox(p, items);
        for (size_t i = 0; i < items.size(); ++i)
            if (items[i].kind == Field::Back || items[i].kind == Field::Export || items[i].kind == Field::Import) drawBottom(items[i], int(i) == focus_);
        drawRight();
    }

    // -- layout (1280 x 720 menu units)
    static constexpr int kUp = kPad_DUp, kDown = kPad_DDown, kLeft = kPad_DLeft, kRight = kPad_DRight;
    static constexpr int kBoxes = 4, kGamePlayers = Input::kPlayers;
    // Everything above the help bar (y 650 and below).
    static constexpr float kLeftX0 = 40, kLeftX1 = 520, kBoxTop = 62, kBoxH = 124, kBoxGap = 8;
    static constexpr float kRightX0 = 540, kRightX1 = 1240, kRightY0 = 20, kRightY1 = 642;
    static constexpr float kBottomY0 = 598, kBottomY1 = 634, kDdRow = 28;
    static constexpr float kRowH = 24, kRowsTop = 88, kRowsBottom = 586, kNameX = 566, kSlot1X0 = 830, kSlot2X0 = 1030, kSlotW = 190;
    // gd01's roles in Encore's colours (PortDraw): titles and headings red-orange,
    // the focus white on a gold bar, live presses green.
    static constexpr std::array<float, 4> kTitle = PortDraw::kTitle, kFocused = PortDraw::kFocused, kNormal = PortDraw::kNormal,
                                          kDim = PortDraw::kDim, kAccent = PortDraw::kAccent, kOn = PortDraw::kOn,
                                          kWarn = PortDraw::kWarn;
    // The mockup's box colours: player 1 blue, 2 green, 3 red, 4 orange.
    static constexpr std::array<std::array<float, 4>, 4> kPlayerColors = {
        std::array<float, 4>{0.3f, 0.55f, 1, 1}, {0.3f, 0.85f, 0.35f, 1}, {0.95f, 0.3f, 0.3f, 1}, {1, 0.6f, 0.15f, 1}};
    static constexpr std::array<std::array<float, 4>, 5> kFretColors = {
        std::array<float, 4>{0.2f, 0.85f, 0.25f, 1}, {0.9f, 0.2f, 0.2f, 1}, {0.95f, 0.85f, 0.2f, 1}, {0.25f, 0.45f, 0.95f, 1},
        {0.95f, 0.55f, 0.15f, 1}};

    // -- the left side's focusable fields
    enum class Field { Toggle, Instrument, BindTo, Customize, Back, Export, Import };
    struct Item {
        Field kind;
        int player = 0;
    };
    std::vector<Item> leftItems() const {
        std::vector<Item> v;
        for (int p = 0; p < kGamePlayers; ++p) {
            v.push_back({Field::Toggle, p});
            if (!input_->playerEnabled(p)) continue;
            v.push_back({Field::Instrument, p});
            v.push_back({Field::BindTo, p});
            v.push_back({Field::Customize, p});
        }
        v.push_back({Field::Back}), v.push_back({Field::Export}), v.push_back({Field::Import});
        return v;
    }
    static void boxRect(int p, float& x0, float& y0, float& x1, float& y1) {
        x0 = kLeftX0, x1 = kLeftX1, y0 = kBoxTop + float(p) * (kBoxH + kBoxGap), y1 = y0 + kBoxH;
    }
    static void rect(const Item& it, float& x0, float& y0, float& x1, float& y1) {
        float bx0, by0, bx1, by1;
        boxRect(it.player, bx0, by0, bx1, by1);
        x0 = bx0, y0 = by0, x1 = bx1, y1 = by1;
        switch (it.kind) {
        case Field::Toggle: x0 = bx0 + 10, y0 = by0 + 8, x1 = bx0 + 150, y1 = by0 + 80; return;
        case Field::Instrument: x0 = bx0 + 284, y0 = by0 + 10, x1 = bx1 - 12, y1 = by0 + 38; return;
        case Field::BindTo: x0 = bx0 + 284, y0 = by0 + 48, x1 = bx1 - 12, y1 = by0 + 76; return;
        case Field::Customize: x0 = bx0 + 10, y0 = by0 + 88, x1 = bx1 - 10, y1 = by0 + 116; return;
        case Field::Back: x0 = kLeftX0, x1 = kLeftX0 + 130; break;
        case Field::Export: x0 = (kLeftX0 + kLeftX1) * 0.5f - 65, x1 = x0 + 130; break;
        case Field::Import: x0 = kLeftX1 - 130, x1 = kLeftX1; break;
        }
        y0 = kBottomY0, y1 = kBottomY1;
    }

    // -- dropdown values
    struct Opt {
        std::string value, label;
        bool ok = true;
    };
    std::vector<Opt> options(const Item& it) const {
        std::vector<Opt> v;
        const std::string dev = input_->playerDevice(it.player);
        if (it.kind == Field::Instrument) {
            if (dev.empty()) return v;
            for (Input::Instrument i : input_->instruments(dev)) v.push_back({std::to_string(int(i)), Input::instrumentName(i)});
            return v;
        }
        v.push_back({"", "None"});
        for (const Input::Device& d : input_->devices())
            if (d.id != "touch") v.push_back({d.id, deviceLabel(d)});
        if (!input_->midiAvailable()) v.push_back({"midi", "MIDI: " + input_->midiBackend(), false});
        v.push_back({"detect", "Detect (press a button)..."});
        return v;
    }
    static std::string deviceLabel(const Input::Device& d) {
        if (d.id.rfind("midi:", 0) == 0) return "MIDI: " + d.name;
        return d.name;
    }
    std::string value(const Item& it) const {
        const std::string dev = input_->playerDevice(it.player);
        if (it.kind == Field::Instrument) return dev.empty() ? "" : std::to_string(int(input_->instrument(dev)));
        return dev;
    }
    std::string label(const Item& it) const {
        const std::string v = value(it);
        for (const Opt& o : options(it))
            if (o.value == v) return o.label;
        return it.kind == Field::Instrument ? "--" : "None";
    }
    void choose(const Item& it, const Opt& o) {
        dropdown_ = -1;
        if (!o.ok) return sound("button_error");
        if (it.kind == Field::Instrument) {
            input_->setInstrument(input_->playerDevice(it.player), Input::Instrument(std::atoi(o.value.c_str())));
        } else if (o.value == "detect") {
            input_->beginDetect(it.player);
            captureMs_ = 0;
        } else if (o.value.empty()) {
            if (!input_->playerDevice(it.player).empty()) input_->setPlayer(input_->playerDevice(it.player), -1);
        } else {
            input_->setPlayer(o.value, it.player);
        }
        sound("button_select");
    }
    // Left / Right on a closed dropdown: the next usable value (not Detect).
    void stepField(const Item& it, int dir) {
        auto opts = options(it);
        opts.erase(std::remove_if(opts.begin(), opts.end(), [](const Opt& o) { return !o.ok || o.value == "detect"; }), opts.end());
        if (opts.size() < 2) return sound("button_error");
        int i = 0;
        for (size_t k = 0; k < opts.size(); ++k)
            if (opts[k].value == value(it)) i = int(k);
        choose(it, opts[size_t((i + dir + int(opts.size())) % int(opts.size()))]);
    }
    void openDropdown(const Item& it) {
        const auto opts = options(it);
        if (opts.empty()) return sound("button_error");
        float x0, y0, x1, y1;
        rect(it, x0, y0, x1, y1);
        dropItem_ = it, dropdown_ = 1;
        ddX0_ = x0, ddX1_ = std::max(x1, x0 + 260);
        const float h = float(opts.size()) * kDdRow;
        ddY_ = y1 + h + 4 < kScreenH - 8 ? y1 + 2 : y0 - h - 2;  // below, or above when there is no room
        ddFocus_ = 0;
        for (size_t k = 0; k < opts.size(); ++k)
            if (opts[k].value == value(it)) ddFocus_ = int(k);
        sound("button_select");
    }
    void dropdownButton(int button, bool select, bool back) {
        const auto opts = options(dropItem_);
        const int n = int(opts.size());
        if (n == 0 || back) {
            dropdown_ = -1;
            return sound("button_back");
        }
        if (button == kUp || button == kDown) ddFocus_ = (ddFocus_ + (button == kDown ? 1 : n - 1)) % n, sound("button_toggle");
        else if (select) choose(dropItem_, opts[size_t(std::clamp(ddFocus_, 0, n - 1))]);
    }

    void activate(const Item& it) {
        switch (it.kind) {
        case Field::Toggle:
            if (it.player == 0) return sound("button_error");  // player 1 is always on
            input_->setPlayerEnabled(it.player, !input_->playerEnabled(it.player));
            return sound("button_select");
        case Field::Instrument:
        case Field::BindTo: return openDropdown(it);
        case Field::Customize: {
            const std::string dev = input_->playerDevice(it.player);
            if (dev.empty()) return sound("button_error");
            input_->beginEdit(dev);
            editPlayer_ = it.player, editDevice_ = dev, editType_ = input_->deviceType(dev);
            rfocus_ = 1, slot_ = 0, scroll_ = 0;
            return sound("button_select");
        }
        case Field::Back:  // as the back button (the screen's auto-back)
            ui.buttonDown(kPad_Tri, 0);
            ui.buttonUp(kPad_Tri, 0);
            return;
        case Field::Export: sound("button_select"); return ui.host().enhancedAction("export_controllers");
        case Field::Import: sound("button_select"); return ui.host().enhancedAction("import_controllers");
        }
    }

    // -- the right side (Customize inputs)
    enum class RKind { Heading, Binding, RequireStrum, Type, Reset, Touch, Save, Discard };
    struct RRow {
        RKind kind;
        std::string text;
        int index = 0;  // the control, or the touch option
    };
    std::vector<RRow> rightRows() const {
        std::vector<RRow> v;
        using C = Input::Control;
        v.push_back({RKind::Heading, "GENERAL CONTROL BINDINGS"});
        for (C c : {C::Start, C::Select, C::MenuUp, C::MenuDown}) v.push_back({RKind::Binding, Input::controlName(c), int(c)});
        // 5 fret guitar: every Encore instrument plays the guitar part (a gamepad too).
        v.push_back({RKind::Heading, "5 FRET GUITAR BINDINGS"});
        for (C c : {C::GuitarStart, C::GuitarSelect, C::StrumUp, C::StrumDown, C::Green, C::Red, C::Yellow, C::Blue, C::Orange,
                    C::Whammy, C::Gyro})
            v.push_back({RKind::Binding, Input::controlName(c), int(c)});
        v.push_back({RKind::Heading, "OPTIONS"});
        v.push_back({RKind::RequireStrum, "Require Strum"});
        // gd03's own: a pad's controller type (Encore's guitar layouts (inherited from gd02): Xbox 360,
        // old Xbox, PS2 adapter, Rock Band, gamepad), as before 0.0.2.
        if (editDevice_.rfind("pad:", 0) == 0) v.push_back({RKind::Type, "Controller Type"});
        v.push_back({RKind::Reset, "Reset to Defaults"});
        if (editPlayer_ == 0 && input_->touch()) {  // the touch screen plays as player 1
            v.push_back({RKind::Heading, "TOUCH SCREEN (PLAYER 1)"});
            for (int i = 0; i < Input::kTouchOptions; ++i) v.push_back({RKind::Touch, Input::touchOptionName(i), i});
        }
        v.push_back({RKind::Save, "Save"});
        v.push_back({RKind::Discard, "Discard"});
        return v;
    }
    // A row's rectangle (Save / Discard sit at the bottom); false: scrolled out of view.
    bool rrect(int r, float& x0, float& y0, float& x1, float& y1) const {
        const auto rows = rightRows();
        const RRow& row = rows[size_t(r)];
        if (row.kind == RKind::Save || row.kind == RKind::Discard) {
            const bool save = row.kind == RKind::Save;
            x0 = save ? kRightX1 - 300 : kRightX1 - 150, x1 = x0 + 130, y0 = kBottomY0, y1 = kBottomY1 - 4;
            return true;
        }
        y0 = kRowsTop + float(r) * kRowH - scroll_, y1 = y0 + kRowH - 2;
        x0 = kRightX0 + 12, x1 = kRightX1 - 12;
        return y0 >= kRowsTop - 1 && y1 <= kRowsBottom + 1;
    }
    void keepInView() {
        const auto rows = rightRows();
        const RRow& row = rows[size_t(rfocus_)];
        if (row.kind == RKind::Save || row.kind == RKind::Discard) return;
        float top = float(rfocus_) * kRowH, room = kRowsBottom - kRowsTop;
        if (top - (rfocus_ > 0 && rows[size_t(rfocus_ - 1)].kind == RKind::Heading ? kRowH : 0) < scroll_)
            scroll_ = std::max(0.0f, top - (rfocus_ > 0 && rows[size_t(rfocus_ - 1)].kind == RKind::Heading ? kRowH : 0));
        if (top + kRowH > scroll_ + room) scroll_ = top + kRowH - room;
    }
    void editButton(int button, bool select, bool back) {
        const auto rows = rightRows();
        const int n = int(rows.size());
        rfocus_ = std::clamp(rfocus_, 0, n - 1);
        if (back) {
            finishEdit(false);
            return;
        }
        if (button == kUp || button == kDown) {
            for (int k = 0; k < n; ++k) {
                rfocus_ = (rfocus_ + (button == kDown ? 1 : n - 1)) % n;
                if (rows[size_t(rfocus_)].kind != RKind::Heading) break;
            }
            keepInView();
            return sound("button_toggle");
        }
        const RRow& row = rows[size_t(rfocus_)];
        if (button == kLeft || button == kRight) {
            if (row.kind == RKind::Binding) slot_ = button == kRight ? 1 : 0, sound("button_toggle");
            else if (row.kind == RKind::Touch) input_->cycleTouchOption(row.index, button == kRight ? 1 : -1), sound("button_select");
            else if (row.kind == RKind::RequireStrum) input_->setRequireStrum(editDevice_, button == kRight), sound("button_select");
            else if (row.kind == RKind::Type) input_->cycleDeviceType(editDevice_, button == kRight ? 1 : -1), sound("button_select");
            else if (row.kind == RKind::Save || row.kind == RKind::Discard) rfocus_ = button == kRight ? n - 1 : n - 2, sound("button_toggle");
            return;
        }
        if (select) activateRow(row);
    }
    void activateRow(const RRow& row) {
        switch (row.kind) {
        case RKind::Heading: return;
        case RKind::Binding:
            input_->beginCapture(editDevice_, Input::Control(row.index), size_t(slot_));
            captureMs_ = 0;
            return sound("button_select");
        case RKind::RequireStrum:
            input_->setRequireStrum(editDevice_, !input_->requireStrum(editDevice_));
            return sound("button_select");
        case RKind::Type: input_->cycleDeviceType(editDevice_, 1); return sound("button_select");
        case RKind::Reset: input_->resetBindings(editDevice_); return sound("button_select");
        case RKind::Touch: input_->cycleTouchOption(row.index, 1); return sound("button_select");
        case RKind::Save: return finishEdit(true);
        case RKind::Discard: return finishEdit(false);
        }
    }
    void finishEdit(bool save, bool quiet = false) {
        if (save) {
            input_->commitEdit();
            if (!quiet) sound("button_select");
        } else {
            // The controller type chosen during the edit goes back too (Auto + 5 types).
            for (int k = 0; k < 6 && editDevice_.rfind("pad:", 0) == 0 && input_->deviceType(editDevice_) != editType_; ++k)
                input_->cycleDeviceType(editDevice_, 1);
            input_->discardEdit();
            if (!quiet) sound("button_back");
        }
        editPlayer_ = -1;
    }

    // -- drawing
    void drawBox(int p, const std::vector<Item>& items) {
        const float o = offset_;
        float x0, y0, x1, y1;
        boxRect(p, x0, y0, x1, y1);
        const bool available = p < kGamePlayers, on = available && input_->playerEnabled(p), locked = editPlayer_ >= 0 && editPlayer_ != p;
        auto pc = kPlayerColors[size_t(p)];
        if (!on) pc[3] = 0.35f;
        if (editPlayer_ == p) quad(x0 + o, y0, x1 + o, y1, {pc[0], pc[1], pc[2], 0.1f});
        frame(x0 + o, y0, x1 + o, y1, 2, pc);
        auto focusOf = [&](Field f) {
            for (size_t i = 0; i < items.size(); ++i)
                if (items[i].kind == f && items[i].player == p) return editPlayer_ < 0 && int(i) == focus_;
            return false;
        };
        // "Player N": the on / off switch.
        float tx0, ty0, tx1, ty1;
        rect({Field::Toggle, p}, tx0, ty0, tx1, ty1);
        const bool tf = focusOf(Field::Toggle);
        if (tf) quad(tx0 + o, ty0, tx1 + o, ty1, {1, 0.86f, 0.2f, 0.16f});
        frame(tx0 + o, ty0, tx1 + o, ty1, 3, on ? (tf ? kFocused : kTitle) : kDim);
        text("impactor.font", "Player " + std::to_string(p + 1), (tx0 + tx1) * 0.5f + o, (ty0 + ty1) * 0.5f - 6, 30,
             on ? (tf ? kFocused : kTitle) : kDim, 0x22);
        text("impactor.font", !available ? "NOT IN THIS GAME" : on ? (p == 0 ? "ALWAYS ON" : "ON") : "OFF", (tx0 + tx1) * 0.5f + o,
             ty1 - 13, 13, on ? kOn : kDim, 0x22);
        if (!available) {
            text("impactor.font", "Guitar Hero II has 2 players: players 3 and 4", x0 + 166 + o, y0 + 34, 16, kDim, 0x21);
            text("impactor.font", "are not available in this game.", x0 + 166 + o, y0 + 56, 16, kDim, 0x21);
            warning(x0 + 166 + o, y0 + 96);
            text("impactor.font", "Not available", x0 + 194 + o, y0 + 97, 16, kWarn, 0x21);
            return;
        }
        const std::array<float, 4> lab = on && !locked ? kNormal : kDim;
        text("impactor.font", "Instrument", x0 + 166 + o, y0 + 25, 19, lab, 0x21);
        text("impactor.font", "Bind to:", x0 + 166 + o, y0 + 63, 19, lab, 0x21);
        for (Field f : {Field::Instrument, Field::BindTo}) {
            float fx0, fy0, fx1, fy1;
            rect({f, p}, fx0, fy0, fx1, fy1);
            const bool ff = focusOf(f);
            const bool detecting = f == Field::BindTo && input_->detecting();
            std::string v = !on ? "--" : detecting ? "Press a button..." : label({f, p});
            dropField(fx0 + o, fy0, fx1 + o, fy1, fit(v, 16, fx1 - fx0 - 34), ff, !on || locked);
        }
        float cx0, cy0, cx1, cy1;
        rect({Field::Customize, p}, cx0, cy0, cx1, cy1);
        const bool cf = focusOf(Field::Customize), canEdit = on && !input_->playerDevice(p).empty();
        button(cx0 + o, cy0, cx1 + o, cy1, editPlayer_ == p ? "Customizing inputs..." : "Customize inputs", cf, !canEdit || locked);
    }
    void drawBottom(const Item& it, bool f) {
        const float o = offset_;
        float x0, y0, x1, y1;
        rect(it, x0, y0, x1, y1);
        // The mockup's colours: Back red, Export yellow, Import green.
        const std::array<float, 4> c = it.kind == Field::Back ? std::array<float, 4>{1, 0.45f, 0.45f, 1}
                                       : it.kind == Field::Export ? std::array<float, 4>{1, 0.86f, 0.35f, 1}
                                                                  : std::array<float, 4>{0.45f, 0.95f, 0.5f, 1};
        const bool locked = editPlayer_ >= 0;
        quad(x0 + o, y0, x1 + o, y1, {c[0], c[1], c[2], locked ? 0.08f : f ? 0.4f : 0.2f});
        frame(x0 + o, y0, x1 + o, y1, f && !locked ? 3.0f : 1.5f, locked ? kDim : c);
        const char* t = it.kind == Field::Back ? "< Back" : it.kind == Field::Export ? "Export" : "Import";
        text("impactor.font", t, (x0 + x1) * 0.5f + o, (y0 + y1) * 0.5f + 1, 21, locked ? kDim : f ? kFocused : kTitle, 0x22);
    }
    void drawRight() {
        const float o = offset_;
        frame(kRightX0 + o, kRightY0, kRightX1 + o, kRightY1, 2, {1, 1, 1, 0.5f});
        quad(kRightX0 + o, kRightY0, kRightX1 + o, kRightY1, {1, 1, 1, 0.03f});
        if (editPlayer_ < 0) {
            text("rokk.font", "INPUTS MAPPING", (kRightX0 + kRightX1) * 0.5f + o, 300, 46, kDim, 0x22);
            text("impactor.font", "Choose \"Customize inputs\" on a player to see and change its buttons.", (kRightX0 + kRightX1) * 0.5f + o,
                 352, 18, kDim, 0x22);
            text("impactor.font", "Report for bug reports: " + fit(input_->reportPath(), 14, 480), (kRightX0 + kRightX1) * 0.5f + o, 612,
                 14, kDim, 0x22);
            return;
        }
        std::string dev;
        for (const Input::Device& d : input_->devices())
            if (d.id == editDevice_) dev = deviceLabel(d);
        text("impactor.font", "INPUTS MAPPING: PLAYER " + std::to_string(editPlayer_ + 1) + " - " + fit(dev, 22, 420), kRightX0 + 20 + o, 50,
             22, kAccent, 0x21);
        text("impactor.font", "KEY 1", kSlot1X0 + kSlotW * 0.5f + o, 78, 14, kDim, 0x22);
        text("impactor.font", "KEY 2", kSlot2X0 + kSlotW * 0.5f + o, 78, 14, kDim, 0x22);
        const auto rows = rightRows();
        const Input::Bindings& b = input_->bindings(editDevice_);
        for (size_t r = 0; r < rows.size(); ++r) {
            const RRow& row = rows[r];
            float x0, y0, x1, y1;
            if (!rrect(int(r), x0, y0, x1, y1)) continue;
            const bool f = int(r) == rfocus_;
            const float cy = (y0 + y1) * 0.5f + 1;
            switch (row.kind) {
            case RKind::Heading:
                text("impactor.font", row.text, x0 + o, cy, 18, kAccent, 0x21);
                break;
            case RKind::Binding: {
                const auto c = Input::Control(row.index);
                const bool live = input_->controlDown(editDevice_, c);
                if (live) quad(x0 + o, y0, x1 + o, y1, {0.3f, 1, 0.35f, 0.2f});
                const int fret = row.index - int(Input::Control::Green);
                if (fret >= 0 && fret < 5) quad(x0 + 6 + o, y0 + 6, x0 + 14 + o, y1 - 6, kFretColors[size_t(fret)]);
                text("impactor.font", row.text, kNameX + o, cy, 18, f ? kFocused : kNormal, 0x21);
                for (size_t s = 0; s < Input::kSlots; ++s) {
                    const float sx0 = s == 0 ? kSlot1X0 : kSlot2X0;
                    const bool sf = f && int(s) == slot_, cap = input_->capturingSlot(editDevice_, c, s);
                    std::string v = cap ? "Press... (Esc, Del)" : s < b[size_t(row.index)].size() ? input_->sourceName(editDevice_, b[size_t(row.index)][s]) : "--";
                    if (sf) quad(sx0 + o, y0 + 1, sx0 + kSlotW + o, y1 - 1, {1, 0.86f, 0.2f, 0.18f});
                    frame(sx0 + o, y0 + 1, sx0 + kSlotW + o, y1 - 1, 1.5f, sf ? kFocused : kDim);
                    text("impactor.font", fit(v, 15, kSlotW - 12), sx0 + kSlotW * 0.5f + o, cy, 15, cap ? kAccent : live ? kOn : sf ? kFocused : kNormal, 0x22);
                }
                break;
            }
            case RKind::RequireStrum: {
                if (f) quad(x0 + o, y0, x1 + o, y1, {1, 0.86f, 0.2f, 0.14f});
                text("impactor.font", row.text, kNameX + o, cy, 18, f ? kFocused : kNormal, 0x21);
                check(kSlot1X0 + o, cy, input_->requireStrum(editDevice_), f);
                text("impactor.font", "Off: a fret press also strums", kSlot1X0 + 34 + o, cy, 15, kDim, 0x21);
                break;
            }
            case RKind::Type: {
                if (f) quad(x0 + o, y0, x1 + o, y1, {1, 0.86f, 0.2f, 0.14f});
                text("impactor.font", row.text, kNameX + o, cy, 18, f ? kFocused : kNormal, 0x21);
                const std::string t = input_->deviceType(editDevice_);
                text("impactor.font", fit(f ? "< " + t + " >" : t, 15, kSlotW * 2), kSlot1X0 + o, cy, 15, f ? kFocused : kOn, 0x21);
                break;
            }
            case RKind::Reset:
                button(kSlot1X0 + o, y0 + 1, kSlot1X0 + kSlotW + o, y1 - 1, row.text, f, false);
                break;
            case RKind::Touch: {
                if (f) quad(x0 + o, y0, x1 + o, y1, {1, 0.86f, 0.2f, 0.14f});
                text("impactor.font", row.text, kNameX + o, cy, 17, f ? kFocused : kNormal, 0x21);
                const std::string v = input_->touchOptionValue(row.index);
                text("impactor.font", f ? "< " + v + " >" : v, kSlot2X0 + o, cy, 16, f ? kFocused : kOn, 0x22);
                break;
            }
            case RKind::Save:
            case RKind::Discard: {
                const bool save = row.kind == RKind::Save;
                const std::array<float, 4> c = save ? std::array<float, 4>{0.45f, 0.95f, 0.5f, 1} : std::array<float, 4>{1, 0.45f, 0.45f, 1};
                quad(x0 + o, y0, x1 + o, y1, {c[0], c[1], c[2], f ? 0.4f : 0.2f});
                frame(x0 + o, y0, x1 + o, y1, f ? 3.0f : 1.5f, c);
                text("impactor.font", row.text, (x0 + x1) * 0.5f + o, cy, 21, f ? kFocused : kTitle, 0x22);
                break;
            }
            }
        }
        // More rows above / below.
        float content = 0;
        for (const RRow& row : rows)
            if (row.kind != RKind::Save && row.kind != RKind::Discard) content += kRowH;
        if (content > kRowsBottom - kRowsTop) {
            const float tx = kRightX1 - 8 + o, h = kRowsBottom - kRowsTop, thumb = h * h / content;
            quad(tx - 2, kRowsTop, tx + 2, kRowsBottom, {1, 1, 1, 0.12f});
            const float t0 = kRowsTop + (h - thumb) * std::clamp(scroll_ / std::max(1.0f, content - h), 0.0f, 1.0f);
            quad(tx - 3, t0, tx + 3, t0 + thumb, {1, 1, 1, 0.45f});
        }
        text("impactor.font", "Choose a slot, then press the new button, key or MIDI note.", kRightX0 + 20 + o, 606, 14, kDim, 0x21);
        text("impactor.font", "Esc cancels, Delete empties the slot.", kRightX0 + 20 + o, 624, 14, kDim, 0x21);
    }
    void drawDropdown() {
        const auto opts = options(dropItem_);
        const float o = offset_, h = float(opts.size()) * kDdRow;
        quad(ddX0_ + o, ddY_, ddX1_ + o, ddY_ + h, {0.06f, 0.06f, 0.08f, 0.97f});
        frame(ddX0_ + o, ddY_, ddX1_ + o, ddY_ + h, 1.5f, kAccent);
        const std::string cur = value(dropItem_);
        for (size_t k = 0; k < opts.size(); ++k) {
            const float y0 = ddY_ + float(k) * kDdRow, cy = y0 + kDdRow * 0.5f + 1;
            const bool f = int(k) == ddFocus_;
            if (f) quad(ddX0_ + 2 + o, y0 + 1, ddX1_ - 2 + o, y0 + kDdRow - 1, {1, 0.86f, 0.2f, 0.2f});
            if (opts[k].value == cur && opts[k].ok) tick(ddX0_ + 8 + o, cy, kOn);
            text("impactor.font", fit(opts[k].label, 16, ddX1_ - ddX0_ - 40), ddX0_ + 30 + o, cy, 16, !opts[k].ok ? kDim : f ? kFocused : kNormal, 0x21);
        }
    }
    void dropField(float x0, float y0, float x1, float y1, const std::string& v, bool f, bool locked) {
        if (f && !locked) quad(x0, y0, x1, y1, {1, 0.86f, 0.2f, 0.16f});
        frame(x0, y0, x1, y1, 1.5f, locked ? kDim : f ? kFocused : kNormal);
        text("impactor.font", v, x0 + 8, (y0 + y1) * 0.5f + 1, 16, locked ? kDim : f ? kFocused : kNormal, 0x21);
        const float ax = x1 - 14, ay = (y0 + y1) * 0.5f;
        triangle(ax - 6, ay - 3, ax + 6, ay - 3, ax, ay + 4, locked ? kDim : f ? kFocused : kNormal);
    }
    void button(float x0, float y0, float x1, float y1, const std::string& t, bool f, bool locked) {
        if (f && !locked) quad(x0, y0, x1, y1, {1, 0.86f, 0.2f, 0.16f});
        frame(x0, y0, x1, y1, f && !locked ? 2.5f : 1.5f, locked ? kDim : f ? kFocused : kNormal);
        text("impactor.font", t, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f + 1, 17, locked ? kDim : f ? kFocused : kNormal, 0x22);
    }
    void check(float x0, float cy, bool on, bool f) {
        frame(x0, cy - 11, x0 + 22, cy + 11, 2.5f, f ? kFocused : kNormal);
        if (on) tick(x0 + 3, cy, kOn);
    }
    void tick(float x, float cy, const std::array<float, 4>& c) {
        line(x + 2, cy + 1, x + 7, cy + 6, 3.5f, c);
        line(x + 7, cy + 6, x + 16, cy - 6, 3.5f, c);
    }

    // ---- the adapter (gd01's SelectHost calls): Encore's menu font for gd01's rokk / impactor
    void sound(const char* seq) { ui.playSfx(seq); }
    std::string fit(const std::string& s, float h, float width) { return d_.fit(s, h, width); }
    void text(const char*, const std::string& s, float x, float y, float h, const std::array<float, 4>& c, uint32_t align) {
        d_.text(s, x, y, h, c, align);
    }
    void quad(float x0, float y0, float x1, float y1, const std::array<float, 4>& c) { d_.quad(x0, y0, x1, y1, c); }
    void frame(float x0, float y0, float x1, float y1, float t, const std::array<float, 4>& c) { d_.frame(x0, y0, x1, y1, t, c); }
    void line(float x0, float y0, float x1, float y1, float w, const std::array<float, 4>& c) { d_.line(x0, y0, x1, y1, w, c); }
    void triangle(float ax, float ay, float bx, float by, float cx, float cy, const std::array<float, 4>& c) {
        d_.triangle(ax, ay, bx, by, cx, cy, c);
    }
    void warning(float x, float cy) { d_.warning(x, cy); }  // the yellow warning sign: not available

    Input* input_;
    PortDraw d_;
    int focus_ = 0, rfocus_ = 0, slot_ = 0, dropdown_ = -1, ddFocus_ = 0, editPlayer_ = -1;
    Item dropItem_{Field::Toggle};
    std::string editDevice_, editType_, status_;
    float ddX0_ = 0, ddX1_ = 0, ddY_ = 0, scroll_ = 0;
    float captureMs_ = 0, statusMs_ = 0, offset_ = 0;
    size_t reportedDevices_ = 0;
};

}  // namespace

std::unique_ptr<ScriptObject> makeGamepadPanel(UiManager& ui, const std::string& name) { return std::make_unique<GamepadPanelObj>(ui, name); }

}  // namespace ui
