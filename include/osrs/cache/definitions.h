#pragma once

// Definitions from the cache's configs index, decoded as RuneLite's loaders
// decode them (net.runelite:cache 1.13.5, definitions/loaders), keeping every
// field RuneLite's definitions hold. NPCs and objects load in the format of
// their config group's revision, as NpcManager and ObjectManager configure
// RuneLite's loaders; sequences in the current format, as RuneLite's
// SequenceLoader decodes them unless configured.
//
// Departure: an opcode the decoder doesn't know fails the definition, since
// what follows it can't be read: the load throws or leaves the definition
// out, as the store's options say (store.h, Undecodable). RuneLite's loaders
// skip it (some log it) and read the next byte as an opcode.

#include <osrs/cache/store.h>

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace osrs::cache {

// A menu's five options, in order; nothing for an option the definition
// leaves out.
using MenuOptions = std::array<std::optional<std::string>, 5>;

// A definition's params by param id: a number or a string.
using Params = std::map<std::int32_t, std::variant<std::int64_t, std::string>>;

// The param as a number; nothing if it's absent or a string.
std::optional<std::int64_t> numberParam(const Params& params, std::int32_t id);

// Colours (the client's 16-bit HSL) or texture ids a definition swaps in its
// models: each of `find` becomes the `replace` at its index.
struct Swaps {
    std::vector<std::uint16_t> find;
    std::vector<std::uint16_t> replace;
};

// The options of an object, an NPC or an item on the ground, as RuneLite
// 1.13.5's EntityOpsDefinition holds them (decoded by its EntityOpsLoader).
struct EntityOps {
    struct SubOp {
        std::uint16_t subId = 0;
        std::string text;
    };
    // An option's text while a varp or varbit holds a value from `min` to
    // `max`.
    struct ConditionalOp {
        std::uint16_t varp = 0;
        std::uint16_t varbit = 0;
        std::int32_t min = 0;
        std::int32_t max = 0;
        std::string text;
    };
    struct ConditionalSubOp {
        std::uint16_t subId = 0;
        ConditionalOp op;
    };

    // By option index; nothing for an option left out or named "Hidden".
    std::vector<std::optional<std::string>> ops;
    // The rest by option index, each in the cache's order.
    std::vector<std::vector<SubOp>> subOps;
    std::vector<std::vector<ConditionalOp>> conditionalOps;
    std::vector<std::map<std::uint16_t, std::vector<ConditionalSubOp>>> conditionalSubOps;
};

struct ObjectDefinition {
    std::int32_t id = 0;
    std::string name = "null";
    std::uint8_t sizeX = 1;
    std::uint8_t sizeY = 1;
    // 0: doesn't block movement; 1: blocks it, also as floor decoration; 2: blocks it.
    std::uint8_t interactType = 2;
    bool blocksProjectile = true;
    bool hollow = false;
    // RuneLite's blockingMask.
    std::int8_t blockingMask = 0;
    // RuneLite's wallOrDoor: from the cache, or as RuneLite's post derives
    // it, 1 for an object of shape 10 models or with an option.
    std::uint8_t wallOrDoor = 0;
    // RuneLite's supportsItems: from the cache, or 1 if interactType isn't 0.
    std::uint8_t supportsItems = 0;
    EntityOps ops;

    // Its models, and the shape (Location::shape in osrs/cache/map.h) each is
    // for; `modelShapes` is empty when the models together make up shape 10.
    std::vector<std::int32_t> models;
    std::vector<std::uint8_t> modelShapes;
    // Scale along x, height and y, 128 for 1.
    std::uint16_t modelSizeX = 128;
    std::uint16_t modelSizeHeight = 128;
    std::uint16_t modelSizeY = 128;
    // In model units. Signed, as the client and Encounter Replay read them,
    // where RuneLite reads them unsigned.
    std::int16_t offsetX = 0;
    std::int16_t offsetHeight = 0;
    std::int16_t offsetY = 0;
    // RuneLite's isRotated.
    bool mirrored = false;
    std::optional<std::int32_t> animation;
    bool randomizeAnimStart = false;
    bool deferAnimChange = false;
    std::int8_t ambient = 0;
    // RuneLite's value: the cache's times 25.
    std::int16_t contrast = 0;
    Swaps recolours;
    Swaps retextures;
    std::optional<std::uint16_t> fullRecolor;
    // How far wall decoration sits out from its wall.
    std::uint8_t decorDisplacement = 16;
    bool mergeNormals = false;
    bool modelClipped = false;
    bool shadow = true;
    bool obstructsGround = false;
    // RuneLite's contouredGround: 0, or the cache's value times 256.
    std::optional<std::int32_t> contouredGround;
    std::uint8_t raise = 0;
    // The world map's icon and the minimap's map scene for it.
    std::optional<std::int32_t> mapAreaId;
    std::optional<std::int32_t> mapSceneId;
    std::uint16_t category = 0;
    // What it turns into by the value of the varbit or varp:
    // transforms[value], or the last entry for a value outside the others;
    // -1 for none. Empty when it doesn't transform. RuneLite's
    // configChangeDest, varbitID and varpID.
    std::optional<std::int32_t> transformVarbit;
    std::optional<std::int32_t> transformVarp;
    std::vector<std::int32_t> transforms;

    // Its ambient sound, or one of `ambientSoundIds` changed to at random
    // every ambientSoundChangeTicksMin to Max ticks.
    std::optional<std::int32_t> ambientSoundId;
    std::vector<std::uint16_t> ambientSoundIds;
    std::uint16_t ambientSoundChangeTicksMin = 0;
    std::uint16_t ambientSoundChangeTicksMax = 0;
    // In tiles.
    std::uint8_t ambientSoundDistance = 0;
    // Read from the revision 220 format on (RuneLite's rev220SoundData).
    std::uint8_t ambientSoundRetain = 0;
    std::uint8_t soundDistanceFadeCurve = 0;
    std::uint8_t soundFadeInCurve = 0;
    std::uint16_t soundFadeInDuration = 300;
    std::uint8_t soundFadeOutCurve = 0;
    std::uint16_t soundFadeOutDuration = 300;
    std::uint8_t soundVisibility = 2;
    // Opcode 94, which RuneLite leaves unnamed.
    bool unknown1 = false;
    Params params;
};

// As RuneLite's ItemManager.link leaves it: a noted, bought or placeholder
// item takes its look and some of its properties from the items it's made
// from (RuneLite ItemDefinition.linkNote, linkBought and linkPlaceholder).
struct ItemDefinition {
    std::int32_t id = 0;
    std::string name = "null";
    std::optional<std::string> examine;
    // Opcode 9, which RuneLite leaves unnamed.
    std::optional<std::string> unknown1;
    // RuneLite's value: 1 from opcode 11 and for noted items, 2 from opcode 160.
    std::uint8_t stackable = 0;
    // Coins.
    std::int32_t cost = 1;
    bool members = false;
    bool tradeable = true;
    bool geTradeable = false;
    bool bronzeman = false;
    // Grams; 0 for a stackable item.
    std::int16_t weight = 0;
    std::uint16_t category = 0;
    std::uint8_t team = 0;
    std::optional<std::int8_t> wearPos1;
    std::optional<std::int8_t> wearPos2;
    std::optional<std::int8_t> wearPos3;
    std::optional<std::int32_t> notedId;
    std::optional<std::int32_t> notedTemplate;
    std::optional<std::int32_t> boughtId;
    std::optional<std::int32_t> boughtTemplate;
    std::optional<std::int32_t> placeholderId;
    std::optional<std::int32_t> placeholderTemplate;
    // Its options on the ground, Take by default.
    EntityOps groundOps = {{std::nullopt, std::nullopt, "Take"}, {}, {}, {}};
    // Its options in the inventory, before the client's own Use and Examine.
    MenuOptions inventoryOptions = {std::nullopt, std::nullopt, std::nullopt, std::nullopt, "Drop"};
    // Each inventory option's sub-options: none, or twenty, nothing for one
    // left out. RuneLite's subops.
    std::array<std::vector<std::optional<std::string>>, 5> inventorySubOptions;
    // RuneLite's shiftClickDropIndex: -2 unless the cache sets it.
    std::int8_t shiftClickDropIndex = -2;

    // The model it shows in the inventory, and how: its scale along x, y and
    // z (128 for 1), the zoom, angles and offsets it's drawn at.
    std::int32_t inventoryModel = 0;
    std::uint16_t resizeX = 128;
    std::uint16_t resizeY = 128;
    std::uint16_t resizeZ = 128;
    std::uint16_t zoom2d = 2000;
    std::uint16_t xan2d = 0;
    std::uint16_t yan2d = 0;
    std::uint16_t zan2d = 0;
    std::int16_t xOffset2d = 0;
    std::int16_t yOffset2d = 0;
    std::int8_t ambient = 0;
    std::int8_t contrast = 0;
    Swaps recolours;
    Swaps retextures;
    std::optional<std::uint16_t> fullRecolor;
    // The item it shows as at a stack of at least countSizes[i]:
    // countItems[i]. Both empty, or ten long with 0 where the cache sets none
    // (RuneLite's countObj and countCo).
    std::vector<std::uint16_t> countItems;
    std::vector<std::uint16_t> countSizes;

    // The models it shows worn by a male and a female player, with RuneLite's
    // maleOffset and femaleOffset for them, and on their chat heads.
    std::array<std::optional<std::int32_t>, 3> maleModels;
    std::uint8_t maleOffset = 0;
    std::array<std::optional<std::int32_t>, 2> maleHeadModels;
    std::array<std::optional<std::int32_t>, 3> femaleModels;
    std::uint8_t femaleOffset = 0;
    std::array<std::optional<std::int32_t>, 2> femaleHeadModels;
    // Opcode 161, RuneLite's holdingWhitelist.
    std::vector<std::uint16_t> holdingWhitelist;
    Params params;
};

struct NpcDefinition {
    std::int32_t id = 0;
    std::string name = "null";
    std::uint8_t size = 1;
    // Attack, defence, strength, hitpoints, ranged, magic.
    std::array<std::uint16_t, 6> stats = {1, 1, 1, 1, 1, 1};
    std::int32_t combatLevel = -1;
    std::uint16_t category = 0;
    // Its options, before the client's own Examine. One the cache names
    // "Hidden" is left out, as RuneLite leaves it.
    MenuOptions options;
    // The same options, with their sub-options and conditional options.
    EntityOps ops;
    // Whether the mouse finds it at all: a click on one that isn't goes to
    // whatever is behind it.
    bool interactable = true;
    bool minimapVisible = true;
    bool isFollower = false;
    bool lowPriorityFollowerOps = false;
    // As an object's transforms; RuneLite's configs, varbitId and varpIndex.
    std::optional<std::int32_t> transformVarbit;
    std::optional<std::int32_t> transformVarp;
    std::vector<std::int32_t> transforms;

    std::vector<std::int32_t> models;
    std::vector<std::int32_t> chatheadModels;
    Swaps recolours;
    Swaps retextures;
    std::optional<std::uint16_t> fullRecolor;
    // 128 for 1.
    std::uint16_t widthScale = 128;
    std::uint16_t heightScale = 128;
    std::int8_t ambient = 0;
    std::int8_t contrast = 0;
    // 0, 1 from opcode 99, 2 from opcode 111.
    std::uint8_t renderPriority = 0;
    std::optional<std::uint16_t> height;
    // RuneLite's footprintSize: from the cache, or 0.4 of its size in model
    // units, as RuneLite's post computes it.
    std::int32_t footprintSize = 0;
    bool canHideForOverlap = false;
    std::uint16_t overlapTintHsl = 39188;
    bool zbuf = true;
    // Opcode 129, which RuneLite leaves unnamed.
    bool unknown1 = false;
    // Its overhead icons' sprite groups and frames: one for each bit of the
    // cache's bitfield up to its highest set bit, -1 for a bit that isn't set.
    std::vector<std::int32_t> headIconArchiveIds;
    std::vector<std::int16_t> headIconSpriteIndex;

    // Sequences.
    std::optional<std::int32_t> standAnimation;
    std::optional<std::int32_t> idleRotateLeftAnimation;
    std::optional<std::int32_t> idleRotateRightAnimation;
    std::optional<std::int32_t> walkAnimation;
    std::optional<std::int32_t> rotate180Animation;
    std::optional<std::int32_t> rotateLeftAnimation;
    std::optional<std::int32_t> rotateRightAnimation;
    std::optional<std::int32_t> runAnimation;
    std::optional<std::int32_t> runRotate180Animation;
    std::optional<std::int32_t> runRotateLeftAnimation;
    std::optional<std::int32_t> runRotateRightAnimation;
    std::optional<std::int32_t> crawlAnimation;
    std::optional<std::int32_t> crawlRotate180Animation;
    std::optional<std::int32_t> crawlRotateLeftAnimation;
    std::optional<std::int32_t> crawlRotateRightAnimation;
    bool idleAnimRestart = false;
    // Client units per cycle it turns at.
    std::uint16_t rotationSpeed = 32;
    // RuneLite's rotationFlag.
    bool rotationFlag = true;
    Params params;
};

// A part of a player's look that isn't worn equipment.
struct KitDefinition {
    std::int32_t id = 0;
    // 0 to 6: a male's head, jaw, torso, arms, hands, legs and feet; 7 to 13
    // a female's.
    std::optional<std::uint8_t> bodyPart;
    std::vector<std::int32_t> models;
    std::array<std::optional<std::int32_t>, 5> chatheadModels;
    // Whether character creation offers it.
    bool selectable = true;
    Swaps recolours;
    Swaps retextures;
};

// A graphic shown on a tile or an actor, as a spell's splash.
struct SpotAnimDefinition {
    std::int32_t id = 0;
    std::optional<std::string> debugName;
    std::optional<std::int32_t> model;
    // A sequence.
    std::optional<std::int32_t> animation;
    // 128 for 1.
    std::uint16_t resizeX = 128;
    std::uint16_t resizeY = 128;
    // Degrees: 0, 90, 180 or 270.
    std::uint16_t rotation = 0;
    std::uint8_t ambient = 0;
    std::uint8_t contrast = 0;
    bool clickable = true;
    Swaps recolours;
    Swaps retextures;
    std::optional<std::uint16_t> fullRecolor;
};

// How a hitsplat draws. RuneLite's HitsplatID values are its ids. The
// sprite and offset fields keep RuneLite's names; every hitsplat of the
// current cache draws only its backgroundSprite, behind its text.
struct HitsplatDefinition {
    std::int32_t id = 0;
    // A font (loadFont in osrs/cache/sprites.h); nothing for the client's
    // default.
    std::optional<std::int32_t> font;
    std::uint32_t textColour = 0xFFFFFF;
    std::optional<std::int32_t> leftSprite;
    std::optional<std::int32_t> leftSprite2;
    std::optional<std::int32_t> backgroundSprite;
    std::optional<std::int32_t> rightSprite;
    std::int16_t scrollToOffsetX = 0;
    std::int16_t scrollToOffsetY = 0;
    std::int16_t textOffsetY = 0;
    // The text, "%1" standing for the hit's value.
    std::string format;
    // Client cycles of 20 ms.
    std::uint16_t displayCycles = 70;
    std::optional<std::uint16_t> fadeStartCycle;
    std::optional<std::uint8_t> useDamage;
    // When `transforms` isn't empty, the hitsplat this one turns into by the
    // value of the varbit or varp: transforms[value], or the last entry for a
    // value outside the others; -1 for none. RuneLite's multihitsplats.
    std::optional<std::int32_t> transformVarbit;
    std::optional<std::int32_t> transformVarp;
    std::vector<std::int32_t> transforms;
};

// How a health bar draws.
struct HealthBarDefinition {
    std::int32_t id = 0;
    std::optional<std::int32_t> frontSprite;
    std::optional<std::int32_t> backSprite;
    // What a full bar stands for: the health the server sends is out of it
    // (RuneLite's Actor.getHealthScale).
    std::uint8_t healthScale = 30;
    std::uint8_t padding = 0;
    // Fields RuneLite leaves unnamed, by its names: opcodes 2, 3, 4 or 11
    // (4 sets 0), and 5. RuneLite reads opcodes 1 and 6 without keeping them.
    std::uint8_t field3277 = 255;
    std::uint8_t field3278 = 255;
    std::optional<std::uint16_t> field3283;
    std::uint16_t field3275 = 70;
};

// Hue, saturation and lightness, 0 to 255 each, as RuneLite's calculateHsl
// derives them from a colour.
struct Hsl {
    int hue = 0;
    int saturation = 0;
    int lightness = 0;
};

// A floor colour under a tile's overlay, which the client blends with the
// underlays around it.
struct UnderlayDefinition {
    std::int32_t id = 0;
    std::uint32_t rgb = 0;
    // RuneLite UnderlayDefinition.calculateHsl. Its hue is scaled by
    // `hueMultiplier`, the weight the client blends underlays' hues by.
    Hsl hsl;
    int hueMultiplier = 1;
};

// A floor colour or texture covering a tile in one of the overlay shapes.
struct OverlayDefinition {
    std::int32_t id = 0;
    // 0xFF00FF for an overlay the client doesn't draw.
    std::uint32_t rgb = 0;
    std::optional<std::uint8_t> texture;
    bool hideUnderlay = true;
    // RuneLite's secondaryRgbColor.
    std::optional<std::uint32_t> secondaryRgb;
    // RuneLite OverlayDefinition.calculateHsl, of `rgb` and of `secondaryRgb`.
    Hsl hsl;
    std::optional<Hsl> secondaryHsl;
};

struct SequenceDefinition {
    // A sound a sequence plays (RuneLite's SequenceDefinition.Sound).
    struct Sound {
        std::int32_t id = 0;
        std::uint8_t loops = 0;
        std::uint8_t location = 0;
        std::uint8_t retain = 0;
        // Read from the revision 226 format on; nothing before it.
        std::optional<std::uint8_t> weight;
    };
    struct FrameSound {
        std::uint16_t frame = 0;
        // Nothing for one RuneLite drops, with an id or loop count below 1.
        std::optional<Sound> sound;
    };

    std::int32_t id = 0;
    std::optional<std::string> debugName;
    // A classic sequence's frames (loadFrames in osrs/cache/frames.h: the
    // group in the high 16 bits, the file in the low 16), and how long each
    // shows, in client cycles of 20 ms.
    std::vector<std::int32_t> frameIds;
    std::vector<std::uint16_t> frameLengths;
    // The frames of the chat head playing it, as frameIds.
    std::vector<std::int32_t> chatFrameIds;
    // How many frames from the end a looping sequence restarts; nothing when
    // it restarts from the first.
    std::optional<std::uint16_t> frameStep;
    std::uint8_t maxLoops = 99;
    // The skeleton's label groups it animates when the client blends it with
    // a movement sequence, ending in 9999999 (RuneLite's interleaveLeave).
    std::vector<std::int32_t> interleaveLeave;
    bool stretches = false;
    std::uint8_t forcedPriority = 5;
    std::optional<std::uint8_t> precedenceAnimating;
    std::optional<std::uint8_t> priority;
    std::uint8_t replyMode = 2;
    // Items it shows held in place of the player's.
    std::optional<std::int32_t> leftHandItem;
    std::optional<std::int32_t> rightHandItem;
    std::int8_t verticalOffset = 0;
    // The sounds it plays, by the frame they play at, in the cache's order.
    std::vector<FrameSound> frameSounds;
    bool soundsCrossWorldView = false;
    // A skeletal animation and its frame range.
    std::optional<std::int32_t> animMayaId;
    std::uint16_t animMayaStart = 0;
    std::uint16_t animMayaEnd = 0;
    // Which of the skeleton's 256 bones it animates; empty for all.
    std::vector<bool> animMayaMasks;
};

// Every definition of a kind, by ascending id. Throw std::runtime_error if
// the cache lacks the group, or if a definition doesn't decode unless the
// store skips such objects (store.h).
std::vector<ObjectDefinition> loadObjectDefinitions(const Store& store);
std::vector<ItemDefinition> loadItemDefinitions(const Store& store);
std::vector<NpcDefinition> loadNpcDefinitions(const Store& store);
std::vector<SequenceDefinition> loadSequenceDefinitions(const Store& store);
std::vector<KitDefinition> loadKitDefinitions(const Store& store);
std::vector<SpotAnimDefinition> loadSpotAnimDefinitions(const Store& store);
std::vector<UnderlayDefinition> loadUnderlayDefinitions(const Store& store);
std::vector<OverlayDefinition> loadOverlayDefinitions(const Store& store);
std::vector<HitsplatDefinition> loadHitsplatDefinitions(const Store& store);
std::vector<HealthBarDefinition> loadHealthBarDefinitions(const Store& store);

// One definition from its bytes in the configs group, in the format of a
// group of this revision, as RuneLite's loaders' configureForRevision picks
// it: NPC head icons in the revision 210 format from 1493; object ambient
// sounds in the revision 220 format from 1673; sequence frame sounds of 3
// bytes up to 1141, and the revision 226 opcodes and sound weights after
// 1268. Throw std::runtime_error if it doesn't decode.
NpcDefinition decodeNpcDefinition(std::int32_t id, std::span<const std::uint8_t> bytes, std::int32_t revision);
ObjectDefinition decodeObjectDefinition(std::int32_t id, std::span<const std::uint8_t> bytes, std::int32_t revision);
SequenceDefinition decodeSequenceDefinition(std::int32_t id, std::span<const std::uint8_t> bytes, std::int32_t revision);

// The client's packing of a colour into the 16 bits terrain and models carry:
// (hue / 4) << 10 | (saturation / 32) << 7 | lightness / 2, the saturation
// halved at each of the lightnesses above 179, 192, 217 and 243. As
// CacheMapDumper's render export packs terrain colours.
std::uint16_t packHsl(Hsl hsl);

} // namespace osrs::cache
