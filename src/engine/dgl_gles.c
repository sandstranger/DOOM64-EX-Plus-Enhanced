#include <SDL3/SDL.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "dgl_gles.h"
#include "i_video.h"
#include "i_shaders.h"
#include "i_sectorcombiner.h"

#define GLES_MATRIX_STACK_DEPTH 32
#define GLES_MAX_TEXTURE_UNITS  8
#define GLES_IMMEDIATE_VERTICES 4096
#define GLES_MAX_TEXTURE_INFO   16384

typedef void (APIENTRY *gles_glGenBuffers_t)(GLsizei, GLuint*);
typedef void (APIENTRY *gles_glBindBuffer_t)(GLenum, GLuint);
typedef void (APIENTRY *gles_glBufferData_t)(GLenum, ptrdiff_t, const void*, GLenum);
typedef void (APIENTRY *gles_glDeleteBuffers_t)(GLsizei, const GLuint*);
typedef void (APIENTRY *gles_glEnableVertexAttribArray_t)(GLuint);
typedef void (APIENTRY *gles_glVertexAttribPointer_t)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void (APIENTRY *gles_glDrawElements_t)(GLenum, GLsizei, GLenum, const void*);
typedef void (APIENTRY *gles_glActiveTexture_t)(GLenum);
typedef void (APIENTRY *gles_glClearDepthf_t)(GLfloat);
typedef void (APIENTRY *gles_glDepthRangef_t)(GLfloat, GLfloat);
typedef void (APIENTRY *gles_glGenFramebuffers_t)(GLsizei, GLuint*);
typedef void (APIENTRY *gles_glBindFramebuffer_t)(GLenum, GLuint);
typedef void (APIENTRY *gles_glFramebufferTexture2D_t)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (APIENTRY *gles_glCheckFramebufferStatus_t)(GLenum);
typedef void (APIENTRY *gles_glDeleteFramebuffers_t)(GLsizei, const GLuint*);

static gles_glGenBuffers_t pglGenBuffers;
static gles_glBindBuffer_t pglBindBuffer;
static gles_glBufferData_t pglBufferData;
static gles_glDeleteBuffers_t pglDeleteBuffers;
static gles_glEnableVertexAttribArray_t pglEnableVertexAttribArray;
static gles_glVertexAttribPointer_t pglVertexAttribPointer;
static gles_glDrawElements_t pglDrawElements;
static gles_glActiveTexture_t pglActiveTexture;
static gles_glClearDepthf_t pglClearDepthf;
static gles_glDepthRangef_t pglDepthRangef;
static gles_glGenFramebuffers_t pglGenFramebuffers;
static gles_glBindFramebuffer_t pglBindFramebuffer;
static gles_glFramebufferTexture2D_t pglFramebufferTexture2D;
static gles_glCheckFramebufferStatus_t pglCheckFramebufferStatus;
static gles_glDeleteFramebuffers_t pglDeleteFramebuffers;

typedef struct {
    GLuint name;
    GLsizei width;
    GLsizei height;
    GLenum format;
} texture_info_t;

static boolean g_active = false;
static GLuint g_vertex_buffer = 0;
static GLuint g_index_buffer = 0;
static GLuint g_readback_framebuffer = 0;
static texture_info_t* g_texture_info = NULL;
static size_t g_texture_info_capacity = 0;
static GLuint g_bound_texture[GLES_MAX_TEXTURE_UNITS] = { 0 };
static int g_active_texture_unit = 0;
static boolean g_texture_enabled[GLES_MAX_TEXTURE_UNITS] = { true };
static boolean g_alpha_test_enabled = false;
static GLenum g_alpha_func = GL_GEQUAL;
static GLfloat g_alpha_reference = 0.0f;
static boolean g_fog_enabled = false;
static GLint g_fog_mode = 1;
static GLfloat g_fog_start = 0.0f;
static GLfloat g_fog_end = 1.0f;
static GLfloat g_fog_density = 1.0f;
static GLfloat g_fog_color[4] = { 0, 0, 0, 1 };
static GLfloat g_rgb_scale = 1.0f;
static boolean g_wireframe = false;
static GLfloat g_modelview[GLES_MATRIX_STACK_DEPTH][16];
static GLfloat g_projection[GLES_MATRIX_STACK_DEPTH][16];
static int g_modelview_top = 0;
static int g_projection_top = 0;
static GLenum g_matrix_mode = GL_MODELVIEW;

static boolean g_inside_begin = false;
static GLenum g_immediate_mode = GL_TRIANGLES;
static vtx_t g_immediate_vertices[GLES_IMMEDIATE_VERTICES];
static GLushort g_immediate_indices[GLES_IMMEDIATE_VERTICES * 6];
static GLsizei g_immediate_vertex_count = 0;
static GLsizei g_immediate_index_count = 0;
static GLfloat g_current_color[4] = { 1, 1, 1, 1 };
static GLfloat g_current_texcoord[2] = { 0, 0 };

static void* I_GLES_GetProc(const char* name) {
    return SDL_GL_GetProcAddress(name);
}

#define GLES_LOAD_PROC(variable, type, name) \
    do { *(void**)(&variable) = I_GLES_GetProc(name); } while (0)

static void I_GLES_MatrixIdentity(GLfloat* matrix) {
    memset(matrix, 0, 16 * sizeof(GLfloat));
    matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
}

