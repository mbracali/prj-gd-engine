// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "scene/scenetest.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "data/game_data.h"
#include "scene/milo.h"
#include "scene/synth_objects.h"
#include "scene/ui_objects.h"
#include "scene/ui_styles.h"
#include "scene/world_objects.h"

namespace {

using namespace scene;

struct ClassStats {
    bool typed = false;
    int objects = 0, decoded = 0, files = 0;
    std::set<uint32_t> versions;
    std::vector<std::string> failures;
};

std::string join(const std::vector<std::string>& v, size_t max = 8) {
    std::string s;
    for (size_t i = 0; i < v.size() && i < max; ++i) s += (i ? ", " : "") + v[i];
    if (v.size() > max) s += ", ...";
    return s;
}

void dumpObject(const Object& o, int indent) {
    std::printf("%*s%-12s v%-3u %7zu  %s%s%s\n", indent, "", o.cls.c_str(), o.version, o.record.size(), o.name.c_str(),
                o.typed && !o.decoded ? "  FAILED: " : "", o.typed && !o.decoded ? o.error.c_str() : "");
    if (!o.decoded) return;
    std::string pad(size_t(indent) + 6, ' ');
    if (!o.header.type.empty() || o.header.hasProps)
        std::printf("%stype '%s'%s\n", pad.c_str(), o.header.type.c_str(), o.header.hasProps ? " +props" : "");
    if (auto* t = o.as<Tex>()) {
        static const char* fmt[] = {"none", "DXT1", "DXT5", "ATI2", "other"};
        std::printf("%s%dx%d %s mips %zu file '%s' type %#x\n", pad.c_str(), t->bitmap.width, t->bitmap.height,
                    fmt[int(t->bitmap.format)], t->bitmap.levels.size(), t->file.c_str(), t->type);
    } else if (auto* m = o.as<Mat>()) {
        std::printf("%sblend %d color %.2f %.2f %.2f %.2f z %d cut %d write %d gen %d wrap %d diffuse '%s'%s%s\n", pad.c_str(),
                    m->blend, m->color[0], m->color[1], m->color[2], m->color[3], m->zMode, m->alphaCut, m->alphaWrite,
                    m->texGen, m->texWrap, m->diffuseTex.c_str(), m->normalMap.empty() ? "" : (" normal '" + m->normalMap + "'").c_str(),
                    m->emissiveMap.empty() ? "" : (" emissive '" + m->emissiveMap + "'").c_str());
    } else if (auto* m = o.as<Mesh>()) {
        std::printf("%smat '%s' %zu verts %zu faces %zu groups%s parent '%s' showing %d order %g pos %.1f %.1f %.1f\n", pad.c_str(),
                    m->mat.c_str(), m->verts.size(), m->faces.size(), m->groupSizes.size(), m->bones.empty() ? "" : " skinned",
                    m->trans.parent.c_str(), m->draw.showing, m->draw.order, m->trans.local.pos[0], m->trans.local.pos[1], m->trans.local.pos[2]);
    } else if (auto* g = o.as<Group>()) {
        std::printf("%sobjects [%s] showing %d order %g environ '%s' lod '%s'\n", pad.c_str(), join(g->objects, 12).c_str(), g->draw.showing,
                    g->draw.order, g->environment.c_str(), g->lod.c_str());
    } else if (auto* a = o.as<TransAnim>()) {
        std::printf("%strans '%s' rot %zu trans %zu scale %zu owner '%s' last frame %.1f\n", pad.c_str(), a->trans.c_str(),
                    a->rotKeys.size(), a->transKeys.size(), a->scaleKeys.size(), a->keysOwner.c_str(),
                    lastFrame(a->rotKeys, a->transKeys, a->scaleKeys));
    } else if (auto* a = o.as<MatAnim>()) {
        std::printf("%smat '%s' color %zu alpha %zu trans %zu scale %zu rot %zu tex %zu last frame %.1f\n", pad.c_str(),
                    a->mat.c_str(), a->colorKeys.size(), a->alphaKeys.size(), a->transKeys.size(), a->scaleKeys.size(),
                    a->rotKeys.size(), a->texKeys.size(),
                    std::max(lastFrame(a->colorKeys, a->alphaKeys, a->transKeys, a->scaleKeys, a->rotKeys),
                             a->texKeys.empty() ? 0.0f : a->texKeys.back().frame));
    } else if (auto* f = o.as<AnimFilter>()) {
        std::printf("%sanim '%s' scale %.2f offset %.2f start %.1f end %.1f type %d period %.1f\n", pad.c_str(), f->anim.c_str(),
                    f->scale, f->offset, f->start, f->end, f->type, f->period);
    } else if (auto* t = o.as<UITrigger>()) {
        std::printf("%sevent '%s' anim '%s' block_transition %d\n", pad.c_str(), t->event.c_str(), t->anim.c_str(), t->blockTransition);
    } else if (auto* c = o.as<Cam>()) {
        std::printf("%snear %.2f far %.1f yfov %.3f\n", pad.c_str(), c->nearPlane, c->farPlane, c->yFov);
    } else if (auto* l = o.as<Light>()) {
        std::printf("%stype %d range %.1f color %.2f %.2f %.2f\n", pad.c_str(), l->type, l->range, l->color[0], l->color[1],
                    l->color[2]);
    } else if (auto* w = o.as<TrackWidget>()) {
        std::printf("%smeshes [%s] environ '%s' base_length %.2f multi_mesh %d allow_rotation %d\n", pad.c_str(),
                    join(w->meshes, 12).c_str(), w->environment.c_str(), w->baseLength, w->multiMesh, w->allowRotation);
    } else if (auto* d = o.as<DirObj>()) {
        const auto& od = d->objectDir;
        std::printf("%sproxy '%s' inline %u subdirs [%s] refs '%s' '%s' type '%s'\n", pad.c_str(), od.proxyFile.c_str(),
                    od.inlineProxy, join(od.subdirs).c_str(), od.postRef0.c_str(), od.postRef1.c_str(), d->header.type.c_str());
        if (d->hasRnd)
            std::printf("%sRndDir environ '%s' showing %d\n", pad.c_str(), d->rnd.environment.c_str(), d->rnd.draw.showing);
        if (d->hasPanel && d->own)
            std::printf("%sPanelDir cam '%s' trigger '%s'\n", pad.c_str(), d->panel.cam.c_str(), d->panel.enterTrigger.c_str());
        if (d->hasTrack && d->own) {
            const auto& t = d->track;
            std::printf("%sTrackDir draw '%s' anim '%s' y_per_second %.2f top_y %.2f bottom_y %.2f slots", pad.c_str(),
                        t.drawGroup.c_str(), t.animGroup.c_str(), t.yPerSecond, t.topY, t.bottomY);
            for (const auto& sl : t.slots) std::printf(" (%.2f %.2f)", sl[0], sl[1]);
            std::printf(" warn %d legacy '%s' %u\n", t.warnOnResort, t.legacyWidget.c_str(), t.legacyValue);
        }
        if (d->hasHud) {
            std::string v;
            for (uint32_t x : d->hud.values) v += (v.empty() ? "" : " ") + std::to_string(x);
            std::printf("%s%s refs [%s] values [%s]\n", pad.c_str(), d->cls.c_str(), join(d->hud.refs, 40).c_str(), v.c_str());
        }
        if (d->hasGHTrack) {
            const auto& g = d->ghTrack;
            std::printf("%sGHTrackDir %d sp_camera '%s' mp_cameras [%s]\n%s  gems [%s]\n%s  hopos [%s]\n%s  stars [%s]\n%s  star hopos [%s]\n",
                        pad.c_str(), g.unknown, g.spCamera.c_str(), join(g.mpCameras).c_str(), pad.c_str(),
                        join(g.gemWidgets).c_str(), pad.c_str(), join(g.hopoWidgets).c_str(), pad.c_str(),
                        join(g.starWidgets).c_str(), pad.c_str(), join(g.starHopoWidgets).c_str());
        }
    } else if (auto* c = o.as<CamShot>()) {
        std::printf("%scategory %s, %zu keyframes, %.0f frames, near %.1f far %.0f, path '%s', flags facing '%s' distance '%s' postprocess '%s'\n",
                    pad.c_str(), c->category.c_str(), c->keyframes.size(), c->lengthFrames(), c->nearPlane, c->farPlane, c->path.c_str(),
                    c->flag("facing").c_str(), c->flag("distance").c_str(), c->flag("postprocess").c_str());
    } else if (auto* p = o.as<LightPreset>()) {
        std::printf("%scategory %s, %zu keyframes, %zu spots %zu environs %zu lights, adjectives [%s], ex %d..%d%s%s\n", pad.c_str(),
                    p->category.c_str(), p->keyframes.size(), p->spotlights.size(), p->environs.size(), p->lights.size(),
                    join(p->adjectives).c_str(), p->minExcitement, p->maxExcitement, p->looping ? " looping" : "", p->manual ? " manual" : "");
    } else if (auto* t = o.as<EventTrigger>()) {
        std::string anims;
        for (const auto& a : t->anims) anims += (anims.empty() ? "" : ", ") + a.anim;
        std::printf("%sevents [%s] anims [%s] shows [%s] hides %zu proxy calls %zu sounds [%s]\n", pad.c_str(), join(t->triggerEvents).c_str(),
                    anims.c_str(), join(t->shows).c_str(), t->hideDelays.size(), t->proxyCalls.size(), join(t->sounds).c_str());
    } else if (auto* sp = o.as<Spotlight>()) {
        std::printf("%sbeam '%s' spot '%s' target '%s' intensity %.2f color %.2f %.2f %.2f showing %d\n", pad.c_str(), sp->beam.material.c_str(),
                    sp->spotMaterial.c_str(), sp->target.c_str(), sp->intensity, sp->color[0], sp->color[1], sp->color[2], sp->draw.showing);
    } else if (auto* s = o.as<SynthSample>()) {
        std::printf("%sfile '%s' format %u, %u frames at %u Hz, %u bytes%s\n", pad.c_str(), s->file.c_str(), s->sample.format,
                    s->sample.sampleCount, s->sample.rate, s->sample.size,
                    s->looping ? (" loop " + std::to_string(s->loopStart) + ".." + std::to_string(s->loopEnd)).c_str() : "");
    } else if (auto* x = o.as<Sfx>()) {
        std::string maps;
        for (const auto& m : x->maps)
            maps += (maps.empty() ? "" : ", ") + m.sample + " (" + std::to_string(m.volume) + " dB pan " + std::to_string(m.pan) + ")";
        std::printf("%smaps [%s] send '%s'\n", pad.c_str(), maps.c_str(), x->send.c_str());
    } else if (auto* g = o.as<GroupSeq>()) {
        std::printf("%schildren [%s] volume %.1f+-%.1f transpose %.1f+-%.1f\n", pad.c_str(), join(g->children).c_str(),
                    g->params.avgVolume, g->params.volumeSpread, g->params.avgTranspose, g->params.transposeSpread);
    } else if (auto* q = o.as<SfxSeq>()) {
        std::printf("%schild '%s'\n", pad.c_str(), q->child.c_str());
    } else if (auto* e = o.as<Environ>()) {
        std::printf("%slights [%s] ambient %.2f %.2f %.2f\n", pad.c_str(), join(e->lights).c_str(), e->ambientColor[0],
                    e->ambientColor[1], e->ambientColor[2]);
    }
}

void dumpDir(const Directory& d, int indent) {
    std::printf("%*s%s '%s' (%zu objects, own record v%u, %zu bytes)\n", indent, "", d.cls.c_str(), d.name.c_str(),
                d.objects.size(), d.self ? d.self->version : 0, d.self ? d.self->record.size() : 0);
    if (d.self) dumpObject(*d.self, indent + 2);
    for (const auto& o : d.objects) {
        dumpObject(*o, indent + 2);
        for (const auto& [owner, sub] : d.subdirs)
            if (owner == o->name) dumpDir(*sub, indent + 4);
    }
}

}  // namespace

