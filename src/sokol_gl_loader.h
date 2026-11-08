/*
 * sokol_gl_loader.h -- sokol_gfx's OpenGL entry points, loaded through SDL (desktop OpenGL on
 * Windows and Linux: SOKOL_GLCORE with SOKOL_EXTERNAL_GL_LOADER; src/sokol_impl.c).
 *
 * The types and constants are SDL's own (SDL_opengl.h, SDL_opengl_glext.h); every function sokol's
 * GL backend calls is a pointer here, filled by SokolGl_Load from SDL_GL_GetProcAddress once a
 * context is current -- so no system GL header is included and no GL library is linked (Cygwin has
 * neither the one sokol's Windows loader wants nor a native one, and the Linux builds keep their
 * DT_NEEDED list). A REQUIRED function the driver lacks fails the load, and the presenter falls
 * back to SDL's renderer; an OPTIONAL one (OpenGL 4.2/4.3 only) stays NULL, and sokol asks the
 * context before it calls those.
 *
 * The list is the gl* names sokol_gfx.h's GL backend uses (regenerate when sokol changes):
 *   sed -n '/>>opengl backend/,/>>[a-z0-9]* backend/p' third_party/sokol/sokol_gfx.h |
 *     grep -o '\bgl[A-Z][A-Za-z0-9]*\s*(' | sort -u
 * plus glReadPixels, glGetString and glFinish for the presenter's own use, and the uniform blocks'
 * (glGetUniformBlockIndex, glUniformBlockBinding) for the slang passes (src/gpu_slang.c).
 */
#ifndef SWOS_SOKOL_GL_LOADER_H
#define SWOS_SOKOL_GL_LOADER_H

#define GL_GLEXT_PROTOTYPES 1       /* (declarations only: their types are what __typeof__ takes) */
#include <SDL_opengl.h>

/* X(name, required) */
#define SWOS_GL_FUNCS \
    X(glActiveTexture, 1) \
    X(glAttachShader, 1) \
    X(glBindBuffer, 1) \
    X(glBindBufferBase, 1) \
    X(glBindBufferRange, 1) \
    X(glGetUniformBlockIndex, 1) \
    X(glGenerateMipmap, 1) \
    X(glUniformBlockBinding, 1) \
    X(glBindFramebuffer, 1) \
    X(glBindImageTexture, 0) \
    X(glBindRenderbuffer, 1) \
    X(glBindSampler, 1) \
    X(glBindTexture, 1) \
    X(glBindVertexArray, 1) \
    X(glBlendColor, 1) \
    X(glBlendEquationSeparate, 1) \
    X(glBlendFuncSeparate, 1) \
    X(glBlitFramebuffer, 1) \
    X(glBufferData, 1) \
    X(glBufferSubData, 1) \
    X(glCheckFramebufferStatus, 1) \
    X(glClearBufferfi, 1) \
    X(glClearBufferfv, 1) \
    X(glClearBufferiv, 1) \
    X(glColorMask, 1) \
    X(glColorMaski, 1) \
    X(glCompileShader, 1) \
    X(glCompressedTexImage2D, 1) \
    X(glCompressedTexImage3D, 1) \
    X(glCompressedTexSubImage2D, 1) \
    X(glCompressedTexSubImage3D, 1) \
    X(glCreateProgram, 1) \
    X(glCreateShader, 1) \
    X(glCullFace, 1) \
    X(glDeleteBuffers, 1) \
    X(glDeleteFramebuffers, 1) \
    X(glDeleteProgram, 1) \
    X(glDeleteRenderbuffers, 1) \
    X(glDeleteSamplers, 1) \
    X(glDeleteShader, 1) \
    X(glDeleteTextures, 1) \
    X(glDeleteVertexArrays, 1) \
    X(glDepthFunc, 1) \
    X(glDepthMask, 1) \
    X(glDisable, 1) \
    X(glDisableVertexAttribArray, 1) \
    X(glDispatchCompute, 0) \
    X(glDrawArrays, 1) \
    X(glDrawArraysInstanced, 1) \
    X(glDrawArraysInstancedBaseInstance, 0) \
    X(glDrawBuffers, 1) \
    X(glDrawElements, 1) \
    X(glDrawElementsBaseVertex, 1) \
    X(glDrawElementsInstanced, 1) \
    X(glDrawElementsInstancedBaseVertex, 1) \
    X(glDrawElementsInstancedBaseVertexBaseInstance, 0) \
    X(glEnable, 1) \
    X(glEnableVertexAttribArray, 1) \
    X(glFinish, 1) \
    X(glFramebufferRenderbuffer, 1) \
    X(glFramebufferTexture2D, 1) \
    X(glFramebufferTextureLayer, 1) \
    X(glFrontFace, 1) \
    X(glGenBuffers, 1) \
    X(glGenFramebuffers, 1) \
    X(glGenRenderbuffers, 1) \
    X(glGenSamplers, 1) \
    X(glGenTextures, 1) \
    X(glGenVertexArrays, 1) \
    X(glGetAttribLocation, 1) \
    X(glGetError, 1) \
    X(glGetIntegerv, 1) \
    X(glGetProgramInfoLog, 1) \
    X(glGetProgramiv, 1) \
    X(glGetShaderInfoLog, 1) \
    X(glGetShaderiv, 1) \
    X(glGetString, 1) \
    X(glGetStringi, 1) \
    X(glGetUniformLocation, 1) \
    X(glInvalidateFramebuffer, 0) \
    X(glLinkProgram, 1) \
    X(glMemoryBarrier, 0) \
    X(glPixelStorei, 1) \
    X(glPolygonOffset, 1) \
    X(glReadBuffer, 1) \
    X(glReadPixels, 1) \
    X(glRenderbufferStorageMultisample, 1) \
    X(glSamplerParameterf, 1) \
    X(glSamplerParameterfv, 1) \
    X(glSamplerParameteri, 1) \
    X(glScissor, 1) \
    X(glShaderSource, 1) \
    X(glStencilFunc, 1) \
    X(glStencilFuncSeparate, 1) \
    X(glStencilMask, 1) \
    X(glStencilOp, 1) \
    X(glStencilOpSeparate, 1) \
    X(glTexImage2D, 1) \
    X(glTexImage2DMultisample, 1) \
    X(glTexImage3D, 1) \
    X(glTexImage3DMultisample, 1) \
    X(glTexParameteri, 1) \
    X(glTexStorage2D, 1) \
    X(glTexStorage2DMultisample, 1) \
    X(glTexStorage3D, 1) \
    X(glTexStorage3DMultisample, 1) \
    X(glTexSubImage2D, 1) \
    X(glTexSubImage3D, 1) \
    X(glTextureView, 0) \
    X(glUniform1fv, 1) \
    X(glUniform1i, 1) \
    X(glUniform1iv, 1) \
    X(glUniform2fv, 1) \
    X(glUniform2iv, 1) \
    X(glUniform3fv, 1) \
    X(glUniform3iv, 1) \
    X(glUniform4fv, 1) \
    X(glUniform4iv, 1) \
    X(glUniformMatrix4fv, 1) \
    X(glUseProgram, 1) \
    X(glVertexAttribDivisor, 1) \
    X(glVertexAttribIPointer, 1) \
    X(glVertexAttribPointer, 1) \
    X(glViewport, 1) \

