// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//      Native OpenGL ES 2.0/3.0 backend of the DGL renderer (Android)
//
// The engine still speaks fixed-function OpenGL through the dgl/gl macros. On
// Android they end up here: matrices, the texture environment, alpha test and
// fog are CPU-side state, and every draw becomes one indexed draw call with a
// program compiled for exactly the state it needs.
//
// Two program families mirror what the desktop/GL4ES build runs:
//  - while the world shader is bound (I_ShaderBind) draws use a GLSL ES port of
//    it (N64 3-point or bilinear filter, sector combiner pass, color enhance,
//    fog), fed from the same state the desktop build uploads as uniforms;
//  - with no shader bound (I_ShaderUnBind) draws emulate the fixed-function
//    pipeline: the texture environment of every enabled unit, alpha test, fog.
//
// Written for tile-based GPUs (Mali, Adreno, PowerVR, ANGLE on Vulkan), see the
// Arm GPU best practices guide:
//  - no uniform branches: every program is compiled for its state with #defines,
//    discard only exists in programs for alpha-tested draws;
//  - uniforms are uploaded and programs switched only when something changed,
//    nothing is queried back from the driver while a frame is recorded;
//  - geometry streams through ring buffers written with unsynchronized maps: no
//    draw waits for the GPU, no buffer is copied and no render pass is split;
//  - every frame starts with a clear of color and depth on the whole surface and
//    ends with depth invalidated, so tile memory is never loaded from or stored
//    to memory for data the frame does not need;
//  - fragment shaders run in mediump, only texture coordinates are highp.
//
//-----------------------------------------------------------------------------

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "dgl_gles.h"

#ifndef GL_NUM_EXTENSIONS
#define GL_NUM_EXTENSIONS 0x821D
#endif
#ifndef GL_PRIMARY_COLOR
#define GL_PRIMARY_COLOR 0x8577
#endif
#ifndef GL_ADD_SIGNED
#define GL_ADD_SIGNED 0x8574
#endif
#ifndef GL_SUBTRACT
#define GL_SUBTRACT 0x84E7
#endif
#ifndef GL_DOT3_RGB
#define GL_DOT3_RGB 0x86AE
#endif
#ifndef GL_DOT3_RGBA
#define GL_DOT3_RGBA 0x86AF
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP 0x8191
#endif

#define GLES_UNITS              4
#define GLES_MATRIX_STACK_DEPTH 32
#define GLES_IMMEDIATE_VERTICES 4096
#define GLES_MAX_PROGRAMS       512

// Streaming ring buffers. Indices are 16-bit and get rebased while they are
// copied, so the vertex ring holds at most 65536 vertices.
#define GLES_RING_VERTICES      65536
#define GLES_RING_INDICES       (256 * 1024)

//
// Context and capabilities
//

static boolean g_active = false;
static int     g_level = 0;             // 2: OpenGL ES 2.0 feature level, 3: OpenGL ES 3.0
static boolean g_has_map_range = false; // ES 3.0 or GL_EXT_map_buffer_range + GL_OES_mapbuffer
static boolean g_has_invalidate = false;
static boolean g_has_discard = false;
static boolean g_has_aniso = false;
static GLint   g_max_fragment_units = 1;
static char*   g_extension_string = NULL;

//
// Program binary cache (OpenGL ES 3.0 feature level)
//

#define GLES_CACHE_MAGIC    0x50343644u     // "D64P"
#define GLES_CACHE_VERSION  1u
#define GLES_CACHE_MAX_SIZE (16u * 1024u * 1024u)
#define GLES_CACHE_FOLDER   "doom64_gles_shader_cache"

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t key;
    uint32_t format;
    uint32_t size;
} program_cache_header_t;

static char     g_cache_dir[1024];          // empty: no cache
static char     g_cache_generation[17];
static GLint*   g_binary_formats = NULL;
static GLint    g_binary_format_count = 0;
static unsigned g_cache_loaded = 0;
static unsigned g_cache_compiled = 0;
static int      g_cache_reported = 0;

//
// Textures
//

typedef struct {
    GLsizei width;
    GLsizei height;
    GLenum  format;     // base format of level 0 as uploaded
} texture_info_t;

static texture_info_t* g_texinfo = NULL;
static size_t          g_texinfo_size = 0;   // indexed by texture name
static GLuint          g_readback_framebuffer = 0;

typedef struct {
    boolean enabled;    // GL_TEXTURE_2D
    GLint   mode;       // GL_TEXTURE_ENV_MODE
    GLint   combineRGB;
    GLint   combineAlpha;
    GLint   srcRGB[3];
    GLint   opRGB[3];
    GLint   srcAlpha[3];
    GLint   opAlpha[3];
    GLfloat envColor[4];
    GLfloat rgbScale;
    GLfloat alphaScale;
} texunit_t;

static texunit_t g_unit[GLES_UNITS];
static GLuint    g_bound[GLES_UNITS];
static GLenum    g_active_texture = GL_TEXTURE0;
static int       g_active_unit = 0;

//
// Fixed-function and render state
//

static boolean   g_cap_blend, g_cap_depth_test, g_cap_cull, g_cap_scissor;
static boolean   g_cap_polygon_offset, g_cap_dither, g_cap_stencil;
static GLenum    g_blend_src = GL_ONE, g_blend_dst = GL_ZERO;
static GLboolean g_depth_mask = GL_TRUE;

static boolean g_alpha_test = false;
static GLenum  g_alpha_func = GL_ALWAYS;
static GLfloat g_alpha_ref = 0.0f;

static boolean g_fog = false;
static GLint   g_fog_mode = 0x0800; // GL_EXP
static GLfloat g_fog_start = 0.0f;
static GLfloat g_fog_end = 1.0f;
static GLfloat g_fog_density = 1.0f;
static GLfloat g_fog_color[4] = { 0, 0, 0, 0 };

static boolean g_wireframe = false;

//
// Matrices
//

static GLfloat  g_modelview[GLES_MATRIX_STACK_DEPTH][16];
static GLfloat  g_projection[GLES_MATRIX_STACK_DEPTH][16];
static int      g_modelview_top = 0;
static int      g_projection_top = 0;
static GLenum   g_matrix_mode = GL_MODELVIEW;
static unsigned g_matrix_serial = 1;    // changes with every matrix edit, never 0
static unsigned g_mvp_serial = 0;
static GLfloat  g_mvp[16];

//
// World shader state: what the desktop build uploads to its world shader
//

static boolean g_world_bound = false;
static boolean g_world_initialised = false;
static boolean g_world_3point = false;  // r_filter 0 when the shader was first bound

static struct {
    int     useTex;
    GLfloat texel[2];
    int     passCount;
    int     passMode;
    GLfloat passColor[3];
    GLfloat passFactor;
    int     fogEnabled;
    int     fogMode;
    GLfloat fogColor[3];
    GLfloat fogFactor;
    GLfloat fogStart;
    GLfloat fogEnd;
    GLfloat fogDensity;
} g_world;

static boolean g_tint_active = false;
static GLfloat g_tint_color[4];

//
// Immediate mode
//

static boolean  g_inside_begin = false;
static GLenum   g_immediate_mode = GL_TRIANGLES;
static vtx_t    g_immediate_vertices[GLES_IMMEDIATE_VERTICES];
static GLushort g_immediate_indices[GLES_IMMEDIATE_VERTICES * 6];
static GLsizei  g_immediate_vertex_count = 0;
static GLsizei  g_immediate_index_count = 0;
static GLfloat  g_current_color[4] = { 1, 1, 1, 1 };
static GLfloat  g_current_texcoord[2] = { 0, 0 };

static GLushort* g_wire_indices = NULL;
static size_t    g_wire_capacity = 0;

//
// Frames and streaming
//

static boolean g_frame_open = false;
static GLuint  g_vbo = 0;
static GLuint  g_ibo = 0;
static GLsizei g_ring_vertex = 0;
static GLsizei g_ring_index = 0;
static boolean g_ring_attribs = false;  // attribute pointers point into the ring

//
// Programs
//

enum {
    PROGRAM_WORLD = 1,
    PROGRAM_FIXED = 2,
    PROGRAM_TINT = 3
};

// World variant bits
#define WORLD_TEX_SHIFT   0     // 0 untextured, 1 bilinear, 2 N64 3-point
#define WORLD_PASS_SHIFT  2     // 0 none, 1 modulate, 2 add, 3 interpolate, 4 replace
#define WORLD_FOG_SHIFT   5     // 0 off, 1 linear, 2 exp, 3 exp2, 4 constant factor
#define WORLD_ALPHA_SHIFT 8     // 0 off, 1 + (func - GL_NEVER)

// Fixed-function variant bits
#define FIXED_FOG_SHIFT   0     // 0 off, 1 linear, 2 exp, 3 exp2
#define FIXED_ALPHA_SHIFT 3

// Per-unit fixed-function key
#define UNIT_ENABLED      1ull
#define UNIT_HAS_ALPHA    2ull
#define UNIT_MODE_SHIFT   2
#define UNIT_CRGB_SHIFT   5
#define UNIT_CA_SHIFT     8
#define UNIT_SRGB_SHIFT   11
#define UNIT_ORGB_SHIFT   20
#define UNIT_SA_SHIFT     26
#define UNIT_OA_SHIFT     35
#define UNIT_SCALE_SHIFT  38
#define UNIT_ASCALE_SHIFT 40

enum { MODE_MODULATE, MODE_REPLACE, MODE_DECAL, MODE_BLEND, MODE_ADD, MODE_COMBINE };
enum { COMB_REPLACE, COMB_MODULATE, COMB_ADD, COMB_ADD_SIGNED, COMB_INTERPOLATE,
       COMB_SUBTRACT, COMB_DOT3_RGB, COMB_DOT3_RGBA };
enum { SRC_TEXTURE, SRC_CONSTANT, SRC_PRIMARY, SRC_PREVIOUS, SRC_TEXTURE0 };

typedef struct {
    uint32_t kind;
    uint32_t variant;
    uint64_t unit[GLES_UNITS];
} program_key_t;

typedef struct {
    program_key_t key;
    GLuint   id;            // 0: could not be built, its draws are skipped
    GLint    uMVP;
    GLint    uMV;
    GLint    uTexel;
    GLint    uPassColor;
    GLint    uPassFactor;
    GLint    uFogColor;
    GLint    uFogParams;
    GLint    uAlphaRef;
    GLint    uColor;
    GLint    uEnvColor[GLES_UNITS];
    boolean  fresh;         // nothing uploaded yet
    unsigned matrixSerial;
    GLfloat  texel[2];
    GLfloat  passColor[3];
    GLfloat  passFactor;
    GLfloat  fogColor[3];
    GLfloat  fogParams[4];
    GLfloat  alphaRef;
    GLfloat  color[4];
    GLfloat  envColor[GLES_UNITS][4];
} program_t;

static program_t     g_programs[GLES_MAX_PROGRAMS];
static int           g_program_count = 0;
static program_t*    g_last_program = NULL;
static GLuint        g_used_program = 0;
static program_key_t g_world_key;
static program_key_t g_fixed_key;
static boolean       g_world_key_dirty = true;
static boolean       g_fixed_key_dirty = true;

//
// Helpers
//

static void I_GLES_MatrixIdentity(GLfloat* matrix) {
    memset(matrix, 0, 16 * sizeof(GLfloat));
    matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
}

static void I_GLES_MatrixMultiply(GLfloat* out, const GLfloat* a, const GLfloat* b) {
    GLfloat result[16];
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            result[column * 4 + row] =
                a[0 * 4 + row] * b[column * 4 + 0] +
                a[1 * 4 + row] * b[column * 4 + 1] +
                a[2 * 4 + row] * b[column * 4 + 2] +
                a[3 * 4 + row] * b[column * 4 + 3];
        }
    }
    memcpy(out, result, sizeof(result));
}

static void I_GLES_MatrixChanged(void) {
    if (++g_matrix_serial == 0)
        g_matrix_serial = 1;
}

static GLfloat* I_GLES_CurrentMatrix(void) {
    if (g_matrix_mode == GL_PROJECTION)
        return g_projection[g_projection_top];
    return g_modelview[g_modelview_top];
}

static void I_GLES_MatrixMultiplyCurrent(const GLfloat* matrix) {
    GLfloat* current = I_GLES_CurrentMatrix();
    I_GLES_MatrixMultiply(current, current, matrix);
    I_GLES_MatrixChanged();
}

