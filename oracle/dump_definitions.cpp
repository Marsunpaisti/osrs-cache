// The config definitions of osrs/cache/definitions.h, as RuneLite's
// definitions/*Definition classes hold them.

#include "lines.h"

#include <osrs/cache/definitions.h>

#include <map>

namespace osrs::cache::oracle {

namespace {

// A value RuneLite holds as -1 when the definition has none.
template <class T>
void orMinusOne(Lines& out, std::string_view path, const std::optional<T>& value) {
    out.integer(path, value ? static_cast<std::int64_t>(*value) : -1);
}

// A short[] RuneLite fills with (short) of each unsigned value; an empty one
// stands for its null.
void shorts(Lines& out, std::string_view path, const std::vector<std::uint16_t>& values) {
    if (values.empty()) return;
    std::vector<std::int16_t> signedValues(values.begin(), values.end());
    out.integers(path, signedValues);
}

// A primitive array; an empty one stands for RuneLite's null.
template <class T>
void array(Lines& out, std::string_view path, const std::vector<T>& values) {
    if (!values.empty()) out.integers(path, values);
}

// RuneLite's fullRecolor: (short) of the cache's value, -1 without one.
void fullRecolor(Lines& out, const std::optional<std::uint16_t>& value) {
    out.integer("fullRecolor", static_cast<std::int16_t>(value.value_or(0xFFFF)));
}

void swaps(Lines& out, std::string_view find, std::string_view replace, const Swaps& swaps) {
    shorts(out, find, swaps.find);
    shorts(out, replace, swaps.replace);
}

void params(Lines& out, const Params& params) {
    for (const auto& [id, value] : params) {
        const std::string path = entry("params", std::to_string(id));
        if (const auto* number = std::get_if<std::int64_t>(&value)) {
            out.integer(path, *number);
        } else {
            out.string(path, std::get<std::string>(value));
        }
    }
}

void conditionalOp(Lines& out, std::string_view path, const EntityOps::ConditionalOp& op) {
    out.string(field(path, "text"), op.text);
    out.integer(field(path, "varpID"), op.varp);
    out.integer(field(path, "varbitID"), op.varbit);
    out.integer(field(path, "minValue"), op.min);
    out.integer(field(path, "maxValue"), op.max);
}

// RuneLite's EntityOpsDefinition: lists, with null for an option left out.
void entityOps(Lines& out, std::string_view path, const EntityOps& ops) {
    for (std::size_t i = 0; i < ops.ops.size(); ++i) {
        if (ops.ops[i]) out.string(field(at(field(path, "ops"), i), "text"), *ops.ops[i]);
    }
    for (std::size_t i = 0; i < ops.subOps.size(); ++i) {
        for (std::size_t j = 0; j < ops.subOps[i].size(); ++j) {
            const std::string element = at(at(field(path, "subOps"), i), j);
            out.string(field(element, "text"), ops.subOps[i][j].text);
            out.integer(field(element, "subID"), ops.subOps[i][j].subId);
        }
    }
    for (std::size_t i = 0; i < ops.conditionalOps.size(); ++i) {
        for (std::size_t j = 0; j < ops.conditionalOps[i].size(); ++j) {
            conditionalOp(out, at(at(field(path, "conditionalOps"), i), j), ops.conditionalOps[i][j]);
        }
    }
    for (std::size_t i = 0; i < ops.conditionalSubOps.size(); ++i) {
        for (const auto& [subId, list] : ops.conditionalSubOps[i]) {
            for (std::size_t j = 0; j < list.size(); ++j) {
                const std::string element = at(entry(at(field(path, "conditionalSubOps"), i), std::to_string(subId)), j);
                conditionalOp(out, element, list[j].op);
                out.integer(field(element, "subID"), list[j].subId);
            }
        }
    }
}

// RuneLite's transform fields: the varbit and varp, -1 for none, and the
// array, null when the definition doesn't transform.
void transforms(Lines& out, std::string_view varbit, std::string_view varp, std::string_view into,
                const std::optional<std::int32_t>& varbitId, const std::optional<std::int32_t>& varpId, const std::vector<std::int32_t>& ids) {
    orMinusOne(out, varbit, varbitId);
    orMinusOne(out, varp, varpId);
    array(out, into, ids);
}

void objects(const Store& store, Lines& out) {
    for (const ObjectDefinition& object : loadObjectDefinitions(store)) {
        out.object({object.id});
        if (out.skipping()) continue;
        out.integer("id", object.id);
        out.string("name", object.name);
        out.integer("sizeX", object.sizeX);
        out.integer("sizeY", object.sizeY);
        out.integer("interactType", object.interactType);
        out.boolean("blocksProjectile", object.blocksProjectile);
        out.boolean("isHollow", object.hollow);
        out.integer("blockingMask", object.blockingMask);
        out.integer("wallOrDoor", object.wallOrDoor);
        out.integer("supportsItems", object.supportsItems);
        entityOps(out, "ops", object.ops);
        array(out, "objectModels", object.models);
        array(out, "objectTypes", object.modelShapes);
        out.integer("modelSizeX", object.modelSizeX);
        out.integer("modelSizeHeight", object.modelSizeHeight);
        out.integer("modelSizeY", object.modelSizeY);
        // RuneLite reads the offsets unsigned, osrs_cache signed.
        out.integer("offsetX", static_cast<std::uint16_t>(object.offsetX));
        out.integer("offsetHeight", static_cast<std::uint16_t>(object.offsetHeight));
        out.integer("offsetY", static_cast<std::uint16_t>(object.offsetY));
        out.boolean("isRotated", object.mirrored);
        orMinusOne(out, "animationID", object.animation);
        out.boolean("randomizeAnimStart", object.randomizeAnimStart);
        out.boolean("deferAnimChange", object.deferAnimChange);
        out.integer("ambient", object.ambient);
        out.integer("contrast", object.contrast);
        swaps(out, "recolorToFind", "recolorToReplace", object.recolours);
        swaps(out, "retextureToFind", "textureToReplace", object.retextures);
        fullRecolor(out, object.fullRecolor);
        out.integer("decorDisplacement", object.decorDisplacement);
        out.boolean("mergeNormals", object.mergeNormals);
        out.boolean("modelClipped", object.modelClipped);
        out.boolean("shadow", object.shadow);
        out.boolean("obstructsGround", object.obstructsGround);
        orMinusOne(out, "contouredGround", object.contouredGround);
        out.integer("raise", object.raise);
        orMinusOne(out, "mapAreaId", object.mapAreaId);
        orMinusOne(out, "mapSceneID", object.mapSceneId);
        out.integer("category", object.category);
        transforms(out, "varbitID", "varpID", "configChangeDest", object.transformVarbit, object.transformVarp, object.transforms);
        orMinusOne(out, "ambientSoundId", object.ambientSoundId);
        array(out, "ambientSoundIds", object.ambientSoundIds);
        out.integer("ambientSoundChangeTicksMin", object.ambientSoundChangeTicksMin);
        out.integer("ambientSoundChangeTicksMax", object.ambientSoundChangeTicksMax);
        out.integer("ambientSoundDistance", object.ambientSoundDistance);
        out.integer("ambientSoundRetain", object.ambientSoundRetain);
        out.integer("soundDistanceFadeCurve", object.soundDistanceFadeCurve);
        out.integer("soundFadeInCurve", object.soundFadeInCurve);
        out.integer("soundFadeInDuration", object.soundFadeInDuration);
        out.integer("soundFadeOutCurve", object.soundFadeOutCurve);
        out.integer("soundFadeOutDuration", object.soundFadeOutDuration);
        out.integer("soundVisibility", object.soundVisibility);
        out.boolean("unknown1", object.unknown1);
        // A field RuneLite never sets.
        out.integer("objectID", 0);
        params(out, object.params);
    }
}

void items(const Store& store, Lines& out) {
    for (const ItemDefinition& item : loadItemDefinitions(store)) {
        out.object({item.id});
        if (out.skipping()) continue;
        out.integer("id", item.id);
        out.string("name", item.name);
        if (item.examine) out.string("examine", *item.examine);
        if (item.unknown1) out.string("unknown1", *item.unknown1);
        out.integer("stackable", item.stackable);
        out.integer("cost", item.cost);
        out.boolean("members", item.members);
        out.boolean("tradeable", item.tradeable);
        out.boolean("geTradeable", item.geTradeable);
        out.boolean("bronzeman", item.bronzeman);
        out.integer("weight", item.weight);
        out.integer("category", item.category);
        out.integer("team", item.team);
        orMinusOne(out, "wearPos1", item.wearPos1);
        orMinusOne(out, "wearPos2", item.wearPos2);
        orMinusOne(out, "wearPos3", item.wearPos3);
        orMinusOne(out, "notedID", item.notedId);
        orMinusOne(out, "notedTemplate", item.notedTemplate);
        orMinusOne(out, "boughtId", item.boughtId);
        orMinusOne(out, "boughtTemplateId", item.boughtTemplate);
        orMinusOne(out, "placeholderId", item.placeholderId);
        orMinusOne(out, "placeholderTemplateId", item.placeholderTemplate);
        entityOps(out, "groundOps", item.groundOps);
        for (std::size_t i = 0; i < item.inventoryOptions.size(); ++i) {
            if (item.inventoryOptions[i]) out.string(at("interfaceOptions", i), *item.inventoryOptions[i]);
        }
        for (std::size_t i = 0; i < item.inventorySubOptions.size(); ++i) {
            for (std::size_t j = 0; j < item.inventorySubOptions[i].size(); ++j) {
                if (item.inventorySubOptions[i][j]) out.string(at(at("subops", i), j), *item.inventorySubOptions[i][j]);
            }
        }
        out.integer("shiftClickDropIndex", item.shiftClickDropIndex);
        out.integer("inventoryModel", item.inventoryModel);
        out.integer("resizeX", item.resizeX);
        out.integer("resizeY", item.resizeY);
        out.integer("resizeZ", item.resizeZ);
        out.integer("zoom2d", item.zoom2d);
        out.integer("xan2d", item.xan2d);
        out.integer("yan2d", item.yan2d);
        out.integer("zan2d", item.zan2d);
        out.integer("xOffset2d", item.xOffset2d);
        out.integer("yOffset2d", item.yOffset2d);
        out.integer("ambient", item.ambient);
        out.integer("contrast", item.contrast);
        swaps(out, "colorFind", "colorReplace", item.recolours);
        swaps(out, "textureFind", "textureReplace", item.retextures);
        fullRecolor(out, item.fullRecolor);
        array(out, "countObj", item.countItems);
        array(out, "countCo", item.countSizes);
        orMinusOne(out, "maleModel0", item.maleModels[0]);
        orMinusOne(out, "maleModel1", item.maleModels[1]);
        orMinusOne(out, "maleModel2", item.maleModels[2]);
        out.integer("maleOffset", item.maleOffset);
        orMinusOne(out, "maleHeadModel", item.maleHeadModels[0]);
        orMinusOne(out, "maleHeadModel2", item.maleHeadModels[1]);
        orMinusOne(out, "femaleModel0", item.femaleModels[0]);
        orMinusOne(out, "femaleModel1", item.femaleModels[1]);
        orMinusOne(out, "femaleModel2", item.femaleModels[2]);
        out.integer("femaleOffset", item.femaleOffset);
        orMinusOne(out, "femaleHeadModel", item.femaleHeadModels[0]);
        orMinusOne(out, "femaleHeadModel2", item.femaleHeadModels[1]);
        array(out, "holdingWhitelist", item.holdingWhitelist);
        params(out, item.params);
    }
}

void npcs(const Store& store, Lines& out) {
    for (const NpcDefinition& npc : loadNpcDefinitions(store)) {
        out.object({npc.id});
        if (out.skipping()) continue;
        out.integer("id", npc.id);
        out.string("name", npc.name);
        out.integer("size", npc.size);
        out.integers("stats", std::span<const std::uint16_t>(npc.stats));
        out.integer("combatLevel", npc.combatLevel);
        out.integer("category", npc.category);
        entityOps(out, "ops", npc.ops);
        out.boolean("isInteractable", npc.interactable);
        out.boolean("isMinimapVisible", npc.minimapVisible);
        out.boolean("isFollower", npc.isFollower);
        out.boolean("lowPriorityFollowerOps", npc.lowPriorityFollowerOps);
        transforms(out, "varbitId", "varpIndex", "configs", npc.transformVarbit, npc.transformVarp, npc.transforms);
        array(out, "models", npc.models);
        array(out, "chatheadModels", npc.chatheadModels);
        swaps(out, "recolorToFind", "recolorToReplace", npc.recolours);
        swaps(out, "retextureToFind", "retextureToReplace", npc.retextures);
        fullRecolor(out, npc.fullRecolor);
        out.integer("widthScale", npc.widthScale);
        out.integer("heightScale", npc.heightScale);
        out.integer("ambient", npc.ambient);
        out.integer("contrast", npc.contrast);
        out.integer("renderPriority", npc.renderPriority);
        orMinusOne(out, "height", npc.height);
        out.integer("footprintSize", npc.footprintSize);
        out.boolean("canHideForOverlap", npc.canHideForOverlap);
        out.integer("overlapTintHSL", npc.overlapTintHsl);
        out.boolean("zbuf", npc.zbuf);
        out.boolean("unknown1", npc.unknown1);
        array(out, "headIconArchiveIds", npc.headIconArchiveIds);
        array(out, "headIconSpriteIndex", npc.headIconSpriteIndex);
        orMinusOne(out, "standingAnimation", npc.standAnimation);
        orMinusOne(out, "idleRotateLeftAnimation", npc.idleRotateLeftAnimation);
        orMinusOne(out, "idleRotateRightAnimation", npc.idleRotateRightAnimation);
        orMinusOne(out, "walkingAnimation", npc.walkAnimation);
        orMinusOne(out, "rotate180Animation", npc.rotate180Animation);
        orMinusOne(out, "rotateLeftAnimation", npc.rotateLeftAnimation);
        orMinusOne(out, "rotateRightAnimation", npc.rotateRightAnimation);
        orMinusOne(out, "runAnimation", npc.runAnimation);
        orMinusOne(out, "runRotate180Animation", npc.runRotate180Animation);
        orMinusOne(out, "runRotateLeftAnimation", npc.runRotateLeftAnimation);
        orMinusOne(out, "runRotateRightAnimation", npc.runRotateRightAnimation);
        orMinusOne(out, "crawlAnimation", npc.crawlAnimation);
        orMinusOne(out, "crawlRotate180Animation", npc.crawlRotate180Animation);
        orMinusOne(out, "crawlRotateLeftAnimation", npc.crawlRotateLeftAnimation);
        orMinusOne(out, "crawlRotateRightAnimation", npc.crawlRotateRightAnimation);
        out.boolean("idleAnimRestart", npc.idleAnimRestart);
        out.integer("rotationSpeed", npc.rotationSpeed);
        out.boolean("rotationFlag", npc.rotationFlag);
        params(out, npc.params);
    }
}

void sequences(const Store& store, Lines& out) {
    for (const SequenceDefinition& sequence : loadSequenceDefinitions(store)) {
        out.object({sequence.id});
        if (out.skipping()) continue;
        out.integer("id", sequence.id);
        if (sequence.debugName) out.string("debugName", *sequence.debugName);
        // Never null in RuneLite, as every sequence the cache holds has
        // opcode 1, a skeletal one with no frames.
        out.integers("frameIDs", sequence.frameIds);
        out.integers("frameLengths", sequence.frameLengths);
        array(out, "chatFrameIds", sequence.chatFrameIds);
        orMinusOne(out, "frameStep", sequence.frameStep);
        out.integer("maxLoops", sequence.maxLoops);
        array(out, "interleaveLeave", sequence.interleaveLeave);
        out.boolean("stretches", sequence.stretches);
        out.integer("forcedPriority", sequence.forcedPriority);
        orMinusOne(out, "precedenceAnimating", sequence.precedenceAnimating);
        orMinusOne(out, "priority", sequence.priority);
        out.integer("replyMode", sequence.replyMode);
        orMinusOne(out, "leftHandItem", sequence.leftHandItem);
        orMinusOne(out, "rightHandItem", sequence.rightHandItem);
        out.integer("verticalOffset", sequence.verticalOffset);
        // A Guava ArrayListMultimap in RuneLite: its fields, its map's lists
        // keeping the sounds it drops as nulls.
        out.integer("frameSounds.expectedValuesPerKey", 3);
        out.integer("frameSounds.totalSize", static_cast<std::int64_t>(sequence.frameSounds.size()));
        std::map<std::uint16_t, std::size_t> perFrame;
        for (const SequenceDefinition::FrameSound& frameSound : sequence.frameSounds) {
            const std::size_t index = perFrame[frameSound.frame]++;
            if (!frameSound.sound) continue;
            const std::string path = at(entry("frameSounds.map", std::to_string(frameSound.frame)), index);
            const SequenceDefinition::Sound& sound = *frameSound.sound;
            out.integer(field(path, "id"), sound.id);
            out.integer(field(path, "loops"), sound.loops);
            out.integer(field(path, "location"), sound.location);
            out.integer(field(path, "retain"), sound.retain);
            orMinusOne(out, field(path, "weight"), sound.weight);
        }
        out.boolean("soundsCrossWorldView", sequence.soundsCrossWorldView);
        orMinusOne(out, "animMayaID", sequence.animMayaId);
        out.integer("animMayaStart", sequence.animMayaStart);
        out.integer("animMayaEnd", sequence.animMayaEnd);
        if (!sequence.animMayaMasks.empty()) out.booleans("animMayaMasks", sequence.animMayaMasks);
    }
}

void kits(const Store& store, Lines& out) {
    for (const KitDefinition& kit : loadKitDefinitions(store)) {
        out.object({kit.id});
        out.integer("id", kit.id);
        orMinusOne(out, "bodyPartId", kit.bodyPart);
        array(out, "models", kit.models);
        std::vector<std::int32_t> chatheadModels;
        for (const auto& model : kit.chatheadModels) chatheadModels.push_back(model.value_or(-1));
        out.integers("chatheadModels", chatheadModels);
        out.boolean("nonSelectable", !kit.selectable);
        swaps(out, "recolorToFind", "recolorToReplace", kit.recolours);
        swaps(out, "retextureToFind", "retextureToReplace", kit.retextures);
    }
}

void spotAnims(const Store& store, Lines& out) {
    for (const SpotAnimDefinition& spotAnim : loadSpotAnimDefinitions(store)) {
        out.object({spotAnim.id});
        out.integer("id", spotAnim.id);
        if (spotAnim.debugName) out.string("debugName", *spotAnim.debugName);
        // RuneLite's modelId is 0 without one.
        out.integer("modelId", spotAnim.model.value_or(0));
        orMinusOne(out, "animationId", spotAnim.animation);
        out.integer("resizeX", spotAnim.resizeX);
        out.integer("resizeY", spotAnim.resizeY);
        out.integer("rotaton", spotAnim.rotation);
        out.integer("ambient", spotAnim.ambient);
        out.integer("contrast", spotAnim.contrast);
        out.boolean("clickable", spotAnim.clickable);
        swaps(out, "recolorToFind", "recolorToReplace", spotAnim.recolours);
        swaps(out, "textureToFind", "textureToReplace", spotAnim.retextures);
        fullRecolor(out, spotAnim.fullRecolor);
    }
}

void hitsplats(const Store& store, Lines& out) {
    // RuneLite's HitSplatDefinition has no id field.
    for (const HitsplatDefinition& hitsplat : loadHitsplatDefinitions(store)) {
        out.object({hitsplat.id});
        orMinusOne(out, "fontType", hitsplat.font);
        out.integer("textColor", hitsplat.textColour);
        orMinusOne(out, "leftSprite", hitsplat.leftSprite);
        orMinusOne(out, "leftSprite2", hitsplat.leftSprite2);
        orMinusOne(out, "backgroundSprite", hitsplat.backgroundSprite);
        orMinusOne(out, "rightSpriteId", hitsplat.rightSprite);
        out.integer("scrollToOffsetX", hitsplat.scrollToOffsetX);
        out.integer("scrollToOffsetY", hitsplat.scrollToOffsetY);
        out.integer("textOffsetY", hitsplat.textOffsetY);
        out.string("stringFormat", hitsplat.format);
        out.integer("displayCycles", hitsplat.displayCycles);
        orMinusOne(out, "fadeStartCycle", hitsplat.fadeStartCycle);
        orMinusOne(out, "useDamage", hitsplat.useDamage);
        transforms(out, "varbitID", "varpID", "multihitsplats", hitsplat.transformVarbit, hitsplat.transformVarp, hitsplat.transforms);
    }
}

void healthBars(const Store& store, Lines& out) {
    for (const HealthBarDefinition& bar : loadHealthBarDefinitions(store)) {
        out.object({bar.id});
        out.integer("id", bar.id);
        orMinusOne(out, "healthBarFrontSpriteId", bar.frontSprite);
        orMinusOne(out, "healthBarBackSpriteId", bar.backSprite);
        out.integer("healthScale", bar.healthScale);
        out.integer("healthBarPadding", bar.padding);
        out.integer("field3277", bar.field3277);
        out.integer("field3278", bar.field3278);
        orMinusOne(out, "field3283", bar.field3283);
        out.integer("field3275", bar.field3275);
        // Fields RuneLite never sets: its loader reads opcodes 1 and 6 without
        // keeping them.
        out.integer("field3276", 0);
        out.integer("field3272", 1);
    }
}

void underlays(const Store& store, Lines& out) {
    for (const UnderlayDefinition& underlay : loadUnderlayDefinitions(store)) {
        out.object({underlay.id});
        out.integer("id", underlay.id);
        out.integer("color", static_cast<std::int32_t>(underlay.rgb));
        out.integer("hue", underlay.hsl.hue);
        out.integer("saturation", underlay.hsl.saturation);
        out.integer("lightness", underlay.hsl.lightness);
        out.integer("hueMultiplier", underlay.hueMultiplier);
    }
}

void overlays(const Store& store, Lines& out) {
    for (const OverlayDefinition& overlay : loadOverlayDefinitions(store)) {
        out.object({overlay.id});
        out.integer("id", overlay.id);
        out.integer("rgbColor", overlay.rgb);
        orMinusOne(out, "texture", overlay.texture);
        out.integer("secondaryRgbColor", overlay.secondaryRgb ? static_cast<std::int64_t>(*overlay.secondaryRgb) : -1);
        out.boolean("hideUnderlay", overlay.hideUnderlay);
        out.integer("hue", overlay.hsl.hue);
        out.integer("saturation", overlay.hsl.saturation);
        out.integer("lightness", overlay.hsl.lightness);
        // 0 without a secondary colour, as RuneLite leaves them.
        const Hsl other = overlay.secondaryHsl.value_or(Hsl{});
        out.integer("otherHue", other.hue);
        out.integer("otherSaturation", other.saturation);
        out.integer("otherLightness", other.lightness);
    }
}

const Type object("object", objects);
const Type item("item", items);
const Type npc("npc", npcs);
const Type sequence("sequence", sequences);
const Type kit("kit", kits);
const Type spotanim("spotanim", spotAnims);
const Type hitsplat("hitsplat", hitsplats);
const Type healthbar("healthbar", healthBars);
const Type underlay("underlay", underlays);
const Type overlay("overlay", overlays);

} // namespace

} // namespace osrs::cache::oracle
