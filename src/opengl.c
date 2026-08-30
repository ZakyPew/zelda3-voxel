#include "third_party/gl_core/gl_core_3_1.h"
#include <SDL.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include "types.h"
#include "util.h"
#include "glsl_shader.h"
#include "config.h"
#include "variables.h"
#include "snes/ppu.h"
#include "zelda_rtl.h"
#include "tile_detect.h"
#include "overworld.h"

#define CODE(...) #__VA_ARGS__

static SDL_Window *g_window;
static uint8 *g_screen_buffer;
static size_t g_screen_buffer_size;
static int g_draw_width, g_draw_height;
static unsigned int g_program, g_hud_program, g_VAO;
static unsigned int g_voxel_program, g_voxel_VAO, g_voxel_VBO;
static GlTextureWithSize g_texture;
static GlslShader *g_glsl_shader;
static bool g_opengl_es;

// World atlas: the loaded map area rasterized from RAM (tilemaps + VRAM +
// CGRAM), so terrain can extend beyond the edges of the rendered frame.
static uint32 *g_world_pixels;      // BGRA, g_world_w * g_world_h
static unsigned int g_world_texture;
static int g_world_w, g_world_h;            // atlas size in world pixels
static int g_world_ox, g_world_oy;          // world coords of atlas (0,0)
static uint32 g_world_key;                  // scene identity for rebuilds
static uint32 g_world_frame;                // frame counter for refreshes
static bool g_world_valid;

// Chase camera: yaw follows Link's facing, pivot follows his position.
static float g_chase_yaw, g_chase_px, g_chase_pz;
static bool g_chase_snap = true;

// With a rotated follow camera, the d-pad must be screen-relative or the
// controls fight the view: rotate Up/Down/Left/Right by the camera's
// quarter-turn so "up" is always the way the camera faces. Only active
// during live gameplay modules (menus and dialogs keep the raw d-pad).
uint32 VoxelRemapJoypadForCamera(uint32 input) {
  bool gameplay = main_module_index == 7 || main_module_index == 9 ||
                  main_module_index == 11 || main_module_index == 17;
  if (!g_config.voxel_mode || g_config.voxel_camera == 0 || g_chase_snap ||
      !gameplay || !(input & 0xf0))
    return input;
  int q = (int)lroundf(g_chase_yaw / 1.57079633f);
  q = ((q % 4) + 4) % 4;
  if (q == 0)
    return input;
  uint32 u = input >> 4 & 1, d = input >> 5 & 1, l = input >> 6 & 1, r = input >> 7 & 1;
  uint32 n, s, w, e;
  switch (q) {
    case 1: w = u, e = d, s = l, n = r; break;   // camera faces west
    case 2: s = u, n = d, e = l, w = r; break;   // camera faces south
    default: e = u, w = d, n = l, s = r; break;  // camera faces east
  }
  return (input & ~0xf0u) | n << 4 | s << 5 | w << 6 | e << 7;
}

// Per-fragment texturing modes, carried in the third uv component. The frame
// texture keeps its per-pixel layer tag in the alpha byte, so the fragment
// shader can show the real pixel art on terrain tops and cut actors out of
// the frame at native resolution, independent of the voxel block size.
enum {
  kVoxelTex_None = 0,     // flat vertex color (cube sides, floor)
  kVoxelTex_Terrain = 1,  // frame pixels; fall back to color on non-world tags
  kVoxelTex_Actor = 2,    // frame pixels; discard non-sprite tags
  kVoxelTex_Shadow = 3,   // translucent black contact shadow
  kVoxelTex_World = 4,    // world-atlas pixels (off-screen terrain), tinted by color
  kVoxelTex_Rain = 5,     // translucent falling rain streak
};

typedef struct VoxelVertex {
  float x, y, z;
  float r, g, b;
  float u, v, mode;
} VoxelVertex;

static void VoxelPush(VoxelVertex *vertices, int *count, float x, float y, float z,
                      float r, float g, float b, float u, float v, float mode) {
  VoxelVertex *vx = &vertices[(*count)++];
  vx->x = x, vx->y = y, vx->z = z;
  vx->r = r, vx->g = g, vx->b = b;
  vx->u = u, vx->v = v, vx->mode = mode;
}

static void VoxelQuad(VoxelVertex *vertices, int *count,
                      float ax, float ay, float az, float bx, float by, float bz,
                      float cx, float cy, float cz, float dx, float dy, float dz,
                      float r, float g, float b) {
  VoxelPush(vertices, count, ax, ay, az, r, g, b, 0, 0, kVoxelTex_None);
  VoxelPush(vertices, count, bx, by, bz, r, g, b, 0, 0, kVoxelTex_None);
  VoxelPush(vertices, count, cx, cy, cz, r, g, b, 0, 0, kVoxelTex_None);
  VoxelPush(vertices, count, ax, ay, az, r, g, b, 0, 0, kVoxelTex_None);
  VoxelPush(vertices, count, cx, cy, cz, r, g, b, 0, 0, kVoxelTex_None);
  VoxelPush(vertices, count, dx, dy, dz, r, g, b, 0, 0, kVoxelTex_None);
}

// A cube whose top face carries a uv rect into the frame texture (pass
// kVoxelTex_None to keep it flat-colored). Sides stay flat-shaded; all four
// side faces are emitted since follow cameras can view from any direction.
static void VoxelCube(VoxelVertex *vertices, int *count, float x, float y, float z,
                      float sx, float sy, float sz, float r, float g, float b,
                      float u0, float v0, float u1, float v1, float top_mode) {
  float x0 = x, x1 = x + sx, y0 = y, y1 = y + sy, z0 = z, z1 = z + sz;
  VoxelPush(vertices, count, x0,y1,z0, r,g,b, u0,v0, top_mode);
  VoxelPush(vertices, count, x1,y1,z0, r,g,b, u1,v0, top_mode);
  VoxelPush(vertices, count, x1,y1,z1, r,g,b, u1,v1, top_mode);
  VoxelPush(vertices, count, x0,y1,z0, r,g,b, u0,v0, top_mode);
  VoxelPush(vertices, count, x1,y1,z1, r,g,b, u1,v1, top_mode);
  VoxelPush(vertices, count, x0,y1,z1, r,g,b, u0,v1, top_mode);
  VoxelQuad(vertices, count, x0,y0,z1, x1,y0,z1, x1,y1,z1, x0,y1,z1, r*.72f,g*.72f,b*.72f);
  VoxelQuad(vertices, count, x1,y0,z0, x1,y0,z1, x1,y1,z1, x1,y1,z0, r*.52f,g*.52f,b*.52f);
  VoxelQuad(vertices, count, x0,y0,z1, x0,y0,z0, x0,y1,z0, x0,y1,z1, r*.62f,g*.62f,b*.62f);
  VoxelQuad(vertices, count, x1,y0,z0, x0,y0,z0, x0,y1,z0, x1,y1,z0, r*.66f,g*.66f,b*.66f);
}

// Terrain data for one sampled block of the frame. height is the terrain
// column's extrusion; kCellUnknown marks ground hidden behind an actor or UI
// glyph (filled from neighbors), kCellVoid marks pits/borders with no ground.
#define kCellUnknown -1.0f
#define kCellVoid -2.0f
typedef struct VoxelCell {
  float r, g, b;  // terrain color (side shading + fill under actors/UI)
  float height;
  float coff[4];  // slope cells: per-corner height offsets (TL,TR,BL,BR)
  float fade;     // distance dimming for off-screen world cells
  bool upper;     // indoors: cell's pixels mostly come from the upper level (BG2)
  bool off;       // cell lies outside the rendered frame (atlas-textured)
  bool slope;     // top face is a diagonal ramp, not a flat plateau
} VoxelCell;

// Terrain profile from the game's tile attribute maps: walls rise, floors
// stay flat, water recedes, pits drop out. Luminance only adds gentle relief
// within a class, so busy tile art no longer produces corrugated ground.
static float VoxelAttrGroundHeight(float lum, float hs) {
  return .035f + lum * hs * .08f;
}

static float VoxelAttrWallHeight(float lum, float hs) {
  return .045f + hs * (.16f + lum * .08f);
}

static float VoxelAttrHeight(uint8 a, float lum, float hs) {
  if (a == 0x20)
    return kCellVoid;                                     // pit
  if (a == 0x08 || a == 0x0A)
    return .015f;                                         // deep water
  if (a == 0x09)
    return .03f;                                          // shallow water
  if (a >= 0x01 && a <= 0x03)
    return VoxelAttrWallHeight(lum, hs);                  // solid walls
  if (a >= 0x28 && a <= 0x2F)
    return .04f + hs * .16f;                              // ledges
  if ((a >= 0x50 && a <= 0x57) || (a >= 0x70 && a <= 0x7F) || a == 0x66 || a == 0x67)
    return .035f + hs * (.075f + lum * .035f);             // furniture, bushes, rocks, pots, pegs
  if (a == 0x0D)
    return .04f + hs * .10f;                              // spikes
  // Everything else is walkable ground - including 0x14-0x17, which are
  // plain "nothing" tiles in the game's collision code, not slopes.
  return VoxelAttrGroundHeight(lum, hs);
}

static bool VoxelAttrIsSolid(uint8 a) {
  return (a >= 0x01 && a <= 0x03) ||
         (a >= 0x50 && a <= 0x57) || (a >= 0x70 && a <= 0x7F) ||
         a == 0x66 || a == 0x67;
}

// Diagonal wall corner tiles: 0x10-0x13 inner and 0x18-0x1B outer variants.
// Orientation is attr&3, matching the game's kSlopedTile collision tables
// (tile-local x=0 west, y=0 north): 0 = NW triangle solid (x+y <= 7),
// 1 = NE (y <= x), 2 = SW (y >= x), 3 = SE (x+y >= 7).
static bool VoxelAttrIsSlope(uint8 a) {
  return (a >= 0x10 && a <= 0x13) || (a >= 0x18 && a <= 0x1B);
}

// Linear ramp across a slope tile: 1.0 deep in the solid corner, 0.5 on the
// diagonal, 0.0 at the opposite corner - so slope cells become true ramps.
static float VoxelSlopeRamp(uint8 a, int x, int y) {
  switch (a & 3) {
    case 0:  return 1.0f - (x + y) * (1.0f / 14.0f);              // NW solid
    case 1:  return 1.0f - ((7 - x) + y) * (1.0f / 14.0f);        // NE solid
    case 2:  return 1.0f - (x + (7 - y)) * (1.0f / 14.0f);        // SW solid
    default: return 1.0f - ((7 - x) + (7 - y)) * (1.0f / 14.0f);  // SE solid
  }
}