static GLfloat I_GLES_Clamp01(GLfloat value) {
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

static unsigned char I_GLES_ToByte(GLfloat value) {
    return (unsigned char)(I_GLES_Clamp01(value) * 255.0f + 0.5f);
}

static texture_info_t* I_GLES_TextureInfo(GLuint name, boolean create) {
    if (!name || name > (1u << 24))
        return NULL;
    if (name >= g_texinfo_size) {
        if (!create)
            return NULL;
        size_t size = g_texinfo_size ? g_texinfo_size : 1024;
        while (size <= name)
            size *= 2;
        texture_info_t* grown = (texture_info_t*)realloc(g_texinfo, size * sizeof(texture_info_t));
        if (!grown)
            return NULL;
        memset(grown + g_texinfo_size, 0, (size - g_texinfo_size) * sizeof(texture_info_t));
        g_texinfo = grown;
        g_texinfo_size = size;
    }
    return &g_texinfo[name];
}

static boolean I_GLES_TextureHasAlpha(GLuint name) {
    const texture_info_t* info = I_GLES_TextureInfo(name, false);
    return !info || (info->format != GL_RGB && info->format != GL_LUMINANCE);
}

static void I_GLES_ResetUnit(texunit_t* unit) {
    memset(unit, 0, sizeof(*unit));
    unit->mode = GL_MODULATE;
    unit->combineRGB = GL_MODULATE;
    unit->combineAlpha = GL_MODULATE;
    unit->srcRGB[0] = unit->srcAlpha[0] = GL_TEXTURE;
    unit->srcRGB[1] = unit->srcAlpha[1] = GL_PREVIOUS;
    unit->srcRGB[2] = unit->srcAlpha[2] = GL_CONSTANT;
    unit->opRGB[0] = unit->opRGB[1] = GL_SRC_COLOR;
    unit->opRGB[2] = GL_SRC_ALPHA;
    unit->opAlpha[0] = unit->opAlpha[1] = unit->opAlpha[2] = GL_SRC_ALPHA;
    unit->rgbScale = 1.0f;
    unit->alphaScale = 1.0f;
}

static void I_GLES_FixedChanged(void) {
    g_fixed_key_dirty = true;
}

static void I_GLES_WorldChanged(void) {
    g_world_key_dirty = true;
}

//
// Shader sources
//

typedef struct {
    char   data[24576];
    size_t length;
    boolean overflow;
} source_t;

static void I_GLES_Append(source_t* source, const char* format, ...) {
    if (source->overflow)
        return;
    va_list args;
    va_start(args, format);
    int written = SDL_vsnprintf(source->data + source->length,
                                sizeof(source->data) - source->length, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= sizeof(source->data) - source->length) {
        source->overflow = true;
        return;
    }
    source->length += (size_t)written;
}

static const char* I_GLES_VertexPrefix(void) {
    return g_level >= 3
        ? "#version 300 es\n#define ATTRIBUTE in\n#define VARYING out\n"
        : "#version 100\n#define ATTRIBUTE attribute\n#define VARYING varying\n";
}

static const char* I_GLES_FragmentPrefix(void) {
    return g_level >= 3
        ? "#version 300 es\n#define VARYING in\n#define TEXTURE texture\n"
          "#define FRAGCOLOR fragColor\nout mediump vec4 fragColor;\n"
        : "#version 100\n#define VARYING varying\n#define TEXTURE texture2D\n"
          "#define FRAGCOLOR gl_FragColor\n";
}

static const char g_fragment_precision[] =
    "precision mediump float;\n"
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
    "#define HIGHP highp\n"
    "#else\n"
    "#define HIGHP mediump\n"
    "#endif\n";

// World shader: GLSL ES port of the desktop world shader (i_shaders.c).
static const char g_world_vertex[] =
    "precision highp float;\n"
    "ATTRIBUTE vec4 aPosition;\n"
    "ATTRIBUTE vec4 aColor;\n"
    "ATTRIBUTE vec2 aTexCoord;\n"
    "uniform mat4 uMVP;\n"
    "VARYING mediump vec4 vColor;\n"
    "VARYING vec2 vUV;\n"
    "#if defined(FOG_LINEAR) || defined(FOG_EXP) || defined(FOG_EXP2)\n"
    "uniform mat4 uMV;\n"
    "#endif\n"
    "#if defined(FOG_LINEAR)\n"
    "uniform vec4 uFogParams;\n"
    "VARYING mediump float vFog;\n"
    "#elif defined(FOG_EXP) || defined(FOG_EXP2)\n"
    "VARYING float vEyeDist;\n"
    "#endif\n"
    "void main() {\n"
    "    gl_Position = uMVP * aPosition;\n"
    "    vColor = aColor;\n"
    "    vUV = aTexCoord;\n"
    "#if defined(FOG_LINEAR)\n"
    // The linear fog factor is affine in the eye distance, so interpolating it is
    // exact; only the clamp is left to the fragment shader.
    "    vFog = (uFogParams.x - length((uMV * aPosition).xyz)) * uFogParams.y;\n"
    "#elif defined(FOG_EXP) || defined(FOG_EXP2)\n"
    "    vEyeDist = length((uMV * aPosition).xyz);\n"
    "#endif\n"
    "}\n";

static const char g_world_fragment[] =
    "VARYING mediump vec4 vColor;\n"
    "VARYING HIGHP vec2 vUV;\n"
    "#if TEXMODE > 0\n"
    "uniform sampler2D uTex0;\n"
    "#endif\n"
    "#if TEXMODE == 2\n"
    "uniform HIGHP vec2 uTexel;\n"
    "#endif\n"
    "#if PASS > 0\n"
    "uniform vec3 uPassColor;\n"
    "uniform float uPassFactor;\n"
    "#endif\n"
    "#if defined(FOG_LINEAR) || defined(FOG_EXP) || defined(FOG_EXP2) || defined(FOG_FACTOR)\n"
    "uniform vec3 uFogColor;\n"
    "#endif\n"
    "#if defined(FOG_LINEAR)\n"
    "VARYING mediump float vFog;\n"
    "#elif defined(FOG_EXP) || defined(FOG_EXP2)\n"
    "VARYING HIGHP float vEyeDist;\n"
    "uniform HIGHP vec4 uFogParams;\n"
    "#elif defined(FOG_FACTOR)\n"
    "uniform vec4 uFogParams;\n"
    "#endif\n"
    "#ifdef ALPHA_TEST\n"
    "uniform float uAlphaRef;\n"
    "#endif\n"
    "float luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }\n"
    "void main() {\n"
    "#if TEXMODE == 0\n"
    "    vec4 col = vColor;\n"
    "#elif TEXMODE == 1\n"
    "    vec4 col = TEXTURE(uTex0, vUV) * vColor;\n"
    "#else\n"
    // N64 3-point filter: three of the four nearest texels, the triangle the
    // sample falls into decides which.
    "    HIGHP vec2 texSize = 1.0 / uTexel;\n"
    "    HIGHP vec2 p = vUV * texSize - 0.5;\n"
    "    HIGHP vec2 ip = floor(p);\n"
    "    vec2 f = p - ip;\n"
    "    HIGHP vec2 baseUV = (ip + 0.5) / texSize;\n"
    "    vec4 c00 = TEXTURE(uTex0, baseUV);\n"
    "    vec4 c10 = TEXTURE(uTex0, baseUV + vec2(uTexel.x, 0.0));\n"
    "    vec4 c01 = TEXTURE(uTex0, baseUV + vec2(0.0, uTexel.y));\n"
    "    vec4 c11 = TEXTURE(uTex0, baseUV + uTexel);\n"
    "    vec4 triA = c00 + f.x * (c10 - c00) + f.y * (c01 - c00);\n"
    "    vec4 triB = c11 + (1.0 - f.x) * (c01 - c11) + (1.0 - f.y) * (c10 - c11);\n"
    "    vec4 col = mix(triA, triB, step(1.0, f.x + f.y)) * vColor;\n"
    "#endif\n"
    "    vec3 rgb = col.rgb;\n"
    "#if PASS == 1\n"
    "    rgb *= uPassColor;\n"
    "#elif PASS == 2\n"
    "    float amp = min(clamp(abs(uPassFactor), 0.0, 1.0) * 0.48,\n"
    "                    max(0.0, 1.0 - luminance(rgb)) + 1e-5);\n"
    "    rgb += uPassColor * amp;\n"
    "#elif PASS == 3\n"
    "    rgb = mix(rgb, uPassColor, clamp(uPassFactor, 0.0, 1.0));\n"
    "#elif PASS == 4\n"
    "    rgb = uPassColor;\n"
    "#endif\n"
    "    rgb = mix(vec3(luminance(rgb)), rgb, 1.02) * 1.08;\n"
    "    rgb = pow(max(rgb, 0.0), vec3(0.94));\n"
    "#if defined(FOG_LINEAR)\n"
    "    rgb = mix(uFogColor, rgb, clamp(vFog, 0.0, 1.0));\n"
    "#elif defined(FOG_EXP)\n"
    "    rgb = mix(uFogColor, rgb, clamp(exp(-uFogParams.z * vEyeDist), 0.0, 1.0));\n"
    "#elif defined(FOG_EXP2)\n"
    "    HIGHP float fogD = uFogParams.z * vEyeDist;\n"
    "    rgb = mix(uFogColor, rgb, clamp(exp(-(fogD * fogD)), 0.0, 1.0));\n"
    "#elif defined(FOG_FACTOR)\n"
    "    rgb = mix(uFogColor, rgb, clamp(1.0 - uFogParams.w, 0.0, 1.0));\n"
    "#endif\n"
    "    vec4 result = vec4(clamp(rgb, 0.0, 1.0), col.a);\n"
    "#ifdef ALPHA_TEST\n"
    "    if (!ALPHA_PASS(result.a, uAlphaRef)) discard;\n"
    "#endif\n"
    "    FRAGCOLOR = result;\n"
    "}\n";

static const char g_fixed_vertex[] =
    "precision highp float;\n"
    "ATTRIBUTE vec4 aPosition;\n"
    "ATTRIBUTE vec4 aColor;\n"
    "ATTRIBUTE vec2 aTexCoord;\n"
    "uniform mat4 uMVP;\n"
    "VARYING mediump vec4 vColor;\n"
    "VARYING vec2 vUV;\n"
    "#ifdef FOG\n"
    "uniform mat4 uMV;\n"
    "VARYING float vEyeDist;\n"
    "#endif\n"
    "void main() {\n"
    "    gl_Position = uMVP * aPosition;\n"
    "    vColor = aColor;\n"
    "    vUV = aTexCoord;\n"
    "#ifdef FOG\n"
    "    vEyeDist = length((uMV * aPosition).xyz);\n"
    "#endif\n"
    "}\n";

static const char g_tint_vertex[] =
    "precision highp float;\n"
    "ATTRIBUTE vec4 aPosition;\n"
    "uniform mat4 uMVP;\n"
    "void main() { gl_Position = uMVP * aPosition; }\n";

static const char g_tint_fragment[] =
    "uniform vec4 uColor;\n"
    "void main() { FRAGCOLOR = uColor; }\n";

static const char* I_GLES_AlphaPassMacro(uint32_t code) {
    switch (code) {
    case 1: return "#define ALPHA_PASS(a, r) false\n";
    case 2: return "#define ALPHA_PASS(a, r) ((a) < (r))\n";
    case 3: return "#define ALPHA_PASS(a, r) (abs((a) - (r)) < 0.002)\n";
    case 4: return "#define ALPHA_PASS(a, r) ((a) <= (r))\n";
    case 5: return "#define ALPHA_PASS(a, r) ((a) > (r))\n";
    case 6: return "#define ALPHA_PASS(a, r) (abs((a) - (r)) >= 0.002)\n";
    default: return "#define ALPHA_PASS(a, r) ((a) >= (r))\n";
    }
}

//
// Fixed-function fragment shader generator
//

static const char* I_GLES_SourceExpr(int source, int unit, char* buffer, size_t size) {
    switch (source) {
    case SRC_TEXTURE:  SDL_snprintf(buffer, size, "t%d", unit); break;
    case SRC_CONSTANT: SDL_snprintf(buffer, size, "uEnv%d", unit); break;
    case SRC_PRIMARY:  SDL_snprintf(buffer, size, "vColor"); break;
    case SRC_PREVIOUS: SDL_snprintf(buffer, size, "prev"); break;
    default:           SDL_snprintf(buffer, size, "t%d", source - SRC_TEXTURE0); break;
    }
    return buffer;
}

static void I_GLES_EmitArgRGB(source_t* out, int index, int source, int operand, int unit) {
    char src[16];
    I_GLES_SourceExpr(source, unit, src, sizeof(src));
    switch (operand) {
    case 1:  I_GLES_Append(out, "        vec3 r%d = vec3(1.0) - %s.rgb;\n", index, src); break;
    case 2:  I_GLES_Append(out, "        vec3 r%d = vec3(%s.a);\n", index, src); break;
    case 3:  I_GLES_Append(out, "        vec3 r%d = vec3(1.0 - %s.a);\n", index, src); break;
    default: I_GLES_Append(out, "        vec3 r%d = %s.rgb;\n", index, src); break;
    }
}

static void I_GLES_EmitArgAlpha(source_t* out, int index, int source, int operand, int unit) {
    char src[16];
    I_GLES_SourceExpr(source, unit, src, sizeof(src));
    if (operand)
        I_GLES_Append(out, "        float a%d = 1.0 - %s.a;\n", index, src);
    else
        I_GLES_Append(out, "        float a%d = %s.a;\n", index, src);
}

static int I_GLES_CombineArgs(int combine) {
    switch (combine) {
    case COMB_REPLACE:     return 1;
    case COMB_INTERPOLATE: return 3;
    default:               return 2;
    }
}

static void I_GLES_EmitUnit(source_t* out, int unit, uint64_t key) {
    const boolean hasAlpha = (key & UNIT_HAS_ALPHA) != 0;
    const int mode = (int)((key >> UNIT_MODE_SHIFT) & 7);

    I_GLES_Append(out, "    {\n");
    switch (mode) {
    case MODE_REPLACE:
        if (hasAlpha)
            I_GLES_Append(out, "        prev = t%d;\n", unit);
        else
            I_GLES_Append(out, "        prev = vec4(t%d.rgb, prev.a);\n", unit);
        break;
    case MODE_DECAL:
        if (hasAlpha)
            I_GLES_Append(out, "        prev = vec4(mix(prev.rgb, t%d.rgb, t%d.a), prev.a);\n", unit, unit);
        else
            I_GLES_Append(out, "        prev = vec4(t%d.rgb, prev.a);\n", unit);
        break;
    case MODE_BLEND:
        I_GLES_Append(out, "        prev = vec4(mix(prev.rgb, uEnv%d.rgb, t%d.rgb), prev.a * t%d.a);\n",
                      unit, unit, unit);
        break;
    case MODE_ADD:
        I_GLES_Append(out, "        prev = vec4(clamp(prev.rgb + t%d.rgb, 0.0, 1.0), prev.a * t%d.a);\n",
                      unit, unit);
        break;
    case MODE_COMBINE: {
        const int crgb = (int)((key >> UNIT_CRGB_SHIFT) & 7);
        const int ca = (int)((key >> UNIT_CA_SHIFT) & 7);
        static const char* scales[] = { "1.0", "2.0", "4.0", "1.0" };
        const char* rgbScale = scales[(key >> UNIT_SCALE_SHIFT) & 3];
        const char* alphaScale = scales[(key >> UNIT_ASCALE_SHIFT) & 3];
        const int rgbArgs = (crgb == COMB_DOT3_RGB || crgb == COMB_DOT3_RGBA) ? 2 : I_GLES_CombineArgs(crgb);
        const int alphaArgs = I_GLES_CombineArgs(ca);

        for (int i = 0; i < rgbArgs; ++i) {
            I_GLES_EmitArgRGB(out, i, (int)((key >> (UNIT_SRGB_SHIFT + 3 * i)) & 7),
                              (int)((key >> (UNIT_ORGB_SHIFT + 2 * i)) & 3), unit);
        }
        if (crgb != COMB_DOT3_RGBA) {
            for (int i = 0; i < alphaArgs; ++i) {
                I_GLES_EmitArgAlpha(out, i, (int)((key >> (UNIT_SA_SHIFT + 3 * i)) & 7),
                                    (int)((key >> (UNIT_OA_SHIFT + i)) & 1), unit);
            }
        }
        switch (crgb) {
        case COMB_REPLACE:     I_GLES_Append(out, "        vec3 rgb = r0;\n"); break;
        case COMB_ADD:         I_GLES_Append(out, "        vec3 rgb = r0 + r1;\n"); break;
        case COMB_ADD_SIGNED:  I_GLES_Append(out, "        vec3 rgb = r0 + r1 - vec3(0.5);\n"); break;
        case COMB_INTERPOLATE: I_GLES_Append(out, "        vec3 rgb = mix(r1, r0, r2);\n"); break;
        case COMB_SUBTRACT:    I_GLES_Append(out, "        vec3 rgb = r0 - r1;\n"); break;
        case COMB_DOT3_RGB:
        case COMB_DOT3_RGBA:
            I_GLES_Append(out, "        vec3 rgb = vec3(4.0 * dot(r0 - vec3(0.5), r1 - vec3(0.5)));\n");
            break;
        default:               I_GLES_Append(out, "        vec3 rgb = r0 * r1;\n"); break;
        }
        if (crgb == COMB_DOT3_RGBA) {
            I_GLES_Append(out, "        prev = clamp(vec4(rgb * %s, rgb.r), 0.0, 1.0);\n", rgbScale);
            break;
        }
        switch (ca) {
        case COMB_REPLACE:     I_GLES_Append(out, "        float alpha = a0;\n"); break;
        case COMB_ADD:         I_GLES_Append(out, "        float alpha = a0 + a1;\n"); break;
        case COMB_ADD_SIGNED:  I_GLES_Append(out, "        float alpha = a0 + a1 - 0.5;\n"); break;
        case COMB_INTERPOLATE: I_GLES_Append(out, "        float alpha = mix(a1, a0, a2);\n"); break;
        case COMB_SUBTRACT:    I_GLES_Append(out, "        float alpha = a0 - a1;\n"); break;
        default:               I_GLES_Append(out, "        float alpha = a0 * a1;\n"); break;
        }
        I_GLES_Append(out, "        prev = clamp(vec4(rgb * %s, alpha * %s), 0.0, 1.0);\n",
                      rgbScale, alphaScale);
        break;
    }
    default: // MODE_MODULATE
        if (hasAlpha)
            I_GLES_Append(out, "        prev *= t%d;\n", unit);
        else
            I_GLES_Append(out, "        prev = vec4(prev.rgb * t%d.rgb, prev.a);\n", unit);
        break;
    }
    I_GLES_Append(out, "    }\n");
}

static boolean I_GLES_UnitUsesEnv(uint64_t key) {
    const int mode = (int)((key >> UNIT_MODE_SHIFT) & 7);
    if (mode == MODE_BLEND)
        return true;
    if (mode != MODE_COMBINE)
        return false;
    for (int i = 0; i < 3; ++i) {
        if (((key >> (UNIT_SRGB_SHIFT + 3 * i)) & 7) == SRC_CONSTANT ||
            ((key >> (UNIT_SA_SHIFT + 3 * i)) & 7) == SRC_CONSTANT)
            return true;
    }
    return false;
}

static void I_GLES_BuildFixedFragment(const program_key_t* key, source_t* out) {
    boolean sampled[GLES_UNITS] = { false, false, false, false };
    const uint32_t fog = (key->variant >> FIXED_FOG_SHIFT) & 7;
    const uint32_t alpha = (key->variant >> FIXED_ALPHA_SHIFT) & 15;

    for (int u = 0; u < GLES_UNITS; ++u) {
        const uint64_t unit = key->unit[u];
        if (!(unit & UNIT_ENABLED))
            continue;
        sampled[u] = true;
        if (((unit >> UNIT_MODE_SHIFT) & 7) == MODE_COMBINE) {
            for (int i = 0; i < 3; ++i) {
                const int rgb = (int)((unit >> (UNIT_SRGB_SHIFT + 3 * i)) & 7);
                const int a = (int)((unit >> (UNIT_SA_SHIFT + 3 * i)) & 7);
                if (rgb >= SRC_TEXTURE0) sampled[rgb - SRC_TEXTURE0] = true;
                if (a >= SRC_TEXTURE0) sampled[a - SRC_TEXTURE0] = true;
            }
        }
    }

    I_GLES_Append(out, "%s", g_fragment_precision);
    if (fog) {
        I_GLES_Append(out, "#define FOG %u\n", fog);
    }
    if (alpha) {
        I_GLES_Append(out, "%s", I_GLES_AlphaPassMacro(alpha));
    }
    I_GLES_Append(out, "VARYING mediump vec4 vColor;\nVARYING HIGHP vec2 vUV;\n");
    for (int u = 0; u < GLES_UNITS; ++u) {
        if (sampled[u])
            I_GLES_Append(out, "uniform sampler2D uTex%d;\n", u);
        if ((key->unit[u] & UNIT_ENABLED) && I_GLES_UnitUsesEnv(key->unit[u]))
            I_GLES_Append(out, "uniform vec4 uEnv%d;\n", u);
    }
    if (fog) {
        I_GLES_Append(out, "VARYING HIGHP float vEyeDist;\nuniform vec3 uFogColor;\nuniform HIGHP vec4 uFogParams;\n");
    }
    if (alpha) {
        I_GLES_Append(out, "uniform float uAlphaRef;\n");
    }
    I_GLES_Append(out, "void main() {\n    vec4 prev = vColor;\n");
    for (int u = 0; u < GLES_UNITS; ++u) {
        if (sampled[u])
            I_GLES_Append(out, "    vec4 t%d = TEXTURE(uTex%d, vUV);\n", u, u);
    }
    for (int u = 0; u < GLES_UNITS; ++u) {
        if (key->unit[u] & UNIT_ENABLED)
            I_GLES_EmitUnit(out, u, key->unit[u]);
    }
    if (fog == 1) {
        I_GLES_Append(out, "    float fogF = (uFogParams.x - vEyeDist) * uFogParams.y;\n");
    } else if (fog == 2) {
        I_GLES_Append(out, "    float fogF = exp(-uFogParams.z * vEyeDist);\n");
    } else if (fog == 3) {
        I_GLES_Append(out, "    HIGHP float fogD = uFogParams.z * vEyeDist;\n    float fogF = exp(-(fogD * fogD));\n");
    }
    if (fog) {
        I_GLES_Append(out, "    prev.rgb = mix(uFogColor, prev.rgb, clamp(fogF, 0.0, 1.0));\n");
    }
    if (alpha) {
        I_GLES_Append(out, "    if (!ALPHA_PASS(prev.a, uAlphaRef)) discard;\n");
    }
    I_GLES_Append(out, "    FRAGCOLOR = prev;\n}\n");
}

//
// Program binary cache
//
// One file per program, <app cache>/doom64_gles_shader_cache/<generation>/<key>.bin,
// the key being a hash of the shader sources. The generation hashes the driver
// identification and the shader templates: after a driver or game update the
// binaries land in a new folder and the old folders are deleted at startup. The
// driver can still reject a binary, the program is then compiled from source and
// the file replaced. The folder comes from SDL (Context.getCacheDir()).
//

static uint64_t I_GLES_Hash(uint64_t hash, const void* data, size_t size) {
    const unsigned char* bytes = (const unsigned char*)data;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static uint64_t I_GLES_HashString(uint64_t hash, const char* string) {
    if (string)
        hash = I_GLES_Hash(hash, string, SDL_strlen(string));
    return I_GLES_Hash(hash, "", 1);
}

#define GLES_CACHE_MAX_ENTRIES 64

typedef struct {
    const char* keep;   // entry to keep, NULL: none
    int         count;
    char        names[GLES_CACHE_MAX_ENTRIES][64];
} cache_listing_t;

static SDL_EnumerationResult SDLCALL I_GLES_ListCacheEntry(void* userdata, const char* dirname,
                                                           const char* fname) {
    cache_listing_t* listing = (cache_listing_t*)userdata;
    (void)dirname;
    if (listing->keep && SDL_strcmp(fname, listing->keep) == 0)
        return SDL_ENUM_CONTINUE;
    if (listing->count >= GLES_CACHE_MAX_ENTRIES || SDL_strlen(fname) >= sizeof(listing->names[0]))
        return SDL_ENUM_CONTINUE;
    SDL_strlcpy(listing->names[listing->count++], fname, sizeof(listing->names[0]));
    return SDL_ENUM_CONTINUE;
}

static void I_GLES_RemoveCacheFolder(const char* path) {
    // Generation folders only hold program files, no subfolders.
    cache_listing_t listing;
    listing.keep = NULL;
    listing.count = 0;
    SDL_EnumerateDirectory(path, I_GLES_ListCacheEntry, &listing);
    for (int i = 0; i < listing.count; ++i) {
        char file[1100];
        SDL_snprintf(file, sizeof(file), "%s/%s", path, listing.names[i]);
        SDL_RemovePath(file);
    }
    SDL_RemovePath(path);
}

static void I_GLES_InitProgramCache(void) {
    g_cache_dir[0] = '\0';
    g_cache_loaded = g_cache_compiled = 0;
    g_cache_reported = 0;
    if (g_level < 3)
        return;

    GLint count = 0;
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &count);
    if (count <= 0) {
        SDL_Log("DGL GLES: the driver offers no program binary formats, shaders are not cached");
        return;
    }
    g_binary_formats = (GLint*)SDL_malloc((size_t)count * sizeof(GLint));
    if (!g_binary_formats)
        return;
    glGetIntegerv(GL_PROGRAM_BINARY_FORMATS, g_binary_formats);
    g_binary_format_count = count;

    const char* base = SDL_GetAndroidCachePath();
    if (!base || !*base) {
        SDL_Log("DGL GLES: no cache folder (%s), shaders are not cached", SDL_GetError());
        return;
    }

    const uint32_t version = GLES_CACHE_VERSION;
    uint64_t generation = I_GLES_HashString(14695981039346656037ull, "doom64-gles-program-cache");
    generation = I_GLES_Hash(generation, &version, sizeof(version));
    static const GLenum strings[] = { GL_VENDOR, GL_RENDERER, GL_VERSION, GL_SHADING_LANGUAGE_VERSION };
    for (size_t i = 0; i < SDL_arraysize(strings); ++i)
        generation = I_GLES_HashString(generation, (const char*)glGetString(strings[i]));
    generation = I_GLES_HashString(generation, I_GLES_VertexPrefix());
    generation = I_GLES_HashString(generation, I_GLES_FragmentPrefix());
    generation = I_GLES_HashString(generation, g_fragment_precision);
    generation = I_GLES_HashString(generation, g_world_vertex);
    generation = I_GLES_HashString(generation, g_world_fragment);
    generation = I_GLES_HashString(generation, g_fixed_vertex);
    generation = I_GLES_HashString(generation, g_tint_vertex);
    generation = I_GLES_HashString(generation, g_tint_fragment);
    SDL_snprintf(g_cache_generation, sizeof(g_cache_generation), "%016llx", (unsigned long long)generation);

    char root[1024];
    SDL_snprintf(root, sizeof(root), "%s/" GLES_CACHE_FOLDER, base);

    // Binaries of other drivers or game versions can never be loaded again.
    cache_listing_t stale;
    stale.keep = g_cache_generation;
    stale.count = 0;
    SDL_EnumerateDirectory(root, I_GLES_ListCacheEntry, &stale);
    for (int i = 0; i < stale.count; ++i) {
        char path[1100];
        SDL_snprintf(path, sizeof(path), "%s/%s", root, stale.names[i]);
        I_GLES_RemoveCacheFolder(path);
    }

    SDL_snprintf(g_cache_dir, sizeof(g_cache_dir), "%s/%s", root, g_cache_generation);
    if (!SDL_CreateDirectory(g_cache_dir)) {
        SDL_Log("DGL GLES: cannot create %s (%s), shaders are not cached", g_cache_dir, SDL_GetError());
        g_cache_dir[0] = '\0';
        return;
    }
    SDL_Log("DGL GLES: program binaries cached in %s", g_cache_dir);
}

static boolean I_GLES_BinaryFormatListed(GLenum format) {
    // Only formats the driver lists may be passed to glProgramBinary().
    for (GLint i = 0; i < g_binary_format_count; ++i) {
        if ((GLenum)g_binary_formats[i] == format)
            return true;
    }
    return false;
}

static GLuint I_GLES_LoadCachedProgram(uint64_t key, const char* path) {
    size_t size = 0;
    void* data = SDL_LoadFile(path, &size);
    if (!data)
        return 0;

    const program_cache_header_t* header = (const program_cache_header_t*)data;
    GLuint program = 0;
    if (size >= sizeof(*header) && header->magic == GLES_CACHE_MAGIC &&
        header->version == GLES_CACHE_VERSION && header->key == key &&
        header->size > 0 && header->size <= GLES_CACHE_MAX_SIZE &&
        size == sizeof(*header) + header->size && I_GLES_BinaryFormatListed(header->format)) {
        program = glCreateProgram();
        if (program) {
            glProgramBinary(program, header->format, (const unsigned char*)data + sizeof(*header),
                            (GLsizei)header->size);
            GLint linked = GL_FALSE;
            glGetProgramiv(program, GL_LINK_STATUS, &linked);
            if (linked != GL_TRUE) {
                glDeleteProgram(program);
                program = 0;
            }
        }
    }
    SDL_free(data);
    if (!program)
        SDL_RemovePath(path);
    return program;
}

static void I_GLES_SaveCachedProgram(GLuint program, uint64_t key, const char* path) {
    GLint length = 0;
    glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &length);
    if (length <= 0 || (uint32_t)length > GLES_CACHE_MAX_SIZE)
        return;

    unsigned char* buffer = (unsigned char*)SDL_malloc(sizeof(program_cache_header_t) + (size_t)length);
    if (!buffer)
        return;
    GLsizei written = 0;
    GLenum format = 0;
    glGetProgramBinary(program, length, &written, &format, buffer + sizeof(program_cache_header_t));
    if (written > 0 && written <= length) {
        program_cache_header_t header;
        header.magic = GLES_CACHE_MAGIC;
        header.version = GLES_CACHE_VERSION;
        header.key = key;
        header.format = (uint32_t)format;
        header.size = (uint32_t)written;
        memcpy(buffer, &header, sizeof(header));

        // A temporary file first, so an interrupted write never leaves a broken binary.
        char temporary[1100];
        SDL_snprintf(temporary, sizeof(temporary), "%s.tmp", path);
        if (!SDL_SaveFile(temporary, buffer, sizeof(header) + (size_t)written) ||
            !SDL_RenamePath(temporary, path)) {
            SDL_RemovePath(temporary);
        }
    }
    SDL_free(buffer);
}

