#ifndef DOOM64_DGL_GLES_H
#define DOOM64_DGL_GLES_H

#include "gl_main.h"

/*
 * The Android loader is generated for OpenGL ES 3.0. It deliberately does
 * not contain desktop-only ARB/EXT function-pointer types, although the
 * shared DGL extension loader still needs those declarations for the
 * Android GL4ES compatibility path.
 *
 * These typedefs only describe legacy desktop entry-point signatures; they
 * do not add GLES functions or provide fallback implementations.
 */
#ifdef ANDROID
typedef void (APIENTRYP PFNGLACTIVETEXTUREARBPROC) (GLenum texture);
typedef void (APIENTRYP PFNGLBINDBUFFERARBPROC) (GLenum target, GLuint buffer);
typedef void (APIENTRYP PFNGLBUFFERDATAARBPROC) (GLenum target, GLsizeiptrARB size, const void *data, GLenum usage);
typedef void (APIENTRYP PFNGLBUFFERSUBDATAARBPROC) (GLenum target, GLintptrARB offset, GLsizeiptrARB size, const void *data);
typedef void (APIENTRYP PFNGLCLIENTACTIVETEXTUREARBPROC) (GLenum texture);
typedef void (APIENTRYP PFNGLDELETEBUFFERSARBPROC) (GLsizei n, const GLuint *buffers);
typedef void (APIENTRYP PFNGLFOGCOORDDEXTPROC) (GLdouble coord);
typedef void (APIENTRYP PFNGLFOGCOORDDVEXTPROC) (const GLdouble *coord);
typedef void (APIENTRYP PFNGLFOGCOORDFEXTPROC) (GLfloat coord);
typedef void (APIENTRYP PFNGLFOGCOORDFVEXTPROC) (const GLfloat *coord);
typedef void (APIENTRYP PFNGLFOGCOORDPOINTEREXTPROC) (GLenum type, GLsizei stride, const void *pointer);
typedef void (APIENTRYP PFNGLGENBUFFERSARBPROC) (GLsizei n, GLuint *buffers);
typedef void (APIENTRYP PFNGLGETBUFFERPARAMETERIVARBPROC) (GLenum target, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNGLGETBUFFERPOINTERVARBPROC) (GLenum target, GLenum pname, void **params);
typedef void (APIENTRYP PFNGLGETBUFFERSUBDATAARBPROC) (GLenum target, GLintptrARB offset, GLsizeiptrARB size, void *data);
typedef GLboolean (APIENTRYP PFNGLISBUFFERARBPROC) (GLuint buffer);
typedef void (APIENTRYP PFNGLLOCKARRAYSEXTPROC) (GLint first, GLsizei count);
typedef void *(APIENTRYP PFNGLMAPBUFFERARBPROC) (GLenum target, GLenum access);
typedef void (APIENTRYP PFNGLMULTITEXCOORD1DARBPROC) (GLenum target, GLdouble s);
typedef void (APIENTRYP PFNGLMULTITEXCOORD1DVARBPROC) (GLenum target, const GLdouble *v);
typedef void (APIENTRYP PFNGLMULTITEXCOORD1FARBPROC) (GLenum target, GLfloat s);
typedef void (APIENTRYP PFNGLMULTITEXCOORD1FVARBPROC) (GLenum target, const GLfloat *v);
typedef void (APIENTRYP PFNGLMULTITEXCOORD1IARBPROC) (GLenum target, GLint s);
typedef void (APIENTRYP PFNGLMULTITEXCOORD1IVARBPROC) (GLenum target, const GLint *v);
typedef void (APIENTRYP PFNGLMULTITEXCOORD1SARBPROC) (GLenum target, GLshort s);
typedef void (APIENTRYP PFNGLMULTITEXCOORD1SVARBPROC) (GLenum target, const GLshort *v);
typedef void (APIENTRYP PFNGLMULTITEXCOORD2DARBPROC) (GLenum target, GLdouble s, GLdouble t);
typedef void (APIENTRYP PFNGLMULTITEXCOORD2DVARBPROC) (GLenum target, const GLdouble *v);
typedef void (APIENTRYP PFNGLMULTITEXCOORD2FARBPROC) (GLenum target, GLfloat s, GLfloat t);
typedef void (APIENTRYP PFNGLMULTITEXCOORD2FVARBPROC) (GLenum target, const GLfloat *v);
typedef void (APIENTRYP PFNGLMULTITEXCOORD2IARBPROC) (GLenum target, GLint s, GLint t);
typedef void (APIENTRYP PFNGLMULTITEXCOORD2IVARBPROC) (GLenum target, const GLint *v);
typedef void (APIENTRYP PFNGLMULTITEXCOORD2SARBPROC) (GLenum target, GLshort s, GLshort t);
typedef void (APIENTRYP PFNGLMULTITEXCOORD2SVARBPROC) (GLenum target, const GLshort *v);
typedef void (APIENTRYP PFNGLMULTITEXCOORD3DARBPROC) (GLenum target, GLdouble s, GLdouble t, GLdouble r);
typedef void (APIENTRYP PFNGLMULTITEXCOORD3DVARBPROC) (GLenum target, const GLdouble *v);
typedef void (APIENTRYP PFNGLMULTITEXCOORD3FARBPROC) (GLenum target, GLfloat s, GLfloat t, GLfloat r);
typedef void (APIENTRYP PFNGLMULTITEXCOORD3FVARBPROC) (GLenum target, const GLfloat *v);
typedef void (APIENTRYP PFNGLMULTITEXCOORD3IARBPROC) (GLenum target, GLint s, GLint t, GLint r);
typedef void (APIENTRYP PFNGLMULTITEXCOORD3IVARBPROC) (GLenum target, const GLint *v);
typedef void (APIENTRYP PFNGLMULTITEXCOORD3SARBPROC) (GLenum target, GLshort s, GLshort t, GLshort r);
typedef void (APIENTRYP PFNGLMULTITEXCOORD3SVARBPROC) (GLenum target, const GLshort *v);
typedef void (APIENTRYP PFNGLMULTITEXCOORD4DARBPROC) (GLenum target, GLdouble s, GLdouble t, GLdouble r, GLdouble q);
typedef void (APIENTRYP PFNGLMULTITEXCOORD4DVARBPROC) (GLenum target, const GLdouble *v);
typedef void (APIENTRYP PFNGLMULTITEXCOORD4FARBPROC) (GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
typedef void (APIENTRYP PFNGLMULTITEXCOORD4FVARBPROC) (GLenum target, const GLfloat *v);
typedef void (APIENTRYP PFNGLMULTITEXCOORD4IARBPROC) (GLenum target, GLint s, GLint t, GLint r, GLint q);
typedef void (APIENTRYP PFNGLMULTITEXCOORD4IVARBPROC) (GLenum target, const GLint *v);
typedef void (APIENTRYP PFNGLMULTITEXCOORD4SARBPROC) (GLenum target, GLshort s, GLshort t, GLshort r, GLshort q);
typedef void (APIENTRYP PFNGLMULTITEXCOORD4SVARBPROC) (GLenum target, const GLshort *v);
typedef void (APIENTRYP PFNGLUNLOCKARRAYSEXTPROC) (void);
typedef GLboolean (APIENTRYP PFNGLUNMAPBUFFERARBPROC) (GLenum target);
#endif /* ANDROID */

/* Legacy fixed-function tokens retained as software state identifiers. */
#ifndef GL_ALPHA_SCALE
#define GL_ALPHA_SCALE 0x0D1C
#endif
#ifndef GL_ALPHA_TEST
#define GL_ALPHA_TEST 0x0BC0
#endif
#ifndef GL_BLEND_DST
#define GL_BLEND_DST 0x0BE0
#endif
#ifndef GL_BLEND_SRC
#define GL_BLEND_SRC 0x0BE1
#endif
#ifndef GL_CLAMP
#define GL_CLAMP 0x2900
#endif
#ifndef GL_COLOR_ARRAY
#define GL_COLOR_ARRAY 0x8076
#endif
#ifndef GL_COLOR_LOGIC_OP
#define GL_COLOR_LOGIC_OP 0x0BF2
#endif
#ifndef GL_COMBINE
#define GL_COMBINE 0x8570
#endif
#ifndef GL_COMBINE_ARB
#define GL_COMBINE_ARB GL_COMBINE
#endif
#ifndef GL_COMBINE_ALPHA
#define GL_COMBINE_ALPHA 0x8572
#endif
#ifndef GL_COMBINE_RGB
#define GL_COMBINE_RGB 0x8571
#endif
#ifndef GL_DECAL
#define GL_DECAL 0x2101
#endif
#ifndef GL_EXP
#define GL_EXP 0x0800
#endif
#ifndef GL_EXP2
#define GL_EXP2 0x0801
#endif
#ifndef GL_FILL
#define GL_FILL 0x1B02
#endif
#ifndef GL_FOG
#define GL_FOG 0x0B60
#endif
#ifndef GL_FOG_COLOR
#define GL_FOG_COLOR 0x0B66
#endif
#ifndef GL_FOG_DENSITY
#define GL_FOG_DENSITY 0x0B62
#endif
#ifndef GL_FOG_END
#define GL_FOG_END 0x0B64
#endif
#ifndef GL_FOG_HINT
#define GL_FOG_HINT 0x0C54
#endif
#ifndef GL_FOG_MODE
#define GL_FOG_MODE 0x0B65
#endif
#ifndef GL_FOG_START
#define GL_FOG_START 0x0B63
#endif
#ifndef GL_LINE
#define GL_LINE 0x1B01
#endif
#ifndef GL_MAX_TEXTURE_UNITS_ARB
#define GL_MAX_TEXTURE_UNITS_ARB 0x84E2
#endif
#ifndef GL_MODELVIEW
#define GL_MODELVIEW 0x1700
#endif
#ifndef GL_MODELVIEW_MATRIX
#define GL_MODELVIEW_MATRIX 0x0BA6
#endif
#ifndef GL_MODELVIEW_STACK_DEPTH
#define GL_MODELVIEW_STACK_DEPTH 0x0BA3
#endif
#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif
#ifndef GL_OPERAND0_ALPHA
#define GL_OPERAND0_ALPHA 0x8598
#endif
#ifndef GL_OPERAND0_RGB
#define GL_OPERAND0_RGB 0x8590
#endif
#ifndef GL_OPERAND1_ALPHA
#define GL_OPERAND1_ALPHA 0x8599
#endif
#ifndef GL_OPERAND1_RGB
#define GL_OPERAND1_RGB 0x8591
#endif
#ifndef GL_OPERAND2_ALPHA
#define GL_OPERAND2_ALPHA 0x859A
#endif
#ifndef GL_OPERAND2_RGB
#define GL_OPERAND2_RGB 0x8592
#endif
#ifndef GL_PERSPECTIVE_CORRECTION_HINT
#define GL_PERSPECTIVE_CORRECTION_HINT 0x0C50
#endif
#ifndef GL_POLYGON
#define GL_POLYGON 0x0009
#endif
#ifndef GL_PREVIOUS
#define GL_PREVIOUS 0x8578
#endif
#ifndef GL_PROJECTION
#define GL_PROJECTION 0x1701
#endif
#ifndef GL_PROJECTION_MATRIX
#define GL_PROJECTION_MATRIX 0x0BA7
#endif
#ifndef GL_PROJECTION_STACK_DEPTH
#define GL_PROJECTION_STACK_DEPTH 0x0BA4
#endif
#ifndef GL_QUADS
#define GL_QUADS 0x0007
#endif
#ifndef GL_QUAD_STRIP
#define GL_QUAD_STRIP 0x0008
#endif
#ifndef GL_RGB_SCALE
#define GL_RGB_SCALE 0x8573
#endif
#ifndef GL_SMOOTH
#define GL_SMOOTH 0x1D01
#endif
#ifndef GL_SOURCE0_ALPHA
#define GL_SOURCE0_ALPHA 0x8588
#endif
#ifndef GL_SOURCE0_RGB
#define GL_SOURCE0_RGB 0x8580
#endif
#ifndef GL_SOURCE1_ALPHA
#define GL_SOURCE1_ALPHA 0x8589
#endif
#ifndef GL_SOURCE1_RGB
#define GL_SOURCE1_RGB 0x8581
#endif
#ifndef GL_SOURCE2_ALPHA
#define GL_SOURCE2_ALPHA 0x858A
#endif
#ifndef GL_SOURCE2_RGB
#define GL_SOURCE2_RGB 0x8582
#endif
#ifndef GL_TEXTURE0_ARB
#define GL_TEXTURE0_ARB (GL_TEXTURE0 + 0)
#endif
#ifndef GL_TEXTURE1_ARB
#define GL_TEXTURE1_ARB (GL_TEXTURE0 + 1)
#endif
#ifndef GL_TEXTURE2_ARB
#define GL_TEXTURE2_ARB (GL_TEXTURE0 + 2)
#endif
#ifndef GL_TEXTURE3_ARB
#define GL_TEXTURE3_ARB (GL_TEXTURE0 + 3)
#endif
#ifndef GL_TEXTURE4_ARB
#define GL_TEXTURE4_ARB (GL_TEXTURE0 + 4)
#endif
#ifndef GL_TEXTURE5_ARB
#define GL_TEXTURE5_ARB (GL_TEXTURE0 + 5)
#endif
#ifndef GL_TEXTURE6_ARB
#define GL_TEXTURE6_ARB (GL_TEXTURE0 + 6)
#endif
#ifndef GL_TEXTURE7_ARB
#define GL_TEXTURE7_ARB (GL_TEXTURE0 + 7)
#endif
#ifndef GL_TEXTURE_COORD_ARRAY
#define GL_TEXTURE_COORD_ARRAY 0x8078
#endif
#ifndef GL_TEXTURE_ENV
#define GL_TEXTURE_ENV 0x2300
#endif
#ifndef GL_TEXTURE_ENV_COLOR
#define GL_TEXTURE_ENV_COLOR 0x2201
#endif
#ifndef GL_TEXTURE_ENV_MODE
#define GL_TEXTURE_ENV_MODE 0x2200
#endif
#ifndef GL_TEXTURE_HEIGHT
#define GL_TEXTURE_HEIGHT 0x1001
#endif
#ifndef GL_TEXTURE_WIDTH
#define GL_TEXTURE_WIDTH 0x1000
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Native GLES renderer. All matrix and immediate-mode state is CPU-side;
   draws are submitted with GLES vertex attributes and streaming buffers. */
boolean I_GLES_Initialize(void);
void I_GLES_Shutdown(void);
boolean I_GLES_IsActive(void);
const GLubyte* I_GLES_GetString(GLenum name);

void I_GLES_MatrixMode(GLenum mode);
void I_GLES_LoadIdentity(void);
void I_GLES_LoadMatrixf(const GLfloat* matrix);
void I_GLES_MultMatrixf(const GLfloat* matrix);
void I_GLES_PushMatrix(void);
void I_GLES_PopMatrix(void);
void I_GLES_Ortho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
                  GLdouble nearVal, GLdouble farVal);
