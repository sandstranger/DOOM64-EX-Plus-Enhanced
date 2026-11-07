// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// Copyright(C) 2025 Gibbon
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//      Texture Combiner -> Shaders (Glue)
//
//-----------------------------------------------------------------------------

#include <SDL3/SDL.h>
#ifndef ANDROID
#include <SDL3/SDL_opengl.h>
#else
#include "glad.h"
#endif
#include <string.h>
#include <math.h>

#include "i_sectorcombiner.h"
#include "i_video.h"

#ifndef COUNTOF
#define COUNTOF(a) ((int)(sizeof(a)/sizeof((a)[0])))
#endif

#define MAX_PASSES 4
#define SC_EPS 1e-5f

// texture combiner to GLSL glue...
static GLint(APIENTRY* pglGetUniformLocation)(GLuint, const GLchar*) = NULL;
static void   (APIENTRY* pglUniform1i)(GLint, int) = NULL;
static void   (APIENTRY* pglUniform1f)(GLint, float) = NULL;
static void   (APIENTRY* pglUniform2f)(GLint, float, float) = NULL;
static void   (APIENTRY* pglUniform3f)(GLint, float, float, float) = NULL;
static void   (APIENTRY* pglUniform4f)(GLint, float, float, float, float) = NULL;

static int g_procs_loaded = 0;

static void load_glsl_procs(void) {
    if (g_procs_loaded) return;
#define GL_GET(fn) *(void**)(&p##fn) = SDL_GL_GetProcAddress(#fn)
    GL_GET(glGetUniformLocation);
    GL_GET(glUniform1i);
    GL_GET(glUniform1f);
    GL_GET(glUniform2f);
    GL_GET(glUniform3f);
    GL_GET(glUniform4f);
#undef GL_GET
    g_procs_loaded = pglGetUniformLocation && pglUniform1i && pglUniform1f && pglUniform2f && pglUniform3f && pglUniform4f;
}


// atsb: this was 's' for 'shaders', I knew it would be a pain to do this so less typing is better...
typedef struct {
    int   combine_rgb;
    int   combine_alpha;
    int   source_rgb[3];
    int   operand_rgb[3];
    int   source_alpha[3];
    int   operand_alpha[3];
    float env_color[4];

    int   pass_mode[MAX_PASSES];
    float pass_color[MAX_PASSES][4];
    float pass_factor[MAX_PASSES];
    int   pass_count;

    int   fog_enabled;
    float fog_color[3];
    float fog_factor;

    int   fog_mode;
    float fog_start;
    float fog_end;
    float fog_density;

    int   use_texture;
    int   tex_w, tex_h;
} sector_combiner_state_t;

/* Desktop/GL4ES retain their original single-state shader path. Native GLES
   keeps a separate fixed-function texture environment state for each unit. */
static sector_combiner_state_t s;
static sector_combiner_state_t s_native[4];
static int s_active_unit = 0;

static sector_combiner_state_t* I_SectorCombinerCurrentState(void) {
    return I_GetGLESVersion() != 0 ? &s_native[s_active_unit] : &s;
}

static void reset_state(sector_combiner_state_t* state, boolean nativeGLES) {
    memset(state, 0, sizeof(*state));
    state->combine_rgb = GL_MODULATE;
    state->combine_alpha = GL_MODULATE;
    for (int i = 0; i < 3; i++) {
        state->source_rgb[i] = nativeGLES
            ? (i == 0 ? GL_TEXTURE : (i == 1 ? 0x8578 : GL_CONSTANT))
            : GL_TEXTURE;
        state->operand_rgb[i] = GL_SRC_COLOR;
        state->source_alpha[i] = (i == 0) ? GL_TEXTURE : 0x8578;
        state->operand_alpha[i] = 0x0302;
    }
    state->env_color[3] = 1.0f;
    state->use_texture = 1;
}

