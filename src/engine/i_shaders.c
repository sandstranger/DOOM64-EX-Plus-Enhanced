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
//      Shaders
//
//-----------------------------------------------------------------------------

#include <SDL3/SDL.h>
#include "doomstat.h"
#include "con_cvar.h"
#include "i_shaders.h"
#include "dgl.h"
#include "i_video.h"

#ifndef GL_MODELVIEW_MATRIX
#define GL_MODELVIEW_MATRIX 0x0BA6
#endif
#ifndef GL_PROJECTION_MATRIX
#define GL_PROJECTION_MATRIX 0x0BA7
#endif

extern void I_SectorCombiner_Commit(void);

CVAR_EXTERNAL(r_filter);

static GLint sLocTexel = -1;
static GLint sLocUseTex = -1;
static int g_shader_texture_unit = 0;
static GLint sLocAlphaTestEnabled = -1;
static GLint sLocAlphaFunc = -1;
static GLint sLocAlphaRef = -1;
static GLint sLocColorScale = -1;
static GLint g_alpha_test_enabled = 0;
static GLint g_alpha_func = 518;
static GLfloat g_alpha_ref = 0.0f;
static GLfloat g_color_scale = 1.0f;
static GLint g_use_texture = 1;

static GLuint generic_tint_overlay_prog = 0;

static GLint sLocPassCount = -1;
static GLint sLocPassMode[4] = { -1,-1,-1,-1 };
static GLint sLocPassColor[4] = { -1,-1,-1,-1 };
static GLint sLocPassFactor[4] = { -1,-1,-1,-1 };
static GLint sLocFogEnabled = -1, sLocFogColor = -1, sLocFogFactor = -1;

typedef struct {
	int combine_rgb;
	int combine_alpha;
	int source_rgb[3];
	int operand_rgb[3];
	float env_color[4];
	int pass_mode[4];
	float pass_color[4][4];
	float pass_factor[4];
	int pass_count;
	int fog_enabled; float fog_color[3]; float fog_factor;
} comb_state_t;

static struct {
	GLuint prog;
	GLint  locTex;
	GLint  locModelView;
	GLint  locModelViewProjection;
	int    initialised;
} shader_struct = { 0, -1, -1, -1, 0 };

static comb_state_t gComb;
static GLint  generic_tint_overlay_colour = -1;
static GLint  generic_tint_overlay_mvp = -1;

/* this is where shaders are defined */

