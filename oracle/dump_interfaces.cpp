// Interface components (osrs/cache/interfaces.h), as RuneLite's
// definitions/InterfaceDefinition holds them.

#include "lines.h"

#include <osrs/cache/interfaces.h>

#include <exception>

namespace osrs::cache::oracle {

namespace {

// RuneLite's ClientScript1Instruction.Opcode constants, in their order.
constexpr const char* kClientScript1Opcodes[] = {
    "RETURN", "BOOSTED_SKILL_LEVELS", "REAL_SKILL_LEVELS", "SKILL_EXPERIENCE", "WIDGET_CONTAINS_ITEM_GET_QUANTITY",
    "VARP", "EXPERIENCE_AT_LEVEL_FOR_SKILL", "VARP_TIMES_469", "COMBAT_LEVEL", "TOTAL_LEVEL",
    "WIDGET_CONTAINS_ITEM_STAR", "RUN_ENERGY", "WEIGHT", "VARP_TESTBIT", "VARBIT",
    "MINUS", "DIV", "MUL", "WORLD_X", "WORLD_Y",
    "CONSTANT",
};

// An Object[] RuneLite leaves null when empty; a null element is written not at all.
void listener(Lines& out, std::string_view path, const std::vector<ListenerArgument>& arguments) {
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        if (const auto* number = std::get_if<std::int32_t>(&arguments[i])) {
            out.integer(at(path, i), *number);
        } else if (const auto* text = std::get_if<std::string>(&arguments[i])) {
            out.string(at(path, i), *text);
        }
    }
}

// An int[] RuneLite leaves null when empty.
template <class T>
void integersOrNull(Lines& out, std::string_view path, const std::vector<T>& values) {
    if (!values.empty()) out.integers(path, values);
}

void component(Lines& out, const InterfaceDefinition& iface) {
    out.integer("id", iface.id);
    out.boolean("isIf3", iface.isIf3);
    out.integer("type", iface.type);
    out.integer("contentType", iface.contentType);
    out.integer("originalX", iface.originalX);
    out.integer("originalY", iface.originalY);
    out.integer("originalWidth", iface.originalWidth);
    out.integer("originalHeight", iface.originalHeight);
    out.integer("widthMode", iface.widthMode);
    out.integer("heightMode", iface.heightMode);
    out.integer("xPositionMode", iface.xPositionMode);
    out.integer("yPositionMode", iface.yPositionMode);
    out.integer("parentId", iface.parentId);
    out.boolean("isHidden", iface.isHidden);
    out.integer("scrollWidth", iface.scrollWidth);
    out.integer("scrollHeight", iface.scrollHeight);
    out.boolean("noClickThrough", iface.noClickThrough);
    out.integer("spriteId", iface.spriteId);
    out.integer("textureId", iface.textureId);
    out.boolean("spriteTiling", iface.spriteTiling);
    out.integer("opacity", iface.opacity);
    out.integer("borderType", iface.borderType);
    out.integer("shadowColor", iface.shadowColor);
    out.boolean("flippedVertically", iface.flippedVertically);
    out.boolean("flippedHorizontally", iface.flippedHorizontally);
    out.integer("modelType", iface.modelType);
    out.integer("modelId", iface.modelId);
    out.integer("offsetX2d", iface.offsetX2d);
    out.integer("offsetY2d", iface.offsetY2d);
    out.integer("rotationX", iface.rotationX);
    out.integer("rotationY", iface.rotationY);
    out.integer("rotationZ", iface.rotationZ);
    out.integer("modelZoom", iface.modelZoom);
    out.integer("animation", iface.animation);
    out.boolean("orthogonal", iface.orthogonal);
    out.integer("modelHeightOverride", iface.modelHeightOverride);
    out.integer("fontId", iface.fontId);
    out.string("text", iface.text);
    out.integer("lineHeight", iface.lineHeight);
    out.integer("xTextAlignment", iface.xTextAlignment);
    out.integer("yTextAlignment", iface.yTextAlignment);
    out.boolean("textShadowed", iface.textShadowed);
    out.integer("textColor", iface.textColor);
    out.boolean("filled", iface.filled);
    out.integer("lineWidth", iface.lineWidth);
    out.boolean("lineDirection", iface.lineDirection);
    out.integer("clickMask", iface.clickMask);
    out.string("name", iface.name);
    for (std::size_t i = 0; i < iface.actions.size(); ++i) out.string(at("actions", i), iface.actions[i]);
    out.integer("dragDeadZone", iface.dragDeadZone);
    out.integer("dragDeadTime", iface.dragDeadTime);
    out.boolean("dragRenderBehavior", iface.dragRenderBehavior);
    out.string("targetVerb", iface.targetVerb);
    listener(out, "onLoadListener", iface.onLoadListener);
    listener(out, "onMouseOverListener", iface.onMouseOverListener);
    listener(out, "onMouseLeaveListener", iface.onMouseLeaveListener);
    listener(out, "onTargetLeaveListener", iface.onTargetLeaveListener);
    listener(out, "onTargetEnterListener", iface.onTargetEnterListener);
    listener(out, "onVarTransmitListener", iface.onVarTransmitListener);
    listener(out, "onInvTransmitListener", iface.onInvTransmitListener);
    listener(out, "onStatTransmitListener", iface.onStatTransmitListener);
    listener(out, "onTimerListener", iface.onTimerListener);
    listener(out, "onOpListener", iface.onOpListener);
    listener(out, "onMouseRepeatListener", iface.onMouseRepeatListener);
    listener(out, "onClickListener", iface.onClickListener);
    listener(out, "onClickRepeatListener", iface.onClickRepeatListener);
    listener(out, "onReleaseListener", iface.onReleaseListener);
    listener(out, "onHoldListener", iface.onHoldListener);
    listener(out, "onDragListener", iface.onDragListener);
    listener(out, "onDragCompleteListener", iface.onDragCompleteListener);
    listener(out, "onScrollWheelListener", iface.onScrollWheelListener);
    integersOrNull(out, "varTransmitTriggers", iface.varTransmitTriggers);
    integersOrNull(out, "invTransmitTriggers", iface.invTransmitTriggers);
    integersOrNull(out, "statTransmitTriggers", iface.statTransmitTriggers);
    out.boolean("hasListener", iface.hasListener);

    out.integer("menuType", iface.menuType);
    out.integer("hoveredSiblingId", iface.hoveredSiblingId);
    integersOrNull(out, "alternateOperators", iface.alternateOperators);
    integersOrNull(out, "alternateRhs", iface.alternateRhs);
    for (std::size_t i = 0; i < iface.clientScripts.size(); ++i) {
        const std::string script = at("clientScripts", i);
        for (std::size_t k = 0; k < iface.clientScripts[i].size(); ++k) {
            const ClientScript1Instruction& instruction = iface.clientScripts[i][k];
            const std::string path = at(script, k);
            out.enumeration(field(path, "opcode"), kClientScript1Opcodes[static_cast<std::size_t>(instruction.opcode)]);
            out.integers(field(path, "operands"), instruction.operands);
        }
    }
    // shortcut: an inventory or item list of no slots is written as null where RuneLite has [];
    // the 2026-11-01 cache has no IF1 inventories or item lists, so make these optional if a
    // later cache has an empty one.
    integersOrNull(out, "itemIds", iface.itemIds);
    integersOrNull(out, "itemQuantities", iface.itemQuantities);
    out.integer("xPitch", iface.xPitch);
    out.integer("yPitch", iface.yPitch);
    integersOrNull(out, "xOffsets", iface.xOffsets);
    integersOrNull(out, "yOffsets", iface.yOffsets);
    integersOrNull(out, "sprites", iface.sprites);
    for (std::size_t i = 0; i < iface.configActions.size(); ++i) {
        if (iface.configActions[i]) out.string(at("configActions", i), *iface.configActions[i]);
    }
    out.string("alternateText", iface.alternateText);
    out.integer("alternateTextColor", iface.alternateTextColor);
    out.integer("hoveredTextColor", iface.hoveredTextColor);
    out.integer("alternateHoveredTextColor", iface.alternateHoveredTextColor);
    out.integer("alternateSpriteId", iface.alternateSpriteId);
    out.integer("alternateModelId", iface.alternateModelId);
    out.integer("alternateAnimation", iface.alternateAnimation);
    out.string("spellName", iface.spellName);
    out.string("tooltip", iface.tooltip);
}

// As RuneLiteOracle.java loads them: each file of each group, keyed by
// both, with the revision InterfaceManager configures InterfaceLoader with.
void interfaces(const Store& store, Lines& out) {
    const IndexInfo* index = store.index(Index::Interfaces);
    if (index == nullptr) return;
    for (const int group : store.groups(Index::Interfaces)) {
        std::optional<std::map<int, std::vector<std::uint8_t>>> files;
        try {
            files = store.files(Index::Interfaces, group);
        } catch (const std::exception&) {
            out.object({group});
            out.error();
            continue;
        }
        if (!files) continue;
        for (const auto& [file, bytes] : *files) {
            out.object({group, file});
            if (out.skipping()) continue;
            try {
                component(out, decodeInterface((group << 16) + file, bytes, index->revision));
            } catch (const std::exception&) {
                out.error();
            }
        }
    }
}

const Type interfaceType("interface", interfaces);

} // namespace

} // namespace osrs::cache::oracle
