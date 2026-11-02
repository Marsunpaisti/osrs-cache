// The storage paths the pinned cache doesn't take: XTEA-encrypted groups, the
// flat format, and a maps index that names its groups. The fixtures are
// RuneLite's own output (Container.compress with a key, FlatStorage.save),
// so they need no game cache.

#include <doctest/doctest.h>

#include <osrs/cache/definitions.h>
#include <osrs/cache/frames.h>
#include <osrs/cache/map.h>
#include <osrs/cache/store.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace osrs::cache;

namespace {

constexpr XteaKey kKey = {0x01234567, static_cast<std::int32_t>(0x89ABCDEF), static_cast<std::int32_t>(0xFEDCBA98), 0x76543210};

// A flat cache of one index, the maps: "m50_50" is region 12850's terrain and
// "l50_50" its objects, encrypted with kKey.
constexpr const char* kFlatMaps = R"(protocol=7
revision=1234
compression=2
crc=-5
named=true
id=0
namehash=-1123920270
revision=3
crc=1769766828
contents=AgAAAFAAAIAHH4sIAAAAAAAAAO3OQQ0AIAwEsNuLgED8S0HGltAqaDLAzspNne4HAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAL97/OVWDgeAAAA=
compression=0
file=0=0
id=1
namehash=-1152549421
revision=3
crc=-2105894136
contents=AgAAACWADcmOO3YvQKGsYLRa+4HINXfZskOtiXEjdrjpezPOcl1jvFp64VwmAA==
compression=0
file=0=0
)";