// atsb: we pass multiple times over, so we mimic the same way texture combiners make multiple passes
static void I_SectorCombinerTexturePasses(sector_combiner_state_t* state) {
    state->pass_count = 0;
    float src0[3] = { 1,1,1 };
    float src1[3] = { 1,1,1 };

    if (state->source_rgb[0] == GL_CONSTANT) {
        src0[0] = state->env_color[0]; src0[1] = state->env_color[1]; src0[2] = state->env_color[2];
        if (state->operand_rgb[0] == GL_ONE_MINUS_SRC_COLOR) {
            src0[0] = 1.0f - src0[0]; src0[1] = 1.0f - src0[1]; src0[2] = 1.0f - src0[2];
        }
    }
    if (state->source_rgb[1] == GL_CONSTANT) {
        src1[0] = state->env_color[0]; src1[1] = state->env_color[1]; src1[2] = state->env_color[2];
        if (state->operand_rgb[1] == GL_ONE_MINUS_SRC_COLOR) {
            src1[0] = 1.0f - src1[0]; src1[1] = 1.0f - src1[1]; src1[2] = 1.0f - src1[2];
        }
    }

    switch (state->combine_rgb) {
    case GL_MODULATE:
    case GL_ADD:
    case GL_REPLACE:
    default:
        state->pass_mode[state->pass_count] = state->combine_rgb;
        state->pass_color[state->pass_count][0] = src1[0];
        state->pass_color[state->pass_count][1] = src1[1];
        state->pass_color[state->pass_count][2] = src1[2];
        state->pass_color[state->pass_count][3] = 1.0f;
        state->pass_factor[state->pass_count] = 1.0f;
        state->pass_count++;
        break;
    case GL_INTERPOLATE: {
        float fac = (src1[0] + src1[1] + src1[2]) / 3.0f;
        state->pass_mode[state->pass_count] = GL_INTERPOLATE;
        state->pass_color[state->pass_count][0] = src0[0];
        state->pass_color[state->pass_count][1] = src0[1];
        state->pass_color[state->pass_count][2] = src0[2];
        state->pass_color[state->pass_count][3] = 1.0f;
        state->pass_factor[state->pass_count] = fac;
        state->pass_count++;
        break;
    }
    }
}

static void I_SectorCombinerLegacyUniforms(void) {
    if (!g_procs_loaded)
        return;
    GLint curProg = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &curProg);
    if (curProg == 0)
        return;

    GLint locPassCount = pglGetUniformLocation(curProg, "uPassCount");
    GLint locFogEn = pglGetUniformLocation(curProg, "uFogEnabled");
    GLint locFogCol = pglGetUniformLocation(curProg, "uFogColor");
    GLint locFogFac = pglGetUniformLocation(curProg, "uFogFactor");
    GLint locFogMode = pglGetUniformLocation(curProg, "uFogMode");
    GLint locFogStart = pglGetUniformLocation(curProg, "uFogStart");
    GLint locFogEnd = pglGetUniformLocation(curProg, "uFogEnd");
    GLint locFogDensity = pglGetUniformLocation(curProg, "uFogDensity");

    if (locPassCount != -1) pglUniform1i(locPassCount, s.pass_count);
    for (int i = 0; i < s.pass_count && i < MAX_PASSES; i++) {
        char n1[32], n2[32], n3[32];
        SDL_snprintf(n1, sizeof(n1), "uPassMode[%d]", i);
        SDL_snprintf(n2, sizeof(n2), "uPassColor[%d]", i);
        SDL_snprintf(n3, sizeof(n3), "uPassFactor[%d]", i);
        GLint m = pglGetUniformLocation(curProg, n1);
        GLint c = pglGetUniformLocation(curProg, n2);
        GLint f = pglGetUniformLocation(curProg, n3);
        if (m != -1) pglUniform1i(m, s.pass_mode[i]);
        if (c != -1) pglUniform4f(c, s.pass_color[i][0], s.pass_color[i][1], s.pass_color[i][2], s.pass_color[i][3]);
        if (f != -1) pglUniform1f(f, s.pass_factor[i]);
    }
    if (locFogEn != -1) pglUniform1i(locFogEn, s.fog_enabled ? 1 : 0);
    if (locFogCol != -1) pglUniform3f(locFogCol, s.fog_color[0], s.fog_color[1], s.fog_color[2]);
    if (locFogFac != -1) pglUniform1f(locFogFac, s.fog_factor);
    if (locFogMode != -1) pglUniform1i(locFogMode, s.fog_mode);
    if (locFogStart != -1) pglUniform1f(locFogStart, s.fog_start);
    if (locFogEnd != -1) pglUniform1f(locFogEnd, s.fog_end);
    if (locFogDensity != -1) pglUniform1f(locFogDensity, s.fog_density);
}

