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
#include "ui/ui_movies.h"

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

// A base-32 digit (0-9, a-v): button glyph and colour numbers [C 0x8228E0B0 writes them so].
int digit32(char16_t c) {
    if (c >= u'0' && c <= u'9') return c - u'0';
    if (c >= u'a' && c <= u'v') return c - u'a' + 10;
    if (c >= u'A' && c <= u'V') return c - u'A' + 10;
    return -1;
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
    // gh4's menu fonts (fontgrid_text_a6, fontgrid_title_a1, buttons_x360,
    // ...): without them every menu fell back to text_a1 [C data ui_shared.pak].
    loadFonts("DATA/COMPRESSED/PAK/ui_shared.pak.xen");
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
        const auto pakShared = Pak::shared(disc_, pakPath);
        const Pak& pak = *pakShared;
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

// The buttons font: GH3's buttonsxenon, gh4's buttons_x360 (ui_shared.pak) [C data].
const UiSystem::FontEntry* UiSystem::buttonsFont() const {
    for (const char* n : {"buttonsxenon", "buttons_x360"})
        if (auto it = fonts_.find(K(n)); it != fonts_.end()) return &it->second;
    return nullptr;
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
    if (it != byId_.end()) return it->second;
    // gh4: an alias (AssignAlias id = <element> alias = <name>) [C scripts].
    auto a = aliases_.find(id);
    if (a == aliases_.end() || a->second == id) return nullptr;
    it = byId_.find(a->second);
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
    const uint32_t ctx = assetContexts_.empty() ? 0 : assetContexts_.back();
    e.texContext = tex_.inContext(ctx, key) ? ctx : 0;
    int tw = 0, th = 0;
    tex_.size(e.texContext, key, tw, th);
    if (!tw) {
        ++stats_.missingTextures;
        return;
    }
    // GH3: on texture set, base dims = texture size and given dims became a
    // scale [C GH3 0x82218F80]. gh4 (Metallica): given dims stay the size and
    // scale stays the scale (the descs give both, dims (1280, 720) on a
    // 1024 x 512 picture with scale (1, 1), and lay children out in those
    // dims) [I: D6]; without dims the texture's size.
    if (!(e.dimsGiven && e.w > 0 && e.h > 0)) e.w = float(tw), e.h = float(th);
}

void UiSystem::applyProps(Element& e, const qb::Struct& p, bool create) {
    (void)create;
    e.lastBefore = {true, e.x, e.y, e.sx, e.sy, e.alpha, e.rot};
    // gh4's menuelement: GH3's vmenu, or its hmenu with isvertical = false
    // (desc menus, CreateScreenElement type = menuelement) [C data uidesc_*; I].
    if (e.type == K("menuelement")) e.type = kVMenu;
    if (const qb::Value* v = qs::Vm::param(p, K("isvertical")); v && (e.type == kVMenu || e.type == kHMenu))
        e.type = (v->type == qb::Type::Key ? v->key != K("false") : v->number() != 0) ? kVMenu : kHMenu;
    float newDimsW = -1, newDimsH = -1;
    uint32_t newTexture = 0;
    bool haveScale = false;
    float scaleX = 1, scaleY = 1;
    // Every props call sets the flips anew: flip_h / flip_v given, or kept
    // with preserve_flip, else cleared [C 0x8221C790: flags 0x2000000 /
    // 0x4000000] (the setlist's first page, flipped by displaySprite, is
    // unflipped by its later z_priority call).
    if (!qs::Vm::flag(p, K("preserve_flip"))) e.flipH = e.flipV = false;
    for (const qb::Item& it : p.items) {
        const qb::Value& v = it.value;
        const uint32_t n = it.name;
        // An unset local passed on (displaySprite's `z_priority = <z>` without
        // z) sets nothing: the element keeps its parent's z + 1 [I: only so is
        // the select-difficulty highlight over its backdrop].
        if (n != 0 && v.type == qb::Type::None) continue;
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
            else if (f == K("shadow")) e.shadow = true;
            else if (f == K("no_shadow") || f == K("noshadow")) e.shadow = false;
            // A scrolling menu's window back to its menu's top [C 0x82317348].
            else if (f == K("reset_window_top")) e.scroll = 0, e.scrollDirty = true;
            else if (f == K("reset_window")) e.scrollDirty = true;
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
                else if (qs::Vm::flag(*v.st, K("absolute"))) absoluteToLocal(e, a, b, e.x, e.y);
                else e.x = a, e.y = b;
            } else {
                readPair(v, e.x, e.y);
            }
        } else if (n == K("dims")) {
            if (v.type == qb::Type::Pair) newDimsW = v.f[0], newDimsH = v.f[1];
        } else if (n == K("scale")) {
            // A number or a pair; a vector is not one (left as it was):
            // create_choose_practice_part_menu's GUITAR / BASS texts pass
            // scale = (1.0, 0.0, 1.0) and show at scale 1 [C 0x8221A738:
            // FUN_821f7028 (int / float) else FUN_821f7380 (pair, type 5)].
            haveScale = v.type != qb::Type::Vector && readPair(v, scaleX, scaleY);
        } else if (n == K("relative_scale")) {
            float a = 1, b = 1;
            if (readPair(v, a, b)) e.sx *= a, e.sy *= b;
        } else if (n == K("just")) readJust(v, e.justX, e.justY);
        else if (n == K("internal_just")) readJust(v, e.ijustX, e.ijustY);
        else if (n == K("child_anchor")) readJust(v, e.anchorX, e.anchorY);
        else if (n == K("pos_anchor")) readJust(v, e.posAnchorX, e.posAnchorY), e.posAnchorGiven = true;
        else if (n == K("hiddenlocal")) e.hidden = v.type == qb::Type::Key ? v.key == K("true") : v.number() != 0;
        else if (n == K("rgba") && v.type == qb::Type::Array && v.arr) {
            for (size_t k = 0; k < 4 && k < v.arr->values.size(); ++k) e.rgba[k] = uint8_t(std::clamp(int(v.arr->values[k].number()), 0, 255));
        } else if (n == K("alpha")) e.alpha = v.number();
        else if (n == K("z_priority")) e.z = v.number(), e.zGiven = true;
        else if (n == K("rot_angle")) e.rot = v.number();
        else if (n == K("relative_rot_angle")) e.rot += v.number();
        else if (n == K("texture")) newTexture = v.key;
        else if (n == K("blend") && (v.type == qb::Type::Key || v.type == qb::Type::KeyPtr))
            e.blend = v.key == K("add") ? 1 : (v.key == K("subtract") || v.key == K("sub")) ? 2 : 0;
        else if (n == K("font")) e.font = v.key;
        else if (n == K("fit_width") && (v.type == qb::Type::Key || v.type == qb::Type::KeyPtr))
            e.fitW = v.key == K("expand dims") ? 1 : v.key == K("scale each line if larger") ? 2 : v.key == K("scale each line to fit") ? 3 : 0;
        else if (n == K("fit_height") && (v.type == qb::Type::Key || v.type == qb::Type::KeyPtr))
            e.fitH = v.key == K("clip bottom lines") ? 1 : v.key == K("clip top lines") ? 2 : v.key == K("expand dims") ? 3
                   : v.key == K("scale down if larger") ? 4 : v.key == K("scale to fit") ? 5 : 0;
        else if (n == K("scale_mode") && (v.type == qb::Type::Key || v.type == qb::Type::KeyPtr)) e.scalePerAxis = v.key == K("per axis");
        else if ((n == K("fit_major") || n == K("fit_minor")) && (v.type == qb::Type::Key || v.type == qb::Type::KeyPtr))
            (n == K("fit_major") ? e.fitMajor : e.fitMinor) = (v.key == K("size dims to content") || v.key == K("fit content")) ? 1
                : (v.key == K("fit content if larger") || v.key == K("expand if content larger")) ? 2 : 0;
        else if (n == K("text_case") && (v.type == qb::Type::Key || v.type == qb::Type::KeyPtr)) e.upperCase = v.key == K("upper");
        else if (n == K("text")) e.text = setText(v.str);
        else if (n == K("shadow_offs")) readPair(v, e.shadowX, e.shadowY);
        else if (n == K("shadow_rgba") && v.type == qb::Type::Array && v.arr) {
            for (size_t k = 0; k < 4 && k < v.arr->values.size(); ++k) e.shadowRgba[k] = uint8_t(std::clamp(int(v.arr->values[k].number()), 0, 255));
        } else if (n == K("internal_scale")) {
            float a = 1, b = 1;
            if (readPair(v, a, b)) e.internalScale = a;
        }
        else if (n == K("local_id")) e.localId = v.key;
        else if (n == K("exclusive_device")) {
            e.exclusiveMask = 0, e.exclusiveDevice = -1;
            if (v.type == qb::Type::Array && v.arr) {
                for (const qb::Value& d : v.arr->values)
                    if (int i = int(d.number()); i >= 0 && i < 32) e.exclusiveMask |= 1u << i;
            } else {
                e.exclusiveDevice = int(v.number());
            }
        }
        else if (n == K("font_spacing")) e.fontSpacing = v.number(), e.fontSpacingGiven = true;
        else if (n == K("space_spacing")) e.spaceSpacing = v.number();
        else if (n == K("line_spacing")) e.lineSpacing = v.number();
        else if (n == K("regular_space_amount")) e.regularSpace = v.number();
        else if (n == K("padding_scale")) e.paddingScale = v.number();
        else if (n == K("spacing_between")) e.spacingBetween = v.number();
        else if (n == K("scroll_speed")) e.scrollSpeed = float(int(v.number()));  // [C 0x82317348]
        else if (n == K("num_items_to_show")) e.numItemsToShow = int(v.number()), e.scrollDirty = true;
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
                    } else if (hi.name == 0 && hi.value.type == qb::Type::Struct && hi.value.st && unnamed == 1) {
                        // `{ pad_choose ( <option> . func ) }` with func a struct
                        // `{ script params = {...} }` (the popups' options:
                        // create_popup_warning_menu_option) [C data; I: read as
                        // the handler's script and params].
                        for (const qb::Item& fi : hi.value.st->items) {
                            if (fi.name == 0 && (fi.value.type == qb::Type::Key || fi.value.type == qb::Type::KeyPtr) && !hd.script) hd.script = fi.value.key;
                            else if (fi.name == K("params") && fi.value.st) hd.params = *fi.value.st;
                        }
                        ++unnamed;
                    }
                }
                e.handlers.push_back(std::move(hd));
            }
        }
    }
    if (newDimsW >= 0) {
        e.w = newDimsW, e.h = newDimsH, e.givenH = newDimsH, e.givenW = newDimsW, e.dimsGiven = true;
        // GH3 turned new dims on a textured sprite into a scale [C GH3
        // 0x82219C78]; gh4 keeps them as the size (D6).
    }
    if (newTexture) setTexture(e, newTexture);
    if (haveScale) {
        // An explicit scale applies after dims [I order].
        if (e.type == kSprite && e.texture && newDimsW >= 0) e.sx *= scaleX, e.sy *= scaleY;
        else e.sx = scaleX, e.sy = scaleY;
    }
    if (e.upperCase && !e.text.empty()) e.text = upperText(e.text);
    if (e.type == kText || e.type == kTextBlock) measureText(e);
}

