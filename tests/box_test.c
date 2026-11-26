/* Exercise the production detours against owned grid objects and ABI stubs. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>

#include "sprite_fixture.h"
#include "../src/plugin.c"

int run_badge_cases(void);

static uint32_t work[0x98 / sizeof(uint32_t)];
static uint8_t row[0x60], swapped_weapon_row[0x60];
static unsigned bind_calls, close_calls;
static bool item_present, kinsect_model_present;
static uint64_t save_data[0xb0cc8 / 8];
static unsigned panel_updates, panel_closes;
static bool panel_force;

static const uint8_t *find_equipment(void *box, uint32_t index) {
    assert(box && index == 12);
    return (const uint8_t *)work;
}

static const uint8_t *find_weapon(uint32_t kind, uint32_t id) {
    assert((kind == 0 && (id == 7 || id == 8)) || (kind == BOW && id == 7));
    return id == 8 ? swapped_weapon_row : row;
}

static const uint8_t *find_palico_weapon(uint32_t id) {
    assert(id == 7);
    return row;
}

static const uint8_t *find_kinsect(uint32_t id) {
    assert(id == 7);
    return kinsect_model_present ? row : NULL;
}

static bool stock_bind(void *box, uint32_t index, void *cell) {
    assert(box && index == 12 && cell);
    ++bind_calls;
    void *layer = read_pointer(cell, 0x708);
    assert(read_pointer(layer, 0x150) == NULL);
    *(uint32_t *)((uint8_t *)cell + 0x778) = item_present ? work[WORK_TYPE / 4] : UINT32_MAX;
    return item_present;
}

static uintptr_t stock_close(void *box) {
    assert(box && owned_box == NULL);
    ++close_calls;
    return 7;
}

static void install_stub(uint32_t rva, uintptr_t target) {
    void *page = (void *)(game + (rva & ~0xfffu));
    assert(VirtualAlloc(page, 0x1000, MEM_COMMIT, PAGE_READWRITE));
    DWORD before;
    assert(VirtualProtect(page, 0x1000, PAGE_READWRITE, &before));
    uint8_t jump[] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
    memcpy(jump + 2, &target, sizeof(target));
    memcpy((void *)(game + rva), jump, sizeof(jump));
    assert(VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &before));
    assert(FlushInstructionCache(GetCurrentProcess(), (void *)(game + rva), sizeof(jump)));
}

static void write_global(uint32_t rva, void *value) {
    assert(VirtualAlloc((void *)(game + (rva & ~0xfffu)), 0x1000, MEM_COMMIT, PAGE_READWRITE));
    *(void **)(game + rva) = value;
}

static const uint8_t *get_save(void *manager) {
    return read_pointer(manager, 0xa8);
}

static uint32_t get_hunter_slot(const uint8_t *save, uint32_t slot) {
    assert(slot < 6);
    return read_u32(save, 0xa8 + slot * sizeof(uint32_t));
}

static bool has_stock_augmentation(const uint8_t *item) {
    return read_u32(item, 0x50) != 0;
}

static void stock_panel_update(void *menu, bool force, bool palico) {
    assert(read_u32(menu, 0x28f0) == (uint32_t)palico);
    ++panel_updates;
    panel_force = force;
    uint8_t *icon = (uint8_t *)menu + (palico ? 0x3560 : 0x2900);
    uint32_t *cache = (uint32_t *)((uint8_t *)menu + (palico ? 0x3c48 : 0x3c20));
    const uint32_t index = read_u32(save_data, palico ? 0x2c8 : 0xa8);
    if (!palico) cache[1] = read_u32(save_data, 0xac);
    if (!force && *cache == index) return;
    assert(read_pointer(read_pointer(icon, 0x80), 0x150) == NULL);
    *(uint32_t *)(icon + 0xf0) = index == UINT32_MAX ? UINT32_MAX : 1;
    *cache = index;
}

static void stock_hunter_panel(void *menu, bool force) {
    stock_panel_update(menu, force, false);
}

static void stock_palico_panel(void *menu, bool force) {
    stock_panel_update(menu, force, true);
}

static bool stock_panel_close(void *menu) {
    ++panel_closes;
    assert(!read_pointer(read_pointer(menu, 0x2980), 0x150));
    assert(!read_pointer(read_pointer(menu, 0x35e0), 0x150));
    return true;
}

static void *find_gui_child(void *root, const char *name) {
    assert(strcmp(name, "icon_itemList") == 0);
    return root;
}

static void stock_info_open(void *menu) {
    *(uint32_t *)((uint8_t *)menu + INFO_MODE) = INFO_HUNTER_MODE;
    *(uint32_t *)((uint8_t *)menu + INFO_SUBMODE) = INFO_EQUIPMENT_SUBMODE;
}

static void stock_info_appearance(void *menu, void *list, int selection) {
    assert(menu && !list && (selection == 0 || selection == 1));
}

static void stock_info_card(void *menu) {
    void *spare = read_pointer(menu, INFO_CARD_SPARE);
    assert(!read_pointer(spare, SPRITE_OVERRIDE_TEXTURE));
    fixture_api.set_hidden(spare, true);
    if (read_u32(menu, INFO_CARD_PAGE) == INFO_KINSECT_PAGE)
        fixture_api.set_hidden(read_pointer(menu, INFO_CARD_PENDANT), true);
}

static void stock_info_close(void *menu) {
    assert(menu && !info_badges.menu);
}

static void stock_info_reset(void *menu) {
    assert(info_badges.menu == menu);
    assert(!info_badges.list_element.layer && !info_badges.card_element.layer);
}

typedef struct GridFixture {
    uint64_t box[0x2960 / 8], grid[0x200 / 8], cell[0x780 / 8];
    void *cells[1];
    Sprite sprite;
} GridFixture;

typedef struct PanelFixture {
    uint64_t menu[0x3c50 / 8];
    Sprite hunter, palico, weapon_augmentation, armor_augmentation;
} PanelFixture;

typedef struct InfoFixture {
    uint64_t menu[0xdb00 / 8], list[0x1f0 / 8];
    uint64_t card_root[0x60 / 8];
    uint64_t row[2][0x1c8 / 8], node[2][0xf0 / 8];
    void *rows[2], *children[2][11];
    Sprite list_weapon_element, list_weapon_bolt, list_armor_bolt;
    Sprite card, pendant, occupied;
} InfoFixture;

static Sprite stock;
static uint64_t manager[0xb0 / 8];

static void reset_scenario(void) {
    memset(work, 0, sizeof(work));
    memset(row, 0, sizeof(row));
    memset(swapped_weapon_row, 0, sizeof(swapped_weapon_row));
    memset(save_data, 0, sizeof(save_data));
    memset(badges, 0, sizeof(badges));
    memset(equipped_badges, 0, sizeof(equipped_badges));
    memset(worn_augmentations, 0, sizeof(worn_augmentations));
    info_badges = (InfoBadges){0};
    augmentation_panel = NULL;
    owned_box = NULL;
    bind_calls = close_calls = panel_updates = panel_closes = 0;
    panel_force = false;
    item_present = true;
    kinsect_model_present = true;
    InterlockedExchange(&hooks_ready, 1);
}

static void prepare_weapon(uint32_t kind) {
    work[WORK_TYPE / 4] = WEAPON;
    work[WORK_KIND / 4] = kind;
    work[WORK_ID / 4] = 7;
}

static void prepare_grid(GridFixture *fixture, uint32_t mode, bool palico) {
    reset_scenario();
    *fixture = (GridFixture){0};
    fixture->sprite = stock;
    fixture->cells[0] = fixture->cell;
    fixture->box[0] = game + BOX_VTABLE;
    *(uint32_t *)((uint8_t *)fixture->box + 0x1d04) = 2;
    fixture->box[BOX_MODE / 8] = mode | ((uint64_t)palico << 32);
    fixture->box[BOX_GRID / 8] = (uintptr_t)fixture->grid;
    fixture->grid[GRID_COUNT / 8] = 1;
    fixture->grid[GRID_CELLS / 8] = (uintptr_t)fixture->cells;
    fixture->cell[0x708 / 8] = (uintptr_t)fixture->sprite.data;
}

static void prepare_panel(PanelFixture *fixture, bool palico) {
    reset_scenario();
    *fixture = (PanelFixture){0};
    fixture->hunter = stock;
    fixture->palico = stock;
    fixture->menu[0] = game + 0x346d968;
    fixture->menu[0x28f0 / 8] = palico;
    *(uint32_t *)((uint8_t *)fixture->menu + 0x1d04) = palico ? 2 : 1;
    fixture->menu[0x2980 / 8] = (uintptr_t)fixture->hunter.data;
    fixture->menu[0x35e0 / 8] = (uintptr_t)fixture->palico.data;
}

static void test_hunter_grid_modes(void) {
    GridFixture fixture;
    prepare_grid(&fixture, UPGRADE_EQUIPMENT, false);
    prepare_weapon(0);
    row[MELEE_ELEMENT] = 1;

    InterlockedExchange(&hooks_ready, 0);
    assert(bind_detour(fixture.box, 12, fixture.cell));
    assert(memcmp(&fixture.sprite, &stock, sizeof(stock)) == 0);
    InterlockedExchange(&hooks_ready, 1);
    assert(bind_detour(fixture.box, 12, fixture.cell));
    assert(read_pointer(fixture.sprite.data, 0x150) == &atlas);

    fixture.box[BOX_MODE / 8] = SET_DECORATIONS;
    assert(bind_detour(fixture.box, 12, fixture.cell));
    assert(read_pointer(fixture.sprite.data, 0x150) == &atlas);
    fixture.box[BOX_MODE / 8] = 5; /* Kinsect upgrade selector */
    assert(bind_detour(fixture.box, 12, fixture.cell));
    assert(memcmp(&fixture.sprite, &stock, sizeof(stock)) == 0);
    fixture.box[BOX_MODE / 8] = CHANGE_EQUIPMENT;
    assert(bind_detour(fixture.box, 12, fixture.cell));
    assert(read_pointer(fixture.sprite.data, 0x150) == &atlas);
    fixture.box[BOX_MODE / 8] = 2; /* sell reuses the cell */
    assert(bind_detour(fixture.box, 12, fixture.cell));
    assert(memcmp(&fixture.sprite, &stock, sizeof(stock)) == 0);
    assert(bind_calls == 6);
    assert(close_detour(fixture.box) == 7 && close_calls == 1);
}

