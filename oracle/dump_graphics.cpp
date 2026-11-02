// Models, frame maps, frames, sprites, fonts and textures (osrs/cache/model.h,
// frames.h and sprites.h), as RuneLite's ModelDefinition, FramemapDefinition,
// FrameDefinition, SpriteDefinition, FontDefinition and TextureDefinition
// hold them.

#include "lines.h"

#include <osrs/cache/frames.h>
#include <osrs/cache/model.h>
#include <osrs/cache/sprites.h>

#include <bit>
#include <cstdio>
#include <stdexcept>

namespace osrs::cache::oracle {

namespace {

// Each value converted to the Java type RuneLite holds it in.
template <class Java, class T>
std::vector<Java> as(const std::vector<T>& values) {
    return std::vector<Java>(values.begin(), values.end());
}

// A float[] as lines.h formats one. Lines has no float arrays, and
// enumeration() writes its value as given.
void floats(Lines& out, std::string_view path, std::span<const float> values) {
    std::string text = "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        char bits[16];
        std::snprintf(bits, sizeof bits, "f%08x", std::bit_cast<std::uint32_t>(values[i]));
        text += i > 0 ? "," : "";
        text += bits;
    }
    out.enumeration(path, text + "]");
}

void models(const Store& store, Lines& out) {
    for (const int id : store.groups(Index::Models)) {
        std::optional<Model> model;
        ModelNormals normals;
        std::vector<std::optional<FaceTextureCoordinates>> coordinates;
        bool failed = false;
        try {
            model = loadModel(store, id);
            if (!model) continue;
            normals = computeNormals(*model);
            coordinates = computeTextureCoordinates(*model);
        } catch (const std::runtime_error&) {
            failed = true;
        }
        out.object({id});
        if (failed) {
            out.error();
            continue;
        }
        if (out.skipping()) continue;
        const Model& m = *model;
        out.integer("id", id);

        out.integer("vertexCount", m.vertexCount());
        out.integers("vertexX", m.x);
        out.integers("vertexY", m.y);
        out.integers("vertexZ", m.z);
        for (std::size_t i = 0; i < normals.vertices.size(); ++i) {
            const VertexNormal& normal = normals.vertices[i];
            const std::string path = at("vertexNormals", i);
            out.integer(field(path, "x"), normal.x);
            out.integer(field(path, "y"), normal.y);
            out.integer(field(path, "z"), normal.z);
            out.integer(field(path, "magnitude"), normal.magnitude);
        }

        out.integer("faceCount", m.faceCount());
        out.integers("faceIndices1", m.faceA);
        out.integers("faceIndices2", m.faceB);
        out.integers("faceIndices3", m.faceC);
        if (!m.alphas.empty()) out.integers("faceTransparencies", as<std::int8_t>(m.alphas));
        out.integers("faceColors", as<std::int16_t>(m.colours));
        if (!m.priorities.empty()) out.integers("faceRenderPriorities", as<std::int8_t>(m.priorities));
        if (!m.renderTypes.empty()) out.integers("faceRenderTypes", as<std::int8_t>(m.renderTypes));
        for (std::size_t i = 0; i < normals.faces.size(); ++i) {
            if (!normals.faces[i]) continue;
            const std::string path = at("faceNormals", i);
            out.integer(field(path, "x"), normals.faces[i]->x);
            out.integer(field(path, "y"), normals.faces[i]->y);
            out.integer(field(path, "z"), normals.faces[i]->z);
        }

        out.integer("numTextureFaces", m.textureFaceCount());
        if (m.textureFaceCount() > 0) {
            out.integers("texIndices1", as<std::int16_t>(m.textureA));
            out.integers("texIndices2", as<std::int16_t>(m.textureB));
            out.integers("texIndices3", as<std::int16_t>(m.textureC));
            out.integers("textureRenderTypes", as<std::int8_t>(m.textureRenderTypes));
        }
        for (std::size_t i = 0; i < coordinates.size(); ++i) {
            if (!coordinates[i]) continue;
            floats(out, at("faceTextureUCoordinates", i), coordinates[i]->u);
            floats(out, at("faceTextureVCoordinates", i), coordinates[i]->v);
        }
        if (!m.textures.empty()) out.integers("faceTextures", m.textures);
        if (!m.depthOffsets.empty()) out.integers("faceZOffsets", m.depthOffsets);
        if (!m.textureCoords.empty()) out.integers("textureCoords", m.textureCoords);

        // RuneLite's computeAnimationTables turns packedVertexGroups into
        // vertexGroups and drops it.
        if (!m.alphaLabels.empty()) out.integers("packedTransparencyVertexGroups", m.alphaLabels);
        out.integer("priority", static_cast<std::int8_t>(m.priority));
        const std::vector<std::vector<int>> groups = vertexGroups(m);
        for (std::size_t g = 0; g < groups.size(); ++g) out.integers(at("vertexGroups", g), groups[g]);
        for (std::size_t i = 0; i < m.bones.size(); ++i) out.integers(at("animayaGroups", i), m.bones[i]);
        for (std::size_t i = 0; i < m.weights.size(); ++i) out.integers(at("animayaScales", i), m.weights[i]);
        // Set by RuneLite's computeMaxPriority alone, which its loader doesn't call.
        out.integer("maxPriority", 0);
    }
}

void framemaps(const Store& store, Lines& out) {
    for (const int id : store.groups(Index::Skeletons)) {
        std::optional<Framemap> framemap;
        bool failed = false;
        try {
            framemap = loadFramemap(store, id);
            if (!framemap) continue;
        } catch (const std::runtime_error&) {
            failed = true;
        }
        out.object({id});
        if (failed) {
            out.error();
            continue;
        }
        out.integer("id", id);
        out.integers("types", framemap->types);
        for (std::size_t i = 0; i < framemap->labels.size(); ++i) out.integers(at("frameMaps", i), framemap->labels[i]);
        out.integer("length", static_cast<std::int64_t>(framemap->types.size()));
    }
}

void frames(const Store& store, Lines& out) {
    for (const int group : store.groups(Index::Frames)) {
        std::optional<std::map<int, std::vector<std::uint8_t>>> files;
        std::map<std::int32_t, Frame> decoded;
        try {
            files = store.files(Index::Frames, group);
            if (!files) continue;
            decoded = loadFrames(store, group);
        } catch (const std::runtime_error&) {
            out.object({group});
            out.error();
            continue;
        }
        for (const auto& [file, bytes] : *files) {
            out.object({group, file});
            const auto found = decoded.find(group << 16 | file);
            // RuneLite fails on each frame loadFrames leaves out, and on one of
            // more than the 500 transforms its scratch arrays hold.
            if (found == decoded.end() || found->second.transforms.size() > 500) {
                out.error();
                continue;
            }
            const Frame& frame = found->second;
            std::vector<int> groups, x, y, z;
            for (const FrameTransform& transform : frame.transforms) {
                groups.push_back(transform.group);
                x.push_back(transform.x);
                y.push_back(transform.y);
                z.push_back(transform.z);
            }
            out.integer("id", file);
            out.integer("framemap", frame.skeleton);
            out.integers("translator_x", x);
            out.integers("translator_y", y);
            out.integers("translator_z", z);
            out.integer("translatorCount", static_cast<std::int64_t>(frame.transforms.size()));
            out.integers("indexFrameIds", groups);
            out.boolean("showing", frame.showing);
        }
    }
}

void sprites(const Store& store, Lines& out) {
    for (const int id : store.groups(Index::Sprites)) {
        std::optional<std::vector<Sprite>> frames;
        try {
            frames = loadSprites(store, id);
            if (!frames) continue;
        } catch (const std::runtime_error&) {
            // RuneLite's oracle keys a group that fails by the group alone.
            out.object({id});
            out.error();
            continue;
        }
        for (std::size_t frame = 0; frame < frames->size(); ++frame) {
            const Sprite& sprite = (*frames)[frame];
            out.object({id, static_cast<std::int64_t>(frame)});
            out.integer("id", id);
            out.integer("frame", static_cast<std::int64_t>(frame));
            out.integer("offsetX", sprite.offsetX);
            out.integer("offsetY", sprite.offsetY);
            out.integer("width", sprite.width);
            out.integer("height", sprite.height);
            out.integers("pixels", as<std::int32_t>(sprite.pixels));
            out.integer("maxWidth", sprite.maxWidth);
            out.integer("maxHeight", sprite.maxHeight);
            out.integers("pixelIdx", as<std::int8_t>(sprite.paletteIndices));
            out.integers("palette", as<std::int32_t>(sprite.palette));
        }
    }
}

void fonts(const Store& store, Lines& out) {
    for (const int id : store.groups(Index::Fonts)) {
        std::optional<Font> font;
        bool failed = false;
        try {
            font = loadFont(store, id);
            if (!font) continue;
        } catch (const std::runtime_error&) {
            failed = true;
        }
        out.object({id});
        if (failed) {
            out.error();
            continue;
        }
        out.integers("advances", std::span<const std::uint8_t>(font->advances));
        out.integer("ascent", font->ascent);
    }
}

void textureFields(Lines& out, const TextureDefinition& texture) {
    out.integer("missingColor", texture.averageHsl);
    out.boolean("field1778", texture.opaque);
    out.integer("id", texture.id);
    out.integers("fileIds", texture.fileIds);
    // Null in RuneLite where empty: the revision 233 format reads none of
    // them, and the older one field1780 and field1781 for a texture of more
    // than one sprite only. (An older texture of no sprites would have an
    // empty field1786; the current cache holds none.)
    if (!texture.field1780.empty()) out.integers("field1780", texture.field1780);
    if (!texture.field1781.empty()) out.integers("field1781", texture.field1781);
    if (!texture.field1786.empty()) out.integers("field1786", texture.field1786);
    out.integer("animationSpeed", texture.animationSpeed);
    out.integer("animationDirection", texture.animationDirection);
}

void textures(const Store& store, Lines& out) {
    for (const TextureDefinition& texture : loadTextureDefinitions(store)) {
        out.object({texture.id});
        textureFields(out, texture);
    }
}

// As the oracle builds them: method2680 at brightness 0.8 and 128 by 128.
void texturesWithPixels(const Store& store, Lines& out) {
    for (const TextureDefinition& texture : loadTextureDefinitions(store)) {
        std::optional<std::vector<std::uint32_t>> pixels;
        try {
            pixels = texturePixels(store, texture, 0.8, 128);
        } catch (const std::runtime_error&) {
        }
        out.object({texture.id});
        // RuneLite fails where a sprite is missing, too.
        if (!pixels) {
            out.error();
            continue;
        }
        textureFields(out, texture);
        out.integers("pixels", as<std::int32_t>(*pixels));
    }
}

const Type model("model", models);
const Type framemap("framemap", framemaps);
const Type frame("frame", frames);
const Type sprite("sprite", sprites);
const Type font("font", fonts);
const Type texture("texture", textures);
const Type texturepixels("texturepixels", texturesWithPixels);

} // namespace

} // namespace osrs::cache::oracle
