// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "char/char_driver.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "scene/milo.h"

namespace chr {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kNever = 1e30f;

// 0x82192610: x mod m in [0, m) (0 when m is 0).
float modPos(float x, float m) {
    if (m == 0) return 0;
    float r = std::fmod(x, m);
    if (r < 0) r += m;
    return r;
}

// The blend curve of a node's progress (0x821A4E30 / 0x8216A588 / 0x821A4988):
// 0.5 - 0.5 cos(pi w) [C: FUN_8226E050 is sin, sin(pi w + pi / 2)].
float eased(float w) { return 0.5f - 0.5f * std::sin(w * kPi + kPi * 0.5f); }

uint32_t alignOf(uint32_t flags) { return (flags >> 12) & 0xf; }

}  // namespace

// ---- transitions -----------------------------------------------------------

bool lastEdge(const scene::CharClipSamples& from, float frame, const scene::CharClipSamples& to, float& src, float& dest) {
    for (const auto& t : from.transitions) {
        if (t.dest != to.name) continue;
        for (size_t k = t.nodes.size(); k-- > 0;)
            if (frame <= t.nodes[k].first) {
                src = t.nodes[k].first, dest = t.nodes[k].second;
                return true;
            }
        return false;
    }
    return false;
}

bool transition(const scene::CharClipSamples& from, float frame, const scene::CharClipSamples& to, uint32_t flags, float& src, float& dest) {
    const uint32_t mode = flags & 0xf;
    if (mode == kPlayNoBlend) return false;
    if (mode == kPlayFirst) {
        // 0x8219E4A8: the first edge at or after the frame.
        for (const auto& t : from.transitions) {
            if (t.dest != to.name) continue;
            for (const auto& [s, d] : t.nodes)
                if (frame <= s) {
                    src = s, dest = d;
                    return true;
                }
            break;
        }
    } else if (mode == kPlayLast && lastEdge(from, frame, to, src, dest)) {
        return true;
    }
    // The default: start now (kPlayLast: not before the older clip's end less
    // half the new blend), at the same phase of the new clip's beat align.
    src = frame;
    if (mode == kPlayLast) src = std::max(src, from.endFrame - to.blendWidth * 0.5f);
    dest = to.startFrame;
    const float a = float(alignOf(to.playFlags));
    if (a > 0) {
        const float d = modPos(src, a) - modPos(to.startFrame, a);
        dest += d;
        if (d < 0) dest += a;
    }
    return true;
}

// ---- ClipSet ---------------------------------------------------------------

ClipSet::ClipSet(scene::Milo& milo) : milo_(milo), path_(milo.path()) {
    const scene::Directory& d = milo.root();
    for (const auto& o : d.objects) {
        if (auto* c = o->as<scene::CharClipSamples>()) {
            clips_.push_back(c);
            byName_.emplace(c->name, c);
            hasFacing_ = hasFacing_ || std::any_of(c->full.bones.begin(), c->full.bones.end(),
                                                   [](const auto& b) { return b.name == "bone_facing.pos"; });
        } else if (auto* g = o->as<scene::CharClipGroup>()) {
            groups_.push_back(g);
            groupByName_.emplace(g->name, g);
            groupIndex_[g] = 0;
            groupOrder_[g] = g->clips;
        }
    }
    if (d.self) data_ = scene::decodeClipSetDir(*d.self, clips_.size());
    // The LRU list starts in the set's clip order (its PostLoad entries).
    if (!data_.clips.empty()) {
        for (const auto& e : data_.clips)
            if (auto* c = clip(e.name)) lru_.push_back(c);
    }
    if (lru_.size() != clips_.size()) lru_.assign(clips_.begin(), clips_.end());
}

scene::CharClipSamples* ClipSet::clip(const std::string& name) const {
    auto it = byName_.find(name);
    return it != byName_.end() && !deleted_.count(it->second) ? it->second : nullptr;
}

scene::CharClipGroup* ClipSet::group(const std::string& name) const {
    auto it = groupByName_.find(name);
    return it == groupByName_.end() ? nullptr : it->second;
}

