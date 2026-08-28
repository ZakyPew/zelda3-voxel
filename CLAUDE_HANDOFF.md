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

### Voxel framebuffer renderer

`src\opengl.c` converts the completed SNES framebuffer into sampled,
depth-tested voxel columns. Relevant settings:

```ini
[Graphics]
OutputMethod=OpenGL
VoxelMode=true
VoxelizeHud=false
VoxelSize=4
VoxelHeight=55
VoxelHudHeight=48
```

- Press `3` in-game to toggle voxel presentation.
- `VoxelSize` controls framebuffer sampling size.
- `VoxelHeight` controls column extrusion.
- A low dark floor grounds the generated geometry.

### Split HUD/world viewports

The earlier implementation tried to solve HUD overlap by changing a pixel
cutoff and color-keying the HUD. That did not work reliably. The apparent
"duplicate scene" above the voxel room was partly 3D geometry projecting upward
into the HUD's screen area, not only a bad flat overlay.

The current solution gives the HUD and world physically separate OpenGL
viewports:

- The 3D world viewport ends at the bottom of the HUD.
- The HUD is redrawn flat in the full viewport with a top-band scissor.
- Perspective geometry therefore cannot render behind the HUD at any angle.

The split happens in `OpenGLRenderer_EndDraw()`.

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

Voxel mode currently runs only in active gameplay modules:

- `7`: Dungeon
- `9` and `11`: Overworld routes
- `17`: Dungeon falling entrance

Title, file select, naming, interface/dialog, game-over, attract, and ending
screens stay in the original 2D renderer. This prevents menus from being
misinterpreted as terrain.

The gating condition is in `src\opengl.c` and reads `main_module_index` from
`src\variables.h`.

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

- Native Release build succeeds.
- Launcher Release publish succeeds.
- Rebuilt game launches and responds.
- Title/file-select presentation was visually confirmed to remain flat 2D.
- Forward-facing chase projection was visually confirmed.
- Split viewport prevents the chase geometry from entering the HUD viewport.

## Known limitations

1. The voxelizer still samples the final framebuffer. It does not know which
   pixels are terrain, Link, sprites, particles, or UI.
2. Link and NPCs become voxel columns instead of upright billboards/models.
3. Dialog/interface module `14` currently falls back to the complete flat 2D
   frame. The 3D world does not remain visible beneath dialog boxes yet.
4. Perspective values are currently hard-coded in the vertex shader.
5. Full tile-aware terrain heights and sprite separation are not implemented.

## Recommended next implementation

Implement a real PPU/world/UI layer split instead of deriving everything from
one completed framebuffer.

Useful boundaries:

- `src\main.c` — `DrawPpuFrameWithPerf()` allocates the render buffer and calls
  `ZeldaDrawPpuFrame()`.
- `src\zelda_rtl.c` — `ZeldaDrawPpuFrame()` drives the PPU one scanline at a
  time.
- `snes\ppu.h` — `Ppu.screenEnabled[2]` contains main/sub-screen layer masks.
- HUD is primarily on BG3; world is primarily BG1/BG2 plus OAM sprites.

Suggested staged approach:

1. Render a normal full frame for UI/reference.
2. Render a second world-only frame with BG3 disabled while preserving PPU
   state and HDMA behavior.
3. Feed only the world frame into the 3D voxel pass.
4. Composite HUD/dialog/menu layers from the normal frame afterward.
5. Separate OAM sprites from the ground pass and render them as upright
   billboards or dedicated voxel actors.
6. Add launcher settings for camera pitch, zoom, and chase-camera enablement.

Be careful when rendering the PPU twice: HDMA state and scanline writes are
stateful. Snapshot/restore the required PPU and DMA state or add explicit layer
masking support to the PPU renderer rather than replaying a destructive second
frame blindly.

## Related documentation

See `VOXEL_SLICE.md` for the user-facing summary of the current prototype.
