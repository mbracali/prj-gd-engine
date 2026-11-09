// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/facefxtest.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "char/facefx.h"
#include "data/dta.h"
#include "data/game_data.h"

namespace {

#include "tests/facefxtest_golden.inc"

using namespace facefx;

bool endsWith(const std::string& s, const char* suffix) {
    const size_t n = std::char_traits<char>::length(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// A deterministic stand-in for the interpreter's generator.
struct Lcg {
    uint32_t s = 12345;
    uint32_t next() { return s = s * 1103515245u + 12345u; }
    int randomInt(int lo, int hi) { return hi <= lo ? lo : lo + int((next() >> 8) % uint32_t(hi - lo)); }
    float randomFloat(float lo, float hi) { return lo + (hi - lo) * float(next() >> 8) * (1.0f / 16777216.0f); }
};

}  // namespace

int runFaceFxTest(const std::string& dataPath) {
    int failures = 0;
    auto fail = [&](const std::string& what) {
        if (failures++ < 30) std::printf("  FAIL %s\n", what.c_str());
    };
    auto check = [&](bool ok, const std::string& what) {
        if (!ok) fail(what);
    };
    auto near = [](float a, float b) { return std::fabs(a - b) <= 2e-5f + 2e-5f * std::fabs(b); };
    try {
        GameData data(dataPath);

        // ---- 1. every archive parses to its end
        std::vector<std::string> vocs, facs;
        for (const std::string& p : data.paths()) {
            if (endsWith(p, ".voc")) vocs.push_back(p);
            if (endsWith(p, ".fac")) facs.push_back(p);
        }
        std::sort(vocs.begin(), vocs.end());
        std::sort(facs.begin(), facs.end());
        std::map<std::string, Anim> anims;
        std::map<uint32_t, int> sdk;
        size_t keys = 0, songKeys = 0, songVocs = 0;
        int with25 = 0, hermite = 0, blendOk = 0;
        for (const std::string& p : vocs) {
            try {
                Anim a = loadAnim(data, p);
                sdk[a.header.sdk]++;
                size_t k = 0;
                for (const Curve& c : a.curves) {
                    k += c.keys.size();
                    hermite += c.interp == 0;
                }
                keys += k;
                if (p.rfind("songs/", 0) == 0) songKeys += k, ++songVocs;
                with25 += a.curves.size() == 25;
                blendOk += a.blendInTime == 0.16f && a.blendOutTime == 0.22f;
                check(a.endTime > a.startTime, p + ": anim bounds");
                anims[p] = std::move(a);
            } catch (const std::exception& e) {
                fail(p + ": " + e.what());
            }
        }
        std::printf("facefxtest: %zu .voc parsed to their end (%zu under songs/), %zu keys (%zu under songs/), SDK 1500 x%d / 1200 x%d\n",
                    anims.size(), songVocs, keys, songKeys, sdk[1500], sdk[1200]);
        check(vocs.size() == 70 && anims.size() == 70, "70 .voc files (69 songs/ + xboxlive rodgort)");
        check(songVocs == 69, "69 .voc under songs/");
        check(keys == 201109 && songKeys == 199000, "key totals 201109 / 199000");
        check(sdk[1500] == 69 && sdk[1200] == 1, "SDK versions");
        check(with25 == 70 && hermite == 70 * 25 && blendOk == 70, "25 Hermite curves and 0.16 / 0.22 in every .voc");
        const Anim* blink = anims.count(kBlinkTrackPath) ? &anims[kBlinkTrackPath] : nullptr;
        if (blink) {
            const int b = blink->find("Blink");
            check(b >= 0 && blink->curves[size_t(b)].keys.size() == 906 && blink->version == 0, "_blinktrack: Blink 906 keys, FxAnim v0");
        } else {
            fail("no _blinktrack.voc");
        }

        std::map<std::string, FaceGraph> graphs;
        for (const std::string& p : facs) {
            try {
                graphs[p] = loadFaceGraph(data, p);
            } catch (const std::exception& e) {
                fail(p + ": " + e.what());
            }
        }
        auto graphStats = [&](const std::string& p, size_t& nodes, size_t& bones, size_t& links, size_t& poses) {
            nodes = bones = links = poses = 0;
            auto it = graphs.find(p);
            if (it == graphs.end()) return;
            for (const Node& n : it->second.nodes) {
                ++nodes;
                bones += n.bones.size();
                links += n.links.size();
                poses += n.isBonePose();
            }
        };
        size_t gn, gb, gl, gp, sn, sb, sl, sp;
        graphStats("char/guitarist.fac", gn, gb, gl, gp);
        graphStats("char/metal_singer/ng/metal_singer.fac", sn, sb, sl, sp);
        std::printf("facefxtest: %zu .fac parsed: guitarist %zu nodes (%zu pose, %zu bones, %zu links), metal_singer %zu nodes (%zu pose, %zu bones, %zu links)\n",
                    graphs.size(), gn, gp, gb, gl, sn, sp, sb, sl);
        check(facs.size() == 2 && graphs.size() == 2, "2 .fac files");
        check(gn == 25 && gp == 11 && gb == 187 && gl == 12, "guitarist.fac counts");
        check(sn == 16 && sp == 16 && sb == 16 && sl == 0, "metal_singer.fac counts");
        if (graphs.size() != 2 || !blink) throw std::runtime_error("archives missing");
        const FaceGraph& guitarist = graphs["char/guitarist.fac"];
        const FaceGraph& singer = graphs["char/metal_singer/ng/metal_singer.fac"];
        check(guitarist.header.sdk == 1500 && singer.header.sdk == 1200, ".fac SDK versions");

        // ---- 2. dta lists and paths
        const std::vector<std::string> expressions = loadServoVisemes(data, "gh2");
        const std::vector<std::string> visemes = loadServoVisemes(data, "singer");
        std::printf("facefxtest: visemes lists: gh2 %zu (%s..%s), singer %zu (%s..%s)\n", expressions.size(),
                    expressions.empty() ? "" : expressions.front().c_str(), expressions.empty() ? "" : expressions.back().c_str(),
                    visemes.size(), visemes.empty() ? "" : visemes.front().c_str(), visemes.empty() ? "" : visemes.back().c_str());
        check(expressions.size() == 11 && expressions.front() == "Neutral" && expressions[9] == "EyesClosed" && expressions[10] == "OwFace",
              "gh2 visemes = EXPRESSIONS_LIST");
        check(visemes.size() == 16 && visemes.front() == "Neutral" && visemes[1] == "Eat" && visemes.back() == "Cage",
              "singer visemes = VISEMES_LIST");
        check(resolveFacePath(data, "char/alterna1/ng/gen/alterna1.milo_xbox", "../../../guitarist.fac") == "char/guitarist.fac",
              "guitarist .fac path");
        check(resolveFacePath(data, "char/female_singer/ng/gen/female_singer.milo_xbox", "../../../metal_singer/og/metal_singer.fac") ==
                  "char/metal_singer/ng/metal_singer.fac",
              "female_singer .fac path (og -> ng)");
        check(resolveFacePath(data, "char/metal_singer/ng/gen/metal_singer.milo_xbox", "../metal_singer.fac") ==
                  "char/metal_singer/ng/metal_singer.fac",
              "metal_singer .fac path");
        // The singer's .voc for every song of songs.dta: only the 6 instrumentals have none.
        {
            DataNode songs = parseDtb(data.read("config/gen/songs.dtb"));
            resolveConditionals(songs, {"HX_XBOX", "_SHIP"});
            int found = 0, total = 0;
            std::set<std::string> missing;
            for (const DataNode& s : songs.children) {
                if (!s.isList() || s.children.empty() || !s.children[0].isText()) continue;
                const DataNode* song = s.find("song");
                const DataNode* midi = song ? song->find("midi_file") : nullptr;
                if (!midi || midi->children.size() < 2) continue;
                ++total;
                if (data.exists(singerVocPath(midi->textAt(1)))) ++found;
                else if (s.children[0].s.rfind("tutorial", 0) != 0) missing.insert(s.children[0].s);
            }
            std::string m;
            for (const auto& s : missing) m += (m.empty() ? "" : " ") + s;
            std::printf("facefxtest: songs.dta: %d / %d songs have their singer .voc; none (tutorials aside): %s\n", found, total, m.c_str());
            check(missing == std::set<std::string>{"gemini", "jessica", "jordan", "misirlou", "soybomb", "yyz"}, "songs without .voc");
            check(singerVocPath("songs/badreputation/badreputation.mid") == "songs/badreputation/badreputation.voc" &&
                      unknownSongVocPath("trogdor") == "songs/trogdor/trogdor.voc",
                  "voc path formats");
        }

        // ---- 3. graph values vs the reference (facefx.py graph_eval)
        int rows = 0, values = 0, bad = 0, nonzero = 0;
        for (const GoldenCase& gc : kGolden) {
            auto g = graphs.find(gc.fac);
            auto a = anims.find(gc.voc);
            if (g == graphs.end() || a == anims.end()) {
                fail(std::string("golden case files ") + gc.fac + " " + gc.voc);
                continue;
            }
            LipSync ls;
            ls.setup(g->second, g->first == "char/guitarist.fac" ? expressions : visemes, {});
            ls.play(std::make_shared<Anim>(a->second));
            if (ls.nodeCount() != gc.nodes) fail(std::string(gc.voc) + ": node count");
            for (int r = 0; r < gc.rows; ++r) {
                const GoldenRow& row = gc.row[r];
                std::vector<float> v = ls.evaluate(row.t);
                ++rows;
                for (int i = 0; i < gc.nodes && i < int(v.size()); ++i) {
                    ++values;
                    nonzero += row.w[i] != 0.0f;
                    if (!near(v[size_t(i)], row.w[i])) {
                        ++bad;
                        char b[256];
                        std::snprintf(b, sizeof b, "%s t=%.4f %s: %.7f, reference %.7f", gc.voc, double(row.t), ls.nodeName(i).c_str(),
                                      double(v[size_t(i)]), double(row.w[i]));
                        fail(b);
                    }
                }
            }
        }
        std::printf("facefxtest: graph evaluation: %d rows, %d node values (%d non-zero), %d differ from the reference\n", rows, values, nonzero,
                    bad);
        check(rows == 96 && bad == 0, "graph values match facefx.py");

        // ---- 4. servo runtime (GH2 semantics, char/facefx.h)
        {
            // Guitarist: _blinktrack, eye entries as on the disc (eye-L/R.mesh z / x).
            LipSync ls;
            const std::vector<std::string> entries{"L-eyeZ", "R-eyeZ", "L-eyeX", "R-eyeX"};
            ls.setup(guitarist, expressions, entries);
            FacePose pose;
            float eyes[4] = {0, 0, 0, 0};
            check(!ls.poll(4.0f, 4.0f, eyes, 4, pose), "no pose before set_song");
            ls.play(std::make_shared<Anim>(*blink));
            const float tb = kGolden[0].row[2].t;  // inside the first blink
            check(ls.poll(tb, 100.0f, eyes, 4, pose), "poll poses while playing");
            const float wb = kGolden[0].row[2].w[9];
            check(pose.frames.size() == 1 && pose.frames[0].frame == 9 && near(pose.frames[0].weight, wb) && near(pose.residual, 1.0f - wb),
                  "blink -> EyesClosed (frame 9) and the residual");
            // Eyes rotated about Z: EyeZCombiner = 0.5 (L + R) feeds EyesClosed.
            float rz[9] = {std::cos(0.2f), std::sin(0.2f), 0, -std::sin(0.2f), std::cos(0.2f), 0, 0, 0, 1};  // +0.2 rad about z (rows = axes)
            float rx[9] = {1, 0, 0, 0, std::cos(0.3f), std::sin(0.3f), 0, -std::sin(0.3f), std::cos(0.3f)};
            float ry[9] = {std::cos(0.1f), 0, -std::sin(0.1f), 0, 1, 0, std::sin(0.1f), 0, std::cos(0.1f)};
            check(near(eyeAngle(rz, 2), 0.2f) && near(eyeAngle(rx, 0), 0.3f) && near(eyeAngle(ry, 1), 0.1f) && eyeAngle(rz, 5) == 0.0f,
                  "eye angle axes");
            float eyesDown[4] = {eyeAngle(rz, 2), eyeAngle(rz, 2), 0, 0};
            ls.poll(tb, 100.0f, eyesDown, 4, pose);
            check(pose.frames.size() == 1 && near(pose.frames[0].weight, wb + 0.2f), "eye Z angle adds to EyesClosed");
            // Expression cross-fade on song seconds.
            const float t0 = 30.0f;
            ls.doExpression("expressionGood3", 0.25f, t0);
            ls.poll(t0 + 0.125f, 0.0f, eyes, 4, pose);
            auto weightOf = [&](int frame) {
                for (const auto& f : pose.frames)
                    if (f.frame == frame) return f.weight;
                return 0.0f;
            };
            // The residual is 1 - the sum of |weights|; frames other than the blink's (9).
            auto residualOk = [&]() {
                float sum = 0;
                for (const auto& f : pose.frames) sum += std::fabs(f.weight);
                return near(pose.residual, 1.0f - sum);
            };
            auto framesBut9 = [&]() {
                int n = 0;
                for (const auto& f : pose.frames) n += f.frame != 9;
                return n;
            };
            check(near(weightOf(3), 0.5f), "Good3 half way");
            ls.doExpression("expressionGood3", 0.25f, t0 + 0.1f);  // same expression: no change
            ls.doExpression("expressionBad1", 0.25f, t0 + 0.125f);
            ls.poll(t0 + 0.1875f, 0.0f, eyes, 4, pose);
            check(near(weightOf(6), 0.25f) && near(weightOf(3), 0.375f), "Bad1 fades in over Good3 (its weight at the switch)");
            ls.poll(t0 + 1.0f, 0.0f, eyes, 4, pose);
            check(near(weightOf(6), 1.0f) && weightOf(3) == 0.0f && framesBut9() == 1 && residualOk(),
                  "Bad1 at full weight, Good3 gone");
            ls.setRegister("OwFace", 0.5f);
            ls.poll(t0 + 1.0f, 0.0f, eyes, 4, pose);
            check(near(weightOf(10), 0.5f) && framesBut9() == 2 && residualOk(), "OwFace register (frame 10), residual 1 - sum");
            // Past the anim's end: the frame that stops still poses (no curves), then nothing.
            check(ls.poll(blink->endTime + 1.0f, 0.0f, eyes, 4, pose) && !ls.playing(), "player stops after the end");
            check(!ls.poll(blink->endTime + 2.0f, 0.0f, eyes, 4, pose), "no pose once stopped");
            // Before the song (negative song seconds): UI clock since the first tick.
            ls.play(std::make_shared<Anim>(*blink));
            check(ls.poll(-3.0f, 50.0f, eyes, 4, pose) && ls.poll(-2.9f, 50.0f + tb, eyes, 4, pose), "negative song time plays on the UI clock");
            check(pose.frames.size() >= 1 && weightOf(9) > 0.0f, "UI-clock anim time reaches the first blink");

            // Singer: badreputation weights -> visemes frames 1..15.
            LipSync sg;
            sg.setup(singer, visemes, {});
            sg.play(std::make_shared<Anim>(anims["songs/badreputation/badreputation.voc"]));
            const GoldenCase& gc = kGolden[1];
            int matched = 0;
            for (int r = 0; r < gc.rows; ++r) {
                if (!sg.poll(gc.row[r].t, 0.0f, nullptr, 0, pose)) break;
                float sum = 0;
                bool ok = true;
                for (int i = 0; i < 16; ++i) {
                    float w = weightOf(i);
                    ok = ok && near(w, gc.row[r].w[i]);
                    sum += gc.row[r].w[i];
                }
                ok = ok && near(pose.residual, 1.0f - sum);
                matched += ok;
            }
            check(matched >= gc.rows - 1 && !sg.playing(), "singer pose weights; stopped past the end");
            std::printf("facefxtest: servo runtime: blink %.3f, eyes +0.2, cross-fade 0.5 / 0.25+0.375, stop after %.2f s; singer %d / %d rows\n",
                        double(wb), double(blink->endTime), matched, gc.rows);
        }
        {
            // The gh2 expression script.
            LipSync ls;
            ls.setup(guitarist, expressions, {});
            ls.play(std::make_shared<Anim>(*blink));
            Lcg rng;
            ExpressionScript es;
            es.randomInt = [&](int lo, int hi) { return rng.randomInt(lo, hi); };
            es.randomFloat = [&](float lo, float hi) { return rng.randomFloat(lo, hi); };
            es.pickExpression(10.0f);
            int picks = 0, repeats = 0, firstPickOne = 0;
            std::string last;
            double prevRun = -1;
            bool spacingOk = true;
            for (int step = 0; step <= 6000; ++step) {  // 100 s at 60 fps
                const float now = 10.0f + float(step) / 60.0f;
                const int excitement = step < 3000 ? 3 : 1;
                const double due = es.nextRun();
                const bool runs = es.taskRunning() && due <= now;
                es.update(now, true, excitement, ls);
                if (!runs) continue;
                if (prevRun >= 0 && (due - prevRun < 1.5 - 1e-6 || due - prevRun > 2.5 + 1e-6)) spacingOk = false;
                prevRun = due;
                const std::string& cur = ls.currentExpression();
                if (picks == 0 && cur == "expressionGood1") ++firstPickOne;
                if (cur == last) ++repeats;
                if ((excitement >= 2) != (cur.rfind("expressionGood", 0) == 0)) spacingOk = false;
                last = cur;
                ++picks;
            }
            check(picks >= 40 && picks <= 67 && repeats == 0 && firstPickOne == 0 && spacingOk,
                  "pick_expression every 1.5-2.5 s, no repeats, good / bad by excitement");
            es.overrideExpression("OwFace", 200.0f, ls);
            es.update(300.0f, true, 3, ls);
            check(ls.currentExpression() == "OwFace" && es.overridden(), "override_expression holds OwFace");
            es.resumeRandomExpression(301.0f, true, 3, ls);
            check(!es.overridden() && ls.currentExpression().rfind("expressionGood", 0) == 0, "resume_random_expression picks at once");
            std::printf("facefxtest: expression script: %d picks in 100 s, %d repeats\n", picks, repeats);
        }

        std::printf("facefxtest: %s\n", failures ? "FAIL" : "OK");
        return failures ? 1 : 0;
    } catch (const std::exception& e) {
        std::printf("facefxtest: FAIL: %s\n", e.what());
        return 1;
    }
}