bool ClipSet::groupHas(const scene::CharClipGroup& g, const scene::CharClipSamples* c) const {
    if (!c) return false;
    auto it = groupOrder_.find(&g);
    if (it == groupOrder_.end()) return false;
    return std::find(it->second.begin(), it->second.end(), c->name) != it->second.end();
}

std::vector<scene::CharClipSamples*> ClipSet::groupClips(const std::string& name) const {
    std::vector<scene::CharClipSamples*> v;
    const scene::CharClipGroup* g = group(name);
    if (!g) return v;
    for (const std::string& n : groupOrder_.at(g))
        if (auto* c = clip(n)) v.push_back(c);
    return v;
}

scene::CharClipSamples* ClipSet::lruByFlags(uint32_t flags) {
    for (auto it = lru_.begin(); it != lru_.end(); ++it) {
        scene::CharClipSamples* c = *it;
        if ((c->flags & flags) == flags) {
            lru_.splice(lru_.end(), lru_, it);
            return c;
        }
    }
    return nullptr;
}

scene::CharClipSamples* ClipSet::lruSafe(uint32_t flags, uint32_t safeFlags, float length) {
    for (auto it = lru_.begin(); it != lru_.end(); ++it) {
        scene::CharClipSamples* c = *it;
        if ((c->flags & flags) != flags) continue;
        // 0x8216A498: not (safe-flagged and at least `length` long).
        if ((c->flags & safeFlags) == 0 || c->endFrame - c->startFrame < length) {
            lru_.splice(lru_.end(), lru_, it);
            return c;
        }
    }
    return nullptr;
}

scene::CharClipSamples* ClipSet::next(scene::CharClipGroup& g) {
    auto& order = groupOrder_[&g];
    // Deleted clips leave the group (ObjPtrList).
    order.erase(std::remove_if(order.begin(), order.end(), [&](const std::string& n) { return !clip(n); }), order.end());
    if (order.empty()) return nullptr;
    int& i = groupIndex_[&g];
    ++i;
    if (size_t(i) >= order.size()) i = 0;
    return clip(order[size_t(i)]);
}

scene::CharClipSamples* ClipSet::nextMatching(scene::CharClipGroup& g, uint32_t flags) {
    auto& order = groupOrder_[&g];
    order.erase(std::remove_if(order.begin(), order.end(), [&](const std::string& n) { return !clip(n); }), order.end());
    int& cur = groupIndex_[&g];
    const int n = int(order.size());
    for (int k = cur + 1; k < n; ++k)
        if (scene::CharClipSamples* c = clip(order[size_t(k)]); c && (c->flags & flags) == flags) return cur = k, c;
    for (int k = 0; k <= cur && k < n; ++k)
        if (scene::CharClipSamples* c = clip(order[size_t(k)]); c && (c->flags & flags) == flags) return cur = k, c;
    return nullptr;
}

void ClipSet::shuffleGroups(Random& rng) {
    for (scene::CharClipGroup* g : groups_) {
        auto& order = groupOrder_[g];
        for (size_t i = 0; i < order.size(); ++i) std::swap(order[i], order[size_t(rng.integer(int(i), int(order.size())))]);
    }
}

void ClipSet::orGroupFlags(const std::string& g, uint32_t flags) {
    for (scene::CharClipSamples* c : groupClips(g)) c->flags |= flags;
}

void ClipSet::deleteClip(scene::CharClipSamples* c) {
    if (!c || deleted_.count(c)) return;
    deleted_[c] = true;
    lru_.remove(c);
}

void ClipSet::deleteDomainAny(uint32_t a, uint32_t b, bool keep) {
    for (scene::CharClipSamples* c : clips_) {
        if (deleted_.count(c)) continue;
        const bool in = (c->flags & a) != 0 || (c->flags & b) == 0;
        if (in != keep) deleteClip(c);
    }
}

void ClipSet::deleteGroupClips(const std::string& g) {
    for (scene::CharClipSamples* c : groupClips(g)) deleteClip(c);
}

