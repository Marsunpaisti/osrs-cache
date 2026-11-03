#include <osrs/cache/item_icons.h>

#include <osrs/cache/definitions.h>
#include <osrs/cache/model.h>

#include "graphics3d.h"
#include "undecodable.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

namespace osrs::cache {

namespace {

constexpr int kWidth = 36;
constexpr int kHeight = 32;

// RuneLite item/Model's buffers: a model of more vertices or faces, more
// faces at one depth or of one priority, or depths beyond them fails.
constexpr int kMaxVertices = 6500;
constexpr int kDepths = 6000;
constexpr int kFacesPerDepth = 512;
constexpr int kFacesPerPriority = 2000;

[[noreturn]] void fail(const std::string& what) { throw std::runtime_error(what); }

// Java's (int) of a double: NaN is 0 and the rest saturates, where C++'s
// conversion is undefined.
int javaInt(double value) {
    if (std::isnan(value)) return 0;
    if (value >= 2147483647.0) return std::numeric_limits<int>::max();
    if (value <= -2147483648.0) return std::numeric_limits<int>::min();
    return static_cast<int>(value);
}

// RuneLite Graphics3D.SINE and COSINE: 2048 steps to the circle, times 65536.
struct Trigonometry {
    std::array<int, 2048> sine{};
    std::array<int, 2048> cosine{};
};

const Trigonometry& trigonometry() {
    static const Trigonometry table = [] {
        Trigonometry t;
        for (int i = 0; i < 2048; ++i) {
            t.sine[i] = static_cast<int>(65536.0 * std::sin(static_cast<double>(i) * (std::numbers::pi / 1024.0)));
            t.cosine[i] = static_cast<int>(65536.0 * std::cos(static_cast<double>(i) * (std::numbers::pi / 1024.0)));
        }
        return t;
    }();
    return table;
}

int sine(int angle) {
    if (angle < 0 || angle >= 2048) fail("angle " + std::to_string(angle) + " is beyond the sine table");
    return trigonometry().sine[angle];
}

int cosine(int angle) {
    if (angle < 0 || angle >= 2048) fail("angle " + std::to_string(angle) + " is beyond the sine table");
    return trigonometry().cosine[angle];
}

int bound2to126(int value) { return value < 2 ? 2 : value > 126 ? 126 : value; }

// RuneLite ItemSpriteFactory.method2608: an HSL colour's lightness scaled by a
// light level.
int method2608(int hsl, int light) { return (hsl & 65408) + bound2to126((hsl & 127) * light >> 7); }

// RuneLite item/Model: a lit model, which shares its vertices, faces and
// textures with the ModelDefinition it was lit from.
struct LitModel {
    const Model* def = nullptr;
    std::vector<int> faceColors1;
    std::vector<int> faceColors2;
    std::vector<int> faceColors3;
    // Each face's texture face among those below, -1 for its own vertices;
    // empty for none, as RuneLite's null.
    std::vector<std::int8_t> textureCoords;
    std::vector<int> texIndices1;
    std::vector<int> texIndices2;
    std::vector<int> texIndices3;
    // calculateBoundsCylinder's.
    int modelHeight = 0;
    int bottomY = 0;
    int XYZMag = 0;
    int radius = 0;
    int diameter = 0;
};

// RuneLite ItemSpriteFactory.light.
LitModel light(const Model& def, int ambient, int contrast, int x, int y, int z) {
    const ModelNormals normals = computeNormals(def);
    const int somethingMagnitude = static_cast<int>(std::sqrt(static_cast<double>(z * z + x * x + y * y)));
    const int var7 = somethingMagnitude * contrast >> 8;
    const int faceCount = def.faceCount();
    LitModel lit;
    lit.def = &def;
    lit.faceColors1.resize(faceCount);
    lit.faceColors2.resize(faceCount);
    lit.faceColors3.resize(faceCount);

    // Only the texture faces a face uses, of render type 0, are kept.
    const int textureFaces = def.textureFaceCount();
    if (textureFaces > 0 && !def.textureCoords.empty()) {
        std::vector<int> var9(textureFaces);
        for (int i = 0; i < faceCount; ++i) {
            if (def.textureCoords[i] == -1) continue;
            const int textureFace = static_cast<std::uint8_t>(def.textureCoords[i]);
            if (textureFace >= textureFaces) fail("face names a texture face beyond the model's");
            ++var9[textureFace];
        }
        for (int i = 0; i < textureFaces; ++i) {
            if (var9[i] > 0 && def.textureRenderTypes[i] == 0) {
                var9[i] = static_cast<int>(lit.texIndices1.size());
                lit.texIndices1.push_back(def.textureA[i]);
                lit.texIndices2.push_back(def.textureB[i]);
                lit.texIndices3.push_back(def.textureC[i]);
            } else {
                var9[i] = -1;
            }
        }
        lit.textureCoords.resize(faceCount);
        for (int i = 0; i < faceCount; ++i) {
            lit.textureCoords[i] = def.textureCoords[i] == -1
                ? std::int8_t{-1}
                : static_cast<std::int8_t>(var9[static_cast<std::uint8_t>(def.textureCoords[i])]);
        }
    }

    // A vertex's light: its normal against the light, over the faces it
    // sums, as a lightness.
    const auto vertexLight = [&](int vertex) {
        const VertexNormal& n = normals.vertices[vertex];
        return jdiv(y * n.y + z * n.z + x * n.x, var7 * n.magnitude) + ambient;
    };
    const auto faceLight = [&](int face) {
        const FaceNormal& n = *normals.faces[face];
        return jdiv(y * n.y + z * n.z + x * n.x, var7 / 2 + var7) + ambient;
    };
    for (int face = 0; face < faceCount; ++face) {
        int faceType = def.renderTypes.empty() ? 0 : static_cast<std::int8_t>(def.renderTypes[face]);
        const int faceAlpha = def.alphas.empty() ? 0 : static_cast<std::int8_t>(def.alphas[face]);
        const int faceTexture = def.textures.empty() ? -1 : def.textures[face];
        // Alpha 254 draws a face in colour 128 untextured, 255 hides it.
        if (faceAlpha == -2) faceType = 3;
        if (faceAlpha == -1) faceType = 2;

        if (faceTexture == -1) {
            if (faceType == 0) {
                const int hsl = def.colours[face];
                lit.faceColors1[face] = method2608(hsl, vertexLight(def.faceA[face]));
                lit.faceColors2[face] = method2608(hsl, vertexLight(def.faceB[face]));
                lit.faceColors3[face] = method2608(hsl, vertexLight(def.faceC[face]));
            } else if (faceType == 1) {
                lit.faceColors1[face] = method2608(def.colours[face], faceLight(face));
                lit.faceColors3[face] = -1;
            } else if (faceType == 3) {
                lit.faceColors1[face] = 128;
                lit.faceColors3[face] = -1;
            } else {
                lit.faceColors3[face] = -2;
            }
        } else if (faceType == 0) {
            lit.faceColors1[face] = bound2to126(vertexLight(def.faceA[face]));
            lit.faceColors2[face] = bound2to126(vertexLight(def.faceB[face]));
            lit.faceColors3[face] = bound2to126(vertexLight(def.faceC[face]));
        } else if (faceType == 1) {
            lit.faceColors1[face] = bound2to126(faceLight(face));
            lit.faceColors3[face] = -1;
        } else {
            lit.faceColors3[face] = -2;
        }
    }
    return lit;
}

// RuneLite item/Model.calculateBoundsCylinder.
void calculateBoundsCylinder(LitModel& model) {
    const Model& def = *model.def;
    model.modelHeight = 0;
    model.bottomY = 0;
    model.XYZMag = 0;
    for (int i = 0; i < def.vertexCount(); ++i) {
        const int x = def.x[i], y = def.y[i], z = def.z[i];
        if (-y > model.modelHeight) model.modelHeight = -y;
        if (y > model.bottomY) model.bottomY = y;
        model.XYZMag = std::max(model.XYZMag, x * x + z * z);
    }
    model.XYZMag = javaInt(std::sqrt(static_cast<double>(model.XYZMag)) + 0.99);
    model.radius = javaInt(std::sqrt(static_cast<double>(model.XYZMag * model.XYZMag + model.modelHeight * model.modelHeight)) + 0.99);
    model.diameter = model.radius + javaInt(std::sqrt(static_cast<double>(model.XYZMag * model.XYZMag + model.bottomY * model.bottomY)) + 0.99);
}

// RuneLite item/Model's buffers, kept between icons. Each icon zeroes what it
// reads, as RuneLite makes them anew for each.
struct Scratch {
    std::vector<int> modelViewportXs = std::vector<int>(kMaxVertices);
    std::vector<int> modelViewportYs = std::vector<int>(kMaxVertices);
    std::vector<int> modelViewportZs = std::vector<int>(kMaxVertices);
    std::vector<int> modelLocalX = std::vector<int>(kMaxVertices);
    std::vector<int> modelLocalY = std::vector<int>(kMaxVertices);
    std::vector<int> modelLocalZ = std::vector<int>(kMaxVertices);
    std::vector<bool> faceClipped = std::vector<bool>(kMaxVertices);
    std::vector<std::vector<int>> facesByDistance = std::vector<std::vector<int>>(kDepths);
    std::array<int, 12> numOfPriority{};
    std::array<std::array<int, kFacesPerPriority>, 12> orderedFaces{};
    std::array<int, kFacesPerPriority> eq10{};
    std::array<int, kFacesPerPriority> eq11{};
    std::array<int, 12> lt10{};
};

// RuneLite item/Model.rasterFace.
void rasterFace(const LitModel& model, Scratch& s, Graphics3D& graphics, int face) {
    const Model& def = *model.def;
    const int var2 = def.faceA[face];
    const int var3 = def.faceB[face];
    const int var4 = def.faceC[face];
    graphics.rasterClipEnable = s.faceClipped[face];
    graphics.rasterAlpha = def.alphas.empty() ? 0 : def.alphas[face];
    const std::vector<int>& xs = s.modelViewportXs;
    const std::vector<int>& ys = s.modelViewportYs;

    if (!def.textures.empty() && def.textures[face] != -1) {
        int var5 = var2, var6 = var3, var7 = var4;
        if (!model.textureCoords.empty() && model.textureCoords[face] != -1) {
            const int var8 = static_cast<std::uint8_t>(model.textureCoords[face]);
            var5 = model.texIndices1[var8];
            var6 = model.texIndices2[var8];
            var7 = model.texIndices3[var8];
        }
        // Java's arrays of 6500 read 0 past the model's vertices.
        const JavaArray<int> localX(s.modelLocalX.data(), kMaxVertices);
        const JavaArray<int> localY(s.modelLocalY.data(), kMaxVertices);
        const JavaArray<int> localZ(s.modelLocalZ.data(), kMaxVertices);
        const bool flat = model.faceColors3[face] == -1;
        graphics.rasterTextureAffine(xs[var2], xs[var3], xs[var4], ys[var2], ys[var3], ys[var4], model.faceColors1[face],
                                     flat ? model.faceColors1[face] : model.faceColors2[face],
                                     flat ? model.faceColors1[face] : model.faceColors3[face], localX[var5], localX[var6], localX[var7],
                                     localY[var5], localY[var6], localY[var7], localZ[var5], localZ[var6], localZ[var7], def.textures[face]);
    } else if (model.faceColors3[face] == -1) {
        graphics.rasterFlat(xs[var2], xs[var3], xs[var4], ys[var2], ys[var3], ys[var4], graphics.colorPalette[model.faceColors1[face]]);
    } else {
        graphics.rasterGouraud(xs[var2], xs[var3], xs[var4], ys[var2], ys[var3], ys[var4], model.faceColors1[face], model.faceColors2[face],
                               model.faceColors3[face]);
    }
}

// RuneLite item/Model.draw: the faces facing the camera, far to near, and
// with priorities, in their order with the priority 10 and 11 faces
// interleaved by depth.
void draw(const LitModel& model, Scratch& s, Graphics3D& graphics) {
    if (model.diameter >= kDepths) return;
    const Model& def = *model.def;
    const std::vector<int>& xs = s.modelViewportXs;
    const std::vector<int>& ys = s.modelViewportYs;
    const std::vector<int>& zs = s.modelViewportZs;
    for (int face = 0; face < def.faceCount(); ++face) {
        if (model.faceColors3[face] == -2) continue;
        const int var7 = def.faceA[face];
        const int var27 = def.faceB[face];
        const int var9 = def.faceC[face];
        const int var10 = ys[var7];
        const int var11 = ys[var27];
        const int var12 = ys[var9];
        if ((var10 - var11) * (xs[var9] - xs[var27]) - (var12 - var11) * (xs[var7] - xs[var27]) <= 0) continue;
        if (face >= kMaxVertices) fail("face " + std::to_string(face) + " is beyond RuneLite's buffers");
        s.faceClipped[face] = !(var10 >= 0 && var11 >= 0 && var12 >= 0 && var10 <= graphics.rasterClipX && var11 <= graphics.rasterClipX
                                && var12 <= graphics.rasterClipX);
        const int var13 = (zs[var7] + zs[var27] + zs[var9]) / 3 + model.radius;
        if (var13 < 0 || var13 >= kDepths) fail("a face's depth is beyond RuneLite's buffers");
        // projectAndDraw starts depth 0's count at -1, which draw resets
        // only for depths within the diameter.
        if (var13 == 0 && model.diameter == 0) fail("a face's depth is beyond RuneLite's buffers");
        std::vector<int>& bucket = s.facesByDistance[var13];
        if (bucket.size() >= static_cast<std::size_t>(kFacesPerDepth)) fail("more faces at one depth than RuneLite's buffers hold");
        bucket.push_back(face);
    }

    if (def.priorities.empty()) {
        for (int depth = model.diameter - 1; depth >= 0; --depth) {
            for (const int face : s.facesByDistance[depth]) rasterFace(model, s, graphics, face);
        }
        return;
    }

    s.numOfPriority.fill(0);
    s.lt10.fill(0);
    for (int var26 = model.diameter - 1; var26 >= 0; --var26) {
        for (const int var10 : s.facesByDistance[var26]) {
            const int var31 = static_cast<std::int8_t>(def.priorities[var10]);
            if (var31 < 0 || var31 >= 12) fail("face priority " + std::to_string(var31) + " is beyond RuneLite's buffers");
            const int var12 = s.numOfPriority[var31]++;
            if (var12 >= kFacesPerPriority) fail("more faces of one priority than RuneLite's buffers hold");
            s.orderedFaces[var31][var12] = var10;
            if (var31 < 10) {
                s.lt10[var31] += var26;
            } else if (var31 == 10) {
                s.eq10[var12] = var26;
            } else {
                s.eq11[var12] = var26;
            }
        }
    }

    // The mean depths of priorities 1 and 2, 3 and 4, and 6 and 8, before
    // which the priority 10 and 11 faces deeper than each are drawn.
    int var26 = 0;
    if (s.numOfPriority[1] > 0 || s.numOfPriority[2] > 0) {
        var26 = (s.lt10[1] + s.lt10[2]) / (s.numOfPriority[1] + s.numOfPriority[2]);
    }
    int var7 = 0;
    if (s.numOfPriority[3] > 0 || s.numOfPriority[4] > 0) {
        var7 = (s.lt10[3] + s.lt10[4]) / (s.numOfPriority[3] + s.numOfPriority[4]);
    }
    int var27 = 0;
    if (s.numOfPriority[6] > 0 || s.numOfPriority[8] > 0) {
        var27 = (s.lt10[8] + s.lt10[6]) / (s.numOfPriority[8] + s.numOfPriority[6]);
    }

    int var10 = 0;
    int var11 = s.numOfPriority[10];
    const int* var28 = s.orderedFaces[10].data();
    const int* var29 = s.eq10.data();
    if (var10 == var11) {
        var10 = 0;
        var11 = s.numOfPriority[11];
        var28 = s.orderedFaces[11].data();
        var29 = s.eq11.data();
    }
    int var9 = var10 < var11 ? var29[var10] : -1000;
    // Draws the next priority 10 or 11 face, moving on to 11 after 10.
    const auto drawNext = [&] {
        rasterFace(model, s, graphics, var28[var10++]);
        if (var10 == var11 && var28 != s.orderedFaces[11].data()) {
            var10 = 0;
            var11 = s.numOfPriority[11];
            var28 = s.orderedFaces[11].data();
            var29 = s.eq11.data();
        }
        var9 = var10 < var11 ? var29[var10] : -1000;
    };
    for (int var14 = 0; var14 < 10; ++var14) {
        while (var14 == 0 && var9 > var26) drawNext();
        while (var14 == 3 && var9 > var7) drawNext();
        while (var14 == 5 && var9 > var27) drawNext();
        for (int i = 0; i < s.numOfPriority[var14]; ++i) rasterFace(model, s, graphics, s.orderedFaces[var14][i]);
    }
    while (var9 != -1000) drawNext();
}

// RuneLite item/Model.projectAndDraw, with its yzRotation of 0 as
// ItemSpriteFactory passes it.
void projectAndDraw(const LitModel& model, Scratch& s, Graphics3D& graphics, int xzRotation, int xyRotation, int orientation, int xOffset,
                    int yOffset, int zOffset) {
    const Model& def = *model.def;
    if (def.vertexCount() > kMaxVertices) fail("more vertices than RuneLite's buffers hold");
    for (std::vector<int>* buffer : {&s.modelViewportXs, &s.modelViewportYs, &s.modelViewportZs, &s.modelLocalX, &s.modelLocalY, &s.modelLocalZ}) {
        std::fill(buffer->begin(), buffer->end(), 0);
    }
    std::fill(s.faceClipped.begin(), s.faceClipped.end(), false);
    for (std::vector<int>& bucket : s.facesByDistance) bucket.clear();

    const int sinX = sine(orientation);
    const int cosX = cosine(orientation);
    const int zRelatedVariable = sinX * yOffset + cosX * zOffset >> 16;
    for (int i = 0; i < def.vertexCount(); ++i) {
        int x = def.x[i];
        int y = def.y[i];
        int z = def.z[i];
        if (xyRotation != 0) {
            const int sinZ = sine(xyRotation);
            const int cosZ = cosine(xyRotation);
            const int tmp = y * sinZ + x * cosZ >> 16;
            y = y * cosZ - x * sinZ >> 16;
            x = tmp;
        }
        if (xzRotation != 0) {
            const int sinY = sine(xzRotation);
            const int cosY = cosine(xzRotation);
            const int tmp = z * sinY + x * cosY >> 16;
            z = z * cosY - x * sinY >> 16;
            x = tmp;
        }
        x += xOffset;
        y += yOffset;
        z += zOffset;
        const int tmp = y * cosX - z * sinX >> 16;
        z = y * sinX + z * cosX >> 16;
        s.modelViewportZs[i] = z - zRelatedVariable;
        s.modelViewportYs[i] = jdiv(x * graphics.Rasterizer3D_zoom, z) + graphics.centerX;
        s.modelViewportXs[i] = jdiv(tmp * graphics.Rasterizer3D_zoom, z) + graphics.centerY;
        if (!def.textures.empty()) {
            s.modelLocalX[i] = x;
            s.modelLocalY[i] = tmp;
            s.modelLocalZ[i] = z;
        }
    }
    draw(model, s, graphics);
}

// RuneLite SpritePixels.drawBorder: each empty pixel beside a drawn one, west,
// north, east or south, takes `colour`.
void drawBorder(std::vector<std::int32_t>& pixels, int colour) {
    std::vector<std::int32_t> bordered(pixels.size());
    for (int y = 0, i = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x, ++i) {
            int pixel = pixels[i];
            if (pixel == 0
                && ((x > 0 && pixels[i - 1] != 0) || (y > 0 && pixels[i - kWidth] != 0) || (x < kWidth - 1 && pixels[i + 1] != 0)
                    || (y < kHeight - 1 && pixels[i + kWidth] != 0))) {
                pixel = colour;
            }
            bordered[i] = pixel;
        }
    }
    pixels = std::move(bordered);
}

// RuneLite SpritePixels.drawShadow: each empty pixel with a drawn one to its
// north-west takes `colour`, bottom row first so that the shadow doesn't
// cast itself.
void drawShadow(std::vector<std::int32_t>& pixels, int colour) {
    for (int y = kHeight - 1; y > 0; --y) {
        const int row = y * kWidth;
        for (int x = kWidth - 1; x > 0; --x) {
            if (pixels[x + row] == 0 && pixels[x + row - 1 - kWidth] != 0) pixels[x + row] = colour;
        }
    }
}

// RuneLite SpritePixels.drawAtOn at 0, 0, for a sprite the size of the icon
// with no offset, which leaves nothing to clip: its drawn pixels over the icon's.
void drawAtOn(const std::vector<std::int32_t>& sprite, std::vector<std::int32_t>& pixels) {
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        if (sprite[i] != 0) pixels[i] = sprite[i];
    }
}

} // namespace

