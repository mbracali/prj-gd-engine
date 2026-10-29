// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// GH2's store (career.dta's store_screen; docs/career_and_store.md):
// StorePanel (handler 0x8229D678) and the item providers the scripts name.
#include "ui/store.h"

#include <algorithm>

#include "scene/ui_styles.h"
#include "ui/campaign.h"
#include "ui/char_select.h"
#include "ui/ui_manager.h"
#include "ui/ui_objects.h"

namespace ui::store {

namespace {

std::string nameOf(const Value& v) { return v.type == Value::Type::Object && v.object ? v.object->name() : v.str(); }
int intArg(const std::vector<Value>& a, size_t i, int def = 0) { return i < a.size() && a[i].isNumber() ? a[i].integerValue() : def; }
std::string strArg(const std::vector<Value>& a, size_t i) { return i < a.size() ? nameOf(a[i]) : std::string(); }

// A store.dta category's (item, price) pairs (a copy: a handful of items).
std::vector<std::pair<std::string, int>> category(UiManager& ui, const std::string& c) {
    const auto& s = campaign::config(ui).store;
    auto it = s.find(c);
    return it == s.end() ? std::vector<std::pair<std::string, int>>{} : it->second;
}

// A list of symbols with GH2's provider messages (list_length, get_symbol,
// get_text: the symbol, localized by the label).
class SymbolProvider : public ListProvider {
public:
    SymbolProvider(UiManager& ui, const std::string& name, const std::string& cls) : ListProvider(name, cls), ui_(ui) {}
    int numData() override { return int(items_.size()); }
    std::string symbol(int i) override { return i >= 0 && i < int(items_.size()) ? items_[size_t(i)] : std::string(); }
    void rows(std::vector<gfx::UiView::ListRow>& out, int selected, int& selectedRow) override {
        out.clear();
        selectedRow = std::max(0, selected);
        for (const std::string& s : items_) {
            gfx::UiView::ListRow r;
            r.texts["list.txt"] = ui_.localize(s);
            out.push_back(std::move(r));
        }
    }
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        if (msg == "list_length" || msg == "num_data") return r = Value::integer(numData()), true;
        if (msg == "get_symbol" || msg == "get_text") return r = Value::symbol(symbol(intArg(args, 0))), true;
        return extra(msg, args, r);
    }
    virtual bool extra(const std::string&, std::vector<Value>&, Value&) { return false; }

protected:
    UiManager& ui_;
    std::vector<std::string> items_;
};

// store_item_provider (0x822C0658): a store.dta category's items.
class ItemProvider : public SymbolProvider {
public:
    explicit ItemProvider(UiManager& ui) : SymbolProvider(ui, "store_item_provider", "StoreItemProvider") {}
    bool extra(const std::string& msg, std::vector<Value>& args, Value&) override {
        if (msg == "set_category") {
            items_.clear();
            for (const auto& [item, price] : category(ui_, strArg(args, 0))) items_.push_back(item);
            return true;
        }
        // An achievement once every item of a category is bought (0x822BF840): Xbox Live's.
        if (msg == "check_achievement" || msg == "init_data") return true;
        return false;
    }
};

// store_guitar_provider: store.dta's guitars (0x822C44D0); a tour reward is
// in stock once its tour is passed / five-starred (0x822C02A8).
class GuitarProvider : public SymbolProvider {
public:
    explicit GuitarProvider(UiManager& ui) : SymbolProvider(ui, "store_guitar_provider", "StoreGuitarProvider") {}
    void init() {
        items_.clear();
        for (const auto& [item, price] : category(ui_, "guitar")) items_.push_back(item);
    }
    bool extra(const std::string& msg, std::vector<Value>& args, Value& r) override {
        if (msg == "init_data") return init(), true;
        const std::string g = symbol(intArg(args, 0));
        const campaign::Config::Reward* reward = nullptr;
        for (const auto& rw : campaign::config(ui_).rewards)
            if (rw.guitar == g) reward = &rw;
        if (msg == "in_stock") {
            bool in = true;
            if (reward) {
                Career::Profile* p = campaign::profile(ui_);
                const int d = reward->difficulty;
                in = ui_.session().allAccess ||
                     (p && (reward->fiveStar ? campaign::allFiveStars(ui_, *p, d) : campaign::tiersCompleted(ui_, *p, d) == campaign::numTiers(ui_, d)));
            }
            return r = Value::integer(in ? 1 : 0), true;
        }
        if (msg == "get_type") return r = Value::symbol(reward ? (reward->fiveStar ? "tour_5_star" : "tour_passed") : "guitar"), true;
        if (msg == "difficulty") return r = Value::integer(reward ? reward->difficulty : 0), true;
        return false;
    }
};

// store_skin_provider (0x822C6128): the skins the store sells (the
// profile's table order: guitars.dta's) of the guitars the band has.
class SkinProvider : public SymbolProvider {
public:
    explicit SkinProvider(UiManager& ui) : SymbolProvider(ui, "store_skin_provider", "StoreSkinProvider") {}
    void init() {
        items_.clear();
        for (const chars::Guitar& g : chars::data(ui_).guitars)
            for (const chars::Skin& s : g.skins)
                if (campaign::price(ui_, "skin", s.name) >= 0 && campaign::isUnlocked(ui_, g.name)) items_.push_back(s.name);
    }
    bool extra(const std::string& msg, std::vector<Value>& args, Value& r) override {
        if (msg == "init_data") return init(), true;
        if (msg == "get_guitar") {
            // 0x822C04C0: the guitar the skin belongs to.
            const std::string skin = symbol(intArg(args, 0));
            for (const chars::Guitar& g : chars::data(ui_).guitars)
                for (const chars::Skin& s : g.skins)
                    if (s.name == skin) return r = Value::symbol(g.name), true;
            return r = Value::symbol(""), true;
        }
        return false;
    }
};

// store_video_provider (0x822C3D10): store.dta's videos and their files.
class VideoProvider : public SymbolProvider {
public:
    explicit VideoProvider(UiManager& ui) : SymbolProvider(ui, "store_video_provider", "StoreVideoProvider") {}
    bool extra(const std::string& msg, std::vector<Value>& args, Value& r) override {
        if (msg == "init_data") {
            items_.clear();
            for (const auto& [item, price] : category(ui_, "video")) items_.push_back(item);
            return true;
        }
        if (msg == "get_video_file") {
            const DataNode* st = ui_.bootTree() ? ui_.bootTree()->find("store") : nullptr;
            const DataNode* v = st ? st->find("video") : nullptr;
            const DataNode* it = v ? v->find(symbol(intArg(args, 0))) : nullptr;
            const DataNode* f = it ? it->find("file") : nullptr;
            return r = Value::symbol(f && f->children.size() > 1 ? f->textAt(1) : std::string()), true;
        }
        return false;
    }
};

// StorePanel (0x8229D678): store.dta's prices.
class StorePanelObj : public PanelObj {
public:
    StorePanelObj(UiManager& ui, const std::string& name) : PanelObj(ui, name, "StorePanel") {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        if (msg == "price") return r = Value::integer(std::max(0, campaign::price(ui, strArg(args, 0), strArg(args, 1)))), true;
        if (msg == "low_cost" || msg == "high_cost") {
            // 0x8229D518 / 0x8229D5C8: the category's lowest / highest price.
            int lo = 10000000, hi = 0;
            for (const auto& [item, price] : category(ui, strArg(args, 0))) lo = std::min(lo, price), hi = std::max(hi, price);
            return r = Value::integer(msg == "low_cost" ? lo : hi), true;
        }
        return PanelObj::handleNative(in, msg, args, r);
    }
};

}  // namespace

std::unique_ptr<ScriptObject> makeClass(UiManager& ui, const std::string& cls, const std::string& name) {
    if (cls == "StorePanel") return std::make_unique<StorePanelObj>(ui, name);
    return nullptr;
}

void installNatives(UiManager& ui, Interp& in, std::vector<std::unique_ptr<ScriptObject>>& owned) {
    auto add = [&](std::unique_ptr<ScriptObject> o) {
        in.registerObject(o.get());
        owned.push_back(std::move(o));
    };
    add(std::make_unique<ItemProvider>(ui));
    add(std::make_unique<GuitarProvider>(ui));
    add(std::make_unique<SkinProvider>(ui));
    add(std::make_unique<VideoProvider>(ui));
}

}  // namespace ui::store