static GLfloat* I_GLES_CurrentMatrix(void) {
    if (g_matrix_mode == GL_PROJECTION)
        return g_projection[g_projection_top];
    return g_modelview[g_modelview_top];
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

static void I_GLES_MatrixMultiplyCurrent(const GLfloat* matrix) {
    GLfloat result[16];
    GLfloat* current = I_GLES_CurrentMatrix();
    I_GLES_MatrixMultiply(result, current, matrix);
    memcpy(current, result, sizeof(result));
}

static GLfloat I_GLES_Clamp01(GLfloat value) {
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

static unsigned char I_GLES_ToByte(GLfloat value) {
    return (unsigned char)(I_GLES_Clamp01(value) * 255.0f + 0.5f);
}

static texture_info_t* I_GLES_FindTextureInfo(GLuint name, boolean create) {
    if (!name)
        return NULL;

    for (size_t i = 0; i < g_texture_info_capacity; ++i) {
        if (g_texture_info[i].name == name)
            return &g_texture_info[i];
    }
    if (!create)
        return NULL;

    for (size_t i = 0; i < g_texture_info_capacity; ++i) {
        if (!g_texture_info[i].name) {
            g_texture_info[i].name = name;
            return &g_texture_info[i];
        }
    }

    size_t oldCapacity = g_texture_info_capacity;
    size_t newCapacity = oldCapacity ? oldCapacity * 2 : 256;
    if (newCapacity > GLES_MAX_TEXTURE_INFO)
        return NULL;

    texture_info_t* resized = (texture_info_t*)realloc(
        g_texture_info, newCapacity * sizeof(texture_info_t));
    if (!resized)
        return NULL;

    memset(resized + oldCapacity, 0, (newCapacity - oldCapacity) * sizeof(texture_info_t));
    g_texture_info = resized;
    g_texture_info_capacity = newCapacity;
    g_texture_info[oldCapacity].name = name;
    return &g_texture_info[oldCapacity];
}

static GLuint I_GLES_BoundTexture(void) {
    int unit = g_active_texture_unit;
    if (unit < 0 || unit >= GLES_MAX_TEXTURE_UNITS)
        unit = 0;
    return g_bound_texture[unit];
}

static int I_GLES_FogMode(GLint mode) {
    if (mode == GL_LINEAR) return 1;
    if (mode == GL_EXP) return 2;
    if (mode == GL_EXP2) return 3;
    return 0;
}

static void I_GLES_CommitFog(void) {
    I_SectorCombiner_SetFog(g_fog_enabled, g_fog_color[0], g_fog_color[1],
                            g_fog_color[2], 0.0f);
    I_SectorCombiner_SetFogParams(g_fog_mode, g_fog_start, g_fog_end, g_fog_density);
}

boolean I_GLES_IsActive(void) {
    return g_active;
}

boolean I_GLES_Initialize(void) {
    int version = I_GetGLESVersion();
    if (version == 0) {
        g_active = false;
        return true;
    }

    GLES_LOAD_PROC(pglGenBuffers, gles_glGenBuffers_t, "glGenBuffers");
    GLES_LOAD_PROC(pglBindBuffer, gles_glBindBuffer_t, "glBindBuffer");
    GLES_LOAD_PROC(pglBufferData, gles_glBufferData_t, "glBufferData");
    GLES_LOAD_PROC(pglDeleteBuffers, gles_glDeleteBuffers_t, "glDeleteBuffers");
    GLES_LOAD_PROC(pglEnableVertexAttribArray, gles_glEnableVertexAttribArray_t, "glEnableVertexAttribArray");
    GLES_LOAD_PROC(pglVertexAttribPointer, gles_glVertexAttribPointer_t, "glVertexAttribPointer");
    GLES_LOAD_PROC(pglDrawElements, gles_glDrawElements_t, "glDrawElements");
    GLES_LOAD_PROC(pglActiveTexture, gles_glActiveTexture_t, "glActiveTexture");
    GLES_LOAD_PROC(pglClearDepthf, gles_glClearDepthf_t, "glClearDepthf");
    GLES_LOAD_PROC(pglDepthRangef, gles_glDepthRangef_t, "glDepthRangef");
    GLES_LOAD_PROC(pglGenFramebuffers, gles_glGenFramebuffers_t, "glGenFramebuffers");
    GLES_LOAD_PROC(pglBindFramebuffer, gles_glBindFramebuffer_t, "glBindFramebuffer");
    GLES_LOAD_PROC(pglFramebufferTexture2D, gles_glFramebufferTexture2D_t, "glFramebufferTexture2D");
    GLES_LOAD_PROC(pglCheckFramebufferStatus, gles_glCheckFramebufferStatus_t, "glCheckFramebufferStatus");
    GLES_LOAD_PROC(pglDeleteFramebuffers, gles_glDeleteFramebuffers_t, "glDeleteFramebuffers");

    if (!pglGenBuffers || !pglBindBuffer || !pglBufferData || !pglDeleteBuffers ||
        !pglEnableVertexAttribArray || !pglVertexAttribPointer || !pglDrawElements ||
        !pglActiveTexture || !pglClearDepthf || !pglDepthRangef) {
        SDL_Log("DGL GLES: required GLES2 buffer/attribute/depth entry points are unavailable");
        return false;
    }

    I_GLES_MatrixIdentity(g_modelview[0]);
    I_GLES_MatrixIdentity(g_projection[0]);
    g_modelview_top = 0;
    g_projection_top = 0;
    g_matrix_mode = GL_MODELVIEW;
    g_active_texture_unit = 0;
    memset(g_bound_texture, 0, sizeof(g_bound_texture));
    memset(g_texture_enabled, 0, sizeof(g_texture_enabled));
    g_texture_enabled[0] = true;
    g_current_color[0] = g_current_color[1] = g_current_color[2] = g_current_color[3] = 1.0f;
    g_current_texcoord[0] = g_current_texcoord[1] = 0.0f;
    g_alpha_test_enabled = false;
    g_alpha_func = GL_GEQUAL;
    g_alpha_reference = 0.0f;
    g_fog_enabled = false;
    g_rgb_scale = 1.0f;
    g_wireframe = false;

    pglGenBuffers(1, &g_vertex_buffer);
    pglGenBuffers(1, &g_index_buffer);
    if (!g_vertex_buffer || !g_index_buffer) {
        SDL_Log("DGL GLES: failed to create streaming vertex/index buffers");
        return false;
    }

    if (pglGenFramebuffers)
        pglGenFramebuffers(1, &g_readback_framebuffer);

    g_active = true;
    SDL_Log("DGL GLES: native GLES %d backend initialized (shader + VBO path)", version);
    return true;
}

void I_GLES_Shutdown(void) {
    if (g_vertex_buffer && pglDeleteBuffers)
        pglDeleteBuffers(1, &g_vertex_buffer);
    if (g_index_buffer && pglDeleteBuffers)
        pglDeleteBuffers(1, &g_index_buffer);
    if (g_readback_framebuffer && pglDeleteFramebuffers)
        pglDeleteFramebuffers(1, &g_readback_framebuffer);
    g_active = false;
    g_vertex_buffer = g_index_buffer = g_readback_framebuffer = 0;
    free(g_texture_info);
    g_texture_info = NULL;
    g_texture_info_capacity = 0;
}

void I_GLES_MatrixMode(GLenum mode) {
    if (!g_active) { glMatrixMode(mode); return; }
    if (mode == GL_MODELVIEW || mode == GL_PROJECTION)
        g_matrix_mode = mode;
}

void I_GLES_LoadIdentity(void) {
    if (!g_active) { glLoadIdentity(); return; }
    I_GLES_MatrixIdentity(I_GLES_CurrentMatrix());
}

void I_GLES_LoadMatrixf(const GLfloat* matrix) {
    if (!g_active) { glLoadMatrixf(matrix); return; }
    memcpy(I_GLES_CurrentMatrix(), matrix, sizeof(GLfloat) * 16);
}

void I_GLES_MultMatrixf(const GLfloat* matrix) {
    if (!g_active) { glMultMatrixf(matrix); return; }
    I_GLES_MatrixMultiplyCurrent(matrix);
}

void I_GLES_PushMatrix(void) {
    if (!g_active) { glPushMatrix(); return; }
    if (g_matrix_mode == GL_PROJECTION) {
        if (g_projection_top + 1 >= GLES_MATRIX_STACK_DEPTH) return;
        memcpy(g_projection[g_projection_top + 1], g_projection[g_projection_top], sizeof(GLfloat) * 16);
        ++g_projection_top;
    } else {
        if (g_modelview_top + 1 >= GLES_MATRIX_STACK_DEPTH) return;
        memcpy(g_modelview[g_modelview_top + 1], g_modelview[g_modelview_top], sizeof(GLfloat) * 16);
        ++g_modelview_top;
    }
}

void I_GLES_PopMatrix(void) {
    if (!g_active) { glPopMatrix(); return; }
    if (g_matrix_mode == GL_PROJECTION) {
        if (g_projection_top > 0) --g_projection_top;
    } else if (g_modelview_top > 0) {
        --g_modelview_top;
    }
}

void I_GLES_Ortho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
                  GLdouble nearVal, GLdouble farVal) {
    if (!g_active) { glOrtho(left, right, bottom, top, nearVal, farVal); return; }
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
    if (!g_active) { glFrustum(left, right, bottom, top, nearVal, farVal); return; }
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
    if (!g_active) { glTranslatef(x, y, z); return; }
    GLfloat matrix[16];
    I_GLES_MatrixIdentity(matrix);
    matrix[12] = x; matrix[13] = y; matrix[14] = z;
    I_GLES_MatrixMultiplyCurrent(matrix);
}

void I_GLES_Rotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) {
    if (!g_active) { glRotatef(angle, x, y, z); return; }
    GLfloat length = sqrtf(x*x + y*y + z*z);
    if (length <= 0.000001f)
        return;
    x /= length; y /= length; z /= length;
    GLfloat radians = angle * (GLfloat)(M_PI / 180.0);
    GLfloat c = cosf(radians), s = sinf(radians), t = 1.0f - c;
    GLfloat matrix[16];
    matrix[0] = t*x*x + c;     matrix[1] = t*x*y + s*z; matrix[2] = t*x*z - s*y; matrix[3] = 0;
    matrix[4] = t*x*y - s*z;   matrix[5] = t*y*y + c;   matrix[6] = t*y*z + s*x; matrix[7] = 0;
    matrix[8] = t*x*z + s*y;   matrix[9] = t*y*z - s*x; matrix[10] = t*z*z + c;  matrix[11] = 0;
    matrix[12] = matrix[13] = matrix[14] = 0; matrix[15] = 1;
    I_GLES_MatrixMultiplyCurrent(matrix);
}

