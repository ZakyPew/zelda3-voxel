# Zelda3 voxel presentation slice

This fork contains a reversible voxel presentation built on a real PPU layer
split.

## Enable it

Use the OpenGL backend and add this to `zelda3.ini`:

```ini
[Graphics]
OutputMethod=OpenGL
VoxelMode=true
```

This dedicated fork enables `VoxelMode=true` in its sample configuration.
Set `VoxelMode=false` to keep the normal Zelda3 renderer. This is a
presentation-only seam: gameplay, collision, saves, menus, and the original
framebuffer renderer are untouched.

Press `3` while running to toggle the voxel presentation without restarting.

- `VoxelizeHud=false` (default) composites the UI layer flat over the 3D
  scene. Set it to `true` to treat UI pixels as terrain instead.
- `VoxelSize=4` controls the sampling block size (2-12).
- `VoxelHeight=55` controls extrusion strength (5-100 percent).
- `VoxelPitch=39` controls the chase-camera tilt in degrees (10-80).
- `VoxelZoom=100` controls camera zoom in percent (50-200).
- `VoxelHudHeight` is deprecated and ignored; the UI is now separated
  per-pixel instead of by a scanline cutoff.

All of these are adjustable from the launcher's Diorama settings tab.

## Layer split

The PPU compositor knows which layer wins every pixel, and it stores that
identity in the otherwise unused alpha byte of the frame (see
`kPpuPixelTag_*` in `snes/ppu.h`). The voxel pass reads those tags and splits
one rendered frame into three planes with no second PPU pass and no HDMA
replay:

- **Terrain** — BG1/BG2 tiles and backdrop become the depth-tested
  heightfield. Heights come from the game's own tile attribute maps (walls
  rise, water recedes, pits drop out, bushes and pots stand as blocks), with
  luminance adding only gentle relief, so floors are flat and walls are
  solid instead of corrugated. Cube tops are textured straight from the
  frame, so the ground keeps its full pixel-art detail regardless of
  `VoxelSize`. The sampling grid is anchored to the world rather than the
  screen, so terrain glides smoothly with the camera instead of shimmering
  as the game scrolls. Ground hidden behind actors or UI is filled from its
  row neighbors so nothing stands in a hole.
- **Actors** — OAM sprite entries are clustered into connected groups (one
  logical actor spans several hardware sprites) and each cluster becomes one
  upright billboard cut out of the frame at native resolution: the fragment
  shader discards non-sprite pixels, so Link and NPCs are their exact 2D
  art, each standing at its own depth with a translucent contact shadow.
- **UI** — BG3 pixels (Zelda draws its whole HUD, dialog boxes, and menus on
  BG3) are composited flat over the scene in screen space, wherever they
  appear. There is no HUD cutoff band, no color keying, and no split
  viewport anymore.

The world projection uses a forward-facing 39-degree chase tilt modeled after
Gen1 Recomp's presentational perspective pass; Zelda's existing scrolling
keeps the player-centered scene moving beneath that camera.

Voxel presentation runs in active dungeon, both overworld routes, and
dungeon-entry gameplay modules. It also stays active beneath BG3 overlays in
the interface module: the item menu, dialogue text boxes, and potion refills
draw flat over the live 3D scene. Map screens, flute/save menus, file select,
naming, game-over, attract, and ending screens remain fully 2D.

## Build

On Windows, use the bundled SDL2 SDK through CMake:

```text
cmake -S . -B build -A x64
cmake --build build --config Release
```

On Linux/macOS, the existing Makefile expects SDL2 development libraries:

```text
make
```

The ROM and generated asset pack remain local inputs and are not part of this
prototype or its source distribution.

## Alpha packaging

`build\dist\Zelda3-Voxel-Alpha-0.2.zip` bundles the game, SDL2, the
launcher, default settings, and a README. It deliberately excludes
`zelda3_assets.dat` — players supply their own asset pack built from their
ROM. Rebuild the package by copying fresh binaries from `build\Release`.

Diagonal wall corners chamfer instead of rendering as square pillars,
two-level dungeon rooms pick each pixel's floor from the PPU layer that drew
it (upper walkways rise above the floor below), actors cast soft radial
contact shadows that can never occlude another actor, and lightning during
the rainstorm flashes across the whole diorama.

## Next implementation slice

- True 3D weather: rain-streak billboards and fog volumes in the scene
  instead of texture-borne streaks.
- Diagonal top faces for slope cells (geometry, not just stepped heights).
- Verify the two-level room handling in the castle sewers.