float UiSystem::textWidth(const FontEntry& fe, const std::u16string& s, const Element& e) const {
    // [C 0x8232ADF0]: a glyph adds its width - bearing, a space the font's
    // space width; each then adds the tracking (font_spacing, if given,
    // replaces the tracking and drops the bearing). Button glyphs
    // ("\b<n>", "\s<n>") measure like glyphs; "\c", "\u" (3 chars) and
    // "\L" (2) take no room.
    const font::Font& f = fe.f;
    const FontEntry* buttons = nullptr;
    buttons = buttonsFont();
    const float bearing = e.fontSpacingGiven ? 0.0f : float(f.bearing);
    const float track = e.fontSpacingGiven ? float(int(e.fontSpacing)) : float(f.tracking);
    float w = 0, best = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const font::Glyph* g = nullptr;
        if (s[i] == u'\\' && i + 1 < s.size() && s[i + 1] != u'\\') {
            const char16_t code = s[i + 1];
            if (code == u'n') {  // the port's line break (GH3 text elements have none)
                best = std::max(best, w), w = 0, ++i;
                continue;
            }
            if (code == u'c' || code == u'C' || code == u'u' || code == u'U') {
                i += 2;
                continue;
            }
            if (code == u'L') {
                ++i;
                continue;
            }
            if ((code == u'b' || code == u'B' || code == u's' || code == u'S') && i + 2 < s.size()) {
                const int n = digit32(s[i + 2]);
                i += 2;
                const FontEntry* src = (code == u's' || code == u'S') ? &fe : buttons;
                if (!src || n < 0 || n >= 32) continue;
                g = &src->f.button(size_t(n));
            } else {
                ++i;  // an unknown code: the character after it is drawn
            }
        } else if (s[i] == u'\\' && i + 1 < s.size()) {
            ++i;  // "\\\\": one backslash
        }
        if (!g && s[i] == u' ') w += f.spaceWidth + e.spaceSpacing + track;
        else w += (g ? g->width : f.glyphFor(uint16_t(s[i])).width) - bearing + track;
    }
    return std::max(best, w);
}

std::vector<std::u16string> UiSystem::blockLines(const FontEntry& fe, const Element& e) const {
    // Words (each with its trailing space) fill a line while it stays within
    // the dims' width (unscaled, times internal_scale); "\n" breaks; a
    // line's last space is dropped; a colour code carries over [C 0x8231B260].
    std::vector<std::u16string> lines(1);
    const std::u16string s = utf16(e.text);
    if (e.fitW != 0) {
        // Not wrap: "\\n" only breaks.
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == u'\\' && i + 1 < s.size() && s[i + 1] == u'n') lines.emplace_back(), ++i;
            else lines.back().push_back(s[i]);
        }
        return lines;
    }
    float lineW = 0;
    std::u16string colour;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == u'\\' && i + 1 < s.size() && s[i + 1] == u'n') {
            lines.emplace_back(colour), lineW = 0, i += 2;
            continue;
        }
        size_t j = i;
        while (j < s.size() && s[j] != u' ' && !(s[j] == u'\\' && j + 1 < s.size() && s[j + 1] == u'n')) j += (s[j] == u'\\' && j + 1 < s.size()) ? 2 : 1;
        while (j < s.size() && s[j] == u' ') ++j;
        const std::u16string word = s.substr(i, j - i);
        const float ww = textWidth(fe, word, e) * e.internalScale;
        if (lineW > 0 && e.w > 0 && e.w < lineW + ww) {
            if (!lines.back().empty() && lines.back().back() == u' ') lines.back().pop_back();
            lines.emplace_back(colour), lineW = 0;
        }
        lines.back() += word, lineW += ww;
        for (size_t k = 0; k + 2 < word.size(); ++k)
            if (word[k] == u'\\' && (word[k + 1] == u'c' || word[k + 1] == u'C')) colour = word.substr(k, 3);
        i = j;
    }
    if (!lines.back().empty() && lines.back().back() == u' ') lines.back().pop_back();
    return lines;
}

UiSystem::BlockFit UiSystem::blockFit(const FontEntry& fe, const Element& e) const {
    BlockFit b;
    b.lines = blockLines(fe, e);
    const float is = e.internalScale;
    const float lineH = fe.f.lineHeight * e.lineSpacing * is;
    // fit_height: clipping keeps the lines that fit (one at least) [I].
    if ((e.fitH == 1 || e.fitH == 2) && e.h > 0 && lineH > 0) {
        const size_t keep = std::max<size_t>(1, size_t(e.h / lineH + 0.01f));
        if (b.lines.size() > keep) {
            if (e.fitH == 1) b.lines.resize(keep);
            else b.lines.erase(b.lines.begin(), b.lines.end() - long(keep));
        }
    }
    // The block's scale: scale down if larger / scale to fit [I].
    const float total = lineH * float(b.lines.size());
    if (e.h > 0 && total > 0 && (e.fitH == 5 || (e.fitH == 4 && total > e.h))) {
        b.sy = e.h / total;
        if (!e.scalePerAxis) b.sx = b.sy;
    }
    // Each line's x scale: scale each line if larger / to fit [I].
    for (const std::u16string& l : b.lines) {
        float k = 1;
        const float lw = textWidth(fe, l, e) * is * b.sx;
        if (e.w > 0 && lw > 0 && (e.fitW == 3 || (e.fitW == 2 && lw > e.w))) k = e.w / lw;
        b.lineScale.push_back(k);
    }
    return b;
}

// text_case = upper: letters up, escape codes ("\\n", "\\c1", "\\b0") kept.
std::string UiSystem::upperText(const std::string& in) {
    std::string out = in;
    for (size_t i = 0; i < out.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(out[i]);
        if (ch == '\\') {
            ++i;  // the code letter stays
            continue;
        }
        if (ch >= 'a' && ch <= 'z') out[i] = char(ch - 32);
        // UTF-8 Latin-1 letters a-grave .. y-thorn (not the division sign) [I].
        else if (ch == 0xC3 && i + 1 < out.size()) {
            const unsigned char c2 = static_cast<unsigned char>(out[i + 1]);
            if (c2 >= 0xA0 && c2 <= 0xBE && c2 != 0xB7) out[i + 1] = char(c2 - 0x20);
            ++i;
        }
    }
    return out;
}

std::string UiSystem::setText(const std::string& in) const {
    if (in.find('\\') == std::string::npos) return in;
    std::string out;
    const qb::Value* map = vm_.global(K("meta_button_map"));
    // gh4: an array of {name map} (guitar, drum, pad); the default name's
    // map (meta_button_map_default_name = guitar) [C data; I: the element's
    // device picks another when metaremapenableauto is on].
    if (map && map->arr && !map->arr->values.empty() && map->arr->values[0].st) {
        const qb::Value* want = vm_.global(K("meta_button_map_default_name"));
        const qb::Value* pick = nullptr;
        for (const qb::Value& m : map->arr->values)
            if (m.st) {
                const qb::Value* nm = m.st->find(K("name"));
                if (!pick || (want && nm && nm->key == want->key)) pick = m.st->find(K("map"));
                if (want && nm && nm->key == want->key) break;
            }
        map = pick;
    }
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '\\' || i + 1 >= in.size()) {
            out += in[i];
            continue;
        }
        const char c = in[i + 1];
        if (c == '\\') {
            out += "\\\\", ++i;
        } else if (c == '_') {
            out += ' ', ++i;
        } else if ((c == 'm' || c == 'M') && i + 2 < in.size()) {
            // meta_button_map[n] = [ps2?, xenon] pairs: the second is the
            // buttons font's glyph, written as a base-32 digit [C 0x8228E0B0].
            const int n = digit32(char16_t(in[i + 2]));
            int glyph = 0;
            if (map && map->type == qb::Type::Array && map->arr && n >= 0 && size_t(n) < map->arr->values.size()) {
                const qb::Value& p = map->arr->values[size_t(n)];
                if (p.type == qb::Type::Array && p.arr && !p.arr->values.empty()) glyph = int(p.arr->values[p.arr->values.size() > 1 ? 1 : 0].number());
            }
            out += "\\b";
            out += char(glyph < 10 ? '0' + glyph : 'a' + glyph - 10);
            i += 2;
        } else {
            out += in[i];
        }
    }
    return out;
}

void UiSystem::measureText(Element& e) const {
    // Base dims = the measured text [C 0x8232ADF0]; a text block keeps its
    // dims, growing in height to its lines with allow_expansion [I].
    const FontEntry* fe = fontFor(e.font);
    if (!fe) return;
    if (e.type == kTextBlock) {
        // fit_width = expand dims: the dims grow to the widest line [I].
        if (e.fitW == 1) {
            float wmax = 0;
            for (const std::u16string& l : blockLines(*fe, e)) wmax = std::max(wmax, textWidth(*fe, l, e) * e.internalScale);
            e.w = std::max(e.givenW, wmax);
        }
        if (!e.allowExpansion && e.fitH != 3) return;
        const float lh = fe->f.lineHeight * e.lineSpacing * e.internalScale;
        e.h = std::max(e.givenH, lh * float(blockLines(*fe, e).size()));
        return;
    }
    // gh4: a text element's internal_scale scales its text and dims
    // (add_menu_frontend_item's items, 0.7) [I].
    e.w = textWidth(*fe, utf16(e.text), e) * e.internalScale;
    e.h = fe->f.lineHeight * e.internalScale;
}

