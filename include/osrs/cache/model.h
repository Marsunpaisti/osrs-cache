#pragma once

// Models (index 7), decoded as RuneLite's cache library decodes them
// (net.runelite:cache 1.13.5, definitions/loaders/ModelLoader), in each of the
// four formats it reads: its types 3 and 2, which the current cache holds,
// and its type 1 and oldest format. The values ModelDefinition computes as it
// loads (its normals, texture coordinates and animation tables) are the
// functions below the struct, so that a model costs nothing for them until
// they are asked for. Coordinates are model units, 128 to a tile, with y
// pointing down. A per-vertex or per-face array is empty when the model has
// none.

#include <osrs/cache/store.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace osrs::cache {

struct Model {
    std::vector<std::int32_t> x;
    std::vector<std::int32_t> y;
    std::vector<std::int32_t> z;
    // The classic animation label of each vertex (RuneLite's
    // packedVertexGroups, which its computeAnimationTables turns into
    // vertexGroups).
    std::vector<std::uint8_t> labels;
    // Whether the model has labels, which `labels` can't say for a model of
    // no vertices.
    bool hasLabels = false;
    // Each vertex's skeletal bones and their weights out of 255 (animayaGroups
    // and animayaScales), as skin in osrs/cache/skeletal.h takes them.
    std::vector<std::vector<std::uint8_t>> bones;
    std::vector<std::vector<std::uint8_t>> weights;

    std::vector<std::uint16_t> faceA;
    std::vector<std::uint16_t> faceB;
    std::vector<std::uint16_t> faceC;
    // The client's 16-bit HSL; a textured face of the older formats has 127.
    std::vector<std::uint16_t> colours;
    std::vector<std::uint8_t> renderTypes;
    // Each face's priority; when empty, every face has `priority`.
    std::vector<std::uint8_t> priorities;
    std::uint8_t priority = 0;
    // 0 opaque, 255 invisible.
    std::vector<std::uint8_t> alphas;
    // Texture ids, -1 for an untextured face.
    std::vector<std::int16_t> textures;
    // The label each face's alpha transforms move (packedTransparencyVertexGroups).
    std::vector<std::uint8_t> alphaLabels;
    // Each face's depth offset (faceZOffsets), in types 3 and 2 only.
    std::vector<std::int8_t> depthOffsets;

    // How textures map onto faces: each texture face's render type and
    // vertices (set for render type 0 only), and the texture face each face
    // takes its coordinates from, -1 for its own vertices.
    std::vector<std::uint8_t> textureRenderTypes;
    std::vector<std::uint16_t> textureA;
    std::vector<std::uint16_t> textureB;
    std::vector<std::uint16_t> textureC;
    std::vector<std::int8_t> textureCoords;

    int vertexCount() const { return static_cast<int>(x.size()); }
    int faceCount() const { return static_cast<int>(faceA.size()); }
    int textureFaceCount() const { return static_cast<int>(textureRenderTypes.size()); }
};

// Nothing if the cache has no such model. Throws std::runtime_error if it
// doesn't decode, unless the store skips such objects (store.h). Decoding
// every model takes about 5 s in a Release build.
std::optional<Model> loadModel(const Store& store, int id);

// The normals RuneLite's ModelDefinition.computeNormals sums: each face's
// cross product scaled to a length of 256, added to its three vertices for a
// smooth-shaded face (render type 0) and kept as the face's own for a flat one
// (render type 1). A face of any other render type adds to neither.
struct VertexNormal {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    // How many faces were added.
    std::int32_t magnitude = 0;
};

struct FaceNormal {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
};

struct ModelNormals {
    std::vector<VertexNormal> vertices;
    // Per face, set for the flat faces only; empty when the model has none.
    std::vector<std::optional<FaceNormal>> faces;
};

// Throws std::runtime_error if a face names a vertex the model lacks, where
// RuneLite's loader fails on the model.
ModelNormals computeNormals(const Model& model);

// A textured face's texture coordinates at its three vertices.
struct FaceTextureCoordinates {
    std::array<float, 3> u{};
    std::array<float, 3> v{};
};

// Per face, as RuneLite's ModelDefinition.computeTextureUVCoordinates
// computes them: projected onto the face's texture face, or 0,0 1,0 0,1 when
// it has none. Nothing for an untextured face. Throws std::runtime_error if
// a face names a vertex or texture face the model lacks, where RuneLite's
// loader fails on the model.
std::vector<std::optional<FaceTextureCoordinates>> computeTextureCoordinates(const Model& model);

// The vertices with each label, ascending, from label 0 to the highest a
// vertex has (RuneLite's vertexGroups, which computeAnimationTables builds):
// one empty group for a model with labels but no vertices, and none for a
// model without labels.
std::vector<std::vector<int>> vertexGroups(const Model& model);

} // namespace osrs::cache
