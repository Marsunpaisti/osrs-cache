#pragma once

// Colour arithmetic the textures and the item icons share.

#include <cstdint>

namespace osrs::cache {

// Each channel of an RGB colour, as a fraction of 256, raised to the power
// `brightness` (below 1 brightens).
std::uint32_t adjustRgb(std::uint32_t rgb, double brightness);

} // namespace osrs::cache