// Build a slope cell: per-corner heights from the ramp, average as the
// cell height, offsets stored so lifts applied to the height carry the
// ramp along unchanged.
static void VoxelSlopeCell(VoxelCell *c, uint8 attr, float lum, float hs,
                           int twx, int twy, int w, int h) {
  float gh = VoxelAttrGroundHeight(lum, hs);
  float wh = VoxelAttrWallHeight(lum, hs);
  float sum = 0.0f;
  for (int i = 0; i < 4; i++) {
    int x = (twx + (i & 1) * (w - 1)) & 7;
    int y = (twy + (i >> 1) * (h - 1)) & 7;
    c->coff[i] = gh + (wh - gh) * VoxelSlopeRamp(attr, x, y);
    sum += c->coff[i];
  }
  c->height = sum * .25f;
  for (int i = 0; i < 4; i++)
    c->coff[i] -= c->height;
  c->slope = true;
}

// Height of a neighbor cell along the shared edge (its lowest corner on
// that edge for slope neighbors), used to hem side faces without slits.
static float VoxelNeighborEdgeH(const VoxelCell *cells, int cols, int rows,
                                int row, int col, int e0, int e1) {
  if (row < 0 || row >= rows || col < 0 || col >= cols)
    return 0.0f;
  const VoxelCell *n = &cells[row * cols + col];
  float h = n->height;
  if (h < 0.0f)
    return 0.0f;
  if (n->slope)
    h += n->coff[e0] < n->coff[e1] ? n->coff[e0] : n->coff[e1];
  return h < 0.0f ? 0.0f : h;
}

// Tile attribute at a world-pixel position, mirroring the game's own
// collision lookups (read-only; no game state is touched). Indoors the
// attribute table has two halves: +0 describes the PPU BG2 tilemap (the
// upper level in two-level rooms) and +0x1000 the PPU BG1 tilemap (the
// lower level), so callers pass which layer won the pixel.
static uint8 VoxelTileAttrAt(int wx, int wy, bool bg1_layer) {
  if (player_is_indoors)
    return dung_bg2_attr_table[((wx & 0x1f8) >> 3) + ((wy & 0x1f8) << 3) +
                               (bg1_layer ? 0x1000 : 0)];
  return Overworld_GetTileAttributeAtLocation((uint16)(wx >> 3), (uint16)wy);
}

// ---- Overworld terracing --------------------------------------------------
// ALTTP implies elevation instead of storing it: ledge tiles (0x28-0x2F) are
// one-story drops in their jump direction. Segment walkable ground into
// regions bounded by walls/slopes/ledges, order the regions with the ledge
// constraints, and lift each region by its solved story count - so mesas and
// terraces rise as real plateaus instead of sinking behind their rim walls.

static bool VoxelAttrIsLedge(uint8 a) {
  return a >= 0x28 && a <= 0x2F;
}

static bool VoxelAttrIsStairs(uint8 a) {
  return a == 0x1D || a == 0x1E || a == 0x1F || a == 0x22 || a == 0x26 ||
         a == 0x3D || a == 0x3E || a == 0x3F;
}

static bool VoxelTerraceBoundary(uint8 a) {
  return (a >= 0x01 && a <= 0x03) || VoxelAttrIsSlope(a) || VoxelAttrIsLedge(a) ||
         VoxelAttrIsStairs(a) || a == 0x20 || (a >= 0x4C && a <= 0x4F);
}

// Downhill (jump) direction per ledge attr, from the game's ledge-hop code:
// 0x28 N, 0x29 S, 0x2A W, 0x2B E, then NW, SW, NE, SE diagonals.
static const int8 kVoxelLedgeDir[8][2] = {
  {0, -1}, {0, 1}, {-1, 0}, {1, 0}, {-1, -1}, {-1, 1}, {1, -1}, {1, 1},
};

static void VoxelSolveTerraces(VoxelCell *cells, int cols, int rows,
                               int wx0, int wy0, int fx_start, int fy_start,
                               int step, int rscale, int snes_step, float lift) {
  // Analyze a margin beyond the visible frame so a terrace keeps its level
  // for a while after its ledges scroll off screen.
  enum { M = 16, kMaxLevel = 6 };
  const int acols = cols + M * 2, arows = rows + M * 2, total = acols * arows;
  uint8 *attr = malloc(total);
  int *region = malloc(total * sizeof(int));
  int *queue = malloc(total * sizeof(int));
  int *level = malloc(total * sizeof(int));
  int *cup = malloc(total * sizeof(int));
  int *cdn = malloc(total * sizeof(int));
  if (!attr || !region || !queue || !level || !cup || !cdn)
    goto cleanup;

  for (int ar = 0; ar < arows; ar++) {
    for (int ac = 0; ac < acols; ac++) {
      int wx = wx0 + (fx_start + (ac - M) * step) / rscale + snes_step / 2;
      int wy = wy0 + (fy_start + (ar - M) * step) / rscale + snes_step / 2;
      // Attribute lookups wrap at the loaded area's edges, which would
      // import phantom regions and ledge constraints from the far side of
      // the map - treat out-of-area samples as solid boundary instead.
      if (g_world_valid && (wx < g_world_ox || wx >= g_world_ox + g_world_w ||
                            wy < g_world_oy || wy >= g_world_oy + g_world_h))
        attr[ar * acols + ac] = 0x01;
      else
        attr[ar * acols + ac] = VoxelTileAttrAt(wx, wy, false);
    }
  }

  // Flood-fill ground regions (4-connected, boundaries block).
  int nregions = 0;
  for (int i = 0; i < total; i++)
    region[i] = VoxelTerraceBoundary(attr[i]) ? -1 : -2;
  for (int i = 0; i < total; i++) {
    if (region[i] != -2)
      continue;
    int head = 0, tail = 0, id = nregions++;
    queue[tail++] = i, region[i] = id;
    while (head < tail) {
      int p = queue[head++], pr = p / acols, pc = p % acols;
      if (pc > 0 && region[p - 1] == -2) region[p - 1] = id, queue[tail++] = p - 1;
      if (pc + 1 < acols && region[p + 1] == -2) region[p + 1] = id, queue[tail++] = p + 1;
      if (pr > 0 && region[p - acols] == -2) region[p - acols] = id, queue[tail++] = p - acols;
      if (pr + 1 < arows && region[p + acols] == -2) region[p + acols] = id, queue[tail++] = p + acols;
    }
  }
  memset(level, 0, (size_t)nregions * sizeof(int));

  // One constraint per ledge cell: the region on the uphill side sits at
  // least one story above the region on the downhill side.
  int ncons = 0;
  for (int i = 0; i < total; i++) {
    if (!VoxelAttrIsLedge(attr[i]))
      continue;
    int dx = kVoxelLedgeDir[attr[i] - 0x28][0], dy = kVoxelLedgeDir[attr[i] - 0x28][1];
    int ar = i / acols, ac = i % acols;
    int ru = -1, rd = -1;
    for (int s = 1; s <= 3 && ru < 0; s++) {
      int nr = ar - dy * s, nc = ac - dx * s;
      if (nr < 0 || nr >= arows || nc < 0 || nc >= acols)
        break;
      ru = region[nr * acols + nc];
    }
    for (int s = 1; s <= 3 && rd < 0; s++) {
      int nr = ar + dy * s, nc = ac + dx * s;
      if (nr < 0 || nr >= arows || nc < 0 || nc >= acols)
        break;
      rd = region[nr * acols + nc];
    }
    if (ru >= 0 && rd >= 0 && ru != rd)
      cup[ncons] = ru, cdn[ncons] = rd, ncons++;
  }
  for (int it = 0; it < 10; it++) {
    bool changed = false;
    for (int k = 0; k < ncons; k++) {
      int want = level[cdn[k]] + 1;
      if (want > kMaxLevel)
        want = kMaxLevel;
      if (level[cup[k]] < want)
        level[cup[k]] = want, changed = true;
    }
    if (!changed)
      break;
  }

  // Lift the visible cells: ground by its region's level; boundary cells
  // (cliff faces, ledges) by the tallest nearby region so they connect the
  // stories; stairs midway so they read as ramps between the levels.
  for (int row = 0; row < rows; row++) {
    for (int col = 0; col < cols; col++) {
      VoxelCell *c = &cells[row * cols + col];
      if (c->height < 0.0f)
        continue;
      int ar = row + M, ac = col + M;
      int r = region[ar * acols + ac];
      int lv = 0;
      if (r >= 0) {
        lv = level[r];
      } else {
        int lvmax = 0, lvmin = kMaxLevel, found = 0;
        for (int dr = -2; dr <= 2; dr++) {
          for (int dc = -2; dc <= 2; dc++) {
            int q = region[(ar + dr) * acols + (ac + dc)];
            if (q >= 0) {
              found = 1;
              if (level[q] > lvmax) lvmax = level[q];
              if (level[q] < lvmin) lvmin = level[q];
            }
          }
        }
        lv = !found ? 0 : VoxelAttrIsStairs(attr[ar * acols + ac])
                          ? (lvmax + lvmin + 1) / 2 : lvmax;
      }
      if (lv > 0)
        c->height += lv * lift;
    }
  }

cleanup:
  free(attr), free(region), free(queue), free(level), free(cup), free(cdn);
}

// ---- World atlas ----------------------------------------------------------
// The full loaded map lives in RAM (dungeon room tilemaps / overworld map16
// grid), so the world can be rasterized from VRAM+CGRAM exactly the way the
// PPU draws it - giving terrain beyond the edges of the rendered frame.