static void test_hunter_grid_reuse(void) {
    GridFixture fixture;
    prepare_grid(&fixture, CHANGE_EQUIPMENT, false);
    prepare_weapon(BOW);
    row[BOW_HIDDEN] = 3;

    assert(bind_detour(fixture.box, 12, fixture.cell));
    assert(read_pointer(fixture.sprite.data, 0x150) == &atlas && fixture.sprite.data[0xd3] == 120);
    item_present = false;
    assert(!bind_detour(fixture.box, 12, fixture.cell));
    assert(memcmp(&fixture.sprite, &stock, sizeof(stock)) == 0);
    item_present = true;
    work[WORK_KIND / 4] = 12; /* bowgun */
    assert(bind_detour(fixture.box, 12, fixture.cell));
    assert(memcmp(&fixture.sprite, &stock, sizeof(stock)) == 0);
    work[WORK_KIND / 4] = 0;
    row[MELEE_ELEMENT] = 1;
    assert(bind_detour(fixture.box, 12, fixture.cell));
    assert(read_pointer(fixture.sprite.data, 0x150) == &atlas);
    assert(close_detour(fixture.box) == 7 && bind_calls == 4 && close_calls == 1);
    assert(memcmp(&fixture.sprite, &stock, sizeof(stock)) == 0);
}

