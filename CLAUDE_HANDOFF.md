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
- Ledges (0x28-0x2F) are one-story drops in a fixed jump direction:
  0x28 N, 0x29 S, 0x2A W, 0x2B E, 0x2C NW, 0x2D SW, 0x2E NE, 0x2F SE
  (from the ledge-hop code in src\player.c / src\tile_detect.c; the W/E and
  NW/NE splits are placement convention — the code treats each pair alike).
  0x4C-0x4F are Eastern-Ruins corner ledges (movement-dependent direction).
  `VoxelSolveTerraces` in src\opengl.c turns these into an elevation model:
  flood-fill ground regions bounded by walls/slopes/ledges/stairs over a
  16-cell margin beyond the frame, constrain uphill-region >= downhill+1 per
  ledge, relax (capped at 6 stories), then lift ground by its region level,
  boundary cells by the tallest nearby region, stairs midway. This is why
  overworld plateaus rise as real stories instead of sinking behind their
  rim walls.

## Known limitations

1. Overlapping distinct actors merge into one cluster while they touch
   (visually benign; they separate again when apart).
2. The game's own OAM shadow sprites (under flying enemies) billboard upright
   like any sprite — small dark discs standing on the ground.
3. Rain/fog render as texture on the ground plus a scene-wide lightning
   flash; true 3D rain particles / fog volumes are future work (the
   kPpuPixelTag_SubMath bit exists to support them).
4. Slope chamfers are cell-resolution steps, not true diagonal geometry.

## World atlas (map-RAM terrain)

`src\opengl.c` rasterizes the loaded map from RAM into a world-space RGBA
atlas (`VoxelWorldBuildOverworld`/`VoxelWorldBuildDungeon` +
`VoxelRasterTile`, decode copied from `PpuDrawBackground_4bpp`), so terrain
extends 96px past the sides/top and 48px past the bottom of the frame,
textured via `kVoxelTex_World` (texture unit 1) and dimmed with distance.
Key facts (research-confirmed):

- Overworld: `overworld_tileattr` (g_ram+0x2000) holds map16 TILE NUMBERS
  (0..0xEA7), row-major, fixed stride 64. Big area = full 64x64 (1024px),
  small = top-left 32x32 only (rest is neighbor-screen garbage). Area origin
  = (`overworld_offset_base_x`*8, `overworld_offset_base_y`) world px.
  map8 quads: `GetMap16toMap8Table()[m16*4 + q]`, q = TL,TR,BL,BR.
- Dungeon: `dung_bg2` (BG2, main floor, VRAM tilemap 0x0000) composited
  first, `dung_bg1` (BG1, overlay/walkways, 0x1000) on top. **PPU BG1 draws
  above BG2**; dungeon.c's `_BG1`/`_BG2` table names are inverted.
- Char base for BG1+BG2 is always VRAM word 0x2000 (`BG12NBA=0x22`, written
  every NMI); read `ppu->bgLayer[n].tileAdr` anyway.
- Normal (unflipped) pixels extract MSB-first (`bits >> (7-x)`); ppu.c's
  `DO_PIXEL` macro name refers to extraction direction, not the flip flag.
- Atlas refreshes on scene-key change and every 32 frames (covers palette
  fades, chest/door tile mutations, and the 32 animated water tiles at char
  ids 0x1C0-0x1DF which are chr-uploaded every frame).

## Camera modes

`VoxelCamera=0|1|2` in `zelda3.ini` (launcher Diorama combo; key `4` cycles
in-game; legacy `VoxelChaseCam=true` maps to 1):

- 0 diorama: classic view, slider pitch, neutral pivot (bit-identical to
  the pre-camera behavior via neutral uniforms).
- 1 chase, third-person over-the-shoulder: pitch .30 rad, `uDist` 1.6 —
  Link large at the lower third, world to the horizon.
- 2 first person: pitch .12 rad, `uDist` 2.55 — eye level; cells and actors
  behind the camera plane are culled CPU-side and Link's own billboard is
  hidden (pivot-distance test in the cluster loop).

Shared machinery: the vertex shader recenters on a smoothed pivot (Link),
yaws toward his facing (`link_direction_facing` 0/2/4/6 = N/S/W/E -> yaw
0/pi/+pi/2/-pi/2, eased shortest-arc .12/frame), then applies pitch/zoom.
Actor billboards counter-rotate by the yaw CPU-side so they keep facing the
camera; grid margins go symmetric (96px) in modes 1-2; the projection
clamps view depth (`z = max(..., 0.1)`).

## 3D rain

When the storm overlay is active (`(overlay_index & 0xff) == 0x9f`,
outdoors), 260 deterministic rain streaks (integer-hash positions, frame
counter drives the fall; no rand()) are emitted over the whole grid as
thin slanted camera-facing quads (`kVoxelTex_Rain`, alpha .40, brightened
by the lightning `uFlash`). They render in the depth-write-off translucent
pass with the shadows, so terrain and walls occlude them naturally. Fog
overlays (0x95/0x9C/0x97) are still texture-only.

The chase grid margin also widens to 192px in the camera's facing
direction (48px behind, 96px lateral) so the vista runs deeper ahead.

## Polish-pass notes (post-review)

- Slope cells are true ramps now (`VoxelSlopeCell` per-corner offsets;
  side faces hem to neighbors' shared-edge heights via
  `VoxelNeighborEdgeH`, so ramps meet walls without slits).
- Terrain emits ALL FOUR side faces (follow cameras can look from any
  direction; the north face was the missing one).
- Atlas rebuilds freeze during module 7/9 transitions (registers and map
  buffers update at different times); dungeon key = `dungeon_room_index`;
  the pixel buffer is calloc'd; texture storage is reused via
  glTexSubImage2D; ES builds store/upload RGBA byte order (no BGRA).
- The vertex shader passes true w through (no near clamp - homogeneous
  clipping handles behind-eye vertices correctly).
- `VoxelSize` snaps to {2,3,4,6,8,12} - other values break the margin
  alignment invariant.
- Terrace solver treats out-of-area samples as solid boundary (attribute
  lookups wrap at area edges and imported phantom ledges).
- Chase snap re-arms on every non-voxel frame; first-person self-cull
  radius shrunk so nearby enemies stop vanishing; rain anchors to 256px
  world tiles (constant density, no scroll drift) with landing flecks.

## Overlay planes (fog / canopy)

`VoxelOverlayRaster` rasterizes the subscreen overlay's BG1 tilemap (VRAM,
64x64 tiles, quadrant layout +0/0x400/0x800/0xC00) into a wrapping 512x512
texture on unit 2 (`uOverlayTex`), refreshed on overlay change and every 16
frames. A translucent world-uv plane (`kVoxelTex_Overlay`, uv = world_px/512
with GL_REPEAT — BG1 scroll is slaved to BG2 for these overlays) draws in
the translucent pass: canopy height .34 for 0x9D/0x9E tree tops, 0x97 grove
mist and 0x94 bridge deck; drifting fog at .10 for 0x95/0x9C Death Mountain.
Rain 0x9F keeps its streak system; verified on the Tower of Hera summit via
the Chapter 4 ref save. Uniform locations are now cached at link time
(`g_vloc`/`g_hloc`).

## Recommended next implementation

1. In-game verification pass on a two-level room (castle sewers) and the
   Lost Woods canopy (0x9D) when the playthrough reaches them.
2. Ease margin changes on chase yaw flips (band fade-in already hides most
   of the pop).

## Related documentation

See `VOXEL_SLICE.md` for the user-facing summary of the current prototype.