// Rasterize one 4bpp BG tilemap word at (dx, dy). `over` keeps existing
// pixels where this tile is transparent (used to layer BG2 over BG1).
static void VoxelRasterTile(uint32 *dst, int dst_w, int dx, int dy,
                            uint16 tile, int tile_base, bool over) {
  Ppu *ppu = g_zenv.ppu;
  for (int y = 0; y < 8; y++) {
    int fine = (tile & 0x8000) ? 7 - y : y;
    const uint16 *addr = &ppu->vram[(tile_base + (tile & 0x3ff) * 16 + fine) & 0x7fff];
    uint32 bits = addr[0] | (uint32)addr[8] << 16;
    uint32 *out = dst + (size_t)(dy + y) * dst_w + dx;
    uint32 pal = (tile & 0x1c00) >> 6;
    for (int x = 0; x < 8; x++) {
      int i = (tile & 0x4000) ? x : 7 - x;
      uint32 pixel = (bits >> i) & 1 | (bits >> (7 + i)) & 2 |
                     (bits >> (14 + i)) & 4 | (bits >> (21 + i)) & 8;
      if (!pixel && over)
        continue;
      uint32 c = ppu->cgram[pixel ? pal + pixel : 0];
      uint32 cr = ppu->brightnessMult[c & 0x1f];
      uint32 cg = ppu->brightnessMult[(c >> 5) & 0x1f];
      uint32 cb = ppu->brightnessMult[(c >> 10) & 0x1f];
      // Desktop uploads BGRA words; ES has no BGRA external format, so its
      // atlas is stored RGBA byte order and uploaded as GL_RGBA.
      out[x] = 0xff000000u | (g_opengl_es ? cb << 16 | cg << 8 | cr
                                          : cr << 16 | cg << 8 | cb);
    }
  }
}

static bool VoxelWorldBuildDungeon(void) {
  // The whole 512x512 room is in dung_bg2/dung_bg1 (64x64 tilemap words).
  // PPU BG2 (dung_bg2) is the room's main layer and PPU BG1 (dung_bg1) the
  // overlay drawn on top of it, so composite bg2 first, bg1 over.
  Ppu *ppu = g_zenv.ppu;
  g_world_w = 512, g_world_h = 512;
  g_world_ox = (((int)(uint16)BG2HOFS_copy + 128) & ~0x1ff);
  g_world_oy = (((int)(uint16)BG2VOFS_copy + 112) & ~0x1ff);
  for (int ty = 0; ty < 64; ty++) {
    for (int tx = 0; tx < 64; tx++) {
      int k = ty * 64 + tx;
      VoxelRasterTile(g_world_pixels, 512, tx * 8, ty * 8,
                      dung_bg2[k], ppu->bgLayer[1].tileAdr, false);
      VoxelRasterTile(g_world_pixels, 512, tx * 8, ty * 8,
                      dung_bg1[k], ppu->bgLayer[0].tileAdr, true);
    }
  }
  return true;
}

static bool VoxelWorldBuildOverworld(void) {
  // overworld_tileattr holds the loaded area's map16 tile numbers, row-major
  // with a fixed stride of 64; a big (2x2-screen) area uses the whole 64x64
  // buffer, a small area only its top-left 32x32 quadrant (the rest holds
  // neighbor-screen data that the masked lookups never address).
  Ppu *ppu = g_zenv.ppu;
  int w = overworld_area_is_big ? 1024 : 512;
  g_world_w = w, g_world_h = w;
  g_world_ox = (int)(uint16)overworld_offset_base_x * 8;
  g_world_oy = (int)(uint16)overworld_offset_base_y;
  const uint16 *m8tab = GetMap16toMap8Table();
  int tile_base = ppu->bgLayer[1].tileAdr;
  for (int y16 = 0; y16 < w / 16; y16++) {
    for (int x16 = 0; x16 < w / 16; x16++) {
      int m16 = overworld_tileattr[y16 * 64 + x16];
      if (m16 >= 0xea8)
        continue;  // outside the map16 asset range (stale buffer data)
      for (int q = 0; q < 4; q++) {
        VoxelRasterTile(g_world_pixels, w, x16 * 16 + (q & 1) * 8,
                        y16 * 16 + (q >> 1) * 8, m8tab[m16 * 4 + q],
                        tile_base, false);
      }
    }
  }
  return true;
}

static bool VoxelWorldAtlas_Refresh(void) {
  if (!g_world_pixels) {
    // Zeroed so skipped tiles can never expose uninitialized memory.
    g_world_pixels = calloc(1024 * 1024, sizeof(uint32));
    if (!g_world_pixels)
      return false;
  }
  g_world_frame++;
  // During overworld/dungeon transitions the map registers and buffers
  // update at different times - freeze the atlas until the scene settles,
  // then rebuild once from consistent data.
  bool transitioning = (main_module_index == 9 || main_module_index == 7) &&
                       submodule_index != 0;
  if (transitioning)
    return g_world_valid;
  uint32 key;
  if (player_is_indoors) {
    key = 0x80000000u | dungeon_room_index;
  } else {
    key = 0x40000000u | (uint32)(uint16)overworld_offset_base_x << 16 |
          (uint16)overworld_offset_base_y;
  }
  // Periodic re-rasterize keeps palette fades and tile animation fresh.
  if (key == g_world_key && g_world_valid && (g_world_frame & 31) != 0)
    return true;
  g_world_key = key;
  g_world_valid = player_is_indoors ? VoxelWorldBuildDungeon()
                                    : VoxelWorldBuildOverworld();
  if (g_world_valid) {
    static int tex_w, tex_h;
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_world_texture);
    GLenum fmt = g_opengl_es ? GL_RGBA : GL_BGRA;
    GLenum type = g_opengl_es ? GL_UNSIGNED_BYTE : GL_UNSIGNED_INT_8_8_8_8_REV;
    if (tex_w != g_world_w || tex_h != g_world_h) {
      tex_w = g_world_w, tex_h = g_world_h;
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_world_w, g_world_h, 0, fmt,
                   type, g_world_pixels);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_world_w, g_world_h, fmt, type,
                      g_world_pixels);
    }
    glActiveTexture(GL_TEXTURE0);
  }
  return g_world_valid;
}

// Average atlas color over a world-pixel rect; false if outside the atlas.
static bool VoxelWorldCell(int wx, int wy, int w, int h,
                           float *r, float *g, float *b) {
  int x0 = wx - g_world_ox, y0 = wy - g_world_oy;
  if (!g_world_valid || x0 < 0 || y0 < 0 || x0 + w > g_world_w || y0 + h > g_world_h)
    return false;
  unsigned sr = 0, sg = 0, sb = 0;
  for (int y = 0; y < h; y++) {
    const uint32 *line = g_world_pixels + (size_t)(y0 + y) * g_world_w + x0;
    for (int x = 0; x < w; x++)
      sb += line[x] & 255, sg += (line[x] >> 8) & 255, sr += (line[x] >> 16) & 255;
  }
  float n = (float)(w * h) * 255.0f;
  if (g_opengl_es) {
    *r = sb / n, *g = sg / n, *b = sr / n;  // atlas stored RGBA byte order
  } else {
    *r = sr / n, *g = sg / n, *b = sb / n;
  }
  return true;
}