static void test_pendant_with_occupied_corner(void) {
    GridFixture fixture;
    prepare_grid(&fixture, CHANGE_EQUIPMENT, false);
    prepare_weapon(0);
    row[MELEE_ELEMENT] = 1;

    Sprite pendant = stock, upper_right = stock;
    fixture_api.set_active(pendant.data, true);
    fixture_api.set_hidden(pendant.data, false);
    fixture_api.set_active(upper_right.data, true);
    fixture_api.set_hidden(upper_right.data, false);
    const Sprite before = pendant;
    fixture.cell[0x740 / 8] = (uintptr_t)pendant.data;
    fixture.cell[0x710 / 8] = (uintptr_t)upper_right.data;
    assert(bind_detour(fixture.box, 12, fixture.cell));
    if (read_pointer(fixture.sprite.data, SPRITE_OVERRIDE_TEXTURE) != &atlas) {
        fputs("FAIL the occupied pendant destination must not suppress the element\n", stderr);
        ExitProcess(1);
    }
    assert(!is_visible_sprite(pendant.data) && is_visible_sprite(upper_right.data));
    assert(close_detour(fixture.box) == 7);
    assert(memcmp(&pendant, &before, sizeof(pendant)) == 0);
}

static void test_kinsect_grid(void) {
    GridFixture fixture;
    prepare_grid(&fixture, CHANGE_EQUIPMENT, false);
    work[WORK_TYPE / 4] = 4; /* stock Kinsect equipment category */
    work[WORK_ID / 4] = 7;

    static const float element_uv[][4] = {
        {0.0f, 0.0f, 64.0f, 64.0f},
        {64.0f, 0.0f, 128.0f, 64.0f},
        {128.0f, 0.0f, 192.0f, 64.0f},
        {192.0f, 0.0f, 256.0f, 64.0f},
        {0.0f, 64.0f, 64.0f, 128.0f}
    };
    for (uint32_t element = 1; element <= 5; ++element) {
        work[0x3c / 4] = element; /* stock runtime element field */
        assert(bind_detour(fixture.box, 12, fixture.cell));
        assert(read_pointer(fixture.sprite.data, SPRITE_OVERRIDE_TEXTURE) == &atlas);
        assert(memcmp(fixture.sprite.data + SPRITE_UV_RECT, element_uv[element - 1], sizeof(element_uv[0])) == 0);
        assert(fixture.sprite.data[SPRITE_FIRST_ALPHA] == stock.data[SPRITE_FIRST_ALPHA]);
    }

    const uint32_t invalid_elements[] = {0, UINT32_MAX, 6};
    for (unsigned i = 0; i < sizeof(invalid_elements) / sizeof(invalid_elements[0]); ++i) {
        work[0x3c / 4] = invalid_elements[i];
        assert(bind_detour(fixture.box, 12, fixture.cell));
        assert(memcmp(&fixture.sprite, &stock, sizeof(stock)) == 0);
    }

    work[0x3c / 4] = 2;
    kinsect_model_present = false;
    assert(bind_detour(fixture.box, 12, fixture.cell));
    assert(memcmp(&fixture.sprite, &stock, sizeof(stock)) == 0);
    kinsect_model_present = true;
    work[WORK_ID / 4] = 201;
    assert(bind_detour(fixture.box, 12, fixture.cell));
    assert(memcmp(&fixture.sprite, &stock, sizeof(stock)) == 0);
    work[WORK_ID / 4] = 7;

    fixture.box[BOX_MODE / 8] = UPGRADE_EQUIPMENT;
    assert(bind_detour(fixture.box, 12, fixture.cell));
    assert(read_pointer(fixture.sprite.data, SPRITE_OVERRIDE_TEXTURE) == &atlas);
    fixture.box[BOX_MODE / 8] = 5; /* Nurture a Kinsect */
    assert(bind_detour(fixture.box, 12, fixture.cell));
    assert(read_pointer(fixture.sprite.data, SPRITE_OVERRIDE_TEXTURE) == &atlas);
    fixture.box[BOX_MODE / 8] = SET_DECORATIONS;
    assert(bind_detour(fixture.box, 12, fixture.cell));
    assert(read_pointer(fixture.sprite.data, SPRITE_OVERRIDE_TEXTURE) == &atlas);
    fixture.box[BOX_MODE / 8] = 7; /* Change Kinsect Elements */
    assert(bind_detour(fixture.box, 12, fixture.cell));
    if (read_pointer(fixture.sprite.data, SPRITE_OVERRIDE_TEXTURE) != &atlas) {
        fputs("FAIL Change Kinsect Elements should show the currently assigned element\n", stderr);
        ExitProcess(1);
    }
    assert(memcmp(fixture.sprite.data + SPRITE_UV_RECT, element_uv[1], sizeof(element_uv[1])) == 0);
    work[0x3c / 4] = 4; /* the same Kinsect's element changed */
    assert(bind_detour(fixture.box, 12, fixture.cell));
    assert(memcmp(fixture.sprite.data + SPRITE_UV_RECT, element_uv[3], sizeof(element_uv[3])) == 0);
    work[0x3c / 4] = 0;
    assert(bind_detour(fixture.box, 12, fixture.cell));
    assert(memcmp(&fixture.sprite, &stock, sizeof(stock)) == 0);
    prepare_weapon(0);
    row[MELEE_ELEMENT] = 1;
    assert(bind_detour(fixture.box, 12, fixture.cell));
    assert(memcmp(&fixture.sprite, &stock, sizeof(stock)) == 0);
    work[WORK_TYPE / 4] = KINSECT;
    work[0x3c / 4] = 2;
    fixture.box[BOX_MODE / 8] = 2; /* sell */
    assert(bind_detour(fixture.box, 12, fixture.cell));
    assert(memcmp(&fixture.sprite, &stock, sizeof(stock)) == 0);

    fixture.box[BOX_MODE / 8] = CHANGE_EQUIPMENT | ((uint64_t)PALICO_GRID << 32);
    assert(palico_bind_detour(fixture.box, 12, fixture.cell));
    assert(memcmp(&fixture.sprite, &stock, sizeof(stock)) == 0);

    fixture.box[BOX_MODE / 8] = CHANGE_EQUIPMENT;
    assert(bind_detour(fixture.box, 12, fixture.cell));
    assert(read_pointer(fixture.sprite.data, SPRITE_OVERRIDE_TEXTURE) == &atlas);
    assert(close_detour(fixture.box) == 7);
    assert(memcmp(&fixture.sprite, &stock, sizeof(stock)) == 0);
    puts("PASS Kinsect grid badges, including the Smithy's Kinsect Elements selector and element changes");
}