void UiSystem::forget(Element* e) {
    if (e->id) {
        auto it = byId_.find(e->id);
        if (it != byId_.end() && it->second == e) byId_.erase(it);
    }
    // Scripts running on the element die with it [I].
    for (auto& r : vm_.runs())
        if (r->object == e->id && !r->handler) r->finished = true;
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

void UiSystem::resolvePendingFocus(uint32_t menuId) {
    auto it = std::find(pendingFocus_.begin(), pendingFocus_.end(), menuId);
    if (it == pendingFocus_.end()) return;
    pendingFocus_.erase(it);
    Element* m = find(menuId);
    if (!m || !isMenu(*m) || !m->focused || m->selected >= 0) return;
    for (size_t i = 0; i < m->children.size(); ++i) {
        if (m->children[i]->notFocusable) continue;
        m->selected = int(i);
        qb::Struct d;
        qs::Vm::set(d, K("controller"), qs::Vm::makeInt(m->controller));
        launch(m->children[i].get(), K("focus"), d);
        return;
    }
}

bool UiSystem::launch(Element* target, uint32_t event, const qb::Struct& data) {
    if (!target) return false;
    if (!pendingFocus_.empty() && isMenu(*target)) resolvePendingFocus(target->id);
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
        // The handler's own `id` param wins (the main menu's items name the
        // text to recolour: `{focus retail_menu_focus params = {id = ...}}`) [C data].
        if (!qs::Vm::param(params, K("id"))) qs::Vm::set(params, K("id"), qs::Vm::makeKey(id));
        for (const qb::Item& it : data.items) qs::Vm::set(params, it.name, it.value);
        ++stats_.handlersRun;
        handled = true;
        bool r = false;
        vm_.run(h.script, params, &r, id);  // the element is the handler's object [I: options_menu_hilight's gettags]
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
        if (event == K("focus")) {
            // One focused item among its siblings for the same player: a focus
            // moved by script (venue_scroll_up / _down's LaunchEvent focus,
            // with no unfocus) takes it from the item before, which hears its
            // unfocus. Left focused, each pad event reached every venue item
            // and the list jumped [I: the C's per-parent focus].
            if (target->parent && !isMenu(*target->parent)) {
                std::vector<uint32_t> was;
                for (const auto& c : target->parent->children)
                    if (c.get() != target && c->focused && !isMenu(*c) && c->exclusiveDevice == target->exclusiveDevice &&
                        c->exclusiveMask == target->exclusiveMask)
                        was.push_back(c->id);
                for (uint32_t w : was)
                    if (Element* e = find(w)) {
                        e->focused = false;
                        launch(e, K("unfocus"), qb::Struct{});
                    }
            }
            if (Element* t2 = find(id)) t2->focused = true;
        } else if (event == K("unfocus")) {
            target->focused = false;
        }
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
        if (e->exclusiveMask ? (device >= 0 && device < 32 && (e->exclusiveMask >> device & 1)) : (e->exclusiveDevice < 0 || e->exclusiveDevice == device))
            ids.push_back(e->id);
    // root_window gets pad_start (the pause handler lives there) [C data].
    if (event == K("pad_start") && std::find(ids.begin(), ids.end(), kRootWindow) == ids.end()) ids.push_back(kRootWindow);
    if (const char* v = SDL_getenv("GD10_UILOG"); v && *v && *v != '0') {
        std::string who;
        for (uint32_t id : ids) {
            who += " " + qbkey::name(id);
            if (const Element* e = find(id); e && !e->handlers.empty()) who += "(" + std::to_string(e->handlers.size()) + " handlers)";
        }
        SDL_Log("ui: %s (device %d) ->%s", qbkey::name(event).c_str(), device, who.c_str());
    }
    for (uint32_t id : ids)
        if (Element* e = find(id)) launch(e, event, data);
}

bool UiSystem::focusElement(uint32_t id) {
    Element* e = find(id);
    if (!e) return false;
    launch(e, K("focus"), qb::Struct{});
    return true;
}

bool UiSystem::startFinishesEntry(int device) const {
    // GH3's text entries take Start on the element that has the focus: the
    // band name (create_enter_band_name_menu: pad_start confirm_band_name),
    // the top rockers' name (menu_top_rockers_init_band_name_creation:
    // pad_start menu_tr_confirm_band_name) and a new cheat (cheats_zoomin_guitar:
    // pad_start new_cheat_finished) [C scripts]. root_window's pause handler
    // and the gamercard / null handlers are not entries [C scripts
    // setup_pause, create_character_hub_menu].
    static const uint32_t notEntry[] = {K("menu_show_gamercard"), K("show_gamercard"), K("menu_show_gamercard_from_netid"),
                                        K("null_script"), K("gh3_start_pressed")};
    for (const Element* e : focusedElements()) {
        if (e->id == kRootWindow) continue;
        const bool mine = e->exclusiveMask ? (device >= 0 && device < 32 && (e->exclusiveMask >> device & 1))
                                           : (e->exclusiveDevice < 0 || e->exclusiveDevice == device);
        if (!mine) continue;
        for (const Element::Handler& h : e->handlers)
            if (h.event == K("pad_start") && std::find(std::begin(notEntry), std::end(notEntry), h.script) == std::end(notEntry)) return true;
    }
    return false;
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
        if (vert) {
            // GH3 lays a menu out on its first-update event only [C 0x823164E0
            // on 0x3056434f; 0x8221D2A0 posts it, SetParent / Die re-arm it]:
            // a child's later text (the store's character name, set by
            // store_show_character after the first one, Slash) keeps the
            // width, so a centred item stays centred where the first
            // layout put it (menu_pos (845, 85) + Slash / 2 = the frame's
            // centre) instead of moving with each name's width (GD10-099).
            const Element* last = m.children.empty() ? nullptr : m.children.back().get();
            if (m.layoutOnce || m.children.size() != m.widthKids || last != m.widthLast)
                m.w = widest, m.widthKids = m.children.size(), m.widthLast = last;
            m.h = total;
        } else {
            m.w = total, m.h = widest;
        }
    } else if (m.fitMajor || m.fitMinor) {
        // gh4: the dims follow the content along (major) / across (minor)
        // when the desc asks, from the given dims [I].
        auto fit = [](uint8_t mode, float given, float content) { return mode == 1 ? content : mode == 2 ? std::max(given, content) : given; };
        float& along = vert ? m.h : m.w;
        float& across = vert ? m.w : m.h;
        along = fit(m.fitMajor, vert ? m.givenH : m.givenW, total);
        across = fit(m.fitMinor, vert ? m.givenW : m.givenH, widest);
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
        // The place is from the menu's top-left; a child anchored elsewhere
        // (gh4 descs' pos_anchor [0 0]: the helper pills) is moved to match [I].
        const float anX = c.posAnchorGiven ? c.posAnchorX : m.anchorX, anY = c.posAnchorGiven ? c.posAnchorY : m.anchorY;
        c.x -= (anX + 1) / 2 * m.w, c.y -= (anY + 1) / 2 * m.h;
    }
}

// Lays out the menus at and below e, innermost first (a menu's fitted dims
// depend on its children's).
void UiSystem::layoutTree(Element& e) {
    for (auto& c : e.children) layoutTree(*c);
    if (isMenu(e) && !e.locked) layoutMenu(e);
}

void UiSystem::scrollMenu(Element& s) {
    // GH3's VScrollingMenu, every update [C 0x82317640]: its menu (the last
    // vmenu child, 0x823174F8) is moved to (x, -scroll) so the focused item
    // (the first one before any focus) lies in the window; it moves only when
    // the item leaves the window. Items outside the window get alpha 0, the
    // others 1, whenever the window is refreshed (first update, a focus
    // change, a child added or gone) [C FUN_8221a418 from 0x82317A4C].
    // The menu lays its items out on its own first-update event, which comes
    // after this update [C 0x8221D2A0 posts 0x3056434f, the VMenu lays out on
    // it in 0x823164E0]: the window first sees the first item where the
    // script made it (pos (0,0), centred = top -h/2) and keeps that offset,
    // so new_menu's lists sit half their first item lower than the menu's
    // pos (the pause menu's 100-high containers 50 px, the store's / cheats'
    // texts so their scripted highlight bars fall on them).
    Element* m = nullptr;
    for (auto& c : s.children)
        if (c->type == kVMenu) m = c.get();
    if (!m) return;
    if (m->children.empty()) return;
    const float itemSize = (m->regularSpace > 0.0001f && s.numItemsToShow >= 0) ? m->regularSpace : -1.0f;  // [C 0x82315918]
    const float window = itemSize >= 0 ? float(s.numItemsToShow) * itemSize : s.h;  // [C 0x823174F8]
    const int count = int(m->children.size());
    const int idx = (m->selected >= 0 && m->selected < count) ? m->selected : 0;
    if (m->children.size() != s.scrollKids) s.scrollDirty = s.scrollRelayout = true;
    if (idx != s.scrollSel) s.scrollDirty = true;
    s.scrollSel = idx, s.scrollKids = m->children.size();
    // The item's top and height in the menu [C 0x82317158].
    auto box = [&](const Element& it, int i, float& top, float& h) {
        if (itemSize >= 0) {
            top = float(i) * itemSize, h = itemSize;
            return;
        }
        h = it.h * m->sy * it.sy;
        top = it.y * m->sy - (it.justY + 1) / 2 * h;
    };
    float d16 = 0, d17 = 0;
    if (itemSize >= 0) {
        const int half = s.numItemsToShow / 2;
        if (half <= idx) d16 = float((s.numItemsToShow - 1) / 2) * itemSize;
        if (idx < count - half) d17 = float(half) * itemSize;
    }
    // The menu's just becomes [left top], its pos kept in place [C 0x8221B788].
    if (m->justX != -1 || m->justY != -1) {
        const float dx = (-1 - m->justX) * m->w * m->sx * 0.5f, dy = (-1 - m->justY) * m->h * m->sy * 0.5f;
        const float r = m->rot * kDeg, cs = std::cos(r), sn = std::sin(r);
        m->x += cs * dx - sn * dy, m->y += sn * dx + cs * dy;
        m->justX = m->justY = -1;
    }
    float top = 0, h = 0;
    box(*m->children[size_t(idx)], idx, top, h);
    // A first item that cannot take the focus shows above the second [C].
    float d13 = 0;
    if (idx == 1 && (m->children[0]->notFocusable || m->children[0]->hidden)) d13 = h;
    if (s.scroll + d16 <= top - d13) {
        if (top + h > s.scroll + window - d17) {
            s.scroll += s.scrollSpeed;
            if (top + h < s.scroll + window) {
                const float step = m->spacingBetween + h;
                if (step > 0) s.scroll = top - float(int(window / step) - 1) * step + d17;
            }
            s.scrollDirty = true;
        }
    } else {
        s.scroll -= s.scrollSpeed;
        if (s.scroll - d13 < top) s.scroll = top - d16 - d13;
        s.scrollDirty = true;
    }
    if (!s.scrollDirty) return;
    s.scrollDirty = false;
    m->y = -s.scroll;
    for (int i = 0; i < count; ++i) {
        Element& it = *m->children[size_t(i)];
        float t = 0, ih = 0;
        box(it, i, t, ih);
        it.alpha = (s.scroll <= t && t + ih <= window + s.scroll) ? 1.0f : 0.0f;
    }
}