void I_GLES_Scalef(GLfloat x, GLfloat y, GLfloat z) {
    if (!g_active) { glScalef(x, y, z); return; }
    GLfloat matrix[16];
    I_GLES_MatrixIdentity(matrix);
    matrix[0] = x; matrix[5] = y; matrix[10] = z;
    I_GLES_MatrixMultiplyCurrent(matrix);
}

void I_GLES_GetFloatv(GLenum pname, GLfloat* values) {
    if (!g_active) { glGetFloatv(pname, values); return; }
    if (pname == GL_MODELVIEW_MATRIX) {
        memcpy(values, g_modelview[g_modelview_top], sizeof(GLfloat) * 16);
        return;
    }
    if (pname == GL_PROJECTION_MATRIX) {
        memcpy(values, g_projection[g_projection_top], sizeof(GLfloat) * 16);
        return;
    }
    glGetFloatv(pname, values);
}

void I_GLES_GetDoublev(GLenum pname, GLdouble* values) {
    if (!g_active) { glGetDoublev(pname, values); return; }
    if (pname == GL_MODELVIEW_MATRIX || pname == GL_PROJECTION_MATRIX) {
        GLfloat matrix[16];
        I_GLES_GetFloatv(pname, matrix);
        for (int i = 0; i < 16; ++i) values[i] = matrix[i];
        return;
    }
    GLfloat value[16] = { 0 };
    glGetFloatv(pname, value);
    values[0] = value[0];
}

