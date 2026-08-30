# Zelda3 Voxel — Claude Handoff

## Project location

`E:\Console Games\Nintendo Recompiles\Build Files\Zelda3\Zelda3 Voxel`

This is a standalone Zelda3 voxel-presentation fork. Keep it separate from the
Epoch & Equinox and Gen1 Recomp workspaces. Do not add or distribute ROM-derived
assets. `build\Release\zelda3_assets.dat` is a local runtime input.

## Current goal

Turn Zelda3's original rendered playfield into a playable voxel/diorama view,
similar in spirit to Gen1 Recomp's perspective presentation, while preserving
the original game logic, collision, saves, and controls.

The launcher is already in good shape. Current work is focused on the renderer.

## What is implemented

### PPU layer tagging (the layer split)

The PPU compositor stores each pixel's winning layer in the frame's otherwise
unused alpha byte. See `kPpuPixelTag_*` in `snes\ppu.h`: 0 = blank/border,
else `0x10 | layer` where layer is 0/1/3 = BG tiles, 2 = BG3 (all of Zelda's
HUD/dialog/menu UI), 4/6 = OAM sprites, 5 = backdrop. All three render paths
tag (new renderer in `PpuDrawWholeLine`, 4x4 mode7 upsample, old per-pixel
renderer), so one rendered frame carries the full split — no second PPU pass,
no HDMA snapshot/replay. Tagging does not affect the normal 2D output; the
blit shaders ignore alpha.

Zelda's UI all lands on BG3: the HUD buffer and the text/menu buffer are both
DMA'd to the BG3 tilemap at VRAM `0x7c00` (see `src\nmi.c`).

### Voxel renderer (tag-aware)

`src\opengl.c` splits the tagged frame into three planes:

- **Terrain**: heights come from the game's tile attribute maps via
  `VoxelAttrHeight()`/`VoxelTileAttrAt()` in `src\opengl.c` — dungeon reads
  `dung_bg2_attr_table` directly, overworld calls
  `Overworld_GetTileAttributeAtLocation()` (both read-only; do NOT call
  `GetTileAttribute()`, it writes `sprite_tiletype`). Luminance only adds
  small in-class relief. The sampling grid is anchored to world coordinates
  via `BG2HOFS_copy2`/`BG2VOFS_copy2`, so cells stay glued to map tiles while
  scrolling (no resample shimmer); edge cells are clipped to the frame. Cube
  top faces carry uv coords into the frame texture, so ground detail is
  native-resolution regardless of `VoxelSize`. Ground hidden behind
  actors/UI is filled from row neighbors.
