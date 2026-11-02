#include <osrs/cache/model.h>

#include "buffer.h"
#include "undecodable.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace osrs::cache {

namespace {

// ModelLoader reads each part of a model through its own stream, set to the
// part's offset.
Buffer at(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset > data.size()) throw std::runtime_error("a part starts past the model's end");
    return Buffer(data.subspan(offset));
}

// Vertices as deltas from the previous one, each axis present when its flag
// bit is set; then each vertex's label, if the model has them.
void readVertices(Model& model, Buffer& flags, Buffer& xs, Buffer& ys, Buffer& zs, Buffer& labels) {
    int x = 0, y = 0, z = 0;
    for (int i = 0; i < model.vertexCount(); ++i) {
        const int f = flags.u8();
        x += (f & 1) != 0 ? xs.shortSmart() : 0;
        y += (f & 2) != 0 ? ys.shortSmart() : 0;
        z += (f & 4) != 0 ? zs.shortSmart() : 0;
        model.x[i] = x;
        model.y[i] = y;
        model.z[i] = z;
        if (!model.labels.empty()) model.labels[i] = static_cast<std::uint8_t>(labels.u8());
    }
}

// Each vertex's bones and weights, read on from the labels.
void readBones(Model& model, Buffer& in) {
    for (int i = 0; i < model.vertexCount(); ++i) {
        const int count = in.u8();
        model.bones[i].resize(count);
        model.weights[i].resize(count);
        for (int k = 0; k < count; ++k) {
            model.bones[i][k] = static_cast<std::uint8_t>(in.u8());
            model.weights[i][k] = static_cast<std::uint8_t>(in.u8());
        }
    }
}

// Faces as strips: type 1 gives three new vertices, 2 and 3 replace one of
// the previous face's, 4 swaps two and adds one. Each new index is a delta
// from the last one read. A face of any other type keeps 0, 0, 0.
void readFaces(Model& model, Buffer& types, Buffer& indices) {
    int a = 0, b = 0, c = 0, last = 0;
    for (int i = 0; i < model.faceCount(); ++i) {
        const int type = types.u8();
        if (type == 1) {
            a = indices.shortSmart() + last;
            b = indices.shortSmart() + a;
            c = indices.shortSmart() + b;
        } else if (type == 2) {
            b = c;
            c = indices.shortSmart() + last;
        } else if (type == 3) {
            a = c;
            c = indices.shortSmart() + last;
        } else if (type == 4) {
            std::swap(a, b);
            c = indices.shortSmart() + last;
        } else {
            continue;
        }
        last = c;
        model.faceA[i] = static_cast<std::uint16_t>(a);
        model.faceB[i] = static_cast<std::uint16_t>(b);
        model.faceC[i] = static_cast<std::uint16_t>(c);
    }
}

void readDepthOffsets(Model& model, Buffer& in, int faces) {
    model.depthOffsets.resize(faces);
    for (std::int8_t& offset : model.depthOffsets) offset = static_cast<std::int8_t>(in.i8());
}

void allocate(Model& model, int vertices, int faces) {
    model.x.resize(vertices);
    model.y.resize(vertices);
    model.z.resize(vertices);
    model.faceA.resize(faces);
    model.faceB.resize(faces);
    model.faceC.resize(faces);
    model.colours.resize(faces);
}

