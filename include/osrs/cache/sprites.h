#pragma once

// Sprites (index 8), and the textures (index 9) and fonts (index 13) made
// from them, decoded as RuneLite's cache library decodes them
// (net.runelite:cache 1.13.5: definitions/loaders SpriteLoader, FontLoader
// and TextureLoader, and TextureDefinition.method2680).

#include <osrs/cache/store.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace osrs::cache {

struct Sprite {
    // Where the pixels sit in a frame of maxWidth by maxHeight.
    int offsetX = 0;
    int offsetY = 0;
    int width = 0;
    int height = 0;
    int maxWidth = 0;
    int maxHeight = 0;
    // ARGB, row by row; 0 is transparent, and an opaque black is 0xFF000001.
    std::vector<std::uint32_t> pixels;
    // The palette index of each pixel, row by row (RuneLite's pixelIdx), and
    // the palette, which every frame of the group shares: RGB, 0 at index 0
    // for transparent, and 1 for black.
    std::vector<std::uint8_t> paletteIndices;
    std::vector<std::uint32_t> palette;
};

// A sprite group's frames, in order. Nothing if the cache has no such group.
// Throws std::runtime_error if it doesn't decode, unless the store skips
// such objects (store.h).
std::optional<std::vector<Sprite>> loadSprites(const Store& store, int id);

// A font's metrics. Its glyphs are the frames of the sprite group with the
// font's id, one per Windows-1252 character code. The client finds fonts by
// name; in the current cache p11_full is 494, p12_full 495, b12_full 496 and
// q8_full 497.
struct Font {
    // How far the pen moves after each character code.
    std::array<std::uint8_t, 256> advances{};
    std::uint8_t ascent = 0;
};

// Nothing if the cache has no such font. Throws std::runtime_error if it
// doesn't decode, unless the store skips such objects (store.h).
std::optional<Font> loadFont(const Store& store, int id);

struct TextureDefinition {
    std::int32_t id = 0;
    // The sprite group whose first frame holds its pixels: fileIds' first, -1
    // if it has none.
    std::int32_t sprite = 0;
    // The client's 16-bit HSL it draws the texture's faces in untextured
    // (RuneLite's missingColor).
    std::uint16_t averageHsl = 0;
    // Whether a face draws the texture's empty (0) texels too, as black;
    // otherwise they show what lies behind. RuneLite's field1778, as the
    // client's rasterizer reads it (Encounter Replay's port, graphics3d.ts).
    bool opaque = false;
    // RuneLite's animationDirection and animationSpeed, for textures that
    // scroll, as water and lava.
    std::uint8_t animationDirection = 0;
    std::uint8_t animationSpeed = 0;

    // The sprite groups it is drawn from, in order; one in the revision 233
    // format.
    std::vector<std::int32_t> fileIds;
    // The older format's, by RuneLite's names: for each sprite after the
    // first, how it combines with those before it (field1780: texturePixels
    // draws a sprite of 0 over them and skips any other, as RuneLite does)
    // and field1781; and for each sprite, a colour that tints its grey
    // palette entries when its top byte is 3 (field1786). Empty in the
    // revision 233 format.
    std::vector<std::int32_t> field1780;
    std::vector<std::int32_t> field1781;
    std::vector<std::int32_t> field1786;
};

// The two formats RuneLite's TextureLoader reads: its rev233 flag, which is
// on unless a caller turns it off. RuneLite's TextureManager never does, and
// the current cache holds the revision 233 format.
enum class TextureFormat : std::uint8_t {
    Revision233,
    Older,
};

// Every texture, by ascending id. Throws std::runtime_error if the cache
// lacks them, or if one doesn't decode unless the store skips such objects
// (store.h).
std::vector<TextureDefinition> loadTextureDefinitions(const Store& store, TextureFormat format = TextureFormat::Revision233);

// The texture's `size` by `size` RGB pixels, row by row, as RuneLite's
// TextureDefinition.method2680 builds them from the first frame of each of
// its sprites: each channel of each colour, as a fraction of 256, raised to
// the power `brightness` (the client's brightness setting; below 1
// brightens). A sprite as wide as `size` is copied, and one 64 wide is
// doubled to 128 and one 128 wide halved to 64. Nothing if a sprite is
// missing or has no frames. Throws std::runtime_error, as RuneLite does, if
// a sprite is any other width, or too short for `size`.
std::optional<std::vector<std::uint32_t>> texturePixels(const Store& store, const TextureDefinition& texture, double brightness, int size = 128);

} // namespace osrs::cache