- **Actors**: OAM entries are decoded from `g_zenv.ppu->oam` (mirroring
  `ppu_evaluateSprites`: y=0xf0 hidden, 9-bit x via the high table, sizes
  8/16 since Zelda's OBSEL is fixed), clustered by rect adjacency
  (union-find, 2px slack), and each cluster becomes one upright quad
  textured from the frame; the shader discards non-sprite pixels, producing
  pixel-perfect cutout billboards, one per logical actor at its own depth,
  each with a translucent contact shadow. The quad is nudged forward to
  avoid z-fighting with terrain faces.
- **UI overlay**: a full-viewport pass draws only BG3-tagged pixels flat over
  the scene (`g_hud_program` fragment shader filters by tag). No cutoff band,
  no color keys, no split viewport — those were all removed.

Vertex format is 9 floats (pos, color, uv + texture mode); see
`kVoxelTex_*` in `src\opengl.c`. The voxel pass binds the same frame texture
the 2D blit uses — the alpha-byte tags ride along into the shaders.

Settings:

```ini
[Graphics]
OutputMethod=OpenGL
VoxelMode=true
VoxelizeHud=false   ; true = treat BG3 as terrain instead of overlay
VoxelSize=4
VoxelHeight=55
VoxelHudHeight=48   ; deprecated, parsed but ignored
VoxelPitch=39       ; chase-camera tilt in degrees (10-80)
VoxelZoom=100       ; camera zoom percent (50-200)
```

- Press `3` in-game to toggle voxel presentation.
- Pitch and zoom are shader uniforms (`uPitch`, `uZoom`) read from config at
  draw time, so an ini edit takes effect on restart; the launcher's Diorama
  tab exposes both as sliders (it replaced the deprecated HUD-height field).
- Each actor run also gets a translucent contact shadow (`kVoxelTex_Shadow`);
  the voxel pass renders with alpha blending enabled for this.

### Gen1-style chase perspective

The old diagonal isometric transform was replaced with a forward-facing chase
tilt in the voxel vertex shader:

- Pitch: approximately 0.68 radians / 39 degrees.
- No diagonal yaw.
- Perspective enlarges the near rows and recedes the upper rows.
- Zelda's existing player-centered scrolling supplies the follow movement.

The local Gen1 reference used for this design is:

`E:\Console Games\Nintendo Recompiles\Build Files\pokemon-gen1-recomp-project-main-backup-20260807-204827\src\render\Tilt.lua`

Gen1's key architectural lesson is that it renders world and UI to separate
canvases and projects only the world canvas.

### Gameplay gating

Voxel mode runs in active gameplay modules:

- `7`: Dungeon
- `9` and `11`: Overworld routes
- `17`: Dungeon falling entrance
- `14`: Interface, but only submodules 1 (item menu), 2 (dialogue text) and
  4/8/9 (potion refills), and only when `saved_module_for_menu` is 7/9/11.
  These draw BG3 UI over the live scene, so the 3D world stays visible
  beneath the item menu and dialog boxes. Map screens (submodules 3/7),
  desert prayer, flute menu, and save menu fall back to 2D.

Title, file select, naming, game-over, attract, and ending screens stay in
the original 2D renderer.

The gating condition is in `src\opengl.c` (`OpenGLRenderer_EndDraw`) and
reads `main_module_index`, `submodule_index`, and `saved_module_for_menu`
from `src\variables.h`.

## Launcher

The Epoch-style Windows launcher lives in `launcher\` and publishes to:

`build\Release\Zelda3 Voxel Launcher.exe`

It has a cinematic hero page plus a separate Settings page. It writes the voxel,
HUD, block-size, height, fullscreen, filtering, scale, and audio settings to
`zelda3.ini`, then launches `zelda3.exe` with the correct working directory.

The launcher hero image is embedded from:

`launcher\assets\zelda3-voxel-launcher-hero.png`

## Build commands

Run from the project root in PowerShell:

```powershell
cmake --build build --config Release --parallel 8
dotnet publish launcher\Zelda3VoxelLauncher.csproj -c Release -r win-x64 --self-contained false -o build\Release
```

If linking fails with `LNK1104` for `zelda3.exe`, a test instance is still
running and holding the executable open. Close only the Zelda3 Voxel test
process, then rebuild.

Run the game with `build\Release` as its working directory so it can find
`zelda3_assets.dat` and `zelda3.ini`.

## Verification completed

- Native Release build succeeds with no new warnings.
- Visually confirmed in-game (Eastern Palace entrance, Chapter 2 ref save):
  - Terrain renders as a full-frame diorama; walls/paths/cliffs separate by
    height, HUD band no longer clips the world.
  - Link and enemy sprites render as upright billboard stacks standing on
    filled ground.
  - HUD glyphs (magic bar, item box, counters, hearts) composite flat and
    clean with no backdrop band and no duplicated scene.
  - Item menu (module 14 submodule 1) draws flat with the 3D world visible
    between its panels.
  - `3` toggle works both directions; flat 2D output is pixel-identical in
    look (tags ride in the ignored alpha channel).
  - Attract/story/world-map screens remain flat 2D.

## Alpha packaging

`build\dist\Zelda3-Voxel-Alpha-0.2.zip` = zelda3.exe + SDL2.dll + the
single-file launcher + default zelda3.ini + README (from
`launcher\ALPHA_README.md`). It deliberately excludes `zelda3_assets.dat`
(ROM-derived — never distribute it). Rebuild by copying fresh binaries from
`build\Release` into `build\dist\Zelda3-Voxel-Alpha-<ver>` and re-zipping.

## Terrain semantics (research-confirmed, do not re-derive)

- Slope attrs: 0x10-0x13 are 45-degree diagonal walls (inner corners),
  0x18-0x1B their outer/convex variants. Orientation = attr&3 with
  tile-local x=0 west, y=0 north: 0 = NW triangle solid (x+y<=7),
  1 = NE (y<=x), 2 = SW (y>=x), 3 = SE (x+y>=7) — from kSlopedTile in
  src\sprite.c. **0x14-0x17 are NOT slopes** — plain walkable tiles.
  The renderer chamfers slope cells by corner-sampling the triangle
  (`VoxelSlopeFraction`).
- Dungeon attr halves: `dung_bg2_attr_table` +0 describes the PPU **BG2**
  tilemap = the **upper** level; +0x1000 (alias `dung_bg1_attr_table`)
  describes PPU **BG1** = the **lower** level. The renderer picks the half
  per cell from the winning-layer majority (BG1 vs BG2 pixel counts) and
  raises upper-level cells when a room has meaningful BG1 coverage
  (two-level rooms). Beware: dungeon.c's `_BG1`/`_BG2` table names are
  swapped relative to the hardware layers.
- Weather (rain/fog/canopy) lives ONLY on the subscreen (BG1, TS=1) and
  reaches the frame via color math — it never wins a main-screen pixel, so
  it never changes tags. The PPU sets `kPpuPixelTag_SubMath` (0x20) on
  pixels a subscreen color was mathed into. Lightning = CGADSUB_copy
  flipping 0x72 -> 0x32; the voxel shader brightens the scene via `uFlash`.
  Rain streaks intentionally ride the terrain-top texture for now.
- The sampling grid anchors to `BG2HOFS_copy`/`BG2VOFS_copy` (NOT the
  `_copy2` variants — those exclude the quake-shake offset the PPU shows).

## Known limitations

1. Overlapping distinct actors merge into one cluster while they touch
   (visually benign; they separate again when apart).
2. The game's own OAM shadow sprites (under flying enemies) billboard upright
   like any sprite — small dark discs standing on the ground.
3. Rain/fog render as texture on the ground plus a scene-wide lightning
   flash; true 3D rain particles / fog volumes are future work (the
   kPpuPixelTag_SubMath bit exists to support them).
4. Slope chamfers are cell-resolution steps, not true diagonal geometry.

## Recommended next implementation

1. 3D weather: rain streak billboards / fog planes driven by the overlay
   index and the SubMath tag, with pre-math ground colors.
2. True diagonal top faces for slope cells (split the top quad along the
   tile diagonal).
3. In-game verification pass on a two-level room (castle sewers) for the
   per-layer attribute split and upper-level elevation boost.

## Related documentation

See `VOXEL_SLICE.md` for the user-facing summary of the current prototype.