static void test_palico_grid(void) {
    GridFixture fixture;
    prepare_grid(&fixture, CHANGE_EQUIPMENT, true);
    prepare_weapon(0);
    row[PALICO_ELEMENT] = 7;

    assert(bind_detour(fixture.box, 12, fixture.cell));
    assert(memcmp(&fixture.sprite, &stock, sizeof(stock)) == 0);
    assert(palico_bind_detour(fixture.box, 12, fixture.cell));
    assert(read_pointer(fixture.sprite.data, 0x150) == &atlas);
    work[WORK_TYPE / 4] = 3; /* Palico gadget */
    assert(palico_bind_detour(fixture.box, 12, fixture.cell));
    assert(memcmp(&fixture.sprite, &stock, sizeof(stock)) == 0);
    work[WORK_TYPE / 4] = WEAPON;
    work[WORK_ID / 4] = 201; /* exceeds the Palico weapon table */
    assert(palico_bind_detour(fixture.box, 12, fixture.cell));
    assert(memcmp(&fixture.sprite, &stock, sizeof(stock)) == 0);
    assert(bind_calls == 4);
    assert(close_detour(fixture.box) == 7 && close_calls == 1);
}

static void expect_panel_update(RefreshPanel detour, PanelFixture *fixture, bool force) {
    const unsigned before = panel_updates;
    detour(fixture->menu, force);
    assert(panel_updates == before + 1 && panel_force == force);
}

