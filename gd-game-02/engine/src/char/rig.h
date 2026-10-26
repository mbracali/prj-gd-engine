// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "char/char_objects.h"
#include "char/skeleton.h"

// A character's rig at run time (docs/band.md "Pose"): the CharPollables of
// its directory other than the drivers and the servo, ported from the Xbox
// 360 code [C addresses in rig.cpp]: CharWeightSetter, CharIKHand,
// CharIKMidi, CharUpperTwist, CharForeTwist, CharHair, CharLookAt,
// CharEyes, CharPosConstraint, CharIKRod, and the weights of every
// CharWeightable (drivers, IK, look-ats, setters; weight_owner followed).
// Character::Poll runs the dir's pollables in GH2's dependency order
// (0x82156B78 / 0x821563C8): the drivers and the servo are the
// character's, called through the hooks at their place in that order.
namespace chr {

struct RigContext {
    double beat = 0;          // song beat (TaskMgr)
    float dBeats = 0;         // this poll
    float dSeconds = 0;
    double seconds = 0;
    // Not used by GH2's eyes (CharEyes picks points ahead of the head,
    // 0x82170130); kept for callers.
    bool hasCamera = false;
    std::array<float, 3> camera{};
    // The eased weight of `driver`'s clips with any of `flags`
    // (ClipDriver::flagWeight; CharWeightSetter).
    std::function<float(const std::string& driver, uint32_t flags)> driverFlagWeight;
    // Hooks run at their place in the poll order.
    // One driver by name at its own place in the order (main.drv before the
    // weight setters, the hand drivers after them); without it pollDrivers
    // runs every driver at the first driver's place.
    std::function<void(const std::string& driver)> pollDriver;
    std::function<void()> pollDrivers;  // main.drv and the hand drivers, into the servo's bones
    std::function<void()> pollServo;    // CharServoBone: the bones posed, root motion
};

class Rig {
public:
    Rig(const scene::Directory& dir, Skeleton& skeleton);
    ~Rig();

    // Every pollable in GH2's order, with the hooks.
    void poll(const RigContext& ctx);

    // Weights (CharWeightable): `name`'s weight (its owner's when it has one).
    float weightOf(const std::string& name) const;
    void setWeight(const std::string& name, float w);

    // CharIKMidi (fret.ik): {midi_parser <spot> <start> <end>} / new_spot.
    bool ikMidi(const std::string& ik, const std::string& spot, double start, double end, double now);
    // Hair back to rest (teleports: reset_hair).
    void resetHair();
    // A component switched on / off by name (e.g. the guitar IK while walking).
    void setEnabled(const std::string& name, bool on);
    // The sorted poll order, by object name (tests, logs).
    std::vector<std::string> order() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace chr
