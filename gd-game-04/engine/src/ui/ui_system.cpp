// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/ui_system.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cctype>
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
constexpr float kDeg = 3.14159265f / 180.0f;

bool isMenu(const Element& e) { return e.type == kVMenu || e.type == kHMenu; }

float justValue(const qb::Value& v) {
    if (v.type == qb::Type::Key) {
        if (v.key == K("left") || v.key == K("top")) return -1;
        if (v.key == K("right") || v.key == K("bottom")) return 1;
        return 0;
    }
    return v.number();
}

bool readPair(const qb::Value& v, float& a, float& b) {
    if (v.type == qb::Type::Pair || v.type == qb::Type::Vector) {
        a = v.f[0], b = v.f[1];
        return true;
    }
    if (v.type == qb::Type::Int || v.type == qb::Type::Float) {
        a = b = v.number();
        return true;
    }
    return false;
}

void readJust(const qb::Value& v, float& x, float& y) {
    if (v.type == qb::Type::Array && v.arr && v.arr->values.size() >= 2) x = justValue(v.arr->values[0]), y = justValue(v.arr->values[1]);
    else if (v.type == qb::Type::Pair) x = v.f[0], y = v.f[1];
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

qb::Struct* locals(qs::ScriptRun& run) {
    if (run.frames.empty()) return nullptr;
    if (!run.frames.back().locals) run.frames.back().locals = std::make_shared<qb::Struct>();
    return run.frames.back().locals.get();
}

float ease(int motion, float t) {
    switch (motion) {
    case 1: return t * t * (3 - 2 * t);  // smooth
    case 2: return t * t;                // ease_in
    case 3: return 2 * t - t * t;        // ease_out
    default: return t;
    }
}

int hexDigit(char16_t c) {
    if (c >= u'0' && c <= u'9') return c - u'0';
    if (c >= u'a' && c <= u'f') return c - u'a' + 10;
    if (c >= u'A' && c <= u'F') return c - u'A' + 10;
    return -1;
}

}  // namespace

UiSystem::UiSystem(qs::Vm& vm, const Disc& disc, TextureCache& textures, gfx::RenderDevice& device)
    : vm_(vm), disc_(disc), tex_(textures), dev_(device) {
    root_ = std::make_unique<Element>();
    root_->id = kRootWindow;
    root_->type = K("windowelement");
    root_->w = 1280, root_->h = 720, root_->dimsGiven = true;
    root_->justX = root_->justY = -1;  // [C 0x82315138]
    byId_[kRootWindow] = root_.get();
    loadFonts("DATA/COMPRESSED/ZONES/global.pak.xen");
    loadFonts("DATA/COMPRESSED/FONTS/text_a1.pak.xen");
    loadFonts("DATA/COMPRESSED/FONTS/debug.pak.xen");
    // Default_Font_Colors: the \cN colour table [C data, docs/ui.md].
    if (const qb::Value* t = vm_.global(K("Default_Font_Colors")); t && t->arr)
        for (const qb::Value& c : t->arr->values) {
            std::array<uint8_t, 4> rgba{255, 255, 255, 255};
            if (c.arr)
                for (size_t k = 0; k < 4 && k < c.arr->values.size(); ++k) rgba[k] = uint8_t(c.arr->values[k].number());
            colorTab_.push_back(rgba);
        }
    vm_.objectCall = [this](qs::Vm&, qs::ScriptRun& run, uint32_t obj, uint32_t method, qb::Struct& p, bool& handled) {
        return member(run, obj, method, p, handled);
    };
}

UiSystem::~UiSystem() {
    vm_.objectCall = nullptr;
    for (auto& [k, f] : fonts_)
        if (f.tex) dev_.destroyTexture(f.tex);
}

void UiSystem::loadFonts(const std::string& pakPath) {
    try {
        Pak pak = Pak::load(disc_, pakPath);
        for (const PakEntry& e : pak.entries()) {
            if (e.type != K(".fnt") || fonts_.count(e.fullName)) continue;
            auto [p, n] = pak.data(e);
            FontEntry fe;
            fe.f = font::parseFont(p, n);
            const gh3tex::Texture t = font::decodeFontTexture(fe.f, p, n);
            std::string err;
            auto lv = t.texLevels();
            fe.tex = dev_.createTexture(t.desc(), lv.data(), err);
            fonts_[e.fullName] = std::move(fe);  // by the font's name (text_a6) [C data]
        }
    } catch (const std::exception& ex) {
        SDL_Log("ui: fonts in %s: %s", pakPath.c_str(), ex.what());
    }
}

const UiSystem::FontEntry* UiSystem::fontFor(uint32_t key) const {
    auto it = fonts_.find(key);
    if (it != fonts_.end()) return &it->second;
    // `dialog` / `small` have no .fnt on the disc [C data]: text_a1 stands in [I].
    it = fonts_.find(K("text_a1"));
    if (it != fonts_.end()) return &it->second;
    return fonts_.empty() ? nullptr : &fonts_.begin()->second;
}

Element* UiSystem::find(uint32_t id) const {
    auto it = byId_.find(id);
    return it == byId_.end() ? nullptr : it->second;
}

// An id: a key, or `{parent child = N | local_id | {N child = ...}}` [C GetProps / morph use].
Element* UiSystem::resolve(const qb::Value* v) const {
    if (!v) return nullptr;
    if (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr) return find(v->key);
    if (v->type != qb::Type::Struct || !v->st) return nullptr;
    Element* parent = nullptr;
    const qb::Value* child = nullptr;
    for (const qb::Item& it : v->st->items) {
        if (it.name == 0 && !parent) {
            if (it.value.type == qb::Type::Key || it.value.type == qb::Type::KeyPtr) parent = find(it.value.key);
            else if (it.value.type == qb::Type::Struct) parent = resolve(&it.value);
        } else if (it.name == K("child")) {
            child = &it.value;
        }
    }
    if (!parent || !child) return parent;
    if (child->type == qb::Type::Int) {
        const int i = child->i;
        return i >= 0 && size_t(i) < parent->children.size() ? parent->children[size_t(i)].get() : nullptr;
    }
    if (child->type == qb::Type::Key) {
        for (auto& c : parent->children)
            if (c->localId == child->key || c->id == child->key) return c.get();
        return nullptr;
    }
    if (child->type == qb::Type::Struct && child->st) {
        int idx = -1;
        uint32_t lid = 0;
        for (const qb::Item& it : child->st->items) {
            if (it.name == 0 && it.value.type == qb::Type::Int) idx = it.value.i;
            if (it.name == 0 && it.value.type == qb::Type::Key) lid = it.value.key;
        }
        Element* mid = nullptr;
        if (idx >= 0 && size_t(idx) < parent->children.size()) mid = parent->children[size_t(idx)].get();
        else if (lid)
            for (auto& c : parent->children)
                if (c->localId == lid || c->id == lid) mid = c.get();
        if (!mid) return nullptr;
        const qb::Value* sub = child->st->find(K("child"));
        if (!sub) return mid;
        qb::Value deeper;
        deeper.type = qb::Type::Struct;
        deeper.st = std::make_shared<qb::Struct>();
        deeper.st->items.push_back(qb::Item{0, qs::Vm::makeKey(mid->id)});
        deeper.st->items.push_back(qb::Item{K("child"), *sub});
        return resolve(&deeper);
    }
    return nullptr;
}