static GLuint(APIENTRY* pglCreateShader)(GLenum);
static void   (APIENTRY* pglShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
static void   (APIENTRY* pglCompileShader)(GLuint);
static void   (APIENTRY* pglGetShaderiv)(GLuint, GLenum, GLint*);
static void   (APIENTRY* pglGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
static GLuint(APIENTRY* pglCreateProgram)(void);
static void   (APIENTRY* pglAttachShader)(GLuint, GLuint);
static void   (APIENTRY* pglLinkProgram)(GLuint);
static void   (APIENTRY* pglGetProgramiv)(GLuint, GLenum, GLint*);
static void   (APIENTRY* pglGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
static void   (APIENTRY* pglUseProgram)(GLuint);
static GLint(APIENTRY* pglGetUniformLocation)(GLuint, const GLchar*);
static void   (APIENTRY* pglUniform1i)(GLint, int);
static void   (APIENTRY* pglUniform2f)(GLint, float, float);
static void   (APIENTRY* pglUniform1f)(GLint, float);
static void   (APIENTRY* pglUniform3f)(GLint, float, float, float);
static void   (APIENTRY* pglUniform4f)(GLint, float, float, float, float);
static void   (APIENTRY* pglUniformMatrix4fv)(GLint, GLsizei, unsigned char, const float*);
static void   (APIENTRY* pglBindAttribLocation)(GLuint, GLuint, const GLchar*);

static int is_glsl_loaded = 0;
static GLuint is_current_prog = 0;

static void I_ShaderLoad(void) {
	if (is_glsl_loaded) 
		return;
#define GL_GET(fn) *(void**)(&p##fn) = SDL_GL_GetProcAddress(#fn)
	GL_GET(glCreateShader);  GL_GET(glShaderSource);
	GL_GET(glCompileShader); GL_GET(glGetShaderiv);
	GL_GET(glGetShaderInfoLog);
	GL_GET(glCreateProgram); GL_GET(glAttachShader);
	GL_GET(glLinkProgram);   GL_GET(glGetProgramiv);
	GL_GET(glGetProgramInfoLog);
	GL_GET(glUseProgram);    GL_GET(glGetUniformLocation);
	GL_GET(glUniform1i);
	GL_GET(glUniform2f);
	GL_GET(glUniform1f);
	GL_GET(glUniform3f);
	GL_GET(glUniform4f);
	GL_GET(glUniformMatrix4fv);
	GL_GET(glBindAttribLocation);
#undef GL_GET

	if (!pglCreateShader || !pglShaderSource || !pglCompileShader ||
		!pglGetShaderiv || !pglGetShaderInfoLog || !pglCreateProgram ||
		!pglAttachShader || !pglLinkProgram || !pglGetProgramiv ||
		!pglGetProgramInfoLog || !pglUseProgram || !pglGetUniformLocation ||
		!pglUniform1i || !pglUniform2f || !pglUniform1f ||
		!pglUniform3f || !pglUniform4f ||
		!pglUniformMatrix4fv || !pglBindAttribLocation) {
#ifdef ANDROID
		SDL_Log("I_ShaderLoad: required shader entry points are unavailable");
#else
		fprintf(stderr, "I_ShaderLoad: required shader entry points are unavailable\\n");
#endif
		return;
	}

	is_glsl_loaded = 1;
}

/* BILATERAL AND N64 SHADERS
============================
*/

static const char* vertex_shader_bilateral =
"#version 120\n\n"
"varying vec2 vUV;\n"
"varying vec4 vColor;\nvarying float vEyeDist;\n"
"uniform int   uPassCount;\n"
"uniform int   uPassMode[4];\n"
"uniform vec4  uPassColor[4];\n"
"uniform float uPassFactor[4];\n"
"uniform int   uFogEnabled;\n"
"uniform vec3  uFogColor;\n"
"uniform float uFogFactor;\n"
"vec3 _applyPass(int mode, vec3 base, vec3 src, float f){\n""  if (mode==8448) return base*src;\n""  if (mode==260)  return base+src;\n""  if (mode==34165) return mix(base,src, clamp(f,0.0,1.0));\n""  if (mode==7681) return src;\n""  return base;\n""}\n""void main(){\n"
"  vec4 eye = gl_ModelViewMatrix * gl_Vertex;\n  vEyeDist = length(eye.xyz);\n  gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
"  vUV = gl_MultiTexCoord0.xy;\n"
"  vColor = gl_Color;\n"
"}\n";

/* N64 3-point filter (atsb) */
static const char* fragment_shader_bilateral_3point =
"#version 120\n\n"
"#define ADD_SCALE 0.60\n"
"uniform sampler2D uTex;\n"
"uniform vec2  uTexel;\n"
"uniform float uStrength;\n"
"uniform float uBleed;\n"
"uniform float uSeamFix;\n"
"uniform float uSnap;\n"
"uniform int   uUseTex;\n"
"varying vec2 vUV;\n"
"varying vec4 vColor;\nvarying float vEyeDist;\n"
"float _lum(vec3 c){ return dot(c, vec3(0.2126,0.7152,0.0722)); }\n"
"uniform int   uPassCount;\n"
"uniform int   uPassMode[4];\n"
"uniform vec4  uPassColor[4];\n"
"uniform float uPassFactor[4];\n"
"uniform int   uFogEnabled;\n"
"uniform vec3  uFogColor;\n"
"uniform float uFogFactor;\nuniform int   uFogMode;\nuniform float uFogStart;\nuniform float uFogEnd;\nuniform float uFogDensity;\n"
"\n"
"vec3 apply_combiner(vec3 rgb){\n"
"  for (int i=0;i<4;i++){\n"
"    if (i>=uPassCount) break;\n"
"    vec3  src = uPassColor[i].rgb;\n"
"    float fac = uPassFactor[i];\n"
"    int   mode= uPassMode[i];\n"
"    if (fac < 0.0 && fac > -1.5) fac = _lum(vColor.rgb);\n"
"    \n"
"    if (mode == 8448) {                           // GL_MODULATE\n"
"      rgb *= src;\n"
"    } else if (mode == 260) {                     // GL_ADD\n"
"      float amp = clamp(abs(fac),0.0,1.0)*ADD_SCALE;\n"
"      amp = min(amp*0.8, max(0.0, 1.0 - _lum(rgb)) + 1e-5);\n"
"      if (fac < -0.5) {                           // colored ADD\n"
"        float m = max(max(vColor.r,vColor.g),vColor.b);\n"
"        vec3 tint = (m > 1e-5) ? (vColor.rgb/m) : vColor.rgb;\n"
"        rgb += tint * src * amp;\n"
"      } else {\n"
"        rgb += src * amp;                         // plain scaled ADD\n"
"      }\n"
"    } else if (mode == 34165) {                   // GL_INTERPOLATE\n"
"      rgb = mix(rgb, src, clamp(fac,0.0,1.0));\n"
"    } else if (mode == 7681) {                    // GL_REPLACE\n"
"      rgb = src;\n"
"    }\n"
"  }\n"
"  return rgb;\n"
"}\n"
"\n"
"vec3 enhance_colors(vec3 rgb){\n"
"  float l = dot(rgb, vec3(0.2126, 0.7152, 0.0722));\n"
"  rgb = mix(vec3(l), rgb, 1.02);  // saturation\n"
"  rgb.r = rgb.r * 1.08;\n"
"  rgb.g = rgb.g * 1.08;\n"
"  rgb.b = rgb.b * 1.08;\n"
"  rgb = pow(max(rgb, 0.0), vec3(0.94));\n"
"  return rgb;\n"
"}\n"
"\n"
"void main(){\n"
"  // 1: no texture\n"
"  if (uUseTex == 0) {\n"
"    vec4 _texel = vec4(1.0); float _texAlpha = 1.0; vec4 col = vColor; vec3 rgb = apply_combiner(col.rgb);\n"
"    rgb = enhance_colors(rgb);\n"
"    if (uFogEnabled!=0) {\n    float fogF = 1.0;\n    if (uFogMode == 1) {\n        float denom = max(uFogEnd - uFogStart, 0.0001);\n        fogF = clamp((uFogEnd - vEyeDist) / denom, 0.0, 1.0);\n    } else if (uFogMode == 2) {\n        fogF = clamp(exp(-uFogDensity * vEyeDist), 0.0, 1.0);\n    } else if (uFogMode == 3) {\n        float d = uFogDensity * vEyeDist;\n        fogF = clamp(exp(-(d*d)), 0.0, 1.0);\n    } else {\n        fogF = clamp(1.0 - uFogFactor, 0.0, 1.0);\n    }\n    rgb = mix(uFogColor, rgb, fogF);\n}\n"
"    gl_FragColor = vec4(clamp(rgb,0.0,1.0), col.a); return; }\n"
"\n"
"  // 2: texture but no 3-point\n"
"  if (uTexel.x <= 0.0 || uTexel.y <= 0.0) {\n"
"    vec4 sampled = texture2D(uTex, vUV); vec4 _texel = sampled; float _texAlpha = sampled.a; vec4 col = sampled * vColor; vec3 rgb = apply_combiner(col.rgb);\n"
"    rgb = enhance_colors(rgb);\n"
"    if (uFogEnabled!=0) {\n    float fogF = 1.0;\n    if (uFogMode == 1) {\n        float denom = max(uFogEnd - uFogStart, 0.0001);\n        fogF = clamp((uFogEnd - vEyeDist) / denom, 0.0, 1.0);\n    } else if (uFogMode == 2) {\n        fogF = clamp(exp(-uFogDensity * vEyeDist), 0.0, 1.0);\n    } else if (uFogMode == 3) {\n        float d = uFogDensity * vEyeDist;\n        fogF = clamp(exp(-(d*d)), 0.0, 1.0);\n    } else {\n        fogF = clamp(1.0 - uFogFactor, 0.0, 1.0);\n    }\n    rgb = mix(uFogColor, rgb, fogF);\n}\n"
"    gl_FragColor = vec4(clamp(rgb,0.0,1.0), col.a); return; }\n"
"\n"
"  // 3: N64 3-point\n"
"  vec2 texSize = 1.0 / uTexel;\n"
"  vec2 p  = vUV * texSize - 0.5;\n"
"  vec2 ip = floor(p);\n"
"  vec2 f  = p - ip;\n"
"  if (uSnap > 0.0) { vec2 q = vec2(uSnap); f = floor(f*q + 0.5)/q; }\n"
"  vec2 baseUV = (ip + 0.5) / texSize;\n"
"\n"
"  float S = (uStrength > 0.0) ? max(uStrength, 1.0) : 1.0;\n"
"  vec2 stepX  = vec2(uTexel.x * S, 0.0);\n"
"  vec2 stepY  = vec2(0.0,           uTexel.y * S);\n"
"  vec2 stepXY = stepX + stepY;\n"
"\n"
"  vec4 c00 = texture2D(uTex, baseUV);\n"
"  vec4 c10 = texture2D(uTex, baseUV + stepX);\n"
"  vec4 c01 = texture2D(uTex, baseUV + stepY);\n"
"  vec4 c11 = texture2D(uTex, baseUV + stepXY);\n"
"\n"
"  float s = f.x + f.y;\n"
"  vec4 triA = c00 + f.x*(c10-c00) + f.y*(c01-c00);\n"
"  vec4 triB = c11 + (1.0-f.x)*(c01-c11) + (1.0-f.y)*(c10-c11);\n"
"  float eps = max(uSeamFix, 0.0);\n"
"  float w   = (eps > 0.0) ? smoothstep(1.0 - eps, 1.0 + eps, s) : step(1.0, s);\n"
"  vec4 tex  = mix(triA, triB, w);\n"
"  if (uBleed > 0.0) { vec4 avg = 0.25*(c00+c10+c01+c11); tex = mix(tex, avg, clamp(uBleed,0.0,1.0)); }\n"
"\n"
"  vec4 _texel = tex; float _texAlpha = tex.a; vec4 col = tex * vColor; vec3 rgb = apply_combiner(col.rgb);\n"
"  rgb = enhance_colors(rgb);\n"
"  if (uFogEnabled!=0) {\n    float fogF = 1.0;\n    if (uFogMode == 1) {\n        float denom = max(uFogEnd - uFogStart, 0.0001);\n        fogF = clamp((uFogEnd - vEyeDist) / denom, 0.0, 1.0);\n    } else if (uFogMode == 2) {\n        fogF = clamp(exp(-uFogDensity * vEyeDist), 0.0, 1.0);\n    } else if (uFogMode == 3) {\n        float d = uFogDensity * vEyeDist;\n        fogF = clamp(exp(-(d*d)), 0.0, 1.0);\n    } else {\n        fogF = clamp(1.0 - uFogFactor, 0.0, 1.0);\n    }\n    rgb = mix(uFogColor, rgb, fogF);\n}\n"
"  gl_FragColor = vec4(clamp(rgb,0.0,1.0), col.a);\n"
"}\n";

/* atsb: bilinear */
static const char* fragment_shader_bilateral =
"#version 120\n\n"
"#define ADD_SCALE 0.60\n"
"uniform sampler2D uTex;\n"
"uniform int uUseTex;\n"
"varying vec2 vUV;\n"
"varying vec4 vColor;\nvarying float vEyeDist;\n"
"float _lum(vec3 c){ return dot(c, vec3(0.2126,0.7152,0.0722)); }\n"
"uniform int   uPassCount;\n"
"uniform int   uPassMode[4];\n"
"uniform vec4  uPassColor[4];\n"
"uniform float uPassFactor[4];\n"
"uniform int   uFogEnabled;\n"
"uniform vec3  uFogColor;\n"
"uniform float uFogFactor;\nuniform int   uFogMode;\nuniform float uFogStart;\nuniform float uFogEnd;\nuniform float uFogDensity;\n"
"\n"
"vec3 apply_combiner(vec3 rgb){\n"
"  for (int i=0;i<4;i++){\n"
"    if (i>=uPassCount) break;\n"
"    vec3  src = uPassColor[i].rgb;\n"
"    float fac = uPassFactor[i];\n"
"    int   mode= uPassMode[i];\n"
"    if (fac < 0.0 && fac > -1.5) fac = _lum(vColor.rgb);\n"
"    \n"
"    if (mode == 8448) {                           // GL_MODULATE\n"
"      rgb *= src;\n"
"    } else if (mode == 260) {                     // GL_ADD\n"
"      float amp = clamp(abs(fac),0.0,1.0)*ADD_SCALE;\n"
"      amp = min(amp*0.8, max(0.0, 1.0 - _lum(rgb)) + 1e-5);\n"
"      if (fac < -0.5) {                           // colored ADD\n"
"        float m = max(max(vColor.r,vColor.g),vColor.b);\n"
"        vec3 tint = (m > 1e-5) ? (vColor.rgb/m) : vColor.rgb;\n"
"        rgb += tint * src * amp;\n"
"      } else {\n"
"        rgb += src * amp;                         // plain scaled ADD\n"
"      }\n"
"    } else if (mode == 34165) {                   // GL_INTERPOLATE\n"
"      rgb = mix(rgb, src, clamp(fac,0.0,1.0));\n"
"    } else if (mode == 7681) {                    // GL_REPLACE\n"
"      rgb = src;\n"
"    }\n"
"  }\n"
"  return rgb;\n"
"}\n"
"\n"
"vec3 enhance_colors(vec3 rgb){\n"
"  float l = dot(rgb, vec3(0.2126, 0.7152, 0.0722));\n"
"  rgb = mix(vec3(l), rgb, 1.02);  // saturation\n"
"  rgb.r = rgb.r * 1.08;\n"
"  rgb.g = rgb.g * 1.08;\n"
"  rgb.b = rgb.b * 1.08;\n"
"  rgb = pow(max(rgb, 0.0), vec3(0.94));\n"
"  return rgb;\n"
"}\n"
"\n"
"void main(){\n"
"  vec4 _texel = (uUseTex != 0) ? texture2D(uTex, vUV) : vec4(1.0);\n"
"  float _texAlpha = _texel.a; vec4 col = _texel * vColor;\n"
"  vec3 rgb = apply_combiner(col.rgb);\n"
"  rgb = enhance_colors(rgb);\n"
"  if (uFogEnabled!=0) {\n    float fogF = 1.0;\n    if (uFogMode == 1) {\n        float denom = max(uFogEnd - uFogStart, 0.0001);\n        fogF = clamp((uFogEnd - vEyeDist) / denom, 0.0, 1.0);\n    } else if (uFogMode == 2) {\n        fogF = clamp(exp(-uFogDensity * vEyeDist), 0.0, 1.0);\n    } else if (uFogMode == 3) {\n        float d = uFogDensity * vEyeDist;\n        fogF = clamp(exp(-(d*d)), 0.0, 1.0);\n    } else {\n        fogF = clamp(1.0 - uFogFactor, 0.0, 1.0);\n    }\n    rgb = mix(uFogColor, rgb, fogF);\n}\n"
"  gl_FragColor = vec4(clamp(rgb,0.0,1.0), col.a);\n"
"}\n";

typedef struct {
	const char* from;
	const char* to;
} shader_replacement_t;

static boolean I_ShaderIdentifierChar(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		(c >= '0' && c <= '9') || c == '_';
}

/*
 * Keep the original GLSL 1.20 source for desktop/GL4ES. Android GLES
 * receives an ESSL 1.00 or 3.00 prologue and explicit vertex attributes.
 * The shader body remains shared so filters, fog and combiner behaviour
 * cannot drift between the two mobile paths.
 */
static char* I_ShaderBuildESSource(GLenum type, const char* source) {
	const int glesVersion = I_GetGLESVersion();
	if (glesVersion != 2 && glesVersion != 3)
		return NULL;

	const boolean isVertex = (type == GL_VERTEX_SHADER);
	const char* body = SDL_strchr(source, '\n');
	body = body ? body + 1 : source;

	const char* header = NULL;
	if (glesVersion == 2) {
		header = isVertex
			? "#version 100\nprecision mediump float;\nattribute vec4 aPosition;\nattribute vec4 aColor;\nattribute vec4 aTexCoord0;\nuniform mat4 uModelViewMatrix;\nuniform mat4 uModelViewProjectionMatrix;\n"
			: "#version 100\nprecision mediump float;\nuniform int uAlphaTestEnabled;\nuniform int uAlphaFunc;\nuniform float uAlphaRef;\nuniform float uColorScale;\nuniform int uCombineRGB;\nuniform vec4 uEnvColor;\nuniform int uRGBSource0;\nuniform int uRGBSource1;\nuniform int uRGBSource2;\nuniform int uRGBOperand0;\nuniform int uRGBOperand1;\nuniform int uRGBOperand2;\nuniform int uCombineAlpha;\nuniform float uEnvAlpha;\nuniform int uAlphaSource0;\nuniform int uAlphaSource1;\nuniform int uAlphaSource2;\nuniform int uAlphaOperand0;\nuniform int uAlphaOperand1;\nuniform int uAlphaOperand2;\n";
	} else {
		header = isVertex
			? "#version 300 es\nprecision mediump float;\nin vec4 aPosition;\nin vec4 aColor;\nin vec4 aTexCoord0;\nuniform mat4 uModelViewMatrix;\nuniform mat4 uModelViewProjectionMatrix;\n"
			: "#version 300 es\nprecision mediump float;\nout vec4 fragColor;\nuniform int uAlphaTestEnabled;\nuniform int uAlphaFunc;\nuniform float uAlphaRef;\nuniform float uColorScale;\nuniform int uCombineRGB;\nuniform vec4 uEnvColor;\nuniform int uRGBSource0;\nuniform int uRGBSource1;\nuniform int uRGBSource2;\nuniform int uRGBOperand0;\nuniform int uRGBOperand1;\nuniform int uRGBOperand2;\nuniform int uCombineAlpha;\nuniform float uEnvAlpha;\nuniform int uAlphaSource0;\nuniform int uAlphaSource1;\nuniform int uAlphaSource2;\nuniform int uAlphaOperand0;\nuniform int uAlphaOperand1;\nuniform int uAlphaOperand2;\n";
	}

	static const shader_replacement_t vertexReplacements[] = {
		{ "gl_ModelViewProjectionMatrix", "uModelViewProjectionMatrix" },
		{ "gl_ModelViewMatrix", "uModelViewMatrix" },
		{ "gl_MultiTexCoord0", "aTexCoord0" },
		{ "gl_Vertex", "aPosition" },
		{ "gl_Color", "aColor" }
	};
	static const shader_replacement_t es3Replacements[] = {
		{ "gl_FragColor", "fragColor" },
		{ "texture2D", "texture" }
	};
	static const shader_replacement_t nativeFragmentReplacements[] = {
		{ "apply_combiner(col.rgb)", "apply_native_combiner(_texel, vColor)" }
	};

	const size_t headerLength = SDL_strlen(header);
	const size_t bodyLength = SDL_strlen(body);
	char* result = (char*)SDL_malloc(bodyLength * 2 + headerLength + 1024);
	if (!result)
		return NULL;

	SDL_memcpy(result, header, headerLength);
	char* output = result + headerLength;
	const char* input = body;

	while (*input) {
		boolean replaced = false;
		if (!I_ShaderIdentifierChar(input > body ? input[-1] : '\0')) {
			const shader_replacement_t* list = vertexReplacements;
			const size_t count = isVertex ? SDL_arraysize(vertexReplacements) : 0;
			for (size_t i = 0; i < count; ++i) {
				const size_t tokenLength = SDL_strlen(list[i].from);
				if (SDL_strncmp(input, list[i].from, tokenLength) == 0 &&
					!I_ShaderIdentifierChar(input[tokenLength])) {
					const size_t replacementLength = SDL_strlen(list[i].to);
					SDL_memcpy(output, list[i].to, replacementLength);
					output += replacementLength;
					input += tokenLength;
					replaced = true;
					break;
				}
			}

			if (!replaced && glesVersion == 3) {
				for (size_t i = 0; i < SDL_arraysize(es3Replacements); ++i) {
					const size_t tokenLength = SDL_strlen(es3Replacements[i].from);
					if (SDL_strncmp(input, es3Replacements[i].from, tokenLength) == 0 &&
						!I_ShaderIdentifierChar(input[tokenLength])) {
						const size_t replacementLength = SDL_strlen(es3Replacements[i].to);
						SDL_memcpy(output, es3Replacements[i].to, replacementLength);
						output += replacementLength;
						input += tokenLength;
						replaced = true;
						break;
					}
				}
			}

			if (!replaced && !isVertex) {
				for (size_t i = 0; i < SDL_arraysize(nativeFragmentReplacements); ++i) {
					const size_t tokenLength = SDL_strlen(nativeFragmentReplacements[i].from);
					if (SDL_strncmp(input, nativeFragmentReplacements[i].from, tokenLength) == 0 &&
						!I_ShaderIdentifierChar(input[tokenLength])) {
						const size_t replacementLength = SDL_strlen(nativeFragmentReplacements[i].to);
						SDL_memcpy(output, nativeFragmentReplacements[i].to, replacementLength);
						output += replacementLength;
						input += tokenLength;
						replaced = true;
						break;
					}
				}
			}

			if (!replaced && glesVersion == 3 &&
				SDL_strncmp(input, "varying", 7) == 0 &&
				!I_ShaderIdentifierChar(input[7])) {
				const char* qualifier = isVertex ? "out" : "in";
				const size_t qualifierLength = SDL_strlen(qualifier);
				SDL_memcpy(output, qualifier, qualifierLength);
				output += qualifierLength;
				input += 7;
				replaced = true;
			}
		}

		if (!replaced)
			*output++ = *input++;
	}

	*output = '\0';

	/* Native ESSL gets shader-based alpha testing. Desktop/GL4ES source stays intact. */
	if (!isVertex && SDL_strstr(result + headerLength, "vec4 col =")) {
		/* Only the main world fragment programs use the combiner/alpha-test
		   contract. The solid tint overlay intentionally does not. */
		static const char alphaFunction[] =
			"\nvec4 _rgbArg(int source, int operand, vec4 texel, vec4 primary) {\n"
			"  vec4 value = primary;\n"
			"  if (source == 0x1702) value = texel;\n"
			"  else if (source == 0x8576) value = uEnvColor;\n"
			"  if (operand == 0x0301) return vec4(1.0 - value.rgb, 1.0 - value.a);\n"
			"  if (operand == 0x0302) return vec4(vec3(value.a), value.a);\n"
			"  if (operand == 0x0303) return vec4(vec3(1.0 - value.a), 1.0 - value.a);\n"
			"  return value;\n"
			"}\n"
			"vec3 apply_native_combiner(vec4 texel, vec4 primary) {\n"
			"  if (uUseTex == 0) return primary.rgb;\n"
			"  vec4 a0 = _rgbArg(uRGBSource0, uRGBOperand0, texel, primary);\n"
			"  vec4 a1 = _rgbArg(uRGBSource1, uRGBOperand1, texel, primary);\n"
			"  vec4 a2 = _rgbArg(uRGBSource2, uRGBOperand2, texel, primary);\n"
			"  if (uCombineRGB == 7681) return a0.rgb;\n"
			"  if (uCombineRGB == 260) return clamp(a0.rgb + a1.rgb, 0.0, 1.0);\n"
			"  if (uCombineRGB == 34165) return clamp(mix(a1.rgb, a0.rgb, a2.rgb), 0.0, 1.0);\n"
			"  return clamp(a0.rgb * a1.rgb, 0.0, 1.0);\n"
			"}\n"
			"float _alphaArg(int source, int operand, float texAlpha, float primaryAlpha) {\n"
			"  float value = primaryAlpha;\n"
			"  if (source == 0x1702) value = texAlpha;\n"
			"  else if (source == 0x8576) value = uEnvAlpha;\n"
			"  if (operand == 0x0303) value = 1.0 - value;\n"
			"  return value;\n"
			"}\n"
			"float apply_combiner_alpha(float texAlpha, float primaryAlpha) {\n"
			"  float a0 = _alphaArg(uAlphaSource0, uAlphaOperand0, texAlpha, primaryAlpha);\n"
			"  float a1 = _alphaArg(uAlphaSource1, uAlphaOperand1, texAlpha, primaryAlpha);\n"
			"  float a2 = _alphaArg(uAlphaSource2, uAlphaOperand2, texAlpha, primaryAlpha);\n"
			"  if (uCombineAlpha == 7681) return a0;\n"
			"  if (uCombineAlpha == 260) return clamp(a0 + a1, 0.0, 1.0);\n"
			"  if (uCombineAlpha == 34165) return clamp(mix(a1, a0, a2), 0.0, 1.0);\n"
			"  return clamp(a0 * a1, 0.0, 1.0);\n"
			"}\n"
			"bool _alphaPass(float a) {\n"
			"  if (uAlphaTestEnabled == 0 || uAlphaFunc == 519) return true;\n"
			"  if (uAlphaFunc == 512) return false;\n"
			"  if (uAlphaFunc == 513) return a < uAlphaRef;\n"
			"  if (uAlphaFunc == 514) return abs(a-uAlphaRef) <= 0.00001;\n"
			"  if (uAlphaFunc == 515) return a <= uAlphaRef;\n"
			"  if (uAlphaFunc == 516) return a > uAlphaRef;\n"
			"  if (uAlphaFunc == 517) return abs(a-uAlphaRef) > 0.00001;\n"
			"  if (uAlphaFunc == 518) return a >= uAlphaRef;\n"
			"  return true;\n"
			"}\n";
		static const char alphaDiscard[] = "\n    col.a = apply_combiner_alpha(_texAlpha, vColor.a);\n    if (!_alphaPass(col.a)) discard;\n";
		char* finalSource = (char*)SDL_malloc(bodyLength * 2 + headerLength + 1024);
		if (!finalSource) {
			SDL_free(result);
			return NULL;
		}
		SDL_memcpy(finalSource, result, headerLength);
		char* write = finalSource + headerLength;
		const char* scan = result + headerLength;
		const char* end = output;
		const char* marker = "vec4 col =";
		while (scan < end) {
			if ((size_t)(end - scan) >= SDL_strlen(marker) &&
				SDL_strncmp(scan, marker, SDL_strlen(marker)) == 0) {
				const char* semicolon = (const char*)SDL_memchr(scan, ';', (size_t)(end - scan));
				if (semicolon) {
					size_t declarationLength = (size_t)(semicolon + 1 - scan);
					SDL_memcpy(write, scan, declarationLength);
					write += declarationLength;
					size_t discardLength = sizeof(alphaDiscard) - 1;
					SDL_memcpy(write, alphaDiscard, discardLength);
					write += discardLength;
					scan = semicolon + 1;
					continue;
				}
			}
			*write++ = *scan++;
		}
		*write = '\0';

		size_t functionLength = sizeof(alphaFunction) - 1;
		/* Insert after all global uniforms/functions and before main(). */
		char* insertAt = SDL_strstr(finalSource + headerLength, "void main()");
		if (!insertAt) {
			SDL_free(finalSource);
			SDL_free(result);
			return NULL;
		}
		size_t tailLength = (size_t)(write - insertAt);
		SDL_memmove(insertAt + functionLength, insertAt, tailLength + 1);
		SDL_memcpy(insertAt, alphaFunction, functionLength);
		SDL_free(result);
		result = finalSource;
	}

	/* The color scale is part of the shader post-process only on native GLES. */
	if (!isVertex) {
		const char* marker = "rgb = pow(max(rgb, 0.0), vec3(0.94));";
		char* scaleAt = SDL_strstr(result + headerLength, marker);
		if (scaleAt) {
			static const char scaleLine[] = "\n  rgb *= uColorScale;";
			size_t markerLength = SDL_strlen(marker);
			size_t insertLength = sizeof(scaleLine) - 1;
			char* scaled = (char*)SDL_malloc(bodyLength * 2 + headerLength + 1024);
			if (!scaled) { SDL_free(result); return NULL; }
			size_t prefix = (size_t)(scaleAt - result) + markerLength;
			SDL_memcpy(scaled, result, prefix);
			SDL_memcpy(scaled + prefix, scaleLine, insertLength);
			SDL_memcpy(scaled + prefix + insertLength, result + prefix, SDL_strlen(result + prefix) + 1);
			SDL_free(result);
			result = scaled;
		}
	}

	return result;
}

static void I_ShaderBindLegacyAttributes(GLuint program, boolean includeColorAndTexCoord) {
	if (I_GetGLESVersion() == 0)
		return;

	/* GLES needs contiguous, conventional vertex attribute slots. */
	pglBindAttribLocation(program, 0, "aPosition");
	if (includeColorAndTexCoord) {
		pglBindAttribLocation(program, 1, "aColor");
		pglBindAttribLocation(program, 2, "aTexCoord0");
	}
}

static GLuint I_3PointShaderCompile(GLenum type, const char* src) {
	GLuint shader = pglCreateShader(type);
	if (!shader)
		return 0;

	char* esSource = I_ShaderBuildESSource(type, src);
	if (I_GetGLESVersion() != 0 && !esSource) {
#ifdef ANDROID
		SDL_Log("I_3PointShaderCompile: failed to build native ESSL source");
#endif
		return 0;
	}
	const char* shaderSource = esSource ? esSource : src;
	pglShaderSource(shader, 1, &shaderSource, NULL);
	pglCompileShader(shader);
	SDL_free(esSource);
	GLint ok = 0;
	pglGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024]; GLsizei n = 0;
		pglGetShaderInfoLog(shader, sizeof(log), &n, log);
#ifdef ANDROID
		SDL_Log("I_3PointShaderCompile: %s compilation error:\\n%.*s\\n",
			type == GL_VERTEX_SHADER ? "vertex" : "fragment", (int)n, log);
#else
        fprintf(stderr, "I_3PointShaderCompile: %s compilation error:\\n%.*s\\n",
			type == GL_VERTEX_SHADER ? "vertex" : "fragment", (int)n, log);
#endif
	}
	if (!ok)
		return 0;
	return shader;
}

static void I_3PointShaderInit(void) {
	if (shader_struct.initialised)
		return;
	GLuint fs;
	I_ShaderLoad();
	if (!is_glsl_loaded)
		return;
	GLuint vs = I_3PointShaderCompile(GL_VERTEX_SHADER, vertex_shader_bilateral);
	if (r_filter.value == 0) {
		fs = I_3PointShaderCompile(GL_FRAGMENT_SHADER, fragment_shader_bilateral_3point);
	}
	else {
		fs = I_3PointShaderCompile(GL_FRAGMENT_SHADER, fragment_shader_bilateral);
	}
	if (!vs || !fs)
		return;

	shader_struct.prog = pglCreateProgram();
	if (!shader_struct.prog)
		return;
	I_ShaderBindLegacyAttributes(shader_struct.prog, true);
	pglAttachShader(shader_struct.prog, vs);
	pglAttachShader(shader_struct.prog, fs);
	pglLinkProgram(shader_struct.prog);
	GLint ok = 0; pglGetProgramiv(shader_struct.prog, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024]; GLsizei n = 0; pglGetProgramInfoLog(shader_struct.prog, sizeof(log), &n, log);
#ifdef ANDROID
		SDL_Log("I_3PointShaderInit: Linkage error:\n%.*s\n", (int)n, log);
#else
        fprintf(stderr, "I_3PointShaderInit: Linkage error:\n%.*s\n", (int)n, log);
#endif
		shader_struct.prog = 0;
		return;
	}
	shader_struct.locTex = pglGetUniformLocation(shader_struct.prog, "uTex");
	shader_struct.locModelView = pglGetUniformLocation(shader_struct.prog, "uModelViewMatrix");
	shader_struct.locModelViewProjection = pglGetUniformLocation(shader_struct.prog, "uModelViewProjectionMatrix");
	sLocTexel = pglGetUniformLocation(shader_struct.prog, "uTexel");
	sLocUseTex = pglGetUniformLocation(shader_struct.prog, "uUseTex");
	sLocPassCount = pglGetUniformLocation(shader_struct.prog, "uPassCount");
	for (int i=0;i<4;i++) {
		char n1[32], n2[32], n3[32];
		SDL_snprintf(n1,sizeof(n1),"uPassMode[%d]",i);
		SDL_snprintf(n2,sizeof(n2),"uPassColor[%d]",i);
		SDL_snprintf(n3,sizeof(n3),"uPassFactor[%d]",i);
		sLocPassMode[i]  = pglGetUniformLocation(shader_struct.prog, n1);
		sLocPassColor[i] = pglGetUniformLocation(shader_struct.prog, n2);
		sLocPassFactor[i]= pglGetUniformLocation(shader_struct.prog, n3);
	}
	sLocFogEnabled = pglGetUniformLocation(shader_struct.prog, "uFogEnabled");
	sLocFogColor   = pglGetUniformLocation(shader_struct.prog, "uFogColor");
	sLocFogFactor  = pglGetUniformLocation(shader_struct.prog, "uFogFactor");
	sLocAlphaTestEnabled = pglGetUniformLocation(shader_struct.prog, "uAlphaTestEnabled");
	sLocAlphaFunc = pglGetUniformLocation(shader_struct.prog, "uAlphaFunc");
	sLocAlphaRef = pglGetUniformLocation(shader_struct.prog, "uAlphaRef");
	sLocColorScale = pglGetUniformLocation(shader_struct.prog, "uColorScale");

	shader_struct.initialised = 1;

	// init combiner defaults
	gComb.combine_rgb = 8448; gComb.combine_alpha = 8448;
	for (int i=0;i<3;i++){ 
		gComb.source_rgb[i]=0x1702; 
		gComb.operand_rgb[i]=0x0300; 
	}
	gComb.env_color[0]=gComb.env_color[1]=gComb.env_color[2]=0.0f; gComb.env_color[3]=1.0f;
	gComb.pass_count=0; gComb.fog_enabled=0; gComb.fog_color[0]=gComb.fog_color[1]=gComb.fog_color[2]=0.0f; gComb.fog_factor=0.0f;

	// push combiner to shader
	if (sLocPassCount>=0) 
		pglUniform1i(sLocPassCount, 0);
	if (sLocFogEnabled>=0) 
		pglUniform1i(sLocFogEnabled, 0);
	if (sLocFogColor>=0) 
		pglUniform3f(sLocFogColor,0.0f,0.0f,0.0f);
	if (sLocFogFactor>=0) 
		pglUniform1f(sLocFogFactor,0.0f);

}

