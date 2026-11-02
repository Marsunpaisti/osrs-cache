// The cache reader on the copy of the game cache OSRS_CACHE_DIR names: the
// official client's, copied on 2026-11-01. Skipped when it is missing. The
// expected values are the game's as of that copy, so a later cache fails some
// of them; RuneLite's cache library decoded the same copy to the same values.

#include <doctest/doctest.h>

#include <osrs/cache/collision.h>
#include <osrs/cache/definitions.h>
#include <osrs/cache/frames.h>
#include <osrs/cache/map.h>
#include <osrs/cache/model.h>
#include <osrs/cache/skeletal.h>
#include <osrs/cache/sprites.h>
#include <osrs/cache/store.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

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

template <class Definition>
const Definition& byId(const std::vector<Definition>& definitions, std::int32_t id) {
    const auto found = std::find_if(definitions.begin(), definitions.end(), [&](const Definition& d) { return d.id == id; });
    REQUIRE(found != definitions.end());
    return *found;
}

} // namespace

TEST_CASE("a noted and a placeholder item take the name of the item they stand for") {
    if (store() == nullptr) return;
    const std::vector<ItemDefinition> items = loadItemDefinitions(*store());

    const ItemDefinition& whip = byId(items, 4151);
    CHECK(whip.name == "Abyssal whip");
    CHECK(whip.weight == 453);
    CHECK(whip.wearPos1 == 3);
    CHECK_FALSE(whip.wearPos2.has_value());
    CHECK(numberParam(whip.params, 1) == 82); // slash attack
    CHECK(numberParam(whip.params, 14) == 4); // attack speed
    CHECK(whip.placeholderId == 14032);

    CHECK(whip.inventoryOptions[1] == "Wield");
    CHECK(whip.inventoryOptions[4] == "Drop");
    CHECK(byId(items, 385).inventoryOptions[0] == "Eat");

    const ItemDefinition& noted = byId(items, 4152);
    CHECK(noted.name == "Abyssal whip");
    CHECK(noted.stackable == 1);
    CHECK(noted.notedTemplate == 799);

    const ItemDefinition& placeholder = byId(items, 14032);
    CHECK(placeholder.name == "Abyssal whip");
    CHECK(placeholder.placeholderTemplate == 14401);

    // A spell's name is a string param.
    const ItemDefinition& tanLeather = byId(items, 1741);
    CHECK(std::get<std::string>(tanLeather.params.at(601)) == "Tan Leather");
    CHECK_FALSE(numberParam(tanLeather.params, 601).has_value());
}

TEST_CASE("an NPC's stats and params, and an animation's frames") {
    if (store() == nullptr) return;
    const std::vector<NpcDefinition> npcs = loadNpcDefinitions(*store());
    const NpcDefinition& jad = byId(npcs, 3127);
    CHECK(jad.name == "TzTok-Jad");
    CHECK(jad.size == 5);
    CHECK(jad.stats == std::array<std::uint16_t, 6>{640, 480, 960, 250, 960, 480});
    CHECK(numberParam(jad.params, 3) == 60); // magic attack bonus
    CHECK(jad.combatLevel == 702);
    CHECK(jad.options[1] == "Attack");
    // An Inferno pillar's NPC has no options, so the client's menu has only
    // its Examine, below Walk here: a left click on it walks, or acts on an
    // NPC behind it.
    const NpcDefinition& pillar = byId(npcs, 7709);
    CHECK(std::none_of(pillar.options.begin(), pillar.options.end(), [](const auto& option) { return option.has_value(); }));

    const std::vector<SequenceDefinition> sequences = loadSequenceDefinitions(*store());
    // Jad's death: 149 cycles of frames, then its last pose held until it despawns.
    const SequenceDefinition& jadDeath = byId(sequences, 2654);
    REQUIRE(jadDeath.frameLengths.size() == 28);
    CHECK(jadDeath.frameLengths.back() == 3392);
    CHECK(std::accumulate(jadDeath.frameLengths.begin(), jadDeath.frameLengths.end() - 1, 0) == 149);
    CHECK_FALSE(jadDeath.animMayaId.has_value());

    const SequenceDefinition& skeletal = byId(sequences, 2973);
    CHECK(skeletal.animMayaId == 5439488);
    CHECK(skeletal.animMayaEnd == 120);
}