static void test_equipped_hunter(void) {
    PanelFixture fixture;
    prepare_panel(&fixture, false);
    save_data[0xa8 / 8] = 0;
    prepare_weapon(0);
    row[MELEE_ELEMENT] = 1;
    memcpy((uint8_t *)save_data + 0x41068, work, sizeof(work));
    work[WORK_ID / 4] = 8;
    swapped_weapon_row[MELEE_ELEMENT] = 4;
    memcpy((uint8_t *)save_data + 0x41100, work, sizeof(work));

    expect_panel_update(hunter_panel_detour, &fixture, true);
    assert(read_pointer(fixture.hunter.data, 0x150) == &atlas);
    const float fire_uv[] = {0.0f, 0.0f, 64.0f, 64.0f};
    assert(memcmp(fixture.hunter.data + 0x15c, fire_uv, sizeof(fire_uv)) == 0);
    const Sprite drawn = fixture.hunter;
    expect_panel_update(hunter_panel_detour, &fixture, false);
    assert(memcmp(&fixture.hunter, &drawn, sizeof(drawn)) == 0);
    expect_panel_update(hunter_panel_detour, &fixture, true);
    assert(read_pointer(fixture.hunter.data, 0x150) == &atlas);

    save_data[0xa8 / 8] = 1;
    expect_panel_update(hunter_panel_detour, &fixture, false);
    const float thunder_uv[] = {192.0f, 0.0f, 256.0f, 64.0f};
    assert(memcmp(fixture.hunter.data + 0x15c, thunder_uv, sizeof(thunder_uv)) == 0);
    save_data[0xa8 / 8] = UINT32_MAX;
    expect_panel_update(hunter_panel_detour, &fixture, false);
    assert(memcmp(&fixture.hunter, &stock, sizeof(stock)) == 0);
    save_data[0xa8 / 8] = 0;
    expect_panel_update(hunter_panel_detour, &fixture, false);
    assert(read_pointer(fixture.hunter.data, 0x150) == &atlas);

    assert(memcmp(&fixture.palico, &stock, sizeof(stock)) == 0);
    assert(panel_close_detour(fixture.menu) && panel_closes == 1);
    assert(memcmp(&fixture.hunter, &stock, sizeof(stock)) == 0);
    assert(memcmp(&fixture.palico, &stock, sizeof(stock)) == 0);
}

static void test_equipped_augmentations(void) {
    PanelFixture fixture;
    prepare_panel(&fixture, false);
    fixture.weapon_augmentation = stock;
    fixture.armor_augmentation = stock;
    fixture.menu[0x29b0 / 8] = (uintptr_t)fixture.weapon_augmentation.data;
    fixture.menu[0x2ac8 / 8] = (uintptr_t)fixture.armor_augmentation.data;
    *(uint32_t *)((uint8_t *)fixture.menu + 0x2b08) = 0; /* equipped armor icon type */
    *(uint32_t *)((uint8_t *)save_data + 0xac) = 1;
    prepare_weapon(0);
    row[MELEE_ELEMENT] = 1;
    work[0x50 / 4] = 1;
    uint8_t *weapon = (uint8_t *)save_data + 0x41068;
    uint8_t *armor = weapon + 0x98;
    memcpy(weapon, work, sizeof(work));
    work[WORK_TYPE / 4] = 0;
    memcpy(armor, work, sizeof(work));

    expect_panel_update(hunter_panel_detour, &fixture, true);
    const float bolt_uv[] = {64.0f, 128.0f, 128.0f, 192.0f};
    assert(read_pointer(fixture.hunter.data, SPRITE_OVERRIDE_TEXTURE) == &atlas);
    assert(is_visible_sprite(fixture.weapon_augmentation.data));
    assert(is_visible_sprite(fixture.armor_augmentation.data));
    assert(memcmp(fixture.armor_augmentation.data + SPRITE_UV_RECT, bolt_uv, sizeof(bolt_uv)) == 0);
    assert(memcmp(fixture.armor_augmentation.data + SPRITE_POSITION,
                  stock.data + SPRITE_POSITION, 4 * sizeof(float)) == 0);

    *(uint32_t *)(armor + 0x50) = 0;
    expect_panel_update(hunter_panel_detour, &fixture, false);
    assert(memcmp(&fixture.armor_augmentation, &stock, sizeof(stock)) == 0);
    assert(is_visible_sprite(fixture.weapon_augmentation.data));
    assert(panel_close_detour(fixture.menu));
    assert(memcmp(&fixture.weapon_augmentation, &stock, sizeof(stock)) == 0);
}

