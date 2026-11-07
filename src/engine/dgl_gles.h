#ifndef DOOM64_DGL_GLES_H
#define DOOM64_DGL_GLES_H

#include "gl_main.h"

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
