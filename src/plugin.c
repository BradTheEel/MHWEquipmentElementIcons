#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>
#include <string.h>

#include "MinHook.h"
#include "badge.h"
#include "sprite_layout.h"

/* All RVAs and layouts below are for Steam build 15539686. */
enum {
    GAME_TIMESTAMP = 0x66d13cb6, GAME_IMAGE_SIZE = 0x055d3000,
    BOX_BIND = 0x01de94f0, BOX_CLOSE = 0x01de33d0, BOX_LOOKUP = 0x01de56a0,
    PALICO_BIND = 0x01de92d0, PALICO_BOX_LOOKUP = 0x01de5710,
    BOX_VTABLE = 0x0346b020, SPRITE_VTABLE = 0x03516088,
    MELEE_LOOKUP = 0x0197e840, RANGED_LOOKUP = 0x0197e880,
    PALICO_LOOKUP = 0x0191b4a0, WEAPON_PARAMS = 0x05011f00,
    DUAL_LOOKUP = 0x00462700, DUAL_TABLE = 0x05012190,
    PANEL_VTABLE = 0x0346d968, HUNTER_PANEL_REFRESH = 0x01e0b5f0,
    PALICO_PANEL_REFRESH = 0x01e0b2e0, PANEL_CLOSE = 0x01e0a540,
    SAVE_MANAGER = 0x05013950, ACTIVE_SAVE = 0x01b8dbb0, HUNTER_EQUIPPED_INDEX = 0x01354930,
    LOAD_TEXTURE = 0x02216e50, RELEASE_TEXTURE = 0x022174d0,
    RESOURCE_MANAGER = 0x05123a00, GET_RESOURCE = 0x022228d0, RELEASE_RESOURCE = 0x02225c70,
    TEXTURE_DTI = 0x051c5468, TEXTURE_VTABLE = 0x03512160,
    BIND_TEXTURE = 0x023442c0, SET_UV = 0x02345260, SET_ALPHA = 0x023454b0,
    SET_HIDDEN = 0x02343310, SET_ACTIVE = 0x02345940,
    SET_POSITION = 0x02344580, SET_SIZE = 0x023451e0
};

enum {
    BOX_MODE = 0x28f0, BOX_SUBMODE = 0x28f4, BOX_GRID = 0x2950,
    UPGRADE_EQUIPMENT = 0, CHANGE_EQUIPMENT = 1, SET_DECORATIONS = 4,
    HUNTER_GRID = 0, PALICO_GRID = 1, HUNTER_GRID_LIMIT = 2500, PALICO_GRID_LIMIT = 750,
    MAX_CELLS = 128,
    GRID_COUNT = 0x1d8, GRID_CELLS = 0x1e8,
    CELL_ICON = 0x688,
    ICON_LEFT = 0x80, ICON_RIGHT = 0x88, ICON_WISHLIST = 0xa8,
    ICON_PENDANT = 0xb8, ICON_TYPE = 0xf0,
    WORK_TYPE = 0x18, WORK_KIND = 0x20, WORK_ID = 0x24,
    WEAPON = 1, DUAL_BLADES = 2, BOW = 11,
    MELEE_ELEMENT = 0x25, MELEE_HIDDEN = 0x28, MELEE_COMBO = 0x31,
    BOW_ELEMENT = 0x27, BOW_HIDDEN = 0x2a, COMBO_PRIMARY = 0xc, PALICO_ELEMENT = 0x2e,
    GUI_LIFECYCLE = 0x1d04, GUI_OPENING = 1, GUI_ACTIVE = 2,
    PANEL_MODE = 0x28f0, SAVE_PALICO_EQUIPPED = 0x2c8, WORK_STRIDE = 0x98
};

typedef struct TextureView {
    uint64_t reserved;
    int16_t pixels[4];
    float bounds[4];
    float inverse_width;
    float inverse_height;
    void *resource;
    uint64_t reserved_tail[2];
} TextureView;

_Static_assert(sizeof(TextureView) == 0x40, "game texture view size");
_Static_assert(offsetof(TextureView, resource) == 0x28, "game texture view resource offset");