static void I_GLES_AppendImmediateVertex(GLfloat x, GLfloat y, GLfloat z) {
    if (!g_inside_begin || g_immediate_vertex_count >= GLES_IMMEDIATE_VERTICES)
        return;
    vtx_t* vertex = &g_immediate_vertices[g_immediate_vertex_count++];
    vertex->x = x; vertex->y = y; vertex->z = z;
    vertex->tu = g_current_texcoord[0]; vertex->tv = g_current_texcoord[1];
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
    GLsizei n = g_immediate_vertex_count;
    g_immediate_index_count = 0;
    switch (g_immediate_mode) {
    case GL_POINTS:
        for (GLsizei i = 0; i < n; ++i) I_GLES_AddImmediateIndex((GLushort)i);
        break;
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
    default:
        for (GLsizei i = 0; i < n; ++i)
            I_GLES_AddImmediateIndex((GLushort)i);
        break;
    }
}

static void I_GLES_DrawBatch(GLenum mode, GLsizei vertexCount, const vtx_t* vertices,
                             GLsizei indexCount, const GLushort* indices) {
    if (!g_active || vertexCount <= 0 || indexCount <= 0 || !vertices || !indices)
        return;

    GLint currentProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
    if (!currentProgram) {
        I_ShaderBind();
        glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
    }
    if (!currentProgram) {
        static boolean reportedMissingShader = false;
        if (!reportedMissingShader) {
            SDL_Log("DGL GLES: skipping draw because the native shader program is not ready");
            reportedMissingShader = true;
        }
        return;
    }
    I_ShaderUpdateMatrices();

    if (g_wireframe && mode == GL_TRIANGLES) {
        GLsizei edgeCount = indexCount * 2;
        GLushort* edges = (GLushort*)SDL_malloc((size_t)edgeCount * sizeof(GLushort));
        if (edges) {
            GLsizei out = 0;
            for (GLsizei i = 0; i + 2 < indexCount; i += 3) {
                GLushort a = indices[i], b = indices[i + 1], c = indices[i + 2];
                edges[out++] = a; edges[out++] = b;
                edges[out++] = b; edges[out++] = c;
                edges[out++] = c; edges[out++] = a;
            }
            mode = GL_LINES;
            indexCount = out;
            pglBindBuffer(GL_ARRAY_BUFFER, g_vertex_buffer);
            pglBufferData(GL_ARRAY_BUFFER, (ptrdiff_t)vertexCount * sizeof(vtx_t),
                          vertices, GL_STREAM_DRAW);
            pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_index_buffer);
            pglBufferData(GL_ELEMENT_ARRAY_BUFFER, (ptrdiff_t)indexCount * sizeof(GLushort),
                          edges, GL_STREAM_DRAW);
            pglEnableVertexAttribArray(0);
            pglEnableVertexAttribArray(1);
            pglEnableVertexAttribArray(2);
            pglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vtx_t), (const void*)offsetof(vtx_t, x));
            pglVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(vtx_t), (const void*)offsetof(vtx_t, r));
            pglVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(vtx_t), (const void*)offsetof(vtx_t, tu));
            pglDrawElements(mode, indexCount, GL_UNSIGNED_SHORT, 0);
            SDL_free(edges);
            return;
        }
    }

    pglBindBuffer(GL_ARRAY_BUFFER, g_vertex_buffer);
    pglBufferData(GL_ARRAY_BUFFER, (ptrdiff_t)vertexCount * sizeof(vtx_t),
                  vertices, GL_STREAM_DRAW);
    pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_index_buffer);
    pglBufferData(GL_ELEMENT_ARRAY_BUFFER, (ptrdiff_t)indexCount * sizeof(GLushort),
                  indices, GL_STREAM_DRAW);

    /* ESSL attributes are explicitly assigned to locations 0/1/2. */
    pglEnableVertexAttribArray(0);
    pglEnableVertexAttribArray(1);
    pglEnableVertexAttribArray(2);
    pglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vtx_t), (const void*)offsetof(vtx_t, x));
    pglVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(vtx_t), (const void*)offsetof(vtx_t, r));
    pglVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(vtx_t), (const void*)offsetof(vtx_t, tu));

    pglDrawElements(mode, indexCount, GL_UNSIGNED_SHORT, 0);
}

void I_GLES_DrawIndexedGeometry(GLsizei vertexCount, const vtx_t* vertices,
                                GLsizei indexCount, const GLushort* indices) {
    if (!g_active)
        return;
    I_GLES_DrawBatch(GL_TRIANGLES, vertexCount, vertices, indexCount, indices);
}