// se_setprops with time: applyProps has already set the end values; undo
// those of the morphable props and morph to them instead.
void UiSystem::morphFromSetProps(Element& e, const qb::Struct& p) {
    (void)p;
    if (!e.lastBefore.valid) return;
    qb::Struct target;
    const Element::Snapshot& b = e.lastBefore;
    auto pair = [](float a, float c) {
        qb::Value v;
        v.type = qb::Type::Pair;
        v.f[0] = a, v.f[1] = c;
        return v;
    };
    if (b.x != e.x || b.y != e.y) qs::Vm::set(target, K("pos"), pair(e.x, e.y));
    if (b.sx != e.sx || b.sy != e.sy) qs::Vm::set(target, K("scale"), pair(e.sx, e.sy));
    if (b.alpha != e.alpha) qs::Vm::set(target, K("alpha"), qs::Vm::makeFloat(e.alpha));
    if (b.rot != e.rot) qs::Vm::set(target, K("rot_angle"), qs::Vm::makeFloat(e.rot));
    for (const char* n : {"time", "motion"})
        if (const qb::Value* v = qs::Vm::param(p, K(n))) qs::Vm::set(target, K(n), *v);
    e.x = b.x, e.y = b.y, e.sx = b.sx, e.sy = b.sy, e.alpha = b.alpha, e.rot = b.rot;
    startMorph(e, target);
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
    // GH3 keeps one target block per element: a new morph starts from the
    // current values but only the props it names get new targets; the
    // others keep heading for the targets of a morph still under way, and
    // `{(x,y) relative}` adds to the target pos [C SetMorph 0x8221CEC8:
    // start (+0x140) = current (+0xa0), target (+0xf0) written per given
    // prop; 0x8221C240 relative = target pos (+0x110) + (x,y)]. The
    // controller select's light_up_controller (pos to the player's spot,
    // scale 1.5, 0.25 s) followed at once by controller_jiggle's relative
    // DoMorphs: the controller reaches its spot (GD10-102).
    if (const Element::Morph& prev = e.morph; prev.active) {
        if (prev.pos) m.pos = true, m.x1 = prev.x1, m.y1 = prev.y1;
        if (prev.scale) m.scale = true, m.sx1 = prev.sx1, m.sy1 = prev.sy1;
        if (prev.alpha) m.alpha = true, m.a1 = prev.a1;
        if (prev.rot) m.rot = true, m.r1 = prev.r1;
        if (prev.rgba) m.rgba = true, m.c1 = prev.c1;
    }
    if (const qb::Value* v = qs::Vm::param(p, K("pos"))) {
        m.pos = true;
        if (v->type == qb::Type::Struct && v->st) {
            float a = 0, b = 0;
            for (const qb::Item& pi : v->st->items)
                if (pi.value.type == qb::Type::Pair) a = pi.value.f[0], b = pi.value.f[1];
            if (qs::Vm::flag(*v->st, K("relative"))) m.x1 += a, m.y1 += b;
            else if (qs::Vm::flag(*v->st, K("absolute"))) absoluteToLocal(e, a, b, m.x1, m.y1);
            else m.x1 = a, m.y1 = b;
        } else {
            readPair(*v, m.x1, m.y1);
        }
    }
    if (const qb::Value* v = qs::Vm::param(p, K("scale"))) m.scale = readPair(*v, m.sx1, m.sy1);
    // The relative_scale flag: the scale times the element's own
    // (jiggle_text_array_elements' random 0.7..1.3) [C flag parsed at
    // 0x8221A738; I: its use].
    if (m.scale && qs::Vm::flag(p, K("relative_scale"))) m.sx1 *= e.sx, m.sy1 *= e.sy;
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
    if (loadingTex_) loadingMs_ += dtMs;
    updateMovies(dtMs);
    while (!pendingFocus_.empty()) resolvePendingFocus(pendingFocus_.front());
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

UiSystem::Frame UiSystem::frameOf(const Element& e) const {
    // The same composition as draw() [C 0x8221BF88], from the root down.
    std::vector<const Element*> chain;
    for (const Element* x = &e; x && x != root_.get(); x = x->parent) chain.push_back(x);
    Frame f;  // the root's frame: (0, 0), scale 1
    for (size_t k = chain.size(); k-- > 0;) {
        const Element& x = *chain[k];
        const Element& p = x.parent ? *x.parent : *root_;
        const float anX = x.posAnchorGiven ? x.posAnchorX : p.anchorX, anY = x.posAnchorGiven ? x.posAnchorY : p.anchorY;
        const float ax = (anX + 1) / 2 * p.w * f.wsx, ay = (anY + 1) / 2 * p.h * f.wsy;
        const float offX = ax + x.x * f.wsx, offY = ay + x.y * f.wsy;
        const float pr = f.wrot * kDeg;
        Frame g;
        g.wx = f.cx + offX * std::cos(pr) - offY * std::sin(pr), g.wy = f.cy + offX * std::sin(pr) + offY * std::cos(pr);
        g.wsx = f.wsx * x.sx, g.wsy = f.wsy * x.sy;
        g.wrot = x.preserveOrientation ? x.rot : f.wrot + x.rot;
        const float jx = (x.justX + 1) / 2 * x.w * g.wsx, jy = (x.justY + 1) / 2 * x.h * g.wsy;
        const float r = g.wrot * kDeg;
        g.cx = g.wx - (jx * std::cos(r) - jy * std::sin(r)), g.cy = g.wy - (jx * std::sin(r) + jy * std::cos(r));
        f = g;
    }
    return f;
}

void UiSystem::absoluteToLocal(const Element& e, float x, float y, float& lx, float& ly) const {
    const Element& p = e.parent ? *e.parent : *root_;
    const Frame f = e.parent && e.parent != root_.get() ? frameOf(p) : Frame{};
    const float pr = -f.wrot * kDeg;
    const float dx = x - f.cx, dy = y - f.cy;
    const float offX = dx * std::cos(pr) - dy * std::sin(pr), offY = dx * std::sin(pr) + dy * std::cos(pr);
    const float anX = e.posAnchorGiven ? e.posAnchorX : p.anchorX, anY = e.posAnchorGiven ? e.posAnchorY : p.anchorY;
    const float ax = (anX + 1) / 2 * p.w * f.wsx, ay = (anY + 1) / 2 * p.h * f.wsy;
    lx = f.wsx != 0 ? (offX - ax) / f.wsx : 0, ly = f.wsy != 0 ? (offY - ay) / f.wsy : 0;
}

void UiSystem::draw(Canvas& c) {
    // Menus lay out their children first (unless locked).
    {
        std::vector<Element*> stack{root_.get()};
        while (!stack.empty()) {
            Element* e = stack.back();
            stack.pop_back();
            // A scrolling menu updates before its menu lays out (parents first) [C 0x8221D2A0].
            if (e->type == kVScrollingMenu) scrollMenu(*e);
            if (isMenu(*e) && (!e->locked || e->layoutOnce)) layoutMenu(*e);
            e->layoutOnce = false;
            // The menu's first-update event (it got a child or lost one) also
            // refreshes its scrolling menu's window, after the menu's layout
            // [C 0x82317CB0 on 0x3056434f]: the items' alphas by where they now are.
            if (isMenu(*e) && e->parent && e->parent->type == kVScrollingMenu && e->parent->scrollRelayout) {
                e->parent->scrollRelayout = false, e->parent->scrollDirty = true;
                scrollMenu(*e->parent);
            }
            for (auto& ch : e->children) stack.push_back(ch.get());
        }
    }
    struct Clip {
        bool on = false;
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    };
    struct Item {
        const Element* e;
        float cx, cy, wsx, wsy, wa, wrot;  // top-left corner, world scale, alpha, rotation (deg)
        float z;
        int depth;
        Clip clip;  // the enclosing window elements' rectangle
        bool shadowPass = false;  // a text's shadow
    };
    struct Node {
        const Element* e;
        float cx, cy, wsx, wsy, wa, wrot;
        float z;  // the parent's effective z
        int depth;
        Clip clip;
    };
    std::vector<Item> items;
    std::vector<Node> stack;
    ++drawCount_;
    for (size_t k = root_->children.size(); k-- > 0;) stack.push_back({root_->children[k].get(), 0, 0, 1, 1, 1, 0, 0, 1, {}});
    while (!stack.empty()) {
        const Node n = stack.back();
        stack.pop_back();
        const Element& e = *n.e;
        if (e.hidden) continue;
        const Element& p = e.parent ? *e.parent : *root_;
        // Composition [C 0x8221BF88]: the parent's child_anchor, pos scaled by the parent.
        const float anX = e.posAnchorGiven ? e.posAnchorX : p.anchorX, anY = e.posAnchorGiven ? e.posAnchorY : p.anchorY;
        const float ax = (anX + 1) / 2 * p.w * n.wsx, ay = (anY + 1) / 2 * p.h * n.wsy;
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
        items.push_back({&e, cx, cy, wsx, wsy, wa, wrot, z, n.depth, n.clip});
        // A text's shadow: pos + shadow_offs (screen px), z - 0.005 [C 0x8231A8C0].
        if (e.shadow && (e.type == kText || e.type == kTextBlock) && !e.text.empty())
            items.push_back({&e, cx + e.shadowX, cy + e.shadowY, wsx, wsy, wa, wrot, z - 0.005f, n.depth, n.clip, true});
        // A window element clips its children to its rectangle (its rotation
        // ignored) [I: the pause menu's scrolling tiles, the store's text].
        Clip clip = n.clip;
        if (e.type == K("windowelement") && e.w > 0 && e.h > 0) {
            const float x0 = cx, y0 = cy, x1 = cx + e.w * wsx, y1 = cy + e.h * wsy;
            if (!clip.on) clip = Clip{true, x0, y0, x1, y1};
            else clip = Clip{true, std::max(clip.x0, x0), std::max(clip.y0, y0), std::min(clip.x1, x1), std::min(clip.y1, y1)};
        }
        for (size_t k = e.children.size(); k-- > 0;) stack.push_back({e.children[k].get(), cx, cy, wsx, wsy, wa, wrot, z, n.depth + 1, clip});
    }
    // The texture-slot movies, full screen at their texturepri.
    {
        std::vector<const Element*> movies;
        moviesToDraw(movies);
        for (const Element* m : movies) items.push_back({m, 0, 0, m->sx, m->sy, 1, 0, m->z, 0, {}});
    }
    // z_priority is one global key for sprites and texts (one list,
    // 0x8238D7D0) [C]. Equal z: the one inserted last (made visible or given
    // a new z last; in tree order within a frame, as the game updates its
    // elements) is drawn behind [C insert before the first equal].
    for (const Item& it : items) {
        if (it.shadowPass) continue;
        const Element& e = *it.e;
        if (e.listSeq == 0 || e.listSeen + 1 != drawCount_ || e.listZ != it.z) e.listSeq = ++listSeq_;
        e.listSeen = drawCount_, e.listZ = it.z;
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& l, const Item& r) {
        return l.z < r.z || (l.z == r.z && l.e->listSeq > r.e->listSeq);
    });
    for (const Item& it : items) {
        const Element& e = *it.e;
        const uint8_t alpha = uint8_t(std::clamp(float(e.rgba[3]) * it.wa, 0.0f, 255.0f));
        if (alpha == 0) continue;
        if (it.clip.on) c.setClip(it.clip.x0, it.clip.y0, it.clip.x1, it.clip.y1);
        else c.clearClip();
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
            gfx::TextureId id = tex_.find(e.texContext, e.texture);
            if (!id) id = movieTexture(e.texture);
            if (!id) continue;
            const float w = e.w * it.wsx, h = e.h * it.wsy;
            float u0 = 0, u1 = 1, v0 = 0, v1 = 1;
            // flip_v mirrors left-right and flip_h top-bottom [C data: the
            // pause frame's top-right quadrant uses flip_v, bottom-left flip_h].
            if (e.flipV) std::swap(u0, u1);
            if (e.flipH) std::swap(v0, v1);
            const Vertex a = P(0, 0, u0, v0, e.rgba), b = P(w, 0, u1, v0, e.rgba), cc = P(w, h, u1, v1, e.rgba), d = P(0, h, u0, v1, e.rgba);
            const Vertex q[6] = {a, b, cc, a, cc, d};
            c.draw(q, 6, int(id.id), e.blend == 1 ? Blend::Add : e.blend == 2 ? Blend::Sub : Blend::Alpha);
            ++stats_.spritesDrawn;
        } else if (e.type == kSprite && !e.texture && e.w > 0 && e.h > 0) {
            // A sprite without a texture is a plain rectangle in its colour
            // (the detailed stats' paper and row highlights) [I].
            const float w = e.w * it.wsx, h = e.h * it.wsy;
            const Vertex a = P(0, 0, 0, 0, e.rgba), b = P(w, 0, 0, 0, e.rgba), cc = P(w, h, 0, 0, e.rgba), d = P(0, h, 0, 0, e.rgba);
            const Vertex q[6] = {a, b, cc, a, cc, d};
            c.draw(q, 6, 0, e.blend == 1 ? Blend::Add : e.blend == 2 ? Blend::Sub : Blend::Alpha);
            ++stats_.spritesDrawn;
        } else if ((e.type == kText || e.type == kTextBlock) && !e.text.empty()) {
            const FontEntry* fe = fontFor(e.font);
            if (!fe) {
                ++stats_.missingFonts;
                continue;
            }
            drawElementText(c, e, *fe, it.cx, it.cy, it.wsx, it.wsy, it.wa, it.wrot, it.shadowPass);
            ++stats_.textDrawn;
        }
    }
    c.clearClip();
    // The loading screen: its image stretched over the screen, the wheel
    // (96 x 96 at spin_x, spin_y) turning [C 00a_boot_legal capture; I speed].
    if (const gfx::TextureId lt = loadingTex_ ? tex_.find(loadingTex_) : gfx::TextureId{}) {
        const std::array<uint8_t, 4> w{255, 255, 255, 255};
        auto V = [&](float x, float y, float u, float v) { return Vertex{x, y, 0, u, v, w[0], w[1], w[2], w[3]}; };
        const Vertex q[6] = {V(0, 0, 0, 0), V(1280, 0, 1, 0), V(1280, 720, 1, 1), V(0, 0, 0, 0), V(1280, 720, 1, 1), V(0, 720, 0, 1)};
        c.draw(q, 6, int(lt.id), Blend::Alpha);
        if (const gfx::TextureId st = loadingSpin_ ? tex_.find(loadingSpin_) : gfx::TextureId{}) {
            const float a = float(loadingMs_ / 1000.0 * 2 * M_PI), cs = std::cos(a), sn = std::sin(a), h = 48;
            const float cx = spinX_ + h, cy = spinY_ + h;
            auto R = [&](float dx, float dy, float u, float v) { return V(cx + dx * cs - dy * sn, cy + dx * sn + dy * cs, u, v); };
            const Vertex s[6] = {R(-h, -h, 0, 0), R(h, -h, 1, 0), R(h, h, 1, 1), R(-h, -h, 0, 0), R(h, h, 1, 1), R(-h, h, 0, 1)};
            c.draw(s, 6, int(st.id), Blend::Alpha);
        }
    }
    c.clearClip();
}

