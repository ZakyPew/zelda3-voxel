# Zelda3 Voxel

Zelda3 Voxel is a new presentation layer for the reverse-engineered **The
Legend of Zelda: A Link to the Past** reimplementation. It keeps the original
gameplay, collision, saves, controls, and ROM-verified logic while presenting
the playfield as a stylized, depth-tested voxel diorama.

It is a standalone ZakyPew project inspired by the presentation work in
Epoch/Equinox and the Gen1 Recomp project. The goal is a polished, configurable
voxel Zelda experience built on top of the proven Zelda3 reimplementation.

> Zelda3 Voxel is an unofficial fan project and is not affiliated with or
> endorsed by Nintendo.

## The product

The project is organized around two pieces:

- **Zelda3 Voxel** — the game runtime with the voxel presentation renderer.
- **Zelda3 Voxel Launcher** — a Windows launcher with a cinematic product page,
  full Zelda3 configuration, and one-click startup.

The launcher is the recommended way to start the Windows build. It writes the
selected settings to `zelda3.ini`, keeps the runtime working directory correct,
and launches the game with the local generated asset pack.

Its settings tabs cover the original Zelda3 configuration sections—graphics,
audio and MSU, widescreen and general behavior, gameplay features, keyboard
bindings, and gamepad bindings—alongside the voxel presentation controls.

## Current features

- Voxelized, depth-tested playfield generated from the original framebuffer.
- Forward-facing chase-camera perspective inspired by Gen1 Recomp.
- Separate world and HUD viewports so the 3D scene cannot project behind HUD
  elements.
- Original HUD and menus kept flat and readable.
- Voxel presentation limited to active dungeon and overworld gameplay modules;
  title, file select, naming, dialogs, game over, and ending screens remain 2D.
- Runtime toggle with the `3` key.
- Configurable voxel mode, HUD treatment, sampling size, extrusion height,
  camera scale, fullscreen, filtering, audio, and display settings.
- Original Zelda3 controls, snapshots, replay tools, saves, and enhanced
  widescreen support remain available.

## Inherited Zelda3 features

The voxel presentation is our project. The following game systems and features
come from the underlying Zelda3 reimplementation and should be credited to its
original authors and contributors:

| Feature | Attribution |
| --- | --- |
| Reverse-engineered C reimplementation of the complete game | [snesrev](https://github.com/snesrev/zelda3) and Zelda3 contributors |
| ROM extraction pipeline and runtime asset format | Zelda3 authors and contributors |
| Original gameplay logic, collision, menus, saves, controls, and snapshots | Zelda3 authors and contributors |
| PPU and DSP emulation foundation | [LakeSnes](https://github.com/elzo-d/LakeSnes), with Zelda3 integration and optimizations |
| Function names, variables, and disassembly research | [spannerism](https://github.com/spannerism) and the documented Zelda3 disassembly contributors |
| Widescreen, pixel shaders, MSU audio, secondary item slot, and other enhancements | Zelda3 authors and contributors |

Zelda3 Voxel does not claim authorship of those systems. Our additions are the
voxel presentation pass, chase-camera projection, HUD/world viewport
composition, launcher, product packaging, and related configuration work.

## Status

This is an active presentation prototype, not a finished commercial release.
The current voxel pass is framebuffer-derived, so terrain, Link, enemies,
particles, and foreground objects are not yet separated into individual 3D
systems. The next renderer milestones are tile-aware terrain extraction,
upright sprite billboards, player grounding, configurable camera controls, and
keeping the voxel world visible beneath independently composited dialogs.

## Windows quick start

1. Obtain a legal US copy of the original ROM. The expected file is named
   `zelda3.sfc`.
2. Run the asset extraction step once to create the local
   `zelda3_assets.dat` runtime pack.
3. Start `build/Release/Zelda3 Voxel Launcher.exe`.
4. Use **Settings** to choose voxel presentation and display options, then
   select **Launch Zelda3 Voxel**.

The ROM and generated asset pack are local runtime inputs. They are not
included in this repository or distributed by this project.

## Launcher

The launcher source is in [`launcher/`](launcher/). To publish the Windows
launcher from the repository root:

```powershell
dotnet publish launcher\Zelda3VoxelLauncher.csproj -c Release -r win-x64 --self-contained false -o build\Release
```

The launcher expects the game executable and generated asset pack in the same
directory as the published launcher, or in the configured project directory.

## Building the game

### Windows with CMake

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel 8
```

### Linux and macOS

Install SDL2 development libraries and the Python packages used by the asset
pipeline, then build with:

```sh
python3 -m pip install -r requirements.txt
make
```

The upstream Zelda3 build documentation contains additional platform and
toolchain notes: [Zelda3 build wiki](https://github.com/snesrev/zelda3/wiki).

## Configuration

The sample [`zelda3.ini`](zelda3.ini) contains the product defaults:

```ini
[Graphics]
OutputMethod=OpenGL
VoxelMode=true
VoxelizeHud=false
VoxelSize=4
VoxelHeight=55
VoxelHudHeight=48
```

Important controls:

| Setting | Purpose |
| --- | --- |
| `VoxelMode` | Enables the voxel presentation at startup. |
| `VoxelizeHud` | Keeps the HUD flat when `false`; voxelizes it when `true`. |
| `VoxelSize` | Framebuffer sampling size, from 2 to 12. |
| `VoxelHeight` | Voxel extrusion strength, from 5 to 100 percent. |
| `VoxelHudHeight` | Height of the flat HUD viewport in SNES pixels. |

Press `3` during gameplay to toggle voxel mode without restarting.

## Controls

The default game controls are inherited from Zelda3:

| Action | Key |
| --- | --- |
| Up / Down / Left / Right | Arrow keys |
| Start | Enter |
| Select | Right Shift |
| A / B | X / Z |
| X / Y | S / A |
| L / R | C / V |
| Toggle voxel presentation | 3 |
| Toggle fullscreen | Alt+Enter |
| Pause | P |
| Turbo mode | Tab |

Controls can be remapped in `zelda3.ini`. The original runtime also supports
snapshots, replay input history, health and inventory test shortcuts, and
renderer diagnostics.

## Technical foundation

Zelda3 Voxel is based on the open-source Zelda3 reverse-engineered
reimplementation. That project reproduces the original game in C and can
compare its runtime state against the original machine code for verification.
The voxel work is intentionally isolated at the presentation boundary so the
game logic remains compatible with the existing runtime.

See [`VOXEL_SLICE.md`](VOXEL_SLICE.md) for the current renderer notes and
[`CLAUDE_HANDOFF.md`](CLAUDE_HANDOFF.md) for the implementation handoff and
next technical slice.

## Credits and attribution

The Zelda3 Voxel product is authored and packaged by ZakyPew. It is built on
the Zelda3 reimplementation by [snesrev](https://github.com/snesrev/zelda3),
its contributors, [spannerism](https://github.com/spannerism), the documented
Zelda3 disassembly contributors, and the [LakeSnes](https://github.com/elzo-d/LakeSnes)
PPU/DSP foundation.

The presentation direction also draws inspiration from [Epoch & Equinox](https://github.com/ZakyPew/epoch-equinox)
and the Gen1 Recomp project. Please preserve all upstream attribution and
license notices when redistributing.

## License

The underlying Zelda3 reimplementation is licensed under the MIT license. See
[`LICENSE.txt`](LICENSE.txt) for the repository license text.