void I_GLES_Frustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
                    GLdouble nearVal, GLdouble farVal);
void I_GLES_Translatef(GLfloat x, GLfloat y, GLfloat z);
void I_GLES_Rotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void I_GLES_Scalef(GLfloat x, GLfloat y, GLfloat z);
void I_GLES_GetFloatv(GLenum pname, GLfloat* values);
void I_GLES_GetDoublev(GLenum pname, GLdouble* values);

void I_GLES_Begin(GLenum mode);
void I_GLES_End(void);
void I_GLES_Vertex2f(GLfloat x, GLfloat y);
void I_GLES_Vertex3f(GLfloat x, GLfloat y, GLfloat z);
void I_GLES_Vertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void I_GLES_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void I_GLES_Color4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a);
void I_GLES_Color4ubv(const GLubyte* color);
void I_GLES_TexCoord2f(GLfloat s, GLfloat t);
void I_GLES_Rectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2);
void I_GLES_Recti(GLint x1, GLint y1, GLint x2, GLint y2);

void I_GLES_DrawIndexedGeometry(GLsizei vertexCount, const vtx_t* vertices,
                                GLsizei indexCount, const GLushort* indices);
void I_GLES_SetVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer);
void I_GLES_SetColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer);
void I_GLES_SetTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer);
void I_GLES_EnableClientState(GLenum array);
void I_GLES_DisableClientState(GLenum array);