// ModelLoader.decodeType3: the face attributes each in their own array, and
// texture faces before the vertices. Its decodeType1 reads the same layout
// without the skeletal bones and depth offsets: its header lacks their flag
// and the group length, as its groups are only the labels.
void decodeType3(Model& model, std::span<const std::uint8_t> data, bool type1) {
    const std::size_t headerSize = type1 ? 23 : 26;
    if (data.size() < headerSize) throw std::runtime_error("model shorter than its header");
    Buffer header = at(data, data.size() - headerSize);
    const int vertices = header.u16();
    const int faces = header.u16();
    const int textureFaces = header.u8();
    const bool hasRenderTypes = header.u8() == 1;
    const int priority = header.u8();
    const bool hasAlphas = header.u8() == 1;
    const bool hasAlphaLabels = header.u8() == 1;
    const bool hasTextures = header.u8() == 1;
    const bool hasLabels = header.u8() == 1;
    const bool hasBones = !type1 && header.u8() == 1;
    const int xLength = header.u16();
    const int yLength = header.u16();
    const int zLength = header.u16();
    const int faceLength = header.u16();
    const int textureCoordLength = header.u16();
    // The labels, then the skeletal bones.
    const int groupLength = type1 ? (hasLabels ? vertices : 0) : header.u16();

    // Texture faces of render type 0 are given by three vertices; types 1 to
    // 3 by more, which RuneLite skips.
    int simple = 0, complex = 0, cylinders = 0;
    if (textureFaces > 0) {
        Buffer types = at(data, 0);
        model.textureRenderTypes.resize(textureFaces);
        for (std::uint8_t& type : model.textureRenderTypes) {
            type = static_cast<std::uint8_t>(types.u8());
            const auto signedType = static_cast<std::int8_t>(type);
            if (signedType == 0) ++simple;
            if (signedType >= 1 && signedType <= 3) ++complex;
            if (signedType == 2) ++cylinders;
        }
    }

    std::size_t pos = static_cast<std::size_t>(textureFaces) + vertices;
    const std::size_t renderTypesAt = pos;
    if (hasRenderTypes) pos += faces;
    const std::size_t faceTypesAt = pos;
    pos += faces;
    const std::size_t prioritiesAt = pos;
    if (priority == 255) pos += faces;
    const std::size_t alphaLabelsAt = pos;
    if (hasAlphaLabels) pos += faces;
    const std::size_t labelsAt = pos;
    pos += groupLength;
    const std::size_t alphasAt = pos;
    if (hasAlphas) pos += faces;
    const std::size_t faceIndicesAt = pos;
    pos += faceLength;
    const std::size_t texturesAt = pos;
    if (hasTextures) pos += faces * 2;
    const std::size_t textureCoordsAt = pos;
    pos += textureCoordLength;
    const std::size_t coloursAt = pos;
    pos += faces * 2;
    const std::size_t xAt = pos;
    pos += xLength;
    const std::size_t yAt = pos;
    pos += yLength;
    const std::size_t zAt = pos;
    pos += zLength;
    const std::size_t simpleTexturesAt = pos;
    pos += simple * 6 + complex * 6 + complex * 6 + complex * 2 + complex + complex * 2 + cylinders * 2;
    const std::size_t footerAt = pos;

    allocate(model, vertices, faces);
    model.hasLabels = hasLabels;
    if (hasLabels) model.labels.resize(vertices);
    if (hasRenderTypes) model.renderTypes.resize(faces);
    if (priority == 255) {
        model.priorities.resize(faces);
    } else {
        model.priority = static_cast<std::uint8_t>(priority);
    }
    if (hasAlphas) model.alphas.resize(faces);
    if (hasAlphaLabels) model.alphaLabels.resize(faces);
    if (hasTextures) model.textures.resize(faces);
    if (hasTextures && textureFaces > 0) model.textureCoords.resize(faces);
    if (hasBones) {
        model.bones.resize(vertices);
        model.weights.resize(vertices);
    }
    if (textureFaces > 0) {
        model.textureA.resize(textureFaces);
        model.textureB.resize(textureFaces);
        model.textureC.resize(textureFaces);
    }

    Buffer flags = at(data, textureFaces), xs = at(data, xAt), ys = at(data, yAt), zs = at(data, zAt), groups = at(data, labelsAt);
    readVertices(model, flags, xs, ys, zs, groups);
    if (hasBones) readBones(model, groups);

    Buffer colours = at(data, coloursAt), renderTypes = at(data, renderTypesAt), priorities = at(data, prioritiesAt),
           alphas = at(data, alphasAt), alphaLabels = at(data, alphaLabelsAt), textures = at(data, texturesAt),
           textureCoords = at(data, textureCoordsAt);
    for (int i = 0; i < faces; ++i) {
        model.colours[i] = static_cast<std::uint16_t>(colours.u16());
        if (hasRenderTypes) model.renderTypes[i] = static_cast<std::uint8_t>(renderTypes.u8());
        if (priority == 255) model.priorities[i] = static_cast<std::uint8_t>(priorities.u8());
        if (hasAlphas) model.alphas[i] = static_cast<std::uint8_t>(alphas.u8());
        if (hasAlphaLabels) model.alphaLabels[i] = static_cast<std::uint8_t>(alphaLabels.u8());
        if (hasTextures) model.textures[i] = static_cast<std::int16_t>(textures.u16() - 1);
        if (!model.textureCoords.empty() && model.textures[i] != -1) {
            model.textureCoords[i] = static_cast<std::int8_t>(textureCoords.u8() - 1);
        }
    }

    Buffer faceTypes = at(data, faceTypesAt), faceIndices = at(data, faceIndicesAt);
    readFaces(model, faceTypes, faceIndices);

    Buffer simpleTextures = at(data, simpleTexturesAt);
    for (int i = 0; i < textureFaces; ++i) {
        if (model.textureRenderTypes[i] != 0) continue;
        model.textureA[i] = static_cast<std::uint16_t>(simpleTextures.u16());
        model.textureB[i] = static_cast<std::uint16_t>(simpleTextures.u16());
        model.textureC[i] = static_cast<std::uint16_t>(simpleTextures.u16());
    }

    Buffer footer = at(data, footerAt);
    if (footer.u8() != 0) footer.skip(10);
    if (type1) return;
    if (footer.u8() == 1) readDepthOffsets(model, footer, faces);
}

