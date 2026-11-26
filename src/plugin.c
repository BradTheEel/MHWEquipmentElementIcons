#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>
#include <string.h>

#include "MinHook.h"
#include "atlas_layout.h"
#include "badge.h"
#include "sprite_layout.h"

/* All RVAs and layouts below are for Steam build 15539686. */
enum {
    GAME_TIMESTAMP = 0x66d13cb6, GAME_IMAGE_SIZE = 0x055d3000,
    BOX_BIND = 0x01de94f0, BOX_CLOSE = 0x01de33d0, BOX_LOOKUP = 0x01de56a0,
    PALICO_BIND = 0x01de92d0, PALICO_BOX_LOOKUP = 0x01de5710,
    BOX_VTABLE = 0x0346b020, SPRITE_VTABLE = 0x03516088,
    MELEE_LOOKUP = 0x0197e840, RANGED_LOOKUP = 0x0197e880,
    PALICO_LOOKUP = 0x0191b4a0, KINSECT_LOOKUP = 0x0197e690, WEAPON_PARAMS = 0x05011f00,
    DUAL_LOOKUP = 0x00462700, DUAL_TABLE = 0x05012190,
    PANEL_VTABLE = 0x0346d968, HUNTER_PANEL_REFRESH = 0x01e0b5f0,
    PALICO_PANEL_REFRESH = 0x01e0b2e0, PANEL_CLOSE = 0x01e0a540,
    HAS_AUGMENTATION = 0x00b1c060,
    INFO_VTABLE = 0x03483d18, INFO_OPEN = 0x01ee7ab0, INFO_APPEARANCE = 0x01edaaa0,
    INFO_CARD_REFRESH = 0x01ee5320, INFO_CLOSE = 0x01edf320, INFO_RESET = 0x01edf420,
    FIND_GUI_CHILD = 0x02340e20,
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
    UPGRADE_EQUIPMENT = 0, CHANGE_EQUIPMENT = 1, SET_DECORATIONS = 4, NURTURE_KINSECT = 5,
    HUNTER_GRID = 0, PALICO_GRID = 1, HUNTER_GRID_LIMIT = 2500, PALICO_GRID_LIMIT = 750,
    MAX_CELLS = 128,
    GRID_COUNT = 0x1d8, GRID_CELLS = 0x1e8,
    CELL_ICON = 0x688,
    ICON_LEFT = 0x80, ICON_RIGHT = 0x88, ICON_WISHLIST = 0xa8, ICON_AUGMENTATION = 0xb0,
    ICON_PENDANT = 0xb8, ICON_TYPE = 0xf0,
    WORK_TYPE = 0x18, WORK_KIND = 0x20, WORK_ID = 0x24, WORK_KINSECT_ELEMENT = 0x3c,
    ARMOR = 0, WEAPON = 1, KINSECT = 4, DUAL_BLADES = 2, BOW = 11,
    KINSECT_MODEL_COUNT = 201, KINSECT_MAX_ELEMENT = 5,
    MELEE_ELEMENT = 0x25, MELEE_HIDDEN = 0x28, MELEE_COMBO = 0x31,
    BOW_ELEMENT = 0x27, BOW_HIDDEN = 0x2a, COMBO_PRIMARY = 0xc, PALICO_ELEMENT = 0x2e,
    GUI_LIFECYCLE = 0x1d04, GUI_OPENING = 1, GUI_ACTIVE = 2,
    PANEL_MODE = 0x28f0, PANEL_ICON_STRIDE = 0x118, HUNTER_WORN_SLOTS = 6,
    SAVE_PALICO_EQUIPPED = 0x2c8, WORK_STRIDE = 0x98,

