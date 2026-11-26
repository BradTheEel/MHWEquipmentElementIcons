#include "badge.h"
#include "atlas_layout.h"
#include "sprite_layout.h"

#include <string.h>

bool badge_resolve(unsigned active, unsigned hidden, BadgeStyle *style) {
    const unsigned effect = active ? active : hidden;
    if (!effect || effect > ATLAS_EFFECT_COUNT) return false;
    /* atlas-layout.json places effect IDs 1..N in row order. */
    *style = (BadgeStyle){.tile = (uint8_t)(effect - 1), .dimmed = !active};
    return true;
}

bool badge_show(const BadgeApi *api, BadgeState *state, void *layer, void *pendant, void *atlas,
                BadgeStyle style, const BadgePlacement *placement, const float *pendant_corner) {
    if (!layer || !atlas || state->layer || style.tile >= ATLAS_COLUMNS * ATLAS_ROWS) return false;

    const uint8_t *sprite = layer;
    const uint32_t flags = *(const uint32_t *)(sprite + SPRITE_FLAGS);
    if (!(flags & SPRITE_HIDDEN) || !*(void *const *)(sprite + SPRITE_BASE_TEXTURE) ||
        *(void *const *)(sprite + SPRITE_OVERRIDE_TEXTURE)) return false;

    const uint8_t alpha = sprite[SPRITE_FIRST_ALPHA];
    if (alpha != sprite[SPRITE_FIRST_ALPHA + 4] || alpha != sprite[SPRITE_FIRST_ALPHA + 8] ||
        alpha != sprite[SPRITE_FIRST_ALPHA + 12]) return false;

    state->layer = layer;
    state->alpha = alpha;
    state->visibility = flags & SPRITE_VISIBILITY_MASK;
    memcpy(state->uv, sprite + SPRITE_UV_RECT, sizeof(state->uv));
    memcpy(state->position, sprite + SPRITE_POSITION, sizeof(state->position));
    memcpy(state->size, sprite + SPRITE_SIZE, sizeof(state->size));

    if (pendant) {
        state->pendant = pendant;
        memcpy(state->pendant_position, (const uint8_t *)pendant + SPRITE_POSITION,
               sizeof(state->pendant_position));
        state->pendant_hidden = pendant_corner == NULL;
        if (state->pendant_hidden) {
            api->set_hidden(pendant, true);
        } else {
            memcpy(state->pendant_corner, pendant_corner, sizeof(state->pendant_corner));
            const float position[] = {pendant_corner[0], pendant_corner[1],
                                      state->pendant_position[2], state->pendant_position[3]};
            api->set_position(pendant, position);
        }
    }

    const float u = (float)(style.tile % ATLAS_COLUMNS) * ATLAS_TILE_SIZE;
    const float v = (float)(style.tile / ATLAS_COLUMNS) * ATLAS_TILE_SIZE;
    const float uv[] = {u, v, u + ATLAS_TILE_SIZE, v + ATLAS_TILE_SIZE};
    api->bind_texture(layer, atlas);
    api->set_uv(layer, uv);
    if (placement) {
        const float position[] = {placement->x, placement->y, state->position[2], state->position[3]};
        const float size[] = {placement->width, placement->height};
        api->set_position(layer, position);
        api->set_size(layer, size);
    }
    if (style.dimmed) api->set_alpha(layer, (uint8_t)(alpha / 2));
    api->set_active(layer, true);
    api->set_hidden(layer, false);
    return true;
}

void badge_restore(const BadgeApi *api, BadgeState *state, void *layer, void *pendant, void *atlas) {
    if (!layer || state->layer != layer) return;
    state->layer = NULL;
    if (pendant && state->pendant == pendant) {
        const uint8_t *sprite = pendant;
        const float *position = (const float *)(sprite + SPRITE_POSITION);
        if (state->pendant_hidden) {
            const uint32_t flags = *(const uint32_t *)(sprite + SPRITE_FLAGS);
            if ((flags & SPRITE_VISIBILITY_MASK) == SPRITE_VISIBILITY_MASK &&
                memcmp(position, state->pendant_position, sizeof(state->pendant_position)) == 0)
                api->set_hidden(pendant, false);
        } else if (position[0] == state->pendant_corner[0] && position[1] == state->pendant_corner[1]) {
            api->set_position(pendant, state->pendant_position);
        }
    }
    state->pendant = NULL;
    if (*(void *const *)((uint8_t *)layer + SPRITE_OVERRIDE_TEXTURE) != atlas) return;

    api->set_hidden(layer, true);
    api->set_active(layer, (state->visibility & SPRITE_ACTIVE) != 0);
    api->bind_texture(layer, NULL);
    api->set_uv(layer, state->uv);
    api->set_position(layer, state->position);
    api->set_size(layer, state->size);
    api->set_alpha(layer, state->alpha);
}
