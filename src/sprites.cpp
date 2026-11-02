#include <osrs/cache/sprites.h>

#include "buffer.h"
#include "undecodable.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace osrs::cache {

namespace {

// The end of a group a part starts that many bytes before.
std::span<const std::uint8_t> fromEnd(std::span<const std::uint8_t> bytes, std::size_t count) {
    if (count > bytes.size()) throw std::runtime_error("sprite group shorter than its header");
    return bytes.subspan(bytes.size() - count);
}

// Row by row, or column by column with the vertical flag.
void readPlanes(Buffer& in, std::vector<std::uint8_t>& out, int width, int height, bool vertical) {
    if (!vertical) {
        for (std::uint8_t& value : out) value = static_cast<std::uint8_t>(in.u8());
        return;
    }
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) out[static_cast<std::size_t>(width) * y + x] = static_cast<std::uint8_t>(in.u8());
    }
}

// RuneLite SpriteLoader.load: the frames' pixels from the start, as palette
// indices; the sizes, then the palette, at the end.
std::vector<Sprite> decodeSprites(std::span<const std::uint8_t> bytes) {
    const int count = Buffer(fromEnd(bytes, 2)).u16();
    const std::size_t sizes = 7 + 8 * static_cast<std::size_t>(count);
    Buffer header(fromEnd(bytes, sizes));
    const int maxWidth = header.u16();
    const int maxHeight = header.u16();
    const int paletteLength = header.u8() + 1;
    std::vector<Sprite> sprites(count);
    for (Sprite& sprite : sprites) {
        sprite.maxWidth = maxWidth;
        sprite.maxHeight = maxHeight;
    }
    for (Sprite& sprite : sprites) sprite.offsetX = header.u16();
    for (Sprite& sprite : sprites) sprite.offsetY = header.u16();
    for (Sprite& sprite : sprites) sprite.width = header.u16();
    for (Sprite& sprite : sprites) sprite.height = header.u16();

    // Index 0 is transparent, so a black in the palette becomes 1.
    Buffer paletteIn(fromEnd(bytes, sizes + 3 * static_cast<std::size_t>(paletteLength - 1)));
    std::vector<std::uint32_t> palette(paletteLength);
    for (int i = 1; i < paletteLength; ++i) {
        palette[i] = static_cast<std::uint32_t>(paletteIn.u24());
        if (palette[i] == 0) palette[i] = 1;
    }

    Buffer in(bytes);
    for (Sprite& sprite : sprites) {
        const std::size_t pixels = static_cast<std::size_t>(sprite.width) * sprite.height;
        // Checked before allocating, so misread sizes fail as std::runtime_error rather than as
        // std::bad_alloc, which a store that skips undecodable objects doesn't catch.
        if (pixels > in.remaining()) throw std::runtime_error("sprite larger than its pixel data");
        std::vector<std::uint8_t>& indices = sprite.paletteIndices;
        indices.resize(pixels);
        std::vector<std::uint8_t> alphas(pixels);
        const int flags = in.u8();
        readPlanes(in, indices, sprite.width, sprite.height, (flags & 1) != 0);
        if ((flags & 2) != 0) readPlanes(in, alphas, sprite.width, sprite.height, (flags & 1) != 0);
        sprite.pixels.resize(pixels);
        for (std::size_t i = 0; i < pixels; ++i) {
            if (indices[i] >= paletteLength) throw std::runtime_error("sprite pixel past its palette");
            // RuneLite makes every coloured pixel opaque, whatever alpha the sprite gives it. Kept: in
            // the 38 sprites with an alpha plane, that alpha would hide most of their pixels.
            const std::uint32_t alpha = indices[i] != 0 ? 0xFF : alphas[i];
            sprite.pixels[i] = palette[indices[i]] | alpha << 24;
        }
        sprite.palette = palette;
    }
    return sprites;
}