void ClipSet::trimGroup(const std::string& g, size_t n) {
    // 0x82175B20 -> 0x8219C368: clips carrying the keep mark (play flags
    // 0x10000) go to the end and count against `n`; the last of the others
    // up to `n` are marked; the rest are deleted.
    std::vector<scene::CharClipSamples*> v = groupClips(g);
    int count = int(v.size());
    int keep = std::min(int(n), count);
    for (int i = 0; i < count; ++i)
        if (v[size_t(i)]->playFlags & 0x10000) {
            --count, --keep;
            std::swap(v[size_t(i)], v[size_t(count)]);
            --i;
        }
    for (int k = 0; k < keep; ++k) v[size_t(count - 1 - k)]->playFlags |= 0x10000;
    for (int i = 0; i < count - std::max(keep, 0); ++i) deleteClip(v[size_t(i)]);
}

void ClipSet::listChannels(Bones& bones, bool delta) const {
    if (hasFacing_) {
        bones.add("bone_facing.pos");
        bones.add("bone_facing.rotz");
        if (delta) {
            bones.add("bone_facing_delta.pos");
            bones.add("bone_facing_delta.rotz");
        }
    }
    // The skeleton: CharBone entries ("bone_x.trans": position, scale,
    // rotation and delta types), else every channel the clips carry.
    bool any = false;
    for (const auto& o : milo_.root().objects) {
        auto* b = o->as<scene::CharBone>();
        if (!b) continue;
        any = true;
        std::string base = b->name;
        if (size_t dot = base.rfind('.'); dot != std::string::npos) base = base.substr(0, dot);
        static const char* kTypes[] = {"pos", "scale", "quat", "rotx", "roty", "rotz", "drotx", "droty", "drotz"};
        if (b->position) bones.add(base + ".pos");
        if (b->scale) bones.add(base + ".scale");
        if (b->rotation < 9) bones.add(base + "." + kTypes[b->rotation]);
        if (b->deltaRotation < 9) bones.add(base + "." + kTypes[b->deltaRotation]);
    }
    if (any) return;
    for (const scene::CharClipSamples* c : clips_) {
        bones.addChannelsOf(c->one);
        for (const auto& b : c->full.bones)
            if (b.name != "bone_facing.pos" && b.name != "bone_facing.rotz") bones.add(b.name);
    }
}

// ---- ClipDriver ------------------------------------------------------------

ClipDriver::ClipDriver(std::string name, ClipSet* set) : name_(std::move(name)), set_(set) {}

scene::CharClipSamples* ClipDriver::resolve(const ClipSel& sel) {
    if (!set_) return nullptr;
    switch (sel.kind) {
    case ClipSel::Flags: return set_->lruByFlags(sel.flags);
    case ClipSel::Name: {
        if (auto* c = set_->clip(sel.name)) return c;
        if (auto* g = set_->group(sel.name)) return set_->next(*g);
        return nullptr;
    }
    case ClipSel::List:
        // 0x8219C8B0 over a list: the first LRU clip matching any element.
        for (const ClipSel& s : sel.list)
            if (auto* c = resolve(s)) return c;
        return nullptr;
    }
    return nullptr;
}

scene::CharClipSamples* ClipDriver::play(const ClipSel& sel, uint32_t flags) {
    lastSel_ = sel, hasLastSel_ = true;
    scene::CharClipSamples* c = resolve(sel);
    if (!c) {
        if (set_)
            SDL_Log("char: %s: no clip for %s in %s", name_.c_str(),
                    sel.kind == ClipSel::Flags ? ("flags " + std::to_string(sel.flags)).c_str() : sel.name.c_str(), set_->path().c_str());
        return nullptr;
    }
    push(c, flags, 0, 0, false);
    return c;
}

scene::CharClipSamples* ClipDriver::playClip(scene::CharClipSamples* clip, uint32_t flags) {
    if (!clip) return nullptr;
    push(clip, flags, 0, 0, false);
    return clip;
}

scene::CharClipSamples* ClipDriver::playGroup(const std::string& group, uint32_t flags) {
    if (!set_) return nullptr;
    scene::CharClipGroup* g = set_->group(group);
    scene::CharClipSamples* c = g ? set_->next(*g) : nullptr;
    if (!c) {
        SDL_Log("char: %s: no clip in group %s of %s", name_.c_str(), group.c_str(), set_->path().c_str());
        return nullptr;
    }
    push(c, flags, 0, 0, false);
    return c;
}

