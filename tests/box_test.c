/* Exercise the production detours against owned grid objects and ABI stubs. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>

#include "sprite_fixture.h"
#include "../src/plugin.c"

int run_badge_cases(void);

static uint32_t work[16];
static uint8_t row[0x60], swapped_weapon_row[0x60];
static unsigned bind_calls, close_calls;
static bool item_present;
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
    assert(slot == 0);
    return read_u32(save, 0xa8);
}

static void stock_panel_update(void *menu, bool force, bool palico) {
    assert(read_u32(menu, 0x28f0) == (uint32_t)palico);
    ++panel_updates;
    panel_force = force;
    uint8_t *icon = (uint8_t *)menu + (palico ? 0x3560 : 0x2900);
    uint32_t *cache = (uint32_t *)((uint8_t *)menu + (palico ? 0x3c48 : 0x3c20));
    const uint32_t index = read_u32(save_data, palico ? 0x2c8 : 0xa8);
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

typedef struct GridFixture {
    uint64_t box[0x2960 / 8], grid[0x200 / 8], cell[0x780 / 8];
    void *cells[1];
    Sprite sprite;
} GridFixture;

typedef struct PanelFixture {
    uint64_t menu[0x3c50 / 8];
    Sprite hunter, palico;
} PanelFixture;

static Sprite stock;
static uint64_t manager[0xb0 / 8];

static void reset_scenario(void) {
    memset(work, 0, sizeof(work));
    memset(row, 0, sizeof(row));
    memset(swapped_weapon_row, 0, sizeof(swapped_weapon_row));
    memset(save_data, 0, sizeof(save_data));
    memset(badges, 0, sizeof(badges));
    memset(equipped_badges, 0, sizeof(equipped_badges));
    owned_box = NULL;
    bind_calls = close_calls = panel_updates = panel_closes = 0;
    panel_force = false;
    item_present = true;
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
    fixture.box[BOX_MODE / 8] = 5; /* unrelated equipment selector */
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
    const float fire_uv[] = {192.0f, 0.0f, 256.0f, 64.0f};
    assert(memcmp(fixture.hunter.data + 0x15c, fire_uv, sizeof(fire_uv)) == 0);
    const Sprite drawn = fixture.hunter;
    expect_panel_update(hunter_panel_detour, &fixture, false);
    assert(memcmp(&fixture.hunter, &drawn, sizeof(drawn)) == 0);
    expect_panel_update(hunter_panel_detour, &fixture, true);
    assert(read_pointer(fixture.hunter.data, 0x150) == &atlas);

    save_data[0xa8 / 8] = 1;
    expect_panel_update(hunter_panel_detour, &fixture, false);
    const float thunder_uv[] = {320.0f, 0.0f, 384.0f, 64.0f};
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
    install_stub(0x1b8dbb0, (uintptr_t)get_save);
    install_stub(0x1354930, (uintptr_t)get_hunter_slot);
    write_global(0x5011f00, work);
    manager[0xa8 / 8] = (uintptr_t)save_data;
    write_global(0x5013950, manager);

    original_bind = stock_bind;
    original_palico_bind = stock_bind;
    original_close = stock_close;
    original_hunter_panel = stock_hunter_panel;
    original_palico_panel = stock_palico_panel;
    original_panel_close = stock_panel_close;
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

int main(void) {
    if (run_badge_cases()) return 1;

    prepare_native_stubs();
    test_hunter_grid_modes();
    test_hunter_grid_reuse();
    test_equipped_hunter();
    test_equipped_palico();
    test_palico_grid();
    assert(VirtualFree((void *)game, 0, MEM_RELEASE));

    puts("PASS hunter upgrade/decoration/change grids, Palico grid, equipped slots, and close cleanup");
    return 0;
}
