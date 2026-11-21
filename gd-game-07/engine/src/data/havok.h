// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Guitar Hero III's Havok 4.6.0-r1 packfiles, .hkc (zone collision) and
// .rag (band ragdolls), decoded through the hkClass reflection their
// __types__ section carries (docs/formats.md "Havok packfiles";
// tools/havok.py is the reference decoder, checked over the whole disc).
// Every object of __data__ is reached from the contents object
// (hkPhysicsData) and every pointer fixup is read by a reflected member, so
// a file that parses is fully understood at the reflection level; the
// summary below keeps what a port could use.
namespace havok {

struct Shape {
    enum Kind { None, Capsule, Box, MoppMesh, Other } kind = None;
    float radius = 0;                     // convex radius (capsule radius)
    std::array<float, 3> a{}, b{};        // capsule end points; box half extents in `a`
    std::array<float, 3> translation{};   // hkConvexTranslateShape around the box / capsule
    size_t triangles = 0, vertices = 0;   // MOPP mesh (hkNxStorageExtendedMeshShape subparts)
    std::string className;
};

struct Body {
    std::string name;                     // the skeleton bone it follows ("Bone_ACC_01", ...)
    float mass = 0;                       // 1 / inertiaAndMassInv.w (0: fixed)
    std::string motion;                   // MOTION_BOX_INERTIA, MOTION_FIXED, ...
    std::array<float, 16> transform{};    // motionState.transform (column-major, w = 1)
    std::array<float, 3> invInertia{};    // inertiaAndMassInv.xyz (the body frame's diagonal)
    std::array<float, 3> comLocal{};      // sweptTransform.centerOfMassLocal
    float maxLinearVelocity = 0, maxAngularVelocity = 0;
    float friction = 0, restitution = 0, linearDamping = 0, angularDamping = 0;
    Shape shape;
};

struct Constraint {
    std::string name, type;               // type: hkRagdollConstraintData, hkLimitedHingeConstraintData, ...
    int bodyA = -1, bodyB = -1;           // indices into Physics::bodies (child, parent)
    std::array<float, 3> pivotA{}, pivotB{};  // the local transforms' translations
    float coneMax = 0, twistMin = 0, twistMax = 0;  // ragdoll limits (radians); hinge: min / max in twist*
    std::array<float, 16> transformA{}, transformB{};  // atoms.transforms (column-major: column k = axis k)
    // The other atoms. hkRagdollConstraintData: twistLimit (twist*), coneLimit
    // (cone*), planesLimit (planes*), angFriction; hkLimitedHingeConstraintData:
    // angLimit (twist*), 2dAng, angFriction; hkPrismaticConstraintData:
    // linLimit (lin*), lin0 / lin1, friction (maxFriction = maxFrictionForce).
    bool twistOn = false, coneOn = false, planesOn = false;
    float twistTau = 0, coneTau = 0, planesMin = 0, planesMax = 0, planesTau = 0;
    int twistAxis = 0, twistRefAxis = 1, coneAxisA = 0, coneAxisB = 0, planesAxisA = 0, planesAxisB = 1;
    bool coneAligned = true, planesAligned = false;  // angleMeasurementMode ZERO_WHEN_VECTORS_ALIGNED
    float maxFriction = 0;                // 0: the friction atom off
    int frictionAxes = 0;
    int hingeAxis = 0;                    // 2dAng freeRotationAxis
    float linMin = 0, linMax = 0;
    int linAxis = 0, linLock0 = 1, linLock1 = 2;
};

struct Physics {
    std::string contentsClass;            // "hkPhysicsData"
    size_t objects = 0, fixups = 0, classes = 0;
    std::vector<std::string> objectClasses;  // each __data__ object's class, in file order
    std::vector<Body> bodies;
    std::vector<Constraint> constraints;
};

// Throws std::runtime_error on anything outside what the disc holds.
Physics parse(const uint8_t* data, size_t size);

}  // namespace havok