scene::CharClipSamples* ClipDriver::playIfSafe(uint32_t flags, uint32_t playFlags, uint32_t safeFlags, float length) {
    if (!set_) return nullptr;
    scene::CharClipSamples* c = set_->lruSafe(flags, safeFlags, length);
    if (!c) {
        SDL_Log("char: %s: play_if_safe found no clip (flags %#x safe %#x length %.2f)", name_.c_str(), flags, safeFlags, double(length));
        return nullptr;
    }
    push(c, playFlags, 0, 0, false);
    return c;
}

scene::CharClipSamples* ClipDriver::playAt(scene::CharClipSamples* clip, uint32_t flags, float frame, float delay, float blendWidth) {
    if (!clip) return nullptr;
    Node* n = push(clip, flags, frame, delay, true);
    if (blendWidth >= 0) n->blendWidth = blendWidth;
    return clip;
}

// 0x821A4BA8.
ClipDriver::Node* ClipDriver::push(scene::CharClipSamples* clip, uint32_t flags, float frame, float delay, bool explicitStart) {
    Node n;
    n.clip = clip;
    n.flags = clip->playFlags;
    if (flags & 0xf0) n.flags = (n.flags & ~0xf0u) | (flags & 0xf0);
    if (flags & 0xf) n.flags = (n.flags & ~0xfu) | (flags & 0xf);
    if (flags & 0xf600) n.flags = (n.flags & ~0xf600u) | (flags & 0xf600);
    n.blendWidth = clip->blendWidth;
    n.rate = 1;
    // Older nodes that have not started are dropped (0x821A4AF0).
    while (!chain_.empty() && chain_.back().weight == 0) {
        Node old = chain_.back();
        chain_.pop_back();
        if (runScript && old.clip && !old.clip->exitEvent.empty()) fire(old.clip, old.clip->exitEvent);
    }
    // The enter event (clip +0x3c) with $dude.
    if (runScript && !clip->enterEvent.empty()) fire(clip, clip->enterEvent);
    // The first event.
    if (!clip->events.empty()) n.eventIndex = 0, n.nextEvent = clip->events[0].frame;
    if (!explicitStart) {
        if ((n.flags & 0xf) == kPlayNoBlend) deleteOlder(chain_.size());
        if (chain_.empty()) {
            n.delay = 0;
            n.frame = clip->startFrame;
            n.weight = 1;
        } else {
            // 0x8219E598: the older clip's transition table, else the default.
            const Node& o = chain_.back();
            float src = 0, dest = 0;
            transition(*o.clip, o.frame, *clip, n.flags, src, dest);
            n.frame = dest;
            n.delay = src - o.frame;
            n.weight = 0;
        }
    } else {
        n.frame = frame;
        n.delay = delay;
        n.weight = 0;
    }
    if ((n.flags & 0xf) == kPlayDirty) n.weight = 1e-6f;
    // A clip that starts at full weight with a range starts at a random phase
    // (clip +0x34 [C 0x821A4BA8; I: the meaning of the field]).
    if (n.weight == 1 && clip->range > 0) {
        const float r = rng ? rng->real(0, clip->range) : 0;
        n.frame = clip->startFrame + modPos(n.frame + r - clip->startFrame, (clip->startFrame + clip->endFrame) * 0.5f - clip->startFrame);
    }
    n.blendWeight = 0;
    chain_.push_back(n);
    if (onPlay) onPlay(*clip, n.flags);
    return &chain_.back();
}

void ClipDriver::deleteOlder(size_t count) {
    // 0x821A48E8: exit events of the deleted nodes, newest first.
    for (size_t k = count; k-- > 0;)
        if (runScript && chain_[k].clip && !chain_[k].clip->exitEvent.empty()) fire(chain_[k].clip, chain_[k].clip->exitEvent);
    chain_.erase(chain_.begin(), chain_.begin() + std::ptrdiff_t(count));
}