// A face's texture coordinates are dropped where they name the face's own
// vertices, and the arrays nothing uses go.
void tidyTextures(Model& model, bool usesTextures, bool usesRenderTypes) {
    if (!model.textureCoords.empty()) {
        bool usesCoords = false;
        for (int i = 0; i < model.faceCount(); ++i) {
            const int coord = static_cast<std::uint8_t>(model.textureCoords[i]);
            if (coord == 255) continue;
            if (coord >= static_cast<int>(model.textureA.size())) throw std::runtime_error("face names a texture face beyond the model's");
            if (model.faceA[i] == model.textureA[coord] && model.faceB[i] == model.textureB[coord] && model.faceC[i] == model.textureC[coord]) {
                model.textureCoords[i] = -1;
            } else {
                usesCoords = true;
            }
        }
        if (!usesCoords) model.textureCoords.clear();
    }
    if (!usesTextures) model.textures.clear();
    if (!usesRenderTypes) model.renderTypes.clear();
}

// ModelLoader.decodeType2: a texture flag byte per face, a textured face
// taking its texture from its colour and a colour of 127. Its decodeOldFormat
// reads the same layout without the skeletal bones and depth offsets: its
// header lacks their flag and the group length, as its groups are only the
// labels.
void decodeType2(Model& model, std::span<const std::uint8_t> data, bool oldFormat) {
    const std::size_t headerSize = oldFormat ? 18 : 23;
    if (data.size() < headerSize) throw std::runtime_error("model shorter than its header");
    Buffer header = at(data, data.size() - headerSize);
    const int vertices = header.u16();
    const int faces = header.u16();
    const int textureFaces = header.u8();
    const bool textured = header.u8() == 1;
    const int priority = header.u8();
    const bool hasAlphas = header.u8() == 1;
    const bool hasAlphaLabels = header.u8() == 1;
    const bool hasLabels = header.u8() == 1;
    const bool hasBones = !oldFormat && header.u8() == 1;
    const int xLength = header.u16();
    const int yLength = header.u16();
    const int zLength = header.u16();
    const int faceLength = header.u16();
    // The labels, then the skeletal bones.
    const int groupLength = oldFormat ? (hasLabels ? vertices : 0) : header.u16();

    std::size_t pos = vertices;
    const std::size_t faceTypesAt = pos;
    pos += faces;
    const std::size_t prioritiesAt = pos;
    if (priority == 255) pos += faces;
    const std::size_t alphaLabelsAt = pos;
    if (hasAlphaLabels) pos += faces;
    const std::size_t textureFlagsAt = pos;
    if (textured) pos += faces;
    const std::size_t labelsAt = pos;
    pos += groupLength;
    const std::size_t alphasAt = pos;
    if (hasAlphas) pos += faces;
    const std::size_t faceIndicesAt = pos;
    pos += faceLength;
    const std::size_t coloursAt = pos;
    pos += faces * 2;
    const std::size_t texturesAt = pos;
    pos += textureFaces * 6;
    const std::size_t xAt = pos;
    pos += xLength;
    const std::size_t yAt = pos;
    pos += yLength;
    const std::size_t zAt = pos;
    pos += zLength;
    const std::size_t footerAt = pos;

    allocate(model, vertices, faces);
    if (textureFaces > 0) {
        model.textureRenderTypes.resize(textureFaces);
        model.textureA.resize(textureFaces);
        model.textureB.resize(textureFaces);
        model.textureC.resize(textureFaces);
    }
    model.hasLabels = hasLabels;
    if (hasLabels) model.labels.resize(vertices);
    if (textured) {
        model.renderTypes.resize(faces);
        model.textureCoords.resize(faces);
        model.textures.resize(faces);
    }
    if (priority == 255) {
        model.priorities.resize(faces);
    } else {
        model.priority = static_cast<std::uint8_t>(priority);
    }
    if (hasAlphas) model.alphas.resize(faces);
    if (hasAlphaLabels) model.alphaLabels.resize(faces);
    if (hasBones) {
        model.bones.resize(vertices);
        model.weights.resize(vertices);
    }

    Buffer flags = at(data, 0), xs = at(data, xAt), ys = at(data, yAt), zs = at(data, zAt), groups = at(data, labelsAt);
    readVertices(model, flags, xs, ys, zs, groups);
    if (hasBones) readBones(model, groups);

    bool usesRenderTypes = false;
    bool usesTextures = false;
    Buffer colours = at(data, coloursAt), textureFlags = at(data, textureFlagsAt), priorities = at(data, prioritiesAt),
           alphas = at(data, alphasAt), alphaLabels = at(data, alphaLabelsAt);
    for (int i = 0; i < faces; ++i) {
        model.colours[i] = static_cast<std::uint16_t>(colours.u16());
        if (textured) {
            const int f = textureFlags.u8();
            model.renderTypes[i] = (f & 1) == 1 ? 1 : 0;
            usesRenderTypes = usesRenderTypes || (f & 1) == 1;
            if ((f & 2) == 2) {
                model.textureCoords[i] = static_cast<std::int8_t>(f >> 2);
                model.textures[i] = static_cast<std::int16_t>(model.colours[i]);
                model.colours[i] = 127;
                usesTextures = usesTextures || model.textures[i] != -1;
            } else {
                model.textureCoords[i] = -1;
                model.textures[i] = -1;
            }
        }
        if (priority == 255) model.priorities[i] = static_cast<std::uint8_t>(priorities.u8());
        if (hasAlphas) model.alphas[i] = static_cast<std::uint8_t>(alphas.u8());
        if (hasAlphaLabels) model.alphaLabels[i] = static_cast<std::uint8_t>(alphaLabels.u8());
    }

    Buffer faceTypes = at(data, faceTypesAt), faceIndices = at(data, faceIndicesAt);
    readFaces(model, faceTypes, faceIndices);

    Buffer textures = at(data, texturesAt);
    for (int i = 0; i < textureFaces; ++i) {
        model.textureA[i] = static_cast<std::uint16_t>(textures.u16());
        model.textureB[i] = static_cast<std::uint16_t>(textures.u16());
        model.textureC[i] = static_cast<std::uint16_t>(textures.u16());
    }

    if (!oldFormat) {
        Buffer footer = at(data, footerAt);
        if (footer.u8() == 1) readDepthOffsets(model, footer, faces);
    }
    tidyTextures(model, usesTextures, usesRenderTypes);
}

} // namespace