//
// Program building
//

static GLuint I_GLES_CompileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    if (!shader)
        return 0;
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        GLsizei length = 0;
        glGetShaderInfoLog(shader, sizeof(log), &length, log);
        SDL_Log("DGL GLES: %s shader failed to compile:\n%.*s\n%s",
                type == GL_VERTEX_SHADER ? "vertex" : "fragment", (int)length, log, source);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint I_GLES_LinkProgram(const char* vertexSource, const char* fragmentSource) {
    const boolean cached = g_cache_dir[0] != '\0';
    uint64_t key = 0;
    char path[1100];
    if (cached) {
        key = I_GLES_HashString(14695981039346656037ull, vertexSource);
        key = I_GLES_HashString(key, fragmentSource);
        SDL_snprintf(path, sizeof(path), "%s/%016llx.bin", g_cache_dir, (unsigned long long)key);
        GLuint program = I_GLES_LoadCachedProgram(key, path);
        if (program) {
            ++g_cache_loaded;
            return program;
        }
    }

    GLuint vs = I_GLES_CompileShader(GL_VERTEX_SHADER, vertexSource);
    if (!vs)
        return 0;
    GLuint fs = I_GLES_CompileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!fs) {
        glDeleteShader(vs);
        return 0;
    }
    GLuint program = glCreateProgram();
    if (program) {
        glBindAttribLocation(program, 0, "aPosition");
        glBindAttribLocation(program, 1, "aColor");
        glBindAttribLocation(program, 2, "aTexCoord");
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        if (cached)
            glProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
        glLinkProgram(program);
        GLint ok = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[2048];
            GLsizei length = 0;
            glGetProgramInfoLog(program, sizeof(log), &length, log);
            SDL_Log("DGL GLES: program failed to link:\n%.*s", (int)length, log);
            glDeleteProgram(program);
            program = 0;
        } else {
            glDetachShader(program, vs);
            glDetachShader(program, fs);
            if (cached) {
                I_GLES_SaveCachedProgram(program, key, path);
                ++g_cache_compiled;
            }
        }
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