/* GENERIC TINT OVERLAY SHADERS
===============================
*/

static GLuint I_OverlayTintShaderCompile(GLenum tp, const char* src) {
	GLuint sh = pglCreateShader(tp);
	if (!sh)
		return 0;
	char* esSource = I_ShaderBuildESSource(tp, src);
	if (I_GetGLESVersion() != 0 && !esSource) {
#ifdef ANDROID
		SDL_Log("I_OverlayTintShaderCompile: failed to build native ESSL source");
#endif
		return 0;
	}
	const char* shaderSource = esSource ? esSource : src;
	pglShaderSource(sh, 1, &shaderSource, NULL);
	pglCompileShader(sh);
	SDL_free(esSource);
	GLint ok = 0;
	pglGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024];
		GLsizei n = 0;
		pglGetShaderInfoLog(sh, sizeof(log), &n, log);
#ifdef ANDROID
		SDL_Log("I_OverlayTintShaderCompile: %.*s", (int)n, log);
#else
		fprintf(stderr, "I_OverlayTintShaderCompile: %.*s\\n", (int)n, log);
#endif
		return 0;
	}
	return sh;
}

void I_OverlayTintShaderInit(void) {
	if (generic_tint_overlay_prog)
		return;
	I_ShaderLoad();
	if (!is_glsl_loaded)
		return;
	GLuint vs = I_OverlayTintShaderCompile(GL_VERTEX_SHADER,
		"#version 120\n\n\n"
		"void main(){ gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; }\n");
	GLuint fs = I_OverlayTintShaderCompile(GL_FRAGMENT_SHADER,
		"#version 120\n\n"
		"uniform vec4 uColor;\n" 
		"void main(){ gl_FragColor = uColor; }\n");
	if (!vs || !fs)
		return;
	generic_tint_overlay_prog = pglCreateProgram();
	if (!generic_tint_overlay_prog)
		return;
	I_ShaderBindLegacyAttributes(generic_tint_overlay_prog, false);
	pglAttachShader(generic_tint_overlay_prog, vs);
	pglAttachShader(generic_tint_overlay_prog, fs);
	pglLinkProgram(generic_tint_overlay_prog);
	GLint linked = 0;
	pglGetProgramiv(generic_tint_overlay_prog, GL_LINK_STATUS, &linked);
	if (!linked) {
		char log[1024];
		GLsizei n = 0;
		pglGetProgramInfoLog(generic_tint_overlay_prog, sizeof(log), &n, log);
#ifdef ANDROID
		SDL_Log("I_OverlayTintShaderInit: link error: %.*s", (int)n, log);
#else
		fprintf(stderr, "I_OverlayTintShaderInit: link error: %.*s\\n", (int)n, log);
#endif
		generic_tint_overlay_prog = 0;
		return;
	}
	generic_tint_overlay_colour = pglGetUniformLocation(generic_tint_overlay_prog, "uColor");
	generic_tint_overlay_mvp = pglGetUniformLocation(generic_tint_overlay_prog, "uModelViewProjectionMatrix");
}