void I_GLES_Begin(GLenum mode) {
    if (!g_active) { glBegin(mode); return; }
    if (g_inside_begin)
        I_GLES_End();
    g_inside_begin = true;
    g_immediate_mode = mode;
    g_immediate_vertex_count = 0;
    g_immediate_index_count = 0;
}

void I_GLES_End(void) {
    if (!g_active) { glEnd(); return; }
    if (!g_inside_begin)
        return;
    g_inside_begin = false;
    I_GLES_BuildImmediateIndices();
    GLenum drawMode = (g_immediate_mode == GL_LINES || g_immediate_mode == GL_LINE_STRIP ||
                       g_immediate_mode == GL_LINE_LOOP) ? GL_LINES :
                      (g_immediate_mode == GL_POINTS ? GL_POINTS : GL_TRIANGLES);
    I_GLES_DrawBatch(drawMode, g_immediate_vertex_count, g_immediate_vertices,
                     g_immediate_index_count, g_immediate_indices);
}

void I_GLES_Vertex2f(GLfloat x, GLfloat y) {
    if (!g_active) { glVertex2f(x, y); return; }
    I_GLES_AppendImmediateVertex(x, y, 0.0f);
}
void I_GLES_Vertex3f(GLfloat x, GLfloat y, GLfloat z) {
    if (!g_active) { glVertex3f(x, y, z); return; }
    I_GLES_AppendImmediateVertex(x, y, z);
}
void I_GLES_Vertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    if (!g_active) { glVertex4f(x, y, z, w); return; }
    if (fabsf(w) < 0.000001f) w = 1.0f;
    I_GLES_AppendImmediateVertex(x / w, y / w, z / w);
}
void I_GLES_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    if (!g_active) { glColor4f(r, g, b, a); return; }
    g_current_color[0] = r; g_current_color[1] = g;
    g_current_color[2] = b; g_current_color[3] = a;
}
void I_GLES_Color4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) {
    if (!g_active) { glColor4ub(r, g, b, a); return; }
    g_current_color[0] = (GLfloat)r / 255.0f;
    g_current_color[1] = (GLfloat)g / 255.0f;
    g_current_color[2] = (GLfloat)b / 255.0f;
    g_current_color[3] = (GLfloat)a / 255.0f;
}
void I_GLES_Color4ubv(const GLubyte* color) {
    if (!color) return;
    I_GLES_Color4ub(color[0], color[1], color[2], color[3]);
}
void I_GLES_TexCoord2f(GLfloat s, GLfloat t) {
    if (!g_active) { glTexCoord2f(s, t); return; }
    g_current_texcoord[0] = s; g_current_texcoord[1] = t;
}
void I_GLES_Rectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2) {
    if (!g_active) { glRectf(x1, y1, x2, y2); return; }
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

void I_GLES_SetVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer) {
    (void)size; (void)type; (void)stride; (void)pointer;
}
void I_GLES_SetColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer) {
    (void)size; (void)type; (void)stride; (void)pointer;
}
void I_GLES_SetTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer) {
    (void)size; (void)type; (void)stride; (void)pointer;
}
void I_GLES_EnableClientState(GLenum array) { (void)array; }
void I_GLES_DisableClientState(GLenum array) { (void)array; }

void I_GLES_Enable(GLenum cap) {
    if (!g_active) { glEnable(cap); return; }
    if (cap == GL_TEXTURE_2D) {
        if (g_active_texture_unit >= 0 && g_active_texture_unit < GLES_MAX_TEXTURE_UNITS)
            g_texture_enabled[g_active_texture_unit] = true;
        I_ShaderSetUseTexture(g_texture_enabled[g_active_texture_unit] ? 1 : 0);
        return;
    }
    if (cap == GL_ALPHA_TEST) {
        g_alpha_test_enabled = true;
        I_ShaderSetAlphaTest(1, g_alpha_func, g_alpha_reference);
        return;
    }
    if (cap == GL_FOG) {
        g_fog_enabled = true;
        I_GLES_CommitFog();
        return;
    }
    if (cap == GL_MULTISAMPLE)
        return;
    glEnable(cap);
}
void I_GLES_Disable(GLenum cap) {
    if (!g_active) { glDisable(cap); return; }
    if (cap == GL_TEXTURE_2D) {
        if (g_active_texture_unit >= 0 && g_active_texture_unit < GLES_MAX_TEXTURE_UNITS)
            g_texture_enabled[g_active_texture_unit] = false;
        I_ShaderSetUseTexture(g_texture_enabled[g_active_texture_unit] ? 1 : 0);
        return;
    }
    if (cap == GL_ALPHA_TEST) {
        g_alpha_test_enabled = false;
        I_ShaderSetAlphaTest(0, g_alpha_func, g_alpha_reference);
        return;
    }
    if (cap == GL_FOG) {
        g_fog_enabled = false;
        I_GLES_CommitFog();
        return;
    }
    if (cap == GL_MULTISAMPLE)
        return;
    glDisable(cap);
}
GLboolean I_GLES_IsEnabled(GLenum cap) {
    if (!g_active) return glIsEnabled(cap);
    if (cap == GL_TEXTURE_2D)
        return (g_active_texture_unit >= 0 && g_active_texture_unit < GLES_MAX_TEXTURE_UNITS)
            ? g_texture_enabled[g_active_texture_unit] : GL_FALSE;
    if (cap == GL_ALPHA_TEST) return g_alpha_test_enabled;
    if (cap == GL_FOG) return g_fog_enabled;
    return glIsEnabled(cap);
}
void I_GLES_AlphaFunc(GLenum func, GLclampf reference) {
    if (!g_active) { glAlphaFunc(func, reference); return; }
    g_alpha_func = func;
    g_alpha_reference = reference;
    I_ShaderSetAlphaTest(g_alpha_test_enabled, g_alpha_func, g_alpha_reference);
}
void I_GLES_DepthRange(GLclampf nearVal, GLclampf farVal) {
    if (!g_active) { glDepthRange(nearVal, farVal); return; }
    if (pglDepthRangef) pglDepthRangef(nearVal, farVal);
}
void I_GLES_ClearDepth(GLclampd depth) {
    if (!g_active) { glClearDepth(depth); return; }
    if (pglClearDepthf) pglClearDepthf((GLfloat)depth);
}