static void I_GLES_UseProgram(GLuint program) {
    if (g_used_program != program) {
        glUseProgram(program);
        g_used_program = program;
    }
}

static void I_GLES_BuildProgram(program_t* program) {
    source_t vertex;
    source_t fragment;
    vertex.length = fragment.length = 0;
    vertex.overflow = fragment.overflow = false;

    const program_key_t* key = &program->key;
    I_GLES_Append(&vertex, "%s", I_GLES_VertexPrefix());
    I_GLES_Append(&fragment, "%s", I_GLES_FragmentPrefix());

    if (key->kind == PROGRAM_WORLD) {
        static const char* fogDefines[] = { "", "#define FOG_LINEAR 1\n", "#define FOG_EXP 1\n",
                                            "#define FOG_EXP2 1\n", "#define FOG_FACTOR 1\n" };
        const uint32_t tex = (key->variant >> WORLD_TEX_SHIFT) & 3;
        const uint32_t pass = (key->variant >> WORLD_PASS_SHIFT) & 7;
        const uint32_t fog = (key->variant >> WORLD_FOG_SHIFT) & 7;
        const uint32_t alpha = (key->variant >> WORLD_ALPHA_SHIFT) & 15;
        const char* fogDefine = fog < 5 ? fogDefines[fog] : "";
        I_GLES_Append(&vertex, "%s%s", fogDefine, g_world_vertex);
        I_GLES_Append(&fragment, "%s#define TEXMODE %u\n#define PASS %u\n%s", g_fragment_precision,
                      tex, pass, fogDefine);
        if (alpha)
            I_GLES_Append(&fragment, "#define ALPHA_TEST 1\n%s", I_GLES_AlphaPassMacro(alpha));
        I_GLES_Append(&fragment, "%s", g_world_fragment);
    } else if (key->kind == PROGRAM_FIXED) {
        if ((key->variant >> FIXED_FOG_SHIFT) & 7)
            I_GLES_Append(&vertex, "#define FOG 1\n");
        I_GLES_Append(&vertex, "%s", g_fixed_vertex);
        I_GLES_BuildFixedFragment(key, &fragment);
    } else {
        I_GLES_Append(&vertex, "%s", g_tint_vertex);
        I_GLES_Append(&fragment, "%s%s", g_fragment_precision, g_tint_fragment);
    }

    program->id = 0;
    if (vertex.overflow || fragment.overflow) {
        SDL_Log("DGL GLES: shader source for program kind %u is too long", key->kind);
        return;
    }
    program->id = I_GLES_LinkProgram(vertex.data, fragment.data);
    if (!program->id)
        return;

    program->uMVP = glGetUniformLocation(program->id, "uMVP");
    program->uMV = glGetUniformLocation(program->id, "uMV");
    program->uTexel = glGetUniformLocation(program->id, "uTexel");
    program->uPassColor = glGetUniformLocation(program->id, "uPassColor");
    program->uPassFactor = glGetUniformLocation(program->id, "uPassFactor");
    program->uFogColor = glGetUniformLocation(program->id, "uFogColor");
    program->uFogParams = glGetUniformLocation(program->id, "uFogParams");
    program->uAlphaRef = glGetUniformLocation(program->id, "uAlphaRef");
    program->uColor = glGetUniformLocation(program->id, "uColor");

    I_GLES_UseProgram(program->id);
    for (int u = 0; u < GLES_UNITS; ++u) {
        char name[16];
        SDL_snprintf(name, sizeof(name), "uTex%d", u);
        GLint sampler = glGetUniformLocation(program->id, name);
        if (sampler >= 0)
            glUniform1i(sampler, u);
        SDL_snprintf(name, sizeof(name), "uEnv%d", u);
        program->uEnvColor[u] = glGetUniformLocation(program->id, name);
    }
    program->fresh = true;
}

static program_t* I_GLES_FindProgram(const program_key_t* key) {
    for (int i = 0; i < g_program_count; ++i) {
        if (memcmp(&g_programs[i].key, key, sizeof(*key)) == 0)
            return &g_programs[i];
    }
    if (g_program_count >= GLES_MAX_PROGRAMS) {
        static boolean reported = false;
        if (!reported) {
            SDL_Log("DGL GLES: more than %d shader variants, further states are not drawn",
                    GLES_MAX_PROGRAMS);
            reported = true;
        }
        return NULL;
    }
    program_t* program = &g_programs[g_program_count++];
    memset(program, 0, sizeof(*program));
    program->key = *key;
    I_GLES_BuildProgram(program);
    return program;
}

//
// Program keys
//

static uint32_t I_GLES_AlphaCode(void) {
    if (!g_alpha_test || g_alpha_func == GL_ALWAYS)
        return 0;
    if (g_alpha_func < GL_NEVER || g_alpha_func > GL_GEQUAL)
        return 0;
    return 1 + (uint32_t)(g_alpha_func - GL_NEVER);
}

static void I_GLES_UpdateWorldKey(void) {
    uint32_t tex = 1;
    if (g_world_3point) {
        if (!g_world.useTex)
            tex = 0;
        else if (g_world.texel[0] > 0.0f && g_world.texel[1] > 0.0f)
            tex = 2;
    }

    uint32_t pass = 0;
    if (g_world.passCount > 0) {
        switch (g_world.passMode) {
        case GL_MODULATE:    pass = 1; break;
        case GL_ADD:         pass = 2; break;
        case GL_INTERPOLATE: pass = 3; break;
        case GL_REPLACE:     pass = 4; break;
        default:             pass = 0; break;
        }
    }

    uint32_t fog = 0;
    if (g_world.fogEnabled) {
        switch (g_world.fogMode) {
        case 1:  fog = 1; break;
        case 2:  fog = 2; break;
        case 3:  fog = 3; break;
        default: fog = 4; break;
        }
    }

    memset(&g_world_key, 0, sizeof(g_world_key));
    g_world_key.kind = PROGRAM_WORLD;
    g_world_key.variant = (tex << WORLD_TEX_SHIFT) | (pass << WORLD_PASS_SHIFT) |
                          (fog << WORLD_FOG_SHIFT) | (I_GLES_AlphaCode() << WORLD_ALPHA_SHIFT);
    g_world_key_dirty = false;
}

static uint64_t I_GLES_ModeCode(GLint mode) {
    switch (mode) {
    case GL_REPLACE: return MODE_REPLACE;
    case GL_DECAL:   return MODE_DECAL;
    case GL_BLEND:   return MODE_BLEND;
    case GL_ADD:     return MODE_ADD;
    case GL_COMBINE: return MODE_COMBINE;
    default:         return MODE_MODULATE;
    }
}

static uint64_t I_GLES_CombineCode(GLint combine) {
    switch (combine) {
    case GL_REPLACE:     return COMB_REPLACE;
    case GL_ADD:         return COMB_ADD;
    case GL_ADD_SIGNED:  return COMB_ADD_SIGNED;
    case GL_INTERPOLATE: return COMB_INTERPOLATE;
    case GL_SUBTRACT:    return COMB_SUBTRACT;
    case GL_DOT3_RGB:    return COMB_DOT3_RGB;
    case GL_DOT3_RGBA:   return COMB_DOT3_RGBA;
    default:             return COMB_MODULATE;
    }
}

static uint64_t I_GLES_SourceCode(GLint source) {
    if (source >= (GLint)GL_TEXTURE0 && source < (GLint)GL_TEXTURE0 + GLES_UNITS)
        return SRC_TEXTURE0 + (uint64_t)(source - GL_TEXTURE0);
    switch (source) {
    case GL_TEXTURE:       return SRC_TEXTURE;
    case GL_CONSTANT:      return SRC_CONSTANT;
    case GL_PRIMARY_COLOR: return SRC_PRIMARY;
    default:               return SRC_PREVIOUS;
    }
}

static uint64_t I_GLES_OperandRGBCode(GLint operand) {
    switch (operand) {
    case GL_ONE_MINUS_SRC_COLOR: return 1;
    case GL_SRC_ALPHA:           return 2;
    case GL_ONE_MINUS_SRC_ALPHA: return 3;
    default:                     return 0;
    }
}

static uint64_t I_GLES_ScaleCode(GLfloat scale) {
    if (scale >= 3.0f) return 2;
    if (scale >= 1.5f) return 1;
    return 0;
}

static uint64_t I_GLES_UnitKey(int u) {
    const texunit_t* unit = &g_unit[u];
    if (!unit->enabled || !g_bound[u])
        return 0;

    uint64_t key = UNIT_ENABLED;
    if (I_GLES_TextureHasAlpha(g_bound[u]))
        key |= UNIT_HAS_ALPHA;
    const uint64_t mode = I_GLES_ModeCode(unit->mode);
    key |= mode << UNIT_MODE_SHIFT;
    if (mode == MODE_COMBINE) {
        key |= I_GLES_CombineCode(unit->combineRGB) << UNIT_CRGB_SHIFT;
        key |= I_GLES_CombineCode(unit->combineAlpha) << UNIT_CA_SHIFT;
        for (int i = 0; i < 3; ++i) {
            key |= I_GLES_SourceCode(unit->srcRGB[i]) << (UNIT_SRGB_SHIFT + 3 * i);
            key |= I_GLES_OperandRGBCode(unit->opRGB[i]) << (UNIT_ORGB_SHIFT + 2 * i);
            key |= I_GLES_SourceCode(unit->srcAlpha[i]) << (UNIT_SA_SHIFT + 3 * i);
            key |= (uint64_t)(unit->opAlpha[i] == GL_ONE_MINUS_SRC_ALPHA) << (UNIT_OA_SHIFT + i);
        }
        key |= I_GLES_ScaleCode(unit->rgbScale) << UNIT_SCALE_SHIFT;
        key |= I_GLES_ScaleCode(unit->alphaScale) << UNIT_ASCALE_SHIFT;
    }
    return key;
}

