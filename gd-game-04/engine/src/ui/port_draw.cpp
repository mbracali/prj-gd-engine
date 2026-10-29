// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/port_draw.h"

#include <algorithm>
#include <cmath>

#include "data/qbkey.h"
#include "render/canvas.h"
#include "ui/ui_system.h"

namespace {

// The main menu's font (create_main_menu: main_menu_font = fontgrid_title_gh3)
// and the options menu's (options_menu_font = text_a5) [C data].
uint32_t fontKey(bool title) {
    static const uint32_t t = qbkey::key("fontgrid_title_gh3"), b = qbkey::key("text_a5");
    return title ? t : b;
}

std::array<uint8_t, 4> bytes(const std::array<float, 4>& c) {
    auto b = [](float v) { return uint8_t(std::lround(std::fmin(std::fmax(v, 0.0f), 1.0f) * 255)); };
    return {b(c[0]), b(c[1]), b(c[2]), b(c[3])};
}

}  // namespace

void PortDraw::quad(float x0, float y0, float x1, float y1, const Color& c) {
    const auto f = bytes(faded(c));
    const Vertex v[6] = {{x0, y0, 0, 0, 0, f[0], f[1], f[2], f[3]}, {x1, y0, 0, 0, 0, f[0], f[1], f[2], f[3]},
                         {x1, y1, 0, 0, 0, f[0], f[1], f[2], f[3]}, {x0, y0, 0, 0, 0, f[0], f[1], f[2], f[3]},
                         {x1, y1, 0, 0, 0, f[0], f[1], f[2], f[3]}, {x0, y1, 0, 0, 0, f[0], f[1], f[2], f[3]}};
    c_.draw(v, 6, 0, Blend::Alpha);
}

void PortDraw::frame(float x0, float y0, float x1, float y1, float t, const Color& c) {
    quad(x0, y0, x1, y0 + t, c), quad(x0, y1 - t, x1, y1, c), quad(x0, y0 + t, x0 + t, y1 - t, c), quad(x1 - t, y0 + t, x1, y1 - t, c);
}

void PortDraw::line(float x0, float y0, float x1, float y1, float w, const Color& c) {
    const float dx = x1 - x0, dy = y1 - y0, l = std::hypot(dx, dy);
    if (l <= 0) return;
    const float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
    const auto f = bytes(faded(c));
    const Vertex v[6] = {{x0 + nx, y0 + ny, 0, 0, 0, f[0], f[1], f[2], f[3]}, {x1 + nx, y1 + ny, 0, 0, 0, f[0], f[1], f[2], f[3]},
                         {x1 - nx, y1 - ny, 0, 0, 0, f[0], f[1], f[2], f[3]}, {x0 + nx, y0 + ny, 0, 0, 0, f[0], f[1], f[2], f[3]},
                         {x1 - nx, y1 - ny, 0, 0, 0, f[0], f[1], f[2], f[3]}, {x0 - nx, y0 - ny, 0, 0, 0, f[0], f[1], f[2], f[3]}};
    c_.draw(v, 6, 0, Blend::Alpha);
}

void PortDraw::triangle(float ax, float ay, float bx, float by, float cx, float cy, const Color& c) {
    const auto f = bytes(faded(c));
    const Vertex v[3] = {{ax, ay, 0, 0, 0, f[0], f[1], f[2], f[3]}, {bx, by, 0, 0, 0, f[0], f[1], f[2], f[3]},
                         {cx, cy, 0, 0, 0, f[0], f[1], f[2], f[3]}};
    c_.draw(v, 3, 0, Blend::Alpha);
}

void PortDraw::arrow(float x, float y, bool up, const Color& c) {
    if (up) triangle(x - 9, y + 5, x + 9, y + 5, x, y - 5, c);
    else triangle(x - 9, y - 5, x + 9, y - 5, x, y + 5, c);
}

void PortDraw::warning(float x, float cy) {
    triangle(x, cy + 9, x + 20, cy + 9, x + 10, cy - 9, kWarn);
    quad(x + 9, cy - 3, x + 11, cy + 3, {0, 0, 0, 1});  // the "!", drawn: the fonts' is too thin at this size
    quad(x + 9, cy + 5, x + 11, cy + 7, {0, 0, 0, 1});
}

void PortDraw::checkBox(float x1, float cy, bool on, const Color& frameColor, const Color& tick) {
    const float x0 = x1 - 24;
    frame(x0, cy - 12, x1, cy + 12, 2.5f, frameColor);
    if (!on) return;
    line(x0 + 5, cy + 1, x0 + 10, cy + 7, 4, tick);
    line(x0 + 10, cy + 7, x0 + 20, cy - 7, 4, tick);
}

void PortDraw::text(const std::string& s, float x, float y, float h, const Color& c, uint32_t align, bool title) {
    if (s.empty()) return;
    // GH3's options font draws ';' as a 'y' [I: seen]: a comma instead.
    std::string t = s;
    std::replace(t.begin(), t.end(), ';', ',');
    ui_.drawText(c_, fontKey(title), t, x, y, h, bytes(faded(c)), align);
}

float PortDraw::width(const std::string& s, float h, bool title) const { return ui_.textWidth(fontKey(title), s, h); }

std::string PortDraw::fit(std::string s, float h, float w, bool title) const {
    if (width(s, h, title) <= w) return s;
    while (s.size() > 1 && width(s + "...", h, title) > w) s.pop_back();
    return s + "...";
}

std::vector<std::string> PortDraw::wrap(const std::string& s, float w, float h) const {
    std::vector<std::string> out;
    std::string line, word;
    auto flush = [&] {
        if (word.empty()) return;
        const std::string tryLine = line.empty() ? word : line + " " + word;
        if (!line.empty() && width(tryLine, h) > w) out.push_back(line), line = word;
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
