#include <osrs/cache/interfaces.h>

#include "buffer.h"
#include "undecodable.h"

#include <iterator>
#include <limits>
#include <stdexcept>

namespace osrs::cache {

namespace {

// ClientScript1Instruction.Opcode's argumentCount, in its order.
constexpr int kClientScript1Arguments[] = {0, 1, 1, 1, 3, 1, 1, 1, 1, 0, 3, 0, 0, 2, 1, 0, 0, 0, 0, 1, 1};

int minusOneFor0xFFFF(int value) { return value == 0xFFFF ? -1 : value; }

// The parent's file id, made the parent's component id in this interface.
std::int32_t parentComponentId(int file, std::int32_t id) {
    return file == 0xFFFF ? -1 : static_cast<std::int32_t>(file + (id & ~0xFFFF));
}

// An IF1 script: each instruction its opcode and as many operands as it
// takes, padded with 0 past the end, as RuneLite's Arrays.copyOfRange pads.
std::vector<ClientScript1Instruction> parseClientScript1(const std::vector<std::int32_t>& bytecode) {
    std::vector<ClientScript1Instruction> instructions;
    for (std::size_t i = 0; i < bytecode.size();) {
        const std::int32_t opcode = bytecode[i++];
        if (opcode < 0 || opcode >= static_cast<std::int32_t>(std::size(kClientScript1Arguments))) {
            throw std::runtime_error("IF1 script has an unknown opcode");
        }
        ClientScript1Instruction& instruction = instructions.emplace_back();
        instruction.opcode = static_cast<ClientScript1Opcode>(opcode);
        const auto arguments = static_cast<std::size_t>(kClientScript1Arguments[opcode]);
        instruction.operands.resize(arguments);
        for (std::size_t k = 0; k < arguments && i + k < bytecode.size(); ++k) instruction.operands[k] = bytecode[i + k];
        i += arguments;
    }
    return instructions;
}

// An inventory's or item list's five options: RuneLite leaves an empty one
// null and adds each other one to the click mask.
void readConfigActions(Buffer& in, InterfaceDefinition& iface) {
    for (int i = 0; i < 5; ++i) {
        std::string action = in.string();
        if (action.empty()) continue;
        iface.configActions[static_cast<std::size_t>(i)] = std::move(action);
        iface.clickMask |= 1 << (i + 23);
    }
}

// Width times height zeros. RuneLite multiplies in a Java int, so an area
// past its range throws there.
std::vector<std::int32_t> itemSlots(const InterfaceDefinition& iface) {
    const long long area = static_cast<long long>(iface.originalWidth) * iface.originalHeight;
    if (area > std::numeric_limits<std::int32_t>::max()) throw std::runtime_error("IF1 inventory too large");
    return std::vector<std::int32_t>(static_cast<std::size_t>(area));
}

// RuneLite InterfaceLoader.decodeIf1.
void decodeIf1(InterfaceDefinition& iface, Buffer& in, bool rev237) {
    iface.isIf3 = false;
    iface.type = static_cast<std::uint8_t>(in.u8());
    iface.menuType = static_cast<std::uint8_t>(in.u8());
    iface.contentType = static_cast<std::uint16_t>(in.u16());
    iface.originalX = static_cast<std::int16_t>(in.i16());
    iface.originalY = static_cast<std::int16_t>(in.i16());
    iface.originalWidth = static_cast<std::uint16_t>(in.u16());
    iface.originalHeight = in.u16();
    iface.opacity = static_cast<std::uint8_t>(in.u8());
    iface.parentId = parentComponentId(in.u16(), iface.id);
    iface.hoveredSiblingId = minusOneFor0xFFFF(in.u16());

    const int alternates = in.u8();
    for (int i = 0; i < alternates; ++i) {
        iface.alternateOperators.push_back(static_cast<std::uint8_t>(in.u8()));
        iface.alternateRhs.push_back(static_cast<std::uint16_t>(in.u16()));
    }

    // RuneLite parses a script again after reading each of its values and
    // keeps the last parse, of the whole script; a partial parse throws only
    // where the whole one does.
    iface.clientScripts.resize(static_cast<std::size_t>(in.u8()));
    for (std::vector<ClientScript1Instruction>& script : iface.clientScripts) {
        std::vector<std::int32_t> bytecode(static_cast<std::size_t>(in.u16()));
        for (std::int32_t& value : bytecode) value = minusOneFor0xFFFF(in.u16());
        if (!bytecode.empty()) script = parseClientScript1(bytecode);
    }

    const int type = iface.type;
    if (type == 0) {
        iface.scrollHeight = static_cast<std::uint16_t>(in.u16());
        iface.isHidden = in.u8() == 1;
    }
    if (type == 1) {
        in.u16();
        in.u8();
    }
    if (type == 2) {
        iface.itemIds = itemSlots(iface);
        iface.itemQuantities = itemSlots(iface);
        if (in.u8() == 1) iface.clickMask |= 1 << 28;
        if (in.u8() == 1) iface.clickMask |= 1 << 30;
        if (in.u8() == 1) iface.clickMask |= std::numeric_limits<std::int32_t>::min();
        if (in.u8() == 1) iface.clickMask |= 1 << 29;
        iface.xPitch = static_cast<std::int16_t>(in.u8());
        iface.yPitch = static_cast<std::int16_t>(in.u8());
        iface.xOffsets.assign(20, 0);
        iface.yOffsets.assign(20, 0);
        iface.sprites.assign(20, 0);
        for (std::size_t i = 0; i < 20; ++i) {
            if (in.u8() == 1) {
                iface.xOffsets[i] = static_cast<std::int16_t>(in.i16());
                iface.yOffsets[i] = static_cast<std::int16_t>(in.i16());
                iface.sprites[i] = in.i32();
            } else {
                iface.sprites[i] = -1;
            }
        }
        readConfigActions(in, iface);
    }
    if (type == 3) iface.filled = in.u8() == 1;
    if (type == 4 || type == 1) {
        iface.xTextAlignment = static_cast<std::uint8_t>(in.u8());
        iface.yTextAlignment = static_cast<std::uint8_t>(in.u8());
        iface.lineHeight = static_cast<std::uint8_t>(in.u8());
        iface.fontId = minusOneFor0xFFFF(in.u16());
        iface.textShadowed = in.u8() == 1;
    }
    if (type == 4) {
        iface.text = in.string();
        iface.alternateText = in.string();
    }
    if (type == 1 || type == 3 || type == 4) iface.textColor = in.i32();
    if (type == 3 || type == 4) {
        iface.alternateTextColor = in.i32();
        iface.hoveredTextColor = in.i32();
        iface.alternateHoveredTextColor = in.i32();
    }
    if (type == 5) {
        iface.spriteId = in.i32();
        iface.alternateSpriteId = in.i32();
    }
    if (type == 6) {
        iface.modelType = 1;
        if (rev237) {
            iface.modelId = in.i32();
            iface.alternateModelId = in.i32();
        } else {
            iface.modelId = minusOneFor0xFFFF(in.u16());
            iface.alternateModelId = minusOneFor0xFFFF(in.u16());
        }
        iface.animation = minusOneFor0xFFFF(in.u16());
        iface.alternateAnimation = minusOneFor0xFFFF(in.u16());
        iface.modelZoom = static_cast<std::uint16_t>(in.u16());
        iface.rotationX = static_cast<std::uint16_t>(in.u16());
        iface.rotationZ = static_cast<std::uint16_t>(in.u16());
    }
    if (type == 7) {
        iface.itemIds = itemSlots(iface);
        iface.itemQuantities = itemSlots(iface);
        iface.xTextAlignment = static_cast<std::uint8_t>(in.u8());
        iface.fontId = minusOneFor0xFFFF(in.u16());
        iface.textShadowed = in.u8() == 1;
        iface.textColor = in.i32();
        iface.xPitch = static_cast<std::int16_t>(in.i16());
        iface.yPitch = static_cast<std::int16_t>(in.i16());
        if (in.u8() == 1) iface.clickMask |= 1 << 30;
        readConfigActions(in, iface);
    }
    if (type == 8) iface.text = in.string();

    const int menuType = iface.menuType;
    if (menuType == 2 || type == 2) {
        iface.targetVerb = in.string();
        iface.spellName = in.string();
        iface.clickMask |= (in.u16() & 63) << 11;
    }
    if (menuType == 1 || menuType == 4 || menuType == 5 || menuType == 6) {
        iface.tooltip = in.string();
        if (iface.tooltip.empty()) {
            if (menuType == 1) iface.tooltip = "Ok";
            if (menuType == 4 || menuType == 5) iface.tooltip = "Select";
            if (menuType == 6) iface.tooltip = "Continue";
        }
    }
    if (menuType == 1 || menuType == 4 || menuType == 5) iface.clickMask |= 1 << 22;
    if (menuType == 6) iface.clickMask |= 1;
}

// RuneLite InterfaceLoader.decodeListener: a script id and its arguments,
// each an int or a string.
std::vector<ListenerArgument> decodeListener(InterfaceDefinition& iface, Buffer& in) {
    std::vector<ListenerArgument> arguments(static_cast<std::size_t>(in.u8()));
    for (ListenerArgument& argument : arguments) {
        const int kind = in.u8();
        if (kind == 0) {
            argument = in.i32();
        } else if (kind == 1) {
            argument = in.string();
        }
    }
    if (!arguments.empty()) iface.hasListener = true;
    return arguments;
}

std::vector<std::int32_t> decodeTriggers(Buffer& in) {
    std::vector<std::int32_t> triggers(static_cast<std::size_t>(in.u8()));
    for (std::int32_t& trigger : triggers) trigger = in.i32();
    return triggers;
}

// RuneLite InterfaceLoader.decodeIf3.
void decodeIf3(InterfaceDefinition& iface, Buffer& in, bool rev237) {
    in.u8(); // the 0xFF marking the format
    iface.isIf3 = true;
    iface.type = static_cast<std::uint8_t>(in.u8());
    iface.contentType = static_cast<std::uint16_t>(in.u16());
    iface.originalX = static_cast<std::int16_t>(in.i16());
    iface.originalY = static_cast<std::int16_t>(in.i16());
    iface.originalWidth = static_cast<std::uint16_t>(in.u16());
    iface.originalHeight = iface.type == 9 ? in.i16() : in.u16();
    iface.widthMode = static_cast<std::int8_t>(in.i8());
    iface.heightMode = static_cast<std::int8_t>(in.i8());
    iface.xPositionMode = static_cast<std::int8_t>(in.i8());
    iface.yPositionMode = static_cast<std::int8_t>(in.i8());
    iface.parentId = parentComponentId(in.u16(), iface.id);
    iface.isHidden = in.u8() == 1;

    const int type = iface.type;
    if (type == 0) {
        iface.scrollWidth = static_cast<std::uint16_t>(in.u16());
        iface.scrollHeight = static_cast<std::uint16_t>(in.u16());
        iface.noClickThrough = in.u8() == 1;
    }
    if (type == 5) {
        iface.spriteId = in.i32();
        iface.textureId = static_cast<std::uint16_t>(in.u16());
        iface.spriteTiling = in.u8() == 1;
        iface.opacity = static_cast<std::uint8_t>(in.u8());
        iface.borderType = static_cast<std::uint8_t>(in.u8());
        iface.shadowColor = in.i32();
        iface.flippedVertically = in.u8() == 1;
        iface.flippedHorizontally = in.u8() == 1;
    }
    if (type == 6) {
        iface.modelType = 1;
        iface.modelId = rev237 ? in.i32() : minusOneFor0xFFFF(in.u16());
        iface.offsetX2d = static_cast<std::int16_t>(in.i16());
        iface.offsetY2d = static_cast<std::int16_t>(in.i16());
        iface.rotationX = static_cast<std::uint16_t>(in.u16());
        iface.rotationZ = static_cast<std::uint16_t>(in.u16());
        iface.rotationY = static_cast<std::uint16_t>(in.u16());
        iface.modelZoom = static_cast<std::uint16_t>(in.u16());
        iface.animation = minusOneFor0xFFFF(in.u16());
        iface.orthogonal = in.u8() == 1;
        in.u16();
        if (iface.widthMode != 0) iface.modelHeightOverride = static_cast<std::uint16_t>(in.u16());
        if (iface.heightMode != 0) in.u16();
    }
    if (type == 4) {
        iface.fontId = minusOneFor0xFFFF(in.u16());
        iface.text = in.string();
        iface.lineHeight = static_cast<std::uint8_t>(in.u8());
        iface.xTextAlignment = static_cast<std::uint8_t>(in.u8());
        iface.yTextAlignment = static_cast<std::uint8_t>(in.u8());
        iface.textShadowed = in.u8() == 1;
        iface.textColor = in.i32();
    }
    if (type == 3) {
        iface.textColor = in.i32();
        iface.filled = in.u8() == 1;
        iface.opacity = static_cast<std::uint8_t>(in.u8());
    }
    if (type == 9) {
        iface.lineWidth = static_cast<std::uint8_t>(in.u8());
        iface.textColor = in.i32();
        iface.lineDirection = in.u8() == 1;
    }

    iface.clickMask = in.u24();
    iface.name = in.string();
    iface.actions.resize(static_cast<std::size_t>(in.u8()));
    for (std::string& action : iface.actions) action = in.string();
    iface.dragDeadZone = static_cast<std::uint8_t>(in.u8());
    iface.dragDeadTime = static_cast<std::uint8_t>(in.u8());
    iface.dragRenderBehavior = in.u8() == 1;
    iface.targetVerb = in.string();
    iface.onLoadListener = decodeListener(iface, in);
    iface.onMouseOverListener = decodeListener(iface, in);
    iface.onMouseLeaveListener = decodeListener(iface, in);
    iface.onTargetLeaveListener = decodeListener(iface, in);
    iface.onTargetEnterListener = decodeListener(iface, in);
    iface.onVarTransmitListener = decodeListener(iface, in);
    iface.onInvTransmitListener = decodeListener(iface, in);
    iface.onStatTransmitListener = decodeListener(iface, in);
    iface.onTimerListener = decodeListener(iface, in);
    iface.onOpListener = decodeListener(iface, in);
    iface.onMouseRepeatListener = decodeListener(iface, in);
    iface.onClickListener = decodeListener(iface, in);
    iface.onClickRepeatListener = decodeListener(iface, in);
    iface.onReleaseListener = decodeListener(iface, in);
    iface.onHoldListener = decodeListener(iface, in);
    iface.onDragListener = decodeListener(iface, in);
    iface.onDragCompleteListener = decodeListener(iface, in);
    iface.onScrollWheelListener = decodeListener(iface, in);
    iface.varTransmitTriggers = decodeTriggers(in);
    iface.invTransmitTriggers = decodeTriggers(in);
    iface.statTransmitTriggers = decodeTriggers(in);
}

} // namespace

InterfaceDefinition decodeInterface(std::int32_t id, std::span<const std::uint8_t> data, std::int32_t indexRevision) {
    const bool rev237 = indexRevision > 1773137432;
    if (data.empty()) throw std::runtime_error("empty interface component");
    InterfaceDefinition iface;
    iface.id = id;
    Buffer in(data);
    if (data[0] == 0xFF) {
        decodeIf3(iface, in, rev237);
    } else {
        decodeIf1(iface, in, rev237);
    }
    return iface;
}

std::optional<std::map<int, InterfaceDefinition>> loadInterface(const Store& store, int group) {
    const auto files = store.files(Index::Interfaces, group);
    if (!files) return std::nullopt;
    const std::int32_t revision = store.index(Index::Interfaces)->revision;
    std::map<int, InterfaceDefinition> components;
    for (const auto& [file, bytes] : *files) {
        const std::int32_t id = (group << 16) + file;
        if (auto component = decodeOrSkip(store, "interface", id, [&] { return decodeInterface(id, bytes, revision); })) {
            components.emplace(file, std::move(*component));
        }
    }
    return components;
}

} // namespace osrs::cache
