// The regions of osrs/cache/map.h, as RuneLite's RegionLoader gives them:
// definitions/MapDefinition, definitions/LocationsDefinition and
// region/Region, for every region id the oracle tries (below 1 << 15).
//
// An encrypted cache's keys come from the file OSRS_CACHE_XTEA names, which
// the oracle's --xtea option sets: RuneLite's XteaKeyManager format,
// [{"region": <id>, "keys": [<k0>, <k1>, <k2>, <k3>]}, ...].

#include "lines.h"

#include <osrs/cache/map.h>

#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>

namespace osrs::cache::oracle {

namespace {

constexpr int kRegions = 1 << 15;

std::optional<std::string> environment(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    std::optional<std::string> found;
    if (_dupenv_s(&value, &length, name) == 0 && value != nullptr) found = value;
    std::free(value);
    return found;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::optional<std::string>(value) : std::nullopt;
#endif
}

// The integers of the JSON member `name` of `object`: one, or an array's.
std::vector<std::int64_t> numbersAfter(std::string_view object, std::string_view name) {
    std::vector<std::int64_t> numbers;
    const std::size_t key = object.find(name);
    if (key == std::string_view::npos) return numbers;
    const std::size_t value = object.find_first_not_of(" \t\r\n:", key + name.size());
    if (value == std::string_view::npos) return numbers;
    const std::size_t end = object[value] == '[' ? object.find(']', value) : object.find_first_of(",}", value);
    const std::string_view text = object.substr(value, end == std::string_view::npos ? end : end - value);
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '-' && (text[i] < '0' || text[i] > '9')) continue;
        std::size_t digits = i + 1;
        while (digits < text.size() && text[digits] >= '0' && text[digits] <= '9') ++digits;
        numbers.push_back(std::stoll(std::string(text.substr(i, digits - i))));
        i = digits;
    }
    return numbers;
}

std::map<int, XteaKey> readKeys(const std::string& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("can't open XTEA key file " + file);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::map<int, XteaKey> keys;
    for (std::size_t start = text.find('{'); start != std::string::npos; start = text.find('{', start + 1)) {
        const std::size_t end = text.find('}', start);
        if (end == std::string::npos) break;
        const std::string_view object = std::string_view(text).substr(start, end - start);
        const std::vector<std::int64_t> region = numbersAfter(object, "\"region\"");
        const std::vector<std::int64_t> key = numbersAfter(object, "\"keys\"");
        if (region.size() != 1 || key.size() != 4) continue;
        keys[static_cast<int>(region[0])] = {static_cast<std::int32_t>(key[0]), static_cast<std::int32_t>(key[1]),
                                             static_cast<std::int32_t>(key[2]), static_cast<std::int32_t>(key[3])};
    }
    return keys;
}

const XteaKeyProvider& keyProvider() {
    static const XteaKeyProvider provider = []() -> XteaKeyProvider {
        const std::optional<std::string> file = environment("OSRS_CACHE_XTEA");
        if (!file) return [](int) { return std::optional<XteaKey>(); };
        return [keys = readKeys(*file)](int region) -> std::optional<XteaKey> {
            const auto found = keys.find(region);
            return found != keys.end() ? std::optional<XteaKey>(found->second) : std::nullopt;
        };
    }();
    return provider;
}

void mapDefinition(Lines& out, const MapDefinition& map) {
    out.integer("regionX", map.regionX);
    out.integer("regionY", map.regionY);
    if (out.skipping()) return;
    for (int plane = 0; plane < 4; ++plane) {
        for (int x = 0; x < 64; ++x) {
            for (int y = 0; y < 64; ++y) {
                const MapTile& tile = map.tile(plane, x, y);
                out.compact("tiles[" + std::to_string(plane) + "][" + std::to_string(x) + "][" + std::to_string(y) + "]", [&](Lines& fields) {
                    if (tile.height) fields.integer("height", *tile.height);
                    fields.integer("attrOpcode", tile.overlayAttribute);
                    fields.integer("settings", static_cast<std::int8_t>(tile.settings));
                    fields.integer("overlayId", static_cast<std::int16_t>(tile.overlay));
                    fields.integer("overlayPath", static_cast<std::int8_t>(tile.overlayShape));
                    fields.integer("overlayRotation", static_cast<std::int8_t>(tile.overlayRotation));
                    fields.integer("underlayId", static_cast<std::int16_t>(tile.underlay));
                });
            }
        }
    }
}

