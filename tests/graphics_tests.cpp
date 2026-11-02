// What RuneLite's ModelDefinition computes as a model loads, and the texture
// sizes TextureDefinition.method2680 makes, on the cache copy cache_tests.cpp
// reads. The oracle compares every value; these pin the cases a change would
// most easily break.

#include <doctest/doctest.h>

#include <osrs/cache/model.h>
#include <osrs/cache/sprites.h>
#include <osrs/cache/store.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <stdexcept>

using namespace osrs::cache;

namespace {

const Store* store() {
    static const std::unique_ptr<Store> opened = []() -> std::unique_ptr<Store> {
        std::error_code missing;
        if (!std::filesystem::exists(std::filesystem::path(OSRS_CACHE_DIR) / "main_file_cache.dat2", missing)) return nullptr;
        return std::make_unique<Store>(OSRS_CACHE_DIR);
    }();
    return opened.get();
}

} // namespace

TEST_CASE("a model with labels but no vertices has one empty vertex group, as RuneLite builds it") {
    if (store() == nullptr) return;
    const std::optional<Model> empty = loadModel(*store(), 7630);
    REQUIRE(empty.has_value());
    REQUIRE(empty->vertexCount() == 0);
    CHECK(vertexGroups(*empty) == std::vector<std::vector<int>>{{}});
}

TEST_CASE("a flat-shaded face keeps its normal to itself, and a smooth one adds its normal to its vertices") {
    if (store() == nullptr) return;
    // Model 20 has both.
    const std::optional<Model> model = loadModel(*store(), 20);
    REQUIRE(model.has_value());
    REQUIRE(model->renderTypes.size() == static_cast<std::size_t>(model->faceCount()));
    const ModelNormals normals = computeNormals(*model);
    REQUIRE(normals.faces.size() == static_cast<std::size_t>(model->faceCount()));
    int smooth = 0;
    for (int i = 0; i < model->faceCount(); ++i) {
        CAPTURE(i);
        CHECK(normals.faces[i].has_value() == (model->renderTypes[i] == 1));
        smooth += model->renderTypes[i] == 0 ? 1 : 0;
    }
    REQUIRE(smooth > 0);
    REQUIRE(smooth < model->faceCount());
    int added = 0;
    for (const VertexNormal& normal : normals.vertices) added += normal.magnitude;
    CHECK(added == 3 * smooth);
}

TEST_CASE("a texture's pixels at the sizes RuneLite makes from a 128-wide sprite") {
    if (store() == nullptr) return;
    const std::vector<TextureDefinition> textures = loadTextureDefinitions(*store());
    const auto found = std::find_if(textures.begin(), textures.end(), [](const TextureDefinition& t) { return t.id == 1; });
    REQUIRE(found != textures.end());
    // Its sprite, 448, is 128 by 128.
    const auto full = texturePixels(*store(), *found, 0.8, 128);
    const auto half = texturePixels(*store(), *found, 0.8, 64);
    REQUIRE(full.has_value());
    REQUIRE(half.has_value());
    REQUIRE(half->size() == 64 * 64);
    // Halving keeps each even texel of each even row.
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            CAPTURE(x);
            CAPTURE(y);
            CHECK((*half)[y * 64 + x] == (*full)[y * 2 * 128 + x * 2]);
        }
    }
    // Any other size of a sprite that isn't as wide fails, as in RuneLite.
    CHECK_THROWS_AS(texturePixels(*store(), *found, 0.8, 32), std::runtime_error);
}