static void test_equipment_info(void) {
    reset_scenario();
    InfoFixture fixture = {0};
    fixture.list_weapon_element = fixture.list_weapon_bolt = fixture.list_armor_bolt = stock;
    fixture.card = fixture.pendant = fixture.occupied = stock;
    fixture_api.set_active(fixture.card.data, true); /* stock tex_great can be active and hidden */
    fixture_api.set_active(fixture.pendant.data, true);
    fixture_api.set_hidden(fixture.pendant.data, false);
    const float bottom_left[] = {0.0f, 28.0f, 0.0f, 1.0f};
    fixture_api.set_position(fixture.pendant.data, bottom_left);
    const Sprite before_card = fixture.card, before_pendant = fixture.pendant;

    fixture.menu[0] = game + INFO_VTABLE;
    fixture.menu[INFO_LIST / 8] = (uintptr_t)fixture.list;
    fixture.menu[INFO_CARD_SPARE / 8] = (uintptr_t)fixture.card.data;
    fixture.menu[INFO_CARD_ROOT / 8] = (uintptr_t)fixture.card_root;
    fixture.card_root[SPRITE_FLAGS / 8] = GUI_COMPONENT_VISIBLE;
    fixture.menu[INFO_CARD_PENDANT / 8] = (uintptr_t)fixture.pendant.data;
    fixture.menu[INFO_CARD_EQUIPPED / 8] = (uintptr_t)fixture.occupied.data;
    *(uint32_t *)((uint8_t *)fixture.menu + INFO_CARD_PAGE) = 6;
    fixture.list[GRID_COUNT / 8] = 2;
    fixture.list[GRID_CELLS / 8] = (uintptr_t)fixture.rows;
    for (uint32_t i = 0; i < 2; ++i) {
        fixture.rows[i] = fixture.row[i];
        fixture.row[i][INFO_ROW_ROOT / 8] = (uintptr_t)fixture.node[i];
        fixture.node[i][GUI_CHILDREN / 8] = (uintptr_t)fixture.children[i];
    }
    fixture.children[0][INFO_ROW_LEFT] = fixture.list_weapon_bolt.data;
    fixture.children[0][INFO_ROW_RIGHT] = fixture.list_weapon_element.data;
    fixture.children[1][INFO_ROW_LEFT] = fixture.list_armor_bolt.data;

    uint32_t *indices = (uint32_t *)((uint8_t *)fixture.menu + INFO_WORN_INDICES);
    for (uint32_t i = 0; i < HUNTER_WORN_SLOTS; ++i) indices[i] = UINT32_MAX;
    indices[0] = 0;
    indices[1] = 1;
    prepare_weapon(0);
    row[MELEE_ELEMENT] = 1;
    work[0x50 / 4] = 1;
    uint8_t *weapon = (uint8_t *)save_data + 0x41068;
    uint8_t *card = (uint8_t *)fixture.menu + INFO_CARD_ITEM;
    memcpy(weapon, work, sizeof(work));
    memcpy(card, weapon, sizeof(work));
    work[WORK_TYPE / 4] = ARMOR;
    memcpy(weapon + 0x98, work, sizeof(work));

    info_open_detour(fixture.menu);
    assert(is_visible_sprite(fixture.list_weapon_element.data));
    assert(is_visible_sprite(fixture.list_weapon_bolt.data));
    assert(is_visible_sprite(fixture.list_armor_bolt.data));
    const float fire_uv[] = {0.0f, 0.0f, 64.0f, 64.0f};
    const float bolt_uv[] = {64.0f, 128.0f, 128.0f, 192.0f};
    assert(memcmp(fixture.list_weapon_element.data + SPRITE_UV_RECT, fire_uv, sizeof(fire_uv)) == 0);
    assert(memcmp(fixture.list_armor_bolt.data + SPRITE_UV_RECT, bolt_uv, sizeof(bolt_uv)) == 0);
    info_reset_detour(fixture.menu);
    assert(is_visible_sprite(fixture.list_weapon_element.data) && is_visible_sprite(fixture.list_armor_bolt.data));

    info_card_detour(fixture.menu);
    assert(is_visible_sprite(fixture.card.data));
    assert(memcmp(fixture.card.data + SPRITE_UV_RECT, fire_uv, sizeof(fire_uv)) == 0);
    const float upper_right[] = {31.0f, 1.0f};
    assert(memcmp(fixture.pendant.data + SPRITE_POSITION, upper_right, sizeof(upper_right)) == 0);
    fixture_api.set_position(fixture.occupied.data, (const float[]){31.0f, 1.0f, 0.0f, 1.0f});
    fixture_api.set_active(fixture.occupied.data, true);
    fixture_api.set_hidden(fixture.occupied.data, false);
    info_card_detour(fixture.menu);
    assert(is_visible_sprite(fixture.card.data) && !is_visible_sprite(fixture.pendant.data));
    assert(is_visible_sprite(fixture.occupied.data));

    memcpy(card, weapon + 0x98, sizeof(work));
    info_card_detour(fixture.menu);
    assert(memcmp(&fixture.card, &before_card, sizeof(fixture.card)) == 0);
    assert(memcmp(&fixture.pendant, &before_pendant, sizeof(fixture.pendant)) == 0);
    info_appearance_detour(fixture.menu, NULL, 1);
    assert(memcmp(&fixture.list_weapon_element, &stock, sizeof(stock)) == 0);
    assert(memcmp(&fixture.list_armor_bolt, &stock, sizeof(stock)) == 0);
    memcpy(card, weapon, sizeof(work));
    info_appearance_detour(fixture.menu, NULL, 0);
    assert(is_visible_sprite(fixture.list_weapon_element.data));
    assert(is_visible_sprite(fixture.card.data));

    *(uint32_t *)(card + WORK_KIND) = INSECT_GLAIVE;
    *(uint32_t *)(card + WORK_GLAIVE_KINSECT_INDEX) = 2;
    *(uint32_t *)((uint8_t *)fixture.menu + INFO_CARD_PAGE) = INFO_KINSECT_PAGE;
    uint8_t *kinsect = weapon + 2 * 0x98;
    *(uint32_t *)(kinsect + WORK_TYPE) = KINSECT;
    *(uint32_t *)(kinsect + WORK_ID) = 7;
    *(uint32_t *)(kinsect + WORK_KINSECT_ELEMENT) = 5;
    info_card_detour(fixture.menu);
    const float dragon_uv[] = {0.0f, 64.0f, 64.0f, 128.0f};
    assert(is_visible_sprite(fixture.card.data));
    assert(memcmp(fixture.card.data + SPRITE_UV_RECT, dragon_uv, sizeof(dragon_uv)) == 0);
    assert(!is_visible_sprite(fixture.pendant.data));
    *(uint32_t *)(card + WORK_GLAIVE_KINSECT_INDEX) = UINT32_MAX;
    info_card_detour(fixture.menu);
    assert(memcmp(&fixture.card, &before_card, sizeof(fixture.card)) == 0);
    info_close_detour(fixture.menu);
    assert(memcmp(&fixture.card, &before_card, sizeof(fixture.card)) == 0);
    assert(memcmp(&fixture.list_weapon_bolt, &stock, sizeof(stock)) == 0);
    assert(memcmp(&fixture.list_armor_bolt, &stock, sizeof(stock)) == 0);
    puts("PASS Equipment Info list and detail badges, pendant conflicts, tab changes, and close cleanup");
}

