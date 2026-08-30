#include "third_party/gl_core/gl_core_3_1.h"
#include <SDL.h>
#include <stdio.h>
#include <stdbool.h>
#include "types.h"
#include "util.h"
#include "glsl_shader.h"
#include "config.h"
#include "variables.h"
#include "snes/ppu.h"
#include "zelda_rtl.h"
#include "tile_detect.h"

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

// Per-fragment texturing modes, carried in the third uv component. The frame
// texture keeps its per-pixel layer tag in the alpha byte, so the fragment
// shader can show the real pixel art on terrain tops and cut actors out of
// the frame at native resolution, independent of the voxel block size.
enum {
  kVoxelTex_None = 0,     // flat vertex color (cube sides, floor)
  kVoxelTex_Terrain = 1,  // frame pixels; fall back to color on non-world tags
  kVoxelTex_Actor = 2,    // frame pixels; discard non-sprite tags
  kVoxelTex_Shadow = 3,   // translucent black contact shadow
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
// kVoxelTex_None to keep it flat-colored). Sides stay flat-shaded.
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
}

// Terrain data for one sampled block of the frame. height is the terrain
// column's extrusion; kCellUnknown marks ground hidden behind an actor or UI
// glyph (filled from neighbors), kCellVoid marks pits/borders with no ground.
#define kCellUnknown -1.0f
#define kCellVoid -2.0f
typedef struct VoxelCell {
  float r, g, b;  // terrain color (side shading + fill under actors/UI)
  float height;
} VoxelCell;

// Terrain profile from the game's tile attribute maps: walls rise, floors
// stay flat, water recedes, pits drop out. Luminance only adds gentle relief
// within a class, so busy tile art no longer produces corrugated ground.
static float VoxelAttrHeight(uint8 a, float lum, float hs) {
  if (a == 0x20)
    return kCellVoid;                                     // pit
  if (a == 0x08 || a == 0x0A)
    return .015f;                                         // deep water
  if (a == 0x09)
    return .03f;                                          // shallow water
  if ((a >= 0x01 && a <= 0x03) || (a >= 0x10 && a < 0x1C))
    return .045f + hs * (.16f + lum * .08f);               // walls, furniture, sloped corners
  if (a >= 0x28 && a <= 0x2F)
    return .04f + hs * .16f;                              // ledges
  if ((a >= 0x50 && a <= 0x57) || (a >= 0x70 && a <= 0x7F) || a == 0x66 || a == 0x67)
    return .035f + hs * (.075f + lum * .035f);             // furniture, bushes, rocks, pots, pegs
  if (a == 0x0D)
    return .04f + hs * .10f;                              // spikes
  return .035f + lum * hs * .08f;                         // walkable ground
}

static bool VoxelAttrIsSolid(uint8 a) {
  return (a >= 0x01 && a <= 0x03) || (a >= 0x10 && a < 0x1C) ||
         (a >= 0x50 && a <= 0x57) || (a >= 0x70 && a <= 0x7F) ||
         a == 0x66 || a == 0x67;
}

