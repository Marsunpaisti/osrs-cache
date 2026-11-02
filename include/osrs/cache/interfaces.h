#pragma once

// Interface components (index 3), decoded as RuneLite's cache library
// decodes them (net.runelite:cache 1.13.5, definitions/loaders/
// InterfaceLoader): each group is an interface, each of its files a
// component, in the client's older format (IF1) or its current one (IF3).
// Fields keep RuneLite's InterfaceDefinition names.

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

// An argument of a listener: the script id first, then the script's
// arguments. Nothing for an argument of a type RuneLite's loader doesn't
// know, which it leaves null.
using ListenerArgument = std::variant<std::monostate, std::int32_t, std::string>;

// RuneLite's ClientScript1Instruction.Opcode, in its order: the cache stores
// the index.
enum class ClientScript1Opcode : std::uint8_t {
    Return,
    BoostedSkillLevels,
    RealSkillLevels,
    SkillExperience,
    WidgetContainsItemGetQuantity,
    Varp,
    ExperienceAtLevelForSkill,
    VarpTimes469,
    CombatLevel,
    TotalLevel,
    WidgetContainsItemStar,
    RunEnergy,
    Weight,
    VarpTestbit,
    Varbit,
    Minus,
    Div,
    Mul,
    WorldX,
    WorldY,
    Constant,
};

// An instruction of an IF1 component's scripts, with as many operands as its
// opcode takes; 0 for those past the end of the script.
struct ClientScript1Instruction {
    ClientScript1Opcode opcode = ClientScript1Opcode::Return;
    std::vector<std::int32_t> operands;
};

struct InterfaceDefinition {
    // The interface in the high 16 bits, the component's file in the low 16.
    std::int32_t id = -1;
    bool isIf3 = false;
    std::uint8_t type = 0;
    std::uint16_t contentType = 0;
    std::int16_t originalX = 0;
    std::int16_t originalY = 0;
    std::uint16_t originalWidth = 0;
    // Signed for a line (type 9), unsigned otherwise.
    std::int32_t originalHeight = 0;
    std::int8_t widthMode = 0;
    std::int8_t heightMode = 0;
    std::int8_t xPositionMode = 0;
    std::int8_t yPositionMode = 0;
    // The parent component's id, in the same interface; -1 for none.
    std::int32_t parentId = -1;
    bool isHidden = false;
    std::uint16_t scrollWidth = 0;
    std::uint16_t scrollHeight = 0;
    bool noClickThrough = false;
    std::int32_t spriteId = -1;
    std::uint16_t textureId = 0;
    bool spriteTiling = false;
    std::uint8_t opacity = 0;
    std::uint8_t borderType = 0;
    std::int32_t shadowColor = 0;
    bool flippedVertically = false;
    bool flippedHorizontally = false;
    std::uint8_t modelType = 1;
    std::int32_t modelId = -1;
    std::int16_t offsetX2d = 0;
    std::int16_t offsetY2d = 0;
    std::uint16_t rotationX = 0;
    std::uint16_t rotationY = 0;
    std::uint16_t rotationZ = 0;
    std::uint16_t modelZoom = 100;
    std::int32_t animation = -1;
    bool orthogonal = false;
    std::uint16_t modelHeightOverride = 0;
    std::int32_t fontId = -1;
    std::string text;
    std::uint8_t lineHeight = 0;
    std::uint8_t xTextAlignment = 0;
    std::uint8_t yTextAlignment = 0;
    bool textShadowed = false;
    std::int32_t textColor = 0;
    bool filled = false;
    std::uint8_t lineWidth = 1;
    bool lineDirection = false;
    std::int32_t clickMask = 0;
    std::string name;
    // Empty when the component has none, which RuneLite leaves null.
    std::vector<std::string> actions;
    std::uint8_t dragDeadZone = 0;
    std::uint8_t dragDeadTime = 0;
    bool dragRenderBehavior = false;
    std::string targetVerb;

