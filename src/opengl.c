#include "third_party/gl_core/gl_core_3_1.h"
#include <SDL.h>
#include <stdio.h>
#include <stdbool.h>
#include "types.h"
#include "util.h"
#include "glsl_shader.h"
#include "config.h"
#include "variables.h"

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

typedef struct VoxelVertex {
  float x, y, z;
  float r, g, b;
} VoxelVertex;

static void VoxelPush(VoxelVertex *vertices, int *count, float x, float y, float z,
                      float r, float g, float b) {
  VoxelVertex *v = &vertices[(*count)++];
  v->x = x, v->y = y, v->z = z;
  v->r = r, v->g = g, v->b = b;
}

static void VoxelQuad(VoxelVertex *vertices, int *count,
                      float ax, float ay, float az, float bx, float by, float bz,
                      float cx, float cy, float cz, float dx, float dy, float dz,
                      float r, float g, float b) {
  VoxelPush(vertices, count, ax, ay, az, r, g, b);
  VoxelPush(vertices, count, bx, by, bz, r, g, b);
  VoxelPush(vertices, count, cx, cy, cz, r, g, b);
  VoxelPush(vertices, count, ax, ay, az, r, g, b);
  VoxelPush(vertices, count, cx, cy, cz, r, g, b);
  VoxelPush(vertices, count, dx, dy, dz, r, g, b);
}

static void VoxelCube(VoxelVertex *vertices, int *count, float x, float y, float z,
                      float sx, float sy, float sz, float r, float g, float b) {
  float x0 = x, x1 = x + sx, y0 = y, y1 = y + sy, z0 = z, z1 = z + sz;
  VoxelQuad(vertices, count, x0,y1,z0, x1,y1,z0, x1,y1,z1, x0,y1,z1, r,g,b);
  VoxelQuad(vertices, count, x0,y0,z1, x1,y0,z1, x1,y1,z1, x0,y1,z1, r*.72f,g*.72f,b*.72f);
  VoxelQuad(vertices, count, x1,y0,z0, x1,y0,z1, x1,y1,z1, x1,y1,z0, r*.52f,g*.52f,b*.52f);
  VoxelQuad(vertices, count, x0,y0,z1, x0,y0,z0, x0,y1,z0, x0,y1,z1, r*.62f,g*.62f,b*.62f);
}

static void VoxelRenderer_Draw(int width, int height, const uint8 *pixels,
                               int pitch, int viewport_x, int viewport_y,
                               int viewport_width, int viewport_height) {
  // This first slice deliberately voxelizes the rendered frame rather than
  // touching gameplay state. It gives us a safe 3D presentation prototype;
  // tile-aware terrain classification can replace this source later.
  const int step = g_config.voxel_size;
  const int hud_height = g_config.voxelize_hud ? 0 : g_config.voxel_hud_height * height / 224;
  const int cols = (width + step - 1) / step;
  const int rows = (height - hud_height + step - 1) / step;
  VoxelVertex *vertices = malloc(((size_t)cols * rows + 1) * 24 * sizeof(*vertices));
  if (!vertices) return;
  int count = 0;
  // A quiet, slightly raised floor gives the framebuffer slice a readable
  // silhouette and keeps the voxelized room from floating in empty space.
  VoxelCube(vertices, &count, -1.0f, -0.035f, -1.0f, 2.0f, .035f, 2.0f,
            .018f, .028f, .075f);
  for (int row = 0; row < rows; row++) {
    for (int col = 0; col < cols; col++) {
      int x0 = col * step, y0 = hud_height + row * step;
      int x1 = x0 + step < width ? x0 + step : width;
      int y1 = y0 + step < height ? y0 + step : height;
      unsigned red = 0, green = 0, blue = 0, samples = 0;
      for (int y = y0; y < y1; y++) {
        const uint32 *line = (const uint32 *)(pixels + y * pitch);
        for (int x = x0; x < x1; x++) {
          uint32 p = line[x];
          blue += p & 255;
          green += (p >> 8) & 255;
          red += (p >> 16) & 255;
          samples++;
        }
      }
      float r = (float)red / (samples * 255.0f);
      float g = (float)green / (samples * 255.0f);
      float b = (float)blue / (samples * 255.0f);
      float luminance = r * .299f + g * .587f + b * .114f;
      if (luminance < .025f) continue;
      float sx = 2.0f / cols, sz = 2.0f / rows;
      float px = -1.0f + col * sx, pz = -1.0f + row * sz;
      float height_scale = g_config.voxel_height * .01f;
      VoxelCube(vertices, &count, px, 0.0f, pz, sx * .98f, .05f + luminance * height_scale,
                sz * .98f, r, g, b);
    }
  }
  glBindVertexArray(g_voxel_VAO);
  glBindBuffer(GL_ARRAY_BUFFER, g_voxel_VBO);
  glBufferData(GL_ARRAY_BUFFER, (size_t)count * sizeof(*vertices), vertices, GL_DYNAMIC_DRAW);
  glUseProgram(g_voxel_program);
  glUniform1f(glGetUniformLocation(g_voxel_program, "uAspect"),
              (float)viewport_width / (float)viewport_height);
  glViewport(viewport_x, viewport_y, viewport_width, viewport_height);
  glEnable(GL_DEPTH_TEST);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glDrawArrays(GL_TRIANGLES, 0, count);
  glDisable(GL_DEPTH_TEST);
  free(vertices);
}