typedef bool (*BindCell)(void *box, uint32_t index, void *cell);
typedef uintptr_t (*CloseBox)(void *box);
typedef const uint8_t *(*FindEquipment)(void *box, uint32_t index);
typedef const uint8_t *(*FindWeapon)(uint32_t kind, uint32_t id);
typedef const uint8_t *(*FindPalicoWeapon)(uint32_t id);
typedef const uint8_t *(*FindCombo)(void *table, uint32_t id);
typedef void *(*LoadTexture)(void *dti, const char *name);
typedef void (*ReleaseTexture)(void *resource);
typedef const uint8_t *(*ActiveSave)(const void *manager);
typedef uint32_t (*EquippedIndex)(const void *save, uint32_t slot);
typedef void (*RefreshPanel)(void *menu, bool force);
typedef bool (*ClosePanel)(void *menu);

typedef struct WeaponElement {
    unsigned active, hidden;
} WeaponElement;

typedef enum EquippedSlotId {
    EQUIPPED_HUNTER,
    EQUIPPED_PALICO,
    EQUIPPED_SLOT_COUNT
} EquippedSlotId;

typedef struct EquippedSlot {
    uint32_t mode;
    size_t icon_offset;
    size_t cache_offset;
    size_t work_offset;
    uint32_t work_capacity;
} EquippedSlot;

static const EquippedSlot equipped_slots[EQUIPPED_SLOT_COUNT] = {
    [EQUIPPED_HUNTER] = {
        .mode = 0, .icon_offset = 0x2900, .cache_offset = 0x3c20,
        .work_offset = 0x41068, .work_capacity = 3010
    },
    [EQUIPPED_PALICO] = {
        .mode = 1, .icon_offset = 0x3560, .cache_offset = 0x3c48,
        .work_offset = 0xb0b98, .work_capacity = 1250
    }
};

typedef struct EquippedBadge {
    void *menu;
    BadgeState badge;
    WeaponElement element;
} EquippedBadge;

static uintptr_t game;
static BindCell original_bind;
static BindCell original_palico_bind;
static CloseBox original_close;
static RefreshPanel original_hunter_panel, original_palico_panel;
static ClosePanel original_panel_close;
static BadgeApi sprite_api;
static TextureView atlas = {0, {0, 0, 512, 1024}, {0.0f, 0.0f, 1.0f, 1.0f},
                            1.0f / 512.0f, 1.0f / 1024.0f, NULL, {0, 0}};
static bool atlas_attempted;
static volatile LONG hooks_ready;
static SRWLOCK ui_lock = SRWLOCK_INIT;
static void *owned_box;
static BadgeState badges[MAX_CELLS];
static EquippedBadge equipped_badges[EQUIPPED_SLOT_COUNT];

static uint32_t read_u32(const void *object, size_t offset) {
    return *(const uint32_t *)((const uint8_t *)object + offset);
}

static void *read_pointer(const void *object, size_t offset) {
    return *(void *const *)((const uint8_t *)object + offset);
}

static bool has_vtable(const void *object, uintptr_t rva) {
    return object && read_pointer(object, 0) == (void *)(game + rva);
}

static bool installed(void) {
    return InterlockedCompareExchange(&hooks_ready, 0, 0) != 0;
}

static bool load_status_atlas(void) {
    if (atlas.resource) return true;
    if (atlas_attempted) return false;

    const void *manager = read_pointer((void *)(game + RESOURCE_MANAGER), 0);
    if (!manager) return false;
    const void *vtable = read_pointer(manager, 0);
    if (!vtable || read_pointer(vtable, 0x58) != (void *)(game + GET_RESOURCE) ||
        read_pointer(vtable, 0x68) != (void *)(game + RELEASE_RESOURCE)) return false;

    atlas_attempted = true;
    void *texture = ((LoadTexture)(game + LOAD_TEXTURE))((void *)(game + TEXTURE_DTI),
                                                       "ui\\common\\tex\\cmn_status00_ID");
    if (!texture) return false;
    if (!has_vtable(texture, TEXTURE_VTABLE) || !(read_u32(texture, 0x64) & 1u) ||
        !read_pointer(texture, 0xa8) || read_u32(texture, 0xb8) != 512 ||
        read_u32(texture, 0xbc) != 1024) {
        ((ReleaseTexture)(game + RELEASE_TEXTURE))(texture);
        return false;
    }
    /* The UI only borrows the view. Retain this single resource ref for the process. */
    atlas.resource = texture;
    return true;
}

static void **grid_cells(void *box, uint32_t *count) {
    void *grid = read_pointer(box, BOX_GRID);
    *count = grid ? read_u32(grid, GRID_COUNT) : 0;
    if (!*count || *count > MAX_CELLS) return NULL;
    return read_pointer(grid, GRID_CELLS);
}

