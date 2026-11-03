#pragma once

// The software rasterizer RuneLite's ItemSpriteFactory draws item icons with
// (net.runelite.cache.item: Graphics3D, Rasterizer2D, RSTextureProvider), and
// JagexColor's palette. The rasterizer keeps RuneLite's decompiled names
// (method2778, var0...), so that it can be compared with the source line by
// line, and its int arithmetic, which relies on two's-complement wrapping
// where a face projects far outside the icon: CMakeLists.txt builds it with
// -fwrapv where the compiler has one.

#include <osrs/cache/sprites.h>
#include <osrs/cache/store.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace osrs::cache {

// Java's int division: INT_MIN / -1 wraps where it traps in C++, and a zero
// divisor throws, as Java's ArithmeticException.
inline int jdiv(int a, int b) {
    if (b == 0) throw std::runtime_error("division by zero");
    if (b == -1) return static_cast<int>(0u - static_cast<std::uint32_t>(a));
    return a / b;
}

// An int array indexed as Java indexes one: an index outside it throws, as
// RuneLite's ArrayIndexOutOfBoundsException makes its icon fail.
template <class T>
class JavaArray {
public:
    JavaArray() = default;
    JavaArray(T* data, int size) : data_(data), size_(size) {}
    T& operator[](int i) const {
        if (i < 0 || i >= size_) throw std::runtime_error("index " + std::to_string(i) + " outside an array of " + std::to_string(size_));
        return data_[i];
    }

private:
    T* data_ = nullptr;
    int size_ = 0;
};

// The client's 16-bit HSL colours as RGB at a brightness (RuneLite
// JagexColor.createPalette).
std::vector<std::int32_t> hslPalette(double brightness);

// RuneLite RSTextureProvider: the textures by id, with their pixels made on
// first use, at the brightness and size ItemSpriteFactory sets.
class TextureCache {
public:
    explicit TextureCache(const Store& store);

    // Nothing for an id between textures; throws for one beyond them, as
    // RuneLite's array does.
    const std::int32_t* load(int id);
    int averageHsl(int id) const;
    bool opaque(int id) const;

private:
    const TextureDefinition& at(int id) const;

    const Store& store_;
    std::vector<std::optional<TextureDefinition>> textures_;
    std::vector<std::vector<std::int32_t>> pixels_;
};

// RuneLite Graphics3D, drawing into an icon's pixels as ItemSpriteFactory sets
// it up: clipped to the whole icon, centred on (16, 16), at zoom 512. Its
// low-resolution Gouraud spans and 64-texel textures aren't ported:
// ItemSpriteFactory turns rasterGouraudLowRes off before drawing and back on
// after, and RSTextureProvider's width is always 128 (vmethod3066).
class Graphics3D {
public:
    Graphics3D(TextureCache& textures, const std::vector<std::int32_t>& palette, std::int32_t* pixels, int width, int height);

    void rasterGouraud(int var0, int var1, int var2, int var3, int var4, int var5, int var6, int var7, int var8);
    void rasterFlat(int var0, int var1, int var2, int var3, int var4, int var5, int var6);
    void rasterTextureAffine(int var0, int var1, int var2, int var3, int var4, int var5, int var6, int var7, int var8, int var9,
                             int var10, int var11, int var12, int var13, int var14, int var15, int var16, int var17, int var18);

    bool rasterClipEnable = false;
    int rasterAlpha = 0;
    int Rasterizer3D_zoom = 512;
    int centerX = 0;
    int centerY = 0;
    int rasterClipX = 0;
    JavaArray<const std::int32_t> colorPalette;

private:
    void method2778(JavaArray<std::int32_t> var0, int var1, int var2, int var3, int var4, int var5, int var6, int var7);
    void method2842(JavaArray<std::int32_t> var0, int var1, int var2, int var3, int var4, int var5);
    void method2791(JavaArray<std::int32_t> var0, const std::int32_t* var1, int var2, int var3, int var4, int var5, int var6, int var7,
                    int var8, int var9, int var10, int var11, int var12, int var13, int var14);
    static int method2794(int var0, int var1);

    TextureCache& textureProvider;
    JavaArray<std::int32_t> graphicsPixels;
    int graphicsPixelsWidth = 0;
    int Rasterizer3D_clipHeight = 0;
    int rasterClipY[1024] = {};
    bool field1909 = false;
};

} // namespace osrs::cache
