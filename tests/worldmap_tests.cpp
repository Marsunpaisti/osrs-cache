// The world map: composite maps built byte by byte, for what the pinned
// cache doesn't exercise, and the intermap links on the copy of the game
// cache OSRS_CACHE_DIR names (cache_tests.cpp says which), which RuneLite's
// WorldMapManager parsed to the same links.

#include <doctest/doctest.h>

#include <osrs/cache/worldmap.h>

#include <filesystem>
#include <memory>

using namespace osrs::cache;

namespace {

struct Bytes {
    std::vector<std::uint8_t> data;

    Bytes& u8(int value) {
        data.push_back(static_cast<std::uint8_t>(value));
        return *this;
    }
    Bytes& u16(int value) { return u8(value >> 8).u8(value); }
    Bytes& i32(std::uint32_t value) { return u16(static_cast<int>(value >> 16)).u16(static_cast<int>(value & 0xFFFF)); }
};

std::uint32_t packed(int x, int y, int plane) { return static_cast<std::uint32_t>(plane << 28 | x << 14 | y); }

// Two squares shown at square (10, 10), the first from square (50, 50) and
// the second from (60, 60); two zones over zone (1, 2) of it, the first
// from zone (3, 4) of square (51, 52) and the second from square (70, 70);
// and elements in that zone, elsewhere in the square, and outside both. The
// format before the world map index's revision 426 adds each square's and
// zone's group and file.
std::vector<std::uint8_t> composite(bool withGroups) {
    Bytes b;
    b.u16(2);
    b.u8(0).u8(0).u8(4).u16(50).u16(50).u16(10).u16(10);
    if (withGroups) b.u16(32767).u16(5); // 32767 stands for -1
    b.u8(0).u8(1).u8(4).u16(60).u16(60).u16(10).u16(10);
    if (withGroups) b.u16(6).u16(6);
    b.u16(2);
    b.u8(1).u8(2).u8(1).u16(51).u16(52).u8(3).u8(4).u16(10).u16(10).u8(1).u8(2);
    if (withGroups) b.i32(0x80000000u | 40000).u16(7);
    b.u8(1).u8(3).u8(1).u16(70).u16(70).u8(0).u8(0).u16(10).u16(10).u8(1).u8(2);
    if (withGroups) b.u16(8).u16(8);
    b.u16(3);
    b.u16(14).i32(packed(10 * 64 + 1 * 8 + 5, 10 * 64 + 2 * 8 + 6, 0)).u8(1);
    b.u16(15).i32(packed(10 * 64 + 40, 10 * 64 + 40, 0)).u8(0);
    b.u16(16).i32(packed(100, 100, 1)).u8(0);
    return b.data;
}

const Store* store() {
    static const std::unique_ptr<Store> opened = []() -> std::unique_ptr<Store> {
        std::error_code missing;
        if (!std::filesystem::exists(std::filesystem::path(OSRS_CACHE_DIR) / "main_file_cache.dat2", missing)) return nullptr;
        return std::make_unique<Store>(OSRS_CACHE_DIR);
    }();
    return opened.get();
}

} // namespace

TEST_CASE("a composite map keeps the first square or zone shown at a place, and places an element by the zone over the square") {
    const std::vector<std::uint8_t> bytes = composite(false);
    const WorldMapCompositeDefinition map = decodeWorldMapComposite(bytes, 426);

    REQUIRE(map.mapSquares.size() == 1);
    CHECK(map.mapSquares[0].sourceSquareX == 50);
    CHECK(map.mapSquares[0].groupId == 0);
    REQUIRE(map.zones.size() == 1);
    CHECK(map.zones[0].sourceSquareX == 51);
    REQUIRE(map.elements.size() == 3);

    const WorldMapElementDefinition& inZone = map.elements[0];
    CHECK(inZone.areaDefinitionId == 14);
    CHECK(inZone.membersOnly);
    CHECK(inZone.offset == WorldPoint{(51 - 10) * 64 + (3 - 1) * 8, (52 - 10) * 64 + (4 - 2) * 8, 2});
    CHECK(inZone.worldPosition() == WorldPoint{653 + 2640, 662 + 2704, 2});

    CHECK(map.elements[1].offset == WorldPoint{40 * 64, 40 * 64, 0});

    // RuneLite puts an element nothing is shown under on plane 0.
    CHECK_FALSE(map.elements[2].offset.has_value());
    CHECK(map.elements[2].worldPosition() == WorldPoint{100, 100, 0});
}

TEST_CASE("composite maps give their squares' groups and files only before the world map index's revision 426") {
    const std::vector<std::uint8_t> bytes = composite(true);
    const WorldMapCompositeDefinition map = decodeWorldMapComposite(bytes, 425);

    REQUIRE(map.mapSquares.size() == 1);
    CHECK(map.mapSquares[0].groupId == -1);
    CHECK(map.mapSquares[0].fileId == 5);
    REQUIRE(map.zones.size() == 1);
    CHECK(map.zones[0].groupId == 40000);
    CHECK(map.zones[0].fileId == 7);
    REQUIRE(map.elements.size() == 3);
    CHECK(map.elements[2].areaDefinitionId == 16);
}

TEST_CASE("the world map's intermap links come from client script 1705") {
    if (store() == nullptr) return;
    const std::map<WorldPoint, WorldPoint> links = loadIntermapLinks(*store());
    CHECK(links.size() == 406);
    // A dungeon under the surface, linked to the surface above it.
    CHECK(links.at(WorldPoint{2544, 10144, 0}) == WorldPoint{2543, 3741, 0});
    CHECK(links.at(WorldPoint{1191, 2787, 0}) == WorldPoint{1191, 9185, 0});
    CHECK(links.at(WorldPoint{1191, 9185, 0}) == WorldPoint{1191, 2787, 0});
}