// RuneLite TextureDefinition.adjustRGB.
std::uint32_t adjustRgb(std::uint32_t rgb, double brightness) {
    const double r = std::pow(static_cast<double>(rgb >> 16) / 256.0, brightness);
    const double g = std::pow(static_cast<double>(rgb >> 8 & 255) / 256.0, brightness);
    const double b = std::pow(static_cast<double>(rgb & 255) / 256.0, brightness);
    return static_cast<std::uint32_t>(static_cast<int>(b * 256.0) + (static_cast<int>(g * 256.0) << 8)
                                      + (static_cast<int>(r * 256.0) << 16));
}

} // namespace

std::optional<std::vector<Sprite>> loadSprites(const Store& store, int id) {
    const auto files = store.files(Index::Sprites, id);
    if (!files || files->empty()) return std::nullopt;
    return decodeOrSkip(store, "sprite", id, [&] { return decodeSprites(files->begin()->second); });
}

std::optional<Font> loadFont(const Store& store, int id) {
    const auto files = store.files(Index::Fonts, id);
    if (!files || !files->contains(0)) return std::nullopt;
    // RuneLite FontLoader.load: each character's advance, then the ascent.
    const std::vector<std::uint8_t>& bytes = files->at(0);
    return decodeOrSkip(store, "font", id, [&] {
        if (bytes.size() != 257) throw std::runtime_error(std::to_string(bytes.size()) + " bytes, not 257");
        Font font;
        std::copy_n(bytes.begin(), 256, font.advances.begin());
        font.ascent = bytes[256];
        return font;
    });
}

namespace {

// RuneLite TextureLoader.load, in the format its rev233 flag picks.
TextureDefinition decodeTexture(int id, std::span<const std::uint8_t> bytes, TextureFormat format) {
    Buffer in(bytes);
    TextureDefinition texture;
    texture.id = id;
    if (format == TextureFormat::Revision233) {
        texture.fileIds.push_back(in.u16());
        texture.averageHsl = static_cast<std::uint16_t>(in.u16());
        texture.opaque = in.u8() == 1;
    } else {
        texture.averageHsl = static_cast<std::uint16_t>(in.u16());
        texture.opaque = in.i8() != 0;
        const int count = in.u8();
        texture.fileIds.resize(count);
        for (std::int32_t& file : texture.fileIds) file = in.u16();
        if (count > 1) {
            texture.field1780.resize(count - 1);
            for (std::int32_t& value : texture.field1780) value = in.u8();
            texture.field1781.resize(count - 1);
            for (std::int32_t& value : texture.field1781) value = in.u8();
        }
        texture.field1786.resize(count);
        for (std::int32_t& value : texture.field1786) value = in.i32();
    }
    texture.animationDirection = static_cast<std::uint8_t>(in.u8());
    texture.animationSpeed = static_cast<std::uint8_t>(in.u8());
    texture.sprite = texture.fileIds.empty() ? -1 : texture.fileIds.front();
    return texture;
}

// SpriteDefinition.normalize: a sprite smaller than its frame has its palette
// indices placed in the whole frame. One as large keeps its indices, offset
// or not.
std::vector<std::uint8_t> normalizedIndices(const Sprite& sprite) {
    if (sprite.width == sprite.maxWidth && sprite.height == sprite.maxHeight) return sprite.paletteIndices;
    std::vector<std::uint8_t> frame(static_cast<std::size_t>(sprite.maxWidth) * sprite.maxHeight);
    for (int y = 0; y < sprite.height; ++y) {
        for (int x = 0; x < sprite.width; ++x) {
            const std::int64_t at = std::int64_t{x} + std::int64_t{y + sprite.offsetY} * sprite.maxWidth + sprite.offsetX;
            if (at < 0 || at >= static_cast<std::int64_t>(frame.size())) throw std::runtime_error("sprite pixels outside its frame");
            frame[static_cast<std::size_t>(at)] = sprite.paletteIndices[static_cast<std::size_t>(y) * sprite.width + x];
        }
    }
    return frame;
}

} // namespace

