#include "graphics3d.h"

#include "colour.h"

#include <algorithm>
#include <cmath>

namespace osrs::cache {

namespace {

// RuneLite JagexColor.HSLtoRGB.
std::int32_t hslToRgb(int hsl, double brightness) {
    const double hue = static_cast<double>(hsl >> 10 & 63) / 64.0 + 0.5 / 64.0;
    const double saturation = static_cast<double>(hsl >> 7 & 7) / 8.0 + 0.5 / 8.0;
    const double luminance = static_cast<double>(hsl & 127) / 128.0;

    const double chroma = (1.0 - std::abs(2.0 * luminance - 1.0)) * saturation;
    const double x = chroma * (1 - std::abs(std::fmod(hue * 6.0, 2.0) - 1.0));
    const double lightness = luminance - chroma / 2;

    double r = lightness, g = lightness, b = lightness;
    switch (static_cast<int>(hue * 6.0)) {
    case 0: r += chroma; g += x; break;
    case 1: g += chroma; r += x; break;
    case 2: g += chroma; b += x; break;
    case 3: b += chroma; g += x; break;
    case 4: b += chroma; r += x; break;
    default: r += chroma; b += x; break;
    }

    const int rgb = static_cast<int>(r * 256.0) << 16 | static_cast<int>(g * 256.0) << 8 | static_cast<int>(b * 256.0);
    const auto adjusted = static_cast<std::int32_t>(adjustRgb(static_cast<std::uint32_t>(rgb), brightness));
    return adjusted == 0 ? 1 : adjusted;
}

} // namespace

// RuneLite JagexColor.createPalette.
std::vector<std::int32_t> hslPalette(double brightness) {
    std::vector<std::int32_t> palette(65536);
    for (int i = 0; i < 65536; ++i) palette[i] = hslToRgb(i, brightness);
    return palette;
}

// RuneLite RSTextureProvider's constructor: an array as long as the highest id.
TextureCache::TextureCache(const Store& store) : store_(store) {
    std::vector<TextureDefinition> textures = loadTextureDefinitions(store);
    int highest = -1;
    for (const TextureDefinition& texture : textures) highest = std::max(highest, static_cast<int>(texture.id));
    textures_.resize(static_cast<std::size_t>(highest + 1));
    pixels_.resize(textures_.size());
    for (TextureDefinition& texture : textures) textures_[texture.id] = std::move(texture);
}

const TextureDefinition& TextureCache::at(int id) const {
    if (id < 0 || id >= static_cast<int>(textures_.size())) throw std::runtime_error("texture " + std::to_string(id) + " is beyond the last");
    // RuneLite's vmethod3057 reads a missing texture's field, which throws.
    if (!textures_[id]) throw std::runtime_error("texture " + std::to_string(id) + " is missing");
    return *textures_[id];
}

// RuneLite RSTextureProvider.load, at ItemSpriteFactory's brightness
// (JagexColor.BRIGHTNESS_MAX) and RSTextureProvider's width.
const std::int32_t* TextureCache::load(int id) {
    if (id < 0 || id >= static_cast<int>(textures_.size())) throw std::runtime_error("texture " + std::to_string(id) + " is beyond the last");
    if (!textures_[id]) return nullptr;
    std::vector<std::int32_t>& pixels = pixels_[id];
    if (pixels.empty()) {
        // RuneLite's method2680 throws for a missing sprite, where
        // texturePixels returns nothing.
        const std::optional<std::vector<std::uint32_t>> made = texturePixels(store_, *textures_[id], 0.6, 128);
        if (!made) throw std::runtime_error("texture " + std::to_string(id) + "'s sprite is missing");
        pixels.assign(made->begin(), made->end());
    }
    return pixels.data();
}

int TextureCache::averageHsl(int id) const {
    if (id < 0 || id >= static_cast<int>(textures_.size())) throw std::runtime_error("texture " + std::to_string(id) + " is beyond the last");
    return textures_[id] ? textures_[id]->averageHsl : 0;
}

bool TextureCache::opaque(int id) const { return at(id).opaque; }

// RuneLite Rasterizer2D.setRasterBuffer, Graphics3D.setRasterClipping and
// setOffset(16, 16), as ItemSpriteFactory calls them.
Graphics3D::Graphics3D(TextureCache& textures, const std::vector<std::int32_t>& palette, std::int32_t* pixels, int width, int height)
    : rasterClipX(width), colorPalette(palette.data(), static_cast<int>(palette.size())), textureProvider(textures),
      graphicsPixels(pixels, width * height), graphicsPixelsWidth(width), Rasterizer3D_clipHeight(height) {
    if (height > 1024) throw std::runtime_error("too tall to rasterize");
    for (int i = 0; i < height; ++i) rasterClipY[i] = i * width;
    centerX = 16;
    centerY = 16;
}

void Graphics3D::rasterGouraud(int var0, int var1, int var2, int var3, int var4, int var5, int var6, int var7, int var8) {
    int var9 = var4 - var3;
    int var10 = var1 - var0;
    int var11 = var5 - var3;
    int var12 = var2 - var0;
    int var13 = var7 - var6;
    int var14 = var8 - var6;
    int var15;
    if (var2 != var1) {
        var15 = jdiv(var5 - var4 << 14, var2 - var1);
    } else {
        var15 = 0;
    }

    int var16;
    if (var0 != var1) {
        var16 = jdiv(var9 << 14, var10);
    } else {
        var16 = 0;
    }

    int var17;
    if (var0 != var2) {
        var17 = jdiv(var11 << 14, var12);
    } else {
        var17 = 0;
    }

    int var18 = var9 * var12 - var11 * var10;
    if (var18 != 0) {
        int var19 = jdiv(var13 * var12 - var14 * var10 << 8, var18);
        int var20 = jdiv(var14 * var9 - var13 * var11 << 8, var18);
        if (var0 <= var1 && var0 <= var2) {
            if (var0 < Rasterizer3D_clipHeight) {
                if (var1 > Rasterizer3D_clipHeight) {
                    var1 = Rasterizer3D_clipHeight;
                }

                if (var2 > Rasterizer3D_clipHeight) {
                    var2 = Rasterizer3D_clipHeight;
                }

                var6 = var19 + ((var6 << 8) - var3 * var19);
                if (var1 < var2) {
                    var5 = var3 <<= 14;
                    if (var0 < 0) {
                        var5 -= var0 * var17;
                        var3 -= var0 * var16;
                        var6 -= var0 * var20;
                        var0 = 0;
                    }

                    var4 <<= 14;
                    if (var1 < 0) {
                        var4 -= var15 * var1;
                        var1 = 0;
                    }

                    if (var0 != var1 && var17 < var16 || var0 == var1 && var17 > var15) {
                        var2 -= var1;
                        var1 -= var0;
                        var0 = rasterClipY[var0];

                        while (true) {
                            --var1;
                            if (var1 < 0) {
                                while (true) {
                                    --var2;
                                    if (var2 < 0) {
                                        return;
                                    }

                                    method2778(graphicsPixels, var0, 0, 0, var5 >> 14, var4 >> 14, var6, var19);
                                    var5 += var17;
                                    var4 += var15;
                                    var6 += var20;
                                    var0 += graphicsPixelsWidth;
                                }
                            }

                            method2778(graphicsPixels, var0, 0, 0, var5 >> 14, var3 >> 14, var6, var19);
                            var5 += var17;
                            var3 += var16;
                            var6 += var20;
                            var0 += graphicsPixelsWidth;
                        }
                    } else {
                        var2 -= var1;
                        var1 -= var0;
                        var0 = rasterClipY[var0];

                        while (true) {
                            --var1;
                            if (var1 < 0) {
                                while (true) {
                                    --var2;
                                    if (var2 < 0) {
                                        return;
                                    }

                                    method2778(graphicsPixels, var0, 0, 0, var4 >> 14, var5 >> 14, var6, var19);
                                    var5 += var17;
                                    var4 += var15;
                                    var6 += var20;
                                    var0 += graphicsPixelsWidth;
                                }
                            }

                            method2778(graphicsPixels, var0, 0, 0, var3 >> 14, var5 >> 14, var6, var19);
                            var5 += var17;
                            var3 += var16;
                            var6 += var20;
                            var0 += graphicsPixelsWidth;
                        }
                    }
                } else {
                    var4 = var3 <<= 14;
                    if (var0 < 0) {
                        var4 -= var0 * var17;
                        var3 -= var0 * var16;
                        var6 -= var0 * var20;
                        var0 = 0;
                    }

                    var5 <<= 14;
                    if (var2 < 0) {
                        var5 -= var15 * var2;
                        var2 = 0;
                    }

                    if (var0 != var2 && var17 < var16 || var0 == var2 && var15 > var16) {
                        var1 -= var2;
                        var2 -= var0;
                        var0 = rasterClipY[var0];

                        while (true) {
                            --var2;
                            if (var2 < 0) {
                                while (true) {
                                    --var1;
                                    if (var1 < 0) {
                                        return;
                                    }

                                    method2778(graphicsPixels, var0, 0, 0, var5 >> 14, var3 >> 14, var6, var19);
                                    var5 += var15;
                                    var3 += var16;
                                    var6 += var20;
                                    var0 += graphicsPixelsWidth;
                                }
                            }

                            method2778(graphicsPixels, var0, 0, 0, var4 >> 14, var3 >> 14, var6, var19);
                            var4 += var17;
                            var3 += var16;
                            var6 += var20;
                            var0 += graphicsPixelsWidth;
                        }
                    } else {
                        var1 -= var2;
                        var2 -= var0;
                        var0 = rasterClipY[var0];

                        while (true) {
                            --var2;
                            if (var2 < 0) {
                                while (true) {
                                    --var1;
                                    if (var1 < 0) {
                                        return;
                                    }

                                    method2778(graphicsPixels, var0, 0, 0, var3 >> 14, var5 >> 14, var6, var19);
                                    var5 += var15;
                                    var3 += var16;
                                    var6 += var20;
                                    var0 += graphicsPixelsWidth;
                                }
                            }

                            method2778(graphicsPixels, var0, 0, 0, var3 >> 14, var4 >> 14, var6, var19);
                            var4 += var17;
                            var3 += var16;
                            var6 += var20;
                            var0 += graphicsPixelsWidth;
                        }
                    }
                }
            }
        } else if (var1 <= var2) {
            if (var1 < Rasterizer3D_clipHeight) {
                if (var2 > Rasterizer3D_clipHeight) {
                    var2 = Rasterizer3D_clipHeight;
                }

                if (var0 > Rasterizer3D_clipHeight) {
                    var0 = Rasterizer3D_clipHeight;
                }

                var7 = var19 + ((var7 << 8) - var19 * var4);
                if (var2 < var0) {
                    var3 = var4 <<= 14;
                    if (var1 < 0) {
                        var3 -= var16 * var1;
                        var4 -= var15 * var1;
                        var7 -= var20 * var1;
                        var1 = 0;
                    }

                    var5 <<= 14;
                    if (var2 < 0) {
                        var5 -= var17 * var2;
                        var2 = 0;
                    }

                    if (var2 != var1 && var16 < var15 || var2 == var1 && var16 > var17) {
                        var0 -= var2;
                        var2 -= var1;
                        var1 = rasterClipY[var1];

                        while (true) {
                            --var2;
                            if (var2 < 0) {
                                while (true) {
                                    --var0;
                                    if (var0 < 0) {
                                        return;
                                    }

                                    method2778(graphicsPixels, var1, 0, 0, var3 >> 14, var5 >> 14, var7, var19);
                                    var3 += var16;
                                    var5 += var17;
                                    var7 += var20;
                                    var1 += graphicsPixelsWidth;
                                }
                            }

                            method2778(graphicsPixels, var1, 0, 0, var3 >> 14, var4 >> 14, var7, var19);
                            var3 += var16;
                            var4 += var15;
                            var7 += var20;
                            var1 += graphicsPixelsWidth;
                        }
                    } else {
                        var0 -= var2;
                        var2 -= var1;
                        var1 = rasterClipY[var1];

                        while (true) {
                            --var2;
                            if (var2 < 0) {
                                while (true) {
                                    --var0;
                                    if (var0 < 0) {
                                        return;
                                    }

                                    method2778(graphicsPixels, var1, 0, 0, var5 >> 14, var3 >> 14, var7, var19);
                                    var3 += var16;
                                    var5 += var17;
                                    var7 += var20;
                                    var1 += graphicsPixelsWidth;
                                }
                            }

                            method2778(graphicsPixels, var1, 0, 0, var4 >> 14, var3 >> 14, var7, var19);
                            var3 += var16;
                            var4 += var15;
                            var7 += var20;
                            var1 += graphicsPixelsWidth;
                        }
                    }
                } else {
                    var5 = var4 <<= 14;
                    if (var1 < 0) {
                        var5 -= var16 * var1;
                        var4 -= var15 * var1;
                        var7 -= var20 * var1;
                        var1 = 0;
                    }

                    var3 <<= 14;
                    if (var0 < 0) {
                        var3 -= var0 * var17;
                        var0 = 0;
                    }

                    if (var16 < var15) {
                        var2 -= var0;
                        var0 -= var1;
                        var1 = rasterClipY[var1];

                        while (true) {
                            --var0;
                            if (var0 < 0) {
                                while (true) {
                                    --var2;
                                    if (var2 < 0) {
                                        return;
                                    }

                                    method2778(graphicsPixels, var1, 0, 0, var3 >> 14, var4 >> 14, var7, var19);
                                    var3 += var17;
                                    var4 += var15;
                                    var7 += var20;
                                    var1 += graphicsPixelsWidth;
                                }
                            }

                            method2778(graphicsPixels, var1, 0, 0, var5 >> 14, var4 >> 14, var7, var19);
                            var5 += var16;
                            var4 += var15;
                            var7 += var20;
                            var1 += graphicsPixelsWidth;
                        }
                    } else {
                        var2 -= var0;
                        var0 -= var1;
                        var1 = rasterClipY[var1];

                        while (true) {
                            --var0;
                            if (var0 < 0) {
                                while (true) {
                                    --var2;
                                    if (var2 < 0) {
                                        return;
                                    }

                                    method2778(graphicsPixels, var1, 0, 0, var4 >> 14, var3 >> 14, var7, var19);
                                    var3 += var17;
                                    var4 += var15;
                                    var7 += var20;
                                    var1 += graphicsPixelsWidth;
                                }
                            }

                            method2778(graphicsPixels, var1, 0, 0, var4 >> 14, var5 >> 14, var7, var19);
                            var5 += var16;
                            var4 += var15;
                            var7 += var20;
                            var1 += graphicsPixelsWidth;
                        }
                    }
                }
            }
        } else if (var2 < Rasterizer3D_clipHeight) {
            if (var0 > Rasterizer3D_clipHeight) {
                var0 = Rasterizer3D_clipHeight;
            }

            if (var1 > Rasterizer3D_clipHeight) {
                var1 = Rasterizer3D_clipHeight;
            }

            var8 = var19 + ((var8 << 8) - var5 * var19);
            if (var0 < var1) {
                var4 = var5 <<= 14;
                if (var2 < 0) {
                    var4 -= var15 * var2;
                    var5 -= var17 * var2;
                    var8 -= var20 * var2;
                    var2 = 0;
                }

                var3 <<= 14;
                if (var0 < 0) {
                    var3 -= var0 * var16;
                    var0 = 0;
                }

                if (var15 < var17) {
                    var1 -= var0;
                    var0 -= var2;
                    var2 = rasterClipY[var2];

                    while (true) {
                        --var0;
                        if (var0 < 0) {
                            while (true) {
                                --var1;
                                if (var1 < 0) {
                                    return;
                                }

                                method2778(graphicsPixels, var2, 0, 0, var4 >> 14, var3 >> 14, var8, var19);
                                var4 += var15;
                                var3 += var16;
                                var8 += var20;
                                var2 += graphicsPixelsWidth;
                            }
                        }

                        method2778(graphicsPixels, var2, 0, 0, var4 >> 14, var5 >> 14, var8, var19);
                        var4 += var15;
                        var5 += var17;
                        var8 += var20;
                        var2 += graphicsPixelsWidth;
                    }
                } else {
                    var1 -= var0;
                    var0 -= var2;
                    var2 = rasterClipY[var2];

                    while (true) {
                        --var0;
                        if (var0 < 0) {
                            while (true) {
                                --var1;
                                if (var1 < 0) {
                                    return;
                                }

                                method2778(graphicsPixels, var2, 0, 0, var3 >> 14, var4 >> 14, var8, var19);
                                var4 += var15;
                                var3 += var16;
                                var8 += var20;
                                var2 += graphicsPixelsWidth;
                            }
                        }

                        method2778(graphicsPixels, var2, 0, 0, var5 >> 14, var4 >> 14, var8, var19);
                        var4 += var15;
                        var5 += var17;
                        var8 += var20;
                        var2 += graphicsPixelsWidth;
                    }
                }
            } else {
                var3 = var5 <<= 14;
                if (var2 < 0) {
                    var3 -= var15 * var2;
                    var5 -= var17 * var2;
                    var8 -= var20 * var2;
                    var2 = 0;
                }

                var4 <<= 14;
                if (var1 < 0) {
                    var4 -= var16 * var1;
                    var1 = 0;
                }

                if (var15 < var17) {
                    var0 -= var1;
                    var1 -= var2;
                    var2 = rasterClipY[var2];

                    while (true) {
                        --var1;
                        if (var1 < 0) {
                            while (true) {
                                --var0;
                                if (var0 < 0) {
                                    return;
                                }

                                method2778(graphicsPixels, var2, 0, 0, var4 >> 14, var5 >> 14, var8, var19);
                                var4 += var16;
                                var5 += var17;
                                var8 += var20;
                                var2 += graphicsPixelsWidth;
                            }
                        }

                        method2778(graphicsPixels, var2, 0, 0, var3 >> 14, var5 >> 14, var8, var19);
                        var3 += var15;
                        var5 += var17;
                        var8 += var20;
                        var2 += graphicsPixelsWidth;
                    }
                } else {
                    var0 -= var1;
                    var1 -= var2;
                    var2 = rasterClipY[var2];

                    while (true) {
                        --var1;
                        if (var1 < 0) {
                            while (true) {
                                --var0;
                                if (var0 < 0) {
                                    return;
                                }

                                method2778(graphicsPixels, var2, 0, 0, var5 >> 14, var4 >> 14, var8, var19);
                                var4 += var16;
                                var5 += var17;
                                var8 += var20;
                                var2 += graphicsPixelsWidth;
                            }
                        }

                        method2778(graphicsPixels, var2, 0, 0, var5 >> 14, var3 >> 14, var8, var19);
                        var3 += var15;
                        var5 += var17;
                        var8 += var20;
                        var2 += graphicsPixelsWidth;
                    }
                }
            }
        }
    }
}

void Graphics3D::method2778(JavaArray<std::int32_t> var0, int var1, int var2, int var3, int var4, int var5, int var6, int var7) {
    if (rasterClipEnable) {
        if (var5 > rasterClipX) {
            var5 = rasterClipX;
        }

        if (var4 < 0) {
            var4 = 0;
        }
    }

    if (var4 < var5) {
        var1 += var4;
        var6 += var4 * var7;
        int var8;
        int var9;
        int var10;
        var3 = var5 - var4;
        if (rasterAlpha == 0) {
            do {
                var0[var1++] = colorPalette[var6 >> 8];
                var6 += var7;
                --var3;
            } while (var3 > 0);
        } else {
            var8 = rasterAlpha;
            var9 = 256 - rasterAlpha;

            do {
                var2 = colorPalette[var6 >> 8];
                var6 += var7;
                var2 = (var9 * (var2 & 65280) >> 8 & 65280) + (var9 * (var2 & 16711935) >> 8 & 16711935);
                var10 = var0[var1];
                var0[var1++] = ((var10 & 16711935) * var8 >> 8 & 16711935) + var2 + (var8 * (var10 & 65280) >> 8 & 65280);
                --var3;
            } while (var3 > 0);
        }
    }
}

void Graphics3D::rasterFlat(int var0, int var1, int var2, int var3, int var4, int var5, int var6) {
    int var7 = 0;
    if (var0 != var1) {
        var7 = jdiv(var4 - var3 << 14, var1 - var0);
    }

    int var8 = 0;
    if (var2 != var1) {
        var8 = jdiv(var5 - var4 << 14, var2 - var1);
    }

    int var9 = 0;
    if (var0 != var2) {
        var9 = jdiv(var3 - var5 << 14, var0 - var2);
    }

    if (var0 <= var1 && var0 <= var2) {
        if (var0 < Rasterizer3D_clipHeight) {
            if (var1 > Rasterizer3D_clipHeight) {
                var1 = Rasterizer3D_clipHeight;
            }

            if (var2 > Rasterizer3D_clipHeight) {
                var2 = Rasterizer3D_clipHeight;
            }

            if (var1 < var2) {
                var5 = var3 <<= 14;
                if (var0 < 0) {
                    var5 -= var0 * var9;
                    var3 -= var0 * var7;
                    var0 = 0;
                }

                var4 <<= 14;
                if (var1 < 0) {
                    var4 -= var8 * var1;
                    var1 = 0;
                }

                if ((var0 == var1 || var9 >= var7) && (var0 != var1 || var9 <= var8)) {
                    var2 -= var1;
                    var1 -= var0;
                    var0 = rasterClipY[var0];

                    while (true) {
                        --var1;
                        if (var1 < 0) {
                            while (true) {
                                --var2;
                                if (var2 < 0) {
                                    return;
                                }

                                method2842(graphicsPixels, var0, var6, 0, var4 >> 14, var5 >> 14);
                                var5 += var9;
                                var4 += var8;
                                var0 += graphicsPixelsWidth;
                            }
                        }

                        method2842(graphicsPixels, var0, var6, 0, var3 >> 14, var5 >> 14);
                        var5 += var9;
                        var3 += var7;
                        var0 += graphicsPixelsWidth;
                    }
                } else {
                    var2 -= var1;
                    var1 -= var0;
                    var0 = rasterClipY[var0];

                    while (true) {
                        --var1;
                        if (var1 < 0) {
                            while (true) {
                                --var2;
                                if (var2 < 0) {
                                    return;
                                }

                                method2842(graphicsPixels, var0, var6, 0, var5 >> 14, var4 >> 14);
                                var5 += var9;
                                var4 += var8;
                                var0 += graphicsPixelsWidth;
                            }
                        }

                        method2842(graphicsPixels, var0, var6, 0, var5 >> 14, var3 >> 14);
                        var5 += var9;
                        var3 += var7;
                        var0 += graphicsPixelsWidth;
                    }
                }
            } else {
                var4 = var3 <<= 14;
                if (var0 < 0) {
                    var4 -= var0 * var9;
                    var3 -= var0 * var7;
                    var0 = 0;
                }

                var5 <<= 14;
                if (var2 < 0) {
                    var5 -= var8 * var2;
                    var2 = 0;
                }

                if (var0 != var2 && var9 < var7 || var0 == var2 && var8 > var7) {
                    var1 -= var2;
                    var2 -= var0;
                    var0 = rasterClipY[var0];

                    while (true) {
                        --var2;
                        if (var2 < 0) {
                            while (true) {
                                --var1;
                                if (var1 < 0) {
                                    return;
                                }

                                method2842(graphicsPixels, var0, var6, 0, var5 >> 14, var3 >> 14);
                                var5 += var8;
                                var3 += var7;
                                var0 += graphicsPixelsWidth;
                            }
                        }

                        method2842(graphicsPixels, var0, var6, 0, var4 >> 14, var3 >> 14);
                        var4 += var9;
                        var3 += var7;
                        var0 += graphicsPixelsWidth;
                    }
                } else {
                    var1 -= var2;
                    var2 -= var0;
                    var0 = rasterClipY[var0];

                    while (true) {
                        --var2;
                        if (var2 < 0) {
                            while (true) {
                                --var1;
                                if (var1 < 0) {
                                    return;
                                }

                                method2842(graphicsPixels, var0, var6, 0, var3 >> 14, var5 >> 14);
                                var5 += var8;
                                var3 += var7;
                                var0 += graphicsPixelsWidth;
                            }
                        }

                        method2842(graphicsPixels, var0, var6, 0, var3 >> 14, var4 >> 14);
                        var4 += var9;
                        var3 += var7;
                        var0 += graphicsPixelsWidth;
                    }
                }
            }
        }
    } else if (var1 <= var2) {
        if (var1 < Rasterizer3D_clipHeight) {
            if (var2 > Rasterizer3D_clipHeight) {
                var2 = Rasterizer3D_clipHeight;
            }

            if (var0 > Rasterizer3D_clipHeight) {
                var0 = Rasterizer3D_clipHeight;
            }

            if (var2 < var0) {
                var3 = var4 <<= 14;
                if (var1 < 0) {
                    var3 -= var7 * var1;
                    var4 -= var8 * var1;
                    var1 = 0;
                }

                var5 <<= 14;
                if (var2 < 0) {
                    var5 -= var9 * var2;
                    var2 = 0;
                }

                if ((var2 == var1 || var7 >= var8) && (var2 != var1 || var7 <= var9)) {
                    var0 -= var2;
                    var2 -= var1;
                    var1 = rasterClipY[var1];

                    while (true) {
                        --var2;
                        if (var2 < 0) {
                            while (true) {
                                --var0;
                                if (var0 < 0) {
                                    return;
                                }

                                method2842(graphicsPixels, var1, var6, 0, var5 >> 14, var3 >> 14);
                                var3 += var7;
                                var5 += var9;
                                var1 += graphicsPixelsWidth;
                            }
                        }

                        method2842(graphicsPixels, var1, var6, 0, var4 >> 14, var3 >> 14);
                        var3 += var7;
                        var4 += var8;
                        var1 += graphicsPixelsWidth;
                    }
                } else {
                    var0 -= var2;
                    var2 -= var1;
                    var1 = rasterClipY[var1];

                    while (true) {
                        --var2;
                        if (var2 < 0) {
                            while (true) {
                                --var0;
                                if (var0 < 0) {
                                    return;
                                }

                                method2842(graphicsPixels, var1, var6, 0, var3 >> 14, var5 >> 14);
                                var3 += var7;
                                var5 += var9;
                                var1 += graphicsPixelsWidth;
                            }
                        }

                        method2842(graphicsPixels, var1, var6, 0, var3 >> 14, var4 >> 14);
                        var3 += var7;
                        var4 += var8;
                        var1 += graphicsPixelsWidth;
                    }
                }
            } else {
                var5 = var4 <<= 14;
                if (var1 < 0) {
                    var5 -= var7 * var1;
                    var4 -= var8 * var1;
                    var1 = 0;
                }

                var3 <<= 14;
                if (var0 < 0) {
                    var3 -= var0 * var9;
                    var0 = 0;
                }

                if (var7 < var8) {
                    var2 -= var0;
                    var0 -= var1;
                    var1 = rasterClipY[var1];

                    while (true) {
                        --var0;
                        if (var0 < 0) {
                            while (true) {
                                --var2;
                                if (var2 < 0) {
                                    return;
                                }

                                method2842(graphicsPixels, var1, var6, 0, var3 >> 14, var4 >> 14);
                                var3 += var9;
                                var4 += var8;
                                var1 += graphicsPixelsWidth;
                            }
                        }

                        method2842(graphicsPixels, var1, var6, 0, var5 >> 14, var4 >> 14);
                        var5 += var7;
                        var4 += var8;
                        var1 += graphicsPixelsWidth;
                    }
                } else {
                    var2 -= var0;
                    var0 -= var1;
                    var1 = rasterClipY[var1];

                    while (true) {
                        --var0;
                        if (var0 < 0) {
                            while (true) {
                                --var2;
                                if (var2 < 0) {
                                    return;
                                }

                                method2842(graphicsPixels, var1, var6, 0, var4 >> 14, var3 >> 14);
                                var3 += var9;
                                var4 += var8;
                                var1 += graphicsPixelsWidth;
                            }
                        }

                        method2842(graphicsPixels, var1, var6, 0, var4 >> 14, var5 >> 14);
                        var5 += var7;
                        var4 += var8;
                        var1 += graphicsPixelsWidth;
                    }
                }
            }
        }
    } else if (var2 < Rasterizer3D_clipHeight) {
        if (var0 > Rasterizer3D_clipHeight) {
            var0 = Rasterizer3D_clipHeight;
        }

        if (var1 > Rasterizer3D_clipHeight) {
            var1 = Rasterizer3D_clipHeight;
        }

        if (var0 < var1) {
            var4 = var5 <<= 14;
            if (var2 < 0) {
                var4 -= var8 * var2;
                var5 -= var9 * var2;
                var2 = 0;
            }

            var3 <<= 14;
            if (var0 < 0) {
                var3 -= var0 * var7;
                var0 = 0;
            }

            if (var8 < var9) {
                var1 -= var0;
                var0 -= var2;
                var2 = rasterClipY[var2];

                while (true) {
                    --var0;
                    if (var0 < 0) {
                        while (true) {
                            --var1;
                            if (var1 < 0) {
                                return;
                            }

                            method2842(graphicsPixels, var2, var6, 0, var4 >> 14, var3 >> 14);
                            var4 += var8;
                            var3 += var7;
                            var2 += graphicsPixelsWidth;
                        }
                    }

                    method2842(graphicsPixels, var2, var6, 0, var4 >> 14, var5 >> 14);
                    var4 += var8;
                    var5 += var9;
                    var2 += graphicsPixelsWidth;
                }
            } else {
                var1 -= var0;
                var0 -= var2;
                var2 = rasterClipY[var2];

                while (true) {
                    --var0;
                    if (var0 < 0) {
                        while (true) {
                            --var1;
                            if (var1 < 0) {
                                return;
                            }

                            method2842(graphicsPixels, var2, var6, 0, var3 >> 14, var4 >> 14);
                            var4 += var8;
                            var3 += var7;
                            var2 += graphicsPixelsWidth;
                        }
                    }

                    method2842(graphicsPixels, var2, var6, 0, var5 >> 14, var4 >> 14);
                    var4 += var8;
                    var5 += var9;
                    var2 += graphicsPixelsWidth;
                }
            }
        } else {
            var3 = var5 <<= 14;
            if (var2 < 0) {
                var3 -= var8 * var2;
                var5 -= var9 * var2;
                var2 = 0;
            }

            var4 <<= 14;
            if (var1 < 0) {
                var4 -= var7 * var1;
                var1 = 0;
            }

            if (var8 < var9) {
                var0 -= var1;
                var1 -= var2;
                var2 = rasterClipY[var2];

                while (true) {
                    --var1;
                    if (var1 < 0) {
                        while (true) {
                            --var0;
                            if (var0 < 0) {
                                return;
                            }

                            method2842(graphicsPixels, var2, var6, 0, var4 >> 14, var5 >> 14);
                            var4 += var7;
                            var5 += var9;
                            var2 += graphicsPixelsWidth;
                        }
                    }

                    method2842(graphicsPixels, var2, var6, 0, var3 >> 14, var5 >> 14);
                    var3 += var8;
                    var5 += var9;
                    var2 += graphicsPixelsWidth;
                }
            } else {
                var0 -= var1;
                var1 -= var2;
                var2 = rasterClipY[var2];

                while (true) {
                    --var1;
                    if (var1 < 0) {
                        while (true) {
                            --var0;
                            if (var0 < 0) {
                                return;
                            }

                            method2842(graphicsPixels, var2, var6, 0, var5 >> 14, var4 >> 14);
                            var4 += var7;
                            var5 += var9;
                            var2 += graphicsPixelsWidth;
                        }
                    }

                    method2842(graphicsPixels, var2, var6, 0, var5 >> 14, var3 >> 14);
                    var3 += var8;
                    var5 += var9;
                    var2 += graphicsPixelsWidth;
                }
            }
        }
    }
}

void Graphics3D::method2842(JavaArray<std::int32_t> var0, int var1, int var2, int var3, int var4, int var5) {
    if (rasterClipEnable) {
        if (var5 > rasterClipX) {
            var5 = rasterClipX;
        }

        if (var4 < 0) {
            var4 = 0;
        }
    }

    if (var4 < var5) {
        var1 += var4;
        var3 = var5 - var4 >> 2;
        if (rasterAlpha != 0) {
            if (rasterAlpha == 254) {
                while (true) {
                    --var3;
                    if (var3 < 0) {
                        var3 = var5 - var4 & 3;

                        while (true) {
                            --var3;
                            if (var3 < 0) {
                                return;
                            }

                            var0[var1] = var0[var1 + 1];
                            ++var1;
                        }
                    }

                    var0[var1] = var0[var1 + 1];
                    ++var1;
                    var0[var1] = var0[var1 + 1];
                    ++var1;
                    var0[var1] = var0[var1 + 1];
                    ++var1;
                    var0[var1] = var0[var1 + 1];
                    ++var1;
                }
            } else {
                int var6 = rasterAlpha;
                int var7 = 256 - rasterAlpha;
                var2 = (var7 * (var2 & 65280) >> 8 & 65280) + (var7 * (var2 & 16711935) >> 8 & 16711935);

                while (true) {
                    --var3;
                    int var8;
                    if (var3 < 0) {
                        var3 = var5 - var4 & 3;

                        while (true) {
                            --var3;
                            if (var3 < 0) {
                                return;
                            }

                            var8 = var0[var1];
                            var0[var1++] = ((var8 & 16711935) * var6 >> 8 & 16711935) + var2 + (var6 * (var8 & 65280) >> 8 & 65280);
                        }
                    }

                    var8 = var0[var1];
                    var0[var1++] = ((var8 & 16711935) * var6 >> 8 & 16711935) + var2 + (var6 * (var8 & 65280) >> 8 & 65280);
                    var8 = var0[var1];
                    var0[var1++] = ((var8 & 16711935) * var6 >> 8 & 16711935) + var2 + (var6 * (var8 & 65280) >> 8 & 65280);
                    var8 = var0[var1];
                    var0[var1++] = ((var8 & 16711935) * var6 >> 8 & 16711935) + var2 + (var6 * (var8 & 65280) >> 8 & 65280);
                    var8 = var0[var1];
                    var0[var1++] = ((var8 & 16711935) * var6 >> 8 & 16711935) + var2 + (var6 * (var8 & 65280) >> 8 & 65280);
                }
            }
        } else {
            while (true) {
                --var3;
                if (var3 < 0) {
                    var3 = var5 - var4 & 3;

                    while (true) {
                        --var3;
                        if (var3 < 0) {
                            return;
                        }

                        var0[var1++] = var2;
                    }
                }

                var0[var1++] = var2;
                var0[var1++] = var2;
                var0[var1++] = var2;
                var0[var1++] = var2;
            }
        }
    }
}

void Graphics3D::rasterTextureAffine(int var0, int var1, int var2, int var3, int var4, int var5, int var6, int var7, int var8, int var9, int var10, int var11, int var12, int var13, int var14, int var15, int var16, int var17, int var18) {
    const std::int32_t* var19 = textureProvider.load(var18);
    int var20;
    if (var19 == nullptr) {
        var20 = textureProvider.averageHsl(var18);
        rasterGouraud(var0, var1, var2, var3, var4, var5, method2794(var20, var6), method2794(var20, var7), method2794(var20, var8));
    } else {
        field1909 = textureProvider.opaque(var18);
        var20 = var4 - var3;
        int var21 = var1 - var0;
        int var22 = var5 - var3;
        int var23 = var2 - var0;
        int var24 = var7 - var6;
        int var25 = var8 - var6;
        int var26 = 0;
        if (var0 != var1) {
            var26 = jdiv(var4 - var3 << 14, var1 - var0);
        }

        int var27 = 0;
        if (var2 != var1) {
            var27 = jdiv(var5 - var4 << 14, var2 - var1);
        }

        int var28 = 0;
        if (var0 != var2) {
            var28 = jdiv(var3 - var5 << 14, var0 - var2);
        }

        int var29 = var20 * var23 - var22 * var21;
        if (var29 != 0) {
            int var30 = jdiv(var24 * var23 - var25 * var21 << 9, var29);
            int var31 = jdiv(var25 * var20 - var24 * var22 << 9, var29);
            var10 = var9 - var10;
            var13 = var12 - var13;
            var16 = var15 - var16;
            var11 -= var9;
            var14 -= var12;
            var17 -= var15;
            int var32 = var11 * var12 - var9 * var14 << 14;
            int var33 = static_cast<int>((static_cast<std::int64_t>(var15 * var14 - var17 * var12) << 3 << 14) / Rasterizer3D_zoom);
            int var34 = static_cast<int>((static_cast<std::int64_t>(var17 * var9 - var11 * var15) << 14) / Rasterizer3D_zoom);
            int var35 = var10 * var12 - var13 * var9 << 14;
            int var36 = static_cast<int>((static_cast<std::int64_t>(var13 * var15 - var16 * var12) << 3 << 14) / Rasterizer3D_zoom);
            int var37 = static_cast<int>((static_cast<std::int64_t>(var16 * var9 - var10 * var15) << 14) / Rasterizer3D_zoom);
            int var38 = var13 * var11 - var10 * var14 << 14;
            int var39 = static_cast<int>((static_cast<std::int64_t>(var16 * var14 - var13 * var17) << 3 << 14) / Rasterizer3D_zoom);
            int var40 = static_cast<int>((static_cast<std::int64_t>(var17 * var10 - var11 * var16) << 14) / Rasterizer3D_zoom);
            int var41;
            if (var0 <= var1 && var0 <= var2) {
                if (var0 < Rasterizer3D_clipHeight) {
                    if (var1 > Rasterizer3D_clipHeight) {
                        var1 = Rasterizer3D_clipHeight;
                    }

                    if (var2 > Rasterizer3D_clipHeight) {
                        var2 = Rasterizer3D_clipHeight;
                    }

                    var6 = var30 + ((var6 << 9) - var3 * var30);
                    if (var1 < var2) {
                        var5 = var3 <<= 14;
                        if (var0 < 0) {
                            var5 -= var0 * var28;
                            var3 -= var0 * var26;
                            var6 -= var0 * var31;
                            var0 = 0;
                        }

                        var4 <<= 14;
                        if (var1 < 0) {
                            var4 -= var27 * var1;
                            var1 = 0;
                        }

                        var41 = var0 - centerY;
                        var32 += var34 * var41;
                        var35 += var37 * var41;
                        var38 += var40 * var41;
                        if ((var0 == var1 || var28 >= var26) && (var0 != var1 || var28 <= var27)) {
                            var2 -= var1;
                            var1 -= var0;
                            var0 = rasterClipY[var0];

                            while (true) {
                                --var1;
                                if (var1 < 0) {
                                    while (true) {
                                        --var2;
                                        if (var2 < 0) {
                                            return;
                                        }

                                        method2791(graphicsPixels, var19, 0, 0, var0, var4 >> 14, var5 >> 14, var6, var30, var32, var35, var38, var33, var36, var39);
                                        var5 += var28;
                                        var4 += var27;
                                        var6 += var31;
                                        var0 += graphicsPixelsWidth;
                                        var32 += var34;
                                        var35 += var37;
                                        var38 += var40;
                                    }
                                }

                                method2791(graphicsPixels, var19, 0, 0, var0, var3 >> 14, var5 >> 14, var6, var30, var32, var35, var38, var33, var36, var39);
                                var5 += var28;
                                var3 += var26;
                                var6 += var31;
                                var0 += graphicsPixelsWidth;
                                var32 += var34;
                                var35 += var37;
                                var38 += var40;
                            }
                        } else {
                            var2 -= var1;
                            var1 -= var0;
                            var0 = rasterClipY[var0];

                            while (true) {
                                --var1;
                                if (var1 < 0) {
                                    while (true) {
                                        --var2;
                                        if (var2 < 0) {
                                            return;
                                        }

                                        method2791(graphicsPixels, var19, 0, 0, var0, var5 >> 14, var4 >> 14, var6, var30, var32, var35, var38, var33, var36, var39);
                                        var5 += var28;
                                        var4 += var27;
                                        var6 += var31;
                                        var0 += graphicsPixelsWidth;
                                        var32 += var34;
                                        var35 += var37;
                                        var38 += var40;
                                    }
                                }

                                method2791(graphicsPixels, var19, 0, 0, var0, var5 >> 14, var3 >> 14, var6, var30, var32, var35, var38, var33, var36, var39);
                                var5 += var28;
                                var3 += var26;
                                var6 += var31;
                                var0 += graphicsPixelsWidth;
                                var32 += var34;
                                var35 += var37;
                                var38 += var40;
                            }
                        }
                    } else {
                        var4 = var3 <<= 14;
                        if (var0 < 0) {
                            var4 -= var0 * var28;
                            var3 -= var0 * var26;
                            var6 -= var0 * var31;
                            var0 = 0;
                        }

                        var5 <<= 14;
                        if (var2 < 0) {
                            var5 -= var27 * var2;
                            var2 = 0;
                        }

                        var41 = var0 - centerY;
                        var32 += var34 * var41;
                        var35 += var37 * var41;
                        var38 += var40 * var41;
                        if (var0 != var2 && var28 < var26 || var0 == var2 && var27 > var26) {
                            var1 -= var2;
                            var2 -= var0;
                            var0 = rasterClipY[var0];

                            while (true) {
                                --var2;
                                if (var2 < 0) {
                                    while (true) {
                                        --var1;
                                        if (var1 < 0) {
                                            return;
                                        }

                                        method2791(graphicsPixels, var19, 0, 0, var0, var5 >> 14, var3 >> 14, var6, var30, var32, var35, var38, var33, var36, var39);
                                        var5 += var27;
                                        var3 += var26;
                                        var6 += var31;
                                        var0 += graphicsPixelsWidth;
                                        var32 += var34;
                                        var35 += var37;
                                        var38 += var40;
                                    }
                                }

                                method2791(graphicsPixels, var19, 0, 0, var0, var4 >> 14, var3 >> 14, var6, var30, var32, var35, var38, var33, var36, var39);
                                var4 += var28;
                                var3 += var26;
                                var6 += var31;
                                var0 += graphicsPixelsWidth;
                                var32 += var34;
                                var35 += var37;
                                var38 += var40;
                            }
                        } else {
                            var1 -= var2;
                            var2 -= var0;
                            var0 = rasterClipY[var0];

                            while (true) {
                                --var2;
                                if (var2 < 0) {
                                    while (true) {
                                        --var1;
                                        if (var1 < 0) {
                                            return;
                                        }

                                        method2791(graphicsPixels, var19, 0, 0, var0, var3 >> 14, var5 >> 14, var6, var30, var32, var35, var38, var33, var36, var39);
                                        var5 += var27;
                                        var3 += var26;
                                        var6 += var31;
                                        var0 += graphicsPixelsWidth;
                                        var32 += var34;
                                        var35 += var37;
                                        var38 += var40;
                                    }
                                }

                                method2791(graphicsPixels, var19, 0, 0, var0, var3 >> 14, var4 >> 14, var6, var30, var32, var35, var38, var33, var36, var39);
                                var4 += var28;
                                var3 += var26;
                                var6 += var31;
                                var0 += graphicsPixelsWidth;
                                var32 += var34;
                                var35 += var37;
                                var38 += var40;
                            }
                        }
                    }
                }
            } else if (var1 <= var2) {
                if (var1 < Rasterizer3D_clipHeight) {
                    if (var2 > Rasterizer3D_clipHeight) {
                        var2 = Rasterizer3D_clipHeight;
                    }

                    if (var0 > Rasterizer3D_clipHeight) {
                        var0 = Rasterizer3D_clipHeight;
                    }

                    var7 = var30 + ((var7 << 9) - var30 * var4);
                    if (var2 < var0) {
                        var3 = var4 <<= 14;
                        if (var1 < 0) {
                            var3 -= var26 * var1;
                            var4 -= var27 * var1;
                            var7 -= var31 * var1;
                            var1 = 0;
                        }

                        var5 <<= 14;
                        if (var2 < 0) {
                            var5 -= var28 * var2;
                            var2 = 0;
                        }

                        var41 = var1 - centerY;
                        var32 += var34 * var41;
                        var35 += var37 * var41;
                        var38 += var40 * var41;
                        if ((var2 == var1 || var26 >= var27) && (var2 != var1 || var26 <= var28)) {
                            var0 -= var2;
                            var2 -= var1;
                            var1 = rasterClipY[var1];

                            while (true) {
                                --var2;
                                if (var2 < 0) {
                                    while (true) {
                                        --var0;
                                        if (var0 < 0) {
                                            return;
                                        }

                                        method2791(graphicsPixels, var19, 0, 0, var1, var5 >> 14, var3 >> 14, var7, var30, var32, var35, var38, var33, var36, var39);
                                        var3 += var26;
                                        var5 += var28;
                                        var7 += var31;
                                        var1 += graphicsPixelsWidth;
                                        var32 += var34;
                                        var35 += var37;
                                        var38 += var40;
                                    }
                                }

                                method2791(graphicsPixels, var19, 0, 0, var1, var4 >> 14, var3 >> 14, var7, var30, var32, var35, var38, var33, var36, var39);
                                var3 += var26;
                                var4 += var27;
                                var7 += var31;
                                var1 += graphicsPixelsWidth;
                                var32 += var34;
                                var35 += var37;
                                var38 += var40;
                            }
                        } else {
                            var0 -= var2;
                            var2 -= var1;
                            var1 = rasterClipY[var1];

                            while (true) {
                                --var2;
                                if (var2 < 0) {
                                    while (true) {
                                        --var0;
                                        if (var0 < 0) {
                                            return;
                                        }

                                        method2791(graphicsPixels, var19, 0, 0, var1, var3 >> 14, var5 >> 14, var7, var30, var32, var35, var38, var33, var36, var39);
                                        var3 += var26;
                                        var5 += var28;
                                        var7 += var31;
                                        var1 += graphicsPixelsWidth;
                                        var32 += var34;
                                        var35 += var37;
                                        var38 += var40;
                                    }
                                }

                                method2791(graphicsPixels, var19, 0, 0, var1, var3 >> 14, var4 >> 14, var7, var30, var32, var35, var38, var33, var36, var39);
                                var3 += var26;
                                var4 += var27;
                                var7 += var31;
                                var1 += graphicsPixelsWidth;
                                var32 += var34;
                                var35 += var37;
                                var38 += var40;
                            }
                        }
                    } else {
                        var5 = var4 <<= 14;
                        if (var1 < 0) {
                            var5 -= var26 * var1;
                            var4 -= var27 * var1;
                            var7 -= var31 * var1;
                            var1 = 0;
                        }

                        var3 <<= 14;
                        if (var0 < 0) {
                            var3 -= var0 * var28;
                            var0 = 0;
                        }

                        var41 = var1 - centerY;
                        var32 += var34 * var41;
                        var35 += var37 * var41;
                        var38 += var40 * var41;
                        if (var26 < var27) {
                            var2 -= var0;
                            var0 -= var1;
                            var1 = rasterClipY[var1];

                            while (true) {
                                --var0;
                                if (var0 < 0) {
                                    while (true) {
                                        --var2;
                                        if (var2 < 0) {
                                            return;
                                        }

                                        method2791(graphicsPixels, var19, 0, 0, var1, var3 >> 14, var4 >> 14, var7, var30, var32, var35, var38, var33, var36, var39);
                                        var3 += var28;
                                        var4 += var27;
                                        var7 += var31;
                                        var1 += graphicsPixelsWidth;
                                        var32 += var34;
                                        var35 += var37;
                                        var38 += var40;
                                    }
                                }

                                method2791(graphicsPixels, var19, 0, 0, var1, var5 >> 14, var4 >> 14, var7, var30, var32, var35, var38, var33, var36, var39);
                                var5 += var26;
                                var4 += var27;
                                var7 += var31;
                                var1 += graphicsPixelsWidth;
                                var32 += var34;
                                var35 += var37;
                                var38 += var40;
                            }
                        } else {
                            var2 -= var0;
                            var0 -= var1;
                            var1 = rasterClipY[var1];

                            while (true) {
                                --var0;
                                if (var0 < 0) {
                                    while (true) {
                                        --var2;
                                        if (var2 < 0) {
                                            return;
                                        }

                                        method2791(graphicsPixels, var19, 0, 0, var1, var4 >> 14, var3 >> 14, var7, var30, var32, var35, var38, var33, var36, var39);
                                        var3 += var28;
                                        var4 += var27;
                                        var7 += var31;
                                        var1 += graphicsPixelsWidth;
                                        var32 += var34;
                                        var35 += var37;
                                        var38 += var40;
                                    }
                                }

                                method2791(graphicsPixels, var19, 0, 0, var1, var4 >> 14, var5 >> 14, var7, var30, var32, var35, var38, var33, var36, var39);
                                var5 += var26;
                                var4 += var27;
                                var7 += var31;
                                var1 += graphicsPixelsWidth;
                                var32 += var34;
                                var35 += var37;
                                var38 += var40;
                            }
                        }
                    }
                }
            } else if (var2 < Rasterizer3D_clipHeight) {
                if (var0 > Rasterizer3D_clipHeight) {
                    var0 = Rasterizer3D_clipHeight;
                }

                if (var1 > Rasterizer3D_clipHeight) {
                    var1 = Rasterizer3D_clipHeight;
                }

                var8 = (var8 << 9) - var5 * var30 + var30;
                if (var0 < var1) {
                    var4 = var5 <<= 14;
                    if (var2 < 0) {
                        var4 -= var27 * var2;
                        var5 -= var28 * var2;
                        var8 -= var31 * var2;
                        var2 = 0;
                    }

                    var3 <<= 14;
                    if (var0 < 0) {
                        var3 -= var0 * var26;
                        var0 = 0;
                    }

                    var41 = var2 - centerY;
                    var32 += var34 * var41;
                    var35 += var37 * var41;
                    var38 += var40 * var41;
                    if (var27 < var28) {
                        var1 -= var0;
                        var0 -= var2;
                        var2 = rasterClipY[var2];

                        while (true) {
                            --var0;
                            if (var0 < 0) {
                                while (true) {
                                    --var1;
                                    if (var1 < 0) {
                                        return;
                                    }

                                    method2791(graphicsPixels, var19, 0, 0, var2, var4 >> 14, var3 >> 14, var8, var30, var32, var35, var38, var33, var36, var39);
                                    var4 += var27;
                                    var3 += var26;
                                    var8 += var31;
                                    var2 += graphicsPixelsWidth;
                                    var32 += var34;
                                    var35 += var37;
                                    var38 += var40;
                                }
                            }

                            method2791(graphicsPixels, var19, 0, 0, var2, var4 >> 14, var5 >> 14, var8, var30, var32, var35, var38, var33, var36, var39);
                            var4 += var27;
                            var5 += var28;
                            var8 += var31;
                            var2 += graphicsPixelsWidth;
                            var32 += var34;
                            var35 += var37;
                            var38 += var40;
                        }
                    } else {
                        var1 -= var0;
                        var0 -= var2;
                        var2 = rasterClipY[var2];

                        while (true) {
                            --var0;
                            if (var0 < 0) {
                                while (true) {
                                    --var1;
                                    if (var1 < 0) {
                                        return;
                                    }

                                    method2791(graphicsPixels, var19, 0, 0, var2, var3 >> 14, var4 >> 14, var8, var30, var32, var35, var38, var33, var36, var39);
                                    var4 += var27;
                                    var3 += var26;
                                    var8 += var31;
                                    var2 += graphicsPixelsWidth;
                                    var32 += var34;
                                    var35 += var37;
                                    var38 += var40;
                                }
                            }

                            method2791(graphicsPixels, var19, 0, 0, var2, var5 >> 14, var4 >> 14, var8, var30, var32, var35, var38, var33, var36, var39);
                            var4 += var27;
                            var5 += var28;
                            var8 += var31;
                            var2 += graphicsPixelsWidth;
                            var32 += var34;
                            var35 += var37;
                            var38 += var40;
                        }
                    }
                } else {
                    var3 = var5 <<= 14;
                    if (var2 < 0) {
                        var3 -= var27 * var2;
                        var5 -= var28 * var2;
                        var8 -= var31 * var2;
                        var2 = 0;
                    }

                    var4 <<= 14;
                    if (var1 < 0) {
                        var4 -= var26 * var1;
                        var1 = 0;
                    }

                    var41 = var2 - centerY;
                    var32 += var34 * var41;
                    var35 += var37 * var41;
                    var38 += var40 * var41;
                    if (var27 < var28) {
                        var0 -= var1;
                        var1 -= var2;
                        var2 = rasterClipY[var2];

                        while (true) {
                            --var1;
                            if (var1 < 0) {
                                while (true) {
                                    --var0;
                                    if (var0 < 0) {
                                        return;
                                    }

                                    method2791(graphicsPixels, var19, 0, 0, var2, var4 >> 14, var5 >> 14, var8, var30, var32, var35, var38, var33, var36, var39);
                                    var4 += var26;
                                    var5 += var28;
                                    var8 += var31;
                                    var2 += graphicsPixelsWidth;
                                    var32 += var34;
                                    var35 += var37;
                                    var38 += var40;
                                }
                            }

                            method2791(graphicsPixels, var19, 0, 0, var2, var3 >> 14, var5 >> 14, var8, var30, var32, var35, var38, var33, var36, var39);
                            var3 += var27;
                            var5 += var28;
                            var8 += var31;
                            var2 += graphicsPixelsWidth;
                            var32 += var34;
                            var35 += var37;
                            var38 += var40;
                        }
                    } else {
                        var0 -= var1;
                        var1 -= var2;
                        var2 = rasterClipY[var2];

                        while (true) {
                            --var1;
                            if (var1 < 0) {
                                while (true) {
                                    --var0;
                                    if (var0 < 0) {
                                        return;
                                    }

                                    method2791(graphicsPixels, var19, 0, 0, var2, var5 >> 14, var4 >> 14, var8, var30, var32, var35, var38, var33, var36, var39);
                                    var4 += var26;
                                    var5 += var28;
                                    var8 += var31;
                                    var2 += graphicsPixelsWidth;
                                    var32 += var34;
                                    var35 += var37;
                                    var38 += var40;
                                }
                            }

                            method2791(graphicsPixels, var19, 0, 0, var2, var5 >> 14, var3 >> 14, var8, var30, var32, var35, var38, var33, var36, var39);
                            var3 += var27;
                            var5 += var28;
                            var8 += var31;
                            var2 += graphicsPixelsWidth;
                            var32 += var34;
                            var35 += var37;
                            var38 += var40;
                        }
                    }
                }
            }
        }
    }
}

void Graphics3D::method2791(JavaArray<std::int32_t> var0, const std::int32_t* var1, int var2, int var3, int var4, int var5, int var6, int var7, int var8, int var9, int var10, int var11, int var12, int var13, int var14) {
    if (rasterClipEnable) {
        if (var6 > rasterClipX) {
            var6 = rasterClipX;
        }

        if (var5 < 0) {
            var5 = 0;
        }
    }

    if (var5 < var6) {
        var4 += var5;
        var7 += var5 * var8;
        int var17 = var6 - var5;
        int var15;
        int var16;
        int var18;
        int var19;
        int var20;
        int var21;
        int var22;
        int var23;
        var23 = var5 - centerX;
        var9 += var23 * (var12 >> 3);
        var10 += (var13 >> 3) * var23;
        var11 += var23 * (var14 >> 3);
        var22 = var11 >> 14;
        if (var22 != 0) {
            var18 = jdiv(var9, var22);
            var19 = jdiv(var10, var22);
            if (var18 < 0) {
                var18 = 0;
            } else if (var18 > 16256) {
                var18 = 16256;
            }
        } else {
            var18 = 0;
            var19 = 0;
        }

        var9 += var12;
        var10 += var13;
        var11 += var14;
        var22 = var11 >> 14;
        if (var22 != 0) {
            var20 = jdiv(var9, var22);
            var21 = jdiv(var10, var22);
            if (var20 < 0) {
                var20 = 0;
            } else if (var20 > 16256) {
                var20 = 16256;
            }
        } else {
            var20 = 0;
            var21 = 0;
        }

        var2 = (var18 << 18) + var19;
        var16 = (var21 - var19 >> 3) + (var20 - var18 >> 3 << 18);
        var17 >>= 3;
        var8 <<= 3;
        var15 = var7 >> 8;
        if (field1909) {
            if (var17 > 0) {
                do {
                    var3 = var1[(var2 & 16256) + static_cast<int>(static_cast<std::uint32_t>(var2) >> 25)];
                    var0[var4++] = (var15 * (var3 & 65280) & 16711680) + ((var3 & 16711935) * var15 & -16711936) >> 8;
                    var2 += var16;
                    var3 = var1[(var2 & 16256) + static_cast<int>(static_cast<std::uint32_t>(var2) >> 25)];
                    var0[var4++] = (var15 * (var3 & 65280) & 16711680) + ((var3 & 16711935) * var15 & -16711936) >> 8;
                    var2 += var16;
                    var3 = var1[(var2 & 16256) + static_cast<int>(static_cast<std::uint32_t>(var2) >> 25)];
                    var0[var4++] = (var15 * (var3 & 65280) & 16711680) + ((var3 & 16711935) * var15 & -16711936) >> 8;
                    var2 += var16;
                    var3 = var1[(var2 & 16256) + static_cast<int>(static_cast<std::uint32_t>(var2) >> 25)];
                    var0[var4++] = (var15 * (var3 & 65280) & 16711680) + ((var3 & 16711935) * var15 & -16711936) >> 8;
                    var2 += var16;
                    var3 = var1[(var2 & 16256) + static_cast<int>(static_cast<std::uint32_t>(var2) >> 25)];
                    var0[var4++] = (var15 * (var3 & 65280) & 16711680) + ((var3 & 16711935) * var15 & -16711936) >> 8;
                    var2 += var16;
                    var3 = var1[(var2 & 16256) + static_cast<int>(static_cast<std::uint32_t>(var2) >> 25)];
                    var0[var4++] = (var15 * (var3 & 65280) & 16711680) + ((var3 & 16711935) * var15 & -16711936) >> 8;
                    var2 += var16;
                    var3 = var1[(var2 & 16256) + static_cast<int>(static_cast<std::uint32_t>(var2) >> 25)];
                    var0[var4++] = (var15 * (var3 & 65280) & 16711680) + ((var3 & 16711935) * var15 & -16711936) >> 8;
                    var2 += var16;
                    var3 = var1[(var2 & 16256) + static_cast<int>(static_cast<std::uint32_t>(var2) >> 25)];
                    var0[var4++] = (var15 * (var3 & 65280) & 16711680) + ((var3 & 16711935) * var15 & -16711936) >> 8;
                    var18 = var20;
                    var19 = var21;
                    var9 += var12;
                    var10 += var13;
                    var11 += var14;
                    var22 = var11 >> 14;
                    if (var22 != 0) {
                        var20 = jdiv(var9, var22);
                        var21 = jdiv(var10, var22);
                        if (var20 < 0) {
                            var20 = 0;
                        } else if (var20 > 16256) {
                            var20 = 16256;
                        }
                    } else {
                        var20 = 0;
                        var21 = 0;
                    }

                    var2 = (var18 << 18) + var19;
                    var16 = (var21 - var19 >> 3) + (var20 - var18 >> 3 << 18);
                    var7 += var8;
                    var15 = var7 >> 8;
                    --var17;
                } while (var17 > 0);
            }

            var17 = var6 - var5 & 7;
            if (var17 > 0) {
                do {
                    var3 = var1[(var2 & 16256) + static_cast<int>(static_cast<std::uint32_t>(var2) >> 25)];
                    var0[var4++] = (var15 * (var3 & 65280) & 16711680) + ((var3 & 16711935) * var15 & -16711936) >> 8;
                    var2 += var16;
                    --var17;
                } while (var17 > 0);
            }
        } else {
            if (var17 > 0) {
                do {
                    if ((var3 = var1[(var2 & 16256) + static_cast<int>(static_cast<std::uint32_t>(var2) >> 25)]) != 0) {
                        var0[var4] = (var15 * (var3 & 65280) & 16711680) + ((var3 & 16711935) * var15 & -16711936) >> 8;
                    }

                    ++var4;
                    var2 += var16;
                    if ((var3 = var1[(var2 & 16256) + static_cast<int>(static_cast<std::uint32_t>(var2) >> 25)]) != 0) {
                        var0[var4] = (var15 * (var3 & 65280) & 16711680) + ((var3 & 16711935) * var15 & -16711936) >> 8;
                    }

                    ++var4;
                    var2 += var16;
                    if ((var3 = var1[(var2 & 16256) + static_cast<int>(static_cast<std::uint32_t>(var2) >> 25)]) != 0) {
                        var0[var4] = (var15 * (var3 & 65280) & 16711680) + ((var3 & 16711935) * var15 & -16711936) >> 8;
                    }

                    ++var4;
                    var2 += var16;
                    if ((var3 = var1[(var2 & 16256) + static_cast<int>(static_cast<std::uint32_t>(var2) >> 25)]) != 0) {
                        var0[var4] = (var15 * (var3 & 65280) & 16711680) + ((var3 & 16711935) * var15 & -16711936) >> 8;
                    }

                    ++var4;
                    var2 += var16;
                    if ((var3 = var1[(var2 & 16256) + static_cast<int>(static_cast<std::uint32_t>(var2) >> 25)]) != 0) {
                        var0[var4] = (var15 * (var3 & 65280) & 16711680) + ((var3 & 16711935) * var15 & -16711936) >> 8;
                    }

                    ++var4;
                    var2 += var16;
                    if ((var3 = var1[(var2 & 16256) + static_cast<int>(static_cast<std::uint32_t>(var2) >> 25)]) != 0) {
                        var0[var4] = (var15 * (var3 & 65280) & 16711680) + ((var3 & 16711935) * var15 & -16711936) >> 8;
                    }

                    ++var4;
                    var2 += var16;
                    if ((var3 = var1[(var2 & 16256) + static_cast<int>(static_cast<std::uint32_t>(var2) >> 25)]) != 0) {
                        var0[var4] = (var15 * (var3 & 65280) & 16711680) + ((var3 & 16711935) * var15 & -16711936) >> 8;
                    }

                    ++var4;
                    var2 += var16;
                    if ((var3 = var1[(var2 & 16256) + static_cast<int>(static_cast<std::uint32_t>(var2) >> 25)]) != 0) {
                        var0[var4] = (var15 * (var3 & 65280) & 16711680) + ((var3 & 16711935) * var15 & -16711936) >> 8;
                    }

                    ++var4;
                    var18 = var20;
                    var19 = var21;
                    var9 += var12;
                    var10 += var13;
                    var11 += var14;
                    var22 = var11 >> 14;
                    if (var22 != 0) {
                        var20 = jdiv(var9, var22);
                        var21 = jdiv(var10, var22);
                        if (var20 < 0) {
                            var20 = 0;
                        } else if (var20 > 16256) {
                            var20 = 16256;
                        }
                    } else {
                        var20 = 0;
                        var21 = 0;
                    }

                    var2 = (var18 << 18) + var19;
                    var16 = (var21 - var19 >> 3) + (var20 - var18 >> 3 << 18);
                    var7 += var8;
                    var15 = var7 >> 8;
                    --var17;
                } while (var17 > 0);
            }

            var17 = var6 - var5 & 7;
            if (var17 > 0) {
                do {
                    if ((var3 = var1[(var2 & 16256) + static_cast<int>(static_cast<std::uint32_t>(var2) >> 25)]) != 0) {
                        var0[var4] = (var15 * (var3 & 65280) & 16711680) + ((var3 & 16711935) * var15 & -16711936) >> 8;
                    }

                    ++var4;
                    var2 += var16;
                    --var17;
                } while (var17 > 0);
            }
        }

    }
}

int Graphics3D::method2794(int var0, int var1) {
    var1 = (var0 & 127) * var1 >> 7;
    if (var1 < 2) {
        var1 = 2;
    } else if (var1 > 126) {
        var1 = 126;
    }

    return (var0 & 65408) + var1;
}

} // namespace osrs::cache
