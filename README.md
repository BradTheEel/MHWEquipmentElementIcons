# Equipment Element Icons 0.2.4

Equipment Element Icons adds a small badge to the lower-left corner of equipment icons in Monster Hunter: World. Weapons show their element or status effect. Kinsects show the element assigned to that individual Kinsect, so you can tell copies of the same model apart in the equipment box.

## Supported menus

| Menu | Where badges appear |
| --- | --- |
| Change Equipment | Hunter weapons and Kinsects in the equipment box, plus the equipped weapon slot |
| Equipment Info | Equipped weapon in the left list, plus the selected weapon or Kinsect card |
| Upgrade Equipment (Smithy) | Equipment selection grid |
| Nurture a Kinsect (Smithy) | Kinsect selection grid |
| Set Decorations | Equipment selection grid in the item box |
| Change Palico Equipment | Palico weapons in the equipment box and the equipped weapon slot |

Hidden weapon effects use a dimmed badge. Dual Blades with two effects display the primary effect reported by the game. Kinsect badges cover fire, water, ice, thunder, and dragon. Equipment without an applicable element or status and bowguns use their standard icons.

The equipped lists in Change Equipment and Equipment Info show the stock purple lightning badge on augmented weapons and armor. When an element badge shares a tile with a pendant, the pendant moves to the upper-right corner. If that corner is occupied, the pendant is hidden.

## Installation

Requires Monster Hunter: World for Windows x64, Steam build `15539686`, and Stracker’s Loader. Close the game and extract the archive’s `nativePC` folder next to `MonsterHunterWorld.exe`. When upgrading, replace the DLL and texture with the copies from the new archive.

To uninstall, close the game and remove these files:

- `nativePC/plugins/EquipmentElementIcons.dll`
- `nativePC/ui/common/tex/equipment_element_icons.tex`

## Build and test

Use Zig `0.13.0`. In the full release archive, `source/` contains the build scripts, plugin source, tests, and vendored MinHook with its license.

```powershell
.\build.ps1 -ZigPath "C:\path\to\zig.exe"
.\verify.ps1 -ZigPath "C:\path\to\zig.exe" -LauncherPath "C:\path\to\launcher.exe"
```

The build writes the DLL to `build/`. The test command uses the owned test launcher from `work/owned-test-launcher-v1` in the development workspace.

The texture contains nine stock effect icons and the stock augmentation icon. `atlas-layout.json` defines the resource path, tile layout, and game effect IDs. To regenerate it, use Python 3.8 or later and the stock `cmn_status00_ID.tex` and `cmn_item00_ID.tex` extracted from Steam build `15539686`.

```powershell
python tools/build_atlas.py "C:\path\to\cmn_status00_ID.tex" "C:\path\to\cmn_item00_ID.tex"
```

The script copies the original BC7 blocks into `build/nativePC/ui/common/tex/equipment_element_icons.tex`. Run it with `--check` to verify an existing output. Ready-to-install archives include the texture.

Offline tests cover badge placement and conflicts, equipment grids, the two equipped lists, the Equipment Info card, asset loading, and all five Kinsect elements. In-game rendering in 0.2.4 remains unverified.

## Changelog

- **0.2.4** — Added elements to Equipment Info and augmentation icons to the equipped lists. When the top-right corner is occupied, the pendant is hidden while the element remains visible.
- **0.2.3** — Added badges for each Kinsect’s assigned element in Change Equipment and Nurture a Kinsect.
- **0.2.2** — Moved the nine effect icons to a dedicated texture and added tools to regenerate and verify it.