static void I_SectorCombinerNativeUniforms(void) {
    if (!g_procs_loaded)
        return;
    GLint curProg = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &curProg);
    if (curProg == 0)
        return;

    GLint loc = pglGetUniformLocation(curProg, "uFogEnabled");
    if (loc != -1) pglUniform1i(loc, s_native[0].fog_enabled ? 1 : 0);
    loc = pglGetUniformLocation(curProg, "uFogColor");
    if (loc != -1) pglUniform3f(loc, s_native[0].fog_color[0], s_native[0].fog_color[1], s_native[0].fog_color[2]);
    loc = pglGetUniformLocation(curProg, "uFogFactor");
    if (loc != -1) pglUniform1f(loc, s_native[0].fog_factor);
    loc = pglGetUniformLocation(curProg, "uFogMode");
    if (loc != -1) pglUniform1i(loc, s_native[0].fog_mode);
    loc = pglGetUniformLocation(curProg, "uFogStart");
    if (loc != -1) pglUniform1f(loc, s_native[0].fog_start);
    loc = pglGetUniformLocation(curProg, "uFogEnd");
    if (loc != -1) pglUniform1f(loc, s_native[0].fog_end);
    loc = pglGetUniformLocation(curProg, "uFogDensity");
    if (loc != -1) pglUniform1f(loc, s_native[0].fog_density);

    for (int unit = 0; unit < 4; ++unit) {
        sector_combiner_state_t* state = &s_native[unit];
        char name[48];
        SDL_snprintf(name, sizeof(name), "uCombineRGB%d", unit);
        loc = pglGetUniformLocation(curProg, name);
        if (loc != -1) pglUniform1i(loc, state->combine_rgb);
        SDL_snprintf(name, sizeof(name), "uCombineAlpha%d", unit);
        loc = pglGetUniformLocation(curProg, name);
        if (loc != -1) pglUniform1i(loc, state->combine_alpha);
        SDL_snprintf(name, sizeof(name), "uEnvColor%d", unit);
        loc = pglGetUniformLocation(curProg, name);
        if (loc != -1) pglUniform4f(loc, state->env_color[0], state->env_color[1], state->env_color[2], state->env_color[3]);

        for (int slot = 0; slot < 3; ++slot) {
            SDL_snprintf(name, sizeof(name), "uRGBSource%d_%d", unit, slot);
            loc = pglGetUniformLocation(curProg, name);
            if (loc != -1) pglUniform1i(loc, state->source_rgb[slot]);
            SDL_snprintf(name, sizeof(name), "uRGBOperand%d_%d", unit, slot);
            loc = pglGetUniformLocation(curProg, name);
            if (loc != -1) pglUniform1i(loc, state->operand_rgb[slot]);
            SDL_snprintf(name, sizeof(name), "uAlphaSource%d_%d", unit, slot);
            loc = pglGetUniformLocation(curProg, name);
            if (loc != -1) pglUniform1i(loc, state->source_alpha[slot]);
            SDL_snprintf(name, sizeof(name), "uAlphaOperand%d_%d", unit, slot);
            loc = pglGetUniformLocation(curProg, name);
            if (loc != -1) pglUniform1i(loc, state->operand_alpha[slot]);
        }
    }
}

static void I_SectorCombinerUniforms(void) {
    if (I_GetGLESVersion() != 0)
        I_SectorCombinerNativeUniforms();
    else
        I_SectorCombinerLegacyUniforms();
}

void I_SectorCombiner_Init(void) {
    load_glsl_procs();
    s_active_unit = 0;
    reset_state(&s, false);
    for (int unit = 0; unit < 4; ++unit)
        reset_state(&s_native[unit], true);
}

int  I_SectorCombiner_IsReady(void) { 
    return g_procs_loaded; 
}
void I_SectorCombiner_Bind(int useTex, int w, int h) {
    sector_combiner_state_t* state = I_SectorCombinerCurrentState();
    state->use_texture = useTex ? 1 : 0;
    state->tex_w = w;
    state->tex_h = h;
    if (I_GetGLESVersion() == 0)
        I_SectorCombinerTexturePasses(&s);
    I_SectorCombinerUniforms();
}