    // The scripts each event runs (IF3). Empty when the component has none
    // for it, which RuneLite leaves null.
    std::vector<ListenerArgument> onLoadListener;
    std::vector<ListenerArgument> onMouseOverListener;
    std::vector<ListenerArgument> onMouseLeaveListener;
    std::vector<ListenerArgument> onTargetLeaveListener;
    std::vector<ListenerArgument> onTargetEnterListener;
    std::vector<ListenerArgument> onVarTransmitListener;
    std::vector<ListenerArgument> onInvTransmitListener;
    std::vector<ListenerArgument> onStatTransmitListener;
    std::vector<ListenerArgument> onTimerListener;
    std::vector<ListenerArgument> onOpListener;
    std::vector<ListenerArgument> onMouseRepeatListener;
    std::vector<ListenerArgument> onClickListener;
    std::vector<ListenerArgument> onClickRepeatListener;
    std::vector<ListenerArgument> onReleaseListener;
    std::vector<ListenerArgument> onHoldListener;
    std::vector<ListenerArgument> onDragListener;
    std::vector<ListenerArgument> onDragCompleteListener;
    std::vector<ListenerArgument> onScrollWheelListener;
    // The varps, inventories and stats whose changes run the transmit
    // listeners. Empty when none, which RuneLite leaves null.
    std::vector<std::int32_t> varTransmitTriggers;
    std::vector<std::int32_t> invTransmitTriggers;
    std::vector<std::int32_t> statTransmitTriggers;
    bool hasListener = false;

    // The fields below are IF1's.
    std::uint8_t menuType = 0;
    // The sibling component that shows hovered when this one is; -1 for
    // none, 0 for an IF3 component.
    std::int32_t hoveredSiblingId = 0;
    // The comparison each of clientScripts' results makes against its
    // alternateRhs. Empty when none, which RuneLite leaves null.
    std::vector<std::uint8_t> alternateOperators;
    std::vector<std::uint16_t> alternateRhs;
    // Empty when the component has no scripts, which RuneLite leaves null;
    // a script of no instructions is empty, which RuneLite leaves null too.
    std::vector<std::vector<ClientScript1Instruction>> clientScripts;
    // Width times height zeros for an inventory (type 2) or an item list
    // (type 7), which the client fills; empty for other types, which
    // RuneLite leaves null.
    std::vector<std::int32_t> itemIds;
    std::vector<std::int32_t> itemQuantities;
    std::int16_t xPitch = 0;
    std::int16_t yPitch = 0;
    // An inventory's (type 2) 20 slot sprites and their offsets, -1 for no
    // sprite; empty for other types, which RuneLite leaves null.
    std::vector<std::int16_t> xOffsets;
    std::vector<std::int16_t> yOffsets;
    std::vector<std::int32_t> sprites;
    // An inventory's or item list's options; nothing for an option it leaves
    // out.
    std::array<std::optional<std::string>, 5> configActions;
    std::string alternateText;
    std::int32_t alternateTextColor = 0;
    std::int32_t hoveredTextColor = 0;
    std::int32_t alternateHoveredTextColor = 0;
    std::int32_t alternateSpriteId = -1;
    std::int32_t alternateModelId = -1;
    std::int32_t alternateAnimation = -1;
    std::string spellName;
    std::string tooltip = "Ok";
};

// Decodes the component `id`. `indexRevision` is the interfaces index's
// revision (IndexInfo::revision), which picks the format as
// InterfaceLoader.configureForRevision does: after 1773137432, revision
// 237's, with 32-bit model ids. Throws std::runtime_error if it doesn't
// decode.
InterfaceDefinition decodeInterface(std::int32_t id, std::span<const std::uint8_t> data, std::int32_t indexRevision);

// An interface's components by file id. Nothing if the cache has no such
// interface. Throws std::runtime_error if one doesn't decode, unless the
// store skips such objects (store.h).
std::optional<std::map<int, InterfaceDefinition>> loadInterface(const Store& store, int group);

} // namespace osrs::cache