struct ItemIconRenderer::State {
    explicit State(const Store& s) : store(s), items(loadItemDefinitions(s)), textures(s), palette(hslPalette(0.6)) {}

    const ItemDefinition* item(int id) const {
        const auto found = std::lower_bound(items.begin(), items.end(), id, [](const ItemDefinition& item, int key) { return item.id < key; });
        return found != items.end() && found->id == id ? &*found : nullptr;
    }

    std::optional<std::vector<std::int32_t>> createSpritePixels(int itemId, int quantity, int border, int shadowColor, bool noted, int depth);

    const Store& store;
    std::vector<ItemDefinition> items;
    TextureCache textures;
    // At JagexColor.BRIGHTNESS_MAX, which ItemSpriteFactory sets.
    std::vector<std::int32_t> palette;
    Scratch scratch;
};

// RuneLite ItemSpriteFactory.createSpritePixels and getModel. Where an item it
// needs is missing, RuneLite throws a NullPointerException; this returns
// nothing, as for a missing model. Items drawn from each other in a loop
// throw, where RuneLite's stack overflows.
std::optional<std::vector<std::int32_t>> ItemIconRenderer::State::createSpritePixels(int itemId, int quantity, int border, int shadowColor,
                                                                                     bool noted, int depth) {
    if (depth > 8) fail("items are drawn from each other in a loop");
    const ItemDefinition* item = this->item(itemId);
    if (item == nullptr) return std::nullopt;
    if (quantity > 1 && !item->countItems.empty()) {
        int stackItemID = -1;
        for (int i = 0; i < 10; ++i) {
            if (quantity >= item->countSizes[i] && item->countSizes[i] != 0) stackItemID = item->countItems[i];
        }
        if (stackItemID != -1) {
            item = this->item(stackItemID);
            if (item == nullptr) return std::nullopt;
        }
    }

    std::optional<Model> model = loadModel(store, item->inventoryModel);
    if (!model) return std::nullopt;
    // RuneLite's ModelLoader computes these as it loads, failing on a face
    // that names a vertex or texture face the model lacks.
    computeTextureCoordinates(*model);
    if (item->resizeX != 128 || item->resizeY != 128 || item->resizeZ != 128) {
        for (int i = 0; i < model->vertexCount(); ++i) {
            model->x[i] = model->x[i] * item->resizeX / 128;
            model->y[i] = item->resizeY * model->y[i] / 128;
            model->z[i] = item->resizeZ * model->z[i] / 128;
        }
    }
    for (std::size_t i = 0; i < item->recolours.find.size(); ++i) {
        std::replace(model->colours.begin(), model->colours.end(), item->recolours.find[i], item->recolours.replace[i]);
    }
    for (std::size_t i = 0; i < item->retextures.find.size(); ++i) {
        std::replace(model->textures.begin(), model->textures.end(), static_cast<std::int16_t>(item->retextures.find[i]),
                     static_cast<std::int16_t>(item->retextures.replace[i]));
    }
    LitModel lit = light(*model, item->ambient + 64, item->contrast + 768, -50, -10, -50);

    std::optional<std::vector<std::int32_t>> aux;
    if (item->notedTemplate) {
        aux = createSpritePixels(item->notedId.value_or(-1), 10, 1, 0, true, depth + 1);
        if (!aux) return std::nullopt;
    } else if (item->boughtTemplate) {
        aux = createSpritePixels(item->boughtId.value_or(-1), quantity, border, 0, false, depth + 1);
        if (!aux) return std::nullopt;
    } else if (item->placeholderTemplate) {
        aux = createSpritePixels(item->placeholderId.value_or(-1), quantity, 0, 0, false, depth + 1);
        if (!aux) return std::nullopt;
    }

    std::vector<std::int32_t> pixels(static_cast<std::size_t>(kWidth) * kHeight);
    Graphics3D graphics(textures, palette, pixels.data(), kWidth, kHeight);
    if (item->placeholderTemplate) drawAtOn(*aux, pixels);

    int zoom2d = item->zoom2d;
    if (noted) {
        zoom2d = static_cast<int>(static_cast<double>(zoom2d) * 1.5);
    } else if (border == 2) {
        zoom2d = static_cast<int>(static_cast<double>(zoom2d) * 1.04);
    }
    const int var17 = zoom2d * sine(item->xan2d) >> 16;
    const int var18 = zoom2d * cosine(item->xan2d) >> 16;

    calculateBoundsCylinder(lit);
    projectAndDraw(lit, scratch, graphics, item->yan2d, item->zan2d, item->xan2d, item->xOffset2d, lit.modelHeight / 2 + var17 + item->yOffset2d,
                   var18 + item->yOffset2d);
    if (item->boughtTemplate) drawAtOn(*aux, pixels);

    if (border >= 1) drawBorder(pixels, 1);
    if (border >= 2) drawBorder(pixels, 0xffffff);
    if (shadowColor != 0) drawShadow(pixels, shadowColor);
    if (item->notedTemplate) drawAtOn(*aux, pixels);
    return pixels;
}