void UiSystem::setTexture(Element& e, uint32_t key) {
    e.texture = key;
    int tw = 0, th = 0;
    tex_.size(key, tw, th);
    if (!tw) {
        ++stats_.missingTextures;
        return;
    }
    // On texture set, base dims = texture size; given dims become a scale [C 0x82218F80].
    if (e.dimsGiven && e.w > 0 && e.h > 0) e.sx = e.w / float(tw), e.sy = e.h / float(th);
    e.w = float(tw), e.h = float(th);
    e.dimsGiven = false;
}

void UiSystem::applyProps(Element& e, const qb::Struct& p, bool create) {
    (void)create;
    float newDimsW = -1, newDimsH = -1;
    uint32_t newTexture = 0;
    bool haveScale = false;
    float scaleX = 1, scaleY = 1;
    for (const qb::Item& it : p.items) {
        const qb::Value& v = it.value;
        const uint32_t n = it.name;
        if (n == 0) {
            if (v.type != qb::Type::Key) continue;
            const uint32_t f = v.key;
            if (f == K("hide")) e.hidden = true;
            else if (f == K("unhide")) e.hidden = false;
            else if (f == K("flip_h")) e.flipH = true;
            else if (f == K("flip_v")) e.flipV = true;
            else if (f == K("not_focusable")) e.notFocusable = true;
            else if (f == K("focusable")) e.notFocusable = false;
            else if (f == K("block_events")) e.blockEvents = true;
            else if (f == K("unblock_events")) e.blockEvents = false;
            else if (f == K("allow_expansion")) e.allowExpansion = true;
            else if (f == K("dont_allow_wrap")) e.dontWrap = true;
            else if (f == K("preserve_local_orientation")) e.preserveOrientation = true;
            continue;
        }
        if (n == K("pos")) {
            if (v.type == qb::Type::Struct && v.st) {
                // {(x,y) relative | proportional | absolute} [C 0x8221C240]
                float a = 0, b = 0;
                for (const qb::Item& pi : v.st->items)
                    if (pi.value.type == qb::Type::Pair) a = pi.value.f[0], b = pi.value.f[1];
                if (qs::Vm::flag(*v.st, K("relative"))) e.x += a, e.y += b;
                else if (qs::Vm::flag(*v.st, K("proportional")) && e.parent) e.x = a * e.parent->w, e.y = b * e.parent->h;
                else e.x = a, e.y = b;
            } else {
                readPair(v, e.x, e.y);
            }
        } else if (n == K("dims")) {
            if (v.type == qb::Type::Pair) newDimsW = v.f[0], newDimsH = v.f[1];
        } else if (n == K("scale")) {
            haveScale = readPair(v, scaleX, scaleY);
        } else if (n == K("relative_scale")) {
            float a = 1, b = 1;
            if (readPair(v, a, b)) e.sx *= a, e.sy *= b;
        } else if (n == K("just")) readJust(v, e.justX, e.justY);
        else if (n == K("internal_just")) readJust(v, e.ijustX, e.ijustY);
        else if (n == K("child_anchor")) readJust(v, e.anchorX, e.anchorY);
        else if (n == K("rgba") && v.type == qb::Type::Array && v.arr) {
            for (size_t k = 0; k < 4 && k < v.arr->values.size(); ++k) e.rgba[k] = uint8_t(std::clamp(int(v.arr->values[k].number()), 0, 255));
        } else if (n == K("alpha")) e.alpha = v.number();
        else if (n == K("z_priority")) e.z = v.number(), e.zGiven = true;
        else if (n == K("rot_angle")) e.rot = v.number();
        else if (n == K("relative_rot_angle")) e.rot += v.number();
        else if (n == K("texture")) newTexture = v.key;
        else if (n == K("font")) e.font = v.key;
        else if (n == K("text")) e.text = v.str;
        else if (n == K("local_id")) e.localId = v.key;
        else if (n == K("exclusive_device")) e.exclusiveDevice = int(v.number());
        else if (n == K("font_spacing")) e.fontSpacing = v.number();
        else if (n == K("space_spacing")) e.spaceSpacing = v.number();
        else if (n == K("line_spacing")) e.lineSpacing = v.number();
        else if (n == K("regular_space_amount")) e.regularSpace = v.number();
        else if (n == K("padding_scale")) e.paddingScale = v.number();
        else if (n == K("spacing_between")) e.spacingBetween = v.number();
        else if (n == K("tags") && v.st) {
            for (const qb::Item& ti : v.st->items) qs::Vm::set(e.tags, ti.name, ti.value);
        } else if (n == K("event_handlers") && v.type == qb::Type::Array && v.arr) {
            if (qs::Vm::flag(p, K("replace_handlers"))) {
                for (const qb::Value& h : v.arr->values) {
                    if (!h.st) continue;
                    for (const qb::Item& hi : h.st->items)
                        if (hi.name == 0 && hi.value.type == qb::Type::Key) {
                            const uint32_t ev = hi.value.key;
                            e.handlers.erase(std::remove_if(e.handlers.begin(), e.handlers.end(),
                                                            [&](const Element::Handler& x) { return x.event == ev; }),
                                             e.handlers.end());
                            break;
                        }
                }
            }
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
    if (newDimsW >= 0) {
        e.w = newDimsW, e.h = newDimsH, e.dimsGiven = true;
        if (e.type == kSprite && e.texture && !newTexture) {
            // New dims on a textured sprite: dims are the on-screen size [C 0x82219C78].
            int tw = 0, th = 0;
            tex_.size(e.texture, tw, th);
            if (tw) e.sx = newDimsW / float(tw), e.sy = newDimsH / float(th), e.w = float(tw), e.h = float(th), e.dimsGiven = false;
        }
    }
    if (newTexture) setTexture(e, newTexture);
    if (haveScale) {
        // An explicit scale applies after dims [I order].
        if (e.type == kSprite && e.texture && newDimsW >= 0) e.sx *= scaleX, e.sy *= scaleY;
        else e.sx = scaleX, e.sy = scaleY;
    }
    if (e.type == kText) measureText(e);
}

float UiSystem::textWidth(const FontEntry& fe, const std::u16string& s, const Element& e) const {
    const font::Font& f = fe.f;
    float w = 0, best = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const char16_t ch = s[i];
        if (ch == u'\\' && i + 1 < s.size()) {
            const char16_t code = s[++i];
            if (code == u'n') {
                best = std::max(best, w), w = 0;
                continue;
            }
            if (code == u'c' || code == u'b' || code == u'm' || code == u's') {
                if (i + 1 < s.size()) ++i;
                if (code != u'c') w += 32;  // a button glyph [I size]
                continue;
            }
            if (code != u'\\') continue;
        }
        if (ch == u' ') w += f.spaceWidth + e.spaceSpacing;
        else w += f.glyphFor(uint16_t(ch)).width + float(f.tracking) + e.fontSpacing;
    }
    return std::max(best, w);
}

void UiSystem::measureText(Element& e) const {
    // Base dims = the measured text [C 0x8232ADF0].
    const FontEntry* fe = fontFor(e.font);
    if (!fe) return;
    e.w = textWidth(*fe, utf16(e.text), e);
    e.h = fe->f.lineHeight;
}

void UiSystem::forget(Element* e) {
    if (e->id) {
        auto it = byId_.find(e->id);
        if (it != byId_.end() && it->second == e) byId_.erase(it);
    }
    // Scripts running on the element die with it [I].
    for (auto& r : vm_.runs())
        if (r->object == e->id) r->finished = true;
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

std::vector<Element*> UiSystem::focusedElements() const {
    std::vector<Element*> out;
    std::vector<Element*> stack{root_.get()};
    while (!stack.empty()) {
        Element* e = stack.back();
        stack.pop_back();
        if (e->focused) out.push_back(e);
        for (auto& c : e->children) stack.push_back(c.get());
    }
    return out;
}

void UiSystem::focusMenu(Element* m, const qb::Struct& data) {
    if (m->focused && m->selected >= 0 && size_t(m->selected) < m->children.size() && m->children[size_t(m->selected)]->focused) return;
    if (const qb::Value* c = qs::Vm::param(data, K("controller"))) m->controller = int(c->number());
    if (const qb::Value* cid = qs::Vm::param(data, K("child_id")))
        for (size_t i = 0; i < m->children.size(); ++i)
            if (m->children[i]->id == cid->key) m->selected = int(i);
    m->focused = true;
    const int n = int(m->children.size());
    if (n == 0) return;
    if (m->selected < 0) m->selected = 0;
    m->selected = std::clamp(m->selected, 0, n - 1);
    for (int k = 0; k < n && m->children[size_t(m->selected)]->notFocusable; ++k) m->selected = (m->selected + 1) % n;
    qb::Struct d;
    qs::Vm::set(d, K("controller"), qs::Vm::makeInt(m->controller));
    launch(m->children[size_t(m->selected)].get(), K("focus"), d);
}

void UiSystem::moveMenu(Element* m, int dir) {
    const int n = int(m->children.size());
    if (n == 0 || m->selected < 0) return;
    int next = m->selected;
    for (int k = 0; k < n; ++k) {
        next += dir;
        if (next < 0 || next >= n) {
            if (m->dontWrap) return;
            next = (next + n) % n;
        }
        if (!m->children[size_t(next)]->notFocusable) break;
    }
    if (next == m->selected) return;
    launch(m->children[size_t(m->selected)].get(), K("unfocus"), qb::Struct{});
    m->selected = next;
    qb::Struct d;
    qs::Vm::set(d, K("controller"), qs::Vm::makeInt(m->controller));
    launch(m->children[size_t(next)].get(), K("focus"), d);
}

bool UiSystem::launch(Element* target, uint32_t event, const qb::Struct& data) {
    if (!target) return false;
    ++stats_.events;
    if (target->blockEvents) return false;
    bool handled = false;
    // Every handler of that type, in order, with the element as object and
    // its id among the params [C table 0x821741C8; I order / id].
    const uint32_t id = target->id;
    std::vector<Element::Handler> hs = target->handlers;  // they may change the element
    for (const Element::Handler& h : hs) {
        if (h.event != event) continue;
        qb::Struct params = h.params;
        qs::Vm::set(params, K("id"), qs::Vm::makeKey(id));
        for (const qb::Item& it : data.items) qs::Vm::set(params, it.name, it.value);
        ++stats_.handlersRun;
        handled = true;
        bool r = false;
        vm_.run(h.script, params, &r);
        if (!find(id)) return true;  // destroyed by its handler
    }
    // Menus [C 0x823164E0].
    if (find(id) && isMenu(*target)) {
        const bool vert = target->type == kVMenu;
        if (event == K("focus")) focusMenu(target, data);
        else if (event == K("unfocus")) {
            target->focused = false;
            if (target->selected >= 0 && size_t(target->selected) < target->children.size())
                launch(target->children[size_t(target->selected)].get(), K("unfocus"), qb::Struct{});
        } else if (target->focused && ((vert && event == K("pad_up")) || (!vert && event == K("pad_left")))) moveMenu(target, -1);
        else if (target->focused && ((vert && event == K("pad_down")) || (!vert && event == K("pad_right")))) moveMenu(target, 1);
        return true;
    }
    if (find(id)) {
        if (event == K("focus")) target->focused = true;
        else if (event == K("unfocus")) target->focused = false;
    }
    // A VScrollingMenu passes focus to its menu [I].
    if (find(id) && target->type == kVScrollingMenu && (event == K("focus") || event == K("unfocus")) && !target->children.empty())
        launch(target->children.front().get(), event, data);
    return handled;
}

void UiSystem::padEvent(uint32_t event, int device) {
    qb::Struct data;
    qs::Vm::set(data, K("device_num"), qs::Vm::makeInt(device));
    qs::Vm::set(data, K("controller"), qs::Vm::makeInt(device));
    // To every focused element (a menu and its selected child) [I].
    std::vector<uint32_t> ids;
    for (Element* e : focusedElements())
        if (e->exclusiveDevice < 0 || e->exclusiveDevice == device) ids.push_back(e->id);
    // root_window gets pad_start (the pause handler lives there) [C data].
    if (event == K("pad_start") && std::find(ids.begin(), ids.end(), kRootWindow) == ids.end()) ids.push_back(kRootWindow);
    if (const char* v = SDL_getenv("GD04_UILOG"); v && *v && *v != '0') {
        std::string who;
        for (uint32_t id : ids) who += " " + qbkey::name(id);
        SDL_Log("ui: %s (device %d) ->%s", qbkey::name(event).c_str(), device, who.c_str());
    }
    for (uint32_t id : ids)
        if (Element* e = find(id)) launch(e, event, data);
}

void UiSystem::layoutMenu(Element& m) {
    // [C 0x82315FB0] stack the children in creation order.
    const bool vert = m.type == kVMenu;
    float total = 0, widest = 0;
    std::vector<float> sizes;
    for (auto& c : m.children) {
        const float along = (vert ? c->h * c->sy : c->w * c->sx) * m.paddingScale;
        const float across = vert ? c->w * c->sx : c->h * c->sy;
        sizes.push_back(m.regularSpace > 0 ? m.regularSpace : along);
        widest = std::max(widest, across);
    }
    for (size_t i = 0; i < sizes.size(); ++i) total += sizes[i] + (i ? m.spacingBetween : 0);
    if (!m.dimsGiven) {
        if (vert) m.w = widest, m.h = total;
        else m.w = total, m.h = widest;
    }
    float cursor = vert ? (m.h - total) * (m.ijustY + 1) / 2 : (m.w - total) * (m.ijustX + 1) / 2;
    for (size_t i = 0; i < m.children.size(); ++i) {
        Element& c = *m.children[i];
        if (vert) {
            c.justX = m.ijustX, c.justY = 0;
            c.x = (m.ijustX + 1) / 2 * m.w, c.y = cursor + sizes[i] / 2;
        } else {
            c.justX = 0, c.justY = m.ijustY;
            c.x = cursor + sizes[i] / 2, c.y = (m.ijustY + 1) / 2 * m.h;
        }
        cursor += sizes[i] + m.spacingBetween;
    }
}

void UiSystem::startMorph(Element& e, const qb::Struct& p) {
    Element::Morph m;
    m.active = true;
    m.duration = double(qs::Vm::number(qs::Vm::param(p, K("time")), 0)) * 1000.0;
    if (const qb::Value* mo = qs::Vm::param(p, K("motion"))) {
        if (mo->key == K("smooth")) m.motion = 1;
        else if (mo->key == K("ease_in")) m.motion = 2;
        else if (mo->key == K("ease_out")) m.motion = 3;
    }
    m.x0 = m.x1 = e.x, m.y0 = m.y1 = e.y, m.sx0 = m.sx1 = e.sx, m.sy0 = m.sy1 = e.sy;
    m.a0 = m.a1 = e.alpha, m.r0 = m.r1 = e.rot;
    for (int k = 0; k < 4; ++k) m.c0[size_t(k)] = m.c1[size_t(k)] = e.rgba[size_t(k)];
    if (const qb::Value* v = qs::Vm::param(p, K("pos"))) {
        m.pos = true;
        if (v->type == qb::Type::Struct && v->st) {
            float a = 0, b = 0;
            for (const qb::Item& pi : v->st->items)
                if (pi.value.type == qb::Type::Pair) a = pi.value.f[0], b = pi.value.f[1];
            if (qs::Vm::flag(*v->st, K("relative"))) m.x1 = e.x + a, m.y1 = e.y + b;
            else m.x1 = a, m.y1 = b;
        } else {
            readPair(*v, m.x1, m.y1);
        }
    }
    if (const qb::Value* v = qs::Vm::param(p, K("scale"))) m.scale = readPair(*v, m.sx1, m.sy1);
    if (const qb::Value* v = qs::Vm::param(p, K("relative_scale"))) {
        float a = 1, b = 1;
        if (readPair(*v, a, b)) m.scale = true, m.sx1 = e.sx * a, m.sy1 = e.sy * b;
    }
    if (const qb::Value* v = qs::Vm::param(p, K("alpha"))) m.alpha = true, m.a1 = v->number();
    if (const qb::Value* v = qs::Vm::param(p, K("rot_angle"))) m.rot = true, m.r1 = v->number();
    if (const qb::Value* v = qs::Vm::param(p, K("rgba")); v && v->arr) {
        m.rgba = true;
        for (size_t k = 0; k < 4 && k < v->arr->values.size(); ++k) m.c1[k] = v->arr->values[k].number();
    }
    e.morph = m;
    ++stats_.morphs;
    if (m.duration <= 0) {
        // Time 0 applies the target at once [C].
        if (m.pos) e.x = m.x1, e.y = m.y1;
        if (m.scale) e.sx = m.sx1, e.sy = m.sy1;
        if (m.alpha) e.alpha = m.a1;
        if (m.rot) e.rot = m.r1;
        if (m.rgba)
            for (int k = 0; k < 4; ++k) e.rgba[size_t(k)] = uint8_t(std::clamp(m.c1[size_t(k)], 0.0f, 255.0f));
        e.morph.active = false;
    }
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
        const float k = ease(m.motion, m.duration > 0 ? float(std::min(1.0, m.t / m.duration)) : 1.0f);
        if (m.pos) e->x = m.x0 + (m.x1 - m.x0) * k, e->y = m.y0 + (m.y1 - m.y0) * k;
        if (m.scale) e->sx = m.sx0 + (m.sx1 - m.sx0) * k, e->sy = m.sy0 + (m.sy1 - m.sy0) * k;
        if (m.alpha) e->alpha = m.a0 + (m.a1 - m.a0) * k;
        if (m.rot) e->rot = m.r0 + (m.r1 - m.r0) * k;
        if (m.rgba)
            for (int c = 0; c < 4; ++c)
                e->rgba[size_t(c)] = uint8_t(std::clamp(m.c0[size_t(c)] + (m.c1[size_t(c)] - m.c0[size_t(c)]) * k, 0.0f, 255.0f));
        if (m.duration <= 0 || m.t >= m.duration) m.active = false;
    }
}

std::vector<std::string> UiSystem::visibleTexts() const {
    std::vector<std::string> out;
    std::vector<const Element*> stack{root_.get()};
    while (!stack.empty()) {
        const Element* e = stack.back();
        stack.pop_back();
        if (e->hidden) continue;
        if ((e->type == kText || e->type == kTextBlock) && !e->text.empty()) out.push_back(e->text);
        for (auto& c : e->children) stack.push_back(c.get());
    }
    return out;
}

void UiSystem::dump() const {
    struct N {
        const Element* e;
        int depth;
    };
    std::vector<N> stack{{root_.get(), 0}};
    while (!stack.empty()) {
        const N n = stack.back();
        stack.pop_back();
        const Element& e = *n.e;
        SDL_Log("ui-dump %*s%s %s tex=%s pos=(%.1f,%.1f) dims=(%.1f,%.1f) scale=(%.3f,%.3f) rot=%.1f just=(%.1f,%.1f) z=%.1f a=%.2f%s%s%s \"%s\"",
                n.depth * 2, "", qbkey::name(e.type).c_str(), qbkey::name(e.id).c_str(), e.texture ? qbkey::name(e.texture).c_str() : "-",
                double(e.x), double(e.y), double(e.w), double(e.h), double(e.sx), double(e.sy), double(e.rot), double(e.justX),
                double(e.justY), double(e.z), double(e.alpha), e.hidden ? " hidden" : "", e.focused ? " FOCUSED" : "", e.flipV ? " flipV" : "",
                e.text.substr(0, 40).c_str());
        for (size_t k = e.children.size(); k-- > 0;) stack.push_back({e.children[k].get(), n.depth + 1});
    }
}

void UiSystem::draw(Canvas& c) {
    // Menus lay out their children first (unless locked).
    {
        std::vector<Element*> stack{root_.get()};
        while (!stack.empty()) {
            Element* e = stack.back();
            stack.pop_back();
            if (isMenu(*e) && !e->locked) layoutMenu(*e);
            for (auto& ch : e->children) stack.push_back(ch.get());
        }
    }
    struct Item {
        const Element* e;
        float cx, cy, wsx, wsy, wa, wrot;  // top-left corner, world scale, alpha, rotation (deg)
        float z;
        int depth;
    };
    struct Node {
        const Element* e;
        float cx, cy, wsx, wsy, wa, wrot;
        float z;  // the parent's effective z
        int depth;
    };
    std::vector<Item> items;
    std::vector<Node> stack;
    ++drawCount_;
    for (size_t k = root_->children.size(); k-- > 0;) stack.push_back({root_->children[k].get(), 0, 0, 1, 1, 1, 0, 0, 1});
    while (!stack.empty()) {
        const Node n = stack.back();
        stack.pop_back();
        const Element& e = *n.e;
        if (e.hidden) continue;
        const Element& p = e.parent ? *e.parent : *root_;
        // Composition [C 0x8221BF88]: the parent's child_anchor, pos scaled by the parent.
        const float ax = (p.anchorX + 1) / 2 * p.w * n.wsx, ay = (p.anchorY + 1) / 2 * p.h * n.wsy;
        const float offX = ax + e.x * n.wsx, offY = ay + e.y * n.wsy;
        const float pr = n.wrot * kDeg;
        const float wposX = n.cx + offX * std::cos(pr) - offY * std::sin(pr), wposY = n.cy + offX * std::sin(pr) + offY * std::cos(pr);
        const float wsx = n.wsx * e.sx, wsy = n.wsy * e.sy, wa = n.wa * e.alpha;
        const float wrot = e.preserveOrientation ? e.rot : n.wrot + e.rot;
        const float jx = (e.justX + 1) / 2 * e.w * wsx, jy = (e.justY + 1) / 2 * e.h * wsy;
        const float r = wrot * kDeg;
        const float cx = wposX - (jx * std::cos(r) - jy * std::sin(r)), cy = wposY - (jx * std::sin(r) + jy * std::cos(r));
        // z: the explicit z_priority (it sticks), else the parent's z + 1, 0
        // without a parent [C SetParent 0x8221D5F0 -> SetZTree 0x8221C160].
        const float z = e.zGiven ? e.z : (e.parent ? n.z + 1.0f : 0.0f);
        items.push_back({&e, cx, cy, wsx, wsy, wa, wrot, z, n.depth});
        for (size_t k = e.children.size(); k-- > 0;) stack.push_back({e.children[k].get(), cx, cy, wsx, wsy, wa, wrot, z, n.depth + 1});
    }
    // z_priority is one global key for sprites and texts (one list,
    // 0x8238D7D0) [C]. Equal z: deeper elements over shallower ones, then
    // tree order [I: the game draws the instance made visible last behind].
    std::stable_sort(items.begin(), items.end(), [](const Item& l, const Item& r) { return l.z < r.z || (l.z == r.z && l.depth < r.depth); });
    for (const Item& it : items) {
        const Element& e = *it.e;
        const uint8_t alpha = uint8_t(std::clamp(float(e.rgba[3]) * it.wa, 0.0f, 255.0f));
        if (alpha == 0) continue;
        const float r = it.wrot * kDeg, cs = std::cos(r), sn = std::sin(r);
        auto P = [&](float lx, float ly, float u, float v, const std::array<uint8_t, 4>& col) {
            return Vertex{it.cx + lx * cs - ly * sn, it.cy + lx * sn + ly * cs, 0, u, v, col[0], col[1], col[2], alpha};
        };
        // The drawn bounds, for the mouse (port addition, not GH3's).
        if ((e.type == kSprite && e.texture) || ((e.type == kText || e.type == kTextBlock) && !e.text.empty())) {
            const float w = e.w * it.wsx, h = e.h * it.wsy;
            const Vertex q[4] = {P(0, 0, 0, 0, e.rgba), P(w, 0, 0, 0, e.rgba), P(w, h, 0, 0, e.rgba), P(0, h, 0, 0, e.rgba)};
            e.bx0 = e.bx1 = q[0].x, e.by0 = e.by1 = q[0].y;
            for (const Vertex& v : q) e.bx0 = std::min(e.bx0, v.x), e.bx1 = std::max(e.bx1, v.x), e.by0 = std::min(e.by0, v.y), e.by1 = std::max(e.by1, v.y);
            e.drawnIn = drawCount_;
        }
        if (e.type == kSprite && e.texture) {
            const gfx::TextureId id = tex_.find(e.texture);
            if (!id) continue;
            const float w = e.w * it.wsx, h = e.h * it.wsy;
            float u0 = 0, u1 = 1, v0 = 0, v1 = 1;
            // flip_v mirrors left-right and flip_h top-bottom [C data: the
            // pause frame's top-right quadrant uses flip_v, bottom-left flip_h].
            if (e.flipV) std::swap(u0, u1);
            if (e.flipH) std::swap(v0, v1);
            const Vertex a = P(0, 0, u0, v0, e.rgba), b = P(w, 0, u1, v0, e.rgba), cc = P(w, h, u1, v1, e.rgba), d = P(0, h, u0, v1, e.rgba);
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
            const float lh = f.lineHeight;
            std::vector<std::u16string> lines(1);
            for (size_t i = 0; i < s.size(); ++i) {
                if (s[i] == u'\\' && i + 1 < s.size() && s[i + 1] == u'n') {
                    lines.emplace_back();
                    ++i;
                } else {
                    lines.back().push_back(s[i]);
                }
            }
            std::vector<Vertex> verts;
            std::array<uint8_t, 4> col = e.rgba;
            float lineY = 0;
            for (const std::u16string& line : lines) {
                const float lw = textWidth(*fe, line, e);
                // Lines inside a text block placed by internal_just [I].
                float pen = e.type == kTextBlock ? (e.w - lw) * (e.ijustX + 1) / 2 : 0;
                const float baseY = lineY + (lh - float(f.vShift));
                for (size_t i = 0; i < line.size(); ++i) {
                    const char16_t ch = line[i];
                    if (ch == u'\\' && i + 1 < line.size()) {
                        const char16_t code = line[++i];
                        if (code == u'c' && i + 1 < line.size()) {
                            const int nidx = hexDigit(line[++i]);
                            if (nidx <= 0) col = e.rgba;
                            else if (size_t(nidx - 1) < colorTab_.size()) col = colorTab_[size_t(nidx - 1)], col[3] = e.rgba[3];
                            continue;
                        }
                        if (code == u'b' || code == u'm' || code == u's') {
                            if (i + 1 < line.size()) ++i;
                            pen += 32;  // button glyphs are not drawn yet [approximation]
                            continue;
                        }
                        if (code != u'\\') continue;
                    }
                    if (ch == u' ') {
                        pen += f.spaceWidth + e.spaceSpacing;
                        continue;
                    }
                    const font::Glyph& g = f.glyphFor(uint16_t(ch));
                    // The glyph's "baseline" is its descent below the line's baseline [C data].
                    const float gx = pen + float(g.xOffset), gy = baseY + g.baseline - g.height;
                    const float sx = it.wsx, sy = it.wsy;
                    const Vertex a = P(gx * sx, gy * sy, g.u0, g.v0, col), b = P((gx + g.width) * sx, gy * sy, g.u1, g.v0, col);
                    const Vertex cc = P((gx + g.width) * sx, (gy + g.height) * sy, g.u1, g.v1, col), d = P(gx * sx, (gy + g.height) * sy, g.u0, g.v1, col);
                    verts.insert(verts.end(), {a, b, cc, a, cc, d});
                    pen += g.width + float(f.tracking) + e.fontSpacing;
                }
                lineY += lh * e.lineSpacing;
            }
            if (!verts.empty()) c.draw(verts.data(), verts.size(), int(fe->tex.id), Blend::Alpha);
            ++stats_.textDrawn;
        }
    }
}

// ---- the port's own screens and the mouse (not GH3's) ----

void UiSystem::drawText(Canvas& c, uint32_t font, const std::string& text, float x, float y, float h, const std::array<uint8_t, 4>& rgba,
                        uint32_t align) const {
    const FontEntry* fe = fontFor(font);
    if (!fe || text.empty() || h <= 0) return;
    const FontEntry* buttons = nullptr;
    if (auto it = fonts_.find(K("buttonsxenon")); it != fonts_.end()) buttons = &it->second;
    const font::Font& f = fe->f;
    const float k = h / std::max(1.0f, f.lineHeight), w = textWidth(font, text, h);
    const uint32_t ha = align & 0xF, va = align >> 4;
    float pen = (ha == 2 ? x - w * 0.5f : ha == 4 ? x - w : x) / k;
    const float top = (va == 2 ? y - h * 0.5f : va == 4 ? y - h : y) / k;
    const float baseY = top + (f.lineHeight - float(f.vShift));
    const std::u16string s = utf16(text);
    std::vector<Vertex> verts;
    auto glyph = [&](const font::Glyph& g, float gy) {
        const float gx = pen + float(g.xOffset);
        const Vertex a{gx * k, gy * k, 0, g.u0, g.v0, rgba[0], rgba[1], rgba[2], rgba[3]};
        const Vertex b{(gx + g.width) * k, gy * k, 0, g.u1, g.v0, rgba[0], rgba[1], rgba[2], rgba[3]};
        const Vertex cc{(gx + g.width) * k, (gy + g.height) * k, 0, g.u1, g.v1, rgba[0], rgba[1], rgba[2], rgba[3]};
        const Vertex d{gx * k, (gy + g.height) * k, 0, g.u0, g.v1, rgba[0], rgba[1], rgba[2], rgba[3]};
        verts.insert(verts.end(), {a, b, cc, a, cc, d});
    };
    for (size_t i = 0; i < s.size(); ++i) {
        const char16_t ch = s[i];
        if (ch == u'\\' && i + 2 < s.size() && s[i + 1] == u'b') {  // a button glyph: its own font, white, a line high [I]
            const int n = hexDigit(s[i + 2]);
            i += 2;
            if (!buttons || n < 0) continue;
            const font::Font& bf = buttons->f;
            const font::Glyph& g = bf.button(size_t(n));
            if (!verts.empty()) c.draw(verts.data(), verts.size(), int(fe->tex.id), Blend::Alpha), verts.clear();
            const float bk = h / std::max(1.0f, bf.lineHeight), x0 = pen * k, y0 = top * k;
            const Vertex a{x0, y0, 0, g.u0, g.v0, 255, 255, 255, rgba[3]}, b{x0 + g.width * bk, y0, 0, g.u1, g.v0, 255, 255, 255, rgba[3]};
            const Vertex cc{x0 + g.width * bk, y0 + g.height * bk, 0, g.u1, g.v1, 255, 255, 255, rgba[3]};
            const Vertex d{x0, y0 + g.height * bk, 0, g.u0, g.v1, 255, 255, 255, rgba[3]};
            const Vertex q[6] = {a, b, cc, a, cc, d};
            c.draw(q, 6, int(buttons->tex.id), Blend::Alpha);
            pen += g.width * bk / k;
            continue;
        }
        if (ch == u' ') {
            pen += f.spaceWidth;
            continue;
        }
        const font::Glyph& g = f.glyphFor(uint16_t(ch));
        glyph(g, baseY + g.baseline - g.height);
        pen += g.width + float(f.tracking);
    }
    if (!verts.empty()) c.draw(verts.data(), verts.size(), int(fe->tex.id), Blend::Alpha);
}

float UiSystem::textWidth(uint32_t font, const std::string& text, float h) const {
    const FontEntry* fe = fontFor(font);
    if (!fe || text.empty()) return 0;
    const font::Font& f = fe->f;
    const float k = h / std::max(1.0f, f.lineHeight);
    const FontEntry* buttons = nullptr;
    if (auto it = fonts_.find(K("buttonsxenon")); it != fonts_.end()) buttons = &it->second;
    const std::u16string s = utf16(text);
    float w = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == u'\\' && i + 2 < s.size() && s[i + 1] == u'b') {
            const int n = hexDigit(s[i + 2]);
            i += 2;
            if (buttons && n >= 0) w += buttons->f.button(size_t(n)).width * h / std::max(1.0f, buttons->f.lineHeight);
            continue;
        }
        w += (s[i] == u' ' ? f.spaceWidth : f.glyphFor(uint16_t(s[i])).width + float(f.tracking)) * k;
    }
    return w;
}