void UiSystem::drawElementText(Canvas& c, const Element& e, const FontEntry& fe, float cx, float cy, float wsx, float wsy, float wa, float wrot,
                               bool shadowPass) const {
    // [C 0x8232B5E8] per glyph: x0 = pen - x offset - bearing, x1 = x0 +
    // width, next pen = x1 + advance adjust + tracking (font_spacing, if
    // given, replaces the tracking and drops the bearing); top = line height
    // - (height - baseline) - vShift, in the glyph's own font for buttons.
    // A space moves the pen by its width - bearing + tracking.
    const font::Font& f = fe.f;
    const FontEntry* buttons = nullptr;
    buttons = buttonsFont();
    const float bearing = e.fontSpacingGiven ? 0.0f : float(f.bearing);
    const float track = e.fontSpacingGiven ? float(int(e.fontSpacing)) : float(f.tracking);
    const std::array<uint8_t, 4> base = shadowPass ? e.shadowRgba : e.rgba;
    const uint8_t alpha = uint8_t(std::clamp(float(base[3]) * wa, 0.0f, 255.0f));
    if (alpha == 0) return;
    const float r = wrot * kDeg, cs = std::cos(r), sn = std::sin(r);
    const bool block = e.type == kTextBlock;
    const float is = e.internalScale;  // text blocks and (gh4) text elements [I]
    const float sx = wsx * is, sy = wsy * is;
    auto P = [&](float lx, float ly, float u, float v, const std::array<uint8_t, 4>& col) {
        return Vertex{cx + lx * cs - ly * sn, cy + lx * sn + ly * cs, 0, u, v, col[0], col[1], col[2], alpha};
    };
    std::vector<std::u16string> lines;
    BlockFit fit;
    if (block) {
        fit = blockFit(fe, e);
        lines = fit.lines;
    } else {
        lines.emplace_back();
        const std::u16string s = utf16(e.text);
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == u'\\' && i + 1 < s.size() && s[i + 1] == u'n') lines.emplace_back(), ++i;
            else lines.back().push_back(s[i]);
        }
    }
    const float lineH = f.lineHeight * e.lineSpacing * (block ? fit.sy : 1.0f);
    // A block's lines sit inside its dims by internal_just [I].
    float lineY = block ? std::max(0.0f, e.h / is - lineH * float(lines.size())) * (e.ijustY + 1) / 2 : 0;
    float lsx = 1, lsy = 1;  // this line's glyph scale (fit_width / fit_height)
    std::vector<Vertex> verts;
    std::array<uint8_t, 4> col = base;
    auto quad = [&](std::vector<Vertex>& out, const font::Glyph& g, float x0, float y0, const std::array<uint8_t, 4>& cl) {
        const float gw = g.width * lsx, gy = (y0 - lineY) * lsy + lineY, gh = g.height * lsy;
        const Vertex a = P(x0 * sx, gy * sy, g.u0, g.v0, cl), b = P((x0 + gw) * sx, gy * sy, g.u1, g.v0, cl);
        const Vertex cc = P((x0 + gw) * sx, (gy + gh) * sy, g.u1, g.v1, cl), d = P(x0 * sx, (gy + gh) * sy, g.u0, g.v1, cl);
        out.insert(out.end(), {a, b, cc, a, cc, d});
    };
    for (size_t li = 0; li < lines.size(); ++li) {
        const std::u16string& line = lines[li];
        // scale_mode proportional: a squeezed line keeps its aspect [I].
        lsx = block ? fit.sx * fit.lineScale[li] : 1.0f, lsy = block ? (e.scalePerAxis ? fit.sy : lsx) : 1.0f;
        float pen = block ? (e.w / is - textWidth(fe, line, e) * lsx) * (e.ijustX + 1) / 2 : 0;
        for (size_t i = 0; i < line.size(); ++i) {
            const FontEntry* gf = &fe;
            const font::Glyph* g = nullptr;
            bool button = false;  // "\\b": the buttons font's glyph, white
            if (line[i] == u'\\' && i + 1 < line.size() && line[i + 1] != u'\\') {
                const char16_t code = line[++i];
                if ((code == u'c' || code == u'C') && i + 1 < line.size()) {
                    const int n = digit32(line[++i]);
                    if (shadowPass) continue;  // the shadow keeps its colour [C 0x8231A8C0]
                    if (n <= 0) col = base;
                    else if (size_t(n - 1) < colorTab_.size()) col = colorTab_[size_t(n - 1)], col[3] = base[3];
                    continue;
                }
                if (code == u'u' || code == u'U') {
                    ++i;
                    continue;
                }
                if (code == u'L') continue;
                if ((code == u'b' || code == u'B' || code == u's' || code == u'S') && i + 1 < line.size()) {
                    const int n = digit32(line[++i]);
                    button = code == u'b' || code == u'B';
                    gf = button ? buttons : &fe;
                    if (!gf || n < 0 || n >= 32) continue;
                    g = &gf->f.button(size_t(n));
                }
            } else if (line[i] == u'\\' && i + 1 < line.size()) {
                ++i;
            }
            if (!g && line[i] == u' ') {
                pen += (f.spaceWidth + e.spaceSpacing - bearing + track) * lsx;
                continue;
            }
            if (!g) g = &f.glyphFor(uint16_t(line[i]));
            const font::Font& gff = gf->f;
            const float x0 = pen - (float(g->xOffset) + bearing) * lsx;
            const float y0 = lineY + gff.lineHeight - (g->height - g->baseline) - float(gff.vShift);
            if (button) {
                // A button glyph: its own texture, white unless the text is
                // black (a shadow) [C 0x8232B5E8].
                std::array<uint8_t, 4> bc = col;
                if (bc[0] || bc[1] || bc[2]) bc = {255, 255, 255, col[3]};
                std::vector<Vertex> q;
                quad(q, *g, x0, y0, bc);
                if (!verts.empty()) c.draw(verts.data(), verts.size(), int(fe.tex.id), Blend::Alpha), verts.clear();
                c.draw(q.data(), q.size(), int(gf->tex.id), Blend::Alpha);
            } else {
                quad(verts, *g, x0, y0, col);
            }
            pen = x0 + (g->width + float(g->advanceAdjust) + track) * lsx;
        }
        lineY += lineH;
    }
    if (!verts.empty()) c.draw(verts.data(), verts.size(), int(fe.tex.id), Blend::Alpha);
}

