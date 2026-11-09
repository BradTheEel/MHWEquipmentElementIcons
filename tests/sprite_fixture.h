#ifndef EQUIPMENT_ELEMENT_SPRITE_FIXTURE_H
#define EQUIPMENT_ELEMENT_SPRITE_FIXTURE_H

#include "badge.h"

typedef union Sprite {
    uint64_t align;
    uint8_t data[0x190];
} Sprite;

extern const BadgeApi fixture_api;
Sprite stock_sprite(void);

#endif
