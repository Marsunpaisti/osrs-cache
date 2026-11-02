#include <osrs/cache/collision.h>

#include <osrs/cache/map.h>

#include <optional>
#include <set>
#include <stdexcept>
#include <string>

namespace osrs::cache {

namespace {

constexpr std::uint32_t kBlockObject = 0x100;
constexpr std::uint32_t kBlockProjectileObject = 0x20000;
constexpr std::uint32_t kBlockFloorDecoration = 0x40000;
constexpr std::uint32_t kBlockFloor = 0x200000;
constexpr int kProjectileShift = 9;

// Rotations turn west, north, east, south. The tile beyond the wall carries
// the opposite bit; these are wall bits, not directions one may walk.
constexpr std::array<std::uint32_t, 4> kWallBits = {0x80, 0x02, 0x08, 0x20};
constexpr std::array<int, 4> kWallDx = {-1, 0, 1, 0};
constexpr std::array<int, 4> kWallDy = {0, 1, 0, -1};
constexpr std::array<std::uint32_t, 4> kCornerBits = {0x01, 0x04, 0x10, 0x40};
constexpr std::array<int, 4> kCornerDx = {-1, 1, 1, -1};
constexpr std::array<int, 4> kCornerDy = {1, 1, -1, -1};

// Bridge bit 2 on plane 1 lowers the floors and objects of that tile by a
// plane.
int collisionPlane(const Region& region, int x, int y, int plane) {
    return plane - ((region.tile(1, x, y).settings & 2) != 0 ? 1 : 0);
}

// Nothing if the region is missing or doesn't decode.
std::optional<Region> regionOrNothing(const Store& store, int regionId) {
    try {
        return loadRegion(store, regionId);
    } catch (const std::runtime_error&) {
        return std::nullopt;
    }
}

class Builder {
public:
    explicit Builder(std::map<int, RegionFlags>& out) : out_(out) {}

    void addFloors(int regionId, const Region& region) {
        for (int plane = 0; plane < 4; ++plane) {
            for (int x = 0; x < 64; ++x) {
                for (int y = 0; y < 64; ++y) {
                    if ((region.tile(plane, x, y).settings & 1) != 0) {
                        add(baseX(regionId) + x, baseY(regionId) + y, collisionPlane(region, x, y, plane), kBlockFloor);
                    }
                }
            }
        }
    }

    void addObject(int regionId, const Region& region, const Location& location, const ObjectDefinition& object) {
        if (location.shape > 22) {
            throw std::runtime_error("object " + std::to_string(object.id) + " in region " + std::to_string(regionId)
                                     + " has unknown shape " + std::to_string(location.shape));
        }
        const int plane = collisionPlane(region, location.x, location.y, location.plane);
        if (plane < 0 || object.hollow || object.interactType == 0) return;
        const int x = baseX(regionId) + location.x;
        const int y = baseY(regionId) + location.y;
        const int rotation = location.rotation;
        if (location.shape == 22) {
            if (object.interactType == 1) add(x, y, plane, kBlockFloorDecoration);
        } else if (location.shape == 1 || location.shape == 3) {
            addWall(x, y, plane, rotation, kCornerBits, kCornerDx, kCornerDy, object.blocksProjectile);
        } else if (location.shape == 0 || location.shape == 2) {
            addWall(x, y, plane, rotation, kWallBits, kWallDx, kWallDy, object.blocksProjectile);
            if (location.shape == 2) addWall(x, y, plane, (rotation + 1) & 3, kWallBits, kWallDx, kWallDy, object.blocksProjectile);
        } else if (location.shape >= 9) {
            const int width = (rotation & 1) == 0 ? object.sizeX : object.sizeY;
            const int height = (rotation & 1) == 0 ? object.sizeY : object.sizeX;
            const std::uint32_t flags = kBlockObject | (object.blocksProjectile ? kBlockProjectileObject : 0);
            for (int dx = 0; dx < width; ++dx) {
                for (int dy = 0; dy < height; ++dy) add(x + dx, y + dy, plane, flags);
            }
        }
    }

private:
    static int baseX(int regionId) { return (regionId >> 8) * 64; }
    static int baseY(int regionId) { return (regionId & 0xFF) * 64; }

    void addWall(int x, int y, int plane, int rotation, const std::array<std::uint32_t, 4>& bits, const std::array<int, 4>& dx,
                 const std::array<int, 4>& dy, bool blocksProjectile) {
        std::uint32_t origin = bits[rotation];
        std::uint32_t beyond = bits[(rotation + 2) & 3];
        if (blocksProjectile) {
            origin |= origin << kProjectileShift;
            beyond |= beyond << kProjectileShift;
        }
        add(x, y, plane, origin);
        add(x + dx[rotation], y + dy[rotation], plane, beyond);
    }

    // Only the requested regions are written.
    void add(int x, int y, int plane, std::uint32_t flags) {
        if (x < 0 || x >= 16384 || y < 0 || y >= 16384 || plane < 0 || plane >= 4) return;
        const auto region = out_.find((x >> 6) << 8 | (y >> 6));
        if (region != out_.end()) region->second[(plane * 64 + (x & 63)) * 64 + (y & 63)] |= flags;
    }

    std::map<int, RegionFlags>& out_;
};

} // namespace

std::map<int, RegionFlags> staticCollision(const Store& store, std::span<const ObjectDefinition> objects,
                                           std::span<const int> regionIds) {
    std::vector<const ObjectDefinition*> byId;
    for (const ObjectDefinition& object : objects) {
        if (object.id < 0) continue;
        if (static_cast<std::size_t>(object.id) >= byId.size()) byId.resize(object.id + 1);
        byId[object.id] = &object;
    }

    // The requested regions and their neighbours, whose walls and footprints
    // can reach across the border.
    std::set<int> wanted;
    std::set<int> needed;
    for (const int id : regionIds) {
        if (id < 0 || id > 0xFFFF) continue;
        wanted.insert(id);
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                const int regionX = (id >> 8) + dx;
                const int regionY = (id & 0xFF) + dy;
                if (regionX >= 0 && regionX <= 0xFF && regionY >= 0 && regionY <= 0xFF) needed.insert(regionX << 8 | regionY);
            }
        }
    }
    std::map<int, Region> regions;
    for (const int id : needed) {
        if (std::optional<Region> region = regionOrNothing(store, id)) regions.emplace(id, std::move(*region));
    }

    std::map<int, RegionFlags> flags;
    for (const int id : wanted) {
        if (regions.contains(id)) flags[id].fill(0);
    }
    Builder builder(flags);
    for (const auto& [id, region] : regions) {
        if (flags.contains(id)) builder.addFloors(id, region);
        for (const Location& location : region.locations) {
            const ObjectDefinition* object =
                static_cast<std::size_t>(location.object) < byId.size() ? byId[location.object] : nullptr;
            if (object == nullptr) {
                throw std::runtime_error("object " + std::to_string(location.object) + " in region " + std::to_string(id)
                                         + " has no definition");
            }
            builder.addObject(id, region, location, *object);
        }
    }
    return flags;
}

} // namespace osrs::cache