// ---- the port's own screens and the mouse (not GH3's) ----

void UiSystem::drawText(Canvas& c, uint32_t font, const std::string& text, float x, float y, float h, const std::array<uint8_t, 4>& rgba,
                        uint32_t align, const TextStyle& style) const {
    const FontEntry* fe = fontFor(font);
    if (!fe || text.empty() || h <= 0) return;
    const FontEntry* buttons = nullptr;
    buttons = buttonsFont();
    const font::Font& f = fe->f;
    // Laid out unscaled in x about the anchor, then squeezed by sx and
    // turned by rot about (x, y).
    const float k = h / std::max(1.0f, f.lineHeight), w = textWidth(font, text, h, style.spacing);
    const uint32_t ha = align & 0xF, va = align >> 4;
    float pen = (ha == 2 ? x - w * 0.5f : ha == 4 ? x - w : x) / k;
    const float top = (va == 2 ? y - h * 0.5f : va == 4 ? y - h : y) / k;
    const float baseY = top + (f.lineHeight - float(f.vShift));
    const std::u16string s = utf16(text);
    std::vector<Vertex> verts;
    const float ra = style.rot * float(M_PI) / 180, rc = std::cos(ra), rs = std::sin(ra);
    const bool plain = style.rot == 0 && style.sx == 1;
    auto V = [&](float px, float py, float u, float v, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        if (plain) return Vertex{px, py, 0, u, v, r, g, b, a};
        const float lx = (px - x) * style.sx, ly = py - y;
        return Vertex{x + lx * rc - ly * rs, y + lx * rs + ly * rc, 0, u, v, r, g, b, a};
    };
    auto glyph = [&](const font::Glyph& g, float gy) {
        const float gx = pen + float(g.xOffset);
        const Vertex a = V(gx * k, gy * k, g.u0, g.v0, rgba[0], rgba[1], rgba[2], rgba[3]);
        const Vertex b = V((gx + g.width) * k, gy * k, g.u1, g.v0, rgba[0], rgba[1], rgba[2], rgba[3]);
        const Vertex cc = V((gx + g.width) * k, (gy + g.height) * k, g.u1, g.v1, rgba[0], rgba[1], rgba[2], rgba[3]);
        const Vertex d = V(gx * k, (gy + g.height) * k, g.u0, g.v1, rgba[0], rgba[1], rgba[2], rgba[3]);
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
            const Vertex a = V(x0, y0, g.u0, g.v0, 255, 255, 255, rgba[3]), b = V(x0 + g.width * bk, y0, g.u1, g.v0, 255, 255, 255, rgba[3]);
            const Vertex cc = V(x0 + g.width * bk, y0 + g.height * bk, g.u1, g.v1, 255, 255, 255, rgba[3]);
            const Vertex d = V(x0, y0 + g.height * bk, g.u0, g.v1, 255, 255, 255, rgba[3]);
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
        pen += g.width + float(f.tracking) + style.spacing;
    }
    if (!verts.empty()) c.draw(verts.data(), verts.size(), int(fe->tex.id), Blend::Alpha);
}

float UiSystem::fontLineHeight(uint32_t font) const {
    const FontEntry* fe = fontFor(font);
    return fe ? fe->f.lineHeight : 0.0f;
}

float UiSystem::textWidth(uint32_t font, const std::string& text, float h, float spacing) const {
    const FontEntry* fe = fontFor(font);
    if (!fe || text.empty()) return 0;
    const font::Font& f = fe->f;
    const float k = h / std::max(1.0f, f.lineHeight);
    const FontEntry* buttons = nullptr;
    buttons = buttonsFont();
    const std::u16string s = utf16(text);
    float w = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == u'\\' && i + 2 < s.size() && s[i + 1] == u'b') {
            const int n = hexDigit(s[i + 2]);
            i += 2;
            if (buttons && n >= 0) w += buttons->f.button(size_t(n)).width * h / std::max(1.0f, buttons->f.lineHeight);
            continue;
        }
        w += (s[i] == u' ' ? f.spaceWidth : f.glyphFor(uint16_t(s[i])).width + float(f.tracking) + spacing) * k;
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

std::vector<UiSystem::HelpPillHit> UiSystem::helperPills() const {
    std::vector<HelpPillHit> out;
    const Element* box = find(K("user_control_container"));
    if (!box) return out;
    // add_user_control_helper's button glyphs, as SetText leaves them
    // (\m0 / \m1 through meta_button_map) [C script add_user_control_helper].
    static const char* const kButtons[][2] = {{"\\m0", "green"},  {"\\m1", "red"},      {"\\b6", "yellow"}, {"\\b7", "blue"},
                                              {"\\b8", "orange"}, {"\\bb", "strumbar"}, {"\\ba", "start"}};
    auto buttonOf = [&](const Element* b) -> std::string {
        if (!b) return {};
        for (const auto& k : kButtons)
            if (b->text == setText(k[0])) return k[1];
        return {};
    };
    const auto& kids = box->children;
    for (size_t i = 0; i < kids.size(); ++i) {
        const Element* c = kids[i].get();
        if (c->type != kText || c->text.empty() || c->drawnIn != drawCount_ || c->text[0] == '\\') continue;
        // Its glyph: uc_button_N for the text uc_text_N (the auto-centred
        // pills), else the glyph made just after it (or, without a pill,
        // just before it) [C script add_user_control_helper].
        const Element* glyph = nullptr;
        for (int n = 0; n < 16 && !glyph; ++n)
            if (c->id == K(("uc_text_" + std::to_string(n)).c_str())) glyph = find(K(("uc_button_" + std::to_string(n)).c_str()));
        if (!glyph && i + 1 < kids.size() && !kids[i + 1]->text.empty() && kids[i + 1]->text[0] == '\\') glyph = kids[i + 1].get();
        if (!glyph && i > 0 && !kids[i - 1]->text.empty() && kids[i - 1]->text[0] == '\\') glyph = kids[i - 1].get();
        // The pill: the text and its glyph on the left [I].
        HelpPillHit h{c->text, buttonOf(glyph), c->bx0 - 40, c->by0, c->bx1, c->by1};
        if (glyph && glyph->drawnIn == drawCount_)
            h.x0 = std::min(c->bx0, glyph->bx0), h.y0 = std::min(h.y0, glyph->by0), h.x1 = std::max(h.x1, glyph->bx1), h.y1 = std::max(h.y1, glyph->by1);
        for (char& ch : h.text) ch = char(std::toupper(static_cast<unsigned char>(ch)));
        out.push_back(h);
    }
    // The backing (GD10-111): each pill's Control_pill_body (the sprite over
    // its text), then the ends touching a body, so the whole visible pill
    // takes the mouse, not just its glyph and text [C script
    // add_user_control_helper: a body, then its two Control_pill_end].
    for (int pass = 0; pass < 2; ++pass)
        for (const auto& k : kids) {
            const Element* sp = k.get();
            if (sp->type == kText || sp->drawnIn != drawCount_) continue;
            HelpPillHit* best = nullptr;
            float bestD = 1e30f;
            for (HelpPillHit& h : out) {
                // Not a backdrop under the whole bar: a pill is at most a third
                // of action_safe_width_for_helpers (925) wide [C script].
                if (sp->by1 < h.y0 - 8 || sp->by0 > h.y1 + 8 || sp->bx1 - sp->bx0 > 420) continue;
                const float overlap = std::min(sp->bx1, h.x1) - std::max(sp->bx0, h.x0);
                // pass 0: a body over at least half its text; pass 1: an end within 2 units of its pill.
                const float d = pass == 0 ? (overlap >= 0.5f * (h.x1 - h.x0) ? -overlap : 1e30f) : std::max({0.0f, h.x0 - sp->bx1, sp->bx0 - h.x1});
                if (d < bestD && (pass == 0 || d <= 2)) bestD = d, best = &h;
            }
            if (!best) continue;
            best->x0 = std::min(best->x0, sp->bx0), best->y0 = std::min(best->y0, sp->by0);
            best->x1 = std::max(best->x1, sp->bx1), best->y1 = std::max(best->y1, sp->by1);
        }
    return out;
}