static void VoxelRenderer_Draw(int width, int height, const uint8 *pixels,
                               int pitch, int viewport_x, int viewport_y,
                               int viewport_width, int viewport_height) {
  // The PPU tags each pixel's source layer in the alpha byte (kPpuPixelTag_*),
  // so a single rendered frame splits into terrain (BG tiles + backdrop),
  // actors (OAM sprites) and UI (BG3) without replaying the stateful PPU.
  const int rscale = height >= 400 ? 4 : 1;  // 4x4-upsampled mode7 frames
  const int snes_step = g_config.voxel_size;
  const int step = snes_step * rscale;
  const int extra_left = (width / rscale - 256) / 2;
  // Anchor the sampling grid to the world, not the screen: cell boundaries
  // stay glued to the same map pixels while the game scrolls, so terrain
  // heights ride along smoothly instead of flickering as the grid resamples.
  // BG2HOFS_copy (not _copy2) is what the PPU actually displayed - the two
  // differ by the shake offset during quake/rumble effects.
  const int wx0 = (int)(uint16)BG2HOFS_copy - extra_left;  // world x at frame x=0
  const int wy0 = (int)(uint16)BG2VOFS_copy;               // world y at frame y=0
  int gx = wx0 % snes_step, gy = wy0 % snes_step;
  if (gx < 0) gx += snes_step;
  if (gy < 0) gy += snes_step;
  // Tile attributes describe the loaded scene; the upsampled mode7 path has
  // no attribute map, so it falls back to pure luminance heights.
  const bool use_attr = rscale == 1;
  // With a valid world atlas the grid extends past the frame: the far rows
  // recede toward the horizon and the sides converge in perspective, so the
  // diorama sits inside a larger world instead of ending at the screen.
  // Margins are multiples of every supported voxel size to keep alignment.
  const bool world_ok = use_attr && VoxelWorldAtlas_Refresh();
  // Follow cameras need world in every direction (the view can face any way).
  const int cam = world_ok ? g_config.voxel_camera : 0;
  const bool chase = cam > 0, first_person = cam == 2;

  // Chase camera state: yaw eases toward Link's facing (N/S/W/E ->
  // 0/pi/+-pi/2) and the view pivots on his smoothed position.
  float yaw = 0.0f, pivot_x = 0.0f, pivot_z = 0.0f;
  if (chase) {
    static const float kFacingYaw[8] = {
      0, 0, 3.14159265f, 0, 1.57079633f, 0, -1.57079633f, 0,
    };
    float target = kFacingYaw[link_direction_facing & 7];
    float tx = -1.0f + ((int)(uint16)link_x_coord + 8 - wx0) * rscale * (2.0f / width);
    float tz = -1.0f + ((int)(uint16)link_y_coord + 16 - wy0) * rscale * (2.0f / height);
    if (g_chase_snap) {
      g_chase_yaw = target, g_chase_px = tx, g_chase_pz = tz;
      g_chase_snap = false;
    } else {
      float d = target - g_chase_yaw;
      while (d > 3.14159265f) d -= 6.28318531f;
      while (d < -3.14159265f) d += 6.28318531f;
      g_chase_yaw += d * .12f;
      while (g_chase_yaw > 3.14159265f) g_chase_yaw -= 6.28318531f;
      while (g_chase_yaw < -3.14159265f) g_chase_yaw += 6.28318531f;
      g_chase_px += (tx - g_chase_px) * .25f;
      g_chase_pz += (tz - g_chase_pz) * .25f;
    }
    yaw = g_chase_yaw, pivot_x = g_chase_px, pivot_z = g_chase_pz;
  } else {
    g_chase_snap = true;
  }
  const float cull_sy = sinf(yaw), cull_cy = cosf(yaw);

  // Grid margins beyond the frame: symmetric for the diorama, widened in
  // the facing direction for follow cameras so the vista runs deeper.
  int ml, mr, mt, mb;
  if (!world_ok) {
    ml = mr = mt = mb = 0;
  } else if (!chase) {
    ml = mr = 96, mt = 96, mb = 48;
  } else {
    int q = (int)lroundf(yaw / 1.57079633f);
    q = ((q % 4) + 4) % 4;
    ml = mr = mt = mb = 96;
    if (q == 0) mt = 192, mb = 48;
    else if (q == 2) mb = 192, mt = 48;
    else if (q == 1) ml = 192, mr = 48;   // facing west
    else mr = 192, ml = 48;               // facing east
  }
  const int fx_start = -gx * rscale - ml * rscale;
  const int fy_start = -gy * rscale - mt * rscale;
  const int cols = (width + mr * rscale - fx_start + step - 1) / step;
  const int rows = (height + mb * rscale - fy_start + step - 1) / step;

  VoxelCell *cells = malloc((size_t)cols * rows * sizeof(*cells));
  VoxelVertex *vertices = malloc(((size_t)cols * rows * 2 + 1) * 24 * sizeof(*vertices));
  if (!cells || !vertices) {
    free(cells), free(vertices);
    return;
  }
  const float height_scale = g_config.voxel_height * .01f;
  unsigned bg1_world_total = 0, world_total = 0;
  for (int row = 0; row < rows; row++) {
    for (int col = 0; col < cols; col++) {
      int fx0 = fx_start + col * step, fy0 = fy_start + row * step;
      VoxelCell *c = &cells[row * cols + col];
      c->fade = 1.0f, c->off = false, c->upper = false, c->slope = false;
      bool inside = fx0 >= 0 && fy0 >= 0 && fx0 + step <= width &&
                    fy0 + step <= height;
      if (!inside && world_ok) {
        // Off-screen (or frame-edge) cell: built purely from the map atlas,
        // dimmed with distance so the world recedes into the dark.
        c->off = true;
        int cwx = wx0 + fx0 / rscale, cwy = wy0 + fy0 / rscale;
        float rr, gg, bb;
        if (!VoxelWorldCell(cwx, cwy, snes_step, snes_step, &rr, &gg, &bb)) {
          c->height = kCellVoid;
          continue;
        }
        float luminance = rr * .299f + gg * .587f + bb * .114f;
        if (luminance < .025f) {
          c->height = kCellVoid;
          continue;
        }
        uint8 attr = VoxelTileAttrAt(cwx + snes_step / 2, cwy + snes_step / 2, false);
        if (VoxelAttrIsSlope(attr)) {
          VoxelSlopeCell(c, attr, luminance, height_scale, cwx, cwy,
                         snes_step, snes_step);
        } else {
          c->height = VoxelAttrHeight(attr, luminance, height_scale);
        }
        int over_x = fx0 < 0 ? -fx0 : (fx0 + step > width ? fx0 + step - width : 0);
        int over_y = fy0 < 0 ? -fy0 : (fy0 + step > height ? fy0 + step - height : 0);
        int over = IntMax(over_x, over_y) / rscale;
        // Fade nearly to black by the deepest margin so bands appearing or
        // vanishing on camera turns are invisible when they pop.
        c->fade = 1.0f - .95f * (IntMin(over, 176) / 176.0f);
        c->r = rr * c->fade, c->g = gg * c->fade, c->b = bb * c->fade;
        continue;
      }
      int x0 = fx0 < 0 ? 0 : fx0, y0 = fy0 < 0 ? 0 : fy0;
      int x1 = fx0 + step < width ? fx0 + step : width;
      int y1 = fy0 + step < height ? fy0 + step : height;
      unsigned wr = 0, wg = 0, wb = 0, wn = 0;
      unsigned an = 0, un = 0, w1 = 0, w2 = 0;
      for (int y = y0; y < y1; y++) {
        const uint32 *line = (const uint32 *)(pixels + y * pitch);
        for (int x = x0; x < x1; x++) {
          uint32 p = line[x], tag = p >> 24;
          if (tag < kPpuPixelTag_Valid)
            continue;  // blank border pixel
          uint32 layer = tag & kPpuPixelTag_LayerMask;
          if (layer == kPpuPixelTag_Sprite || layer == kPpuPixelTag_SpriteNoMath) {
            an++;
          } else if (layer == kPpuPixelTag_Bg3 && !g_config.voxelize_hud) {
            un++;  // UI composites flat afterwards; hides the ground here
          } else {
            wb += p & 255, wg += (p >> 8) & 255, wr += (p >> 16) & 255, wn++;
            w1 += layer == 0, w2 += layer == 1;  // BG1 (lower) vs BG2 (upper)
          }
        }
      }
      bg1_world_total += w1, world_total += wn;
      // Cells that also contain UI pixels still build terrain from their
      // world pixels: the terrain-top shader never shows UI-tagged pixels
      // (it falls back to the cell color there), so dialogue glyphs and the
      // HUD cannot contaminate the ground, and the world stays continuous
      // beneath them instead of tearing holes.
      // Indoors, PPU BG1 (dung_bg1) is the overlay layer drawn above the
      // BG2 main floor - walkways and bridges live there.
      c->upper = player_is_indoors && w1 > w2;
      if (wn) {
        c->r = (float)wr / (wn * 255.0f);
        c->g = (float)wg / (wn * 255.0f);
        c->b = (float)wb / (wn * 255.0f);
        float luminance = c->r * .299f + c->g * .587f + c->b * .114f;
        if (luminance < .025f) {
          c->height = kCellVoid;
        } else if (use_attr) {
          // The winning PPU layer says which tilemap drew the pixel, and
          // each attr-table half describes one tilemap: +0 for BG2 (the
          // main floor), +0x1000 for BG1 (the overlay walkways).
          bool bg1_layer = player_is_indoors && w1 > w2;
          int cwx = wx0 + (x0 + x1) / (2 * rscale);
          int cwy = wy0 + (y0 + y1) / (2 * rscale);
          uint8 attr = VoxelTileAttrAt(cwx, cwy, bg1_layer);
          if (VoxelAttrIsSlope(attr)) {
            // Diagonal wall corner: build a true ramp across the cell.
            VoxelSlopeCell(c, attr, luminance, height_scale,
                           wx0 + x0 / rscale, wy0 + y0 / rscale,
                           (x1 - x0) / rscale, (y1 - y0) / rscale);
          } else {
            c->height = VoxelAttrHeight(attr, luminance, height_scale);
            // Dungeon furniture often shares the collision class used by
            // solid walls. Keep those interior tiles as shallow platforms;
            // the room perimeter remains tall and provides the diorama rim.
            if (player_is_indoors && VoxelAttrIsSolid(attr) &&
                x0 > 24 && x1 < width - 24 && y0 > 24 && y1 < height - 24)
              c->height = .030f + height_scale * (.035f + luminance * .015f);
          }
        } else {
          c->height = .05f + luminance * height_scale;
        }
      } else {
        c->r = c->g = c->b = 0.0f;
        // Fully covered by actors or UI: reconstruct the floor from row
        // neighbors so actors have ground to stand on and dialog boxes sit
        // over ground instead of a hole.
        c->height = (an || un) ? kCellUnknown : kCellVoid;
      }
    }
  }

  // Two-level rooms: raise everything on the upper level so walkways read
  // as elevated above the floor seen through and beside them. Detected by a
  // meaningful share of lower-level (BG1) world pixels in the frame.
  if (player_is_indoors && bg1_world_total * 50 > world_total) {
    for (int i = 0; i < cols * rows; i++)
      if (cells[i].height >= 0.0f && cells[i].upper)
        cells[i].height += height_scale * .12f;
  }

  // Outdoors, solve terrace elevations from the ledge tiles so plateaus
  // rise as real stories instead of sinking flat behind their rim walls.
  if (use_attr && !player_is_indoors)
    VoxelSolveTerraces(cells, cols, rows, wx0, wy0, fx_start, fy_start,
                       step, rscale, snes_step, .02f + height_scale * .16f);

  // Follow cameras flatten the perspective, which makes low collision
  // relief (fences, hedges, wall strips) nearly invisible - amplify
  // everything above the ground plane so boundaries read clearly.
  if (chase) {
    for (int i = 0; i < cols * rows; i++)
      if (cells[i].height > .085f)
        cells[i].height = .085f + (cells[i].height - .085f) * 1.5f;
  }

  // Ground hidden behind actors or UI inherits the nearest resolved cell in
  // its row, so actors stand on plausible terrain instead of holes.
  for (int row = 0; row < rows; row++) {
    VoxelCell *line = &cells[row * cols];
    for (int pass = 0; pass < 2; pass++) {
      float dr = 0, dg = 0, db = 0, dh = kCellUnknown;
      for (int i = 0; i < cols; i++) {
        VoxelCell *c = &line[pass ? cols - 1 - i : i];
        if (c->height != kCellUnknown)
          dh = c->height, dr = c->r, dg = c->g, db = c->b;
        else if (dh != kCellUnknown)
          c->height = dh, c->r = dr, c->g = dg, c->b = db;
      }
    }
    for (int col = 0; col < cols; col++)
      if (line[col].height == kCellUnknown)
        line[col].height = kCellVoid;
  }

  int count = 0;
  // A quiet, slightly raised floor gives the scene a readable silhouette and
  // keeps the voxelized world from floating in empty space; it spans the
  // whole (possibly extended) grid.
  {
    float fl_x = -1.0f + fx_start * (2.0f / width);
    float fl_z = -1.0f + fy_start * (2.0f / height);
    float fl_w = cols * step * (2.0f / width);
    float fl_d = rows * step * (2.0f / height);
    VoxelCube(vertices, &count, fl_x, -0.035f, fl_z, fl_w, .035f, fl_d,
              .018f, .028f, .075f, 0, 0, 0, 0, kVoxelTex_None);
  }
  // Terrain cells tile seamlessly and their tops sample the frame's own
  // pixels, so the ground keeps its full pixel-art detail at any block size.
  // Side faces are emitted only down to the neighboring cell's height, so
  // flat ground produces no hidden interior walls and no coplanar overdraw.
  const float pxw = 2.0f / width, pxh = 2.0f / height;  // world units per frame px
  const float tw = 1.0f / width, th = 1.0f / height;
  for (int row = 0; row < rows; row++) {
    for (int col = 0; col < cols; col++) {
      VoxelCell *c = &cells[row * cols + col];
      if (c->height < 0.0f)
        continue;
      int fx0 = fx_start + col * step, fy0 = fy_start + row * step;
      float px0, px1, pz0, pz1, u0, v0, u1, v1, mode, tr, tg, tb;
      if (c->off) {
        // Atlas-textured cell: full geometry, uv into the world texture,
        // top tinted by the distance fade.
        px0 = -1.0f + fx0 * pxw, px1 = -1.0f + (fx0 + step) * pxw;
        pz0 = -1.0f + fy0 * pxh, pz1 = -1.0f + (fy0 + step) * pxh;
        int cwx = wx0 + fx0 / rscale, cwy = wy0 + fy0 / rscale;
        u0 = (cwx - g_world_ox) / (float)g_world_w;
        v0 = (cwy - g_world_oy) / (float)g_world_h;
        u1 = u0 + snes_step / (float)g_world_w;
        v1 = v0 + snes_step / (float)g_world_h;
        mode = kVoxelTex_World;
        tr = tg = tb = c->fade;
      } else {
        int x0 = fx0 < 0 ? 0 : fx0, y0 = fy0 < 0 ? 0 : fy0;
        int x1 = fx0 + step < width ? fx0 + step : width;
        int y1 = fy0 + step < height ? fy0 + step : height;
        if (x0 >= x1 || y0 >= y1)
          continue;
        px0 = -1.0f + x0 * pxw, px1 = -1.0f + x1 * pxw;
        pz0 = -1.0f + y0 * pxh, pz1 = -1.0f + y1 * pxh;
        u0 = x0 * tw, v0 = y0 * th, u1 = x1 * tw, v1 = y1 * th;
        mode = kVoxelTex_Terrain;
        tr = c->r, tg = c->g, tb = c->b;
      }
      if (first_person) {
        float dxm = (px0 + px1) * .5f - pivot_x, dzm = (pz0 + pz1) * .5f - pivot_z;
        if (dxm * cull_sy + dzm * cull_cy > .35f)
          continue;
      }
      float h = c->height;
      // Slope cells carry a diagonal ramp in their corner offsets; flat
      // cells keep all four corners at the cell height.
      float y00 = h, y10 = h, y01 = h, y11 = h;
      if (c->slope) {
        y00 += c->coff[0], y10 += c->coff[1];
        y01 += c->coff[2], y11 += c->coff[3];
      }
      float r = c->r, g = c->g, b = c->b;
      VoxelPush(vertices, &count, px0, y00, pz0, tr, tg, tb, u0, v0, mode);
      VoxelPush(vertices, &count, px1, y10, pz0, tr, tg, tb, u1, v0, mode);
      VoxelPush(vertices, &count, px1, y11, pz1, tr, tg, tb, u1, v1, mode);
      VoxelPush(vertices, &count, px0, y00, pz0, tr, tg, tb, u0, v0, mode);
      VoxelPush(vertices, &count, px1, y11, pz1, tr, tg, tb, u1, v1, mode);
      VoxelPush(vertices, &count, px0, y01, pz1, tr, tg, tb, u0, v1, mode);
      // All four sides, hemmed to each neighbor's shared-edge height so
      // slope ramps meet walls without slits; the north face matters when
      // follow cameras look from the south.
      float ns = VoxelNeighborEdgeH(cells, cols, rows, row + 1, col, 0, 1);
      float ne = VoxelNeighborEdgeH(cells, cols, rows, row, col + 1, 0, 2);
      float nw = VoxelNeighborEdgeH(cells, cols, rows, row, col - 1, 1, 3);
      float nn = VoxelNeighborEdgeH(cells, cols, rows, row - 1, col, 2, 3);
      if (y01 > ns + .0005f || y11 > ns + .0005f)
        VoxelQuad(vertices, &count, px0,ns,pz1, px1,ns,pz1, px1,y11,pz1, px0,y01,pz1,
                  r*.72f, g*.72f, b*.72f);
      if (y10 > ne + .0005f || y11 > ne + .0005f)
        VoxelQuad(vertices, &count, px1,ne,pz0, px1,ne,pz1, px1,y11,pz1, px1,y10,pz0,
                  r*.52f, g*.52f, b*.52f);
      if (y00 > nw + .0005f || y01 > nw + .0005f)
        VoxelQuad(vertices, &count, px0,nw,pz1, px0,nw,pz0, px0,y00,pz0, px0,y01,pz1,
                  r*.62f, g*.62f, b*.62f);
      if (y00 > nn + .0005f || y10 > nn + .0005f)
        VoxelQuad(vertices, &count, px1,nn,pz0, px0,nn,pz0, px0,y00,pz0, px1,y10,pz0,
                  r*.66f, g*.66f, b*.66f);
    }
  }

  // Actors: OAM sprite entries are clustered into connected groups (one
  // logical actor is several adjacent 8x8/16x16 entries), and each cluster
  // becomes one upright quad textured straight from the frame. The fragment
  // shader discards non-sprite pixels, so every actor is its exact 2D art
  // standing at its own depth, and separate actors no longer share a plane.
  // Shadows collect in their own buffer so they can draw after everything
  // else with depth writes off (a shadow must never occlude an actor).
  VoxelVertex shadow_verts[128 * 6];
  int shadow_count = 0;
  {
    Ppu *ppu = g_zenv.ppu;
    const int vis_h = height / rscale;  // 224, or 240 in extend_y mode
    int rx0[128], ry0[128], rx1[128], ry1[128], group[128];
    int n = 0;
    for (int i = 0; i < 128; i++) {
      int idx = i * 2;
      int yy = ppu->oam[idx] >> 8;
      if (yy == 0xf0)
        continue;  // hidden (zelda parks unused sprites at y=0xf0)
      int high = ppu->oam[0x100 + (idx >> 4)] >> (idx & 15);
      int size = (high >> 1 & 1) ? 16 : 8;
      int x = (ppu->oam[idx] & 0xff) + (high & 1) * 256;
      x -= (x >= 256 + extra_left) * 512;
      int sy = yy < vis_h ? yy : yy - 256;
      if (x + size <= -extra_left || x >= 256 + extra_left || sy + size <= 0 || sy >= vis_h)
        continue;
      rx0[n] = x, ry0[n] = sy, rx1[n] = x + size, ry1[n] = sy + size;
      group[n] = n, n++;
    }
    // Union overlapping/touching rects (within 2px) into clusters.
    for (int i = 0; i < n; i++) {
      for (int j = i + 1; j < n; j++) {
        if (rx0[i] < rx1[j] + 2 && rx0[j] < rx1[i] + 2 &&
            ry0[i] < ry1[j] + 2 && ry0[j] < ry1[i] + 2) {
          int gi = group[i], gj = group[j];
          if (gi != gj) {
            for (int k = 0; k < n; k++)
              if (group[k] == gj)
                group[k] = gi;
          }
        }
      }
    }
    for (int g = 0; g < n; g++) {
      if (group[g] != g)
        continue;  // not a cluster root
      int cx0 = rx0[g], cy0 = ry0[g], cx1 = rx1[g], cy1 = ry1[g];
      for (int i = 0; i < n; i++) {
        if (group[i] != g) continue;
        if (rx0[i] < cx0) cx0 = rx0[i];
        if (ry0[i] < cy0) cy0 = ry0[i];
        if (rx1[i] > cx1) cx1 = rx1[i];
        if (ry1[i] > cy1) cy1 = ry1[i];
      }
      // SNES coords -> frame pixels, clipped to the frame.
      int fx0 = (cx0 + extra_left) * rscale, fx1 = (cx1 + extra_left) * rscale;
      int fy0 = cy0 * rscale, fy1 = cy1 * rscale;
      if (fx0 < 0) fx0 = 0;
      if (fy0 < 0) fy0 = 0;
      if (fx1 > width) fx1 = width;
      if (fy1 > height) fy1 = height;
      if (fx0 >= fx1 || fy0 >= fy1)
        continue;
      // Skip clusters with no composited sprite pixels at all (entries fully
      // hidden behind BG tiles or pointing at transparent tiles) - otherwise
      // they'd cast phantom shadows with no actor above them.
      bool any_visible = false;
      for (int y = fy0; y < fy1 && !any_visible; y++) {
        const uint32 *line = (const uint32 *)(pixels + y * pitch);
        for (int x = fx0; x < fx1; x++) {
          uint32 layer = (line[x] >> 24) & kPpuPixelTag_LayerMask;
          if (line[x] >> 24 >= kPpuPixelTag_Valid &&
              (layer == kPpuPixelTag_Sprite || layer == kPpuPixelTag_SpriteNoMath)) {
            any_visible = true;
            break;
          }
        }
      }
      if (!any_visible)
        continue;
      // Ground height under the cluster's feet, from the terrain grid.
      int gcol = ((fx0 + fx1) / 2 - fx_start) / step;
      int grow = (fy1 - 1 - fy_start) / step;
      if (gcol < 0) gcol = 0;
      if (gcol >= cols) gcol = cols - 1;
      if (grow < 0) grow = 0;
      if (grow >= rows) grow = rows - 1;
      float cell_h = cells[grow * cols + gcol].height;
      float ground = cell_h >= 0.0f ? cell_h : 0.0f;
      float px0 = -1.0f + fx0 * pxw, px1 = -1.0f + fx1 * pxw;
      // Nudged forward so the quad never shares a plane with terrain faces;
      // the extra .37px keeps the offset non-commensurate with the cell grid
      // for every voxel size, so it can never land back on a face plane.
      float pz = -1.0f + fy1 * pxh + (step * .2f + .37f) * pxh;
      if (first_person) {
        float dxm = (px0 + px1) * .5f - pivot_x, dzm = pz - pivot_z;
        if (dxm * cull_sy + dzm * cull_cy > .35f ||
            dxm * dxm + dzm * dzm < .01f)
          continue;  // behind the camera, or Link's own billboard
      }
      float h = (fy1 - fy0) * pxw;  // square source pixels stay square upright
      float u0 = fx0 * tw, u1 = fx1 * tw;
      float v0 = fy0 * th, v1 = fy1 * th;
      // Translucent contact shadow on the ground at the actor's feet. The
      // uv corners span 0..1 so the shader can shape a soft radial falloff.
      float sy = ground + 0.004f;
      float z0 = pz - step * pxh * .6f, z1 = pz + step * pxh * .35f;
      float mx = (px1 - px0) * .08f;  // slight horizontal inset
      VoxelPush(shadow_verts, &shadow_count, px0 + mx, sy, z0, 0,0,0, 0,0, kVoxelTex_Shadow);
      VoxelPush(shadow_verts, &shadow_count, px1 - mx, sy, z0, 0,0,0, 1,0, kVoxelTex_Shadow);
      VoxelPush(shadow_verts, &shadow_count, px1 - mx, sy, z1, 0,0,0, 1,1, kVoxelTex_Shadow);
      VoxelPush(shadow_verts, &shadow_count, px0 + mx, sy, z0, 0,0,0, 0,0, kVoxelTex_Shadow);
      VoxelPush(shadow_verts, &shadow_count, px1 - mx, sy, z1, 0,0,0, 1,1, kVoxelTex_Shadow);
      VoxelPush(shadow_verts, &shadow_count, px0 + mx, sy, z1, 0,0,0, 0,1, kVoxelTex_Shadow);
      // Billboards counter-rotate by the chase yaw so they still face the
      // camera after the scene rotation in the vertex shader.
      float bx0 = px0, bx1 = px1, bz0 = pz, bz1 = pz;
      if (chase) {
        float cx = (px0 + px1) * .5f, hw = (px1 - px0) * .5f;
        float cyw = cosf(yaw), syw = sinf(yaw);
        bx0 = cx - hw * cyw, bz0 = pz + hw * syw;
        bx1 = cx + hw * cyw, bz1 = pz - hw * syw;
      }
      VoxelPush(vertices, &count, bx0, ground,     bz0, 0,0,0, u0, v1, kVoxelTex_Actor);
      VoxelPush(vertices, &count, bx1, ground,     bz1, 0,0,0, u1, v1, kVoxelTex_Actor);
      VoxelPush(vertices, &count, bx1, ground + h, bz1, 0,0,0, u1, v0, kVoxelTex_Actor);
      VoxelPush(vertices, &count, bx0, ground,     bz0, 0,0,0, u0, v1, kVoxelTex_Actor);
      VoxelPush(vertices, &count, bx1, ground + h, bz1, 0,0,0, u1, v0, kVoxelTex_Actor);
      VoxelPush(vertices, &count, bx0, ground + h, bz0, 0,0,0, u0, v0, kVoxelTex_Actor);
    }
  }
  // Shadows go last in the buffer; they draw with depth writes disabled.
  const int opaque_count = count;
  memcpy(vertices + count, shadow_verts, shadow_count * sizeof(*shadow_verts));
  count += shadow_count;

  // 3D rain: the storm overlay (0x9F) exists only as subscreen color math,
  // so recreate it as falling streaks in the scene. Depth testing lets
  // terrain and walls occlude them, and they share the translucent
  // depth-write-off pass with the shadows.
  if (world_ok && !player_is_indoors && (overlay_index & 0xff) == 0x9f) {
    // Drops anchor to 256px world tiles, so the storm stays fixed relative
    // to the terrain while scrolling and keeps constant density in every
    // camera mode.
    float cyw = cosf(yaw), syw = sinf(yaw);
    int wxa = wx0 + fx_start / rscale, wya = wy0 + fy_start / rscale;
    int wxb = wxa + cols * snes_step, wyb = wya + rows * snes_step;
    int emitted = 0;
    for (int ty = wya >> 8; ty <= (wyb - 1) >> 8 && emitted < 420; ty++) {
    for (int tx = wxa >> 8; tx <= (wxb - 1) >> 8 && emitted < 420; tx++) {
    for (int k = 0; k < 28 && emitted < 420; k++) {
      uint32 h1 = ((uint32)tx * 73856093u) ^ ((uint32)ty * 19349663u) ^
                  ((uint32)(k + 1) * 2654435761u);
      int wpx = (tx << 8) + (int)(h1 & 0xff);
      int wpy = (ty << 8) + (int)((h1 >> 8) & 0xff);
      if (wpx < wxa || wpx >= wxb || wpy < wya || wpy >= wyb)
        continue;
      emitted++;
      float rx = -1.0f + (wpx - wx0) * rscale * pxw;
      float rz = -1.0f + (wpy - wy0) * rscale * pxh;
      float phase = (float)((h1 >> 16) & 0x1ff) * (1.0f / 512.0f);
      float fall = fmodf((float)g_world_frame * .045f + phase * 1.13f, 1.13f);
      float ry = 1.0f - fall;
      if (ry < 0.0f)
        continue;
      // Ground height under this streak, for landing splashes and culling
      // the part of the fall that would be inside the terrain.
      int scol = ((int)((rx + 1.0f) * width * .5f) - fx_start) / step;
      int srow = ((int)((rz + 1.0f) * height * .5f) - fy_start) / step;
      if (scol < 0) scol = 0;
      if (scol >= cols) scol = cols - 1;
      if (srow < 0) srow = 0;
      if (srow >= rows) srow = rows - 1;
      float gh = cells[srow * cols + scol].height;
      if (gh < 0.0f) gh = 0.0f;
      float len = .085f, w = .0045f, slant = .02f;
      if (ry <= gh + .05f) {
        // Landing: a small bright fleck flashes on the ground.
        float s = .014f, sy2 = gh + .006f;
        VoxelPush(vertices, &count, rx - s, sy2, rz, .85f, .90f, 1.0f, 0, 0, kVoxelTex_Rain);
        VoxelPush(vertices, &count, rx, sy2, rz - s, .85f, .90f, 1.0f, 0, 0, kVoxelTex_Rain);
        VoxelPush(vertices, &count, rx + s, sy2, rz, .85f, .90f, 1.0f, 0, 0, kVoxelTex_Rain);
        VoxelPush(vertices, &count, rx - s, sy2, rz, .85f, .90f, 1.0f, 0, 0, kVoxelTex_Rain);
        VoxelPush(vertices, &count, rx + s, sy2, rz, .85f, .90f, 1.0f, 0, 0, kVoxelTex_Rain);
        VoxelPush(vertices, &count, rx, sy2, rz + s, .85f, .90f, 1.0f, 0, 0, kVoxelTex_Rain);
        if (ry + len < gh)
          continue;  // remaining streak is fully inside the terrain
      }
      float ox = w * cyw, oz = -w * syw;             // camera-facing width axis
      float sx2 = slant * cyw, sz2 = -slant * syw;   // wind slant, same axis
      VoxelPush(vertices, &count, rx - ox, ry, rz - oz, .72f, .80f, 1.0f, 0, 0, kVoxelTex_Rain);
      VoxelPush(vertices, &count, rx + ox, ry, rz + oz, .72f, .80f, 1.0f, 0, 0, kVoxelTex_Rain);
      VoxelPush(vertices, &count, rx + ox + sx2, ry + len, rz + oz + sz2, .72f, .80f, 1.0f, 0, 0, kVoxelTex_Rain);
      VoxelPush(vertices, &count, rx - ox, ry, rz - oz, .72f, .80f, 1.0f, 0, 0, kVoxelTex_Rain);
      VoxelPush(vertices, &count, rx + ox + sx2, ry + len, rz + oz + sz2, .72f, .80f, 1.0f, 0, 0, kVoxelTex_Rain);
      VoxelPush(vertices, &count, rx - ox + sx2, ry + len, rz - oz + sz2, .72f, .80f, 1.0f, 0, 0, kVoxelTex_Rain);
    }}}
  }

  glBindVertexArray(g_voxel_VAO);
  glBindBuffer(GL_ARRAY_BUFFER, g_voxel_VBO);
  glBufferData(GL_ARRAY_BUFFER, (size_t)count * sizeof(*vertices), vertices, GL_DYNAMIC_DRAW);
  glUseProgram(g_voxel_program);
  glBindTexture(GL_TEXTURE_2D, g_texture.gl_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, g_world_texture);
  glActiveTexture(GL_TEXTURE0);
  glUniform1i(glGetUniformLocation(g_voxel_program, "texture1"), 0);
  glUniform1i(glGetUniformLocation(g_voxel_program, "uWorldTex"), 1);
  glUniform1i(glGetUniformLocation(g_voxel_program, "uHudIsWorld"),
              g_config.voxelize_hud);
  glUniform1f(glGetUniformLocation(g_voxel_program, "uAspect"),
              (float)viewport_width / (float)viewport_height);
  // Camera profiles: diorama keeps the slider pitch and a neutral pivot;
  // chase reframes as a third-person over-the-shoulder view (low pitch,
  // pivot pulled close so Link stands large at the lower third with the
  // world running to the horizon); first person drops to eye level with the
  // pivot at the camera.
  glUniform1f(glGetUniformLocation(g_voxel_program, "uPitch"),
              first_person ? .20f :
              chase ? .40f : g_config.voxel_pitch * (3.14159265f / 180.0f));
  glUniform1f(glGetUniformLocation(g_voxel_program, "uZoom"),
              g_config.voxel_zoom * .01f);
  glUniform1f(glGetUniformLocation(g_voxel_program, "uYaw"), yaw);
  glUniform2f(glGetUniformLocation(g_voxel_program, "uPivot"), pivot_x, pivot_z);
  glUniform1f(glGetUniformLocation(g_voxel_program, "uDist"),
              first_person ? 2.35f : chase ? 1.35f : 0.0f);
  // Lightning: the rain overlay flips CGADSUB from half-add (0x72) to
  // full-add (0x32) on flash frames; brighten the whole diorama with it so
  // lightning illuminates the scene instead of just retinting the ground.
  glUniform1f(glGetUniformLocation(g_voxel_program, "uFlash"),
              CGADSUB_copy == 0x32 ? 1.35f : 1.0f);
  glViewport(viewport_x, viewport_y, viewport_width, viewport_height);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glDrawArrays(GL_TRIANGLES, 0, opaque_count);
  // Shadows: depth-tested against the scene but never writing depth, so a
  // raised shadow can't punch a hole through an actor standing behind it.
  glDepthMask(GL_FALSE);
  glDrawArrays(GL_TRIANGLES, opaque_count, count - opaque_count);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  free(vertices);
  free(cells);
}