// RuneLite's region/Location, at a position of the region's own or, in a
// Region, of the world.
void location(Lines& out, const std::string& path, const Location& location, int baseX, int baseY) {
    out.integer(field(path, "id"), location.object);
    out.integer(field(path, "type"), location.shape);
    out.integer(field(path, "orientation"), location.rotation);
    out.integer(field(path, "position.x"), baseX + location.x);
    out.integer(field(path, "position.y"), baseY + location.y);
    out.integer(field(path, "position.z"), location.plane);
}

void locationsDefinition(Lines& out, const LocationsDefinition& locations) {
    out.integer("regionX", locations.regionX);
    out.integer("regionY", locations.regionY);
    if (out.skipping()) return;
    for (std::size_t i = 0; i < locations.locations.size(); ++i) location(out, at("locations", i), locations.locations[i], 0, 0);
}

void region(Lines& out, const Region& region) {
    const int baseX = ((region.id >> 8) & 0xFF) << 6;
    const int baseY = (region.id & 0xFF) << 6;
    out.integer("regionID", region.id);
    out.integer("baseX", baseX);
    out.integer("baseY", baseY);
    if (out.skipping()) return;
    for (int plane = 0; plane < 4; ++plane) {
        for (int x = 0; x < 64; ++x) {
            const std::string column = "[" + std::to_string(plane) + "][" + std::to_string(x) + "]";
            std::vector<std::int32_t> heights;
            std::vector<std::int8_t> settings, overlayPaths, overlayRotations;
            std::vector<std::int16_t> overlays, underlays;
            for (int y = 0; y < 64; ++y) {
                const TerrainTile& tile = region.tile(plane, x, y);
                heights.push_back(tile.height);
                settings.push_back(static_cast<std::int8_t>(tile.settings));
                overlays.push_back(static_cast<std::int16_t>(tile.overlay));
                overlayPaths.push_back(static_cast<std::int8_t>(tile.overlayShape));
                overlayRotations.push_back(static_cast<std::int8_t>(tile.overlayRotation));
                underlays.push_back(static_cast<std::int16_t>(tile.underlay));
            }
            out.integers("tileHeights" + column, heights);
            out.integers("tileSettings" + column, settings);
            out.integers("overlayIds" + column, overlays);
            out.integers("overlayPaths" + column, overlayPaths);
            out.integers("overlayRotations" + column, overlayRotations);
            out.integers("underlayIds" + column, underlays);
        }
    }
    for (std::size_t i = 0; i < region.locations.size(); ++i) location(out, at("locations", i), region.locations[i], baseX, baseY);
}

// Each region RuneLite has an object for: none where the loader gives
// nothing, error() where it throws.
template <class Load, class Write>
void regions(Lines& out, Load load, Write write) {
    for (int id = 0; id < kRegions; ++id) {
        try {
            const auto loaded = load(id);
            if (!loaded) continue;
            out.object({id});
            write(out, *loaded);
        } catch (const std::exception&) {
            out.object({id});
            out.error();
        }
    }
}

void maps(const Store& store, Lines& out) {
    regions(out, [&](int id) { return loadMapDefinition(store, id); }, mapDefinition);
}

void locations(const Store& store, Lines& out) {
    regions(out, [&](int id) { return loadLocationsDefinition(store, id, keyProvider()); }, locationsDefinition);
}

void regionsOf(const Store& store, Lines& out) {
    // As the oracle loads a region: the terrain decides whether there is one.
    regions(out, [&](int id) -> std::optional<Region> {
        const std::optional<MapDefinition> terrain = loadMapDefinition(store, id);
        if (!terrain) return std::nullopt;
        const std::optional<LocationsDefinition> placed = loadLocationsDefinition(store, id, keyProvider());
        return loadRegion(id, *terrain, placed ? &*placed : nullptr);
    }, region);
}

const Type mapType("map", maps);
const Type locationsType("locations", locations);
const Type regionType("region", regionsOf);

} // namespace

} // namespace osrs::cache::oracle