static void I_GLES_UpdateFixedKey(void) {
    uint32_t fog = 0;
    if (g_fog) {
        if (g_fog_mode == GL_LINEAR) fog = 1;
        else if (g_fog_mode == GL_EXP2) fog = 3;
        else fog = 2;
    }
    memset(&g_fixed_key, 0, sizeof(g_fixed_key));
    g_fixed_key.kind = PROGRAM_FIXED;
    g_fixed_key.variant = (fog << FIXED_FOG_SHIFT) | (I_GLES_AlphaCode() << FIXED_ALPHA_SHIFT);
    for (int u = 0; u < GLES_UNITS; ++u)
        g_fixed_key.unit[u] = I_GLES_UnitKey(u);
    g_fixed_key_dirty = false;
}

static program_t* I_GLES_SelectProgram(void) {
    program_key_t tintKey;
    const program_key_t* key;

    if (g_tint_active) {
        memset(&tintKey, 0, sizeof(tintKey));
        tintKey.kind = PROGRAM_TINT;
        key = &tintKey;
    } else if (g_world_bound) {
        if (g_world_key_dirty)
            I_GLES_UpdateWorldKey();
        key = &g_world_key;
    } else {
        if (g_fixed_key_dirty)
            I_GLES_UpdateFixedKey();
        key = &g_fixed_key;
    }

    program_t* program = g_last_program;
    if (!program || memcmp(&program->key, key, sizeof(*key)) != 0) {
        program = I_GLES_FindProgram(key);
        g_last_program = program;
    }
    if (!program || !program->id)
        return NULL;
    I_GLES_UseProgram(program->id);
    return program;
}

//
// Uniforms: uploaded only when the value differs from what the program holds
//

static boolean I_GLES_Differs(const GLfloat* cached, const GLfloat* value, int count) {
    return memcmp(cached, value, (size_t)count * sizeof(GLfloat)) != 0;
}

static void I_GLES_Uniform1f(GLint location, boolean fresh, GLfloat* cached, GLfloat value) {
    if (location >= 0 && (fresh || *cached != value)) {
        glUniform1f(location, value);
        *cached = value;
    }
}

static void I_GLES_Uniform2fv(GLint location, boolean fresh, GLfloat* cached, const GLfloat* value) {
    if (location >= 0 && (fresh || I_GLES_Differs(cached, value, 2))) {
        glUniform2fv(location, 1, value);
        memcpy(cached, value, 2 * sizeof(GLfloat));
    }
}

static void I_GLES_Uniform3fv(GLint location, boolean fresh, GLfloat* cached, const GLfloat* value) {
    if (location >= 0 && (fresh || I_GLES_Differs(cached, value, 3))) {
        glUniform3fv(location, 1, value);
        memcpy(cached, value, 3 * sizeof(GLfloat));
    }
}

static void I_GLES_Uniform4fv(GLint location, boolean fresh, GLfloat* cached, const GLfloat* value) {
    if (location >= 0 && (fresh || I_GLES_Differs(cached, value, 4))) {
        glUniform4fv(location, 1, value);
        memcpy(cached, value, 4 * sizeof(GLfloat));
    }
}

static void I_GLES_FogParams(GLfloat start, GLfloat end, GLfloat density, GLfloat factor,
                             GLfloat* params) {
    GLfloat range = end - start;
    if (range < 0.0001f)
        range = 0.0001f;
    params[0] = end;
    params[1] = 1.0f / range;
    params[2] = density;
    params[3] = factor;
}

static void I_GLES_ApplyUniforms(program_t* program) {
    const boolean fresh = program->fresh;

    if (program->uMVP >= 0 && (fresh || program->matrixSerial != g_matrix_serial)) {
        if (g_mvp_serial != g_matrix_serial) {
            I_GLES_MatrixMultiply(g_mvp, g_projection[g_projection_top], g_modelview[g_modelview_top]);
            g_mvp_serial = g_matrix_serial;
        }
        glUniformMatrix4fv(program->uMVP, 1, GL_FALSE, g_mvp);
        if (program->uMV >= 0)
            glUniformMatrix4fv(program->uMV, 1, GL_FALSE, g_modelview[g_modelview_top]);
        program->matrixSerial = g_matrix_serial;
    }

    if (program->key.kind == PROGRAM_WORLD) {
        GLfloat fogParams[4];
        I_GLES_Uniform2fv(program->uTexel, fresh, program->texel, g_world.texel);
        I_GLES_Uniform3fv(program->uPassColor, fresh, program->passColor, g_world.passColor);
        I_GLES_Uniform1f(program->uPassFactor, fresh, &program->passFactor, g_world.passFactor);
        I_GLES_Uniform3fv(program->uFogColor, fresh, program->fogColor, g_world.fogColor);
        if (program->uFogParams >= 0) {
            I_GLES_FogParams(g_world.fogStart, g_world.fogEnd, g_world.fogDensity,
                             g_world.fogFactor, fogParams);
            I_GLES_Uniform4fv(program->uFogParams, fresh, program->fogParams, fogParams);
        }
        I_GLES_Uniform1f(program->uAlphaRef, fresh, &program->alphaRef, g_alpha_ref);
    } else if (program->key.kind == PROGRAM_FIXED) {
        GLfloat fogParams[4];
        for (int u = 0; u < GLES_UNITS; ++u)
            I_GLES_Uniform4fv(program->uEnvColor[u], fresh, program->envColor[u], g_unit[u].envColor);
        I_GLES_Uniform3fv(program->uFogColor, fresh, program->fogColor, g_fog_color);
        if (program->uFogParams >= 0) {
            I_GLES_FogParams(g_fog_start, g_fog_end, g_fog_density, 0.0f, fogParams);
            I_GLES_Uniform4fv(program->uFogParams, fresh, program->fogParams, fogParams);
        }
        I_GLES_Uniform1f(program->uAlphaRef, fresh, &program->alphaRef, g_alpha_ref);
    } else {
        I_GLES_Uniform4fv(program->uColor, fresh, program->color, g_tint_color);
    }

    program->fresh = false;
}

//
// Frames
//

static void I_GLES_ClearWholeSurface(GLbitfield mask) {
    // A full clear of every attachment becomes the load operation of the render
    // pass: tile memory is initialized and nothing is read from memory.
    const boolean scissor = g_cap_scissor;
    const boolean restoreDepthMask = (mask & GL_DEPTH_BUFFER_BIT) && !g_depth_mask;
    if (scissor)
        glDisable(GL_SCISSOR_TEST);
    if (restoreDepthMask)
        glDepthMask(GL_TRUE);
    glClear(mask);
    if (restoreDepthMask)
        glDepthMask(GL_FALSE);
    if (scissor)
        glEnable(GL_SCISSOR_TEST);
}

static void I_GLES_OpenFrame(void) {
    if (g_frame_open)
        return;
    g_frame_open = true;
    I_GLES_ClearWholeSurface(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void I_GLES_Clear(GLbitfield mask) {
    if (!g_active) {
        glClear(mask);
        return;
    }
    if (!g_frame_open) {
        // The surface is undefined after a swap: clear all of it, whatever part the
        // engine asked for, instead of letting the GPU load the previous contents.
        g_frame_open = true;
        I_GLES_ClearWholeSurface(mask | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        return;
    }
    glClear(mask);
}

void I_GLES_EndFrame(void) {
    if (!g_active)
        return;
    if (g_cache_dir[0] && g_cache_reported != g_program_count) {
        // Only frames that needed new shader variants report.
        SDL_Log("DGL GLES: %d shader variants, %u loaded from the cache, %u compiled",
                g_program_count, g_cache_loaded, g_cache_compiled);
        g_cache_reported = g_program_count;
    }
    if (g_frame_open) {
        // Depth is never needed after the frame: do not write it back.
        const GLenum depth = GL_DEPTH;
        if (g_has_invalidate)
            glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, &depth);
        else if (g_has_discard)
            glDiscardFramebufferEXT(GL_FRAMEBUFFER, 1, &depth);
    }
    g_frame_open = false;
}

//
// Geometry streaming
//

static void I_GLES_SetRingAttributes(void) {
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ibo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vtx_t), (const void*)offsetof(vtx_t, x));
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(vtx_t), (const void*)offsetof(vtx_t, r));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(vtx_t), (const void*)offsetof(vtx_t, tu));
    g_ring_attribs = true;
}

static void* I_GLES_MapRange(GLenum target, GLintptr offset, GLsizeiptr size) {
    // Unsynchronized and without invalidate flags: the range was never used since
    // the last orphan, so neither the driver nor the GPU has anything to wait for
    // or to copy (invalidate flags make Mali drivers ghost the buffer).
    const GLbitfield access = GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT;
    if (g_level >= 3)
        return glMapBufferRange(target, offset, size, access);
    return glMapBufferRangeEXT(target, offset, size, access);
}

static void I_GLES_Unmap(GLenum target) {
    if (g_level >= 3)
        glUnmapBuffer(target);
    else
        glUnmapBufferOES(target);
}

static void I_GLES_DrawClientArrays(GLenum mode, const vtx_t* vertices, GLsizei indexCount,
                                    const GLushort* indices) {
    if (g_ring_attribs || g_vbo) {
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        g_ring_attribs = false;
    }
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vtx_t), &vertices->x);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(vtx_t), &vertices->r);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(vtx_t), &vertices->tu);
    glDrawElements(mode, indexCount, GL_UNSIGNED_SHORT, indices);
}

static void I_GLES_Submit(GLenum mode, GLsizei vertexCount, const vtx_t* vertices,
                          GLsizei indexCount, const GLushort* indices) {
    if (!g_has_map_range || !g_vbo || vertexCount > GLES_RING_VERTICES ||
        indexCount > GLES_RING_INDICES) {
        I_GLES_DrawClientArrays(mode, vertices, indexCount, indices);
        return;
    }

    if (!g_ring_attribs)
        I_GLES_SetRingAttributes();

    if (g_ring_vertex + vertexCount > GLES_RING_VERTICES ||
        g_ring_index + indexCount > GLES_RING_INDICES) {
        // Orphan both rings: draws in flight keep the old storage, new draws get
        // fresh storage without waiting for the GPU.
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)GLES_RING_VERTICES * sizeof(vtx_t), NULL, GL_STREAM_DRAW);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)GLES_RING_INDICES * sizeof(GLushort), NULL,
                     GL_STREAM_DRAW);
        g_ring_vertex = 0;
        g_ring_index = 0;
    }

    void* vertexData = I_GLES_MapRange(GL_ARRAY_BUFFER, (GLintptr)g_ring_vertex * sizeof(vtx_t),
                                       (GLsizeiptr)vertexCount * sizeof(vtx_t));
    if (!vertexData) {
        I_GLES_DrawClientArrays(mode, vertices, indexCount, indices);
        return;
    }
    memcpy(vertexData, vertices, (size_t)vertexCount * sizeof(vtx_t));
    I_GLES_Unmap(GL_ARRAY_BUFFER);

    GLushort* indexData = (GLushort*)I_GLES_MapRange(GL_ELEMENT_ARRAY_BUFFER,
                                                     (GLintptr)g_ring_index * sizeof(GLushort),
                                                     (GLsizeiptr)indexCount * sizeof(GLushort));
    if (!indexData) {
        g_ring_vertex += vertexCount;
        I_GLES_DrawClientArrays(mode, vertices, indexCount, indices);
        return;
    }
    // Rebase the indices onto the ring position: the attribute pointers stay
    // where they are for the whole lifetime of the buffer.
    const GLushort base = (GLushort)g_ring_vertex;
    for (GLsizei i = 0; i < indexCount; ++i)
        indexData[i] = (GLushort)(indices[i] + base);
    I_GLES_Unmap(GL_ELEMENT_ARRAY_BUFFER);

    const void* offset = (const void*)((uintptr_t)g_ring_index * sizeof(GLushort));
    if (g_level >= 3) {
        // The range spares the driver a scan of the indices.
        glDrawRangeElements(mode, base, (GLuint)base + (GLuint)vertexCount - 1, indexCount,
                            GL_UNSIGNED_SHORT, offset);
    } else {
        glDrawElements(mode, indexCount, GL_UNSIGNED_SHORT, offset);
    }
    g_ring_vertex += vertexCount;
    g_ring_index += indexCount;
}

static void I_GLES_DrawBatch(GLenum mode, GLsizei vertexCount, const vtx_t* vertices,
                             GLsizei indexCount, const GLushort* indices) {
    if (!g_active || vertexCount <= 0 || indexCount <= 0 || !vertices || !indices)
        return;

    I_GLES_OpenFrame();

    program_t* program = I_GLES_SelectProgram();
    if (!program)
        return;
    I_GLES_ApplyUniforms(program);

    if (g_wireframe && mode == GL_TRIANGLES) {
        const size_t needed = (size_t)indexCount * 2;
        if (needed > g_wire_capacity) {
            GLushort* grown = (GLushort*)SDL_realloc(g_wire_indices, needed * sizeof(GLushort));
            if (!grown)
                return;
            g_wire_indices = grown;
            g_wire_capacity = needed;
        }
        GLsizei out = 0;
        for (GLsizei i = 0; i + 2 < indexCount; i += 3) {
            const GLushort a = indices[i], b = indices[i + 1], c = indices[i + 2];
            g_wire_indices[out++] = a; g_wire_indices[out++] = b;
            g_wire_indices[out++] = b; g_wire_indices[out++] = c;
            g_wire_indices[out++] = c; g_wire_indices[out++] = a;
        }
        I_GLES_Submit(GL_LINES, vertexCount, vertices, out, g_wire_indices);
        return;
    }

    I_GLES_Submit(mode, vertexCount, vertices, indexCount, indices);
}