void ClipDriver::setBeatScale(float scale) {
    if (scale == 0) return;
    // 0x8216AE90: the first started node and the older ones keep their speed.
    for (size_t k = chain_.size(); k-- > 0;) {
        if (chain_[k].weight == 0) continue;
        const uint32_t f = chain_[k].flags;
        if ((f & 0xf600) != kPlayRealTime) {
            for (size_t j = 0; j <= k; ++j) {
                chain_[j].rate *= beatScale / scale;
                chain_[j].flags &= 0xffff0fffu;
            }
            beatScale = scale;
            playClip(chain_[k].clip, 0x38);
            return;
        }
        break;
    }
    beatScale = scale;
}

void ClipDriver::offset(float frames) {
    if (!chain_.empty()) chain_.back().frame += frames;
}

float ClipDriver::alignCorrection(const Node& n, double beat) const {
    // 0x821A4A50.
    const float a = float(alignOf(n.flags));
    if (a == 0 || n.rate != 1 || (n.flags & 0xf0) == kPlayLoop) return 0;
    float d = modPos(float(beat) - n.frame, a);
    if (a * 0.5f < d) d -= a;
    return d;
}

void ClipDriver::runEvents(Node& n, float from, float to) {
    if (n.eventIndex < 0 || !n.clip || n.clip->events.empty()) return;
    const auto& ev = n.clip->events;
    int first = -1;
    for (;;) {
        if (n.nextEvent <= from || to <= n.nextEvent) break;
        if (first < 0) first = n.eventIndex;
        if (runScript) fire(n.clip, ev[size_t(n.eventIndex)].script);
        // FUN_821A4B40: the next event, wrapping.
        n.eventIndex = (n.eventIndex + 1) % int(ev.size());
        n.nextEvent = ev[size_t(n.eventIndex)].frame;
        if (n.eventIndex == first) break;
    }
}

// 0x821A4E30 for node i (the older ones already advanced).
void ClipDriver::advance(size_t i, double beat, float dBeats, float dSeconds, float& total) {
    Node& n = chain_[i];
    const bool realtime = (n.flags & kPlayRealTime) != 0;
    const bool usertime = (n.flags & kPlayUserTime) != 0;
    const float olderTotal = total;
    float dt = i > 0 ? chain_[i - 1].advancedAll : (realtime ? dSeconds : dBeats);
    const float before = n.delay;
    n.delay = before - dt;
    if (0 <= n.delay) {
        n.advanced = 0, n.advancedAll = 0;
    } else {
        const float from = n.frame;
        if (!usertime) n.advanced = n.rate * (realtime ? n.clip->framesPerSec * dSeconds : dBeats);
        n.frame += n.advanced;
        const float corr = alignCorrection(n, beat);
        n.frame += corr;
        n.advancedAll = corr + n.advanced;
        runEvents(n, from, n.frame);
        if (n.weight < 1) {
            if (n.blendWidth == 0) n.weight = 1;
            else n.weight += (usertime ? dBeats : n.advanced) / n.blendWidth;
            if (n.weight > 1) n.weight = 1;
        }
    }
    if (i > 0 && n.weight == 1) {
        deleteOlder(i);
        i = 0;
    }
    Node& m = chain_[i];
    if ((m.flags & 0xf0) == kPlayLoop) {
        const float over = m.frame - m.clip->endFrame;
        if (0 < over) {
            m.frame = m.clip->startFrame + over;
            m.frame += alignCorrection(m, beat);
        }
    }
    const float e = eased(m.weight);
    total = (1 - e) * olderTotal + e;
}