ItemIconRenderer::ItemIconRenderer(const Store& store) : state_(std::make_unique<State>(store)) {}

ItemIconRenderer::~ItemIconRenderer() = default;

std::optional<Sprite> ItemIconRenderer::render(int itemId, int quantity, int border, std::uint32_t shadowColour, bool noted) {
    const auto pixels = decodeOrSkip(state_->store, "item icon", itemId, [&] {
        return state_->createSpritePixels(itemId, quantity, border, static_cast<int>(shadowColour), noted, 0);
    });
    if (!pixels || !*pixels) return std::nullopt;
    Sprite sprite;
    sprite.width = sprite.maxWidth = kWidth;
    sprite.height = sprite.maxHeight = kHeight;
    // RuneLite SpritePixels.toBufferedImage: every drawn pixel opaque.
    sprite.pixels.reserve((*pixels)->size());
    for (const std::int32_t pixel : **pixels) {
        sprite.pixels.push_back(pixel == 0 ? 0u : static_cast<std::uint32_t>(pixel) | 0xFF000000u);
    }
    return sprite;
}

std::optional<Sprite> loadItemIcon(const Store& store, int itemId, int quantity, int border, std::uint32_t shadowColour, bool noted) {
    return ItemIconRenderer(store).render(itemId, quantity, border, shadowColour, noted);
}

} // namespace osrs::cache
