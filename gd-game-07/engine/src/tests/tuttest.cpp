// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/tuttest.h"

#include <cstdio>
#include <exception>
#include <string>

#include "data/disc.h"
#include "data/qbkey.h"
#include "game/tutorial.h"
#include "script/globals.h"

// --tuttest: drive World Tour's Tutorial / Training mode on the real disc data,
// headless. Proves (data-derived, no eyeballing):
//   (a) the Tutorial-select menu is the disc's own four categories and nine
//       training scripts, grouped exactly as menu_tutorial_select.qb groups
//       them; every track's lesson count equals the `lesson` markers in its
//       real training_<item>_tutorial_script array;
//   (b) the training interpreter advances on the clock the way
//       training_script_update does: a `time` step fires at its absolute ms;
//   (c) a wait-for-the-player lesson BLOCKS until the scripted/autoplayed input
//       satisfies its gate, then advances — with the exact thresholds read from
//       the gate bytecode (notes_hit>=8 for lessons 3-6, >=4 for lesson 7):
//       one input short does not advance, the threshold-th input does;
//   (d) every one of the nine tracks runs to completion when each gate is fed,
//       and g_training_lessons_completed reaches the track's lesson count.
namespace {

std::string nm(uint32_t k) {
    const std::string* n = qbkey::names().find(k);
    return n ? *n : ("0x" + std::to_string(k));
}

// Run a session to completion, feeding each gate its scripted input. Returns
// the simulated frames spent; sets `ok` false if it never finishes.
int driveToEnd(const tutorial::Tutorial& t, bool& ok) {
    tutorial::Session s(t);
    int frames = 0;
    for (; frames < 2000000; ++frames) {
        s.tick(16);  // ~one gameframe
        if (s.done()) break;
        if (s.atGate()) s.gateInput();  // the autoplayed / scripted player action
    }
    ok = s.done() && s.lessonsCompleted() == int(t.lessons.size());
    return frames;
}

}  // namespace