void I_ShaderFullscreenTint(float r, float g, float b, float a) {
	if (a <= 0.0f)
		return;

	I_OverlayTintShaderInit();

	if (!(generic_tint_overlay_prog && generic_tint_overlay_colour >= 0))
		return;

	GLint oldProg = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &oldProg);

	GLboolean wasBlend = glIsEnabled(GL_BLEND);
	GLboolean wasTex2D = glIsEnabled(GL_TEXTURE_2D);
	GLboolean wasDepthTest = glIsEnabled(GL_DEPTH_TEST);
	GLboolean wasAlphaTest = glIsEnabled(GL_ALPHA_TEST);

	GLint oldSrc = 0, oldDst = 0; glGetIntegerv(GL_BLEND_SRC, &oldSrc);
	glGetIntegerv(GL_BLEND_DST, &oldDst);
	GLboolean oldDepthMask; glGetBooleanv(GL_DEPTH_WRITEMASK, &oldDepthMask);

	pglUseProgram(generic_tint_overlay_prog);
	is_current_prog = generic_tint_overlay_prog;
	pglUniform4f(generic_tint_overlay_colour, r, g, b, a);

	GL_SetOrtho(1);
	I_ShaderUpdateMatrices();
	if (!wasBlend) glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	if (wasTex2D)
		glDisable(GL_TEXTURE_2D);

	if (wasAlphaTest)
		glDisable(GL_ALPHA_TEST);

	if (wasDepthTest)
		glDisable(GL_DEPTH_TEST);

	glDepthMask(GL_FALSE);

	glColor4ub(255, 255, 255, 255);
	dglRecti(SCREENWIDTH, SCREENHEIGHT, 0, 0);

	glDepthMask(oldDepthMask);

	if (wasDepthTest)
		glEnable(GL_DEPTH_TEST);
	else
		glDisable(GL_DEPTH_TEST);

	if (wasAlphaTest)
		glEnable(GL_ALPHA_TEST);

	if (wasTex2D)
		glEnable(GL_TEXTURE_2D);
	glBlendFunc(oldSrc, oldDst);

	if (!wasBlend)
		glDisable(GL_BLEND);
	GL_ResetViewport();

	pglUseProgram(oldProg);
	is_current_prog = (GLuint)oldProg;
}