TEST_CASE("the Fight Caves' collision: walls block both sides, and objects their footprint") {
    if (store() == nullptr) return;
    const std::vector<ObjectDefinition> objects = loadObjectDefinitions(*store());
    const int fightCaves = 9551;
    const std::map<int, RegionFlags> flags = staticCollision(*store(), objects, std::vector<int>{fightCaves});
    REQUIRE(flags.contains(fightCaves));
    const auto at = [&](int x, int y) { return flags.at(fightCaves)[x * 64 + y]; };

    // A corner of the cave wall: its north, west and north-west wall bits,
    // with the projectile bits of the north and west walls, ...
    CHECK(at(3, 25) == 0x10683);
    // ... and the tile north of it gets the south wall and its projectile
    // bit, on blocked floor.
    CHECK(at(3, 26) == 0x204020);
    CHECK(at(0, 55) == 0x20100); // a rock's footprint, blocking projectiles
    CHECK(at(2, 9) == 0x200000); // blocked floor alone

    // Region 0 has no map.
    CHECK(staticCollision(*store(), objects, std::vector<int>{0}).empty());
}

TEST_CASE("a skeletal animation moves its skeleton's bones, and the vertices on them, as Encounter Replay's decoder does") {
    if (store() == nullptr) return;
    // Sequence 2973's animation. The expected values are what Encounter
    // Replay's TypeScript computes from the same cache files.
    const std::optional<SkeletalAnimation> animation = loadSkeletalAnimation(*store(), 5439488);
    REQUIRE(animation.has_value());
    CHECK(animation->skeleton == 2244);
    CHECK(animation->hasAlpha);
    const std::optional<Skeleton> skeleton = loadSkeleton(*store(), animation->skeleton);
    REQUIRE(skeleton.has_value());
    CHECK(skeleton->bones.size() == 214);
    CHECK(skeleton->types.size() == 12);

    const std::vector<Matrix4> transforms = boneTransforms(*skeleton, *animation, 30);
    REQUIRE(transforms.size() == 214);
    const Matrix4& bone = transforms[98];
    CHECK(bone[0] == 0.6673802137374878f);
    CHECK(bone[12] == 314.35595703125f);
    CHECK(bone[13] == 632.54736328125f);
    CHECK(bone[14] == 660.8253784179688f);

    // A vertex wholly on that bone moves with it; one on no bone stays.
    std::vector<std::int32_t> x{100, 7}, y{-200, 8}, z{50, 9};
    const std::vector<std::vector<std::uint8_t>> bones{{98}, {}};
    const std::vector<std::vector<std::uint8_t>> weights{{255}, {}};
    skin(x, y, z, bones, weights, transforms);
    CHECK(x == std::vector<std::int32_t>{256, 7});
    CHECK(y == std::vector<std::int32_t>{-509, 8});
    CHECK(z == std::vector<std::int32_t>{-477, 9});

    CHECK_FALSE(loadSkeletalAnimation(*store(), 0x7FFF0000).has_value());
}

TEST_CASE("models of both formats the cache holds decode as RuneLite's ModelLoader decodes them") {
    if (store() == nullptr) return;
    // The expected values are RuneLite's, from CacheMapDumper's render export
    // of a cache copy in which these models are the same.
    const std::optional<Model> type2 = loadModel(*store(), 1001);
    REQUIRE(type2.has_value());
    CHECK(type2->vertexCount() == 50);
    CHECK(type2->faceCount() == 62);
    CHECK((type2->x[0] == -56 && type2->y[0] == -80 && type2->z[0] == -48));
    CHECK((type2->faceA[0] == 0 && type2->faceB[0] == 1 && type2->faceC[0] == 2));
    CHECK(type2->colours[0] == 6932);
    CHECK(type2->priorities.size() == 62);
    CHECK(type2->textures.empty());

    const std::optional<Model> type3 = loadModel(*store(), 1027);
    REQUIRE(type3.has_value());
    CHECK(type3->vertexCount() == 27);
    CHECK(type3->faceCount() == 19);
    CHECK((type3->x[0] == 42 && type3->y[0] == -1 && type3->z[0] == -1));
    CHECK(type3->colours[0] == 3008);
    CHECK(type3->priorities.empty());
    CHECK(type3->priority == 0);

    CHECK_FALSE(loadModel(*store(), 0x7FFFFFF).has_value());
}