std::string UiSystem::helperAt(float x, float y, std::string* button, float* midY) const {
    // The pill under the point, else one within 8 units of it; pills that
    // touch or overlap (the set list's five, narrowed to the safe width):
    // the one whose middle is nearest (GD10-111).
    const HelpPillHit* best = nullptr;
    float bestOut = 9, bestMid = 0;
    const std::vector<HelpPillHit> pills = helperPills();
    for (const HelpPillHit& h : pills) {
        const float out = std::max({0.0f, h.x0 - x, x - h.x1, h.y0 - y, y - h.y1}), mid = std::fabs(x - (h.x0 + h.x1) * 0.5f);
        if (out < bestOut || (out == bestOut && mid < bestMid)) best = &h, bestOut = out, bestMid = mid;
    }
    if (!best) return {};
    if (button) *button = best->button;
    if (midY) *midY = (best->y0 + best->y1) * 0.5f;
    return best->text;
}

bool UiSystem::member(qs::ScriptRun& run, uint32_t obj, uint32_t method, qb::Struct& p, bool& handled) {
    Element* e = find(obj);
    handled = true;
    ++stats_.memberCalls;
    if (method == K("SetProps")) {
        // gh4: a desc's named props too (create_popup_warning_text's
        // PopupElement : setprops {PopupTitle_text PopupBody_text}) [C scripts].
        if (e && e->desc) descSetProps(*e, p);
        else if (e) applyProps(*e, p, false);
        return e != nullptr;
    }
    // gh4's element members: se_setprops (a desc's named props too),
    // se_getprops, desc_resolvealias {name = <alias> [param = <local>]}.
    if (method == K("se_setprops")) {
        if (!e) return false;
        if (e->desc) descSetProps(*e, p);
        else applyProps(*e, p, false);
        // With time = s: pos / scale / alpha / rot_angle / rgba move there
        // over that time (GH3's DoMorph), the rest at once [C scripts:
        // DifficultyInterface fades in with se_setprops alpha = 1 time = ...; I].
        if (const qb::Value* t = qs::Vm::param(p, K("time")); t && t->number() > 0) morphFromSetProps(*e, p);
        return true;
    }
    if (method == K("se_waitprops")) {
        // Waits for the element's morphs (its own and its desc parts') to end.
        if (!e) return true;
        const uint32_t id = e->id;
        run.waitFor = [this, id] {
            Element* x = find(id);
            if (!x) return true;
            std::vector<const Element*> st{x};
            while (!st.empty()) {
                const Element* y = st.back();
                st.pop_back();
                if (y->morph.active) return false;
                for (auto& c : y->children) st.push_back(c.get());
            }
            return true;
        };
        return true;
    }
    if (method == K("obj_spawnscript") || method == K("obj_spawnscriptnow")) {
        // gh4: `<element> : obj_spawnscript <script> params = {...}` runs the
        // script on the element (destroy_loading_screen) [C scripts].
        if (!e) return false;
        uint32_t scr = 0;
        for (const qb::Item& it : p.items)
            if (it.name == 0 && (it.value.type == qb::Type::Key || it.value.type == qb::Type::KeyPtr)) scr = it.value.key;
        if (!scr) return false;
        qb::Struct args;
        if (const qb::Value* ps = qs::Vm::param(p, K("params")); ps && ps->st) args = *ps->st;
        const uint32_t id = vm_.spawnOn(scr, args, e->id);
        if (id && method == K("obj_spawnscriptnow")) vm_.runNow(id);
        return true;
    }
    // gh4: `<element> : se_getparentid` -> parent_id [C scripts: the
    // scrollbars', select controller's and setlist's callers read <parent_id>].
    if (method == K("se_getparentid")) {
        if (!e || !e->parent) return false;
        if (qb::Struct* l = locals(run)) qs::Vm::set(*l, K("parent_id"), qs::Vm::makeKey(e->parent->id));
        return true;
    }
    if (method == K("se_getprops")) {
        if (!e) return false;
        // A desc's named props (se_getprops {main_menu_bg_scale}) -> locals of that name.
        bool named = false;
        if (e->desc)
            for (const qb::Item& it : p.items)
                if (it.name == 0 && it.value.type == qb::Type::Key) {
                    qb::Value v;
                    if (descGetProp(*e, it.value.key, v)) {
                        if (qb::Struct* l = locals(run)) qs::Vm::set(*l, it.value.key, v);
                        named = true;
                    }
                }
        if (named) return true;
        qb::Struct q;
        qs::Vm::set(q, K("id"), qs::Vm::makeKey(e->id));
        const bool ok = vm_.callNative(K("GetScreenElementProps"), run, q);
        // Without names: every named prop of a desc too (add_user_control_helper
        // reads <helper_pill_menu_dims> after `<id> : se_getprops`) [C script].
        if (e->desc)
            if (const qb::Value* d = vm_.global(e->desc); d && d->st)
                if (const qb::Value* props = d->st->find(K("props")); props && props->arr)
                    for (const qb::Value& pv : props->arr->values)
                        if (pv.st)
                            if (const qb::Value* nm = pv.st->find(K("name")); nm && nm->key) {
                                qb::Value v;
                                if (descGetProp(*e, nm->key, v))
                                    if (qb::Struct* l = locals(run)) qs::Vm::set(*l, nm->key, v);
                            }
        return ok;
    }
    if (method == K("desc_resolvealias")) {
        if (!e) return false;
        const qb::Value* a = qs::Vm::param(p, K("name"));
        Element* r = a ? descAlias(*e, a->key) : nullptr;
        if (!r) return false;
        const qb::Value* pn = qs::Vm::param(p, K("param"));
        if (qb::Struct* l = locals(run)) qs::Vm::set(*l, pn && pn->key ? pn->key : K("resolved_id"), qs::Vm::makeKey(r->id));
        return true;
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
        if (qb::Struct* l = locals(run)) {
            // The element's id comes with its tags when the script has none
            // [I: retail_menu_focus without an id recolours `<id>` after its
            // gettags; jiggle_text_array_elements' gettags on each letter
            // keeps its own id (the container) for explode_text_array_elements <...>].
            if (!qs::Vm::param(e->tags, K("id")) && !qs::Vm::param(*l, K("id"))) qs::Vm::set(*l, K("id"), qs::Vm::makeKey(e->id));
            for (const qb::Item& it : e->tags.items) qs::Vm::set(*l, it.name, it.value);
        }
        return true;
    }
    if (method == K("RemoveTags")) {
        // `: removetags [a b]`, `{ [$a] }`, `tags = [a]` [C data].
        if (!e) return false;
        std::function<void(const qb::Struct&)> scan = [&](const qb::Struct& s) {
            for (const qb::Item& it : s.items) {
                if (it.value.type == qb::Type::Struct && it.value.st) scan(*it.value.st);
                if (it.value.type != qb::Type::Array || !it.value.arr) continue;
                for (const qb::Value& k : it.value.arr->values) {
                    if (k.type != qb::Type::Key && k.type != qb::Type::KeyPtr) continue;
                    auto& tags = e->tags.items;
                    tags.erase(std::remove_if(tags.begin(), tags.end(), [&](const qb::Item& t) { return t.name == k.key; }), tags.end());
                }
            }
        };
        scan(p);
        return true;
    }
    if (method == K("Obj_SpawnScriptNow") || method == K("Obj_SpawnScriptLater")) {
        // A script on this element: `: obj_spawnscriptlater script params = {...}`
        // (animate_dialog_box_in, pulse_dialog_title), or written as a struct
        // `{ $script params = {...} }` [C data].
        const qb::Struct* in = &p;
        for (const qb::Item& it : p.items)
            if (!it.name && it.value.type == qb::Type::Struct && it.value.st) in = it.value.st.get();
        uint32_t script = 0;
        for (const qb::Item& it : in->items)
            if (!it.name && (it.value.type == qb::Type::Key || it.value.type == qb::Type::KeyPtr) && !script) script = it.value.key;
        if (!e || !script) return false;
        qb::Struct params;
        if (const qb::Value* ps = qs::Vm::param(*in, K("params")); ps && ps->st) params = *ps->st;
        const uint32_t id = vm_.spawnOn(script, params, e->id);
        if (id && method == K("Obj_SpawnScriptNow")) vm_.runNow(id);
        return id != 0;
    }
    if (method == K("GetProps")) {
        if (!e) return false;
        qb::Struct q;
        qs::Vm::set(q, K("id"), qs::Vm::makeKey(obj));
        return vm_.callNative(K("GetScreenElementProps"), run, q);
    }
    if (method == K("GetParentID")) {
        if (!e) return false;
        if (qb::Struct* l = locals(run)) qs::Vm::set(*l, K("parent_id"), qs::Vm::makeKey(e->parent ? e->parent->id : 0));
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
    // gh4: a global script called as a member runs on the element
    // (`<id> : button_pill_apply_relative_scale ...`) [C scripts].
    if (e) {
        if (const qb::Value* g = vm_.global(method); g && g->type == qb::Type::Script) {
            bool result = true;
            vm_.run(method, p, &result, e->id);  // the element itself, not an alias of it
            return result;
        }
    }
    handled = false;
    return false;
}

uint32_t UiSystem::loadAssetContext(uint32_t context, const qb::Struct* body) {
    // An asset context is a pak's name key (its header's, the key of the
    // pak's file name: axel_1); for a character it is
    // musician_body[].asset_context, whose `pak` is the body pak [C data, qb
    // musician_body / download_musician_body]. Morello_1 and the BoneMan
    // bodies have no asset_context (the script pushes nothing): GH3 then finds
    // Mag_Photo in the paks loaded, the players' bodies alone; the port keeps
    // every body loaded, so it takes the body the script just looked up
    // (`info_struct`) [I].
    auto strOf = [](const qb::Value* v) { return v && (v->type == qb::Type::String || v->type == qb::Type::WString) ? v->str : std::string(); };
    auto pathOf = [&](const qb::Struct& b, std::string& stem) {
        const std::string pak = strOf(b.find(K("pak")));
        const size_t slash = pak.find_last_of("\\/");
        stem = slash == std::string::npos ? pak : pak.substr(slash + 1);
        if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, ".pak") == 0) stem.resize(stem.size() - 4);
        return pak.empty() ? std::string() : "DATA/COMPRESSED/PAK/" + stem + ".pak.xen";
    };
    auto load = [&](const qb::Struct& b, uint32_t ctx) -> uint32_t {
        std::string stem;
        const std::string path = pathOf(b, stem);
        if (!ctx) ctx = qbkey::key(stem);
        if (tex_.hasContext(ctx)) return ctx;
        if (path.empty() || !disc_.exists(path)) return 0;
        return tex_.loadContextImages(disc_, path, ctx) > 0 ? ctx : 0;
    };
    if (!context) return body ? load(*body, 0) : 0;
    if (tex_.hasContext(context)) return context;
    for (uint32_t arr : {K("musician_body"), K("download_musician_body")}) {
        const qb::Value* a = vm_.global(arr);
        if (!a || !a->arr) continue;
        for (const qb::Value& b : a->arr->values)
            if (b.st)
                if (const qb::Value* ac = b.st->find(K("asset_context")); ac && ac->key == context) return load(*b.st, context);
    }
    return 0;
}