void I_GLES_Fogf(GLenum pname, GLfloat param) {
    if (!g_active) { glFogf(pname, param); return; }
    if (pname == GL_FOG_START) g_fog_start = param;
    else if (pname == GL_FOG_END) g_fog_end = param;
    else if (pname == GL_FOG_DENSITY) g_fog_density = param;
    I_GLES_CommitFog();
}
void I_GLES_Fogfv(GLenum pname, const GLfloat* params) {
    if (!g_active) { glFogfv(pname, params); return; }
    if (pname == GL_FOG_COLOR && params) {
        g_fog_color[0] = params[0]; g_fog_color[1] = params[1];
        g_fog_color[2] = params[2]; g_fog_color[3] = params[3];
    }
    I_GLES_CommitFog();
}
void I_GLES_Fogi(GLenum pname, GLint param) {
    if (!g_active) { glFogi(pname, param); return; }
    if (pname == GL_FOG_MODE) g_fog_mode = I_GLES_FogMode(param);
    I_GLES_CommitFog();
}

static GLint I_GLES_NormalizeCombinerSource(GLint source) {
    /* GL_TEXTURE0_ARB .. GL_TEXTURE7_ARB are legacy stage selectors.
       The native GLES shader samples the currently active texture unit. */
    if (source >= 0x84C0 && source < 0x84C8)
        return GL_TEXTURE;
    return source;
}

void I_GLES_TexEnvi(GLenum target, GLenum pname, GLint param) {
    if (!g_active) { glTexEnvi(target, pname, param); return; }
    if (target != GL_TEXTURE_ENV)
        return;
    switch (pname) {
    case GL_TEXTURE_ENV_MODE:
        if (param != GL_COMBINE) {
            /* Legacy GL_TEXTURE_ENV_MODE controls both RGB and alpha
               equations and resets their default argument ordering. */
            I_SectorCombiner_SetCombineRGB(param);
            /* Fixed-function GL_ADD affects RGB only; alpha remains MODULATE.
               GL_REPLACE is the only legacy mode here that replaces alpha. */
            I_SectorCombiner_SetCombineAlpha(param == GL_REPLACE ? GL_REPLACE : GL_MODULATE);
            I_SectorCombiner_SetSourceRGB(0, GL_TEXTURE);
            I_SectorCombiner_SetOperandRGB(0, GL_SRC_COLOR);
            I_SectorCombiner_SetSourceRGB(1, 0x8578); /* GL_PREVIOUS */
            I_SectorCombiner_SetOperandRGB(1, GL_SRC_COLOR);
            I_SectorCombiner_SetSourceAlpha(0, GL_TEXTURE);
            I_SectorCombiner_SetOperandAlpha(0, 0x0302); /* GL_SRC_ALPHA */
            I_SectorCombiner_SetSourceAlpha(1, 0x8578); /* GL_PREVIOUS */
            I_SectorCombiner_SetOperandAlpha(1, 0x0302); /* GL_SRC_ALPHA */
        }
        break;
    case GL_COMBINE_RGB:
        I_SectorCombiner_SetCombineRGB(param);
        break;
    case GL_COMBINE_ALPHA:
        I_SectorCombiner_SetCombineAlpha(param);
        break;
    case GL_SOURCE0_RGB: I_SectorCombiner_SetSourceRGB(0, I_GLES_NormalizeCombinerSource(param)); break;
    case GL_SOURCE1_RGB: I_SectorCombiner_SetSourceRGB(1, I_GLES_NormalizeCombinerSource(param)); break;
    case GL_SOURCE2_RGB: I_SectorCombiner_SetSourceRGB(2, I_GLES_NormalizeCombinerSource(param)); break;
    case GL_OPERAND0_RGB: I_SectorCombiner_SetOperandRGB(0, param); break;
    case GL_OPERAND1_RGB: I_SectorCombiner_SetOperandRGB(1, param); break;
    case GL_OPERAND2_RGB: I_SectorCombiner_SetOperandRGB(2, param); break;
    case GL_SOURCE0_ALPHA: I_SectorCombiner_SetSourceAlpha(0, I_GLES_NormalizeCombinerSource(param)); break;
    case GL_SOURCE1_ALPHA: I_SectorCombiner_SetSourceAlpha(1, I_GLES_NormalizeCombinerSource(param)); break;
    case GL_SOURCE2_ALPHA: I_SectorCombiner_SetSourceAlpha(2, I_GLES_NormalizeCombinerSource(param)); break;
    case GL_OPERAND0_ALPHA: I_SectorCombiner_SetOperandAlpha(0, param); break;
    case GL_OPERAND1_ALPHA: I_SectorCombiner_SetOperandAlpha(1, param); break;
    case GL_OPERAND2_ALPHA: I_SectorCombiner_SetOperandAlpha(2, param); break;
    case GL_RGB_SCALE:
        g_rgb_scale = (GLfloat)param;
        I_ShaderSetColorScale(g_rgb_scale);
        break;
    default:
        break;
    }
}
void I_GLES_TexEnvfv(GLenum target, GLenum pname, const GLfloat* params) {
    if (!g_active) { glTexEnvfv(target, pname, params); return; }
    if (target == GL_TEXTURE_ENV && pname == GL_TEXTURE_ENV_COLOR && params)
        I_SectorCombiner_SetEnvColor(params[0], params[1], params[2], params[3]);
}