void I_GLES_DrawIndexedGeometry(GLsizei vertexCount, const vtx_t* vertices,
                                GLsizei indexCount, const GLushort* indices) {
    I_GLES_DrawBatch(GL_TRIANGLES, vertexCount, vertices, indexCount, indices);
}

//
// Context
//

boolean I_GLES_IsActive(void) {
    return g_active;
}

int I_GLES_FeatureLevel(void) {
    return g_active ? g_level : 0;
}

const GLubyte* I_GLES_GetString(GLenum name) {
    if (name != GL_EXTENSIONS)
        return glGetString(name);

    if (g_level < 3) {
        // The extension dump command edits the returned list in place: hand out a copy.
        const GLubyte* raw = glGetString(name);
        if (!raw)
            return raw;
        size_t required = SDL_strlen((const char*)raw) + 1;
        char* resized = (char*)SDL_realloc(g_extension_string, required);
        if (!resized)
            return (const GLubyte*)"";
        g_extension_string = resized;
        SDL_memcpy(g_extension_string, raw, required);
        return (const GLubyte*)g_extension_string;
    }

    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    if (count < 0)
        count = 0;
    size_t required = 1;
    for (GLint i = 0; i < count; ++i) {
        const GLubyte* extension = glGetStringi(GL_EXTENSIONS, (GLuint)i);
        if (extension)
            required += SDL_strlen((const char*)extension) + 1;
    }
    char* resized = (char*)SDL_realloc(g_extension_string, required);
    if (!resized)
        return (const GLubyte*)"";
    g_extension_string = resized;
    g_extension_string[0] = '\0';
    for (GLint i = 0; i < count; ++i) {
        const GLubyte* extension = glGetStringi(GL_EXTENSIONS, (GLuint)i);
        if (!extension)
            continue;
        if (g_extension_string[0])
            SDL_strlcat(g_extension_string, " ", required);
        SDL_strlcat(g_extension_string, (const char*)extension, required);
    }
    return (const GLubyte*)g_extension_string;
}

static void I_GLES_ResetState(void) {
    I_GLES_MatrixIdentity(g_modelview[0]);
    I_GLES_MatrixIdentity(g_projection[0]);
    g_modelview_top = g_projection_top = 0;
    g_matrix_mode = GL_MODELVIEW;
    I_GLES_MatrixChanged();
    g_mvp_serial = 0;

    for (int u = 0; u < GLES_UNITS; ++u) {
        I_GLES_ResetUnit(&g_unit[u]);
        g_bound[u] = 0;
    }
    g_active_texture = GL_TEXTURE0;
    g_active_unit = 0;

    // OpenGL ES defaults of a new context
    g_cap_blend = g_cap_depth_test = g_cap_cull = g_cap_scissor = false;
    g_cap_polygon_offset = g_cap_stencil = false;
    g_cap_dither = true;
    g_blend_src = GL_ONE;
    g_blend_dst = GL_ZERO;
    g_depth_mask = GL_TRUE;

    g_alpha_test = false;
    g_alpha_func = GL_ALWAYS;
    g_alpha_ref = 0.0f;
    g_fog = false;
    g_fog_mode = 0x0800;
    g_fog_start = 0.0f;
    g_fog_end = 1.0f;
    g_fog_density = 1.0f;
    memset(g_fog_color, 0, sizeof(g_fog_color));
    g_wireframe = false;

    g_world_bound = false;
    g_world_initialised = false;
    g_world_3point = false;
    memset(&g_world, 0, sizeof(g_world));
    g_world.useTex = 1;
    g_tint_active = false;

    g_inside_begin = false;
    g_immediate_vertex_count = g_immediate_index_count = 0;
    g_current_color[0] = g_current_color[1] = g_current_color[2] = g_current_color[3] = 1.0f;
    g_current_texcoord[0] = g_current_texcoord[1] = 0.0f;

    g_frame_open = false;
    g_ring_vertex = g_ring_index = 0;
    g_ring_attribs = false;
    g_program_count = 0;
    g_last_program = NULL;
    g_used_program = 0;
    g_world_key_dirty = g_fixed_key_dirty = true;
}

boolean I_GLES_Initialize(int featureLevel) {
    g_level = featureLevel >= 3 ? 3 : 2;

    // The OpenGL ES 2.0 feature level only uses OpenGL ES 2.0 and its extensions,
    // even when the driver created a newer context.
    g_has_map_range = g_level >= 3 ||
        (GLAD_GL_EXT_map_buffer_range && GLAD_GL_OES_mapbuffer &&
         glMapBufferRangeEXT != NULL && glUnmapBufferOES != NULL);
    g_has_invalidate = g_level >= 3;
    g_has_discard = !g_has_invalidate && GLAD_GL_EXT_discard_framebuffer &&
                    glDiscardFramebufferEXT != NULL;
    g_has_aniso = GLAD_GL_EXT_texture_filter_anisotropic != 0;
    g_max_fragment_units = 1;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &g_max_fragment_units);

    I_GLES_ResetState();
    I_GLES_InitProgramCache();

    g_vbo = g_ibo = 0;
    if (g_has_map_range) {
        glGenBuffers(1, &g_vbo);
        glGenBuffers(1, &g_ibo);
        glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)GLES_RING_VERTICES * sizeof(vtx_t), NULL, GL_STREAM_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)GLES_RING_INDICES * sizeof(GLushort), NULL,
                     GL_STREAM_DRAW);
        I_GLES_SetRingAttributes();
    }
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);

    g_active = true;
    SDL_Log("DGL GLES: OpenGL ES %d.0 feature level, geometry %s, frame end %s",
            g_level, g_has_map_range ? "ring buffers (unsynchronized maps)" : "client arrays",
            g_has_invalidate ? "glInvalidateFramebuffer"
            : g_has_discard ? "glDiscardFramebufferEXT" : "no depth invalidation");
    return true;
}

void I_GLES_Shutdown(void) {
    if (g_active) {
        for (int i = 0; i < g_program_count; ++i) {
            if (g_programs[i].id)
                glDeleteProgram(g_programs[i].id);
        }
        if (g_vbo)
            glDeleteBuffers(1, &g_vbo);
        if (g_ibo)
            glDeleteBuffers(1, &g_ibo);
        if (g_readback_framebuffer)
            glDeleteFramebuffers(1, &g_readback_framebuffer);
    }
    g_program_count = 0;
    g_last_program = NULL;
    g_used_program = 0;
    g_vbo = g_ibo = g_readback_framebuffer = 0;
    g_active = false;
    SDL_free(g_binary_formats);
    g_binary_formats = NULL;
    g_binary_format_count = 0;
    g_cache_dir[0] = '\0';
    free(g_texinfo);
    g_texinfo = NULL;
    g_texinfo_size = 0;
    SDL_free(g_extension_string);
    g_extension_string = NULL;
    SDL_free(g_wire_indices);
    g_wire_indices = NULL;
    g_wire_capacity = 0;
}

//
// World shader and tint
//

void I_GLES_BindWorldShader(boolean threePointFilter) {
    if (!g_world_initialised) {
        // Like the desktop shader, the filter is chosen when it is first bound.
        g_world_3point = threePointFilter;
        g_world_initialised = true;
    }
    g_world_bound = true;
    g_world.useTex = 1;
    g_world.texel[0] = g_world.texel[1] = 0.0f;
    I_GLES_WorldChanged();
}

void I_GLES_UnbindWorldShader(void) {
    g_world_bound = false;
}

void I_GLES_SetWorldTextureSize(int width, int height) {
    // The desktop build uploads to the bound program, and only the 3-point
    // shader has the uniform.
    if (!g_world_bound || !g_world_3point)
        return;
    g_world.texel[0] = (width > 0 && height > 0) ? 1.0f / (GLfloat)width : 0.0f;
    g_world.texel[1] = (width > 0 && height > 0) ? 1.0f / (GLfloat)height : 0.0f;
    I_GLES_WorldChanged();
}

void I_GLES_SetWorldUseTexture(boolean on) {
    if (!g_world_bound || !g_world_3point)
        return;
    g_world.useTex = on ? 1 : 0;
    I_GLES_WorldChanged();
}

void I_GLES_SetWorldCombiner(int useTexture, int texWidth, int texHeight,
                             int passCount, int passMode, const float* passColor,
                             float passFactor, int fogEnabled, const float* fogColor,
                             float fogFactor, int fogMode, float fogStart, float fogEnd,
                             float fogDensity) {
    if (!g_world_bound)
        return;
    if (g_world_3point) {
        g_world.useTex = useTexture ? 1 : 0;
        g_world.texel[0] = (texWidth > 0 && texHeight > 0) ? 1.0f / (GLfloat)texWidth : 0.0f;
        g_world.texel[1] = (texWidth > 0 && texHeight > 0) ? 1.0f / (GLfloat)texHeight : 0.0f;
    }
    g_world.passCount = passCount;
    g_world.passMode = passMode;
    if (passColor) {
        g_world.passColor[0] = passColor[0];
        g_world.passColor[1] = passColor[1];
        g_world.passColor[2] = passColor[2];
    }
    g_world.passFactor = passFactor;
    g_world.fogEnabled = fogEnabled;
    if (fogColor) {
        g_world.fogColor[0] = fogColor[0];
        g_world.fogColor[1] = fogColor[1];
        g_world.fogColor[2] = fogColor[2];
    }
    g_world.fogFactor = fogFactor;
    g_world.fogMode = fogMode;
    g_world.fogStart = fogStart;
    g_world.fogEnd = fogEnd;
    g_world.fogDensity = fogDensity;
    I_GLES_WorldChanged();
}

void I_GLES_SetTint(boolean active, float r, float g, float b, float a) {
    g_tint_active = active;
    g_tint_color[0] = r;
    g_tint_color[1] = g;
    g_tint_color[2] = b;
    g_tint_color[3] = a;
}

//
// Matrices
//

void I_GLES_MatrixMode(GLenum mode) {
    if (mode == GL_MODELVIEW || mode == GL_PROJECTION)
        g_matrix_mode = mode;
}

void I_GLES_LoadIdentity(void) {
    I_GLES_MatrixIdentity(I_GLES_CurrentMatrix());
    I_GLES_MatrixChanged();
}

void I_GLES_LoadMatrixf(const GLfloat* matrix) {
    memcpy(I_GLES_CurrentMatrix(), matrix, sizeof(GLfloat) * 16);
    I_GLES_MatrixChanged();
}

void I_GLES_MultMatrixf(const GLfloat* matrix) {
    I_GLES_MatrixMultiplyCurrent(matrix);
}

void I_GLES_PushMatrix(void) {
    if (g_matrix_mode == GL_PROJECTION) {
        if (g_projection_top + 1 >= GLES_MATRIX_STACK_DEPTH)
            return;
        memcpy(g_projection[g_projection_top + 1], g_projection[g_projection_top], sizeof(GLfloat) * 16);
        ++g_projection_top;
    } else {
        if (g_modelview_top + 1 >= GLES_MATRIX_STACK_DEPTH)
            return;
        memcpy(g_modelview[g_modelview_top + 1], g_modelview[g_modelview_top], sizeof(GLfloat) * 16);
        ++g_modelview_top;
    }
}

void I_GLES_PopMatrix(void) {
    if (g_matrix_mode == GL_PROJECTION) {
        if (g_projection_top > 0) {
            --g_projection_top;
            I_GLES_MatrixChanged();
        }
    } else if (g_modelview_top > 0) {
        --g_modelview_top;
        I_GLES_MatrixChanged();
    }
}

void I_GLES_Ortho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
                  GLdouble nearVal, GLdouble farVal) {
    if (right == left || top == bottom || farVal == nearVal)
        return;
    GLfloat matrix[16];
    I_GLES_MatrixIdentity(matrix);
    matrix[0] = (GLfloat)(2.0 / (right - left));
    matrix[5] = (GLfloat)(2.0 / (top - bottom));
    matrix[10] = (GLfloat)(-2.0 / (farVal - nearVal));
    matrix[12] = (GLfloat)(-(right + left) / (right - left));
    matrix[13] = (GLfloat)(-(top + bottom) / (top - bottom));
    matrix[14] = (GLfloat)(-(farVal + nearVal) / (farVal - nearVal));
    I_GLES_MatrixMultiplyCurrent(matrix);
}

void I_GLES_Frustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
                    GLdouble nearVal, GLdouble farVal) {
    if (right == left || top == bottom || farVal == nearVal || nearVal <= 0.0)
        return;
    GLfloat matrix[16] = { 0 };
    matrix[0] = (GLfloat)((2.0 * nearVal) / (right - left));
    matrix[5] = (GLfloat)((2.0 * nearVal) / (top - bottom));
    matrix[8] = (GLfloat)((right + left) / (right - left));
    matrix[9] = (GLfloat)((top + bottom) / (top - bottom));
    matrix[10] = (GLfloat)(-(farVal + nearVal) / (farVal - nearVal));
    matrix[11] = -1.0f;
    matrix[14] = (GLfloat)(-(2.0 * farVal * nearVal) / (farVal - nearVal));
    I_GLES_MatrixMultiplyCurrent(matrix);
}

void I_GLES_Translatef(GLfloat x, GLfloat y, GLfloat z) {
    GLfloat matrix[16];
    I_GLES_MatrixIdentity(matrix);
    matrix[12] = x;
    matrix[13] = y;
    matrix[14] = z;
    I_GLES_MatrixMultiplyCurrent(matrix);
}