bool UiSystem::unionBounds(const Element& e, float& x0, float& y0, float& x1, float& y1) const {
    bool any = false;
    std::vector<const Element*> stack{&e};
    while (!stack.empty()) {
        const Element* n = stack.back();
        stack.pop_back();
        if (n->hidden) continue;
        if (n->drawnIn == drawCount_ && drawCount_) {
            if (!any) x0 = n->bx0, y0 = n->by0, x1 = n->bx1, y1 = n->by1;
            else x0 = std::min(x0, n->bx0), y0 = std::min(y0, n->by0), x1 = std::max(x1, n->bx1), y1 = std::max(y1, n->by1);
            any = true;
        }
        for (auto& c : n->children) stack.push_back(c.get());
    }
    return any;
}

bool UiSystem::drawnBounds(uint32_t id, float& x0, float& y0, float& x1, float& y1) const {
    const Element* e = find(id);
    return e && unionBounds(*e, x0, y0, x1, y1);
}

bool UiSystem::menuAt(float x, float y, bool focus, bool* isList) {
    Element* bestMenu = nullptr;
    int best = -1;
    float bestD = 1e30f;
    for (Element* m : focusedElements()) {
        if (!isMenu(*m) || m->hidden) continue;
        for (size_t i = 0; i < m->children.size(); ++i) {
            const Element& ch = *m->children[i];
            if (ch.notFocusable || ch.hidden) continue;
            float x0, y0, x1, y1;
            bool got = unionBounds(ch, x0, y0, x1, y1);
            // An item that draws nothing itself: the element its focus
            // handler highlights (the main menu's empty items name their
            // texts: {focus retail_menu_focus params = {id = ...}}) [C data; I use].
            for (const Element::Handler& h : ch.handlers) {
                if (got) break;
                if (h.event != K("focus")) continue;
                for (const char* name : {"id", "text_id"})
                    if (const qb::Value* v = qs::Vm::param(h.params, K(name)); v && v->type == qb::Type::Key && !got)
                        if (const Element* t = find(v->key); t && t != &ch) got = unionBounds(*t, x0, y0, x1, y1);
            }
            if (!got || x < x0 - 4 || x > x1 + 4 || y < y0 - 4 || y > y1 + 4) continue;
            // Overlapping entries: the one whose centre is nearest wins.
            const float dx = x - (x0 + x1) * 0.5f, dy = y - (y0 + y1) * 0.5f, d = dx * dx + dy * dy;
            if (d < bestD) bestD = d, best = int(i), bestMenu = m;
        }
    }
    if (!bestMenu) return false;
    // A list that scrolls (the setlist) takes clicks only: a hover would
    // scroll it. Every GH3 menu sits in a VScrollingMenu (new_menu), so a
    // long one counts as a list [I: more than 12 items].
    const bool list = bestMenu->parent && bestMenu->parent->type == kVScrollingMenu && bestMenu->children.size() > 12;
    if (isList) *isList = list;
    if (focus && best != bestMenu->selected) {
        if (bestMenu->selected >= 0 && size_t(bestMenu->selected) < bestMenu->children.size())
            launch(bestMenu->children[size_t(bestMenu->selected)].get(), K("unfocus"), qb::Struct{});
        bestMenu->selected = best;
        qb::Struct d;
        qs::Vm::set(d, K("controller"), qs::Vm::makeInt(bestMenu->controller));
        launch(bestMenu->children[size_t(best)].get(), K("focus"), d);
        vm_.run(K("generic_menu_up_or_down_sound"), qb::Struct{});
    }
    return true;
}

