#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "sprite_fixture.h"

static char status_atlas, foreign_atlas;

static void test_badge_effect_selection(void) {
    const uint8_t expected_tiles[] = {3, 4, 6, 5, 7, 8, 10, 12, 13};
    BadgeStyle style;
    for (unsigned i = 0; i < sizeof(expected_tiles); ++i) {
        assert(badge_resolve(i + 1, 0, &style));
        assert(style.tile == expected_tiles[i] && !style.dimmed);
    }
    assert(badge_resolve(1, 5, &style) && style.tile == 3 && !style.dimmed);
    assert(badge_resolve(0, 7, &style) && style.tile == 10 && style.dimmed);
    assert(!badge_resolve(0, 0, &style));
    assert(!badge_resolve(10, 5, &style));
}

static bool show(BadgeState *state, void *layer, void *pendant, unsigned active, unsigned hidden) {
    BadgeStyle style;
    return badge_resolve(active, hidden, &style) &&
           badge_show(&fixture_api, state, layer, pendant, &status_atlas, style);
}

int run_badge_cases(void) {
    test_badge_effect_selection();
    BadgeState state = {0};
    Sprite sprite = stock_sprite();
    const Sprite before = sprite;
    const float fire_uv[] = {192.0f, 0.0f, 256.0f, 64.0f};

    if (!show(&state, sprite.data, NULL, 1, 5)) {
        fputs("FAIL active fire should show a badge\n", stderr);
        return 1;
    }
    assert(*(void **)(sprite.data + 0x150) == &status_atlas);
    assert(memcmp(sprite.data + 0x15c, fire_uv, sizeof(fire_uv)) == 0);
    assert(*(float *)(sprite.data + 0xa0) == -19.0f);
    assert(*(float *)(sprite.data + 0xa4) == 18.0f);
    assert(*(float *)(sprite.data + 0xe0) == 24.0f);
    assert(*(uint32_t *)(sprite.data + 0x58) == 1u && sprite.data[0xd3] == 240);
    badge_restore(&fixture_api, &state, sprite.data, NULL, &status_atlas);
    assert(state.layer == NULL && memcmp(&sprite, &before, sizeof(sprite)) == 0);

    assert(show(&state, sprite.data, NULL, 0, 1));
    assert(sprite.data[0xd3] == 120 && sprite.data[0xdf] == 120);
    badge_restore(&fixture_api, &state, sprite.data, NULL, &status_atlas);
    assert(memcmp(&sprite, &before, sizeof(sprite)) == 0);
    assert(!show(&state, sprite.data, NULL, 0, 0));
    assert(!show(&state, sprite.data, NULL, 10, 0));
    assert(memcmp(&sprite, &before, sizeof(sprite)) == 0);

    fixture_api.set_hidden(sprite.data, false);
    assert(!show(&state, sprite.data, NULL, 1, 0));
    sprite = before;
    fixture_api.bind_texture(sprite.data, &foreign_atlas);
    assert(!show(&state, sprite.data, NULL, 1, 0));
    assert(*(void **)(sprite.data + 0x150) == &foreign_atlas);
    sprite = before;
    assert(show(&state, sprite.data, NULL, 1, 0));
    fixture_api.bind_texture(sprite.data, &foreign_atlas);
    const Sprite changed = sprite;
    badge_restore(&fixture_api, &state, sprite.data, NULL, &status_atlas);
    assert(state.layer == NULL && memcmp(&sprite, &changed, sizeof(sprite)) == 0);

    sprite = before;
    Sprite pendant = stock_sprite();
    const float pendant_position[] = {-18.0f, 18.0f, 0.0f, 1.0f};
    fixture_api.set_position(pendant.data, pendant_position);
    fixture_api.set_hidden(pendant.data, false);
    fixture_api.set_active(pendant.data, true);
    const Sprite original_pendant = pendant;
    assert(show(&state, sprite.data, pendant.data, 1, 0));
    assert(*(float *)(pendant.data + 0xa0) == 18.0f && *(float *)(pendant.data + 0xa4) == -18.0f);
    badge_restore(&fixture_api, &state, sprite.data, pendant.data, &status_atlas);
    assert(memcmp(&sprite, &before, sizeof(sprite)) == 0);
    assert(memcmp(&pendant, &original_pendant, sizeof(pendant)) == 0);

    const float paralysis_uv[] = {128.0f, 64.0f, 192.0f, 128.0f};
    assert(show(&state, sprite.data, NULL, 7, 0));
    assert(memcmp(sprite.data + 0x15c, paralysis_uv, sizeof(paralysis_uv)) == 0);
    badge_restore(&fixture_api, &state, sprite.data, NULL, &status_atlas);
    puts("PASS effect mapping, active, hidden, raw, invalid, lower left, pendant, cell reuse, conflicting layer states");
    return 0;
}