/*
 * ESSL does not expose the compatibility matrix built-ins used by the
 * desktop shader. GL4ES still owns the legacy matrix stacks, so mirror
 * them into explicit uniforms immediately before a draw.
 */
void I_ShaderUpdateMatrices(void) {
	if (I_GetGLESVersion() == 0 || !pglUniformMatrix4fv)
		return;
	if (is_current_prog == 0 ||
		(is_current_prog != shader_struct.prog &&
		 is_current_prog != generic_tint_overlay_prog))
		return;

	float modelView[16];
	float projection[16];
	float modelViewProjection[16];
	glGetFloatv(GL_MODELVIEW_MATRIX, modelView);
	glGetFloatv(GL_PROJECTION_MATRIX, projection);

	/* OpenGL matrices are column-major: MVP = projection * modelView. */
	for (int column = 0; column < 4; ++column) {
		for (int row = 0; row < 4; ++row) {
			float value = 0.0f;
			for (int k = 0; k < 4; ++k)
				value += projection[k * 4 + row] * modelView[column * 4 + k];
			modelViewProjection[column * 4 + row] = value;
		}
	}

	if (is_current_prog == shader_struct.prog) {
		if (shader_struct.locModelView >= 0)
			pglUniformMatrix4fv(shader_struct.locModelView, 1, 0, modelView);
		if (shader_struct.locModelViewProjection >= 0)
			pglUniformMatrix4fv(shader_struct.locModelViewProjection, 1, 0, modelViewProjection);
	} else if (is_current_prog == generic_tint_overlay_prog &&
		generic_tint_overlay_mvp >= 0) {
		pglUniformMatrix4fv(generic_tint_overlay_mvp, 1, 0, modelViewProjection);
	}
}