std::string UiSystem::helperAt(float x, float y) const {
    const Element* box = find(K("user_control_container"));
    if (!box) return {};
    for (const auto& c : box->children) {
        if (c->type != kText || c->text.empty() || c->drawnIn != drawCount_ || c->text[0] == '\\') continue;
        // The text and its button glyph on the left [I].
        if (x < c->bx0 - 40 || x > c->bx1 + 8 || y < c->by0 - 8 || y > c->by1 + 8) continue;
        std::string t = c->text;
        for (char& ch : t) ch = char(std::toupper(static_cast<unsigned char>(ch)));
        return t;
    }
    return {};
}

bool UiSystem::member(qs::ScriptRun& run, uint32_t obj, uint32_t method, qb::Struct& p, bool& handled) {
    Element* e = find(obj);
    handled = true;
    ++stats_.memberCalls;
    if (method == K("SetProps")) {
        if (e) applyProps(*e, p, false);
        return e != nullptr;
    }
    if (method == K("DoMorph")) {
        if (!e) return false;
        startMorph(*e, p);
        // A member morph waits for its end [I: glow loops have no Wait].
        if (e->morph.active) {
            const uint32_t id = obj;
            run.waitFor = [this, id] {
                Element* x = find(id);
                return !x || !x->morph.active;
            };
        }
        return true;
    }
    if (method == K("Die")) {
        destroy(e);
        return true;
    }
    if (method == K("Hide")) {
        if (e) e->hidden = true;
        return true;
    }
    if (method == K("Unhide")) {
        if (e) e->hidden = false;
        return true;
    }
    if (method == K("SetTags")) {
        if (e)
            for (const qb::Item& it : p.items) qs::Vm::set(e->tags, it.name, it.value);
        return e != nullptr;
    }
    if (method == K("GetTags")) {
        if (!e) return false;
        if (qb::Struct* l = locals(run))
            for (const qb::Item& it : e->tags.items) qs::Vm::set(*l, it.name, it.value);
        return true;
    }
    if (method == K("GetSingleTag")) {
        if (!e) return false;
        uint32_t key = 0;
        for (const qb::Item& it : p.items)
            if (it.name == 0 && it.value.type == qb::Type::Key) key = it.value.key;
        if (const qb::Value* v = qs::Vm::param(e->tags, key)) {
            if (qb::Struct* l = locals(run)) qs::Vm::set(*l, key, *v);
            return true;
        }
        return false;
    }
    handled = false;
    return false;
}