static GLint I_GLES_NormalizeWrap(GLint param) {
    return param == GL_CLAMP ? GL_CLAMP_TO_EDGE : param;
}

void I_GLES_TexParameteri(GLenum target, GLenum pname, GLint param) {
    if (!g_active) { glTexParameteri(target, pname, param); return; }
    if (I_GetGLESVersion() == 2 &&
        (pname == GL_TEXTURE_BASE_LEVEL || pname == GL_TEXTURE_MAX_LEVEL))
        return;
    if (pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T)
        param = I_GLES_NormalizeWrap(param);
    if (pname == GL_TEXTURE_MAX_ANISOTROPY_EXT &&
        !SDL_GL_ExtensionSupported("GL_EXT_texture_filter_anisotropic"))
        return;
    glTexParameteri(target, pname, param);
}
void I_GLES_TexParameterf(GLenum target, GLenum pname, GLfloat param) {
    if (!g_active) { glTexParameterf(target, pname, param); return; }
    if (pname == GL_TEXTURE_MAX_ANISOTROPY_EXT &&
        !SDL_GL_ExtensionSupported("GL_EXT_texture_filter_anisotropic"))
        return;
    glTexParameterf(target, pname, param);
}

static void I_GLES_ConvertLegacyPixels(GLenum format, GLsizei width, GLsizei height,
                                       GLenum type, const GLvoid* source,
                                       GLenum* outFormat, GLubyte** converted) {
    *outFormat = format;
    *converted = NULL;
    if (!g_active || I_GetGLESVersion() < 3 || type != GL_UNSIGNED_BYTE || !source)
        return;
    if (format != GL_LUMINANCE && format != GL_ALPHA && format != GL_LUMINANCE_ALPHA)
        return;

    size_t pixels = (size_t)width * (size_t)height;
    size_t stride = format == GL_LUMINANCE_ALPHA ? 2 : 1;
    GLubyte* rgba = (GLubyte*)SDL_malloc(pixels * 4);
    if (!rgba)
        return;
    const GLubyte* input = (const GLubyte*)source;
    for (size_t i = 0; i < pixels; ++i) {
        GLubyte l = input[i * stride];
        if (format == GL_ALPHA) {
            rgba[i * 4 + 0] = 255; rgba[i * 4 + 1] = 255;
            rgba[i * 4 + 2] = 255; rgba[i * 4 + 3] = l;
        } else {
            rgba[i * 4 + 0] = l; rgba[i * 4 + 1] = l;
            rgba[i * 4 + 2] = l;
            rgba[i * 4 + 3] = format == GL_LUMINANCE_ALPHA ? input[i * stride + 1] : 255;
        }
    }
    *outFormat = GL_RGBA;
    *converted = rgba;
}

void I_GLES_TexImage2D(GLenum target, GLint level, GLint internalFormat,
                       GLsizei width, GLsizei height, GLint border,
                       GLenum format, GLenum type, const GLvoid* pixels) {
    if (!g_active) {
        glTexImage2D(target, level, internalFormat, width, height, border, format, type, pixels);
        return;
    }
    GLenum uploadFormat = format;
    GLubyte* converted = NULL;
    I_GLES_ConvertLegacyPixels(format, width, height, type, pixels, &uploadFormat, &converted);
    if (I_GetGLESVersion() == 2) {
        if (internalFormat == GL_RGBA8) internalFormat = GL_RGBA;
        else if (internalFormat == GL_RGB8) internalFormat = GL_RGB;
    }
    if (converted) {
        internalFormat = GL_RGBA;
        glTexImage2D(target, level, internalFormat, width, height, border,
                     uploadFormat, GL_UNSIGNED_BYTE, converted);
        SDL_free(converted);
    } else {
        glTexImage2D(target, level, internalFormat, width, height, border,
                     uploadFormat, type, pixels);
    }
    if (target == GL_TEXTURE_2D) {
        texture_info_t* info = I_GLES_FindTextureInfo(I_GLES_BoundTexture(), true);
        if (info) {
            info->width = width;
            info->height = height;
            info->format = uploadFormat;
        }
    }
}
void I_GLES_TexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                          GLsizei width, GLsizei height, GLenum format,
                          GLenum type, const GLvoid* pixels) {
    if (!g_active) {
        glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
        return;
    }
    GLenum uploadFormat = format;
    GLubyte* converted = NULL;
    I_GLES_ConvertLegacyPixels(format, width, height, type, pixels, &uploadFormat, &converted);
    if (converted) {
        glTexSubImage2D(target, level, xoffset, yoffset, width, height,
                        uploadFormat, GL_UNSIGNED_BYTE, converted);
        SDL_free(converted);
    } else {
        glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
    }
}