void I_GLES_Enable(GLenum cap);
void I_GLES_Disable(GLenum cap);
GLboolean I_GLES_IsEnabled(GLenum cap);
void I_GLES_AlphaFunc(GLenum func, GLclampf reference);
void I_GLES_DepthRange(GLclampf nearVal, GLclampf farVal);
void I_GLES_ClearDepth(GLclampd depth);
void I_GLES_Fogf(GLenum pname, GLfloat param);
void I_GLES_Fogfv(GLenum pname, const GLfloat* params);
void I_GLES_Fogi(GLenum pname, GLint param);
void I_GLES_TexEnvi(GLenum target, GLenum pname, GLint param);
void I_GLES_TexEnvfv(GLenum target, GLenum pname, const GLfloat* params);
void I_GLES_TexParameteri(GLenum target, GLenum pname, GLint param);
void I_GLES_TexParameterf(GLenum target, GLenum pname, GLfloat param);
void I_GLES_TexImage2D(GLenum target, GLint level, GLint internalFormat,
                       GLsizei width, GLsizei height, GLint border,
                       GLenum format, GLenum type, const GLvoid* pixels);
void I_GLES_TexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                          GLsizei width, GLsizei height, GLenum format,
                          GLenum type, const GLvoid* pixels);
void I_GLES_ActiveTexture(GLenum texture);
void I_GLES_ClientActiveTexture(GLenum texture);
void I_GLES_BindTexture(GLenum target, GLuint texture);
void I_GLES_GenTextures(GLsizei count, GLuint* textures);
void I_GLES_DeleteTextures(GLsizei count, const GLuint* textures);
void I_GLES_GetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint* params);
void I_GLES_GetTexParameteriv(GLenum target, GLenum pname, GLint* params);
void I_GLES_GetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid* pixels);

void I_GLES_GetIntegerv(GLenum pname, GLint* params);
void I_GLES_GetBooleanv(GLenum pname, GLboolean* params);
void I_GLES_GetFloatvRaw(GLenum pname, GLfloat* params);
void I_GLES_PolygonMode(GLenum face, GLenum mode);
void I_GLES_PolygonOffset(GLfloat factor, GLfloat units);
void I_GLES_LineWidth(GLfloat width);
void I_GLES_ShadeModel(GLenum mode);
void I_GLES_Hint(GLenum target, GLenum mode);

#ifdef __cplusplus
}
#endif
#endif