void UiSystem::addNatives() {
    using qs::ScriptRun;
    using qs::Vm;
    vm_.addNative("CreateScreenElement", [this](Vm&, ScriptRun& run, qb::Struct& p) {
        auto e = std::make_unique<Element>();
        static uint32_t anonymous = 0x80000000u;
        const qb::Value* type = Vm::param(p, K("type"));
        e->type = type ? type->key : K("containerelement");
        const qb::Value* id = Vm::param(p, K("id"));
        e->id = id && (id->type == qb::Type::Key || id->type == qb::Type::KeyPtr) ? id->key : anonymous++;
        Element* parent = resolve(Vm::param(p, K("parent")));
        if (!parent) parent = root_.get();
        e->parent = parent;
        if (Element* old = find(e->id)) destroy(old);  // ids are unique [I]
        Element* raw = e.get();
        byId_[e->id] = raw;
        parent->children.push_back(std::move(e));
        applyProps(*raw, p, true);
        ++stats_.created;
        // new_menu focuses its menu before the items exist: the first
        // focusable item added to a focused menu takes the focus [I].
        if (isMenu(*parent) && parent->focused && parent->selected < 0 && !raw->notFocusable) {
            parent->selected = int(parent->children.size()) - 1;
            qb::Struct d;
            Vm::set(d, K("controller"), Vm::makeInt(parent->controller));
            launch(raw, K("focus"), d);
        }
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("id"), Vm::makeKey(raw->id));
        return true;
    });
    vm_.addNative("DestroyScreenElement", [this](Vm&, ScriptRun&, qb::Struct& p) {
        Element* e = resolve(Vm::param(p, K("id")));
        if (!e) return false;
        if (e == root_.get() || Vm::flag(p, K("dont_recurse"))) {
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
        applyProps(*e, p, false);
        return true;
    });
    vm_.addNative("DoScreenElementMorph", [this](Vm&, ScriptRun&, qb::Struct& p) {
        Element* e = resolve(Vm::param(p, K("id")));
        if (!e) return false;
        startMorph(*e, p);
        return true;
    });
    vm_.addNative("GetScreenElementDims", [this](Vm&, ScriptRun& run, qb::Struct& p) {
        Element* e = resolve(Vm::param(p, K("id")));
        if (!e) return false;
        if (isMenu(*e)) layoutMenu(*e);
        if (qb::Struct* l = locals(run)) {
            // int(dims x own scale) [C 0x82172310].
            Vm::set(*l, K("width"), Vm::makeInt(int32_t(e->w * e->sx)));
            Vm::set(*l, K("height"), Vm::makeInt(int32_t(e->h * e->sy)));
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
    vm_.addNative("GetScreenElementProps", [this](Vm&, ScriptRun& run, qb::Struct& p) {
        Element* e = resolve(Vm::param(p, K("id")));
        if (!e) return false;
        if (qb::Struct* l = locals(run)) {
            qb::Value pos;
            pos.type = qb::Type::Pair;
            pos.f[0] = e->x, pos.f[1] = e->y;
            Vm::set(*l, K("pos"), pos);
            if (e->sx == e->sy) {
                Vm::set(*l, K("scale"), Vm::makeFloat(e->sx));
            } else {
                qb::Value sc;
                sc.type = qb::Type::Pair;
                sc.f[0] = e->sx, sc.f[1] = e->sy;
                Vm::set(*l, K("scale"), sc);
            }
            Vm::set(*l, K("alpha"), Vm::makeFloat(e->alpha));
            Vm::set(*l, K("rot_angle"), Vm::makeFloat(e->rot));
            Vm::set(*l, K("z_priority"), Vm::makeFloat(e->z));
            qb::Value rgba;
            rgba.type = qb::Type::Array;
            rgba.arr = std::make_shared<qb::Array>();
            rgba.arr->type = qb::Type::Int;
            for (uint8_t b : e->rgba) rgba.arr->values.push_back(Vm::makeInt(b));
            Vm::set(*l, K("rgba"), rgba);
            if (e->texture) Vm::set(*l, K("texture"), Vm::makeKey(e->texture));
        }
        return true;
    });
    vm_.addNative("GetScreenElementChildren", [this](Vm&, ScriptRun& run, qb::Struct& p) {
        Element* e = resolve(Vm::param(p, K("id")));
        if (!e) return false;
        qb::Value arr;
        arr.type = qb::Type::Array;
        arr.arr = std::make_shared<qb::Array>();
        arr.arr->type = qb::Type::Key;
        for (auto& ch : e->children) arr.arr->values.push_back(Vm::makeKey(ch->id));
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("children"), arr);
        return !e->children.empty();
    });
    vm_.addNative("ResolveScreenElementID", [this](Vm&, ScriptRun& run, qb::Struct& p) {
        Element* e = resolve(Vm::param(p, K("id")));
        if (!e) return false;
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("resolved_id"), Vm::makeKey(e->id));
        return true;
    });
    vm_.addNative("LaunchEvent", [this](Vm&, ScriptRun&, qb::Struct& p) {
        const qb::Value* type = Vm::param(p, K("type"));
        Element* target = resolve(Vm::param(p, K("target")));
        if (!type || !target) return false;
        qb::Struct data;
        if (const qb::Value* d = Vm::param(p, K("data")); d && d->st) data = *d->st;
        launch(target, type->key, data);
        return true;
    });
    vm_.addNative("RunScriptOnScreenElement", [this](Vm& v, ScriptRun&, qb::Struct& p) {
        Element* e = resolve(Vm::param(p, K("id")));
        uint32_t script = 0;
        for (const qb::Item& it : p.items)
            if (it.name == 0 && (it.value.type == qb::Type::Key || it.value.type == qb::Type::KeyPtr)) script = it.value.key;
        if (!e || !script) return false;
        qb::Struct params;
        if (const qb::Value* ps = Vm::param(p, K("params")); ps && ps->st) params = *ps->st;
        Vm::set(params, K("id"), Vm::makeKey(e->id));
        v.spawnOn(script, params, e->id);
        return true;
    });
    vm_.addNative("TerminateObjectsScripts", [this](Vm& v, ScriptRun&, qb::Struct& p) {
        Element* e = resolve(Vm::param(p, K("id")));
        if (!e) return false;
        for (auto& r : v.runs())
            if (r->object == e->id) r->finished = true;
        return true;
    });
    // SetScreenElementLock id on|off: on suspends the menu relayout, off relayouts [C flag 0x4000].
    vm_.addNative("SetScreenElementLock", [this](Vm&, ScriptRun&, qb::Struct& p) {
        Element* e = resolve(Vm::param(p, K("id")));
        if (!e) return false;
        e->locked = Vm::flag(p, K("on"));
        if (!e->locked && isMenu(*e)) layoutMenu(*e);
        return true;
    });
}

}  // namespace ui