TEST_CASE("the Manticore's stand animation curls its tail up through its model's skeletal bones") {
    if (store() == nullptr) return;
    const std::optional<Model> manticore = loadModel(*store(), 52612);
    REQUIRE(manticore.has_value());
    REQUIRE(manticore->bones.size() == 726);
    // The weights of each vertex's bones make up the whole vertex.
    for (std::size_t v = 0; v < manticore->weights.size(); ++v) {
        int sum = 0;
        for (const std::uint8_t weight : manticore->weights[v]) sum += weight;
        CAPTURE(v);
        CHECK((sum == 254 || sum == 255));
    }

    const std::vector<SequenceDefinition> sequences = loadSequenceDefinitions(*store());
    const SequenceDefinition& stand = byId(sequences, 10863);
    REQUIRE(stand.animMayaId.has_value());
    const std::optional<SkeletalAnimation> animation = loadSkeletalAnimation(*store(), *stand.animMayaId);
    REQUIRE(animation.has_value());
    const std::optional<Skeleton> skeleton = loadSkeleton(*store(), animation->skeleton);
    REQUIRE(skeleton.has_value());
    for (const auto& bones : manticore->bones) {
        for (const std::uint8_t bone : bones) CHECK(bone < skeleton->bones.size());
    }

    // The model holds the tail stretched out behind; standing, its tip (vertex
    // 0) curls up over the body.
    std::vector<std::int32_t> x = manticore->x, y = manticore->y, z = manticore->z;
    skin(x, y, z, manticore->bones, manticore->weights, boneTransforms(*skeleton, *animation, 0));
    CHECK((manticore->x[0] == 21 && manticore->y[0] == -479 && manticore->z[0] == 1289));
    CHECK((x[0] == 44 && y[0] == -790 && z[0] == 237));
}

TEST_CASE("a classic frame moves its skeleton's transform groups as RuneLite's FrameLoader reads them") {
    if (store() == nullptr) return;
    // The player's stand (sequence 808), its first frame; RuneLite's values
    // from CacheMapDumper's render export.
    const std::vector<SequenceDefinition> sequences = loadSequenceDefinitions(*store());
    const SequenceDefinition& stand = byId(sequences, 808);
    REQUIRE(stand.frameIds.size() == stand.frameLengths.size());
    CHECK(stand.frameIds[0] == 208928769);
    CHECK(stand.frameIds[1] == 208928770);
    CHECK(stand.frameLengths[0] == 21);
    CHECK_FALSE(stand.frameStep.has_value());
    CHECK(stand.maxLoops == 99);
    const std::map<std::int32_t, Frame> frames = loadFrames(*store(), stand.frameIds[0] >> 16);
    CHECK(frames.size() == 12);
    REQUIRE(frames.contains(208928769));
    const Frame& frame = frames.at(208928769);
    CHECK(frame.skeleton == 0);
    REQUIRE(frame.transforms.size() == 31);
    const auto is = [](const FrameTransform& t, int group, int x, int y, int z) {
        return t.group == group && t.x == x && t.y == y && t.z == z;
    };
    CHECK(is(frame.transforms[0], 0, 0, 0, 0));
    CHECK(is(frame.transforms[1], 1, 0, 2, 0));
    CHECK(is(frame.transforms[2], 5, 0, 3, -3));
    CHECK(is(frame.transforms[3], 8, -8, 0, 0));

    CHECK(loadFrames(*store(), 0x7FFF).empty());
}