#define X(name, req) extern __typeof__(name) *swos_##name;
SWOS_GL_FUNCS
#undef X

#define glActiveTexture swos_glActiveTexture
#define glAttachShader swos_glAttachShader
#define glBindBuffer swos_glBindBuffer
#define glBindBufferBase swos_glBindBufferBase
#define glBindBufferRange swos_glBindBufferRange
#define glGetUniformBlockIndex swos_glGetUniformBlockIndex
#define glGenerateMipmap swos_glGenerateMipmap
#define glUniformBlockBinding swos_glUniformBlockBinding
#define glBindFramebuffer swos_glBindFramebuffer
#define glBindImageTexture swos_glBindImageTexture
#define glBindRenderbuffer swos_glBindRenderbuffer
#define glBindSampler swos_glBindSampler
#define glBindTexture swos_glBindTexture
#define glBindVertexArray swos_glBindVertexArray
#define glBlendColor swos_glBlendColor
#define glBlendEquationSeparate swos_glBlendEquationSeparate
#define glBlendFuncSeparate swos_glBlendFuncSeparate
#define glBlitFramebuffer swos_glBlitFramebuffer
#define glBufferData swos_glBufferData
#define glBufferSubData swos_glBufferSubData
#define glCheckFramebufferStatus swos_glCheckFramebufferStatus
#define glClearBufferfi swos_glClearBufferfi
#define glClearBufferfv swos_glClearBufferfv
#define glClearBufferiv swos_glClearBufferiv
#define glColorMask swos_glColorMask
#define glColorMaski swos_glColorMaski
#define glCompileShader swos_glCompileShader
#define glCompressedTexImage2D swos_glCompressedTexImage2D
#define glCompressedTexImage3D swos_glCompressedTexImage3D
#define glCompressedTexSubImage2D swos_glCompressedTexSubImage2D
#define glCompressedTexSubImage3D swos_glCompressedTexSubImage3D
#define glCreateProgram swos_glCreateProgram
#define glCreateShader swos_glCreateShader
#define glCullFace swos_glCullFace
#define glDeleteBuffers swos_glDeleteBuffers
#define glDeleteFramebuffers swos_glDeleteFramebuffers
#define glDeleteProgram swos_glDeleteProgram
#define glDeleteRenderbuffers swos_glDeleteRenderbuffers
#define glDeleteSamplers swos_glDeleteSamplers
#define glDeleteShader swos_glDeleteShader
#define glDeleteTextures swos_glDeleteTextures
#define glDeleteVertexArrays swos_glDeleteVertexArrays
#define glDepthFunc swos_glDepthFunc
#define glDepthMask swos_glDepthMask
#define glDisable swos_glDisable
#define glDisableVertexAttribArray swos_glDisableVertexAttribArray
#define glDispatchCompute swos_glDispatchCompute
#define glDrawArrays swos_glDrawArrays
#define glDrawArraysInstanced swos_glDrawArraysInstanced
#define glDrawArraysInstancedBaseInstance swos_glDrawArraysInstancedBaseInstance
#define glDrawBuffers swos_glDrawBuffers
#define glDrawElements swos_glDrawElements
#define glDrawElementsBaseVertex swos_glDrawElementsBaseVertex
#define glDrawElementsInstanced swos_glDrawElementsInstanced
#define glDrawElementsInstancedBaseVertex swos_glDrawElementsInstancedBaseVertex
#define glDrawElementsInstancedBaseVertexBaseInstance swos_glDrawElementsInstancedBaseVertexBaseInstance
#define glEnable swos_glEnable
#define glEnableVertexAttribArray swos_glEnableVertexAttribArray
#define glFinish swos_glFinish
#define glFramebufferRenderbuffer swos_glFramebufferRenderbuffer
#define glFramebufferTexture2D swos_glFramebufferTexture2D
#define glFramebufferTextureLayer swos_glFramebufferTextureLayer
#define glFrontFace swos_glFrontFace
#define glGenBuffers swos_glGenBuffers
#define glGenFramebuffers swos_glGenFramebuffers
#define glGenRenderbuffers swos_glGenRenderbuffers
#define glGenSamplers swos_glGenSamplers
#define glGenTextures swos_glGenTextures
#define glGenVertexArrays swos_glGenVertexArrays
#define glGetAttribLocation swos_glGetAttribLocation
#define glGetError swos_glGetError
#define glGetIntegerv swos_glGetIntegerv
#define glGetProgramInfoLog swos_glGetProgramInfoLog
#define glGetProgramiv swos_glGetProgramiv
#define glGetShaderInfoLog swos_glGetShaderInfoLog
#define glGetShaderiv swos_glGetShaderiv
#define glGetString swos_glGetString
#define glGetStringi swos_glGetStringi
#define glGetUniformLocation swos_glGetUniformLocation
#define glInvalidateFramebuffer swos_glInvalidateFramebuffer
#define glLinkProgram swos_glLinkProgram
#define glMemoryBarrier swos_glMemoryBarrier
#define glPixelStorei swos_glPixelStorei
#define glPolygonOffset swos_glPolygonOffset
#define glReadBuffer swos_glReadBuffer
#define glReadPixels swos_glReadPixels
#define glRenderbufferStorageMultisample swos_glRenderbufferStorageMultisample
#define glSamplerParameterf swos_glSamplerParameterf
#define glSamplerParameterfv swos_glSamplerParameterfv
#define glSamplerParameteri swos_glSamplerParameteri
#define glScissor swos_glScissor
#define glShaderSource swos_glShaderSource
#define glStencilFunc swos_glStencilFunc
#define glStencilFuncSeparate swos_glStencilFuncSeparate
#define glStencilMask swos_glStencilMask
#define glStencilOp swos_glStencilOp
#define glStencilOpSeparate swos_glStencilOpSeparate
#define glTexImage2D swos_glTexImage2D
#define glTexImage2DMultisample swos_glTexImage2DMultisample
#define glTexImage3D swos_glTexImage3D
#define glTexImage3DMultisample swos_glTexImage3DMultisample
#define glTexParameteri swos_glTexParameteri
#define glTexStorage2D swos_glTexStorage2D
#define glTexStorage2DMultisample swos_glTexStorage2DMultisample
#define glTexStorage3D swos_glTexStorage3D
#define glTexStorage3DMultisample swos_glTexStorage3DMultisample
#define glTexSubImage2D swos_glTexSubImage2D
#define glTexSubImage3D swos_glTexSubImage3D
#define glTextureView swos_glTextureView
#define glUniform1fv swos_glUniform1fv
#define glUniform1i swos_glUniform1i
#define glUniform1iv swos_glUniform1iv
#define glUniform2fv swos_glUniform2fv
#define glUniform2iv swos_glUniform2iv
#define glUniform3fv swos_glUniform3fv
#define glUniform3iv swos_glUniform3iv
#define glUniform4fv swos_glUniform4fv
#define glUniform4iv swos_glUniform4iv
#define glUniformMatrix4fv swos_glUniformMatrix4fv
#define glUseProgram swos_glUseProgram
#define glVertexAttribDivisor swos_glVertexAttribDivisor
#define glVertexAttribIPointer swos_glVertexAttribIPointer
#define glVertexAttribPointer swos_glVertexAttribPointer
#define glViewport swos_glViewport

/* Every entry point, from the current context; 0 when all the required ones are there. */
int SokolGl_Load(void);

#endif
