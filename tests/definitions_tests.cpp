// The definition decoders' revision formats and the fields RuneLite derives
// after decoding, on bytes built here: the pinned cache holds only the
// current formats. RuneLiteOracle.java checks every field of every
// definition the cache holds.

#include <doctest/doctest.h>

#include <osrs/cache/definitions.h>

#include <cstdint>
#include <string_view>
#include <vector>

using namespace osrs::cache;

namespace {

// A definition's bytes, big-endian as the cache stores them.
struct Bytes {
    std::vector<std::uint8_t> data;
    Bytes& u8(int value) {
        data.push_back(static_cast<std::uint8_t>(value));
        return *this;
    }
    Bytes& u16(int value) { return u8(value >> 8).u8(value); }
    Bytes& u24(int value) { return u8(value >> 16).u16(value); }
    Bytes& i32(std::int32_t value) { return u16(value >> 16).u16(value); }
    Bytes& string(std::string_view text) {
        for (const char c : text) u8(c);
        return u8(0);
    }
};

constexpr std::int32_t kCurrent = 2'000'000'000;

} // namespace

TEST_CASE("NPC head icons: one sprite before revision 1493, a sprite group and frame per bit from it") {
    const NpcDefinition old = decodeNpcDefinition(1, Bytes().u8(102).u16(5).u8(0).data, 1492);
    CHECK(old.headIconArchiveIds == std::vector<std::int32_t>{-1});
    CHECK(old.headIconSpriteIndex == std::vector<std::int16_t>{5});

    // Bits 0 and 2: group 16 frame 2, then a group of 32767 (none) frame 0.
    const Bytes current = Bytes().u8(102).u8(0b101).u16(16).u8(3).u16(32767).u8(1).u8(0);
    const NpcDefinition npc = decodeNpcDefinition(1, current.data, 1493);
    CHECK(npc.headIconArchiveIds == std::vector<std::int32_t>{16, -1, -1});
    CHECK(npc.headIconSpriteIndex == std::vector<std::int16_t>{2, -1, 0});
}

TEST_CASE("an NPC option named Hidden keeps the one before it, and a footprint follows the NPC's size") {
    const NpcDefinition npc = decodeNpcDefinition(1, Bytes().u8(30).string("Talk-to").u8(30).string("hidden").u8(12).u8(3).u8(0).data, kCurrent);
    CHECK(npc.options[0] == "Talk-to");
    REQUIRE(npc.ops.ops.size() == 1);
    CHECK(npc.ops.ops[0] == "Talk-to");
    // 0.4 of 3 tiles of 128 units, truncated, unless the cache sets it.
    CHECK(npc.footprintSize == 153);
    CHECK(decodeNpcDefinition(1, Bytes().u8(12).u8(3).u8(126).u16(200).u8(0).data, kCurrent).footprintSize == 200);
}

TEST_CASE("an object's ambient sound has a retain byte from revision 1673") {
    const ObjectDefinition old = decodeObjectDefinition(1, Bytes().u8(78).u16(100).u8(5).u8(0).data, 1672);
    CHECK(old.ambientSoundId == 100);
    CHECK(old.ambientSoundDistance == 5);
    CHECK(old.ambientSoundRetain == 0);

    const ObjectDefinition object = decodeObjectDefinition(1, Bytes().u8(78).u16(100).u8(5).u8(7).u8(0).data, 1673);
    CHECK(object.ambientSoundDistance == 5);
    CHECK(object.ambientSoundRetain == 7);
}

TEST_CASE("an object's wallOrDoor and supportsItems, unless the cache sets them, as RuneLite derives them") {
    const auto decode = [](const Bytes& bytes) { return decodeObjectDefinition(1, bytes.data, kCurrent); };
    // Models making up shape 10, as opcode 5's do, or any option, make 1.
    CHECK(decode(Bytes().u8(5).u8(1).u16(9).u8(0)).wallOrDoor == 1);
    CHECK(decode(Bytes().u8(30).string("Open").u8(0)).wallOrDoor == 1);
    CHECK(decode(Bytes().u8(1).u8(1).u16(9).u8(0).u8(0)).wallOrDoor == 0);
    CHECK(decode(Bytes().u8(5).u8(1).u16(9).u8(19).u8(0).u8(0)).wallOrDoor == 0);

    CHECK(decode(Bytes().u8(0)).supportsItems == 1);
    CHECK(decode(Bytes().u8(17).u8(0)).supportsItems == 0);
    CHECK(decode(Bytes().u8(17).u8(75).u8(1).u8(0)).supportsItems == 1);
}

TEST_CASE("sequence opcodes and frame sounds by revision: packed sounds up to 1141, the revision 226 opcodes after 1268") {
    // Before 226: 13 holds sounds by index, 14 the skeletal animation and 16
    // its frame range.
    const Bytes old = Bytes().u8(13).u8(1).u16(258).u8(1).u8(2).u8(0).u8(14).i32(66051).u8(16).u16(5).u16(10).u8(0);
    const SequenceDefinition before226 = decodeSequenceDefinition(1, old.data, 1268);
    REQUIRE(before226.frameSounds.size() == 1);
    CHECK(before226.frameSounds[0].frame == 0);
    REQUIRE(before226.frameSounds[0].sound.has_value());
    CHECK(before226.frameSounds[0].sound->id == 258);
    CHECK(before226.frameSounds[0].sound->location == 2);
    CHECK_FALSE(before226.frameSounds[0].sound->weight.has_value());
    CHECK(before226.animMayaId == 66051);
    CHECK(before226.animMayaStart == 5);
    CHECK(before226.animMayaEnd == 10);
    CHECK(before226.verticalOffset == 0);

    // Up to 220: id, loops and location in 3 bytes; a sound with id 0 is
    // dropped, keeping its place.
    const Bytes packed = Bytes().u8(15).u16(2).u16(7).u24(300 << 8 | 2 << 4 | 3).u16(8).u24(1 << 4).u8(0);
    const SequenceDefinition before220 = decodeSequenceDefinition(1, packed.data, 1141);
    REQUIRE(before220.frameSounds.size() == 2);
    CHECK(before220.frameSounds[0].frame == 7);
    REQUIRE(before220.frameSounds[0].sound.has_value());
    CHECK(before220.frameSounds[0].sound->id == 300);
    CHECK(before220.frameSounds[0].sound->loops == 2);
    CHECK(before220.frameSounds[0].sound->location == 3);
    CHECK(before220.frameSounds[1].frame == 8);
    CHECK_FALSE(before220.frameSounds[1].sound.has_value());

    // From 226: 13 is the skeletal animation, 14 the sounds with a weight,
    // 16 the vertical offset.
    const Bytes current = Bytes().u8(13).i32(66051).u8(14).u16(1).u16(3).u16(258).u8(9).u8(1).u8(2).u8(0).u8(16).u8(0xFD).u8(0);
    const SequenceDefinition sequence = decodeSequenceDefinition(1, current.data, 1269);
    CHECK(sequence.animMayaId == 66051);
    REQUIRE(sequence.frameSounds.size() == 1);
    REQUIRE(sequence.frameSounds[0].sound.has_value());
    CHECK(sequence.frameSounds[0].sound->weight == 9);
    CHECK(sequence.verticalOffset == -3);
}
