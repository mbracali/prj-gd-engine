// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/ui_system.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cmath>
#include <exception>

#include "data/disc.h"
#include "data/pak.h"
#include "data/qbkey.h"
#include "render/canvas.h"
#include "render/texture_cache.h"
#include "script/vm.h"

namespace ui {

namespace {

uint32_t K(const char* s) { return qbkey::key(s); }

const uint32_t kRootWindow = K("root_window");
const uint32_t kVMenu = K("vmenu"), kHMenu = K("hmenu"), kVScrollingMenu = K("vscrollingmenu");
const uint32_t kSprite = K("spriteelement"), kText = K("textelement"), kTextBlock = K("textblockelement");

float justValue(const qb::Value& v) {
    if (v.type == qb::Type::Key) {
        if (v.key == K("left") || v.key == K("top")) return -1;
        if (v.key == K("right") || v.key == K("bottom")) return 1;
        return 0;  // center
    }
    return v.number();
}

void readPair(const qb::Value& v, float& a, float& b) {
    if (v.type == qb::Type::Pair || v.type == qb::Type::Vector) a = v.f[0], b = v.f[1];
    else if (v.type == qb::Type::Int || v.type == qb::Type::Float) a = b = v.number();
}

std::u16string utf16(const std::string& s) {
    std::u16string out;
    for (size_t i = 0; i < s.size();) {
        uint32_t c = uint8_t(s[i]);
        size_t n = 1;
        if (c >= 0xE0 && i + 2 < s.size()) c = (c & 0x0F) << 12 | (uint8_t(s[i + 1]) & 0x3F) << 6 | (uint8_t(s[i + 2]) & 0x3F), n = 3;
        else if (c >= 0xC0 && i + 1 < s.size()) c = (c & 0x1F) << 6 | (uint8_t(s[i + 1]) & 0x3F), n = 2;
        out.push_back(char16_t(c));
        i += n;
    }
    return out;
}

// The script that runs a native: its innermost frame's parameters.
qb::Struct* locals(qs::ScriptRun& run) {
    if (run.frames.empty()) return nullptr;
    if (!run.frames.back().locals) run.frames.back().locals = std::make_shared<qb::Struct>();
    return run.frames.back().locals.get();
}

}  // namespace

UiSystem::UiSystem(qs::Vm& vm, const Disc& disc, TextureCache& textures, gfx::RenderDevice& device)
    : vm_(vm), disc_(disc), tex_(textures), dev_(device) {
    root_ = std::make_unique<Element>();
    root_->id = kRootWindow;
    root_->type = K("containerelement");
    root_->w = 1280, root_->h = 720;
    root_->justX = root_->justY = -1;
    byId_[kRootWindow] = root_.get();
    // Fonts: every .fnt of global.pak, by the entry's short name (text_a6 ...) [C data].
    try {
        Pak pak = Pak::load(disc_, "DATA/COMPRESSED/ZONES/global.pak.xen");
        for (const PakEntry& e : pak.entries()) {
            if (e.type != K(".fnt")) continue;
            auto [p, n] = pak.data(e);
            FontEntry fe;
            fe.f = font::parseFont(p, n);
            const gh3tex::Texture t = font::decodeFontTexture(fe.f, p, n);
            std::string err;
            auto lv = t.texLevels();
            fe.tex = dev_.createTexture(t.desc(), lv.data(), err);
            fonts_[e.fullName] = std::move(fe);  // by the font name (text_a6) [C data]
        }
    } catch (const std::exception& ex) {
        SDL_Log("ui: fonts: %s", ex.what());
    }
}

UiSystem::~UiSystem() {
    for (auto& [k, f] : fonts_)
        if (f.tex) dev_.destroyTexture(f.tex);
}

const UiSystem::FontEntry* UiSystem::fontFor(uint32_t key) const {
    auto it = fonts_.find(key);
    if (it != fonts_.end()) return &it->second;
    if (!fonts_.empty()) return &fonts_.begin()->second;
    return nullptr;
}

Element* UiSystem::find(uint32_t id) const {
    auto it = byId_.find(id);
    return it == byId_.end() ? nullptr : it->second;
}

Element* UiSystem::resolve(const qb::Value* v) const {
    if (!v) return nullptr;
    if (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr) return find(v->key);
    return nullptr;
}

void UiSystem::applyProps(Element& e, const qb::Struct& p) {
    for (const qb::Item& it : p.items) {
        const qb::Value& v = it.value;
        const uint32_t n = it.name;
        if (n == 0) {
            // Flags: allow_expansion, hide / unhide ... [I]
            if (v.type == qb::Type::Key) {
                if (v.key == K("allow_expansion")) e.allowExpansion = true;
                else if (v.key == K("hide")) e.hidden = true;
                else if (v.key == K("unhide")) e.hidden = false;
            }
            continue;
        }
        if (n == K("pos")) readPair(v, e.x, e.y);
        else if (n == K("dims")) readPair(v, e.w, e.h);
        else if (n == K("scale")) readPair(v, e.sx, e.sy);
        else if (n == K("just") && v.type == qb::Type::Array && v.arr && v.arr->values.size() >= 2)
            e.justX = justValue(v.arr->values[0]), e.justY = justValue(v.arr->values[1]);
        else if (n == K("internal_just") && v.type == qb::Type::Array && v.arr && v.arr->values.size() >= 2)
            e.ijustX = justValue(v.arr->values[0]), e.ijustY = justValue(v.arr->values[1]);
        else if (n == K("rgba") && v.type == qb::Type::Array && v.arr) {
            for (size_t k = 0; k < 4 && k < v.arr->values.size(); ++k)
                e.rgba[k] = uint8_t(std::clamp(int(v.arr->values[k].number()), 0, 255));
        } else if (n == K("alpha")) e.alpha = v.number();
        else if (n == K("z_priority")) e.z = v.number();
        else if (n == K("rot_angle")) e.rot = v.number();
        else if (n == K("texture")) e.texture = v.key;
        else if (n == K("material")) e.material = v.key;
        else if (n == K("font")) e.font = v.key;
        else if (n == K("text")) e.text = v.str;
        else if (n == K("event_handlers") && v.type == qb::Type::Array && v.arr) {
            for (const qb::Value& h : v.arr->values) {
                if (h.type != qb::Type::Struct || !h.st) continue;
                Element::Handler hd;
                int unnamed = 0;
                for (const qb::Item& hi : h.st->items) {
                    if (hi.name == 0 && (hi.value.type == qb::Type::Key || hi.value.type == qb::Type::KeyPtr)) {
                        if (unnamed++ == 0) hd.event = hi.value.key;
                        else hd.script = hi.value.key;
                    } else if (hi.name == K("params") && hi.value.st) {
                        hd.params = *hi.value.st;
                    }
                }
                e.handlers.push_back(std::move(hd));
            }
        }
    }
}

void UiSystem::forget(Element* e) {
    if (e == focus_) focus_ = nullptr;
    if (e->id) {
        auto it = byId_.find(e->id);
        if (it != byId_.end() && it->second == e) byId_.erase(it);
    }
    for (auto& c : e->children) forget(c.get());
}

void UiSystem::destroy(Element* e) {
    if (!e || e == root_.get()) return;
    forget(e);
    ++stats_.destroyed;
    Element* p = e->parent;
    if (!p) return;
    p->children.erase(std::remove_if(p->children.begin(), p->children.end(), [&](const std::unique_ptr<Element>& c) { return c.get() == e; }),
                      p->children.end());
}

bool UiSystem::launch(Element* target, uint32_t event, const qb::Struct& data) {
    ++stats_.events;
    for (Element* e = target; e; e = e->parent) {
        for (const Element::Handler& h : e->handlers) {
            if (h.event != event) continue;
            qb::Struct params = h.params;
            for (const qb::Item& it : data.items) qs::Vm::set(params, it.name, it.value);
            ++stats_.handlersRun;
            vm_.spawn(h.script, params, 0);
            return true;
        }
        // Menus move their focus between their children [I until docs/ui.md].
        const bool vertical = e->type == kVMenu || e->type == kVScrollingMenu, horizontal = e->type == kHMenu;
        if ((vertical && (event == K("pad_up") || event == K("pad_down"))) ||
            (horizontal && (event == K("pad_left") || event == K("pad_right")))) {
            if (e->children.empty()) return true;
            const int dir = (event == K("pad_up") || event == K("pad_left")) ? -1 : 1;
            const int n = int(e->children.size());
            Element* old = e->children[size_t(std::clamp(e->menuIndex, 0, n - 1))].get();
            e->menuIndex = (e->menuIndex + dir + n) % n;
            Element* now = e->children[size_t(e->menuIndex)].get();
            launch(old, K("unfocus"), qb::Struct{});
            focus_ = now;
            launch(now, K("focus"), qb::Struct{});
            return true;
        }
        if (event == K("focus") || event == K("unfocus")) return false;  // not bubbled
    }
    return false;
}

void UiSystem::padEvent(uint32_t event, int device) {
    qb::Struct data;
    qs::Vm::set(data, K("device_num"), qs::Vm::makeInt(device));
    if (focus_) launch(focus_, event, data);
}

void UiSystem::update(double dtMs) {
    std::vector<Element*> stack{root_.get()};
    while (!stack.empty()) {
        Element* e = stack.back();
        stack.pop_back();
        for (auto& c : e->children) stack.push_back(c.get());
        Element::Morph& m = e->morph;
        if (!m.active) continue;
        m.t += dtMs;
        const float k = m.duration > 0 ? float(std::min(1.0, m.t / m.duration)) : 1.0f;
        if (m.pos) e->x = m.x0 + (m.x1 - m.x0) * k, e->y = m.y0 + (m.y1 - m.y0) * k;
        if (m.scale) e->sx = m.sx0 + (m.sx1 - m.sx0) * k, e->sy = m.sy0 + (m.sy1 - m.sy0) * k;
        if (m.alpha) e->alpha = m.a0 + (m.a1 - m.a0) * k;
        if (m.rot) e->rot = m.r0 + (m.r1 - m.r0) * k;
        if (k >= 1) m.active = false;
    }
}

void UiSystem::draw(Canvas& c) const {
    struct Item {
        const Element* e;
        float x, y, sx, sy, a;
        size_t order;
    };
    std::vector<Item> items;
    struct Node {
        const Element* e;
        float x, y, sx, sy, a;
    };
    std::vector<Node> stack{{root_.get(), 0, 0, 1, 1, 1}};
    size_t order = 0;
    while (!stack.empty()) {
        Node n = stack.back();
        stack.pop_back();
        if (n.e->hidden) continue;
        // A child's pos is relative to its parent's, scaled by it; alpha
        // multiplies down the tree [I until docs/ui.md].
        const float x = n.x + n.e->x * n.sx, y = n.y + n.e->y * n.sy;
        const float sx = n.sx * n.e->sx, sy = n.sy * n.e->sy, a = n.a * n.e->alpha;
        if (n.e != root_.get()) items.push_back({n.e, x, y, sx, sy, a, order++});
        for (size_t k = n.e->children.size(); k-- > 0;) stack.push_back({n.e->children[k].get(), x, y, sx, sy, a});
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& l, const Item& r) { return l.e->z < r.e->z; });
    for (const Item& it : items) {
        const Element& e = *it.e;
        const uint8_t alpha = uint8_t(std::clamp(float(e.rgba[3]) * it.a, 0.0f, 255.0f));
        if (alpha == 0) continue;
        if (e.type == kSprite && e.texture) {
            const gfx::TextureId id = tex_.find(e.texture);
            if (!id) {
                ++stats_.missingTextures;
                continue;
            }
            int tw = 0, th = 0;
            tex_.size(e.texture, tw, th);
            const float w = (e.w > 0 ? e.w : float(tw)) * it.sx, h = (e.h > 0 ? e.h : float(th)) * it.sy;
            const float x0 = it.x - (e.justX + 1) / 2 * w, y0 = it.y - (e.justY + 1) / 2 * h;
            const float rad = e.rot * 3.14159265f / 180.0f, cs = std::cos(rad), sn = std::sin(rad);
            auto P = [&](float px, float py, float u, float v) {
                const float dx = px - it.x, dy = py - it.y;
                return Vertex{it.x + dx * cs - dy * sn, it.y + dx * sn + dy * cs, 0, u, v, e.rgba[0], e.rgba[1], e.rgba[2], alpha};
            };
            const Vertex a = P(x0, y0, 0, 0), b = P(x0 + w, y0, 1, 0), cc = P(x0 + w, y0 + h, 1, 1), d = P(x0, y0 + h, 0, 1);
            const Vertex q[6] = {a, b, cc, a, cc, d};
            c.draw(q, 6, int(id.id), Blend::Alpha);
            ++stats_.spritesDrawn;
        } else if ((e.type == kText || e.type == kTextBlock) && !e.text.empty()) {
            const FontEntry* fe = fontFor(e.font);
            if (!fe) {
                ++stats_.missingFonts;
                continue;
            }
            const font::Font& f = fe->f;
            const std::u16string s = utf16(e.text);
            // Measure, then place by just (and internal_just inside dims) [I].
            float width = 0;
            for (char16_t ch : s) width += (ch == u' ' ? f.spaceWidth : f.glyphFor(uint16_t(ch)).width) + float(f.tracking);
            const float lh = f.lineHeight;
            const float bw = (e.type == kTextBlock && e.w > 0) ? e.w : width, bh = (e.type == kTextBlock && e.h > 0) ? e.h : lh;
            float x0 = it.x - (e.justX + 1) / 2 * bw * it.sx, y0 = it.y - (e.justY + 1) / 2 * bh * it.sy;
            if (e.type == kTextBlock) x0 += (e.ijustX + 1) / 2 * (bw - width) * it.sx;
            float pen = x0;
            const float tw = float(f.texWidth), th = float(f.texHeight);
            std::vector<Vertex> verts;
            for (char16_t ch : s) {
                if (ch == u' ') {
                    pen += (f.spaceWidth + float(f.tracking)) * it.sx;
                    continue;
                }
                const font::Glyph& g = f.glyphFor(uint16_t(ch));
                // Glyph uv in texels or normalised: normalise if > 1 [I].
                float u0 = g.u0, v0 = g.v0, u1 = g.u1, v1 = g.v1;
                if (u1 > 1.5f || v1 > 1.5f) u0 /= tw, u1 /= tw, v0 /= th, v1 /= th;
                // The glyph's "baseline" field is its descent below the line's
                // baseline (g 6, ',' 4, '.' -1) [C data]; the baseline sits at
                // lineHeight - vShift from the top [I].
                const float baseY = y0 + (lh - float(f.vShift)) * it.sy;
                const float gx = pen + float(g.xOffset) * it.sx, gy = baseY + (g.baseline - g.height) * it.sy;
                const float gw = g.width * it.sx, gh = g.height * it.sy;
                const Vertex a{gx, gy, 0, u0, v0, e.rgba[0], e.rgba[1], e.rgba[2], alpha}, b{gx + gw, gy, 0, u1, v0, e.rgba[0], e.rgba[1], e.rgba[2], alpha};
                const Vertex cc{gx + gw, gy + gh, 0, u1, v1, e.rgba[0], e.rgba[1], e.rgba[2], alpha}, d{gx, gy + gh, 0, u0, v1, e.rgba[0], e.rgba[1], e.rgba[2], alpha};
                verts.insert(verts.end(), {a, b, cc, a, cc, d});
                pen += (g.width + float(f.tracking)) * it.sx;
            }
            if (!verts.empty()) c.draw(verts.data(), verts.size(), int(fe->tex.id), Blend::Alpha);
            ++stats_.textDrawn;
        }
    }
}