    INFO_MODE = 0x28f0, INFO_SUBMODE = 0x28f4, INFO_HUNTER_MODE = 1, INFO_EQUIPMENT_SUBMODE = 0x26,
    INFO_REMOTE_HUNTER = 0xd2a0, INFO_LIST = 0xd938, INFO_WORN_INDICES = 0xd900,
    INFO_ROW_ROOT = 0x1c0, GUI_CHILDREN = 0xe8, INFO_ROW_LEFT = 9, INFO_ROW_RIGHT = 10,
    INFO_CARD_ITEM = 0xa7b0, INFO_CARD_PAGE = 0xdaf8, INFO_CARD_DETAIL_KIND = 0x8270,
    INFO_CARD_ROOT = 0x2948, GUI_COMPONENT_VISIBLE = 1u << 1,
    INFO_CARD_SPARE = 0x2a00, INFO_CARD_EQUIPPED = 0x29e0,
    INFO_CARD_AUGMENTATION = 0x2a20, INFO_CARD_PENDANT = 0x2a30,
    INFO_KINSECT_PAGE = 0xd, INSECT_GLAIVE = 0xa, WORK_GLAIVE_KINSECT_INDEX = 0x3c
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

enum {
    TEXTURE_FLAGS = 0x64, TEXTURE_SHADER = 0xa8,
    TEXTURE_WIDTH = 0xb8, TEXTURE_HEIGHT = 0xbc,
    TEXTURE_LOADED = 1
};

_Static_assert(sizeof(TextureView) == 0x40, "game texture view size");
_Static_assert(offsetof(TextureView, resource) == 0x28, "game texture view resource offset");

typedef bool (*BindCell)(void *box, uint32_t index, void *cell);
typedef uintptr_t (*CloseBox)(void *box);
typedef const uint8_t *(*FindEquipment)(void *box, uint32_t index);
typedef const uint8_t *(*FindWeapon)(uint32_t kind, uint32_t id);
typedef const uint8_t *(*FindModel)(uint32_t id);
typedef const uint8_t *(*FindCombo)(void *table, uint32_t id);
typedef void *(*LoadTexture)(void *dti, const char *name);
typedef void (*ReleaseTexture)(void *resource);
typedef const uint8_t *(*ActiveSave)(const void *manager);
typedef uint32_t (*EquippedIndex)(const void *save, uint32_t slot);
typedef bool (*HasAugmentation)(const void *item);
typedef void (*InfoUpdate)(void *menu);
typedef void (*InfoSelectTab)(void *menu, void *list, int selection);
typedef void *(*FindGuiChild)(void *object, const char *name);
typedef void (*RefreshPanel)(void *menu, bool force);
typedef bool (*ClosePanel)(void *menu);

typedef struct EquipmentEffect {
    unsigned active, hidden;
} EquipmentEffect;

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
    EquipmentEffect element;
} EquippedBadge;

typedef struct InfoBadges {
    void *menu;
    bool show_equipment;
    BadgeState list_element;
    BadgeState list_augmentations[HUNTER_WORN_SLOTS];
    BadgeState card_element;
} InfoBadges;

static uintptr_t game;
static BindCell original_bind;
static BindCell original_palico_bind;
static CloseBox original_close;
static RefreshPanel original_hunter_panel, original_palico_panel;
static ClosePanel original_panel_close;
static InfoUpdate original_info_open, original_info_card, original_info_close, original_info_reset;
static InfoSelectTab original_info_appearance;
static BadgeApi sprite_api;
static TextureView atlas = {
    .pixels = {0, 0, ATLAS_WIDTH, ATLAS_HEIGHT},
    .bounds = {0.0f, 0.0f, 1.0f, 1.0f},
    .inverse_width = 1.0f / ATLAS_WIDTH,
    .inverse_height = 1.0f / ATLAS_HEIGHT
};
static bool atlas_attempted;
static volatile LONG hooks_ready;
static SRWLOCK ui_lock = SRWLOCK_INIT;
static void *owned_box;
static BadgeState badges[MAX_CELLS];
static EquippedBadge equipped_badges[EQUIPPED_SLOT_COUNT];
static void *augmentation_panel;
static BadgeState worn_augmentations[HUNTER_WORN_SLOTS];
static InfoBadges info_badges;

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

