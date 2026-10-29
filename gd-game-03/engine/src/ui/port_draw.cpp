// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/port_draw.h"

#include <cmath>

#include "render/canvas.h"
#include "ui/screen_text.h"
#include "ui/ui_manager.h"

namespace ui {

namespace {

// ScreenText's height is the capitals' height, a line being 1.45 times it.
constexpr float kLineToCap = 1.45f;

}  // namespace

PortDraw::FullFrame::FullFrame(UiManager& ui) : ui_(ui) {
    ui_.canvas().flush();
    was_ = ui_.canvas().renderer().setFullFrame(true);
}

PortDraw::FullFrame::~FullFrame() {
    ui_.canvas().flush();
    ui_.canvas().renderer().setFullFrame(was_);
}

void PortDraw::updateTransition() {
    int phase = 0;
    float t = 1;
    bool back = false;
    ui_.transitionState(phase, t, back);
    const float e = t * t * (3 - 2 * t);
    offset_ = 0, fade_ = 1;
    if (phase == 2) offset_ = (back ? -1.0f : 1.0f) * (1 - e) * kScreenW * 0.6f, fade_ = e;
    if (phase == 1) offset_ = (back ? 1.0f : -1.0f) * e * kScreenW * 0.6f, fade_ = 1 - e;
}

void PortDraw::begin(int pass) {
    pass_ = pass;
    if (pass == 1) {
        ui_.canvas().flush();
        ui_.screenText().begin(ui_.canvas().renderer());
    }
}

void PortDraw::quad(float x0, float y0, float x1, float y1, const Color& c) {
    if (pass_ != 0) return;
    const Color f = faded(c);
    const uint8_t r = uint8_t(f[0] * 255), g = uint8_t(f[1] * 255), b = uint8_t(f[2] * 255), a = uint8_t(f[3] * 255);
    const Vertex v[6] = {{x0, y0, 0, 0, 0, r, g, b, a}, {x1, y0, 0, 0, 0, r, g, b, a}, {x1, y1, 0, 0, 0, r, g, b, a},
                         {x0, y0, 0, 0, 0, r, g, b, a}, {x1, y1, 0, 0, 0, r, g, b, a}, {x0, y1, 0, 0, 0, r, g, b, a}};
    ui_.canvas().draw(v, 6, 0, Blend::Alpha);
}

void PortDraw::frame(float x0, float y0, float x1, float y1, float t, const Color& c) {
    quad(x0, y0, x1, y0 + t, c), quad(x0, y1 - t, x1, y1, c), quad(x0, y0 + t, x0 + t, y1 - t, c), quad(x1 - t, y0 + t, x1, y1 - t, c);
}

void PortDraw::line(float x0, float y0, float x1, float y1, float w, const Color& c) {
    if (pass_ != 0) return;
    const float dx = x1 - x0, dy = y1 - y0, l = std::hypot(dx, dy);
    if (l <= 0) return;
    const float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
    const Color f = faded(c);
    const uint8_t r = uint8_t(f[0] * 255), g = uint8_t(f[1] * 255), b = uint8_t(f[2] * 255), a = uint8_t(f[3] * 255);
    const Vertex v[6] = {{x0 + nx, y0 + ny, 0, 0, 0, r, g, b, a}, {x1 + nx, y1 + ny, 0, 0, 0, r, g, b, a},
                         {x1 - nx, y1 - ny, 0, 0, 0, r, g, b, a}, {x0 + nx, y0 + ny, 0, 0, 0, r, g, b, a},
                         {x1 - nx, y1 - ny, 0, 0, 0, r, g, b, a}, {x0 - nx, y0 - ny, 0, 0, 0, r, g, b, a}};
    ui_.canvas().draw(v, 6, 0, Blend::Alpha);
}

void PortDraw::triangle(float ax, float ay, float bx, float by, float cx, float cy, const Color& c) {
    if (pass_ != 0) return;
    const Color f = faded(c);
    const uint8_t r = uint8_t(f[0] * 255), g = uint8_t(f[1] * 255), b = uint8_t(f[2] * 255), a = uint8_t(f[3] * 255);
    const Vertex v[3] = {{ax, ay, 0, 0, 0, r, g, b, a}, {bx, by, 0, 0, 0, r, g, b, a}, {cx, cy, 0, 0, 0, r, g, b, a}};
    ui_.canvas().draw(v, 3, 0, Blend::Alpha);
}

void PortDraw::arrow(float x, float y, bool up, const Color& c) {
    if (up) triangle(x - 9, y + 5, x + 9, y + 5, x, y - 5, c);
    else triangle(x - 9, y - 5, x + 9, y - 5, x, y + 5, c);
}

void PortDraw::warning(float x, float cy) {
    triangle(x, cy + 9, x + 20, cy + 9, x + 10, cy - 9, kWarn);
    text("!", x + 10, cy + 2, 15, {0, 0, 0, 1}, 0x22);
}

void PortDraw::checkBox(float x1, float cy, bool on, const Color& frameColor, const Color& tick) {
    const float x0 = x1 - 24;
    frame(x0, cy - 12, x1, cy + 12, 2.5f, frameColor);
    if (!on) return;
    line(x0 + 5, cy + 1, x0 + 10, cy + 7, 4, tick);
    line(x0 + 10, cy + 7, x0 + 20, cy - 7, 4, tick);
}

void PortDraw::text(const std::string& s, float x, float y, float h, const Color& c, uint32_t align, const char* font) {
    if (pass_ != 1 || ui_.headless() || s.empty()) return;
    ui_.screenText().draw(ui_.canvas().renderer().materials(), font, s, x, y, h / kLineToCap, faded(c), align);
}

float PortDraw::width(const std::string& s, float h, const char* font) {
    return ui_.headless() ? float(s.size()) * h * 0.4f : ui_.screenText().width(font, s, h / kLineToCap);
}

std::string PortDraw::fit(std::string s, float h, float w, const char* font) {
    if (width(s, h, font) <= w) return s;
    while (s.size() > 1 && width(s + "...", h, font) > w) s.pop_back();
    return s + "...";
}

std::vector<std::string> PortDraw::wrap(const std::string& s, float w, float h, const char* font) {
    std::vector<std::string> out;
    std::string line, word;
    auto flush = [&] {
        if (word.empty()) return;
        const std::string tryLine = line.empty() ? word : line + " " + word;
        if (!line.empty() && width(tryLine, h, font) > w) out.push_back(line), line = word;
        else line = tryLine;
        word.clear();
    };
    for (char ch : s) {
        if (ch == ' ') flush();
        else word += ch;
    }
    flush();
    if (!line.empty()) out.push_back(line);
    return out;
}

}  // namespace ui
