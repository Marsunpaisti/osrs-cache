#pragma once

// Skeletal ("maya") animations and the skeletons they move, which RuneLite's
// cache library doesn't decode. A skeleton (index 1, a frame map's file) is a
// tree of bones with rest poses; an animation (index 22) is a set of curves
// giving each bone's rotation, translation and scale on every client cycle. A
// sequence names its animation by SequenceDefinition::animMayaId, and plays
// cycles animMayaStart to animMayaEnd of it.
//
// Ported from Encounter Replay's src/cache/skeletal (skeleton.ts, curve.ts)
// and the skeletal part of its rig.ts, which port rs-map-viewer's
// src/rs/model/skeletal and Model.ts (BSD-2-Clause, LICENSE-rs-map-viewer.txt
// in this library). Its arithmetic is kept: doubles, stored as floats where
// the port stores Float32Arrays. Departs from it in dead code only: a curve
// is sampled inside its points' range alone, so its extrapolation modes are
// left out.

#include <osrs/cache/store.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace osrs::cache {

// A 4x4 matrix in column-major order, as gl-matrix lays it out.
using Matrix4 = std::array<float, 16>;

// One value over client cycles, sampled on each cycle from the first point's
// to the last's.
class Curve {
public:
    // Outside the sampled cycles a curve gives 0, as the client's does; an
    // empty curve gives nothing on cycle 0 and 0 elsewhere.
    std::optional<float> value(int cycle) const;

    int start = 0;
    std::vector<float> values;
};

struct Skeleton {
    // The classic transform groups the frame map also carries: each group's
    // type (0 origin, 1 translate, 2 rotate, 3 scale, 5 alpha; the cache's 6
    // reads as 2) and the labels it moves.
    std::vector<std::uint8_t> types;
    std::vector<std::vector<std::uint8_t>> labels;

    struct Bone {
        // -1 for a root.
        std::int16_t parent = -1;
        // Per rest pose: the bone's matrix relative to its parent, the inverse
        // of its matrix relative to the model, and the rotation, translation
        // and scale an animation falls back to for a curve it lacks.
        std::vector<Matrix4> local;
        std::vector<Matrix4> inverseModel;
        std::vector<std::array<float, 3>> rotation;
        std::vector<std::array<float, 3>> translation;
        std::vector<std::array<float, 3>> scale;
    };
    std::vector<Bone> bones;
    int poseCount = 0;
};

struct SkeletalAnimation {
    // The skeleton (index 1 group) it moves, and the rest pose it starts from.
    int skeleton = 0;
    int pose = 0;
    // Per bone, its curves: rotation x, y and z, translation x, y and z,
    // scale x, y and z.
    std::vector<std::array<std::optional<Curve>, 9>> boneCurves;
    // Per classic transform group, the curves that aren't a bone's. The
    // client fades the faces of an alpha group (Skeleton::types 5) by its
    // first curve's value times 255 on top of their alpha, clamped to 0 to
    // 255, when the animation has alpha curves.
    std::vector<std::vector<std::optional<Curve>>> groupCurves;
    bool hasAlpha = false;
};

// Nothing if the cache has no such skeleton or animation. Throw
// std::runtime_error if one doesn't decode, unless the store skips such
// objects (store.h). An animation samples each of its
// curves on every cycle it covers when it loads: about 8 ms in a Release
// build, so keep the ones in use.
std::optional<Skeleton> loadSkeleton(const Store& store, int id);
std::optional<SkeletalAnimation> loadSkeletalAnimation(const Store& store, std::int32_t animMayaId);

// Each bone's matrix on a cycle of the animation, taking a vertex from the
// skeleton's rest pose to its animated place, in the client's skeletal axes
// (skin's y and z flipped).
std::vector<Matrix4> boneTransforms(const Skeleton& skeleton, const SkeletalAnimation& animation, int cycle);

// Moves each vertex by the sum of its bones' transforms weighted by their
// weights over 255, as the client poses a skeletal model. A vertex's bones and
// weights are the model's animaya groups and scales; a vertex with none stays.
// x, y and z are model units, y down.
void skin(std::span<std::int32_t> x, std::span<std::int32_t> y, std::span<std::int32_t> z,
          std::span<const std::vector<std::uint8_t>> bones, std::span<const std::vector<std::uint8_t>> weights,
          std::span<const Matrix4> transforms);

} // namespace osrs::cache
