#ifndef EQUIPMENT_ELEMENT_BADGE_H
#define EQUIPMENT_ELEMENT_BADGE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct BadgeApi {
    void *(*bind_texture)(void *layer, void *texture);
    void (*set_uv)(void *layer, const float *uv);
    void (*set_alpha)(void *layer, uint8_t alpha);
    void (*set_hidden)(void *layer, bool hidden);
    void (*set_active)(void *layer, bool active);
    void (*set_position)(void *layer, const float *position);
    void (*set_size)(void *layer, const float *size);
} BadgeApi;

typedef struct BadgeState {
    void *layer;
    float uv[4];
    float position[4];
    float size[2];
    void *pendant;
    float pendant_position[4];
    uint8_t alpha;
} BadgeState;

typedef struct BadgeStyle {
    uint8_t tile;
    bool dimmed;
} BadgeStyle;

bool badge_resolve(unsigned active, unsigned hidden, BadgeStyle *style);
bool badge_show(const BadgeApi *api, BadgeState *state, void *layer, void *pendant, void *atlas,
                BadgeStyle style);
void badge_restore(const BadgeApi *api, BadgeState *state, void *layer, void *pendant, void *atlas);

#endif