TEST_CASE("how NPCs, worn items, kits and spot animations look, with models of 2 and 4 byte ids") {
    if (store() == nullptr) return;
    const std::vector<NpcDefinition> npcs = loadNpcDefinitions(*store());
    const NpcDefinition& jad = byId(npcs, 3127);
    CHECK(jad.models == std::vector<std::int32_t>{9319});
    CHECK(jad.standAnimation == 2650);
    CHECK(jad.walkAnimation == 2651);
    CHECK_FALSE(jad.runAnimation.has_value());
    CHECK(jad.ambient == 60);
    CHECK(byId(npcs, 7700).widthScale == 110); // the Inferno's Jad, drawn smaller

    const std::vector<ItemDefinition> items = loadItemDefinitions(*store());
    // The whip's worn model is one of the newer 4-byte ids (opcode 45).
    CHECK(byId(items, 4151).maleModels[0] == 5409);
    CHECK_FALSE(byId(items, 4151).maleModels[1].has_value());
    // Recolours.
    const ItemDefinition& secateurs = byId(items, 11711);
    CHECK(secateurs.maleModels[0] == 7126);
    CHECK(secateurs.recolours.find == std::vector<std::uint16_t>{61, 41, 5772});
    CHECK(secateurs.recolours.replace == std::vector<std::uint16_t>{22445, 22422, 22439});
    // A bought copy, as of a bond, offers Discard in place of Drop.
    const ItemDefinition& bond = byId(items, 13191);
    REQUIRE(bond.boughtTemplate.has_value());
    CHECK(bond.name == "Old school bond");
    CHECK(bond.inventoryOptions[4] == "Discard");

    const std::vector<KitDefinition> kits = loadKitDefinitions(*store());
    CHECK(byId(kits, 0).bodyPart == 0);
    CHECK(byId(kits, 0).models == std::vector<std::int32_t>{230});
    CHECK(byId(kits, 0).selectable);

    const std::vector<SpotAnimDefinition> spotAnims = loadSpotAnimDefinitions(*store());
    const SpotAnimDefinition& spotAnim = byId(spotAnims, 709);
    CHECK(spotAnim.model == 15836);
    CHECK(spotAnim.animation == 4252);
    CHECK(spotAnim.rotation == 180);
    CHECK(spotAnim.recolours.find == std::vector<std::uint16_t>{20});
    CHECK(spotAnim.recolours.replace == std::vector<std::uint16_t>{414});
}

TEST_CASE("an object's models by shape, and its offsets read signed") {
    if (store() == nullptr) return;
    const std::vector<ObjectDefinition> objects = loadObjectDefinitions(*store());
    // A wall with a model for each shape it can be placed as.
    const ObjectDefinition& wall = byId(objects, 85);
    CHECK(wall.models == std::vector<std::int32_t>{2214, 46749, 4873, 46750, 16239, 2215, 16238});
    CHECK(wall.modelShapes == std::vector<std::uint8_t>{0, 1, 2, 3, 4, 9, 22});
    // An Inferno pillar sits 1 unit up, where RuneLite reads 65535 units down.
    const ObjectDefinition& pillar = byId(objects, 30284);
    CHECK(pillar.models == std::vector<std::int32_t>{33044});
    CHECK(pillar.modelShapes.empty());
    CHECK(pillar.offsetHeight == -1);
    CHECK(byId(objects, 11847).ambient == -30);
    CHECK(byId(objects, 15).contrast == 125);
}

TEST_CASE("a region's terrain: heights from the cache, from the plane below and from the client's noise") {
    if (store() == nullptr) return;
    const std::optional<Region> caves = loadRegion(*store(), 9551);
    REQUIRE(caves.has_value());
    const TerrainTile& corner = caves->tile(0, 0, 0);
    CHECK(corner.height == -480);
    CHECK(corner.underlay == 72);
    CHECK(corner.settings == 1);
    // Plane 1 gives no height there, so it lies 240 above plane 0.
    CHECK(caves->tile(1, 0, 0).height == -720);
    const TerrainTile& edge = caves->tile(0, 1, 9);
    CHECK(edge.overlay == 19);
    CHECK(edge.overlayShape == 1);
    CHECK(edge.overlayRotation == 2);
    REQUIRE_FALSE(caves->locations.empty());
    const Location& first = caves->locations.front();
    CHECK((first.object == 664 && first.shape == 10 && first.rotation == 2 && first.plane == 0 && first.x == 6 && first.y == 40));

    // Lumbridge's fields give no height on this tile: the noise does, as
    // RuneLite's HeightCalc computes it.
    const std::optional<Region> fields = loadRegion(*store(), 12851);
    REQUIRE(fields.has_value());
    CHECK(fields->tile(0, 0, 18).height == -176);

    CHECK_FALSE(loadRegion(*store(), 0).has_value());
}

TEST_CASE("floor colours as RuneLite's calculateHsl derives them, packed as the client packs them") {
    if (store() == nullptr) return;
    const std::vector<UnderlayDefinition> underlays = loadUnderlayDefinitions(*store());
    const UnderlayDefinition& grass = byId(underlays, 47);
    CHECK(grass.rgb == 0x35720A);
    CHECK((grass.hsl.hue == 27 && grass.hsl.saturation == 214 && grass.hsl.lightness == 62));
    CHECK(grass.hueMultiplier == 104);

    const std::vector<OverlayDefinition> overlays = loadOverlayDefinitions(*store());
    const OverlayDefinition& path = byId(overlays, 13);
    CHECK(path.rgb == 0x3D2B0B);
    CHECK((path.hsl.hue == 27 && path.hsl.saturation == 177 && path.hsl.lightness == 36));
    CHECK(packHsl(path.hsl) == 6802);
    CHECK(byId(overlays, 18).texture == 31); // the Fight Caves' lava
}