std::optional<Model> loadModel(const Store& store, int id) {
    auto files = store.files(Index::Models, id);
    if (!files || files->empty()) return std::nullopt;
    const std::vector<std::uint8_t>& data = files->begin()->second;
    try {
        // The format is in the last two bytes: FF FD for type 3, FF FE for
        // type 2, FF FF for type 1; the oldest format has no marker.
        const bool marked = data.size() >= 2 && data[data.size() - 2] == 0xFF;
        Model model;
        if (marked && data.back() == 0xFD) {
            decodeType3(model, data, false);
        } else if (marked && data.back() == 0xFE) {
            decodeType2(model, data, false);
        } else if (marked && data.back() == 0xFF) {
            decodeType3(model, data, true);
        } else {
            decodeType2(model, data, true);
        }
        return model;
    } catch (const std::runtime_error& error) {
        undecodable(store, "model", id, error);
        return std::nullopt;
    }
}

namespace {

// Java's int arithmetic, which wraps where C++'s signed overflow is undefined.
std::int32_t wrap(std::int64_t value) { return static_cast<std::int32_t>(value); }

void requireVertex(const Model& model, int vertex) {
    if (vertex < 0 || vertex >= model.vertexCount()) throw std::runtime_error("face names a vertex beyond the model's");
}

} // namespace