static BadgeState *find_badge(void *box, void *cell) {
    uint32_t count;
    void **cells = grid_cells(box, &count);
    if (!cells) return NULL;
    for (uint32_t i = 0; i < count; ++i) {
        if (cells[i] == cell) return &badges[i];
    }
    return NULL;
}

static bool is_visible_sprite(void *layer) {
    return has_vtable(layer, SPRITE_VTABLE) &&
           (read_u32(layer, SPRITE_FLAGS) & SPRITE_VISIBILITY_MASK) == SPRITE_ACTIVE;
}

static bool has_base_texture(void *layer) {
    if (!has_vtable(layer, SPRITE_VTABLE)) return false;
    void *view = read_pointer(layer, SPRITE_BASE_TEXTURE);
    void *texture = view ? read_pointer(view, 0x28) : NULL;
    return has_vtable(texture, TEXTURE_VTABLE) && read_u32(texture, 0xb8) && read_u32(texture, 0xbc);
}

static void restore_icon(BadgeState *state, void *icon) {
    if (!state->layer) return;
    void *layer = icon ? read_pointer(icon, ICON_LEFT) : NULL;
    if (state->layer == layer && has_base_texture(layer)) {
        void *pendant = read_pointer(icon, ICON_PENDANT);
        if (!has_vtable(pendant, SPRITE_VTABLE)) pendant = NULL;
        badge_restore(&sprite_api, state, layer, pendant, &atlas);
    }
    *state = (BadgeState){0};
}

static WeaponElement weapon_element(const uint8_t *item, bool palico) {
    const WeaponElement none = {0};
    if (!item || read_u32(item, WORK_TYPE) != WEAPON) return none;
    const uint32_t id = read_u32(item, WORK_ID);
    if (palico) {
        if (id >= 201 || !read_pointer((void *)(game + WEAPON_PARAMS), 0)) return none;
        const uint8_t *row = ((FindPalicoWeapon)(game + PALICO_LOOKUP))(id);
        return row ? (WeaponElement){row[PALICO_ELEMENT], 0} : none;
    }
    const uint32_t kind = read_u32(item, WORK_KIND);
    if (kind > BOW || id >= 321) return none;

    const bool bow = kind == BOW;
    const uint8_t *row = ((FindWeapon)(game + (bow ? RANGED_LOOKUP : MELEE_LOOKUP)))(kind, id);
    if (!row) return none;
    if (kind == DUAL_BLADES && row[MELEE_COMBO]) {
        void *table = read_pointer((void *)(game + DUAL_TABLE), 0);
        const uint8_t *combo = table && read_pointer(table, 0xc8)
                               ? ((FindCombo)(game + DUAL_LOOKUP))(table, row[MELEE_COMBO]) : NULL;
        return combo ? (WeaponElement){combo[COMBO_PRIMARY], 0} : none;
    }
    return (WeaponElement){row[bow ? BOW_ELEMENT : MELEE_ELEMENT], row[bow ? BOW_HIDDEN : MELEE_HIDDEN]};
}

static bool show_icon(BadgeState *state, void *icon, WeaponElement element) {
    BadgeStyle style;
    if (!badge_resolve(element.active, element.hidden, &style)) return false;
    void *layer = read_pointer(icon, ICON_LEFT);
    if (!has_base_texture(layer)) return false;

    void *pendant = read_pointer(icon, ICON_PENDANT);
    if (!is_visible_sprite(pendant)) pendant = NULL;
    if (pendant && (is_visible_sprite(read_pointer(icon, ICON_RIGHT)) ||
                    is_visible_sprite(read_pointer(icon, ICON_WISHLIST)))) return false;
    return load_status_atlas() && badge_show(&sprite_api, state, layer, pendant, &atlas, style);
}

static bool supported_box_mode(void *box, bool palico) {
    const uint32_t mode = read_u32(box, BOX_MODE);
    const uint32_t submode = read_u32(box, BOX_SUBMODE);
    if (palico) return submode == PALICO_GRID && mode == CHANGE_EQUIPMENT;
    return submode == HUNTER_GRID &&
           (mode == UPGRADE_EQUIPMENT || mode == CHANGE_EQUIPMENT || mode == SET_DECORATIONS);
}

static bool gui_is_open(const void *menu) {
    const uint32_t state = read_u32(menu, GUI_LIFECYCLE);
    return state == GUI_OPENING || state == GUI_ACTIVE;
}