void ClipDriver::poll(double beat, float dBeats, float dSeconds, Bones* bones) {
    if (chain_.empty()) return;
    // 0x8216AF70: the advance gets the song beat and the beats of this poll
    // times the beat scale, and the seconds as they are (TaskMgr 0x82311988
    // beat, 0x82311998 delta beats, 0x82311970 delta seconds).
    const float dB = dBeats * beatScale, dS = dSeconds;
    const double sb = beat * double(beatScale);
    // Realign (drummer: main.drv set realign TRUE): when the song beat jumps
    // by other than one whole beat, the chain's beat align is dropped and
    // the newest started clip replayed (GraphLoop | Dirty).
    if (realign && sb > 0) {
        if (lastBeat_ == kNever) lastBeat_ = float(sb);
        const int last = int(std::floor(lastBeat_)), now = int(std::floor(sb));
        if (last != now) {
            size_t s = chain_.size();
            for (size_t k = chain_.size(); k-- > 0;)
                if (chain_[k].weight != 0) {
                    s = k;
                    break;
                }
            if (s < chain_.size()) {
                uint32_t own = alignOf(chain_[s].flags), most = own;
                for (size_t k = 0; k < s; ++k) most = std::max(most, alignOf(chain_[k].flags));
                if (most > 1) {
                    const uint32_t x = uint32_t(last + 1) ^ uint32_t(now);
                    if (x & (most - 1)) {
                        for (size_t k = 0; k <= s; ++k) chain_[k].flags &= 0xffff0fffu;
                        if (own > 1 && (x & (own - 1))) playClip(chain_[s].clip, 0x38);
                    }
                }
            }
        }
        lastBeat_ = float(sb);
    }
    const uint32_t flags0 = chain_.back().flags;
    if (chain_.size() == 1 && !starved.empty() && sendStarved) sendStarved(starved);
    if (chain_.size() == 1 && (flags0 & 0xf0) == kPlayGraphLoop) playClip(chain_.back().clip, (chain_.back().flags & ~0xfu) | kPlayLast);
    if (chain_.size() == 1 && (flags0 & 0xf0) == kPlayNodeLoop && hasLastSel_) play(lastSel_, 0x44);
    if (chain_.empty()) return;
    float total = 0;
    for (size_t i = 0; i < chain_.size(); ++i) {
        const size_t before = chain_.size();
        advance(i, sb, dB, dS, total);
        if (chain_.size() < before) i = 0;  // older nodes freed: this node is now the first
    }
    if (!bones) return;
    const float w = weight ? weight() : 1.0f;
    if (!skipScaleDown) bones->scaleDown(*chain_.back().clip, 1 - total * w);
    // 0x821A4988: newest first, each its eased share of what is left.
    float left = w;
    for (size_t k = chain_.size(); k-- > 0 && left != 0;) {
        Node& n = chain_[k];
        n.blendWeight = eased(n.weight) * left;
        bones->scaleAdd(*n.clip, n.blendWeight, n.frame, n.advanced);
        left -= n.blendWeight;
    }
}

float ClipDriver::flagWeight(uint32_t flags) const {
    // 0x8216A588: newest first, each weight eased and scaled by what the
    // newer nodes leave.
    float sum = 0, left = 1;
    for (size_t k = chain_.size(); k-- > 0;) {
        const float e = eased(chain_[k].weight);
        if (chain_[k].clip->flags & flags) sum += e * left;
        left *= 1 - e;
    }
    return sum;
}

scene::CharClipSamples* ClipDriver::firstPlaying() const {
    for (size_t k = chain_.size(); k-- > 0;)
        if (chain_[k].weight != 0) return chain_[k].clip;
    return nullptr;
}

uint32_t ClipDriver::firstFlags() const {
    const scene::CharClipSamples* c = firstPlaying();
    return c ? c->flags : 0;
}

void ClipDriver::fire(const scene::CharClipSamples* clip, const std::string& script) {
    const scene::CharClipSamples* was = scriptClip;
    scriptClip = clip;
    runScript(script);
    scriptClip = was;
}

int ClipDriver::dominant() const {
    // 0x8216B3A0: newest first, each eased weight scaled by what the newer
    // nodes leave; the largest share (the newest when none is above 0).
    int best = chain_.empty() ? -1 : int(chain_.size()) - 1;
    float most = 0, left = 1;
    for (size_t k = chain_.size(); k-- > 0;) {
        const float e = eased(chain_[k].weight);
        if (e * left > most) most = e * left, best = int(k);
        left *= 1 - e;
    }
    return best;
}

int ClipDriver::newestStarted() const {
    for (size_t k = chain_.size(); k-- > 0;)
        if (chain_[k].weight != 0) return int(k);
    return -1;
}

void ClipDriver::dropUnstarted() {
    if (chain_.size() > 1 && chain_.back().weight == 0) chain_.pop_back();
}

}  // namespace chr