int runTutTest(const std::string& discPath) {
    std::string error;
    auto disc = Disc::open(discPath, error);
    if (!disc) {
        std::printf("tuttest: %s\n", error.c_str());
        return 2;
    }
    disc->loadNames();
    Globals g;
    try {
        g.load(*disc);
    } catch (const std::exception& e) {
        std::printf("tuttest: globals: %s\n", e.what());
        return 2;
    }

    int failures = 0;
    auto check = [&](bool cond, const std::string& what) {
        if (!cond) {
            std::printf("tuttest: FAIL %s\n", what.c_str());
            ++failures;
        }
        return cond;
    };

    // (a) The menu is the disc's own four categories / nine scripts.
    tutorial::Menu menu = tutorial::load(g);
    check(menu.tutorials.size() == 9,
          "expected 9 training scripts, got " + std::to_string(menu.tutorials.size()));

    int totalLessons = 0, totalGates = 0;
    for (const tutorial::Tutorial& t : menu.tutorials) {
        // Lesson count must equal the `lesson` markers in the raw step array.
        int markers = 0;
        for (const tutorial::Step& s : t.steps)
            if (s.lesson != 0) ++markers;
        int gates = 0;
        for (const tutorial::Step& s : t.steps)
            if (s.gate) ++gates;
        check(!t.steps.empty(), std::string("empty script ") + nm(t.scriptName));
        check(int(t.lessons.size()) == markers,
              std::string("lesson count != markers for ") + nm(t.scriptName) + " (" +
                  std::to_string(t.lessons.size()) + " vs " + std::to_string(markers) + ")");
        totalLessons += int(t.lessons.size());
        totalGates += gates;
        std::printf("tuttest: %-7s %-22s %3zu steps, %zu lessons, %d gates\n",
                    tutorial::categoryName(t.category), nm(t.item).c_str(), t.steps.size(),
                    t.lessons.size(), gates);
    }
    std::printf("tuttest: %zu tracks, %d lessons, %d player gates total\n",
                menu.tutorials.size(), totalLessons, totalGates);
    check(totalLessons > 0, "no lessons parsed");
    check(totalGates > 0, "no player gates parsed");

    // The exact category grouping (tutorial_lessons order).
    auto countIn = [&](tutorial::Category c) {
        int n = 0;
        for (const auto& t : menu.tutorials)
            if (t.category == c) ++n;
        return n;
    };
    check(countIn(tutorial::Category::Guitar) == 5, "guitar should have 5 tracks");
    check(countIn(tutorial::Category::Drums) == 2, "drums should have 2 tracks");
    check(countIn(tutorial::Category::Vocals) == 1, "vocals should have 1 track");
    check(countIn(tutorial::Category::Band) == 1, "band should have 1 track");

    // Find guitar basic for the detailed interpreter checks.
    const tutorial::Tutorial* basic = nullptr;
    for (const auto& t : menu.tutorials)
        if (t.item == qbkey::key("basic") && t.category == tutorial::Category::Guitar) basic = &t;
    if (!check(basic != nullptr, "guitar basic tutorial not found")) {
        std::printf("tuttest: FAIL (cannot run interpreter checks)\n");
        return 1;
    }

    // (b) Clock model: the `time=1000` title step does not fire before 1000 ms.
    {
        tutorial::Session s(*basic);
        s.tick(1);  // fires step 0 (startup); step 1 is time=1000
        const int afterStartup = s.currentStepIndex();
        check(afterStartup == 1, "startup step did not fire on the first frame");
        for (int i = 0; i < 9; ++i) s.tick(100);  // clock = 901 ms, still < 1000
        check(s.currentStepIndex() == 1, "a time=1000 step fired before 1000 ms");
        s.tick(200);  // clock = 1101 ms, now past 1000
        check(s.currentStepIndex() > 1, "a time=1000 step did not fire after 1000 ms");
    }

    // (c) A gate BLOCKS until satisfied, with the real threshold. Lesson 3's
    // gate is notes_hit>=8: seven hits do not advance, the eighth does.
    {
        tutorial::Session s(*basic);
        // Advance to the first gate, feeding nothing.
        int guard = 0;
        while (!s.atGate() && !s.done() && guard++ < 200000) s.tick(16);
        check(s.atGate(), "never reached a gate in guitar basic");
        const int atStep = s.currentStepIndex();
        // No input: it must not advance no matter how long we wait.
        for (int i = 0; i < 5000; ++i) s.tick(16);
        check(s.currentStepIndex() == atStep, "gate advanced with no player input");
        check(s.atGate(), "gate released itself with no player input");
    }
    {
        // The first gate in guitar basic is the orientation prompt (threshold 1);
        // find the notes_hit>=8 gate (training_1_3_wait_for_hit_notes) and prove
        // 7 vs 8. Drive, feeding every earlier gate, until that call is current.
        const uint32_t hit8 = qbkey::key("training_1_3_wait_for_hit_notes");
        tutorial::Session s(*basic);
        int guard = 0;
        bool reached = false;
        while (!s.done() && guard++ < 500000) {
            s.tick(16);
            if (s.atGate()) {
                // Is the current gate the notes_hit>=8 one?
                const tutorial::Step& cur = basic->steps[s.currentStepIndex() - 1];
                if (cur.call == hit8) {
                    reached = true;
                    const int atStep = s.currentStepIndex();
                    for (int i = 0; i < 7; ++i) s.gateInput();  // one short of 8
                    for (int i = 0; i < 2000; ++i) s.tick(16);
                    check(s.currentStepIndex() == atStep && s.atGate(),
                          "notes_hit gate released before 8 hits");
                    s.gateInput();  // the 8th hit
                    check(s.gateActions() == 8, "gate action count wrong");
                    s.tick(16);
                    check(s.currentStepIndex() > atStep, "notes_hit gate did not release at 8 hits");
                    break;
                }
                s.gateInput();  // satisfy an earlier (threshold-1) gate and move on
            }
        }
        check(reached, "never reached the notes_hit>=8 gate");
    }

    // (d) Every track runs to completion when each gate is fed; the completed
    // lesson count reaches the track's lesson count.
    int ranToEnd = 0;
    for (const tutorial::Tutorial& t : menu.tutorials) {
        bool ok = false;
        const int frames = driveToEnd(t, ok);
        if (check(ok, std::string("track ") + nm(t.item) + " did not complete all lessons"))
            ++ranToEnd;
        else
            std::printf("tuttest:   (%s stalled after %d frames)\n", nm(t.item).c_str(), frames);
    }
    std::printf("tuttest: %d/%zu tracks ran to completion with every gate satisfied\n",
                ranToEnd, menu.tutorials.size());

    std::printf("tuttest: %s (%d checks failed)\n",
                failures ? "FAIL" : "all tutorial checks passed", failures);
    return failures ? 1 : 0;
}