void UiSystem::addNatives() {
    using qs::ScriptRun;
    using qs::Vm;
    // CreateScreenElement type = ... parent = ... id = ... props: the element,
    // its id returned in the caller's `id` [C data: scripts use <id> after it].
    vm_.addNative("CreateScreenElement", [this](Vm&, ScriptRun& run, qb::Struct& p) {
        auto e = std::make_unique<Element>();
        static uint32_t anonymous = 0x80000000u;
        const qb::Value* type = Vm::param(p, K("type"));
        e->type = type ? type->key : K("containerelement");
        const qb::Value* id = Vm::param(p, K("id"));
        e->id = id ? id->key : anonymous++;
        Element* parent = resolve(Vm::param(p, K("parent")));
        if (!parent) parent = root_.get();
        if (e->type == kSprite) e->justX = e->justY = 0;
        applyProps(*e, p);
        e->parent = parent;
        if (Element* old = find(e->id)) destroy(old);  // an id is unique [I]
        byId_[e->id] = e.get();
        const uint32_t newId = e->id;
        parent->children.push_back(std::move(e));
        ++stats_.created;
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("id"), Vm::makeKey(newId));
        return true;
    });
    vm_.addNative("DestroyScreenElement", [this](Vm&, ScriptRun&, qb::Struct& p) {
        Element* e = resolve(Vm::param(p, K("id")));
        if (!e) return false;
        if (Vm::flag(p, K("preserve_parent"))) {
            while (!e->children.empty()) destroy(e->children.back().get());
        } else {
            destroy(e);
        }
        return true;
    });
    vm_.addNative("ScreenElementExists", [this](Vm&, ScriptRun&, qb::Struct& p) { return resolve(Vm::param(p, K("id"))) != nullptr; });
    vm_.addNative("SetScreenElementProps", [this](Vm&, ScriptRun&, qb::Struct& p) {
        Element* e = resolve(Vm::param(p, K("id")));
        if (!e) return false;
        applyProps(*e, p);
        return true;
    });
    vm_.addNative("DoScreenElementMorph", [this](Vm&, ScriptRun&, qb::Struct& p) {
        Element* e = resolve(Vm::param(p, K("id")));
        if (!e) return false;
        Element::Morph m;
        m.active = true;
        m.duration = double(Vm::number(Vm::param(p, K("time")), 0)) * 1000.0;
        m.x0 = m.x1 = e->x, m.y0 = m.y1 = e->y, m.sx0 = m.sx1 = e->sx, m.sy0 = m.sy1 = e->sy;
        m.a0 = m.a1 = e->alpha, m.r0 = m.r1 = e->rot;
        if (const qb::Value* v = Vm::param(p, K("pos"))) m.pos = true, readPair(*v, m.x1, m.y1);
        if (const qb::Value* v = Vm::param(p, K("scale"))) m.scale = true, readPair(*v, m.sx1, m.sy1);
        if (const qb::Value* v = Vm::param(p, K("alpha"))) m.alpha = true, m.a1 = v->number();
        if (const qb::Value* v = Vm::param(p, K("rot_angle"))) m.rot = true, m.r1 = v->number();
        e->morph = m;
        ++stats_.morphs;
        if (m.duration <= 0) update(0);
        return true;
    });
    vm_.addNative("GetScreenElementDims", [this](Vm&, ScriptRun& run, qb::Struct& p) {
        Element* e = resolve(Vm::param(p, K("id")));
        if (!e) return false;
        float w = e->w, h = e->h;
        if (e->type == kSprite && w <= 0 && e->texture) {
            int tw = 0, th = 0;
            tex_.size(e->texture, tw, th);
            w = float(tw), h = float(th);
        }
        if (qb::Struct* l = locals(run)) {
            Vm::set(*l, K("width"), Vm::makeFloat(w * e->sx));
            Vm::set(*l, K("height"), Vm::makeFloat(h * e->sy));
        }
        return true;
    });
    vm_.addNative("GetScreenElementPosition", [this](Vm&, ScriptRun& run, qb::Struct& p) {
        Element* e = resolve(Vm::param(p, K("id")));
        if (!e) return false;
        if (qb::Struct* l = locals(run)) {
            qb::Value v;
            v.type = qb::Type::Pair;
            v.f[0] = e->x, v.f[1] = e->y;
            Vm::set(*l, K("ScreenElementPos"), v);
        }
        return true;
    });
    vm_.addNative("LaunchEvent", [this](Vm&, ScriptRun&, qb::Struct& p) {
        const qb::Value* type = Vm::param(p, K("type"));
        Element* target = resolve(Vm::param(p, K("target")));
        if (!type) return false;
        qb::Struct data;
        if (const qb::Value* d = Vm::param(p, K("data")); d && d->st) data = *d->st;
        if (type->key == K("focus") && target) {
            focus_ = target;
            if (target->type == kVMenu || target->type == kHMenu || target->type == kVScrollingMenu) {
                if (!target->children.empty()) {
                    target->menuIndex = std::clamp(target->menuIndex, 0, int(target->children.size()) - 1);
                    focus_ = target->children[size_t(target->menuIndex)].get();
                    launch(focus_, K("focus"), data);
                }
            }
        }
        if (!target) return false;
        launch(target, type->key, data);
        return true;
    });
    vm_.addNative("SetScreenElementLock", [](Vm&, ScriptRun&, qb::Struct&) { return true; });
}

}  // namespace ui
