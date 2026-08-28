# Zelda3 Voxel — Alpha 0.2

A voxel/diorama presentation of The Legend of Zelda: A Link to the Past,
built on the zelda3 reimplementation. Gameplay, collision, saves, and
controls are the original game; only the presentation is new.

## You need to supply one file

This package contains **no game data**. Copy your own `zelda3_assets.dat`
(produced by the zelda3 asset extractor from your legally owned ALTTP ROM)
into this folder, next to `zelda3.exe`.

## Running

1. Put `zelda3_assets.dat` in this folder.
2. Start `Zelda3 Voxel Launcher.exe` (requires the .NET 9 Desktop Runtime;
   Windows will offer to install it if missing).
3. Press START GAME. All diorama, graphics, sound, and gameplay settings
   live in the launcher's SETTINGS page and are written to `zelda3.ini`.

You can also run `zelda3.exe` directly; it reads the same `zelda3.ini`.

## The voxel presentation

- Press `3` in-game to toggle between the voxel diorama and the original
  flat renderer at any time.
- Terrain heights come from the game's own tile attributes (walls rise,
  water recedes, pits drop out), textured with the game's real pixel art.
- Link, enemies, and NPCs stand upright as pixel-perfect billboards with
  contact shadows.
- The HUD, dialog boxes, and menus render flat above the 3D scene.
- Camera pitch and zoom are adjustable in the launcher's Diorama tab.

## Alpha limitations

- Menus/map screens intentionally fall back to the flat renderer.
- Weather overlays (rain, fog) blend into the terrain rather than
  rendering as separate effects.
- Multi-level dungeon rooms use the lower floor's height profile.

Save files live in the `saves` folder next to the game. F1-F10 load save
states, Shift+F1-F10 save them.