static void OpenGLRenderer_DrawHudElements(int viewport_x, int viewport_y,
                                           int viewport_width, int viewport_height) {
  if (g_config.voxelize_hud || g_config.voxel_hud_height == 0)
    return;
  int hud_pixels = g_config.voxel_hud_height * g_draw_height / 224;
  int scissor_height = viewport_height * hud_pixels / g_draw_height;
  glBindTexture(GL_TEXTURE_2D, g_texture.gl_texture);
  glUseProgram(g_program);
  glBindVertexArray(g_VAO);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glEnable(GL_SCISSOR_TEST);
  glScissor(viewport_x, viewport_y + viewport_height - scissor_height,
            viewport_width, scissor_height);
  glViewport(viewport_x, viewport_y, viewport_width, viewport_height);
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
    out vec3 Color;
    uniform float uAspect;
    void main() {
      vec3 p = aPos;
      float pitch = 0.68;
      float cp = cos(pitch), sp = sin(pitch);
      float y = p.y * cp - p.z * sp;
      float z = 3.15 - p.z * cp - p.y * sp;
      gl_Position = vec4(p.x * 2.1 / uAspect, (y - 0.05) * 1.7,
                         z - 1.0, z);
      Color = aColor;
    }
  );
  static const GLchar *voxel_vs_es = "#version 300 es\n" CODE(
    layout(location = 0) in vec3 aPos;
    layout(location = 1) in vec3 aColor;
    out vec3 Color;
    uniform float uAspect;
    void main() {
      vec3 p = aPos;
      float pitch = 0.68;
      float cp = cos(pitch), sp = sin(pitch);
      float y = p.y * cp - p.z * sp;
      float z = 3.15 - p.z * cp - p.y * sp;
      gl_Position = vec4(p.x * 2.1 / uAspect, (y - 0.05) * 1.7,
                         z - 1.0, z);
      Color = aColor;
    }
  );
  static const GLchar *voxel_fs_core = "#version 330 core\n" CODE(
    in vec3 Color; out vec4 FragColor; void main() { FragColor = vec4(Color, 1.0); }
  );
  static const GLchar *voxel_fs_es = "#version 300 es\n" CODE(
    precision mediump float; in vec3 Color; out vec4 FragColor; void main() { FragColor = vec4(Color, 1.0); }
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

  // Draw only the visible HUD glyphs over the voxel scene. The solid HUD
  // backdrop is sampled from the frame and discarded by this color key.
  const GLchar *hud_fs_core = "#version 330 core\n" CODE(
  out vec4 FragColor;
  in vec2 TexCoord;
  uniform sampler2D texture1;
  uniform float uHudCutoff;
  uniform vec3 uKeyColorA;
  uniform vec3 uKeyColorB;
  void main() {
    if (TexCoord.y > uHudCutoff) discard;
    vec4 color = texture(texture1, TexCoord);
    if (distance(color.rgb, uKeyColorA) < 0.06 || distance(color.rgb, uKeyColorB) < 0.06) discard;
    FragColor = color;
  }
  );
  const GLchar *hud_fs_es = "#version 300 es\n" CODE(
  precision mediump float;
  out vec4 FragColor;
  in vec2 TexCoord;
  uniform sampler2D texture1;
  uniform float uHudCutoff;
  uniform vec3 uKeyColorA;
  uniform vec3 uKeyColorB;
  void main() {
    if (TexCoord.y > uHudCutoff) discard;
    vec4 color = texture(texture1, TexCoord);
    if (distance(color.rgb, uKeyColorA) < 0.06 || distance(color.rgb, uKeyColorB) < 0.06) discard;
    FragColor = color;
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

  bool voxel_gameplay = main_module_index == 7 || main_module_index == 9 ||
                        main_module_index == 11 || main_module_index == 17;
  if (g_config.voxel_mode && voxel_gameplay && g_glsl_shader == NULL) {
    int hud_pixels = g_config.voxelize_hud ? 0 :
                     g_config.voxel_hud_height * g_draw_height / 224;
    int hud_viewport_height = viewport_height * hud_pixels / g_draw_height;
    int world_viewport_height = IntMax(1, viewport_height - hud_viewport_height);
    VoxelRenderer_Draw(g_draw_width, g_draw_height, g_screen_buffer, g_draw_width * 4,
                       viewport_x, viewport_y, viewport_width, world_viewport_height);
    OpenGLRenderer_DrawHudElements(viewport_x, viewport_y, viewport_width, viewport_height);
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