static bool bind_cell(void *box, uint32_t index, void *cell, bool palico, BindCell original) {
    if (!installed() || !cell || !has_vtable(box, BOX_VTABLE)) return original(box, index, cell);
    void *icon = (uint8_t *)cell + CELL_ICON;

    AcquireSRWLockExclusive(&ui_lock);
    if (owned_box == box) {
        BadgeState *state = find_badge(box, cell);
        if (state) restore_icon(state, icon);
    }
    ReleaseSRWLockExclusive(&ui_lock);

    const bool result = original(box, index, cell);
    if (!result || index >= (palico ? PALICO_GRID_LIMIT : HUNTER_GRID_LIMIT) ||
        !gui_is_open(box) || !supported_box_mode(box, palico) || read_u32(icon, ICON_TYPE) != WEAPON)
        return result;

    AcquireSRWLockExclusive(&ui_lock);
    if (!owned_box || owned_box == box) {
        BadgeState *state = find_badge(box, cell);
        if (state && !state->layer) {
            const FindEquipment lookup = (FindEquipment)(game + (palico ? PALICO_BOX_LOOKUP : BOX_LOOKUP));
            if (show_icon(state, icon, weapon_element(lookup(box, index), palico))) owned_box = box;
        }
    }
    ReleaseSRWLockExclusive(&ui_lock);
    return result;
}

static bool bind_detour(void *box, uint32_t index, void *cell) {
    return bind_cell(box, index, cell, false, original_bind);
}

static bool palico_bind_detour(void *box, uint32_t index, void *cell) {
    return bind_cell(box, index, cell, true, original_palico_bind);
}

static uintptr_t close_detour(void *box) {
    if (!installed()) return original_close(box);
    AcquireSRWLockExclusive(&ui_lock);
    if (owned_box == box && has_vtable(box, BOX_VTABLE)) {
        uint32_t count;
        void **cells = grid_cells(box, &count);
        if (cells) {
            for (uint32_t i = 0; i < count; ++i)
                restore_icon(&badges[i], cells[i] ? (uint8_t *)cells[i] + CELL_ICON : NULL);
        }
        memset(badges, 0, sizeof(badges));
        owned_box = NULL;
    }
    ReleaseSRWLockExclusive(&ui_lock);
    return original_close(box);
}

static const uint8_t *active_save(void) {
    const void *manager = read_pointer((void *)(game + SAVE_MANAGER), 0);
    if (!manager || read_u32(manager, 0xa0) >= 3 || !read_pointer(manager, 0xa8)) return NULL;
    return ((ActiveSave)(game + ACTIVE_SAVE))(manager);
}

static void refresh_panel(void *menu, bool force, EquippedSlotId slot_id, RefreshPanel original) {
    if (!installed() || !has_vtable(menu, PANEL_VTABLE)) {
        original(menu, force);
        return;
    }
    const EquippedSlot *slot = &equipped_slots[slot_id];
    const bool palico = slot_id == EQUIPPED_PALICO;
    const bool selected = gui_is_open(menu) && read_u32(menu, PANEL_MODE) == slot->mode;
    void *icon = (uint8_t *)menu + slot->icon_offset;
    EquippedBadge *state = &equipped_badges[slot_id];
    const uint8_t *save = selected ? active_save() : NULL;
    uint32_t index = UINT32_MAX;
    if (save) index = palico ? read_u32(save, SAVE_PALICO_EQUIPPED)
                             : ((EquippedIndex)(game + HUNTER_EQUIPPED_INDEX))(save, 0);

    /* Stock skips unchanged inventory indices unless the caller requests a refresh. */
    if (!selected || force || index != read_u32(menu, slot->cache_offset)) {
        AcquireSRWLockExclusive(&ui_lock);
        if (state->menu == menu) {
            restore_icon(&state->badge, icon);
            *state = (EquippedBadge){0};
        }
        ReleaseSRWLockExclusive(&ui_lock);
    }
    original(menu, force);

    WeaponElement element = {0};
    if (save && gui_is_open(menu) && read_u32(menu, PANEL_MODE) == slot->mode &&
        read_u32(menu, slot->cache_offset) == index && index < slot->work_capacity &&
        read_u32(icon, ICON_TYPE) == WEAPON) {
        const uint8_t *item = save + slot->work_offset + index * WORK_STRIDE;
        element = weapon_element(item, palico);
    }

    AcquireSRWLockExclusive(&ui_lock);
    if (state->menu == menu) {
        void *layer = read_pointer(icon, ICON_LEFT);
        if (state->badge.layer == layer && state->element.active == element.active &&
            state->element.hidden == element.hidden && is_visible_sprite(layer) &&
            read_pointer(layer, SPRITE_OVERRIDE_TEXTURE) == &atlas) {
            ReleaseSRWLockExclusive(&ui_lock);
            return;
        }
        restore_icon(&state->badge, icon);
        *state = (EquippedBadge){0};
    }
    if (!state->menu && show_icon(&state->badge, icon, element)) {
        state->menu = menu;
        state->element = element;
    }
    ReleaseSRWLockExclusive(&ui_lock);
}