static void test_equipped_palico(void) {
    PanelFixture fixture;
    prepare_panel(&fixture, true);
    save_data[0x2c8 / 8] = 1;
    prepare_weapon(0);
    row[PALICO_ELEMENT] = 7;
    memcpy((uint8_t *)save_data + 0xb0c30, work, sizeof(work));

    expect_panel_update(palico_panel_detour, &fixture, true);
    const float palico_uv[] = {128.0f, 64.0f, 192.0f, 128.0f};
    assert(read_pointer(fixture.palico.data, 0x150) == &atlas);
    assert(memcmp(fixture.palico.data + 0x15c, palico_uv, sizeof(palico_uv)) == 0);
    const Sprite drawn = fixture.palico;
    expect_panel_update(palico_panel_detour, &fixture, false);
    assert(memcmp(&fixture.palico, &drawn, sizeof(drawn)) == 0);
    expect_panel_update(palico_panel_detour, &fixture, true);
    assert(memcmp(&fixture.palico, &drawn, sizeof(drawn)) == 0);

    assert(memcmp(&fixture.hunter, &stock, sizeof(stock)) == 0);
    *(uint32_t *)((uint8_t *)fixture.menu + 0x1d04) = 3;
    assert(panel_close_detour(fixture.menu) && panel_closes == 1);
    assert(memcmp(&fixture.palico, &stock, sizeof(stock)) == 0);
    expect_panel_update(palico_panel_detour, &fixture, false);
    assert(memcmp(&fixture.palico, &stock, sizeof(stock)) == 0);
    assert(memcmp(&fixture.hunter, &stock, sizeof(stock)) == 0);
}