void I_SectorCombiner_Unbind(void) { /* no-op */ }

void I_SectorCombiner_SetFogParams(int mode, float start, float end, float density) {
    if (I_GetGLESVersion() != 0) {
        for (int unit = 0; unit < 4; ++unit) {
            s_native[unit].fog_mode = mode;
            s_native[unit].fog_start = start;
            s_native[unit].fog_end = end;
            s_native[unit].fog_density = density;
        }
    } else {
        s.fog_mode = mode;
        s.fog_start = start;
        s.fog_end = end;
        s.fog_density = density;
    }
    I_SectorCombinerUniforms();
}

void I_SectorCombiner_SetEnvColor(float r, float g, float b, float a) {
    sector_combiner_state_t* state = I_SectorCombinerCurrentState();
    state->env_color[0] = r;
    state->env_color[1] = g;
    state->env_color[2] = b;
    state->env_color[3] = a;
    if (I_GetGLESVersion() == 0)
        I_SectorCombinerTexturePasses(&s);
    I_SectorCombinerUniforms();
}

void I_SectorCombiner_SetCombineRGB(int mode) {
    sector_combiner_state_t* state = I_SectorCombinerCurrentState();
    state->combine_rgb = mode;
    if (I_GetGLESVersion() == 0)
        I_SectorCombinerTexturePasses(&s);
    I_SectorCombinerUniforms();
}

void I_SectorCombiner_SetCombineAlpha(int mode) {
    I_SectorCombinerCurrentState()->combine_alpha = mode;
    I_SectorCombinerUniforms();
}

void I_SectorCombiner_SetSourceAlpha(int slot, int src) {
    sector_combiner_state_t* state = I_SectorCombinerCurrentState();
    if (slot >= 0 && slot < 3)
        state->source_alpha[slot] = src;
    I_SectorCombinerUniforms();
}

void I_SectorCombiner_SetOperandAlpha(int slot, int op) {
    sector_combiner_state_t* state = I_SectorCombinerCurrentState();
    if (slot >= 0 && slot < 3)
        state->operand_alpha[slot] = op;
    I_SectorCombinerUniforms();
}

void I_SectorCombiner_SetSourceRGB(int slot, int src) {
    sector_combiner_state_t* state = I_SectorCombinerCurrentState();
    if (slot >= 0 && slot < 3)
        state->source_rgb[slot] = src;
    if (I_GetGLESVersion() == 0)
        I_SectorCombinerTexturePasses(&s);
    I_SectorCombinerUniforms();
}

void I_SectorCombiner_SetOperandRGB(int slot, int op) {
    sector_combiner_state_t* state = I_SectorCombinerCurrentState();
    if (slot >= 0 && slot < 3)
        state->operand_rgb[slot] = op;
    if (I_GetGLESVersion() == 0)
        I_SectorCombinerTexturePasses(&s);
    I_SectorCombinerUniforms();
}

void I_SectorCombiner_SetFog(int en, float r, float g, float b, float fac) {
    if (I_GetGLESVersion() != 0) {
        for (int unit = 0; unit < 4; ++unit) {
            s_native[unit].fog_enabled = en ? 1 : 0;
            s_native[unit].fog_color[0] = r;
            s_native[unit].fog_color[1] = g;
            s_native[unit].fog_color[2] = b;
            s_native[unit].fog_factor = fac;
        }
    } else {
        s.fog_enabled = en ? 1 : 0;
        s.fog_color[0] = r;
        s.fog_color[1] = g;
        s.fog_color[2] = b;
        s.fog_factor = fac;
    }
    I_SectorCombinerUniforms();
}

void I_SectorCombiner_Commit(void) {
    if (I_GetGLESVersion() == 0)
        I_SectorCombinerTexturePasses(&s);
    I_SectorCombinerUniforms();
}

void I_SectorCombiner_SetActiveUnit(int unit) {
    if (unit < 0) unit = 0;
    if (unit > 3) unit = 3;
    s_active_unit = unit;
    if (I_GetGLESVersion() != 0)
        I_SectorCombinerUniforms();
}