static void hunter_panel_detour(void *menu, bool force) {
    refresh_panel(menu, force, EQUIPPED_HUNTER, original_hunter_panel);
}

static void palico_panel_detour(void *menu, bool force) {
    refresh_panel(menu, force, EQUIPPED_PALICO, original_palico_panel);
}

static bool panel_close_detour(void *menu) {
    if (installed() && has_vtable(menu, PANEL_VTABLE)) {
        AcquireSRWLockExclusive(&ui_lock);
        for (unsigned i = 0; i < EQUIPPED_SLOT_COUNT; ++i) {
            if (equipped_badges[i].menu == menu) {
                restore_icon(&equipped_badges[i].badge, (uint8_t *)menu + equipped_slots[i].icon_offset);
                equipped_badges[i] = (EquippedBadge){0};
            }
        }
        ReleaseSRWLockExclusive(&ui_lock);
    }
    return original_panel_close(menu);
}

/* Reject incompatible image layouts and entry detours already installed by another mod. */
static bool supported_game(uintptr_t base) {
    if (!base) return false;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x800) return false;
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->FileHeader.TimeDateStamp != GAME_TIMESTAMP || nt->OptionalHeader.SizeOfImage != GAME_IMAGE_SIZE)
        return false;

    static const struct { uint32_t rva; uint8_t bytes[16]; } entries[] = {
        {BOX_BIND,        {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x41,0x56,0x41,0x57,0x48}},
        {BOX_CLOSE,       {0x48,0x89,0x5c,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x33,0xff,0x48,0x8b,0xd9,0x48}},
        {BOX_LOOKUP,      {0x40,0x53,0x48,0x83,0xec,0x20,0x8b,0x81,0x78,0x29,0x00,0x00,0x8b,0xda,0x85,0xc0}},
        {PALICO_BIND,     {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x57,0x48,0x83}},
        {PALICO_BOX_LOOKUP,{0x40,0x53,0x48,0x83,0xec,0x20,0x8b,0x81,0x78,0x29,0x00,0x00,0x25,0x00,0x0f,0x00}},
        {PALICO_LOOKUP,   {0x8b,0xd1,0x83,0xf9,0xff,0x75,0x03,0x33,0xc0,0xc3,0x48,0x8b,0x0d,0x4f,0x6a,0x6f}},
        {HUNTER_PANEL_REFRESH,{0x40,0x53,0x56,0x41,0x54,0x41,0x55,0x41,0x57,0x48,0x83,0xec,0x20,0x48,0x89,0x6c}},
        {PALICO_PANEL_REFRESH,{0x40,0x53,0x57,0x41,0x54,0x41,0x55,0x41,0x57,0x48,0x83,0xec,0x20,0x44,0x0f,0xb6}},
        {PANEL_CLOSE,     {0x48,0x89,0x6c,0x24,0x20,0x57,0x48,0x83,0xec,0x20,0x83,0x89,0x98,0x22,0x00,0x00}},
        {ACTIVE_SAVE,     {0x48,0x63,0x81,0xa0,0x00,0x00,0x00,0x48,0x69,0xc0,0x00,0xcc,0x26,0x00,0x48,0x03}},
        {HUNTER_EQUIPPED_INDEX,{0x80,0xb9,0xa0,0x91,0x26,0x00,0x00,0x4c,0x8b,0xc9,0x74,0x43,0x8d,0x42,0xf9,0x83}},
        {MELEE_LOOKUP,    {0x8b,0xc1,0x83,0xfa,0xff,0x75,0x03,0x33,0xc0,0xc3,0x48,0x8b,0x0d,0xaf,0x36,0x69}},
        {RANGED_LOOKUP,   {0x8b,0xc1,0x83,0xfa,0xff,0x75,0x03,0x33,0xc0,0xc3,0x48,0x8b,0x0d,0x6f,0x36,0x69}},
        {DUAL_LOOKUP,     {0x48,0x8b,0xc1,0x3b,0x91,0xb8,0x00,0x00,0x00,0x72,0x03,0x33,0xc0,0xc3,0x48,0x8b}},
        {LOAD_TEXTURE,    {0x4c,0x8b,0xd1,0x4c,0x8b,0xc2,0x48,0x8b,0x0d,0xa3,0xcb,0xf0,0x02,0x41,0xb9,0x01}},
        {RELEASE_TEXTURE, {0x48,0x8b,0xd1,0x48,0x8b,0x0d,0x26,0xc5,0xf0,0x02,0x48,0x8b,0x01,0x48,0xff,0x60}},
        {BIND_TEXTURE,    {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x30,0x48,0x8b,0xb9,0x50,0x01,0x00}},
        {SET_UV,          {0x40,0x53,0x48,0x83,0xec,0x20,0xf3,0x0f,0x10,0x02,0x48,0x8b,0xd9,0x0f,0x2e,0x81}},
        {SET_ALPHA,       {0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x38,0x91,0xd3,0x00,0x00,0x00,0x74}},
        {SET_HIDDEN,      {0x48,0x83,0xec,0x28,0x44,0x8b,0x41,0x58,0x41,0x8b,0xc0,0xc1,0xe8,0x1a,0x24,0x01}},
        {SET_ACTIVE,      {0x40,0x57,0x48,0x83,0xec,0x20,0x0f,0xb6,0x41,0x58,0x48,0x8b,0xf9,0x24,0x01,0x3a}},
        {SET_POSITION,    {0xf3,0x0f,0x10,0x0a,0x0f,0x2e,0x89,0xa0,0x00,0x00,0x00,0x75,0x2a,0xf3,0x0f,0x10}},
        {SET_SIZE,        {0x40,0x53,0x48,0x83,0xec,0x20,0xf3,0x0f,0x10,0x02,0x48,0x8b,0xd9,0x0f,0x2e,0x81}},
    };
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i) {
        if (memcmp((const void *)(base + entries[i].rva), entries[i].bytes, sizeof(entries[i].bytes)))
            return false;
    }
    return read_pointer((void *)(base + BOX_VTABLE), 0x2d8) == (void *)(base + BOX_CLOSE) &&
           read_pointer((void *)(base + PANEL_VTABLE), 0x2d8) == (void *)(base + PANEL_CLOSE);
}

