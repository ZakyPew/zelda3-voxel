# Zelda3 voxel presentation slice

This fork contains the first reversible voxel presentation prototype.

## Enable it

Use the OpenGL backend and add this to `zelda3.ini`:

```ini
[Graphics]
OutputMethod=OpenGL
VoxelMode=true
```

This dedicated fork enables `VoxelMode=true` in its sample configuration.
Set `VoxelMode=false` to keep the normal Zelda3 renderer. The voxel
path currently converts the finished frame into a colored, depth-tested
heightfield. This is intentionally a presentation-only seam: gameplay,
collision, saves, menus, and the original framebuffer renderer are untouched.

Press `3` while running to toggle the voxel presentation without restarting.
The sample settings keep the top HUD flat while voxelizing the playfield.

- `VoxelizeHud=false` keeps the actual HUD glyphs, counters, and icons flat while
  removing the solid HUD backdrop. Set it to `true` only if you intentionally
  want the whole top strip voxelized.
- `VoxelSize=4` controls the sampling block size (2-12).
- `VoxelHeight=55` controls extrusion strength (5-100 percent).
- `VoxelHudHeight=48` controls the HUD-only cutoff in SNES pixels. Keeping this
  at the bottom of the UI band prevents the first gameplay rows from being
  repeated as a flat 2D strip above the voxel scene.

The voxel scene includes a low-contrast raised floor and depth-tested cubes so
the presentation reads as a grounded diorama. The current heightfield remains
frame-derived; it does not yet know which pixels are terrain, actors, or UI.

The HUD and 3D world use separate screen-space viewports. The world viewport is
physically clipped below the HUD, so perspective geometry cannot climb behind
the UI. The world projection uses a forward-facing 39-degree chase tilt modeled
after Gen1 Recomp's presentational perspective pass; Zelda's existing scrolling
keeps the player-centered scene moving beneath that camera.

Voxel presentation is gated to active dungeon, both overworld routes, and
dungeon-entry gameplay modules. File select, naming, interface/dialog,
game-over, attract, and ending screens remain in the original 2D presentation
instead of being interpreted as terrain. A future PPU-layer split can keep the
3D world active beneath dialogs while drawing their UI independently.

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

## Next implementation slice

Replace the framebuffer sampling in `src/opengl.c` with tile-aware extraction
from the overworld tilemap and sprite/OAM state. That will allow terrain
profiles, Link grounding, and proper sprite billboards while retaining this
renderer boundary.