static bool load_badge_atlas(void) {
    if (atlas.resource) return true;
    if (atlas_attempted) return false;

    const void *manager = read_pointer((void *)(game + RESOURCE_MANAGER), 0);
    if (!manager) return false;
    const void *vtable = read_pointer(manager, 0);
    if (!vtable || read_pointer(vtable, 0x58) != (void *)(game + GET_RESOURCE) ||
        read_pointer(vtable, 0x68) != (void *)(game + RELEASE_RESOURCE)) return false;

    atlas_attempted = true;
    void *texture = ((LoadTexture)(game + LOAD_TEXTURE))((void *)(game + TEXTURE_DTI), ATLAS_RESOURCE_PATH);
    if (!texture) return false;
    if (!has_vtable(texture, TEXTURE_VTABLE) || !(read_u32(texture, TEXTURE_FLAGS) & TEXTURE_LOADED) ||
        !read_pointer(texture, TEXTURE_SHADER) || read_u32(texture, TEXTURE_WIDTH) != ATLAS_WIDTH ||
        read_u32(texture, TEXTURE_HEIGHT) != ATLAS_HEIGHT) {
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
    return has_vtable(texture, TEXTURE_VTABLE) && read_u32(texture, TEXTURE_WIDTH) &&
           read_u32(texture, TEXTURE_HEIGHT);
}

static void restore_layer(BadgeState *state, void *layer, void *pendant) {
    if (layer && state->layer == layer && has_base_texture(layer)) {
        if (!has_vtable(pendant, SPRITE_VTABLE)) pendant = NULL;
        badge_restore(&sprite_api, state, layer, pendant, &atlas);
    }
    *state = (BadgeState){0};
}

static void restore_icon(BadgeState *state, void *icon) {
    if (!state->layer) return;
    restore_layer(state, icon ? read_pointer(icon, ICON_LEFT) : NULL,
                  icon ? read_pointer(icon, ICON_PENDANT) : NULL);
}

static EquipmentEffect weapon_element(const uint8_t *item, bool palico) {
    const EquipmentEffect none = {0};
    if (!item || read_u32(item, WORK_TYPE) != WEAPON) return none;
    const uint32_t id = read_u32(item, WORK_ID);
    if (palico) {
        if (id >= 201 || !read_pointer((void *)(game + WEAPON_PARAMS), 0)) return none;
        const uint8_t *row = ((FindModel)(game + PALICO_LOOKUP))(id);
        return row ? (EquipmentEffect){row[PALICO_ELEMENT], 0} : none;
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
        return combo ? (EquipmentEffect){combo[COMBO_PRIMARY], 0} : none;
    }
    return (EquipmentEffect){row[bow ? BOW_ELEMENT : MELEE_ELEMENT], row[bow ? BOW_HIDDEN : MELEE_HIDDEN]};
}

static EquipmentEffect kinsect_element(const uint8_t *item) {
    const EquipmentEffect none = {0};
    if (!item || read_u32(item, WORK_TYPE) != KINSECT) return none;

    /* The stock Kinsect details read the assigned element from the owned item. */
    const uint32_t element = read_u32(item, WORK_KINSECT_ELEMENT);
    const uint32_t id = read_u32(item, WORK_ID);
    if (!element || element > KINSECT_MAX_ELEMENT || id >= KINSECT_MODEL_COUNT ||
        !read_pointer((void *)(game + WEAPON_PARAMS), 0)) return none;

    /* The box binder can return success even when the model row is unavailable. */
    if (!((FindModel)(game + KINSECT_LOOKUP))(id)) return none;
    return (EquipmentEffect){.active = element};
}

static bool show_element(BadgeState *state, void *layer, EquipmentEffect element, void *pendant,
                         const BadgePlacement *placement, const float *pendant_corner) {
    BadgeStyle style;
    return badge_resolve(element.active, element.hidden, &style) && has_base_texture(layer) &&
           load_badge_atlas() &&
           badge_show(&sprite_api, state, layer, pendant, &atlas, style, placement, pendant_corner);
}

static bool show_icon(BadgeState *state, void *icon, EquipmentEffect element) {
    static const BadgePlacement placement = {-19.0f, 18.0f, 24.0f, 24.0f};
    static const float upper_right[] = {18.0f, -18.0f};
    void *layer = read_pointer(icon, ICON_LEFT);
    if (!has_vtable(layer, SPRITE_VTABLE) ||
        (read_u32(layer, SPRITE_FLAGS) & SPRITE_VISIBILITY_MASK) != SPRITE_HIDDEN) return false;
    void *pendant = read_pointer(icon, ICON_PENDANT);
    if (!is_visible_sprite(pendant)) pendant = NULL;
    const bool occupied = pendant && (is_visible_sprite(read_pointer(icon, ICON_RIGHT)) ||
                                     is_visible_sprite(read_pointer(icon, ICON_WISHLIST)));
    return show_element(state, layer, element, pendant, &placement, occupied ? NULL : upper_right);
}

static bool supported_box_mode(void *box, bool palico, bool kinsect) {
    const uint32_t mode = read_u32(box, BOX_MODE);
    const uint32_t submode = read_u32(box, BOX_SUBMODE);
    if (palico) return submode == PALICO_GRID && mode == CHANGE_EQUIPMENT;
    return submode == HUNTER_GRID &&
           (mode == UPGRADE_EQUIPMENT || mode == CHANGE_EQUIPMENT || mode == SET_DECORATIONS ||
            (kinsect && mode == NURTURE_KINSECT));
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
        !gui_is_open(box))
        return result;

    const uint32_t type = read_u32(icon, ICON_TYPE);
    const bool kinsect = !palico && type == KINSECT;
    if ((type != WEAPON && !kinsect) || !supported_box_mode(box, palico, kinsect)) return result;

    AcquireSRWLockExclusive(&ui_lock);
    if (!owned_box || owned_box == box) {
        BadgeState *state = find_badge(box, cell);
        if (state && !state->layer) {
            const FindEquipment lookup = (FindEquipment)(game + (palico ? PALICO_BOX_LOOKUP : BOX_LOOKUP));
            const uint8_t *item = lookup(box, index);
            const EquipmentEffect effect = kinsect ? kinsect_element(item) : weapon_element(item, palico);
            if (show_icon(state, icon, effect)) owned_box = box;
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

static const uint8_t *hunter_item(const uint8_t *save, uint32_t index) {
    const EquippedSlot *layout = &equipped_slots[EQUIPPED_HUNTER];
    return save && index < layout->work_capacity ? save + layout->work_offset + index * WORK_STRIDE : NULL;
}

static bool show_augmentation(BadgeState *state, void *layer, const BadgePlacement *placement) {
    const BadgeStyle style = {.tile = ATLAS_AUGMENTATION_TILE};
    return has_base_texture(layer) && load_badge_atlas() &&
           badge_show(&sprite_api, state, layer, NULL, &atlas, style, placement, NULL);
}

static void clear_worn_augmentations(void *menu) {
    if (augmentation_panel != menu) return;
    const uint8_t *first_icon = (const uint8_t *)menu + equipped_slots[EQUIPPED_HUNTER].icon_offset;
    for (size_t slot = 0; slot < HUNTER_WORN_SLOTS; ++slot)
        restore_layer(&worn_augmentations[slot],
                      read_pointer(first_icon + slot * PANEL_ICON_STRIDE, ICON_AUGMENTATION), NULL);
    augmentation_panel = NULL;
}

static void prepare_worn_augmentations(void *menu, bool force) {
    if (!installed() || !has_vtable(menu, PANEL_VTABLE)) return;
    const EquippedSlot *layout = &equipped_slots[EQUIPPED_HUNTER];
    const bool selected = gui_is_open(menu) && read_u32(menu, PANEL_MODE) == layout->mode;
    const uint8_t *save = selected ? active_save() : NULL;

    AcquireSRWLockExclusive(&ui_lock);
    if (!save || force) {
        clear_worn_augmentations(menu);
    } else if (augmentation_panel == menu) {
        const EquippedIndex equipped_index = (EquippedIndex)(game + HUNTER_EQUIPPED_INDEX);
        const uint8_t *first_icon = (const uint8_t *)menu + layout->icon_offset;
        for (uint32_t slot = 0; slot < HUNTER_WORN_SLOTS; ++slot) {
            BadgeState *state = &worn_augmentations[slot];
            if (state->layer && read_u32(menu, layout->cache_offset + slot * sizeof(uint32_t)) !=
                                equipped_index(save, slot))
                restore_layer(state, read_pointer(first_icon + slot * PANEL_ICON_STRIDE, ICON_AUGMENTATION), NULL);
        }
    }
    ReleaseSRWLockExclusive(&ui_lock);
}

static void update_worn_augmentations(void *menu) {
    if (!installed() || !has_vtable(menu, PANEL_VTABLE)) return;
    const EquippedSlot *layout = &equipped_slots[EQUIPPED_HUNTER];
    const bool selected = gui_is_open(menu) && read_u32(menu, PANEL_MODE) == layout->mode;
    const uint8_t *save = selected ? active_save() : NULL;

    AcquireSRWLockExclusive(&ui_lock);
    if (!save) {
        clear_worn_augmentations(menu);
    } else if (!augmentation_panel || augmentation_panel == menu) {
        for (uint32_t slot = 0; slot < HUNTER_WORN_SLOTS; ++slot) {
            const uint8_t *icon = (const uint8_t *)menu + layout->icon_offset + slot * PANEL_ICON_STRIDE;
            void *layer = read_pointer(icon, ICON_AUGMENTATION);
            const uint32_t index = ((EquippedIndex)(game + HUNTER_EQUIPPED_INDEX))(save, slot);
            const uint32_t type = slot ? ARMOR : WEAPON;
            const uint8_t *item = hunter_item(save, index);
            const bool show = layer && item && read_u32(icon, ICON_TYPE) == type &&
                read_u32(item, WORK_TYPE) == type &&
                read_u32(menu, layout->cache_offset + slot * sizeof(index)) == index &&
                ((HasAugmentation)(game + HAS_AUGMENTATION))(item);
            BadgeState *state = &worn_augmentations[slot];
            if (show && state->layer == layer && is_visible_sprite(layer) &&
                read_pointer(layer, SPRITE_OVERRIDE_TEXTURE) == &atlas) continue;
            if (state->layer) restore_layer(state, layer, NULL);
            if (show && show_augmentation(state, layer, NULL)) augmentation_panel = menu;
        }
    }
    ReleaseSRWLockExclusive(&ui_lock);
}

static bool is_local_equipment_info(void *menu) {
    return has_vtable(menu, INFO_VTABLE) && read_u32(menu, INFO_MODE) == INFO_HUNTER_MODE &&
           read_u32(menu, INFO_SUBMODE) == INFO_EQUIPMENT_SUBMODE &&
           !((const uint8_t *)menu)[INFO_REMOTE_HUNTER];
}

static void **info_row_layers(void *menu, uint32_t slot) {
    void *list = read_pointer(menu, INFO_LIST);
    if (!list || read_u32(list, GRID_COUNT) <= slot) return NULL;
    void **rows = read_pointer(list, GRID_CELLS);
    void *root = rows && rows[slot] ? read_pointer(rows[slot], INFO_ROW_ROOT) : NULL;
    if (!root) return NULL;
    void *icon = ((FindGuiChild)(game + FIND_GUI_CHILD))(root, "icon_itemList");
    return icon ? read_pointer(icon, GUI_CHILDREN) : NULL;
}

/* The list's stock left/right layers share a centered 40 px icon. */
static void update_info_list(void *menu) {
    static const BadgePlacement placement = {-13.0f, 13.0f, 16.0f, 16.0f};
    if (!is_local_equipment_info(menu)) return;
    const uint8_t *save = active_save();
    if (!save) return;

    AcquireSRWLockExclusive(&ui_lock);
    if (info_badges.menu == menu && info_badges.show_equipment) {
        for (uint32_t slot = 0; slot < HUNTER_WORN_SLOTS; ++slot) {
            const uint8_t *item = hunter_item(save, read_u32(menu, INFO_WORN_INDICES + slot * sizeof(uint32_t)));
            if (!item || read_u32(item, WORK_TYPE) != (slot ? ARMOR : WEAPON)) continue;
            void **layers = info_row_layers(menu, slot);
            if (!layers) continue;
            if (!slot)
                show_element(&info_badges.list_element, layers[INFO_ROW_RIGHT],
                             weapon_element(item, false), NULL, &placement, NULL);
            if (((HasAugmentation)(game + HAS_AUGMENTATION))(item))
                show_augmentation(&info_badges.list_augmentations[slot], layers[INFO_ROW_LEFT], NULL);
        }
    }
    ReleaseSRWLockExclusive(&ui_lock);
}

static void restore_info_card(void *menu) {
    if (info_badges.card_element.layer)
        restore_layer(&info_badges.card_element, read_pointer(menu, INFO_CARD_SPARE),
                      read_pointer(menu, INFO_CARD_PENDANT));
}

static void restore_info(void *menu) {
    if (info_badges.menu != menu) return;
    restore_info_card(menu);
    for (uint32_t slot = 0; slot < HUNTER_WORN_SLOTS; ++slot) {
        BadgeState *augmentation = &info_badges.list_augmentations[slot];
        BadgeState *element = slot == 0 ? &info_badges.list_element : NULL;
        if (!augmentation->layer && (!element || !element->layer)) continue;
        void **layers = info_row_layers(menu, slot);
        if (element) restore_layer(element, layers ? layers[INFO_ROW_RIGHT] : NULL, NULL);
        restore_layer(augmentation, layers ? layers[INFO_ROW_LEFT] : NULL, NULL);
    }
}

static bool is_card_corner_occupied(void *layer) {
    if (!is_visible_sprite(layer)) return false;
    const float *position = (const float *)((const uint8_t *)layer + SPRITE_POSITION);
    return position[0] > 16.0f && position[1] < 16.0f;
}

/* tex_great is stock-hidden on weapon and Kinsect detail pages. */
static void update_info_card(void *menu) {
    static const BadgePlacement placement = {1.0f, 31.0f, 16.0f, 16.0f};
    static const float upper_right[] = {31.0f, 1.0f};
    const uint8_t *item = (const uint8_t *)menu + INFO_CARD_ITEM;
    const uint32_t page = read_u32(menu, INFO_CARD_PAGE);
    if (read_u32(item, WORK_TYPE) != WEAPON || read_u32(item, WORK_ID) == UINT32_MAX) return;

    EquipmentEffect effect;
    if (page == INFO_KINSECT_PAGE) {
        if (read_u32(item, WORK_KIND) != INSECT_GLAIVE) return;
        const uint8_t *kinsect = hunter_item(active_save(), read_u32(item, WORK_GLAIVE_KINSECT_INDEX));
        effect = kinsect_element(kinsect);
    } else {
        if ((page < 6 || (page > 10 && page != 12)) ||
            read_u32(menu, INFO_CARD_DETAIL_KIND) == INFO_KINSECT_PAGE) return;
        effect = weapon_element(item, false);
    }
    void *pendant = read_pointer(menu, INFO_CARD_PENDANT);
    if (!is_visible_sprite(pendant)) pendant = NULL;
    const bool occupied = pendant && (is_card_corner_occupied(read_pointer(menu, INFO_CARD_EQUIPPED)) ||
                                     is_card_corner_occupied(read_pointer(menu, INFO_CARD_AUGMENTATION)));
    show_element(&info_badges.card_element, read_pointer(menu, INFO_CARD_SPARE), effect, pendant,
                 &placement, occupied ? NULL : upper_right);
}

static void update_visible_info_card(void *menu) {
    if (!is_local_equipment_info(menu)) return;
    const void *root = read_pointer(menu, INFO_CARD_ROOT);
    if (!root || !(read_u32(root, SPRITE_FLAGS) & GUI_COMPONENT_VISIBLE)) return;

    AcquireSRWLockExclusive(&ui_lock);
    if (info_badges.menu == menu && info_badges.show_equipment && !info_badges.card_element.layer)
        update_info_card(menu);
    ReleaseSRWLockExclusive(&ui_lock);
}

static void info_open_detour(void *menu) {
    const bool tracked = installed() && has_vtable(menu, INFO_VTABLE);
    if (tracked) {
        AcquireSRWLockExclusive(&ui_lock);
        restore_info(menu);
        if (!info_badges.menu || info_badges.menu == menu) {
            info_badges.menu = menu;
            info_badges.show_equipment = true;
        }
        ReleaseSRWLockExclusive(&ui_lock);
    }
    original_info_open(menu);
    if (tracked && is_local_equipment_info(menu)) {
        AcquireSRWLockExclusive(&ui_lock);
        /* Opening may have reset an earlier instance of the stock sidebar. */
        if (!info_badges.menu) {
            info_badges.menu = menu;
            info_badges.show_equipment = true;
        }
        ReleaseSRWLockExclusive(&ui_lock);
        update_info_list(menu);
        update_visible_info_card(menu);
    }
}

static void info_appearance_detour(void *menu, void *list, int selection) {
    const bool tracked = installed() && is_local_equipment_info(menu);
    if (tracked) {
        AcquireSRWLockExclusive(&ui_lock);
        restore_info(menu);
        if (info_badges.menu == menu) info_badges.show_equipment = selection == 0;
        ReleaseSRWLockExclusive(&ui_lock);
    }
    original_info_appearance(menu, list, selection);
    if (tracked && selection == 0) {
        update_info_list(menu);
        update_visible_info_card(menu);
    }
}

static void info_card_detour(void *menu) {
    const bool tracked = installed() && has_vtable(menu, INFO_VTABLE);
    if (tracked) {
        AcquireSRWLockExclusive(&ui_lock);
        if (info_badges.menu == menu) restore_info_card(menu);
        ReleaseSRWLockExclusive(&ui_lock);
    }
    original_info_card(menu);
    if (tracked) update_visible_info_card(menu);
}

static void finish_info(void *menu) {
    if (installed() && has_vtable(menu, INFO_VTABLE)) {
        AcquireSRWLockExclusive(&ui_lock);
        if (info_badges.menu == menu) {
            restore_info(menu);
            info_badges = (InfoBadges){0};
        }
        ReleaseSRWLockExclusive(&ui_lock);
    }
}

static void info_close_detour(void *menu) {
    finish_info(menu);
    original_info_close(menu);
}

static void info_reset_detour(void *menu) {
    const bool tracked = installed() && has_vtable(menu, INFO_VTABLE);
    if (tracked) {
        AcquireSRWLockExclusive(&ui_lock);
        restore_info(menu);
        ReleaseSRWLockExclusive(&ui_lock);
    }
    original_info_reset(menu);
    /* The opening virtual can run after the Equipment Info opener has bound the list. */
    if (tracked) {
        update_info_list(menu);
        update_visible_info_card(menu);
    }
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

    EquipmentEffect element = {0};
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
    prepare_worn_augmentations(menu, force);
    refresh_panel(menu, force, EQUIPPED_HUNTER, original_hunter_panel);
    update_worn_augmentations(menu);
}

static void palico_panel_detour(void *menu, bool force) {
    refresh_panel(menu, force, EQUIPPED_PALICO, original_palico_panel);
}

static bool panel_close_detour(void *menu) {
    if (installed() && has_vtable(menu, PANEL_VTABLE)) {
        AcquireSRWLockExclusive(&ui_lock);
        clear_worn_augmentations(menu);
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
        {KINSECT_LOOKUP,  {0x8b,0xd1,0x83,0xf9,0xff,0x75,0x03,0x33,0xc0,0xc3,0x48,0x8b,0x0d,0x5f,0x38,0x69}},
        {HUNTER_PANEL_REFRESH,{0x40,0x53,0x56,0x41,0x54,0x41,0x55,0x41,0x57,0x48,0x83,0xec,0x20,0x48,0x89,0x6c}},
        {PALICO_PANEL_REFRESH,{0x40,0x53,0x57,0x41,0x54,0x41,0x55,0x41,0x57,0x48,0x83,0xec,0x20,0x44,0x0f,0xb6}},
        {PANEL_CLOSE,     {0x48,0x89,0x6c,0x24,0x20,0x57,0x48,0x83,0xec,0x20,0x83,0x89,0x98,0x22,0x00,0x00}},
        {HAS_AUGMENTATION,{0x83,0x79,0x50,0x00,0x4c,0x8b,0xc1,0xba,0x01,0x00,0x00,0x00,0x0f,0x95,0xc0,0x0f}},
        {INFO_OPEN,       {0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,0x48}},
        {INFO_APPEARANCE, {0x48,0x8b,0xc4,0x44,0x89,0x40,0x18,0x55,0x56,0x48,0x8d,0xa8,0xa8,0xfe,0xff,0xff}},
        {INFO_CARD_REFRESH,{0x40,0x56,0x48,0x83,0xec,0x40,0x48,0x8b,0xf1,0xb2,0x01,0x48,0x8b,0x89,0x48,0x29}},
        {INFO_CLOSE,      {0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xec,0x30,0x0f}},
        {INFO_RESET,      {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8,0x1e,0x44}},
        {FIND_GUI_CHILD,  {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x7c,0x24,0x10,0x48,0x8b,0x81,0xc8,0x00,0x00}},
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
           read_pointer((void *)(base + PANEL_VTABLE), 0x2d8) == (void *)(base + PANEL_CLOSE) &&
           read_pointer((void *)(base + INFO_VTABLE), 0x2d8) == (void *)(base + INFO_CLOSE) &&
           read_pointer((void *)(base + INFO_VTABLE), 0x2d0) == (void *)(base + INFO_RESET);
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
        {INFO_OPEN, (void *)info_open_detour, (void **)&original_info_open},
        {INFO_APPEARANCE, (void *)info_appearance_detour, (void **)&original_info_appearance},
        {INFO_CARD_REFRESH, (void *)info_card_detour, (void **)&original_info_card},
        {INFO_CLOSE, (void *)info_close_detour, (void **)&original_info_close},
        {INFO_RESET, (void *)info_reset_detour, (void **)&original_info_reset},
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