// RuneLite ModelDefinition.computeNormals.
ModelNormals computeNormals(const Model& model) {
    ModelNormals normals;
    normals.vertices.resize(model.vertexCount());
    for (int i = 0; i < model.faceCount(); ++i) {
        const int a = model.faceA[i], b = model.faceB[i], c = model.faceC[i];
        requireVertex(model, a);
        requireVertex(model, b);
        requireVertex(model, c);
        const std::int32_t xA = wrap(std::int64_t{model.x[b]} - model.x[a]);
        const std::int32_t yA = wrap(std::int64_t{model.y[b]} - model.y[a]);
        const std::int32_t zA = wrap(std::int64_t{model.z[b]} - model.z[a]);
        const std::int32_t xB = wrap(std::int64_t{model.x[c]} - model.x[a]);
        const std::int32_t yB = wrap(std::int64_t{model.y[c]} - model.y[a]);
        const std::int32_t zB = wrap(std::int64_t{model.z[c]} - model.z[a]);
        std::int32_t nx = wrap(std::int64_t{yA} * zB - std::int64_t{yB} * zA);
        std::int32_t ny = wrap(std::int64_t{zA} * xB - std::int64_t{zB} * xA);
        std::int32_t nz = wrap(std::int64_t{xA} * yB - std::int64_t{xB} * yA);
        while (nx > 8192 || ny > 8192 || nz > 8192 || nx < -8192 || ny < -8192 || nz < -8192) {
            nx >>= 1;
            ny >>= 1;
            nz >>= 1;
        }
        int length = static_cast<int>(std::sqrt(static_cast<double>(nx * nx + ny * ny + nz * nz)));
        if (length <= 0) length = 1;
        nx = nx * 256 / length;
        ny = ny * 256 / length;
        nz = nz * 256 / length;

        const int renderType = model.renderTypes.empty() ? 0 : static_cast<std::int8_t>(model.renderTypes[i]);
        if (renderType == 0) {
            for (const int vertex : {a, b, c}) {
                VertexNormal& normal = normals.vertices[vertex];
                normal.x += nx;
                normal.y += ny;
                normal.z += nz;
                ++normal.magnitude;
            }
        } else if (renderType == 1) {
            if (normals.faces.empty()) normals.faces.resize(model.faceCount());
            normals.faces[i] = FaceNormal{nx, ny, nz};
        }
    }
    return normals;
}