class TemporaryDirectory {
public:
    TemporaryDirectory() : path_(std::filesystem::temp_directory_path() / ("osrs_cache_tests_" + std::to_string(std::random_device{}()))) {
        std::filesystem::create_directories(path_);
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace

TEST_CASE("an encrypted container decrypts each whole 8-byte block, and leaves the bytes after them as they are") {
    // "Hello, XTEA!!" stored uncompressed with revision 7.
    const std::vector<std::uint8_t> stored = {0x00, 0x00, 0x00, 0x00, 0x0d, 0xe1, 0x98, 0x38, 0xe4, 0xcd,
                                              0x0c, 0x8b, 0xe8, 0x54, 0x45, 0x41, 0x21, 0x21, 0x00, 0x07};
    const Container decrypted = decompressContainer(stored, kKey);
    CHECK(std::string(decrypted.data.begin(), decrypted.data.end()) == "Hello, XTEA!!");
    CHECK(decrypted.revision == 7);
    // The checksum is the stored bytes', as the reference table records it.
    CHECK(decrypted.crc == static_cast<std::uint32_t>(-615800941));

    const Container raw = decompressContainer(stored);
    CHECK(std::string(raw.data.begin() + 8, raw.data.end()) == "TEA!!");
    CHECK(raw.crc == decrypted.crc);
}

TEST_CASE("a flat cache whose maps index names its groups, with the objects encrypted") {
    const TemporaryDirectory directory;
    std::ofstream(directory.path() / "5.flatcache", std::ios::binary) << kFlatMaps;
    const Store store(directory.path());
    CHECK(store.indices() == std::vector<Index>{Index::Maps});
    const IndexInfo* maps = store.index(Index::Maps);
    REQUIRE(maps != nullptr);
    CHECK((maps->protocol == 7 && maps->revision == 1234 && maps->named && !maps->sized));
    CHECK((maps->crc == static_cast<std::uint32_t>(-5) && maps->compression == 2));

    const int region = 50 << 8 | 50;
    const std::optional<MapDefinition> terrain = loadMapDefinition(store, region);
    REQUIRE(terrain.has_value());
    const MapTile& tile = terrain->tile(0, 1, 2);
    CHECK(tile.height == 10);
    CHECK((tile.overlay == 7 && tile.overlayShape == 1 && tile.overlayRotation == 3 && tile.overlayAttribute == 9));
    CHECK(tile.underlay == 5);
    CHECK_FALSE(terrain->tile(0, 0, 0).height.has_value());
    CHECK_FALSE(loadMapDefinition(store, region + 1).has_value());

    // Encrypted objects need the region's key.
    CHECK_FALSE(loadLocationsDefinition(store, region).has_value());
    const std::optional<LocationsDefinition> placed =
        loadLocationsDefinition(store, region, [&](int id) { return id == region ? std::optional(kKey) : std::nullopt; });
    REQUIRE(placed.has_value());
    REQUIRE(placed->locations.size() == 3);
    const auto is = [](const Location& l, int object, int shape, int rotation, int plane, int x, int y) {
        return l.object == object && l.shape == shape && l.rotation == rotation && l.plane == plane && l.x == x && l.y == y;
    };
    CHECK(is(placed->locations[0], 1234, 10, 2, 1, 5, 6));
    CHECK(is(placed->locations[1], 1234, 0, 1, 1, 13, 20));
    // An id delta above 32767 comes as several smarts.
    CHECK(is(placed->locations[2], 40000, 22, 3, 0, 0, 0));

    const XteaKey wrong = {1, 2, 3, 4};
    CHECK_THROWS_AS(loadLocationsDefinition(store, region, [&](int) { return std::optional(wrong); }), std::runtime_error);
}

TEST_CASE("an object that doesn't decode fails the whole load by default, and only itself when the store skips such objects") {
    // A configs index whose underlays group holds underlay 0, colour
    // 0x123456, and underlay 1, whose first opcode (2) no underlay has, as
    // after a game update adds one.
    const TemporaryDirectory directory;
    std::ofstream(directory.path() / "2.flatcache", std::ios::binary) << "protocol=7\nrevision=0\ncompression=0\ncrc=0\nnamed=false\n"
                                                                         "id=1\nrevision=0\ncrc=2072097342\n"
                                                                         "contents=AAAAABABEjRWAAIAAAAABf////0B\ncompression=0\n"
                                                                         "file=0=0\nfile=1=0\n";

    const Store strict(directory.path());
    CHECK_THROWS_WITH_AS(loadUnderlayDefinitions(strict), doctest::Contains("underlay 1"), std::runtime_error);

    std::vector<std::pair<std::string, std::int64_t>> skipped;
    const Store lenient(directory.path(), StoreOptions{Undecodable::Skip, [&](const SkippedObject& object) {
                                                           skipped.emplace_back(object.type, object.id);
                                                       }});
    const std::vector<UnderlayDefinition> underlays = loadUnderlayDefinitions(lenient);
    REQUIRE(underlays.size() == 1);
    CHECK((underlays[0].id == 0 && underlays[0].rgb == 0x123456));
    CHECK(skipped == std::vector<std::pair<std::string, std::int64_t>>{{"underlay", 1}});

    // A group whose checksum doesn't match can't be read at all, and skipping
    // what doesn't decode mustn't hide that.
    const TemporaryDirectory damaged;
    std::ofstream(damaged.path() / "2.flatcache", std::ios::binary) << "protocol=7\nrevision=0\ncompression=0\ncrc=0\nnamed=false\n"
                                                                       "id=1\nrevision=0\ncrc=1\n"
                                                                       "contents=AAAAABABEjRWAAIAAAAABf////0B\ncompression=0\n"
                                                                       "file=0=0\nfile=1=0\n";
    CHECK_THROWS_AS(loadUnderlayDefinitions(Store(damaged.path(), StoreOptions{Undecodable::Skip})), std::runtime_error);
}

TEST_CASE("a loader of one object returns nothing for it when the store skips what doesn't decode") {
    // A skeletons index whose frame map 0 counts 5 transform groups and ends
    // there.
    const TemporaryDirectory directory;
    std::ofstream(directory.path() / "1.flatcache", std::ios::binary) << "protocol=7\nrevision=0\ncompression=0\ncrc=0\nnamed=false\n"
                                                                         "id=0\nrevision=0\ncrc=-659331987\n"
                                                                         "contents=AAAAAAEF\ncompression=0\n"
                                                                         "file=0=0\n";

    CHECK_THROWS_WITH_AS(loadFramemap(Store(directory.path()), 0), doctest::Contains("frame map 0"), std::runtime_error);

    std::vector<std::pair<std::string, std::int64_t>> skipped;
    const Store lenient(directory.path(), StoreOptions{Undecodable::Skip, [&](const SkippedObject& object) {
                                                           skipped.emplace_back(object.type, object.id);
                                                       }});
    CHECK_FALSE(loadFramemap(lenient, 0).has_value());
    CHECK(skipped == std::vector<std::pair<std::string, std::int64_t>>{{"frame map", 0}});
}
