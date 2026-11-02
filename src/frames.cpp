#include <osrs/cache/frames.h>

#include "buffer.h"
#include "undecodable.h"

#include <stdexcept>
#include <string>

namespace osrs::cache {

namespace {

// RuneLite FrameLoader.load: a flag byte per transform group, then the
// groups' values as short smarts.
Frame decodeFrame(std::span<const std::uint8_t> bytes, int skeletonId, const std::vector<std::uint8_t>& types) {
    Buffer in(bytes);
    in.u16(); // the skeleton
    const int length = in.u8();
    if (bytes.size() < 3 + static_cast<std::size_t>(length)) throw std::runtime_error("frame shorter than its flags");
    Buffer values(bytes.subspan(3 + length));

    Frame frame;
    frame.skeleton = skeletonId;
    int lastGroup = -1;
    for (int i = 0; i < length; ++i) {
        const int flags = in.u8();
        if (flags == 0) continue;
        if (i >= static_cast<int>(types.size())) throw std::runtime_error("frame moves a group its frame map lacks");
        if (types[i] != 0) {
            for (int k = i - 1; k > lastGroup; --k) {
                if (types[k] == 0) {
                    frame.transforms.push_back(FrameTransform{static_cast<std::uint8_t>(k)});
                    break;
                }
            }
        }
        const int fallback = types[i] == 3 ? 128 : 0;
        FrameTransform& transform = frame.transforms.emplace_back();
        transform.group = static_cast<std::uint8_t>(i);
        transform.x = static_cast<std::int16_t>((flags & 1) != 0 ? values.shortSmart() : fallback);
        transform.y = static_cast<std::int16_t>((flags & 2) != 0 ? values.shortSmart() : fallback);
        transform.z = static_cast<std::int16_t>((flags & 4) != 0 ? values.shortSmart() : fallback);
        lastGroup = i;
        if (types[i] == 5) frame.showing = true;
    }
    if (values.remaining() != 0) throw std::runtime_error("frame has bytes after its values");
    return frame;
}

// RuneLite FramemapLoader.load.
Framemap decodeFramemap(std::span<const std::uint8_t> bytes) {
    Buffer in(bytes);
    Framemap framemap;
    const int count = in.u8();
    framemap.types.resize(count);
    for (std::uint8_t& type : framemap.types) type = static_cast<std::uint8_t>(in.u8());
    framemap.labels.resize(count);
    for (auto& labels : framemap.labels) labels.resize(in.u8());
    for (auto& labels : framemap.labels) {
        for (std::uint8_t& label : labels) label = static_cast<std::uint8_t>(in.u8());
    }
    return framemap;
}

} // namespace

std::optional<Framemap> loadFramemap(const Store& store, int id) {
    const auto files = store.files(Index::Skeletons, id);
    if (!files || !files->contains(0)) return std::nullopt;
    return decodeOrSkip(store, "frame map", id, [&] { return decodeFramemap(files->at(0)); });
}

std::map<std::int32_t, Frame> loadFrames(const Store& store, int group) {
    std::map<std::int32_t, Frame> frames;
    const auto files = store.files(Index::Frames, group);
    if (!files) return frames;
    std::map<int, std::optional<Framemap>> framemaps;
    for (const auto& [file, bytes] : *files) {
        if (bytes.size() < 2) continue;
        const int skeletonId = bytes[0] << 8 | bytes[1];
        auto known = framemaps.find(skeletonId);
        if (known == framemaps.end()) {
            std::optional<Framemap> framemap;
            const auto skeleton = store.files(Index::Skeletons, skeletonId);
            if (skeleton && skeleton->contains(0)) {
                try {
                    framemap = decodeFramemap(skeleton->at(0));
                } catch (const std::runtime_error& error) {
                    reportSkipped(store, "frame map", skeletonId, error);
                }
            }
            known = framemaps.emplace(skeletonId, std::move(framemap)).first;
        }
        if (!known->second) continue;
        try {
            frames.emplace(group << 16 | file, decodeFrame(bytes, skeletonId, known->second->types));
        } catch (const std::runtime_error& error) {
            reportSkipped(store, "frame", group << 16 | file, error);
        }
    }
    return frames;
}

} // namespace osrs::cache
