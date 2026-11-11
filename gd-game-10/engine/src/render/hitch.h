// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// The frame-time budget of the render thread (docs/guidelines/usual-bugs.md,
// "no stutter"): loads upload and compile a slice a frame while the frame has
// time left; GD10_HITCHLOG=<ms> logs every frame over <ms> with its screen
// and the scopes that took its time, and the longest at exit.
namespace hitch {

// The frame's work starts (App::run, the render thread).
void frameBegin();
// The frame's work ended: logged when over the hitch log's budget.
void frameEnd(const char* screen);
// Milliseconds since frameBegin.
double frameMs();
// Whether a load may do one more piece of its uploads and compiles in this
// frame: while the frame has spent less than the slice budget (8 ms), and
// always one piece a frame (of all the loads: they go on in turn).
bool slice();
// While one is open, slice() is always true: a load done at once in a loop
// (VenueStage::attach, a song started without its preload) would otherwise
// make no progress once the frame's slice is taken (it spun for ever).
class AllAtOnce {
public:
    AllAtOnce();
    ~AllAtOnce();
    AllAtOnce(const AllAtOnce&) = delete;
    AllAtOnce& operator=(const AllAtOnce&) = delete;
};
// The hitch log's summary (frames over the budget, the longest, by screen).
void report();
// The last frame's work without its present (a "present" scope: the swap,
// which waits for a GPU other programs share): the no-stutter check's measure.
double lastWorkMs();

// A named piece of the frame's work, logged with a hitch (render thread only).
class Scope {
public:
    explicit Scope(const char* name);
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    const char* name_;
    unsigned long long t0_;
};

}  // namespace hitch