void UiSystem::addNatives() {
    using qs::ScriptRun;
    using qs::Vm;
    // PushAssetContext context = <pak name> / PopAssetContext: what the UI
    // creates meanwhile takes its textures from that pak first (the results
    // screen's Mag_Photo, one per character body) [C script create_newspaper_menu].
    vm_.addNative("PushAssetContext", [this](Vm&, ScriptRun& run, qb::Struct& p) {
        const qb::Value* c = Vm::param(p, K("context"));
        const uint32_t ctx = c && (c->type == qb::Type::Key || c->type == qb::Type::KeyPtr) ? c->key : 0;
        const qb::Struct* body = nullptr;
        if (!ctx && !run.frames.empty() && run.frames.back().locals)
            if (const qb::Value* is = run.frames.back().locals->find(K("info_struct")); is && is->st) body = is->st.get();
        assetContexts_.push_back(loadAssetContext(ctx, body));
        return true;
    });
    vm_.addNative("PopAssetContext", [this](Vm&, ScriptRun&, qb::Struct&) {
        if (!assetContexts_.empty()) assetContexts_.pop_back();
        return true;
    });
    vm_.addNative("CreateScreenElement", [this](Vm&, ScriptRun& run, qb::Struct& p) {
        auto e = std::make_unique<Element>();
        static uint32_t anonymous = 0x80000000u;
        const qb::Value* type = Vm::param(p, K("type"));
        e->type = type ? type->key : K("containerelement");
        // A window starts at just [left top], the screen's size [C ctor 0x82315138].
        if (e->type == K("windowelement")) e->justX = e->justY = -1, e->w = 1280, e->h = 720;
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
        // gh4: type = descinterface desc = '<name>': the uidesc_<name> tree,
        // then the call's own desc props (loading_text = ...) [C data ui\uidesc_*.qb].
        if (raw->type == K("descinterface")) {
            const qb::Value* d = Vm::param(p, K("desc"));
            const std::string dn = d ? (d->type == qb::Type::String ? d->str : qbkey::name(d->key)) : std::string();
            if (!buildDesc(*raw, dn)) {
                vm_.logOnce("ui: no desc " + dn);
            } else {
                // The call's `just` does not move the desc's root (D14 [I]):
                // make_menu_frontend's just = [center center] at pos (0, 0)
                // is the full-screen career / game mode menu in the original.
                qb::Struct q = p;
                q.items.erase(std::remove_if(q.items.begin(), q.items.end(), [](const qb::Item& it) { return it.name == K("just"); }), q.items.end());
                descSetProps(*raw, q);
            }
        }
        // The main menu's XBOX LIVE (ONLINE: no online play in the port) as
        // GH3 disables it in demo mode: create_main_menu's demo_mode_disable
        // = {rgba = [128 128 128 255] not_focusable} on its text and its
        // vmenu item [C script create_main_menu] (GD10-101).
        bool online = raw->id == K("main_menu_leaderboards_text");
        for (const Element::Handler& h : raw->handlers) online = online || h.script == K("main_menu_select_xbox_live");
        if (online) raw->rgba = {128, 128, 128, 255}, raw->notFocusable = true;
        ++stats_.created;
        // new_menu focuses its menu before the items exist: the first
        // focusable item added to a focused menu takes the focus, once the
        // script has made its siblings too (the pause menu's items recolour
        // a text made after them), i.e. at the menu's next update or event [I].
        if (isMenu(*parent) && parent->focused && parent->selected < 0 && !raw->notFocusable &&
            std::find(pendingFocus_.begin(), pendingFocus_.end(), parent->id) == pendingFocus_.end())
            pendingFocus_.push_back(parent->id);
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("id"), Vm::makeKey(raw->id));
        return true;
    });
    // DisplayLoadingScreen '<name>' [spin_texture = '<img>' spin_x spin_y]
    // [localized]: IMAGES/LOADINGSCREENS/<name>.img.xen over everything
    // (the English disc's own names) [C 0x8218DDE8]; HideLoadingScreen
    // takes it away [C 0x8218E0A0].
    vm_.addNative("DisplayLoadingScreen", [this](Vm&, ScriptRun&, qb::Struct& p) {
        const qb::Value* name = nullptr;
        for (const qb::Item& it : p.items)
            if (it.name == 0 && (it.value.type == qb::Type::String || it.value.type == qb::Type::WString)) name = &it.value;
        if (!name) return false;
        auto path = [](const std::string& n) { return "DATA/COMPRESSED/IMAGES/LOADINGSCREENS/" + n + ".img.xen"; };
        const uint32_t key = qbkey::key("loadingscreen:" + name->str);
        if (!tex_.loadImage(disc_, path(name->str), key)) return false;
        loadingTex_ = key, loadingSpin_ = 0, loadingMs_ = 0;
        if (const qb::Value* sp = Vm::param(p, K("spin_texture")); sp && !sp->str.empty()) {
            const uint32_t sk = qbkey::key("loadingscreen:" + sp->str);
            if (tex_.loadImage(disc_, path(sp->str), sk)) loadingSpin_ = sk;
            spinX_ = Vm::number(Vm::param(p, K("spin_x")), 0), spinY_ = Vm::number(Vm::param(p, K("spin_y")), 0);
        }
        return true;
    });
    vm_.addNative("HideLoadingScreen", [this](Vm&, ScriptRun&, qb::Struct&) {
        loadingTex_ = 0;
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
    vm_.addNative("AssignAlias", [this](Vm&, ScriptRun&, qb::Struct& p) {
        Element* e = resolve(Vm::param(p, K("id")));
        const qb::Value* a = Vm::param(p, K("alias"));
        if (!e || !a || !a->key) return false;
        aliases_[a->key] = e->id;
        return true;
    });
    vm_.addNative("ScreenElementExists", [this](Vm&, ScriptRun&, qb::Struct& p) { return resolve(Vm::param(p, K("id"))) != nullptr; });
    vm_.addNative("SetScreenElementProps", [this](Vm&, ScriptRun&, qb::Struct& p) {
        Element* e = resolve(Vm::param(p, K("id")));
        if (!e) return false;
        if (e->desc) descSetProps(*e, p);  // a desc's named props too [I]
        else applyProps(*e, p, false);
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
        layoutTree(*e);
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
            // `summed`: on screen (the pos point through every parent) [I:
            // mainmenu_focus puts the highlight there with pos = {absolute}].
            if (Vm::flag(p, K("summed"))) {
                const Frame f = frameOf(*e);
                v.f[0] = f.wx, v.f[1] = f.wy;
            }
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
    // se_getparentid without an object: the element the script runs on.
    vm_.addNative("se_getparentid", [this](Vm&, ScriptRun& run, qb::Struct&) {
        Element* e = run.object ? find(run.object) : nullptr;
        if (!e || !e->parent) return false;
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("parent_id"), Vm::makeKey(e->parent->id));
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
            if (r->object == e->id && !r->handler) r->finished = true;
        return true;
    });
    // SetScreenElementLock id on|off: on posts the first-update event
    // (0x3056434f), so the menu lays out once more, after this frame's
    // scrolling-menu update, then suspends its relayout; off relayouts
    // [C 0x82172258 -> 0x8221A588, flag 0x4000] (the calibrate lag menu locks
    // its menu right after making the items: they still get laid out, the
    // window offset by the first one, its highlights calibrate_lag_hilite_pos0..2).
    vm_.addNative("SetScreenElementLock", [this](Vm&, ScriptRun&, qb::Struct& p) {
        Element* e = resolve(Vm::param(p, K("id")));
        if (!e) return false;
        const bool on = Vm::flag(p, K("on"));
        if (on && !e->locked) e->layoutOnce = true;
        e->locked = on;
        if (!on && isMenu(*e)) e->widthKids = size_t(-1), layoutMenu(*e);
        return true;
    });
}

bool UiSystem::drawSprite(Canvas& c, uint32_t texture, float x0, float y0, float x1, float y1, const std::array<uint8_t, 4>& rgba, bool flipU) const {
    const gfx::TextureId id = tex_.find(texture);
    if (!id) return false;
    const float u0 = flipU ? 1.0f : 0.0f, u1 = flipU ? 0.0f : 1.0f;
    auto V = [&](float x, float y, float u, float v) { return Vertex{x, y, 0, u, v, rgba[0], rgba[1], rgba[2], rgba[3]}; };
    const Vertex q[6] = {V(x0, y0, u0, 0), V(x1, y0, u1, 0), V(x1, y1, u1, 1), V(x0, y0, u0, 0), V(x1, y1, u1, 1), V(x0, y1, u0, 1)};
    c.draw(q, 6, int(id.id), Blend::Alpha);
    return true;
}

}  // namespace ui