void I_GLES_Rotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) {
    GLfloat length = sqrtf(x * x + y * y + z * z);
    if (length <= 0.000001f)
        return;
    x /= length;
    y /= length;
    z /= length;
    GLfloat radians = angle * (GLfloat)(M_PI / 180.0);
    GLfloat c = cosf(radians), s = sinf(radians), t = 1.0f - c;
    GLfloat matrix[16];
    matrix[0] = t * x * x + c;     matrix[1] = t * x * y + s * z; matrix[2] = t * x * z - s * y;  matrix[3] = 0;
    matrix[4] = t * x * y - s * z; matrix[5] = t * y * y + c;     matrix[6] = t * y * z + s * x;  matrix[7] = 0;
    matrix[8] = t * x * z + s * y; matrix[9] = t * y * z - s * x; matrix[10] = t * z * z + c;     matrix[11] = 0;
    matrix[12] = matrix[13] = matrix[14] = 0;
    matrix[15] = 1;
    I_GLES_MatrixMultiplyCurrent(matrix);
}

void I_GLES_Scalef(GLfloat x, GLfloat y, GLfloat z) {
    GLfloat matrix[16];
    I_GLES_MatrixIdentity(matrix);
    matrix[0] = x;
    matrix[5] = y;
    matrix[10] = z;
    I_GLES_MatrixMultiplyCurrent(matrix);
}

void I_GLES_GetFloatv(GLenum pname, GLfloat* values) {
    if (pname == GL_MODELVIEW_MATRIX) {
        memcpy(values, g_modelview[g_modelview_top], sizeof(GLfloat) * 16);
        return;
    }
    if (pname == GL_PROJECTION_MATRIX) {
        memcpy(values, g_projection[g_projection_top], sizeof(GLfloat) * 16);
        return;
    }
    I_GLES_GetFloatvRaw(pname, values);
}

void I_GLES_GetDoublev(GLenum pname, GLdouble* values) {
    GLfloat value[16] = { 0 };
    I_GLES_GetFloatv(pname, value);
    if (pname == GL_MODELVIEW_MATRIX || pname == GL_PROJECTION_MATRIX) {
        for (int i = 0; i < 16; ++i)
            values[i] = value[i];
        return;
    }
    values[0] = value[0];
}

//
// Immediate mode
//

static void I_GLES_AppendImmediateVertex(GLfloat x, GLfloat y, GLfloat z) {
    if (!g_inside_begin || g_immediate_vertex_count >= GLES_IMMEDIATE_VERTICES)
        return;
    vtx_t* vertex = &g_immediate_vertices[g_immediate_vertex_count++];
    vertex->x = x;
    vertex->y = y;
    vertex->z = z;
    vertex->tu = g_current_texcoord[0];
    vertex->tv = g_current_texcoord[1];
    vertex->r = I_GLES_ToByte(g_current_color[0]);
    vertex->g = I_GLES_ToByte(g_current_color[1]);
    vertex->b = I_GLES_ToByte(g_current_color[2]);
    vertex->a = I_GLES_ToByte(g_current_color[3]);
}

static void I_GLES_AddImmediateIndex(GLushort index) {
    if (g_immediate_index_count < GLES_IMMEDIATE_VERTICES * 6)
        g_immediate_indices[g_immediate_index_count++] = index;
}

static void I_GLES_BuildImmediateIndices(void) {
    const GLsizei n = g_immediate_vertex_count;
    g_immediate_index_count = 0;
    switch (g_immediate_mode) {
    case GL_LINES:
        for (GLsizei i = 0; i + 1 < n; i += 2) {
            I_GLES_AddImmediateIndex((GLushort)i);
            I_GLES_AddImmediateIndex((GLushort)(i + 1));
        }
        break;
    case GL_LINE_STRIP:
    case GL_LINE_LOOP:
        for (GLsizei i = 0; i + 1 < n; ++i) {
            I_GLES_AddImmediateIndex((GLushort)i);
            I_GLES_AddImmediateIndex((GLushort)(i + 1));
        }
        if (g_immediate_mode == GL_LINE_LOOP && n > 2) {
            I_GLES_AddImmediateIndex((GLushort)(n - 1));
            I_GLES_AddImmediateIndex(0);
        }
        break;
    case GL_TRIANGLE_STRIP:
        for (GLsizei i = 0; i + 2 < n; ++i) {
            if (i & 1) {
                I_GLES_AddImmediateIndex((GLushort)(i + 1));
                I_GLES_AddImmediateIndex((GLushort)i);
            } else {
                I_GLES_AddImmediateIndex((GLushort)i);
                I_GLES_AddImmediateIndex((GLushort)(i + 1));
            }
            I_GLES_AddImmediateIndex((GLushort)(i + 2));
        }
        break;
    case GL_TRIANGLE_FAN:
    case GL_POLYGON:
        for (GLsizei i = 1; i + 1 < n; ++i) {
            I_GLES_AddImmediateIndex(0);
            I_GLES_AddImmediateIndex((GLushort)i);
            I_GLES_AddImmediateIndex((GLushort)(i + 1));
        }
        break;
    case GL_QUADS:
        for (GLsizei i = 0; i + 3 < n; i += 4) {
            I_GLES_AddImmediateIndex((GLushort)i);
            I_GLES_AddImmediateIndex((GLushort)(i + 1));
            I_GLES_AddImmediateIndex((GLushort)(i + 2));
            I_GLES_AddImmediateIndex((GLushort)i);
            I_GLES_AddImmediateIndex((GLushort)(i + 2));
            I_GLES_AddImmediateIndex((GLushort)(i + 3));
        }
        break;
    case GL_QUAD_STRIP:
        for (GLsizei i = 0; i + 3 < n; i += 2) {
            I_GLES_AddImmediateIndex((GLushort)i);
            I_GLES_AddImmediateIndex((GLushort)(i + 1));
            I_GLES_AddImmediateIndex((GLushort)(i + 3));
            I_GLES_AddImmediateIndex((GLushort)i);
            I_GLES_AddImmediateIndex((GLushort)(i + 3));
            I_GLES_AddImmediateIndex((GLushort)(i + 2));
        }
        break;
    default: // GL_POINTS, GL_TRIANGLES
        for (GLsizei i = 0; i < n; ++i)
            I_GLES_AddImmediateIndex((GLushort)i);
        break;
    }
}

void I_GLES_Begin(GLenum mode) {
    if (g_inside_begin)
        I_GLES_End();
    g_inside_begin = true;
    g_immediate_mode = mode;
    g_immediate_vertex_count = 0;
    g_immediate_index_count = 0;
}

void I_GLES_End(void) {
    if (!g_inside_begin)
        return;
    g_inside_begin = false;
    I_GLES_BuildImmediateIndices();

    GLenum drawMode = GL_TRIANGLES;
    if (g_immediate_mode == GL_LINES || g_immediate_mode == GL_LINE_STRIP ||
        g_immediate_mode == GL_LINE_LOOP)
        drawMode = GL_LINES;
    else if (g_immediate_mode == GL_POINTS)
        drawMode = GL_POINTS;

    // Polygons and quads in line mode: only their outline, not the diagonals of
    // the triangulation.
    if (g_wireframe && g_immediate_mode == GL_POLYGON && g_immediate_vertex_count >= 2) {
        g_immediate_index_count = 0;
        for (GLsizei i = 0; i < g_immediate_vertex_count; ++i) {
            I_GLES_AddImmediateIndex((GLushort)i);
            I_GLES_AddImmediateIndex((GLushort)((i + 1) % g_immediate_vertex_count));
        }
        drawMode = GL_LINES;
    } else if (g_wireframe && g_immediate_mode == GL_QUADS) {
        g_immediate_index_count = 0;
        for (GLsizei i = 0; i + 3 < g_immediate_vertex_count; i += 4) {
            for (GLsizei e = 0; e < 4; ++e) {
                I_GLES_AddImmediateIndex((GLushort)(i + e));
                I_GLES_AddImmediateIndex((GLushort)(i + ((e + 1) & 3)));
            }
        }
        drawMode = GL_LINES;
    }

    I_GLES_DrawBatch(drawMode, g_immediate_vertex_count, g_immediate_vertices,
                     g_immediate_index_count, g_immediate_indices);
}

void I_GLES_Vertex2f(GLfloat x, GLfloat y) {
    I_GLES_AppendImmediateVertex(x, y, 0.0f);
}

void I_GLES_Vertex3f(GLfloat x, GLfloat y, GLfloat z) {
    I_GLES_AppendImmediateVertex(x, y, z);
}

void I_GLES_Vertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    if (fabsf(w) < 0.000001f)
        w = 1.0f;
    I_GLES_AppendImmediateVertex(x / w, y / w, z / w);
}

void I_GLES_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    g_current_color[0] = r;
    g_current_color[1] = g;
    g_current_color[2] = b;
    g_current_color[3] = a;
}

void I_GLES_Color4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) {
    g_current_color[0] = (GLfloat)r / 255.0f;
    g_current_color[1] = (GLfloat)g / 255.0f;
    g_current_color[2] = (GLfloat)b / 255.0f;
    g_current_color[3] = (GLfloat)a / 255.0f;
}

void I_GLES_Color4ubv(const GLubyte* color) {
    if (color)
        I_GLES_Color4ub(color[0], color[1], color[2], color[3]);
}

void I_GLES_TexCoord2f(GLfloat s, GLfloat t) {
    g_current_texcoord[0] = s;
    g_current_texcoord[1] = t;
}

void I_GLES_Rectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2) {
    I_GLES_Begin(GL_TRIANGLE_STRIP);
    I_GLES_Vertex2f(x1, y1);
    I_GLES_Vertex2f(x2, y1);
    I_GLES_Vertex2f(x1, y2);
    I_GLES_Vertex2f(x2, y2);
    I_GLES_End();
}

void I_GLES_Recti(GLint x1, GLint y1, GLint x2, GLint y2) {
    I_GLES_Rectf((GLfloat)x1, (GLfloat)y1, (GLfloat)x2, (GLfloat)y2);
}

// Client-side arrays: the engine hands its vertices to dglDrawGeometry directly.
void I_GLES_SetVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer) {
    (void)size; (void)type; (void)stride; (void)pointer;
}

void I_GLES_SetColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer) {
    (void)size; (void)type; (void)stride; (void)pointer;
}

void I_GLES_SetTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer) {
    (void)size; (void)type; (void)stride; (void)pointer;
}

void I_GLES_EnableClientState(GLenum array) {
    (void)array;
}

void I_GLES_DisableClientState(GLenum array) {
    (void)array;
}

//
// Capabilities and render state
//

static boolean* I_GLES_TrackedCap(GLenum cap) {
    switch (cap) {
    case GL_BLEND:               return &g_cap_blend;
    case GL_DEPTH_TEST:          return &g_cap_depth_test;
    case GL_CULL_FACE:           return &g_cap_cull;
    case GL_SCISSOR_TEST:        return &g_cap_scissor;
    case GL_POLYGON_OFFSET_FILL: return &g_cap_polygon_offset;
    case GL_DITHER:              return &g_cap_dither;
    case GL_STENCIL_TEST:        return &g_cap_stencil;
    default:                     return NULL;
    }
}

static void I_GLES_SetCap(GLenum cap, boolean enable) {
    switch (cap) {
    case GL_TEXTURE_2D:
        if (g_unit[g_active_unit].enabled != enable) {
            g_unit[g_active_unit].enabled = enable;
            I_GLES_FixedChanged();
        }
        return;
    case GL_ALPHA_TEST:
        if (g_alpha_test != enable) {
            g_alpha_test = enable;
            I_GLES_FixedChanged();
            I_GLES_WorldChanged();
        }
        return;
    case GL_FOG:
        if (g_fog != enable) {
            g_fog = enable;
            I_GLES_FixedChanged();
        }
        return;
    default:
        break;
    }

    boolean* tracked = I_GLES_TrackedCap(cap);
    if (!tracked)
        return;     // desktop-only capability (multisample, logic op, smoothing...)
    if (*tracked == enable)
        return;
    *tracked = enable;
    if (enable)
        glEnable(cap);
    else
        glDisable(cap);
}

void I_GLES_Enable(GLenum cap) {
    I_GLES_SetCap(cap, true);
}

void I_GLES_Disable(GLenum cap) {
    I_GLES_SetCap(cap, false);
}

GLboolean I_GLES_IsEnabled(GLenum cap) {
    switch (cap) {
    case GL_TEXTURE_2D: return g_unit[g_active_unit].enabled;
    case GL_ALPHA_TEST: return g_alpha_test;
    case GL_FOG:        return g_fog;
    default:
        break;
    }
    boolean* tracked = I_GLES_TrackedCap(cap);
    return tracked ? *tracked : GL_FALSE;
}

void I_GLES_AlphaFunc(GLenum func, GLclampf reference) {
    if (reference < 0.0f) reference = 0.0f;
    if (reference > 1.0f) reference = 1.0f;
    if (g_alpha_func != func) {
        g_alpha_func = func;
        I_GLES_FixedChanged();
        I_GLES_WorldChanged();
    }
    g_alpha_ref = reference;
}

void I_GLES_BlendFunc(GLenum sfactor, GLenum dfactor) {
    if (g_blend_src == sfactor && g_blend_dst == dfactor)
        return;
    g_blend_src = sfactor;
    g_blend_dst = dfactor;
    glBlendFunc(sfactor, dfactor);
}

void I_GLES_DepthMask(GLboolean flag) {
    flag = flag ? GL_TRUE : GL_FALSE;
    if (g_depth_mask == flag)
        return;
    g_depth_mask = flag;
    glDepthMask(flag);
}

void I_GLES_DepthRange(GLclampf nearVal, GLclampf farVal) {
    glDepthRangef(nearVal, farVal);
}

void I_GLES_ClearDepth(GLclampd depth) {
    glClearDepthf((GLfloat)depth);
}

