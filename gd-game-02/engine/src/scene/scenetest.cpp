// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "scene/scenetest.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <map>
#include <set>
#include <vector>

#include "data/game_data.h"
#include "scene/milo.h"

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
        std::printf("%smat '%s' %zu verts %zu faces %zu groups%s parent '%s' showing %d pos %.1f %.1f %.1f\n", pad.c_str(),
                    m->mat.c_str(), m->verts.size(), m->faces.size(), m->groupSizes.size(), m->bones.empty() ? "" : " skinned",
                    m->trans.parent.c_str(), m->draw.showing, m->trans.local.pos[0], m->trans.local.pos[1], m->trans.local.pos[2]);
    } else if (auto* g = o.as<Group>()) {
        std::printf("%sobjects [%s] showing %d\n", pad.c_str(), join(g->objects, 12).c_str(), g->draw.showing);
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
            if (p.size() > 10 && p.compare(p.size() - 10, 10, ".milo_xbox") == 0) paths.push_back(p);
        std::sort(paths.begin(), paths.end());

        std::map<std::string, ClassStats> stats;
        std::map<std::string, int> dirClasses;
        int files = 0, fileFailures = 0, inlineDirs = 0, proxiesFound = 0, proxiesMissing = 0;
        std::vector<std::string> missingProxies;
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
                    if (o->decoded) ++s.decoded;
                    else if (o->typed && s.failures.size() < 5) s.failures.push_back(p + ": " + o->name + ": " + o->error);
                    seen.insert(key);
                }
            }
            for (const auto& c : seen) ++stats[c].files;
        }

        if (const char* dump = std::getenv("GD02_SCENE_DUMP")) {
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
        bool ok = fileFailures == 0 && typedFailures == 0 && files > 0;
        std::printf("\nscene test %s: %d typed objects failed, %d files failed\n", ok ? "passed" : "FAILED", typedFailures,
                    fileFailures);
        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "scene test failed: %s\n", e.what());
        return 2;
    }
}