int runSceneTest(const std::string& dataPath) {
    try {
        GameData data(dataPath);
        std::vector<std::string> paths;
        for (const auto& p : data.paths())
            if (p.size() > 9 && p.compare(p.size() - 9, 9, ".milo_ps2") == 0) paths.push_back(p);
        std::sort(paths.begin(), paths.end());

        std::map<std::string, ClassStats> stats;
        std::map<std::string, int> dirClasses;
        int files = 0, fileFailures = 0, inlineDirs = 0, proxiesFound = 0, proxiesMissing = 0;
        std::vector<std::string> missingProxies;
        std::map<std::string, int> rawInUi;  // untyped objects in ui/ milos, per class
        for (const auto& p : paths) {
            std::unique_ptr<Milo> milo;
            try {
                milo = std::make_unique<Milo>(data.read(p), p);
            } catch (const std::exception& e) {
                std::fprintf(stderr, "%s: %s\n", p.c_str(), e.what());
                ++fileFailures;
                continue;
            }
            ++files;
            ++dirClasses[milo->root().cls];
            std::set<std::string> seen;
            std::vector<const Directory*> stack{&milo->root()};
            while (!stack.empty()) {
                const Directory* d = stack.back();
                stack.pop_back();
                for (const auto& [owner, sub] : d->subdirs) {
                    ++inlineDirs;
                    stack.push_back(sub.get());
                }
                std::vector<const Object*> objs;
                if (d->self) objs.push_back(d->self.get());
                for (const auto& o : d->objects) objs.push_back(o.get());
                for (const Object* o : objs) {
                    if (const auto* dobj = o->as<DirObj>(); dobj && !dobj->own && !dobj->objectDir.proxyFile.empty() &&
                                                           dobj->objectDir.inlineProxy != 1 && dobj->objectDir.inlineProxy != 2) {
                        std::string target = findMiloRelative(data, p, dobj->objectDir.proxyFile);
                        if (data.exists(target)) ++proxiesFound;
                        else if (missingProxies.size() < 5) missingProxies.push_back(p + ": " + o->name + " -> " + target);
                        else ++proxiesMissing;
                    }
                    std::string key = o == d->self.get() ? o->cls + " (own)" : o->cls;
                    ClassStats& s = stats[key];
                    s.typed = o->typed;
                    ++s.objects;
                    s.versions.insert(o->version);
                    if (!o->typed && p.rfind("ui/", 0) == 0) ++rawInUi[o->cls];
                    if (o->decoded) ++s.decoded;
                    else if (o->typed && s.failures.size() < 5) s.failures.push_back(p + ": " + o->name + ": " + o->error);
                    seen.insert(key);
                }
            }
            for (const auto& c : seen) ++stats[c].files;
        }

        if (const char* dump = std::getenv("GD03_SCENE_DUMP")) {
            auto milo = scene::loadMilo(data, dump);
            dumpDir(milo->root(), 0);
        }

        std::printf("%d milo files parsed, %d failed, %d inline sub-directories\n", files, fileFailures, inlineDirs);
        std::printf("proxy directories: %d resolve to an archive file, %d do not\n", proxiesFound,
                    proxiesMissing + int(missingProxies.size()));
        for (const auto& m : missingProxies) std::printf("  missing: %s\n", m.c_str());
        std::printf("top directory classes:");
        for (const auto& [c, n] : dirClasses) std::printf(" %s %d", c.c_str(), n);
        std::printf("\n\ntyped classes                objects  decoded  failed  files  versions\n");
        int typedFailures = 0, untypedObjects = 0;
        auto versions = [](const ClassStats& s) {
            std::string v;
            for (uint32_t x : s.versions) v += (v.empty() ? "" : ",") + std::to_string(x);
            return v;
        };
        for (const auto& [cls, s] : stats) {
            if (!s.typed) {
                untypedObjects += s.objects;
                continue;
            }
            int failed = s.objects - s.decoded;
            typedFailures += failed;
            std::printf("  %-26s %7d  %7d  %6d  %5d  %s\n", cls.c_str(), s.objects, s.decoded, failed, s.files, versions(s).c_str());
            for (const auto& f : s.failures) std::printf("      %s\n", f.c_str());
        }
        std::printf("\nraw (untyped) classes: %d objects\n", untypedObjects);
        std::string line = " ";
        for (const auto& [cls, s] : stats) {
            if (s.typed) continue;
            std::string item = " " + cls + " " + std::to_string(s.objects) + " (v" + versions(s) + ")";
            if (line.size() + item.size() > 100) {
                std::printf("%s\n", line.c_str());
                line = " ";
            }
            line += item;
        }
        if (line.size() > 1) std::printf("%s\n", line.c_str());
        int rawUi = 0;
        std::string rawUiList;
        for (const auto& [cls, n] : rawInUi) rawUi += n, rawUiList += " " + cls + " " + std::to_string(n);
        std::printf("raw (untyped) objects in ui/: %d%s\n", rawUi, rawUiList.c_str());
        // The text chain of every label / button / slider in ui/ (scene/ui_styles.h):
        // type -> resource_file -> template Text -> Font; localized tokens in eng.
        int chained = 0, chainFailures = 0, tokens = 0, missingTokens = 0;
        size_t localeStrings = 0;
        std::vector<std::string> chainProblems, tokenProblems;
        {
            UiStyles styles;
            styles.load(data);
            Locale locale;
            locale.load(data, "eng");
            localeStrings = locale.size();
            MiloLibrary lib(data);
            for (const auto& p : paths) {
                if (p.rfind("ui/", 0) != 0) continue;
                Milo& milo = lib.load(p);
                milo.root().forEach([&](Object& o) {
                    const auto* label = o.as<UILabel>();
                    const auto* slider = o.as<BandSlider>();
                    if (!label && !slider) return;
                    const std::string& token = label ? (label->localize ? label->textToken : std::string()) : slider->textToken;
                    if (!token.empty()) {
                        ++tokens;
                        if (!locale.find(token)) {
                            ++missingTokens;
                            if (tokenProblems.size() < 8) tokenProblems.push_back(p + ": " + o.name + " '" + token + "'");
                        }
                    }
                    const UiStyle st = styles.style(o.cls, o.header.type);
                    const std::string file = st.resourceFile.empty() ? st.classResourceFile : st.resourceFile;
                    std::string why;
                    const Text* tmpl = nullptr;
                    if (!st.node) why = "unknown type '" + o.header.type + "'";
                    else if (file.empty() || st.textTemplate.empty()) why = "no resource_file / text";
                    else {
                        const std::string path = UiStyles::resourcePath(data, file);
                        if (!data.exists(path)) why = "missing " + path;
                        else if (!(tmpl = lib.load(path).find<Text>(st.textTemplate))) why = "no " + st.textTemplate + " in " + path;
                    }
                    if (tmpl && !tmpl->font.empty()) {
                        // The font may live in a subdirectory milo of the resource (slider.milo).
                        bool found = tmpl->dir && tmpl->dir->resolve<Font>(tmpl->font);
                        if (!found)
                            for (const auto& sub : lib.load(UiStyles::resourcePath(data, file)).root().self->as<DirObj>()->objectDir.subdirs) {
                                const std::string sp = findMiloRelative(data, UiStyles::resourcePath(data, file), sub);
                                if (data.exists(sp) && lib.load(sp).find<Font>(tmpl->font)) found = true;
                            }
                        if (!found) why = tmpl->name + ": no font " + tmpl->font;
                    }
                    if (why.empty()) ++chained;
                    else {
                        ++chainFailures;
                        if (chainProblems.size() < 8) chainProblems.push_back(p + ": " + o.cls + " " + o.name + ": " + why);
                    }
                });
            }
        }
        std::printf("ui text chain: %d components resolve type -> resource -> template Text -> Font, %d do not\n", chained,
                    chainFailures);
        for (const auto& m : chainProblems) std::printf("  %s\n", m.c_str());
        std::printf("ui locale (eng, %zu strings): %d of %d text tokens missing\n", localeStrings, missingTokens, tokens);
        for (const auto& m : tokenProblems) std::printf("  %s\n", m.c_str());
        // The venue classes (M8): every object on the disc, by class
        // (docs/venues.md); a different count means a file or class went missing.
        // This disc's counts (equal to tools/milo.py scan's, an independent
        // parser; GH80s has no PostProc, Flare or CubeTex objects).
        static const std::pair<const char*, int> kVenueCounts[] = {
            {"CamShot", 326}, {"LightPreset", 120}, {"Spotlight", 209}, {"EventTrigger", 214}, {"WorldDir (own)", 18},
            {"WorldCrowd", 8}, {"PostProc", 0}, {"Flare", 0}, {"LightAnim", 4}, {"EnvAnim", 28}, {"Set", 26},
            {"CubeTex", 0}, {"Waypoint", 48}, {"CamAnim", 2}};
        int countFailures = 0;
        std::string counts;
        for (const auto& [cls, want] : kVenueCounts) {
            const auto it = stats.find(cls);
            const int got = it == stats.end() ? 0 : it->second.decoded;
            counts += std::string(counts.empty() ? "" : ", ") + cls + " " + std::to_string(got);
            if (got != want) {
                ++countFailures;
                std::printf("  venue class %s: %d decoded, expected %d\n", cls, got, want);
            }
        }
        std::printf("venue classes decoded: %s (%s)\n", counts.c_str(), countFailures ? "MISMATCH" : "as expected");
        // The character classes (M9, char/char_objects.h; their directory
        // records are --chartest's): every object on the disc, by class.
        static const std::pair<const char*, int> kCharCounts[] = {
            {"CharClipSamples", 1105}, {"CharClipGroup", 191},   {"CharClipFilter", 43},    {"CharBone", 2738},
            {"Character", 58},         {"BandCharacter", 12},    {"CharDriver", 47},        {"CharDriverMidi", 28},
            {"CharIKHand", 28},        {"CharIKMidi", 14},       {"CharHair", 33},          {"CharLookAt", 12},
            {"CharEyes", 6},           {"CharWeightSetter", 28}, {"CharUpperTwist", 64},    {"CharForeTwist", 46},
            {"CharServoBone", 45},     {"CharWalk", 14},         {"CharPosConstraint", 11}, {"CharIKRod", 6},
            {"OutfitLoader", 16},      {"FaceFxLipSyncServo", 18}};
        const int venueCountFailures = countFailures;
        counts.clear();
        for (const auto& [cls, want] : kCharCounts) {
            const auto it = stats.find(cls);
            const int got = it == stats.end() ? 0 : it->second.decoded;
            counts += std::string(counts.empty() ? "" : ", ") + cls + " " + std::to_string(got);
            if (got != want) {
                ++countFailures;
                std::printf("  character class %s: %d decoded, expected %d\n", cls, got, want);
            }
        }
        std::printf("character classes decoded: %s (%s)\n", counts.c_str(), countFailures != venueCountFailures ? "MISMATCH" : "as expected");
        bool ok = fileFailures == 0 && typedFailures == 0 && files > 0 && chainFailures == 0 && countFailures == 0;
        std::printf("\nscene test %s: %d typed objects failed, %d files failed\n", ok ? "passed" : "FAILED", typedFailures,
                    fileFailures);
        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "scene test failed: %s\n", e.what());
        return 2;
    }
}