static void OpenGLRenderer_DrawUiOverlay(int viewport_x, int viewport_y,
                                         int viewport_width, int viewport_height) {
  // Composite the visible BG3 pixels (HUD, dialog boxes, menus) flat over the
  // 3D scene, wherever they appear on screen. The tag filter lives in the
  // fragment shader, so no cutoff band or color keying is needed.
  glBindTexture(GL_TEXTURE_2D, g_texture.gl_texture);
  glUseProgram(g_hud_program);
  glBindVertexArray(g_VAO);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glViewport(viewport_x, viewport_y, viewport_width, viewport_height);

  // During dialogue, back the message with a translucent scrim spanning the
  // rows its BG3 pixels actually occupy, so box-less text (like the intro
  // telepathy) reads as an anchored subtitle bar instead of loose glyphs
  // floating over the diorama.
  if (main_module_index == 14 && submodule_index == 2 && g_draw_height <= 960) {
    uint8 row_has_ui[960];
    memset(row_has_ui, 0, g_draw_height);
    for (int y = 0; y < g_draw_height; y++) {
      const uint32 *line = (const uint32 *)(g_screen_buffer + (size_t)y * g_draw_width * 4);
      for (int x = 0; x < g_draw_width; x++) {
        uint32 tag = line[x] >> 24;
        if (tag >= kPpuPixelTag_Valid &&
            (tag & kPpuPixelTag_LayerMask) == kPpuPixelTag_Bg3) {
          row_has_ui[y] = 1;
          break;
        }
      }
    }
    // The message band is the contiguous UI row run (small gaps allowed)
    // that ends at the bottom-most UI row; walking up from there keeps the
    // HUD's own rows out of the band for bottom-positioned messages.
    int rscale = g_draw_height >= 400 ? 4 : 1;
    int last = -1;
    for (int y = 0; y < g_draw_height; y++)
      if (row_has_ui[y])
        last = y;
    if (last >= 0) {
      int first = last, gap = 0, tolerance = 8 * rscale;
      for (int y = last; y >= 0; y--) {
        if (row_has_ui[y])
          first = y, gap = 0;
        else if (++gap > tolerance)
          break;
      }
      int pad = 8 * rscale;
      first = IntMax(first - pad, 0);
      last = IntMin(last + pad + 1, g_draw_height);
      int sy = viewport_y + viewport_height - last * viewport_height / g_draw_height;
      int sh = (last - first) * viewport_height / g_draw_height;
      glUniform1i(glGetUniformLocation(g_hud_program, "uScrim"), 1);
      glUniform2f(glGetUniformLocation(g_hud_program, "uScrimBand"),
                  (float)first / g_draw_height, (float)last / g_draw_height);
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glEnable(GL_SCISSOR_TEST);
      glScissor(viewport_x, sy, viewport_width, sh);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      glDisable(GL_SCISSOR_TEST);
      glDisable(GL_BLEND);
    }
  }
  glUniform1i(glGetUniformLocation(g_hud_program, "uScrim"), 0);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void GL_APIENTRY MessageCallback(GLenum source,
                GLenum type,
                GLuint id,
                GLenum severity,
                GLsizei length,
                const GLchar *message,
                const void *userParam) {
  if (type == GL_DEBUG_TYPE_OTHER)
    return;

  fprintf(stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
          (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""),
          type, severity, message);
  if (type == GL_DEBUG_TYPE_ERROR)
    Die("OpenGL error!\n");
}

static bool OpenGLRenderer_Init(SDL_Window *window) {
  g_window = window;
  SDL_GLContext context = SDL_GL_CreateContext(window);
  (void)context;

  SDL_GL_SetSwapInterval(1);
  ogl_LoadFunctions();

  if (!g_opengl_es) {
    if (!ogl_IsVersionGEQ(3, 3))
      Die("You need OpenGL 3.3");
  } else {
    int majorVersion = 0, minorVersion = 0;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &majorVersion);
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minorVersion);
    if (majorVersion < 3)
      Die("You need OpenGL ES 3.0");

  }

  if (kDebugFlag) {
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(MessageCallback, 0);
  }

  glGenTextures(1, &g_texture.gl_texture);
  glGenTextures(1, &g_world_texture);

  static const GLchar *voxel_vs_core = "#version 330 core\n" CODE(
    layout(location = 0) in vec3 aPos;
    layout(location = 1) in vec3 aColor;
    layout(location = 2) in vec3 aTexData;
    out vec3 Color;
    out vec3 TexData;
    uniform float uAspect;
    uniform float uPitch;
    uniform float uZoom;
    uniform float uYaw;
    uniform vec2 uPivot;
    uniform float uDist;
    void main() {
      vec3 p = aPos;
      vec2 d = p.xz - uPivot;
      float cy = cos(uYaw), sy = sin(uYaw);
      p.x = d.x * cy - d.y * sy;
      p.z = d.x * sy + d.y * cy + uDist;
      float cp = cos(uPitch), sp = sin(uPitch);
      float y = p.y * cp - p.z * sp;
      float z = 3.15 - p.z * cp - p.y * sp;
      gl_Position = vec4(p.x * 2.1 * uZoom / uAspect, (y - 0.05) * 1.7 * uZoom,
                         z - 1.0, z);
      Color = aColor;
      TexData = aTexData;
    }
  );
  static const GLchar *voxel_vs_es = "#version 300 es\n" CODE(
    layout(location = 0) in vec3 aPos;
    layout(location = 1) in vec3 aColor;
    layout(location = 2) in vec3 aTexData;
    out vec3 Color;
    out vec3 TexData;
    uniform float uAspect;
    uniform float uPitch;
    uniform float uZoom;
    uniform float uYaw;
    uniform vec2 uPivot;
    uniform float uDist;
    void main() {
      vec3 p = aPos;
      vec2 d = p.xz - uPivot;
      float cy = cos(uYaw), sy = sin(uYaw);
      p.x = d.x * cy - d.y * sy;
      p.z = d.x * sy + d.y * cy + uDist;
      float cp = cos(uPitch), sp = sin(uPitch);
      float y = p.y * cp - p.z * sp;
      float z = 3.15 - p.z * cp - p.y * sp;
      gl_Position = vec4(p.x * 2.1 * uZoom / uAspect, (y - 0.05) * 1.7 * uZoom,
                         z - 1.0, z);
      Color = aColor;
      TexData = aTexData;
    }
  );
  // Mode 0 draws the flat vertex color. Mode 1 (terrain tops) shows the
  // frame's own pixels where they are world-tagged, falling back to the cell
  // color under actors/UI. Mode 2 (actor billboards) cuts sprites out of the
  // frame pixel-perfectly by discarding everything not sprite-tagged.
  static const GLchar *voxel_fs_core = "#version 330 core\n" CODE(
    in vec3 Color;
    in vec3 TexData;
    out vec4 FragColor;
    uniform sampler2D texture1;
    uniform sampler2D uWorldTex;
    uniform int uHudIsWorld;
    uniform float uFlash;
    void main() {
      if (TexData.z > 4.5) {
        FragColor = vec4(min(Color * uFlash, vec3(1.0)), 0.40);
        return;
      }
      if (TexData.z > 2.5 && TexData.z < 3.5) {
        float d = length(TexData.xy - vec2(0.5)) * 2.0;
        FragColor = vec4(0.0, 0.0, 0.0, 0.42 * smoothstep(1.0, 0.35, d));
        return;
      }
      vec3 col = Color;
      if (TexData.z > 3.5) {
        col = texture(uWorldTex, TexData.xy).rgb * Color;
      } else if (TexData.z > 0.5) {
        vec4 t = texture(texture1, TexData.xy);
        int tag = int(t.a * 255.0 + 0.5);
        int layer = tag & 15;
        bool sprite = tag >= 16 && (layer == 4 || layer == 6);
        if (TexData.z > 1.5) {
          if (!sprite) discard;
          col = t.rgb;
        } else if (tag >= 16 && !sprite && (layer != 2 || uHudIsWorld == 1)) {
          col = t.rgb;
        }
      }
      FragColor = vec4(min(col * uFlash, vec3(1.0)), 1.0);
    }
  );
  static const GLchar *voxel_fs_es = "#version 300 es\n" CODE(
    precision mediump float;
    in vec3 Color;
    in vec3 TexData;
    out vec4 FragColor;
    uniform sampler2D texture1;
    uniform sampler2D uWorldTex;
    uniform int uHudIsWorld;
    uniform float uFlash;
    void main() {
      if (TexData.z > 4.5) {
        FragColor = vec4(min(Color * uFlash, vec3(1.0)), 0.40);
        return;
      }
      if (TexData.z > 2.5 && TexData.z < 3.5) {
        float d = length(TexData.xy - vec2(0.5)) * 2.0;
        FragColor = vec4(0.0, 0.0, 0.0, 0.42 * smoothstep(1.0, 0.35, d));
        return;
      }
      vec3 col = Color;
      if (TexData.z > 3.5) {
        col = texture(uWorldTex, TexData.xy).rgb * Color;
      } else if (TexData.z > 0.5) {
        vec4 t = texture(texture1, TexData.xy);
        int tag = int(t.a * 255.0 + 0.5);
        int layer = tag & 15;
        bool sprite = tag >= 16 && (layer == 4 || layer == 6);
        if (TexData.z > 1.5) {
          if (!sprite) discard;
          col = t.rgb;
        } else if (tag >= 16 && !sprite && (layer != 2 || uHudIsWorld == 1)) {
          col = t.rgb;
        }
      }
      FragColor = vec4(min(col * uFlash, vec3(1.0)), 1.0);
    }
  );
  const GLchar *voxel_vs = g_opengl_es ? voxel_vs_es : voxel_vs_core;
  const GLchar *voxel_fs = g_opengl_es ? voxel_fs_es : voxel_fs_core;
  unsigned int voxel_vshader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(voxel_vshader, 1, &voxel_vs, NULL);
  glCompileShader(voxel_vshader);
  unsigned int voxel_fshader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(voxel_fshader, 1, &voxel_fs, NULL);
  glCompileShader(voxel_fshader);
  g_voxel_program = glCreateProgram();
  glAttachShader(g_voxel_program, voxel_vshader);
  glAttachShader(g_voxel_program, voxel_fshader);
  glLinkProgram(g_voxel_program);
  glGenVertexArrays(1, &g_voxel_VAO);
  glGenBuffers(1, &g_voxel_VBO);
  glBindVertexArray(g_voxel_VAO);
  glBindBuffer(GL_ARRAY_BUFFER, g_voxel_VBO);
  glBufferData(GL_ARRAY_BUFFER, 0, NULL, GL_DYNAMIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VoxelVertex), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(VoxelVertex), (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(VoxelVertex), (void *)(6 * sizeof(float)));
  glEnableVertexAttribArray(2);

  static const float kVertices[] = {
    // positions          // texture coords
    -1.0f,  1.0f, 0.0f,   0.0f, 0.0f, // top left
    -1.0f, -1.0f, 0.0f,   0.0f, 1.0f, // bottom left
     1.0f,  1.0f, 0.0f,   1.0f, 0.0f, // top right
     1.0f, -1.0f, 0.0f,   1.0f, 1.0f,  // bottom right
  };

  // create a vertex buffer object
  unsigned int vbo;
  glGenBuffers(1, &vbo);

  // vertex array object
  glGenVertexArrays(1, &g_VAO);
  // 1. bind Vertex Array Object
  glBindVertexArray(g_VAO);
  // 2. copy our vertices array in a buffer for OpenGL to use
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kVertices), kVertices, GL_STATIC_DRAW);
  // position attribute
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  // texture coord attribute
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  // vertex shader
  const GLchar *vs_code_core = "#version 330 core\n" CODE(
  layout(location = 0) in vec3 aPos;
  layout(location = 1) in vec2 aTexCoord;
  out vec2 TexCoord;
  void main() {
    gl_Position = vec4(aPos, 1.0);
    TexCoord = vec2(aTexCoord.x, aTexCoord.y);
  }
);

  const GLchar *vs_code_es = "#version 300 es\n" CODE(
  layout(location = 0) in vec3 aPos;
  layout(location = 1) in vec2 aTexCoord;
  out vec2 TexCoord;
  void main() {
    gl_Position = vec4(aPos, 1.0);
    TexCoord = vec2(aTexCoord.x, aTexCoord.y);
  }
);

  const GLchar *vs_code = g_opengl_es ? vs_code_es : vs_code_core;
  unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vs, 1, &vs_code, NULL);
  glCompileShader(vs);

  int success;
  char infolog[512];
  glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(vs, 512, NULL, infolog);
    printf("%s\n", infolog);
  }

  // fragment shader
  const GLchar *fs_code_core = "#version 330 core\n" CODE(
  out vec4 FragColor;
  in vec2 TexCoord;
  // texture samplers
  uniform sampler2D texture1;
  void main() {
    FragColor = texture(texture1, TexCoord);
  }
);

  const GLchar *fs_code_es = "#version 300 es\n" CODE(
  precision mediump float;
  out vec4 FragColor;
  in vec2 TexCoord;
  // texture samplers
  uniform sampler2D texture1;
  void main() {
    FragColor = texture(texture1, TexCoord);
  }
);


  const GLchar *fs_code = g_opengl_es ? fs_code_es : fs_code_core;
  unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fs, 1, &fs_code, NULL);
  glCompileShader(fs);

  glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(fs, 512, NULL, infolog);
    printf("%s\n", infolog);
  }

  // create program
  int program = g_program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  glGetProgramiv(program, GL_LINK_STATUS, &success);

  if (!success) {
    glGetProgramInfoLog(program, 512, NULL, infolog);
    printf("%s\n", infolog);
  }

  // Draw only the pixels the PPU tagged as BG3 (HUD, dialog boxes, menus)
  // over the voxel scene; everything else is discarded. The tag lives in the
  // alpha byte: 0x10 | layer, and Zelda draws all UI on layer 2 (BG3).
  const GLchar *hud_fs_core = "#version 330 core\n" CODE(
  out vec4 FragColor;
  in vec2 TexCoord;
  uniform sampler2D texture1;
  uniform int uScrim;
  uniform vec2 uScrimBand;
  void main() {
    if (uScrim == 1) {
      float t = (TexCoord.y - uScrimBand.x) / max(uScrimBand.y - uScrimBand.x, 1e-4);
      float a = 0.55 * smoothstep(0.0, 0.16, t) * (1.0 - smoothstep(0.84, 1.0, t));
      FragColor = vec4(0.0, 0.0, 0.0, a);
      return;
    }
    vec4 color = texture(texture1, TexCoord);
    int tag = int(color.a * 255.0 + 0.5);
    if (tag < 16 || (tag & 15) != 2) discard;
    FragColor = vec4(color.rgb, 1.0);
  }
  );
  const GLchar *hud_fs_es = "#version 300 es\n" CODE(
  precision mediump float;
  out vec4 FragColor;
  in vec2 TexCoord;
  uniform sampler2D texture1;
  uniform int uScrim;
  uniform vec2 uScrimBand;
  void main() {
    if (uScrim == 1) {
      float t = (TexCoord.y - uScrimBand.x) / max(uScrimBand.y - uScrimBand.x, 1e-4);
      float a = 0.55 * smoothstep(0.0, 0.16, t) * (1.0 - smoothstep(0.84, 1.0, t));
      FragColor = vec4(0.0, 0.0, 0.0, a);
      return;
    }
    vec4 color = texture(texture1, TexCoord);
    int tag = int(color.a * 255.0 + 0.5);
    if (tag < 16 || (tag & 15) != 2) discard;
    FragColor = vec4(color.rgb, 1.0);
  }
  );
  const GLchar *hud_fs_code = g_opengl_es ? hud_fs_es : hud_fs_core;
  unsigned int hud_fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(hud_fs, 1, &hud_fs_code, NULL);
  glCompileShader(hud_fs);
  glGetShaderiv(hud_fs, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(hud_fs, 512, NULL, infolog);
    printf("%s\n", infolog);
  }
  g_hud_program = glCreateProgram();
  glAttachShader(g_hud_program, vs);
  glAttachShader(g_hud_program, hud_fs);
  glLinkProgram(g_hud_program);
  glGetProgramiv(g_hud_program, GL_LINK_STATUS, &success);
  if (!success) {
    glGetProgramInfoLog(g_hud_program, 512, NULL, infolog);
    printf("%s\n", infolog);
  }

  if (g_config.shader)
    g_glsl_shader = GlslShader_CreateFromFile(g_config.shader, g_opengl_es);
  
  return true;
}

