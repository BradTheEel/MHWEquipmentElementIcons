#include "sprite_fixture.h"
#include "sprite_layout.h"

#include <string.h>

static const float stock_uv[] = {768.0f, 768.0f, 832.0f, 832.0f};
static char item_atlas;

static void *bind_texture(void *layer, void *atlas) {
    void **override = (void **)((uint8_t *)layer + SPRITE_OVERRIDE_TEXTURE);
    void *old = *override;
    *override = atlas;
    /* The real setter rescales UV when the texture size changes. */
    memset((uint8_t *)layer + SPRITE_UV_RECT, 0, sizeof(stock_uv));
    return old;
}

static void set_uv(void *layer, const float *uv) {
    memcpy((uint8_t *)layer + SPRITE_UV_RECT, uv, sizeof(stock_uv));
}

static void set_alpha(void *layer, uint8_t alpha) {
    for (unsigned i = 0; i < 4; ++i) ((uint8_t *)layer)[SPRITE_FIRST_ALPHA + i * 4] = alpha;
}

static void set_hidden(void *layer, bool hidden) {
    uint32_t *flags = (uint32_t *)((uint8_t *)layer + SPRITE_FLAGS);
    *flags = (*flags & ~SPRITE_HIDDEN) | (hidden ? SPRITE_HIDDEN : 0u);
}

static void set_active(void *layer, bool active) {
    uint32_t *flags = (uint32_t *)((uint8_t *)layer + SPRITE_FLAGS);
    *flags = (*flags & ~SPRITE_ACTIVE) | (active ? SPRITE_ACTIVE : 0u);
}

static void set_position(void *layer, const float *position) {
    memcpy((uint8_t *)layer + SPRITE_POSITION, position, 4 * sizeof(float));
}

static void set_size(void *layer, const float *size) {
    memcpy((uint8_t *)layer + SPRITE_SIZE, size, 2 * sizeof(float));
}

const BadgeApi fixture_api = {bind_texture, set_uv, set_alpha, set_hidden, set_active,
                             set_position, set_size};

Sprite stock_sprite(void) {
    Sprite sprite = {0};
    *(void **)(sprite.data + SPRITE_BASE_TEXTURE) = &item_atlas;
    const float position[] = {-19.0f, -18.0f, 0.0f, 1.0f};
    const float size[] = {36.0f, 36.0f};
    memcpy(sprite.data + SPRITE_POSITION, position, sizeof(position));
    memcpy(sprite.data + SPRITE_SIZE, size, sizeof(size));
    set_hidden(sprite.data, true);
    set_uv(sprite.data, stock_uv);
    set_alpha(sprite.data, 240);
    return sprite;
}