static void prepare_native_stubs(void) {
    /* Stub addresses match the game image offsets without loading the game. */
    game = (uintptr_t)VirtualAlloc(NULL, GAME_IMAGE_SIZE, MEM_RESERVE, PAGE_NOACCESS);
    assert(game);
    install_stub(BOX_LOOKUP, (uintptr_t)find_equipment);
    install_stub(MELEE_LOOKUP, (uintptr_t)find_weapon);
    install_stub(RANGED_LOOKUP, (uintptr_t)find_weapon);
    install_stub(0x1de5710, (uintptr_t)find_equipment);
    install_stub(0x191b4a0, (uintptr_t)find_palico_weapon);
    install_stub(0x197e690, (uintptr_t)find_kinsect);
    install_stub(0x1b8dbb0, (uintptr_t)get_save);
    install_stub(0x1354930, (uintptr_t)get_hunter_slot);
    install_stub(0xb1c060, (uintptr_t)has_stock_augmentation);
    install_stub(FIND_GUI_CHILD, (uintptr_t)find_gui_child);
    write_global(0x5011f00, work);
    manager[0xa8 / 8] = (uintptr_t)save_data;
    write_global(0x5013950, manager);

    original_bind = stock_bind;
    original_palico_bind = stock_bind;
    original_close = stock_close;
    original_hunter_panel = stock_hunter_panel;
    original_palico_panel = stock_palico_panel;
    original_panel_close = stock_panel_close;
    original_info_open = stock_info_open;
    original_info_appearance = stock_info_appearance;
    original_info_card = stock_info_card;
    original_info_close = stock_info_close;
    original_info_reset = stock_info_reset;
    sprite_api = fixture_api;

    static uint64_t texture[0x18];
    static TextureView item_view;
    texture[0] = game + TEXTURE_VTABLE;
    texture[0xb8 / 8] = UINT64_C(0x0000040000000400);
    item_view = atlas;
    item_view.resource = texture;
    stock = stock_sprite();
    *(uintptr_t *)stock.data = game + SPRITE_VTABLE;
    *(void **)(stock.data + SPRITE_BASE_TEXTURE) = &item_view;
    atlas.resource = texture;
}

static unsigned atlas_load_calls, atlas_release_calls;
static void *test_texture;

static void *load_test_texture(void *dti, const char *name) {
    assert(dti == (void *)(game + TEXTURE_DTI));
    assert(strcmp(name, "ui\\common\\tex\\equipment_element_icons") == 0);
    ++atlas_load_calls;
    return test_texture;
}

static void release_test_texture(void *texture) {
    assert(texture == test_texture);
    ++atlas_release_calls;
}

static void test_private_atlas(void) {
    GridFixture fixture;
    prepare_grid(&fixture, CHANGE_EQUIPMENT, false);
    prepare_weapon(0);
    row[MELEE_ELEMENT] = 1;

    uintptr_t resource_methods[0x70 / 8] = {0};
    resource_methods[0x58 / 8] = game + GET_RESOURCE;
    resource_methods[0x68 / 8] = game + RELEASE_RESOURCE;
    void *resource_manager[] = {resource_methods};
    write_global(RESOURCE_MANAGER, resource_manager);
    install_stub(LOAD_TEXTURE, (uintptr_t)load_test_texture);
    install_stub(RELEASE_TEXTURE, (uintptr_t)release_test_texture);

    atlas.resource = NULL;
    assert(bind_detour(fixture.box, 12, fixture.cell));
    assert(bind_detour(fixture.box, 12, fixture.cell));
    assert(atlas_load_calls == 1 && atlas_release_calls == 0);
    assert(memcmp(&fixture.sprite, &stock, sizeof(stock)) == 0);

    static uint64_t texture[0xc0 / 8];
    texture[0] = game + TEXTURE_VTABLE;
    *(uint32_t *)((uint8_t *)texture + 0x64) = 1;
    texture[0xa8 / 8] = (uintptr_t)&texture;
    texture[0xb8 / 8] = UINT64_C(0x0000040000000200); /* 512x1024 */
    test_texture = texture;
    atlas_attempted = false;
    assert(bind_detour(fixture.box, 12, fixture.cell));
    assert(atlas_load_calls == 2 && atlas_release_calls == 1);
    assert(memcmp(&fixture.sprite, &stock, sizeof(stock)) == 0);

    texture[0xb8 / 8] = UINT64_C(0x0000010000000100); /* 256x256 */
    atlas_attempted = false;
    assert(bind_detour(fixture.box, 12, fixture.cell));
    assert(atlas_load_calls == 3 && atlas_release_calls == 1);
    assert(read_pointer(fixture.sprite.data, 0x150) == &atlas);
    assert(atlas.pixels[2] == 256 && atlas.pixels[3] == 256);
    assert(atlas.inverse_width == 1.0f / 256 && atlas.inverse_height == 1.0f / 256);
    const float fire_uv[] = {0.0f, 0.0f, 64.0f, 64.0f};
    assert(memcmp(fixture.sprite.data + 0x15c, fire_uv, sizeof(fire_uv)) == 0);
    assert(bind_detour(fixture.box, 12, fixture.cell) && atlas_load_calls == 3);
    assert(close_detour(fixture.box) == 7);
    assert(memcmp(&fixture.sprite, &stock, sizeof(stock)) == 0);
    puts("PASS dedicated atlas path, absent or wrong-size resource, and texture binding");
}

int main(void) {
    if (run_badge_cases()) return 1;

    prepare_native_stubs();
    test_hunter_grid_modes();
    test_hunter_grid_reuse();
    test_pendant_with_occupied_corner();
    test_kinsect_grid();
    test_equipped_hunter();
    test_equipped_augmentations();
    test_equipment_info();
    test_equipped_palico();
    test_palico_grid();
    test_private_atlas();
    assert(VirtualFree((void *)game, 0, MEM_RELEASE));

    puts("PASS hunter upgrade/decoration/change grids, Palico grid, equipped slots, and close cleanup");
    return 0;
}
