#pragma once

// Item icons, the 36 by 32 pictures the inventory shows, drawn as RuneLite's
// cache library draws them (net.runelite:cache 1.13.5,
// item/ItemSpriteFactory): the item's model lit and rasterized in software,
// with the banknote, bought or placeholder sprite it's made from, its
// outline and its shadow.

#include <osrs/cache/sprites.h>
#include <osrs/cache/store.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace osrs::cache {

// The shadow the client and RuneLite draw inventory icons with (RuneLite's
// SpritePixels.DEFAULT_SHADOW_COLOR).
inline constexpr std::uint32_t kItemIconShadow = 0x302020;

// Draws icons from one store, reading its items, textures and colour palette
// once (about 0.1 s). Not to be shared between threads: it makes each
// texture's pixels when it first draws it.
class ItemIconRenderer {
public:
    // Throws std::runtime_error if the cache lacks the items or textures.
    explicit ItemIconRenderer(const Store& store);
    ~ItemIconRenderer();
    ItemIconRenderer(const ItemIconRenderer&) = delete;
    ItemIconRenderer& operator=(const ItemIconRenderer&) = delete;

    // RuneLite ItemSpriteFactory.createSprite. `quantity` picks the model a
    // stack of that many shows; `border` 1 outlines the item in black, and 2
    // adds a white outline around that and zooms in 4%, as the client shows a
    // selected item; `shadowColour` (RGB, 0 for none) falls below and to the
    // right of it; `noted` draws it zoomed out by half, as a banknote shows
    // it. A Sprite of 36 by 32 ARGB pixels, each 0 or opaque, without a
    // palette. Nothing if the cache lacks the item, an item it's drawn from,
    // or a model. Throws std::runtime_error where RuneLite's drawing fails (an
    // angle of 2048 or more, a model too big for its buffers, a face that
    // reaches outside the icon), unless the store skips such objects (store.h).
    std::optional<Sprite> render(int itemId, int quantity = 1, int border = 1, std::uint32_t shadowColour = kItemIconShadow,
                                 bool noted = false);

private:
    struct State;
    std::unique_ptr<State> state_;
};

// One icon, as ItemIconRenderer::render draws it. Reads the items, textures
// and palette for it alone: keep an ItemIconRenderer to draw more.
std::optional<Sprite> loadItemIcon(const Store& store, int itemId, int quantity = 1, int border = 1,
                                   std::uint32_t shadowColour = kItemIconShadow, bool noted = false);

} // namespace osrs::cache