void I_GLES_ActiveTexture(GLenum texture) {
    if (!g_active) { glActiveTexture(texture); return; }
    pglActiveTexture(texture);
    g_active_texture_unit = (int)(texture - GL_TEXTURE0);
    if (g_active_texture_unit < 0 || g_active_texture_unit >= GLES_MAX_TEXTURE_UNITS)
        g_active_texture_unit = 0;
    I_ShaderSetTextureUnit(g_active_texture_unit);
    I_ShaderSetUseTexture(g_texture_enabled[g_active_texture_unit] ? 1 : 0);
}
void I_GLES_BindTexture(GLenum target, GLuint texture) {
    glBindTexture(target, texture);
    if (g_active && target == GL_TEXTURE_2D)
        g_bound_texture[g_active_texture_unit] = texture;
}
void I_GLES_GenTextures(GLsizei count, GLuint* textures) {
    glGenTextures(count, textures);
}
void I_GLES_DeleteTextures(GLsizei count, const GLuint* textures) {
    glDeleteTextures(count, textures);
    if (!g_active || !textures) return;
    for (GLsizei i = 0; i < count; ++i) {
        for (int u = 0; u < GLES_MAX_TEXTURE_UNITS; ++u)
            if (g_bound_texture[u] == textures[i]) g_bound_texture[u] = 0;
        texture_info_t* info = I_GLES_FindTextureInfo(textures[i], false);
        if (info) memset(info, 0, sizeof(*info));
    }
}

void I_GLES_GetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint* params) {
    if (!g_active) { glGetTexLevelParameteriv(target, level, pname, params); return; }
    if (target == GL_TEXTURE_2D && level == 0 && params) {
        texture_info_t* info = I_GLES_FindTextureInfo(I_GLES_BoundTexture(), false);
        if (pname == GL_TEXTURE_WIDTH) { *params = info ? info->width : 0; return; }
        if (pname == GL_TEXTURE_HEIGHT) { *params = info ? info->height : 0; return; }
    }
    if (params) *params = 0;
}
void I_GLES_GetTexParameteriv(GLenum target, GLenum pname, GLint* params) {
    glGetTexParameteriv(target, pname, params);
}

void I_GLES_GetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid* pixels) {
    if (!g_active) { glGetTexImage(target, level, format, type, pixels); return; }
    if (target != GL_TEXTURE_2D || level != 0 || !pixels ||
        !g_readback_framebuffer || !pglBindFramebuffer || !pglFramebufferTexture2D ||
        !pglCheckFramebufferStatus) {
        SDL_Log("DGL GLES: texture readback unavailable for requested target/level");
        return;
    }
    texture_info_t* info = I_GLES_FindTextureInfo(I_GLES_BoundTexture(), false);
    if (!info || info->width <= 0 || info->height <= 0)
        return;

    GLint previousFramebuffer = 0;
    GLint previousPackAlignment = 4;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
    pglBindFramebuffer(GL_FRAMEBUFFER, g_readback_framebuffer);
    pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target,
                            I_GLES_BoundTexture(), level);
    GLenum status = pglCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status == GL_FRAMEBUFFER_COMPLETE) {
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, info->width, info->height, format, type, pixels);
        glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
    } else {
        SDL_Log("DGL GLES: texture readback framebuffer incomplete (0x%x)", status);
    }
    pglBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
}

void I_GLES_GetIntegerv(GLenum pname, GLint* params) {
    if (!g_active) { glGetIntegerv(pname, params); return; }
    if (!params) return;
    if (pname == GL_MAX_TEXTURE_UNITS_ARB) {
        GLint maxUnits = 1;
        glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxUnits);
        *params = maxUnits < 4 ? maxUnits : 4;
        return;
    }
    if (pname == GL_BLEND_SRC) {
        glGetIntegerv(GL_BLEND_SRC_RGB, params);
        return;
    }
    if (pname == GL_BLEND_DST) {
        glGetIntegerv(GL_BLEND_DST_RGB, params);
        return;
    }
    if (pname == GL_MODELVIEW_STACK_DEPTH) { *params = g_modelview_top + 1; return; }
    if (pname == GL_PROJECTION_STACK_DEPTH) { *params = g_projection_top + 1; return; }
    glGetIntegerv(pname, params);
}
void I_GLES_GetBooleanv(GLenum pname, GLboolean* params) {
    if (!g_active) { glGetBooleanv(pname, params); return; }
    if (!params) return;
    if (pname == GL_TEXTURE_2D || pname == GL_ALPHA_TEST || pname == GL_FOG) {
        *params = I_GLES_IsEnabled(pname);
        return;
    }
    glGetBooleanv(pname, params);
}
void I_GLES_GetFloatvRaw(GLenum pname, GLfloat* params) {
    if (!g_active) { glGetFloatv(pname, params); return; }
    if (pname == GL_MODELVIEW_MATRIX || pname == GL_PROJECTION_MATRIX) {
        I_GLES_GetFloatv(pname, params);
        return;
    }
    if (pname == GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT &&
        !SDL_GL_ExtensionSupported("GL_EXT_texture_filter_anisotropic")) {
        *params = 1.0f;
        return;
    }
    glGetFloatv(pname, params);
}

void I_GLES_PolygonMode(GLenum face, GLenum mode) {
    if (!g_active) { glPolygonMode(face, mode); return; }
    if (face == GL_FRONT_AND_BACK || face == GL_FRONT || face == GL_BACK)
        g_wireframe = (mode == GL_LINE);
}
void I_GLES_PolygonOffset(GLfloat factor, GLfloat units) {
    if (!g_active) { glPolygonOffset(factor, units); return; }
    glPolygonOffset(factor, units);
}
void I_GLES_LineWidth(GLfloat width) {
    if (!g_active) { glLineWidth(width); return; }
    glLineWidth(width);
}
void I_GLES_ShadeModel(GLenum mode) {
    if (!g_active) { glShadeModel(mode); return; }
    (void)mode;
}
void I_GLES_Hint(GLenum target, GLenum mode) {
    if (!g_active) { glHint(target, mode); return; }
    (void)target; (void)mode;
}