TEST_CASE("sprites, a font's metrics, and textures built from sprites") {
    if (store() == nullptr) return;
    const std::optional<std::vector<Sprite>> sprite = loadSprites(*store(), 0);
    REQUIRE(sprite.has_value());
    REQUIRE(sprite->size() == 1);
    const Sprite& frame = sprite->front();
    CHECK((frame.offsetX == 7 && frame.offsetY == 11 && frame.width == 26 && frame.height == 18));
    CHECK((frame.maxWidth == 40 && frame.maxHeight == 40));
    CHECK(frame.pixels[0] == 0);           // palette index 0: transparent
    CHECK(frame.pixels[12] == 0xFF000001); // black, kept apart from transparent
    CHECK_FALSE(loadSprites(*store(), 0x7FFF).has_value());

    // As RuneLite decodes it, a sprite's alpha plane doesn't make its coloured
    // pixels see-through.
    const std::optional<std::vector<Sprite>> thumbsUp = loadSprites(*store(), 880);
    REQUIRE(thumbsUp.has_value());
    for (const std::uint32_t pixel : thumbsUp->front().pixels) CHECK(((pixel & 0xFFFFFF) == 0 || pixel >> 24 == 0xFF));

    // p11_full: its glyphs are sprite 494's frames, one per character.
    const std::optional<Font> font = loadFont(*store(), 494);
    REQUIRE(font.has_value());
    CHECK(font->ascent == 10);
    CHECK(font->advances['A'] == 7);
    CHECK(font->advances[' '] == 3);
    const std::optional<std::vector<Sprite>> glyphs = loadSprites(*store(), 494);
    REQUIRE(glyphs.has_value());
    REQUIRE(glyphs->size() == 256);
    CHECK(((*glyphs)['A'].width == 5 && (*glyphs)['A'].height == 8));

    const std::vector<TextureDefinition> textures = loadTextureDefinitions(*store());
    const TextureDefinition& texture = byId(textures, 1);
    CHECK(texture.sprite == 448);
    CHECK(texture.averageHsl == 39103);
    CHECK(texture.opaque);
    const std::optional<std::vector<std::uint32_t>> pixels = texturePixels(*store(), texture, 0.8);
    REQUIRE(pixels.has_value());
    REQUIRE(pixels->size() == 128 * 128);
    CHECK((*pixels)[0] == 0x7E95AB);
    const TextureDefinition& scrolling = byId(textures, 17);
    CHECK((scrolling.animationDirection == 1 && scrolling.animationSpeed == 2));
    CHECK_FALSE(scrolling.opaque);
}

TEST_CASE("a hitsplat turns into another by a varbit, and a health bar's sprites and scale") {
    if (store() == nullptr) return;
    const std::vector<HitsplatDefinition> hitsplats = loadHitsplatDefinitions(*store());
    // RuneLite's HitsplatID.DAMAGE_ME: the red splat, by varbit 10236's value.
    const HitsplatDefinition& damageMe = byId(hitsplats, 16);
    CHECK(damageMe.transformVarbit == 10236);
    CHECK_FALSE(damageMe.transformVarp.has_value());
    CHECK(damageMe.transforms == std::vector<std::int32_t>{28, 28, 28});
    CHECK(byId(hitsplats, 13).transforms == std::vector<std::int32_t>{27, 26, 27});
    const HitsplatDefinition& splat = byId(hitsplats, 28);
    CHECK(splat.backgroundSprite == 1359);
    CHECK_FALSE(splat.leftSprite.has_value());
    CHECK(splat.format == "%1");
    CHECK(splat.displayCycles == 50);
    CHECK(splat.textOffsetY == 1);
    CHECK(splat.textColour == 0xFFFFFF);

    const std::vector<HealthBarDefinition> bars = loadHealthBarDefinitions(*store());
    const HealthBarDefinition& bar = byId(bars, 0);
    CHECK(bar.frontSprite == 2176);
    CHECK(bar.backSprite == 2177);
    CHECK(bar.healthScale == 30);
    CHECK_FALSE(bar.field3283.has_value());
    CHECK(byId(bars, 1).field3283 == 280);
    CHECK(byId(bars, 1).healthScale == 50);
}
