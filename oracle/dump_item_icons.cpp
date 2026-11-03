// Item icons (osrs/cache/item_icons.h), each as RuneLite's ItemSpriteFactory
// draws it: its ARGB pixels as a Java int[]. The parameter sets must stay the
// same as RuneLiteOracle.java's.

#include "lines.h"

#include <osrs/cache/definitions.h>
#include <osrs/cache/item_icons.h>

#include <stdexcept>

namespace osrs::cache::oracle {

namespace {

template <int Quantity, int Border, std::uint32_t Shadow, bool Noted>
void icons(const Store& store, Lines& out) {
    ItemIconRenderer renderer(store);
    for (const ItemDefinition& item : loadItemDefinitions(store)) {
        std::optional<Sprite> icon;
        bool failed = false;
        try {
            icon = renderer.render(item.id, Quantity, Border, Shadow, Noted);
            if (!icon) continue;
        } catch (const std::runtime_error&) {
            failed = true;
        }
        out.object({item.id});
        if (failed) {
            out.error();
            continue;
        }
        out.integers("pixels", std::vector<std::int32_t>(icon->pixels.begin(), icon->pixels.end()));
    }
}

} // namespace

// As the inventory shows an item; as the client shows a selected stack of
// 1000; and a large stack on a banknote, unbordered.
const Type itemicon("itemicon", icons<1, 1, kItemIconShadow, false>);
const Type itemiconselected("itemiconselected", icons<1000, 2, 0, false>);
const Type itemiconnoted("itemiconnoted", icons<100000, 0, kItemIconShadow, true>);

} // namespace osrs::cache::oracle
