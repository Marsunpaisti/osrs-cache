#pragma once

// Classic animation frames (index 0) and the frame maps they move (index 1),
// decoded as RuneLite's cache library decodes them (net.runelite:cache
// 1.13.5, definitions/loaders FrameLoader and FramemapLoader). A sequence
// names its frames by SequenceDefinition frame ids, the frame's group in the
// high 16 bits and its file in the low 16. A frame moves the transform
// groups of the frame map it names.

#include <osrs/cache/store.h>

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace osrs::cache {

// A frame map's transform groups (RuneLite's FramemapDefinition): each
// group's type (0 sets the origin, 1 translates, 2 rotates, 3 scales, 5
// changes face alpha) and the labels it moves. The types are as the cache
// holds them, as RuneLite reads them: the client reads a 6 as 2, as
// loadSkeleton in osrs/cache/skeletal.h does, which also reads the bones a
// skeletal animation moves.
struct Framemap {
    std::vector<std::uint8_t> types;
    std::vector<std::vector<std::uint8_t>> labels;
};

// File 0 of the index 1 group, as RuneLite's frame maps are. Nothing if the
// cache has no such group or file. Throws std::runtime_error if it doesn't
// decode, unless the store skips such objects (store.h).
std::optional<Framemap> loadFramemap(const Store& store, int id);

// What a frame does to one transform group of its frame map. A group of type
// 0 sets the origin, 1 translates, 2 rotates, 3 scales (128 for 1), 5 changes
// face alpha. An axis the frame leaves out is 0, or 128 for a scale.
struct FrameTransform {
    std::uint8_t group = 0;
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int16_t z = 0;
};

struct Frame {
    // The frame map (RuneLite's framemap).
    int skeleton = 0;
    // Whether a transform changes alpha (RuneLite's FrameDefinition.showing).
    bool showing = false;
    // In the order the client applies them (RuneLite's indexFrameIds and
    // translator_x, _y and _z). Before a transform that isn't an origin, the
    // nearest earlier origin group the frame skips is added, at 0.
    std::vector<FrameTransform> transforms;
};

// The frames of one group by frame id; none if the cache lacks the group. A
// frame whose frame map is missing or that doesn't decode is left out, as
// RuneLite's FrameLoader fails on it, whatever the store says to do with
// what doesn't decode; a frame or frame map that doesn't decode is reported
// to its onSkipped (store.h). RuneLite also fails on a frame of more than
// 500 transforms, which its scratch arrays hold; it is kept here.
std::map<std::int32_t, Frame> loadFrames(const Store& store, int group);

} // namespace osrs::cache