std::vector<TextureDefinition> loadTextureDefinitions(const Store& store, TextureFormat format) {
    const auto files = store.files(Index::Textures, 0);
    if (!files) throw std::runtime_error("the cache has no texture definitions");
    std::vector<TextureDefinition> textures;
    textures.reserve(files->size());
    for (const auto& [id, bytes] : *files) {
        try {
            textures.push_back(decodeTexture(id, bytes, format));
        } catch (const std::runtime_error& error) {
            undecodable(store, "texture", id, error);
        }
    }
    return textures;
}

// RuneLite TextureDefinition.method2680.
std::optional<std::vector<std::uint32_t>> texturePixels(const Store& store, const TextureDefinition& texture, double brightness, int size) {
    if (size < 0) throw std::runtime_error("negative texture size");
    const std::size_t area = static_cast<std::size_t>(size) * size;
    std::vector<std::uint32_t> pixels(area);
    for (std::size_t s = 0; s < texture.fileIds.size(); ++s) {
        const std::optional<std::vector<Sprite>> sprites = loadSprites(store, texture.fileIds[s]);
        if (!sprites || sprites->empty()) return std::nullopt;
        const Sprite& sprite = sprites->front();
        const std::vector<std::uint8_t> indices = normalizedIndices(sprite);
        std::vector<std::uint32_t> palette = sprite.palette;

        if (!texture.field1786.empty()) {
            if (s >= texture.field1786.size()) throw std::runtime_error("texture has fewer tints than sprites");
            const std::int32_t tint = texture.field1786[s];
            if ((static_cast<std::uint32_t>(tint) & 0xFF000000u) == 0x03000000u) {
                const std::uint32_t redBlue = static_cast<std::uint32_t>(tint) & 0xFF00FF;
                const std::uint32_t green = static_cast<std::uint32_t>(tint) >> 8 & 0xFF;
                for (std::uint32_t& colour : palette) {
                    if (colour >> 8 != (colour & 0xFFFF)) continue;
                    const std::uint32_t grey = colour & 0xFF;
                    // Java's int product, which wraps, shifted down signed.
                    const std::int32_t product = static_cast<std::int32_t>(redBlue * grey);
                    colour = (static_cast<std::uint32_t>(product >> 8) & 0xFF00FF) | (green * grey & 0xFF00);
                }
            }
        }
        for (std::uint32_t& colour : palette) colour = adjustRgb(colour, brightness);

        if (s > texture.field1780.size()) throw std::runtime_error("texture has fewer combine modes than sprites");
        if (s > 0 && texture.field1780[s - 1] != 0) continue;
        const auto texel = [&](std::size_t at) {
            if (at >= indices.size()) throw std::runtime_error("texture " + std::to_string(texture.id) + "'s sprite is too short");
            return palette[indices[at]];
        };
        if (size == sprite.maxWidth) {
            for (std::size_t i = 0; i < area; ++i) pixels[i] = texel(i);
        } else if (sprite.maxWidth == 64 && size == 128) {
            for (int y = 0; y < size; ++y) {
                for (int x = 0; x < size; ++x) pixels[static_cast<std::size_t>(y) * size + x] = texel(static_cast<std::size_t>(y >> 1 << 6) + (x >> 1));
            }
        } else if (sprite.maxWidth == 128 && size == 64) {
            for (int y = 0; y < size; ++y) {
                for (int x = 0; x < size; ++x) pixels[static_cast<std::size_t>(y) * size + x] = texel(static_cast<std::size_t>(x << 1) + (y << 1 << 7));
            }
        } else {
            throw std::runtime_error("texture " + std::to_string(texture.id) + "'s sprite is " + std::to_string(sprite.maxWidth) + " wide");
        }
    }
    return pixels;
}

} // namespace osrs::cache