// RuneLite ModelDefinition.computeTextureUVCoordinates, its float arithmetic
// in its order.
std::vector<std::optional<FaceTextureCoordinates>> computeTextureCoordinates(const Model& model) {
    std::vector<std::optional<FaceTextureCoordinates>> coordinates(model.faceCount());
    if (model.textures.empty()) return coordinates;
    for (int i = 0; i < model.faceCount(); ++i) {
        if (model.textures[i] == -1) continue;
        FaceTextureCoordinates& face = coordinates[i].emplace();
        if (model.textureCoords.empty() || model.textureCoords[i] == -1) {
            face.u = {0.0f, 1.0f, 0.0f};
            face.v = {0.0f, 0.0f, 1.0f};
            continue;
        }
        const int textureFace = static_cast<std::uint8_t>(model.textureCoords[i]);
        if (textureFace >= model.textureFaceCount()) throw std::runtime_error("face names a texture face beyond the model's");
        // RuneLite holds the texture face's vertices as shorts.
        const int texA = static_cast<std::int16_t>(model.textureA[textureFace]);
        const int texB = static_cast<std::int16_t>(model.textureB[textureFace]);
        const int texC = static_cast<std::int16_t>(model.textureC[textureFace]);
        const int a = model.faceA[i], b = model.faceB[i], c = model.faceC[i];
        for (const int vertex : {texA, texB, texC, a, b, c}) requireVertex(model, vertex);
        const auto fx = [&](int vertex) { return static_cast<float>(model.x[vertex]); };
        const auto fy = [&](int vertex) { return static_cast<float>(model.y[vertex]); };
        const auto fz = [&](int vertex) { return static_cast<float>(model.z[vertex]); };

        const float v1x = fx(texA), v1y = fy(texA), v1z = fz(texA);
        const float v2x = fx(texB) - v1x, v2y = fy(texB) - v1y, v2z = fz(texB) - v1z;
        const float v3x = fx(texC) - v1x, v3y = fy(texC) - v1y, v3z = fz(texC) - v1z;
        const float v4x = fx(a) - v1x, v4y = fy(a) - v1y, v4z = fz(a) - v1z;
        const float v5x = fx(b) - v1x, v5y = fy(b) - v1y, v5z = fz(b) - v1z;
        const float v6x = fx(c) - v1x, v6y = fy(c) - v1y, v6z = fz(c) - v1z;

        const float v7x = v2y * v3z - v2z * v3y;
        const float v7y = v2z * v3x - v2x * v3z;
        const float v7z = v2x * v3y - v2y * v3x;

        float v8x = v3y * v7z - v3z * v7y;
        float v8y = v3z * v7x - v3x * v7z;
        float v8z = v3x * v7y - v3y * v7x;
        float f = 1.0f / (v8x * v2x + v8y * v2y + v8z * v2z);
        face.u[0] = (v8x * v4x + v8y * v4y + v8z * v4z) * f;
        face.u[1] = (v8x * v5x + v8y * v5y + v8z * v5z) * f;
        face.u[2] = (v8x * v6x + v8y * v6y + v8z * v6z) * f;

        v8x = v2y * v7z - v2z * v7y;
        v8y = v2z * v7x - v2x * v7z;
        v8z = v2x * v7y - v2y * v7x;
        f = 1.0f / (v8x * v3x + v8y * v3y + v8z * v3z);
        face.v[0] = (v8x * v4x + v8y * v4y + v8z * v4z) * f;
        face.v[1] = (v8x * v5x + v8y * v5y + v8z * v5z) * f;
        face.v[2] = (v8x * v6x + v8y * v6y + v8z * v6z) * f;
    }
    return coordinates;
}

// RuneLite ModelDefinition.computeAnimationTables.
std::vector<std::vector<int>> vertexGroups(const Model& model) {
    std::vector<std::vector<int>> groups;
    if (!model.hasLabels) return groups;
    const auto highest = std::max_element(model.labels.begin(), model.labels.end());
    groups.resize(highest == model.labels.end() ? 1 : static_cast<std::size_t>(*highest) + 1);
    for (int vertex = 0; vertex < model.vertexCount(); ++vertex) groups[model.labels[vertex]].push_back(vertex);
    return groups;
}

} // namespace osrs::cache