static void OpenGLRenderer_Destroy() {
}

static void OpenGLRenderer_BeginDraw(int width, int height, uint8 **pixels, int *pitch) {
  int size = width * height;

  if (size > g_screen_buffer_size) {
    g_screen_buffer_size = size;
    free(g_screen_buffer);
    g_screen_buffer = malloc(size * 4);
  }

  g_draw_width = width;
  g_draw_height = height;
  *pixels = g_screen_buffer;
  *pitch = width * 4;
}

static void OpenGLRenderer_EndDraw() {
  int drawable_width, drawable_height;

  SDL_GL_GetDrawableSize(g_window, &drawable_width, &drawable_height);
  
  int viewport_width = drawable_width, viewport_height = drawable_height;

  if (!g_config.ignore_aspect_ratio) {
    if (viewport_width * g_draw_height < viewport_height * g_draw_width)
      viewport_height = viewport_width * g_draw_height / g_draw_width;  // limit height
    else
      viewport_width = viewport_height * g_draw_width / g_draw_height;  // limit width
  }

  int viewport_x = (drawable_width - viewport_width) >> 1;
  int viewport_y = (drawable_height - viewport_height) >> 1;

  glBindTexture(GL_TEXTURE_2D, g_texture.gl_texture);
  if (g_draw_width == g_texture.width && g_draw_height == g_texture.height) {
    if (!g_opengl_es)
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_draw_width, g_draw_height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, g_screen_buffer);
    else
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_draw_width, g_draw_height, GL_BGRA, GL_UNSIGNED_BYTE, g_screen_buffer);
  } else {
    g_texture.width = g_draw_width;
    g_texture.height = g_draw_height;
    if (!g_opengl_es)
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_draw_width, g_draw_height, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, g_screen_buffer);
    else
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_draw_width, g_draw_height, 0, GL_BGRA, GL_UNSIGNED_BYTE, g_screen_buffer);
  }

  glClearColor(0.008f, 0.012f, 0.03f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  bool voxel_scene = main_module_index == 7 || main_module_index == 9 ||
                     main_module_index == 11 || main_module_index == 17;
  if (main_module_index == 14) {
    // Interface module: the item menu (1), dialogue text (2) and potion
    // refills (4/8/9) draw their UI on BG3 above the paused scene, so the 3D
    // world stays visible beneath them. Map, prayer, flute and save
    // submodules replace the scene entirely and fall back to flat 2D.
    bool scene_behind = saved_module_for_menu == 7 || saved_module_for_menu == 9 ||
                        saved_module_for_menu == 11;
    voxel_scene = scene_behind &&
                  (submodule_index == 1 || submodule_index == 2 ||
                   submodule_index == 4 || submodule_index == 8 ||
                   submodule_index == 9);
  }
  if (g_config.voxel_mode && voxel_scene && g_glsl_shader == NULL) {
    VoxelRenderer_Draw(g_draw_width, g_draw_height, g_screen_buffer, g_draw_width * 4,
                       viewport_x, viewport_y, viewport_width, viewport_height);
    if (!g_config.voxelize_hud)
      OpenGLRenderer_DrawUiOverlay(viewport_x, viewport_y, viewport_width, viewport_height);
    SDL_GL_SwapWindow(g_window);
    return;
  }

  // Any frame presented without the voxel scene (menus, loads, map screens)
  // re-arms the chase snap, so the camera never eases from a stale pose.
  g_chase_snap = true;

  if (g_glsl_shader == NULL) {
    glViewport(viewport_x, viewport_y, viewport_width, viewport_height);
    glUseProgram(g_program);
    int filter = g_config.linear_filtering ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glBindVertexArray(g_VAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  } else {
    GlslShader_Render(g_glsl_shader, &g_texture, viewport_x, viewport_y, viewport_width, viewport_height);
  }

  SDL_GL_SwapWindow(g_window);
}

static const struct RendererFuncs kOpenGLRendererFuncs = {
  &OpenGLRenderer_Init,
  &OpenGLRenderer_Destroy,
  &OpenGLRenderer_BeginDraw,
  &OpenGLRenderer_EndDraw,
};

void OpenGLRenderer_Create(struct RendererFuncs *funcs, bool use_opengl_es) {
  g_opengl_es = use_opengl_es;
  // Must be requested before SDL creates the OpenGL window/context.
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  if (!g_opengl_es) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  } else {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  }
  *funcs = kOpenGLRendererFuncs;
}