/* SHADER FUNCTIONS USED THROUGHOUT THE CODE
============================================
*/

void I_ShaderBind(void) {
	I_3PointShaderInit();
	I_OverlayTintShaderInit();
	if (!is_glsl_loaded || !shader_struct.initialised || !shader_struct.prog)
		return;
	if (is_current_prog != shader_struct.prog) {
		pglUseProgram(shader_struct.prog);
		is_current_prog = shader_struct.prog;
	}
	if (shader_struct.locTex >= 0)
		pglUniform1i(shader_struct.locTex, g_shader_texture_unit);
	if (sLocUseTex >= 0)
		pglUniform1i(sLocUseTex, g_use_texture);
	if (sLocTexel >= 0)
		pglUniform2f(sLocTexel, 0.0f, 0.0f);
	if (sLocAlphaTestEnabled >= 0)
		pglUniform1i(sLocAlphaTestEnabled, g_alpha_test_enabled);
	if (sLocAlphaFunc >= 0)
		pglUniform1i(sLocAlphaFunc, g_alpha_func);
	if (sLocAlphaRef >= 0)
		pglUniform1f(sLocAlphaRef, g_alpha_ref);
	if (sLocColorScale >= 0)
		pglUniform1f(sLocColorScale, g_color_scale);
	I_SectorCombiner_Commit();
}

