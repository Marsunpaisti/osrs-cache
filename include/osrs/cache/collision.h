#pragma once

// Static collision flags built from the cache's maps: each region's terrain
// and the objects placed on it. Static means what the cache alone gives: no
// opened doors, server-spawned objects, account-dependent scenery, NPCs or
// instance rotations.
//
// The rules are those of CacheMapDumper's FullCollisionMap without its
// navigation exclusions (the "static-cache-base-definitions-v1" policy its
// docs/full-collision-format.md describes), which RuneLite's cache library
// decodes the maps for (MapLoader, LocationsLoader):
// - a terrain tile whose settings have bit 1 blocks as floor (0x200000);
// - bridge bit 2 on plane 1 lowers the floors and objects of that tile by a
//   plane, and those below plane 0 are dropped;
// - hollow objects, and those with interact type 0, don't block;
// - walls (shapes 0 to 3) set their wall bit and the opposite bit on the
//   tile beyond, L-shaped walls (2) both sides, and the projectile bit
//   (shifted left by 9) too if they block projectiles;
// - wall decorations (4 to 8) don't block;
// - shapes 9 to 21 block their rotated footprint (0x100, and 0x20000 if they
//   block projectiles);
// - floor decorations (22) block (0x40000) only with interact type 1.
// Inside the Fight Caves the result matched the live client's collision.

#include <osrs/cache/definitions.h>
#include <osrs/cache/store.h>

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace osrs::cache {

// One region's flags in the client's bit layout, indexed
// (plane * 64 + x) * 64 + y with x and y local to the region.
using RegionFlags = std::array<std::uint32_t, 4 * 64 * 64>;

// The flags of each requested region that the cache has whole, by region id
// ((regionX << 8) | regionY). A region whose terrain or objects are missing
// or don't decode is left out, so callers must treat it as unknown, not as
// open ground. Walls and footprints reaching in from neighbouring regions
// count. Throws std::runtime_error if an object placed on the map has no
// definition in `objects` or an unknown shape.
std::map<int, RegionFlags> staticCollision(const Store& store, std::span<const ObjectDefinition> objects,
                                           std::span<const int> regionIds);

} // namespace osrs::cache