static DWORD WINAPI install(void *unused) {
    (void)unused;
    game = (uintptr_t)GetModuleHandleW(NULL);
    if (!supported_game(game)) return 0;

    sprite_api = (BadgeApi){
        (void *(*)(void *, void *))(game + BIND_TEXTURE),
        (void (*)(void *, const float *))(game + SET_UV),
        (void (*)(void *, uint8_t))(game + SET_ALPHA),
        (void (*)(void *, bool))(game + SET_HIDDEN),
        (void (*)(void *, bool))(game + SET_ACTIVE),
        (void (*)(void *, const float *))(game + SET_POSITION),
        (void (*)(void *, const float *))(game + SET_SIZE)
    };

    HMODULE pinned;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            (LPCWSTR)&install, &pinned)) return 0;
    if (MH_Initialize() != MH_OK) return 0;

    const struct { uintptr_t rva; void *detour; void **original; } hooks[] = {
        {BOX_CLOSE, (void *)close_detour, (void **)&original_close},
        {PANEL_CLOSE, (void *)panel_close_detour, (void **)&original_panel_close},
        {BOX_BIND, (void *)bind_detour, (void **)&original_bind},
        {PALICO_BIND, (void *)palico_bind_detour, (void **)&original_palico_bind},
        {HUNTER_PANEL_REFRESH, (void *)hunter_panel_detour, (void **)&original_hunter_panel},
        {PALICO_PANEL_REFRESH, (void *)palico_panel_detour, (void **)&original_palico_panel},
    };
    for (size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        void *target = (void *)(game + hooks[i].rva);
        if (MH_CreateHook(target, hooks[i].detour, hooks[i].original) != MH_OK ||
            MH_QueueEnableHook(target) != MH_OK) {
            MH_Uninitialize();
            return 0;
        }
    }
    if (MH_ApplyQueued() == MH_OK) {
        InterlockedExchange(&hooks_ready, 1);
    } else {
        /* Leave trampolines allocated for a passthrough already in flight. */
        MH_DisableHook(MH_ALL_HOOKS);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(NULL, 0, install, NULL, 0, NULL);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
