// Music on the copy of the game cache OSRS_CACHE_DIR names (cache_tests.cpp
// says which). Skipped when it is missing.

#include <doctest/doctest.h>

#include <osrs/cache/audio.h>

#include <filesystem>
#include <memory>

using namespace osrs::cache;

namespace {

const Store* store() {
    static const std::unique_ptr<Store> opened = []() -> std::unique_ptr<Store> {
        std::error_code missing;
        if (!std::filesystem::exists(std::filesystem::path(OSRS_CACHE_DIR) / "main_file_cache.dat2", missing)) return nullptr;
        return std::make_unique<Store>(OSRS_CACHE_DIR);
    }();
    return opened.get();
}

std::uint32_t u32(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    return std::uint32_t{bytes[at]} << 24 | std::uint32_t{bytes[at + 1]} << 16 | std::uint32_t{bytes[at + 2]} << 8 | bytes[at + 3];
}

// Whether a MIDI file's header counts the track chunks that fill the rest
// of it, each ending with the end of track event, so that a player reads
// every track.
bool wellFormed(const std::vector<std::uint8_t>& midi) {
    if (midi.size() < 14 || u32(midi, 0) != 0x4D546864 || u32(midi, 4) != 6) return false;
    const int tracks = midi[10] << 8 | midi[11];
    if ((midi[8] << 8 | midi[9]) != (tracks > 1 ? 1 : 0)) return false;
    std::size_t at = 14;
    for (int track = 0; track < tracks; ++track) {
        if (at + 8 > midi.size() || u32(midi, at) != 0x4D54726B) return false;
        const std::size_t end = at + 8 + u32(midi, at + 4);
        if (end > midi.size() || end < at + 11) return false;
        if (midi[end - 3] != 0xFF || midi[end - 2] != 0x2F || midi[end - 1] != 0) return false;
        at = end;
    }
    return at == midi.size();
}

} // namespace

TEST_CASE("every music track and jingle converts to a MIDI file whose chunks fill it") {
    if (store() == nullptr) return;
    for (const Index index : {Index::MusicTracks, Index::MusicJingles}) {
        int converted = 0;
        for (const int id : store()->groups(index)) {
            const std::optional<std::vector<std::uint8_t>> midi = loadMidi(*store(), index, id);
            REQUIRE(midi.has_value());
            INFO("index ", static_cast<int>(index), " group ", id);
            CHECK(wellFormed(*midi));
            ++converted;
        }
        CHECK(converted > 300);
    }
}