// Tile attribute at a world-pixel position, mirroring the game's own
// collision lookups (read-only; no game state is touched).
static uint8 VoxelTileAttrAt(int wx, int wy) {
  if (player_is_indoors)
    return dung_bg2_attr_table[((wx & 0x1f8) >> 3) + ((wy & 0x1f8) << 3)];
  return Overworld_GetTileAttributeAtLocation((uint16)(wx >> 3), (uint16)wy);
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
  const int wx0 = (int)(uint16)BG2HOFS_copy2 - extra_left;  // world x at frame x=0
  const int wy0 = (int)(uint16)BG2VOFS_copy2;               // world y at frame y=0
  int gx = wx0 % snes_step, gy = wy0 % snes_step;
  if (gx < 0) gx += snes_step;
  if (gy < 0) gy += snes_step;
  const int fx_start = -gx * rscale, fy_start = -gy * rscale;
  const int cols = (width - fx_start + step - 1) / step;
  const int rows = (height - fy_start + step - 1) / step;
  // Tile attributes describe the loaded scene; the upsampled mode7 path has
  // no attribute map, so it falls back to pure luminance heights.
  const bool use_attr = rscale == 1;

  VoxelCell *cells = malloc((size_t)cols * rows * sizeof(*cells));
  VoxelVertex *vertices = malloc(((size_t)cols * rows * 2 + 1) * 24 * sizeof(*vertices));
  if (!cells || !vertices) {
    free(cells), free(vertices);
    return;
  }
  const float height_scale = g_config.voxel_height * .01f;
  for (int row = 0; row < rows; row++) {
    for (int col = 0; col < cols; col++) {
      int fx0 = fx_start + col * step, fy0 = fy_start + row * step;
      int x0 = fx0 < 0 ? 0 : fx0, y0 = fy0 < 0 ? 0 : fy0;
      int x1 = fx0 + step < width ? fx0 + step : width;
      int y1 = fy0 + step < height ? fy0 + step : height;
      VoxelCell *c = &cells[row * cols + col];
      unsigned wr = 0, wg = 0, wb = 0, wn = 0;
      unsigned an = 0, un = 0;
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
          }
        }
      }
      // UI is painted flat after the voxel pass.  A cell that contains UI is
      // deliberately left empty, even if it also includes world pixels; this
      // avoids terrain geometry contaminating dialogue glyphs or borders.
      if (un) {
        c->r = c->g = c->b = 0.0f;
        c->height = kCellVoid;
      } else if (wn) {
        c->r = (float)wr / (wn * 255.0f);
        c->g = (float)wg / (wn * 255.0f);
        c->b = (float)wb / (wn * 255.0f);
        float luminance = c->r * .299f + c->g * .587f + c->b * .114f;
        if (luminance < .025f) {
          c->height = kCellVoid;
        } else if (use_attr) {
          uint8 attr = VoxelTileAttrAt(wx0 + (x0 + x1) / (2 * rscale),
                                       wy0 + (y0 + y1) / (2 * rscale));
          c->height = VoxelAttrHeight(attr, luminance, height_scale);
          // Dungeon furniture often shares the collision class used by
          // solid walls. Keep those interior tiles as shallow platforms;
          // the room perimeter remains tall and provides the diorama rim.
          if (player_is_indoors && VoxelAttrIsSolid(attr) &&
              x0 > 24 && x1 < width - 24 && y0 > 24 && y1 < height - 24)
            // A middle profile keeps beds and tables dimensional without
            // turning their shared solid collision tiles into tall pillars.
            c->height = .030f + height_scale * (.035f + luminance * .015f);
        } else {
          c->height = .05f + luminance * height_scale;
        }
      } else {
        c->r = c->g = c->b = 0.0f;
        // Actors need a reconstructed floor to stand on. UI is composited in
        // a later flat pass, so it should cut a clean hole instead of pulling
        // neighboring terrain into its letters and borders.
        c->height = an ? kCellUnknown : kCellVoid;
      }
    }
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
  // keeps the voxelized room from floating in empty space.
  VoxelCube(vertices, &count, -1.0f, -0.035f, -1.0f, 2.0f, .035f, 2.0f,
            .018f, .028f, .075f, 0, 0, 0, 0, kVoxelTex_None);
  // Terrain cubes tile seamlessly and their tops sample the frame's own
  // pixels, so the ground keeps its full pixel-art detail at any block size.
  const float pxw = 2.0f / width, pxh = 2.0f / height;  // world units per frame px
  const float tw = 1.0f / width, th = 1.0f / height;
  for (int row = 0; row < rows; row++) {
    for (int col = 0; col < cols; col++) {
      VoxelCell *c = &cells[row * cols + col];
      if (c->height < 0.0f)
        continue;
      int fx0 = fx_start + col * step, fy0 = fy_start + row * step;
      int x0 = fx0 < 0 ? 0 : fx0, y0 = fy0 < 0 ? 0 : fy0;
      int x1 = fx0 + step < width ? fx0 + step : width;
      int y1 = fy0 + step < height ? fy0 + step : height;
      if (x0 >= x1 || y0 >= y1)
        continue;
      VoxelCube(vertices, &count, -1.0f + x0 * pxw, 0.0f, -1.0f + y0 * pxh,
                (x1 - x0) * pxw, c->height, (y1 - y0) * pxh, c->r, c->g, c->b,
                x0 * tw, y0 * th, x1 * tw, y1 * th, kVoxelTex_Terrain);
    }
  }

  // Actors: OAM sprite entries are clustered into connected groups (one
  // logical actor is several adjacent 8x8/16x16 entries), and each cluster
  // becomes one upright quad textured straight from the frame. The fragment
  // shader discards non-sprite pixels, so every actor is its exact 2D art
  // standing at its own depth, and separate actors no longer share a plane.
  {
    Ppu *ppu = g_zenv.ppu;
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
      int sy = yy < 224 ? yy : yy - 256;
      if (x + size <= -extra_left || x >= 256 + extra_left || sy + size <= 0 || sy >= 224)
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
      // Nudged forward so the quad never shares a plane with terrain faces.
      float pz = -1.0f + fy1 * pxh + step * pxh * .2f;
      float h = (fy1 - fy0) * pxw;  // square source pixels stay square upright
      float u0 = fx0 * tw, u1 = fx1 * tw;
      float v0 = fy0 * th, v1 = fy1 * th;
      // Translucent contact shadow on the ground at the actor's feet.
      float sy = ground + 0.004f;
      float z0 = pz - step * pxh * .6f, z1 = pz + step * pxh * .35f;
      float mx = (px1 - px0) * .08f;  // slight horizontal inset
      VoxelPush(vertices, &count, px0 + mx, sy, z0, 0,0,0, 0,0, kVoxelTex_Shadow);
      VoxelPush(vertices, &count, px1 - mx, sy, z0, 0,0,0, 0,0, kVoxelTex_Shadow);
      VoxelPush(vertices, &count, px1 - mx, sy, z1, 0,0,0, 0,0, kVoxelTex_Shadow);
      VoxelPush(vertices, &count, px0 + mx, sy, z0, 0,0,0, 0,0, kVoxelTex_Shadow);
      VoxelPush(vertices, &count, px1 - mx, sy, z1, 0,0,0, 0,0, kVoxelTex_Shadow);
      VoxelPush(vertices, &count, px0 + mx, sy, z1, 0,0,0, 0,0, kVoxelTex_Shadow);
      VoxelPush(vertices, &count, px0, ground,     pz, 0,0,0, u0, v1, kVoxelTex_Actor);
      VoxelPush(vertices, &count, px1, ground,     pz, 0,0,0, u1, v1, kVoxelTex_Actor);
      VoxelPush(vertices, &count, px1, ground + h, pz, 0,0,0, u1, v0, kVoxelTex_Actor);
      VoxelPush(vertices, &count, px0, ground,     pz, 0,0,0, u0, v1, kVoxelTex_Actor);
      VoxelPush(vertices, &count, px1, ground + h, pz, 0,0,0, u1, v0, kVoxelTex_Actor);
      VoxelPush(vertices, &count, px0, ground + h, pz, 0,0,0, u0, v0, kVoxelTex_Actor);
    }
  }

  glBindVertexArray(g_voxel_VAO);
  glBindBuffer(GL_ARRAY_BUFFER, g_voxel_VBO);
  glBufferData(GL_ARRAY_BUFFER, (size_t)count * sizeof(*vertices), vertices, GL_DYNAMIC_DRAW);
  glUseProgram(g_voxel_program);
  glBindTexture(GL_TEXTURE_2D, g_texture.gl_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glUniform1i(glGetUniformLocation(g_voxel_program, "texture1"), 0);
  glUniform1i(glGetUniformLocation(g_voxel_program, "uHudIsWorld"),
              g_config.voxelize_hud);
  glUniform1f(glGetUniformLocation(g_voxel_program, "uAspect"),
              (float)viewport_width / (float)viewport_height);
  glUniform1f(glGetUniformLocation(g_voxel_program, "uPitch"),
              g_config.voxel_pitch * (3.14159265f / 180.0f));
  glUniform1f(glGetUniformLocation(g_voxel_program, "uZoom"),
              g_config.voxel_zoom * .01f);
  glViewport(viewport_x, viewport_y, viewport_width, viewport_height);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glDrawArrays(GL_TRIANGLES, 0, count);
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
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void OpenGLRenderer_DrawDialoguePanel(int viewport_x, int viewport_y,
                                             int viewport_width, int viewport_height) {
  // The dialogue border is split between several PPU backgrounds, while the
  // letters themselves are BG3.  Drawing BG3 alone leaves loose giant text
  // over the diorama.  Restore the entire native lower message area as one
  // flat panel, precisely aligned with the game's source frame.
  const int source_top = 128;
  int panel_height = viewport_height * (g_draw_height - source_top) / g_draw_height;
  if (panel_height <= 0)
    return;
  glBindTexture(GL_TEXTURE_2D, g_texture.gl_texture);
  glUseProgram(g_program);
  glBindVertexArray(g_VAO);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glViewport(viewport_x, viewport_y, viewport_width, viewport_height);
  glEnable(GL_SCISSOR_TEST);
  glScissor(viewport_x, viewport_y, viewport_width, panel_height);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisable(GL_SCISSOR_TEST);
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

  static const GLchar *voxel_vs_core = "#version 330 core\n" CODE(
    layout(location = 0) in vec3 aPos;
    layout(location = 1) in vec3 aColor;
    layout(location = 2) in vec3 aTexData;
    out vec3 Color;
    out vec3 TexData;
    uniform float uAspect;
    uniform float uPitch;
    uniform float uZoom;
    void main() {
      vec3 p = aPos;
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
    void main() {
      vec3 p = aPos;
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
    uniform int uHudIsWorld;
    void main() {
      if (TexData.z > 2.5) {
        FragColor = vec4(0.0, 0.0, 0.0, 0.34);
        return;
      }
      vec3 col = Color;
      if (TexData.z > 0.5) {
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
      FragColor = vec4(col, 1.0);
    }
  );
  static const GLchar *voxel_fs_es = "#version 300 es\n" CODE(
    precision mediump float;
    in vec3 Color;
    in vec3 TexData;
    out vec4 FragColor;
    uniform sampler2D texture1;
    uniform int uHudIsWorld;
    void main() {
      if (TexData.z > 2.5) {
        FragColor = vec4(0.0, 0.0, 0.0, 0.34);
        return;
      }
      vec3 col = Color;
      if (TexData.z > 0.5) {
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
      FragColor = vec4(col, 1.0);
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
  void main() {
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
  void main() {
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
    if (!g_config.voxelize_hud && main_module_index == 14 && submodule_index == 2)
      OpenGLRenderer_DrawDialoguePanel(viewport_x, viewport_y, viewport_width, viewport_height);
    if (!g_config.voxelize_hud)
      OpenGLRenderer_DrawUiOverlay(viewport_x, viewport_y, viewport_width, viewport_height);
    SDL_GL_SwapWindow(g_window);
    return;
  }

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