void I_GLES_Fogf(GLenum pname, GLfloat param) {
    if (pname == GL_FOG_START) g_fog_start = param;
    else if (pname == GL_FOG_END) g_fog_end = param;
    else if (pname == GL_FOG_DENSITY) g_fog_density = param;
    else if (pname == GL_FOG_MODE) I_GLES_Fogi(pname, (GLint)param);
}

void I_GLES_Fogfv(GLenum pname, const GLfloat* params) {
    if (pname == GL_FOG_COLOR && params) {
        g_fog_color[0] = params[0];
        g_fog_color[1] = params[1];
        g_fog_color[2] = params[2];
        g_fog_color[3] = params[3];
    } else if (params) {
        I_GLES_Fogf(pname, params[0]);
    }
}

void I_GLES_Fogi(GLenum pname, GLint param) {
    if (pname == GL_FOG_MODE) {
        if (g_fog_mode != param) {
            g_fog_mode = param;
            I_GLES_FixedChanged();
        }
    } else {
        I_GLES_Fogf(pname, (GLfloat)param);
    }
}

void I_GLES_PolygonMode(GLenum face, GLenum mode) {
    if (face == GL_FRONT_AND_BACK || face == GL_FRONT || face == GL_BACK)
        g_wireframe = (mode == GL_LINE);
}

void I_GLES_PolygonOffset(GLfloat factor, GLfloat units) {
    glPolygonOffset(factor, units);
}

void I_GLES_LineWidth(GLfloat width) {
    glLineWidth(width);
}

void I_GLES_ShadeModel(GLenum mode) {
    (void)mode;
}

void I_GLES_Hint(GLenum target, GLenum mode) {
    // Perspective correction and fog hints do not exist in OpenGL ES.
    (void)target;
    (void)mode;
}

//
// Texture environment
//

void I_GLES_TexEnvi(GLenum target, GLenum pname, GLint param) {
    if (target != GL_TEXTURE_ENV)
        return;
    texunit_t* unit = &g_unit[g_active_unit];
    switch (pname) {
    case GL_TEXTURE_ENV_MODE: unit->mode = param; break;
    case GL_COMBINE_RGB:      unit->combineRGB = param; break;
    case GL_COMBINE_ALPHA:    unit->combineAlpha = param; break;
    case GL_SOURCE0_RGB:      unit->srcRGB[0] = param; break;
    case GL_SOURCE1_RGB:      unit->srcRGB[1] = param; break;
    case GL_SOURCE2_RGB:      unit->srcRGB[2] = param; break;
    case GL_OPERAND0_RGB:     unit->opRGB[0] = param; break;
    case GL_OPERAND1_RGB:     unit->opRGB[1] = param; break;
    case GL_OPERAND2_RGB:     unit->opRGB[2] = param; break;
    case GL_SOURCE0_ALPHA:    unit->srcAlpha[0] = param; break;
    case GL_SOURCE1_ALPHA:    unit->srcAlpha[1] = param; break;
    case GL_SOURCE2_ALPHA:    unit->srcAlpha[2] = param; break;
    case GL_OPERAND0_ALPHA:   unit->opAlpha[0] = param; break;
    case GL_OPERAND1_ALPHA:   unit->opAlpha[1] = param; break;
    case GL_OPERAND2_ALPHA:   unit->opAlpha[2] = param; break;
    case GL_RGB_SCALE:        unit->rgbScale = (GLfloat)param; break;
    case GL_ALPHA_SCALE:      unit->alphaScale = (GLfloat)param; break;
    default:                  return;
    }
    I_GLES_FixedChanged();
}

void I_GLES_TexEnvfv(GLenum target, GLenum pname, const GLfloat* params) {
    if (target != GL_TEXTURE_ENV || !params)
        return;
    if (pname == GL_TEXTURE_ENV_COLOR) {
        memcpy(g_unit[g_active_unit].envColor, params, 4 * sizeof(GLfloat));
    } else if (pname == GL_RGB_SCALE || pname == GL_ALPHA_SCALE) {
        I_GLES_TexEnvi(target, pname, (GLint)params[0]);
    }
}

//
// Textures
//

void I_GLES_TexParameteri(GLenum target, GLenum pname, GLint param) {
    if (g_level < 3 && (pname == GL_TEXTURE_BASE_LEVEL || pname == GL_TEXTURE_MAX_LEVEL ||
                        pname == GL_TEXTURE_MIN_LOD || pname == GL_TEXTURE_MAX_LOD))
        return;
    if (pname == GL_GENERATE_MIPMAP)
        return;
    if (pname == GL_TEXTURE_MAX_ANISOTROPY_EXT) {
        if (g_has_aniso)
            glTexParameterf(target, pname, (GLfloat)param);
        return;
    }
    if ((pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T) && param == GL_CLAMP)
        param = GL_CLAMP_TO_EDGE;
    glTexParameteri(target, pname, param);
}

void I_GLES_TexParameterf(GLenum target, GLenum pname, GLfloat param) {
    if (pname == GL_TEXTURE_MAX_ANISOTROPY_EXT) {
        if (g_has_aniso)
            glTexParameterf(target, pname, param < 1.0f ? 1.0f : param);
        return;
    }
    I_GLES_TexParameteri(target, pname, (GLint)param);
}

static GLubyte* I_GLES_ExpandLegacyPixels(GLenum format, GLsizei width, GLsizei height,
                                          GLenum type, const GLvoid* source) {
    // Luminance and alpha textures become RGBA with the same fixed-function
    // meaning, so both feature levels sample them the same way.
    if (type != GL_UNSIGNED_BYTE || !source)
        return NULL;
    if (format != GL_LUMINANCE && format != GL_ALPHA && format != GL_LUMINANCE_ALPHA)
        return NULL;
    const size_t pixels = (size_t)width * (size_t)height;
    const size_t stride = format == GL_LUMINANCE_ALPHA ? 2 : 1;
    GLubyte* rgba = (GLubyte*)SDL_malloc(pixels * 4);
    if (!rgba)
        return NULL;
    const GLubyte* input = (const GLubyte*)source;
    for (size_t i = 0; i < pixels; ++i) {
        const GLubyte l = input[i * stride];
        if (format == GL_ALPHA) {
            rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = l;
        } else {
            rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = l;
            rgba[i * 4 + 3] = format == GL_LUMINANCE_ALPHA ? input[i * stride + 1] : 255;
        }
    }
    return rgba;
}

static GLint I_GLES_InternalFormat(GLint internalFormat, GLenum format) {
    // Unsized formats on both feature levels, like GL4ES: OpenGL ES 2.0 requires
    // the internal format to equal the format, and on OpenGL ES 3.0 a sized RGB8
    // texture cannot receive glCopyTexSubImage2D from an RGB565 window surface.
    if (g_level < 3 || format == GL_RGB || format == GL_RGBA)
        return (GLint)format;
    return internalFormat;
}

void I_GLES_TexImage2D(GLenum target, GLint level, GLint internalFormat,
                       GLsizei width, GLsizei height, GLint border,
                       GLenum format, GLenum type, const GLvoid* pixels) {
    GLubyte* expanded = I_GLES_ExpandLegacyPixels(format, width, height, type, pixels);
    if (expanded || format == GL_LUMINANCE || format == GL_ALPHA || format == GL_LUMINANCE_ALPHA) {
        // A NULL upload of a legacy format allocates RGBA storage as well.
        format = GL_RGBA;
        internalFormat = GL_RGBA;
        type = GL_UNSIGNED_BYTE;
    }
    glTexImage2D(target, level, I_GLES_InternalFormat(internalFormat, format), width, height,
                 border, format, type, expanded ? expanded : pixels);
    SDL_free(expanded);

    if (target == GL_TEXTURE_2D && level == 0) {
        texture_info_t* info = I_GLES_TextureInfo(g_bound[g_active_unit], true);
        if (info) {
            info->width = width;
            info->height = height;
            info->format = format;
        }
        if (g_unit[g_active_unit].enabled)
            I_GLES_FixedChanged();
    }
}

void I_GLES_TexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                          GLsizei width, GLsizei height, GLenum format,
                          GLenum type, const GLvoid* pixels) {
    GLubyte* expanded = I_GLES_ExpandLegacyPixels(format, width, height, type, pixels);
    if (expanded) {
        format = GL_RGBA;
        type = GL_UNSIGNED_BYTE;
    }
    glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type,
                    expanded ? expanded : pixels);
    SDL_free(expanded);
}

void I_GLES_CopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                              GLint x, GLint y, GLsizei width, GLsizei height) {
    // Screen captures for wipes read what is on screen: they do not open a frame,
    // so they are never preceded by the clear at the start of one.
    glCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height);
}

void I_GLES_ActiveTexture(GLenum texture) {
    if (texture < GL_TEXTURE0 || texture >= GL_TEXTURE0 + 32)
        texture = GL_TEXTURE0;
    if (g_active_texture != texture) {
        glActiveTexture(texture);
        g_active_texture = texture;
    }
    g_active_unit = (int)(texture - GL_TEXTURE0);
    if (g_active_unit >= GLES_UNITS)
        g_active_unit = GLES_UNITS - 1;
}

void I_GLES_ClientActiveTexture(GLenum texture) {
    (void)texture;
}

void I_GLES_BindTexture(GLenum target, GLuint texture) {
    const GLuint index = g_active_texture - GL_TEXTURE0;
    if (target != GL_TEXTURE_2D || index >= GLES_UNITS) {
        glBindTexture(target, texture);
        return;
    }
    if (g_bound[index] == texture)
        return;
    glBindTexture(target, texture);
    g_bound[index] = texture;
    if (g_unit[index].enabled)
        I_GLES_FixedChanged();
}

void I_GLES_GenTextures(GLsizei count, GLuint* textures) {
    glGenTextures(count, textures);
}

void I_GLES_DeleteTextures(GLsizei count, const GLuint* textures) {
    glDeleteTextures(count, textures);
    if (!textures)
        return;
    for (GLsizei i = 0; i < count; ++i) {
        for (int u = 0; u < GLES_UNITS; ++u) {
            if (g_bound[u] == textures[i]) {
                g_bound[u] = 0;
                I_GLES_FixedChanged();
            }
        }
        texture_info_t* info = I_GLES_TextureInfo(textures[i], false);
        if (info)
            memset(info, 0, sizeof(*info));
    }
}

void I_GLES_GetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint* params) {
    if (!params)
        return;
    *params = 0;
    if (target == GL_TEXTURE_2D && level == 0) {
        const texture_info_t* info = I_GLES_TextureInfo(g_bound[g_active_unit], false);
        if (pname == GL_TEXTURE_WIDTH)
            *params = info ? info->width : 0;
        else if (pname == GL_TEXTURE_HEIGHT)
            *params = info ? info->height : 0;
    }
}

void I_GLES_GetTexParameteriv(GLenum target, GLenum pname, GLint* params) {
    glGetTexParameteriv(target, pname, params);
}

void I_GLES_GetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid* pixels) {
    // Only used by tools and screenshots: read the texture back through a framebuffer.
    if (target != GL_TEXTURE_2D || level != 0 || !pixels)
        return;
    const GLuint texture = g_bound[g_active_unit];
    const texture_info_t* info = I_GLES_TextureInfo(texture, false);
    if (!info || info->width <= 0 || info->height <= 0)
        return;
    if (!g_readback_framebuffer)
        glGenFramebuffers(1, &g_readback_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, g_readback_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, texture, level);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status == GL_FRAMEBUFFER_COMPLETE) {
        GLint packAlignment = 4;
        glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, info->width, info->height, format, type, pixels);
        glPixelStorei(GL_PACK_ALIGNMENT, packAlignment);
    } else {
        SDL_Log("DGL GLES: texture readback framebuffer incomplete (0x%x)", status);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

//
// Queries
//

void I_GLES_GetIntegerv(GLenum pname, GLint* params) {
    if (!params)
        return;
    switch (pname) {
    case GL_MAX_TEXTURE_UNITS_ARB:
        // Texture stages become fragment samplers.
        *params = g_max_fragment_units < GLES_UNITS ? g_max_fragment_units : GLES_UNITS;
        return;
    case GL_BLEND_SRC:
    case GL_BLEND_SRC_RGB:
        *params = (GLint)g_blend_src;
        return;
    case GL_BLEND_DST:
    case GL_BLEND_DST_RGB:
        *params = (GLint)g_blend_dst;
        return;
    case GL_MODELVIEW_STACK_DEPTH:
        *params = g_modelview_top + 1;
        return;
    case GL_PROJECTION_STACK_DEPTH:
        *params = g_projection_top + 1;
        return;
    case GL_ACTIVE_TEXTURE:
        *params = (GLint)g_active_texture;
        return;
    case GL_TEXTURE_BINDING_2D:
        *params = (GLint)g_bound[g_active_unit];
        return;
    case GL_CURRENT_PROGRAM:
        *params = (GLint)g_used_program;
        return;
    default:
        glGetIntegerv(pname, params);
        return;
    }
}

void I_GLES_GetBooleanv(GLenum pname, GLboolean* params) {
    if (!params)
        return;
    if (pname == GL_DEPTH_WRITEMASK) {
        *params = g_depth_mask;
        return;
    }
    if (pname == GL_TEXTURE_2D || pname == GL_ALPHA_TEST || pname == GL_FOG ||
        I_GLES_TrackedCap(pname)) {
        *params = I_GLES_IsEnabled(pname);
        return;
    }
    glGetBooleanv(pname, params);
}

void I_GLES_GetFloatvRaw(GLenum pname, GLfloat* params) {
    if (!params)
        return;
    if (pname == GL_MODELVIEW_MATRIX || pname == GL_PROJECTION_MATRIX) {
        I_GLES_GetFloatv(pname, params);
        return;
    }
    if (pname == GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT && !g_has_aniso) {
        *params = 1.0f;
        return;
    }
    glGetFloatv(pname, params);
}