void I_ShaderUnBind(void) {
	if (!shader_struct.initialised)
		return;
	if (is_current_prog != 0) {
		pglUseProgram(0);
		is_current_prog = 0;
	}
}

void I_ShaderSetTextureSize(int w, int h) {
	if (!shader_struct.initialised || sLocTexel < 0)
		return;
	if (w > 0 && h > 0)
		pglUniform2f(sLocTexel, 1.0f / (float)w, 1.0f / (float)h);
	else
		pglUniform2f(sLocTexel, 0.0f, 0.0f);
}

void I_ShaderSetUseTexture(int on) {
	g_use_texture = on ? 1 : 0;
	if (!shader_struct.initialised || sLocUseTex < 0 || is_current_prog != shader_struct.prog)
		return;
	pglUniform1i(sLocUseTex, g_use_texture);
}

void I_ShaderSetTextureUnit(int unit) {
	g_shader_texture_unit = unit < 0 ? 0 : unit;
	if (!shader_struct.initialised || shader_struct.locTex < 0 ||
		is_current_prog != shader_struct.prog)
		return;
	pglUniform1i(shader_struct.locTex, g_shader_texture_unit);
}

void I_ShaderSetAlphaTest(int enabled, int func, float reference) {
	g_alpha_test_enabled = enabled ? 1 : 0;
	g_alpha_func = func;
	g_alpha_ref = reference;
	if (!shader_struct.initialised || is_current_prog != shader_struct.prog)
		return;
	if (sLocAlphaTestEnabled >= 0) pglUniform1i(sLocAlphaTestEnabled, g_alpha_test_enabled);
	if (sLocAlphaFunc >= 0) pglUniform1i(sLocAlphaFunc, g_alpha_func);
	if (sLocAlphaRef >= 0) pglUniform1f(sLocAlphaRef, g_alpha_ref);
}

void I_ShaderSetColorScale(float scale) {
	g_color_scale = scale;
	if (!shader_struct.initialised || is_current_prog != shader_struct.prog || sLocColorScale < 0)
		return;
	pglUniform1f(sLocColorScale, g_color_scale);
}
