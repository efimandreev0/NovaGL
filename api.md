# NovaGL API Reference

Status legend: ✅ **Full** — works as spec says. ⚠️ **Partial** — works but with asterisks. ❌ **Stub** — compiles, does nothing useful.

---

## NovaGL-specific API

These don't exist in stock GL. Use them.

| Function | Status | Notes |
|---|---|---|
| `nova_init()` | ✅ Full | Initialises Citro3D + all NovaGL state. Call before anything else. |
| `nova_init_ex(cmd, array, index, staging)` | ✅ Full | Same but you pick the buffer sizes. Useful if the 2MB defaults are too fat. |
| `nova_fini()` | ✅ Full | Frees everything. |
| `nova_set_render_target(mode)` | ✅ Full | 0 = top screen, 1 = right eye, 2 = bottom screen. |
| `novaSwapBuffers()` | ✅ Full | Ends frame, presents, resets render target to top screen. |
| `novaGetEyeCount()` | ✅ Full | Returns 1 or 2 depending on stereoscopic slider. |
| `novaBeginEye(eye)` | ✅ Full | Switch render target to left (0) or right (1) eye. |
| `novaSet3DDepth(depth)` | ✅ Full | Stereo separation for next frame. |
| `nova_invalidate_state_cache()` | ✅ Full | Flush the "last applied state" cache. Call after level loads or when foreign code touches Citro3D directly. |
| `novaInvalidateStateCache()` | ✅ Full | Same thing, capitalized alias for engine integration. |
| `nova_texture_cache_set_directory(dir)` | ✅ Full | Point to SD card dir for persistent texture cache. Pass NULL to disable. |
| `nova_texture_cache_has(hash)` | ✅ Full | Check if a cache entry exists without loading it. |
| `nova_texture_cache_load(hash, w, h)` | ✅ Full | Load cached swizzled texture into the bound slot. Skips PNG decode + morton pass. |
| `nova_texture_cache_save(hash)` | ✅ Full | Persist the bound texture's swizzled data to disk. |
| `novaVertexPointerFast(vbo, offset)` | ✅ Full | Fast-lane: commit vertex buffer once, skip per-draw dispatch overhead. |
| `novaIndexPointerFast(vbo, type)` | ✅ Full | Fast-lane: same for index buffer. |
| `novaDrawObjects(mode, count)` | ✅ Full | Fast-lane draw call (assumes fixed 24-byte PTC layout). |
| `novaDrawObjectsIndexed(mode, count, indices)` | ✅ Full | Indexed variant of the above. |
| `novaCreateRenderTextureFBO(w, h, depth, tex, fbo)` | ✅ Full | Allocates a proper VRAM-backed render texture + FBO in one call. Use this instead of the vanilla glGenFramebuffers path for things that need to actually work. |
| `novaBlitTargetToFBO(src, dst)` | ✅ Full | Copies one render target into another via textured quad. PICA has no glBlitFramebuffer equivalent, this is the replacement. |
| `novaSetExplicitTevStages(count, stages)` | ✅ Full | Direct PICA TEV programming, bypasses GL_COMBINE machinery. For multi-stage CC / N64-style two-cycle modes. |
| `novaClearExplicitTevStages()` | ✅ Full | Revert to GL_COMBINE-driven TEV. |
| `novaBeginClipSpace2D()` | ✅ Full | Switch to clip-space passthrough shader (no MVP, no fog, ~10 dp4 saved per vertex). For batched UI/HUD. |
| `novaEndClipSpace2D()` | ✅ Full | Restore normal shader. |
| `novaDrawClipspaceTris(verts, count)` | ✅ Full | Draw pre-packed clip-space verts directly. Fastest path for UI rendering. |
| `novaGetTexCoordScale(tex, su, sv)` | ✅ Full | Returns logical/POT UV scale for NPOT textures. Multiply your texcoords by this when using the clip-space fast lane. |
| `novaGetScreenTextureId()` | ✅ Full | Returns a GL texture id that aliases the on-screen surface for sampling. Read-after-write hazard — finish your frame writes first. |
| `novaglGetProcAddress(name)` | ✅ Full | Extension function lookup table. O(N) scan, only called on init, fine. |
| `novaglGetFuncName(func)` | ✅ Full | Debug helper: enum value → function name string. |

---

## Core State

| Function | Status | Notes |
|---|---|---|
| `glEnable(cap)` / `glDisable(cap)` | ✅ Full | All meaningful PICA caps supported: DEPTH_TEST, BLEND, ALPHA_TEST, CULL_FACE, TEXTURE_2D, SCISSOR_TEST, STENCIL_TEST, FOG, LIGHTING, LIGHT0–7, POLYGON_OFFSET_FILL, LINE_SMOOTH, client-state arrays. Everything else silently ignored. |
| `glIsEnabled(cap)` | ✅ Full | Returns correct state for all caps listed above. |
| `glGetIntegerv(pname, params)` | ✅ Full | Covers viewport, scissor, texture/buffer bindings, matrix stack depths, blend state, depth state, stencil state, framebuffer binding, max texture size (1024), etc. |
| `glGetFloatv(pname, params)` | ✅ Full | Covers matrix queries, fog params, depth range, clear values, current color. Falls back to integer path for unknown pnames. |
| `glGetString(name)` | ✅ Full | VENDOR = "NovaGL", RENDERER = "PICA200 (3DS)", VERSION = "OpenGL ES-CM 1.1 NovaGL". |
| `glGetError()` | ✅ Full | Tracks INVALID_ENUM, INVALID_VALUE, INVALID_OPERATION, STACK_OVERFLOW/UNDERFLOW, OUT_OF_MEMORY. Clears on read. |
| `glHint(target, mode)` | ❌ Stub | No-op. PICA doesn't have quality hints. |
| `glFlush()` | ⚠️ Partial | Submits the pending command list via C3D_FrameSplit. Not a true flush. |
| `glFinish()` | ⚠️ Partial | Same as glFlush. No GPU-idle wait beyond what Citro3D provides. |
| `glPushAttrib(mask)` / `glPopAttrib()` | ⚠️ Partial | Saves/restores: depth test, blend, alpha test, cull face, scissor, fog, texture 2D per unit, blend func, alpha func, cull mode, front face. Does NOT save lighting or material state. Stack depth 16. |
| `glPushClientAttrib(mask)` / `glPopClientAttrib()` | ⚠️ Partial | Saves/restores vertex/color/texcoord/normal array enables only. Stack depth 16. |

---

## Clearing

| Function | Status | Notes |
|---|---|---|
| `glClear(mask)` | ✅ Full | COLOR, DEPTH, STENCIL bits work. D24S8 is a single HW register so depth-only clear also rewrites stencil to `clear_stencil` value and vice versa. That's a hardware limitation, not a bug. |
| `glClearColor(r, g, b, a)` | ✅ Full | |
| `glClearDepthf(depth)` / `glClearDepth(depth)` | ✅ Full | |
| `glClearStencil(s)` | ✅ Full | |

---

## Rasterization

| Function | Status | Notes |
|---|---|---|
| `glViewport(x, y, w, h)` | ✅ Full | Swaps x/y for PICA's rotated screen when drawing to the screen target. |
| `glScissor(x, y, w, h)` | ✅ Full | |
| `glDepthFunc(func)` | ✅ Full | All 8 comparison funcs. Inverted to match PICA convention internally. |
| `glDepthMask(flag)` | ✅ Full | |
| `glDepthRangef` / `glDepthRange` | ✅ Full | |
| `glBlendFunc(src, dst)` | ✅ Full | All ES 1.1 factors including SRC_ALPHA_SATURATE. |
| `glBlendEquation(mode)` | ✅ Full | ADD, SUBTRACT, REVERSE_SUBTRACT, MIN, MAX — real GPU_BLENDEQUATION path, not a stub. |
| `glBlendEquationSeparate` | ✅ Full | Separate RGB/alpha equations, both OES and core spellings. |
| `glAlphaFunc(func, ref)` | ✅ Full | |
| `glCullFace(mode)` | ✅ Full | |
| `glFrontFace(mode)` | ✅ Full | |
| `glColorMask(r, g, b, a)` | ✅ Full | |
| `glStencilFunc(func, ref, mask)` | ✅ Full | Real C3D_StencilTest path. Can be compiled out with `-DNOVAGL_DISABLE_STENCIL=1` if citro3d asserts on your setup. |
| `glStencilMask(mask)` | ✅ Full | |
| `glStencilOp(fail, zfail, zpass)` | ✅ Full | KEEP, ZERO, REPLACE, INCR, DECR, INVERT. |
| `glPolygonOffset(factor, units)` | ⚠️ Partial | `factor` adjusts the depth map. `units` is accepted but PICA's depth bias works differently from desktop GL so the result isn't spec-accurate. |
| `glLineWidth(width)` | ❌ Stub | PICA doesn't have programmable line width. Always 1 pixel. |
| `glPolygonMode(face, mode)` | ❌ Stub | PICA has no wireframe mode. Silently ignored. |
| `glClipPlane(plane, eq)` | ❌ Stub | PICA has no user-defined clip planes. Silently ignored. |
| `glPixelStorei(pname, param)` / `glPixelStoref` | ✅ Full | UNPACK_ALIGNMENT and PACK_ALIGNMENT. |
| `glDrawBuffer(mode)` | ❌ Stub | No-op. |
| `glReadBuffer(mode)` | ❌ Stub | No-op. |

---

## Matrix Stack

| Function | Status | Notes |
|---|---|---|
| `glMatrixMode(mode)` | ✅ Full | MODELVIEW, PROJECTION, TEXTURE. |
| `glLoadIdentity()` | ✅ Full | |
| `glPushMatrix()` / `glPopMatrix()` | ✅ Full | 32-deep stack per mode. |
| `glLoadMatrixf(m)` / `glMultMatrixf(m)` | ✅ Full | |
| `glLoadMatrixd(m)` / `glMultMatrixd(m)` | ✅ Full | Double-precision inputs cast to float internally. |
| `glTranslatef` / `glRotatef` / `glScalef` | ✅ Full | |
| `glTranslated` / `glRotated` / `glScaled` | ✅ Full | |
| `glOrtho` / `glOrthof` | ✅ Full | |
| `glFrustum` / `glFrustumf` / `glFrustumx` | ✅ Full | Including the fixed-point x variant. |

---

## Textures

| Function | Status | Notes |
|---|---|---|
| `glGenTextures` / `glDeleteTextures` / `glBindTexture` | ✅ Full | Up to 2048 texture slots. Deletion queues GPU-side cleanup for deferred frame safety. |
| `glIsTexture(tex)` | ✅ Full | |
| `glTexImage2D` | ✅ Full | Handles RGB, RGBA, LUMINANCE, LUMINANCE_ALPHA, ALPHA, BGR, BGRA, packed types (5_6_5, 4_4_4_4, 5_5_5_1). Auto-downscales to nearest PoT ≤ 1024. Morton-swizzles to PICA tiling on CPU. |
| `glTexSubImage2D` | ✅ Full | Updates a sub-region, re-swizzles only the affected tiles. |
| `glTexParameteri/f/fv/iv` | ✅ Full | MIN/MAG filter (including mip variants), WRAP_S/T (CLAMP, REPEAT, CLAMP_TO_EDGE, MIRRORED_REPEAT), GENERATE_MIPMAP. |
| `glCompressedTexImage2D` | ⚠️ Partial | ETC1 (GL_ETC1_RGB8_OES) works. PVRTC formats are accepted for compatibility, may not render correctly. |
| `glCopyTexSubImage2D` | ⚠️ Partial | Reads from current render target, untiles, re-swizzles into the texture. Works but is slow (CPU per-pixel loop). |
| `glCopyTexImage2D` | ⚠️ Partial | Same caveats. |
| `glTexImage1D` / `glTexSubImage1D` | ❌ Stub | PICA has no 1D textures. |
| `glTexGen*` | ❌ Stub | SPHERE_MAP, EYE_LINEAR, OBJECT_LINEAR — none of these map to PICA. |
| `glActiveTexture` / `glClientActiveTexture` | ✅ Full | Up to 3 texture units (PICA limit). |
| `glActiveTextureARB` / `glClientActiveTextureARB` | ✅ Full | ARB aliases, same implementation. |
| `glMultiTexCoord4f` / `glMultiTexCoord2fARB` | ✅ Full | |
| `glTexEnvi` / `glTexEnvf` / `glTexEnvfv` | ✅ Full | GL_TEXTURE_ENV_MODE: MODULATE, REPLACE, DECAL, BLEND, ADD. Full GL_COMBINE path: REPLACE, MODULATE, ADD, INTERPOLATE, SUBTRACT, plus the `GL_MULT_ADD_NOVA` extension that maps to PICA's GPU_MULTIPLY_ADD. |

---

## Vertex Arrays & Drawing

| Function | Status | Notes |
|---|---|---|
| `glVertexPointer` / `glTexCoordPointer` / `glColorPointer` / `glNormalPointer` | ✅ Full | Accepts interleaved or separate arrays from linear heap or VBO-backed memory. |
| `glFogCoordPointer` | ❌ Stub | Stored but never read. NovaGL computes fog from depth in the shader. |
| `glEnableClientState` / `glDisableClientState` | ✅ Full | VERTEX_ARRAY, COLOR_ARRAY, TEXTURE_COORD_ARRAY, NORMAL_ARRAY, FOG_COORDINATE_ARRAY (last one: accepted, ignored). |
| `glDrawArrays(mode, first, count)` | ✅ Full | TRIANGLES, TRIANGLE_STRIP, TRIANGLE_FAN, QUADS (fan-converted), LINES, LINE_STRIP, LINE_LOOP, POINTS (points fallback to degenerate triangles). |
| `glDrawElements(mode, count, type, indices)` | ✅ Full | UNSIGNED_BYTE and UNSIGNED_SHORT index types. |
| `glDrawRangeElements` | ✅ Full | Hint ignored, forwards to glDrawElements. |
| `glDrawElementsBaseVertex` / `glDrawRangeElementsBaseVertex` | ✅ Full | CPU-side index offset when basevertex ≠ 0. Allocates a scratch buffer for the rebuild. |
| `glArrayElement(i)` | ✅ Full | Used in display list recording. |
| `glGenVertexArrays` / `glBindVertexArray` / `glDeleteVertexArrays` | ✅ Full | Real VAO implementation that stores the client array state per object. |
| `glVertexAttribPointer` / `glEnableVertexAttribArray` / `glDisableVertexAttribArray` | ❌ Stub | PICA is fixed-function. These exist so GL 2.0-style code compiles. |

---

## Buffer Objects (VBO)

| Function | Status | Notes |
|---|---|---|
| `glGenBuffers` / `glDeleteBuffers` / `glBindBuffer` | ✅ Full | Up to 32768 VBO slots. Backed by linearAlloc for direct GPU access. |
| `glBufferData(target, size, data, usage)` | ✅ Full | ARRAY_BUFFER and ELEMENT_ARRAY_BUFFER. STATIC/DYNAMIC/STREAM_DRAW accepted (hint only, no behaviour difference). |
| `glBufferSubData` | ✅ Full | |
| `glMapBuffer(target, access)` | ✅ Full | Returns direct pointer to linearAlloc'd VBO memory. access mode accepted but all modes give a writable pointer. |
| `glMapBufferRange` | ✅ Full | INVALIDATE_* bits accepted. Offset applied to the returned pointer. |
| `glUnmapBuffer` | ✅ Full | Flushes dcache for the mapped range, returns GL_TRUE. |

---

## Immediate Mode

| Function | Status | Notes |
|---|---|---|
| `glBegin(mode)` / `glEnd()` | ✅ Full | Emulated via an internal ring buffer. Starts at 256 verts and doubles if needed, up to half the client array ring. |
| `glVertex2/3/4 f/d/i/s/fv/dv/iv/sv` | ✅ Full | All type/dimension variants. |
| `glTexCoord1/2/3/4 f/d/i/s/fv/dv/iv/sv` | ✅ Full | All variants. |
| `glNormal3 f/b/d/i/s/fv/bv/dv/iv/sv` | ✅ Full | All variants. |
| `glColor3/4 f/b/d/i/s/ub/ui/us + vector variants` | ✅ Full | Yes, all 40+ glColor variants. Don't ask how many lines that is. |

---

## Fog

| Function | Status | Notes |
|---|---|---|
| `glFogf` / `glFogfv` / `glFogi` / `glFogiv` / `glFogx` | ✅ Full | GL_LINEAR, GL_EXP, GL_EXP2 modes. Computed in vertex shader from depth, not fragment depth — close enough for everything we've tested. FOG_COORDINATE_SOURCE and NV_fog_distance hints are accepted and ignored. |

---

## Lighting

| Function | Status | Notes |
|---|---|---|
| `glMaterialf/fv/i/iv(face, pname, ...)` | ⚠️ Partial | State is fully stored. Uploaded to C3D_LightEnv when GL_LIGHTING is enabled. Works on real HW, Citra's emulation of the lighting unit is incomplete. |
| `glGetMaterialfv/iv` | ✅ Full | Returns stored values. |
| `glLightf/fv/i/iv(light, pname, ...)` | ⚠️ Partial | Same story. Position is passed as raw world-space — no eye-space transform. Spot cutoff/exponent and attenuation are stored but partially mapped to PICA's different attenuation model. |
| `glGetLightfv` | ✅ Full | |
| `glLightModelf/fv/i/iv` | ⚠️ Partial | AMBIENT stored and applied as scene ambient. LOCAL_VIEWER and TWO_SIDE stored, not fully wired. |
| `glShadeModel(mode)` | ✅ Full | FLAT / SMOOTH stored. PICA is always Gouraud so FLAT is best-effort. |
| `glColorMaterial(face, mode)` | ✅ Full | Stored, applied during lighting setup. |

---

## Display Lists

| Function | Status | Notes |
|---|---|---|
| `glGenLists(range)` / `glDeleteLists` | ✅ Full | Up to 512 lists. |
| `glNewList(list, mode)` / `glEndList()` | ⚠️ Partial | GL_COMPILE and GL_COMPILE_AND_EXECUTE. Records a fixed subset of ops: DrawArrays, DrawElements, BindTexture, Color, TexEnv, Enable/Disable, MatrixMode + transforms, Begin/End vertex data. Max 64 ops per list. |
| `glCallList(list)` / `glCallLists` | ⚠️ Partial | Replays recorded ops. BYTE/UNSIGNED_BYTE/SHORT/UNSIGNED_SHORT/INT/UNSIGNED_INT/FLOAT list types in glCallLists. Nested call lists not supported. |

---

## Framebuffer Objects

| Function | Status | Notes |
|---|---|---|
| `glGenFramebuffers` / `glDeleteFramebuffers` / `glBindFramebuffer` | ✅ Full | |
| `glFramebufferTexture2D` | ✅ Full | Attaches a texture to the bound FBO and creates a C3D_RenderTarget from it. |
| `glCheckFramebufferStatus` | ⚠️ Partial | Returns FRAMEBUFFER_COMPLETE if a color texture is attached. More nuanced status codes not implemented. |
| `glBlitFramebuffer` | ❌ Stub | PICA has no HW blit. Use `novaBlitTargetToFBO()` instead. |
| `glGenRenderbuffers` / `glDeleteRenderbuffers` / `glBindRenderbuffer` | ⚠️ Partial | Allocation tracked, but renderbuffers don't back real PICA targets. |
| `glRenderbufferStorage` / `glFramebufferRenderbuffer` | ❌ Stub | Accepted for compatibility, does nothing meaningful. |
| `glReadPixels` | ⚠️ Partial | Works. Slow — CPU per-pixel Morton untile + axis swap. Full-screen RGBA reads can optionally use DisplayTransfer HW path (`-DNOVAGL_ENABLE_GLREADPIXELS_HW=1`) but that path skips the rotation, so it's opt-in only. |

---

## GL 2.0+ Shader Pipeline

None of this runs on PICA. These exist so ports that link against GL 2.0 headers compile without `#ifdef`.

| Function | Status | Notes |
|---|---|---|
| `glCreateShader` / `glDeleteShader` | ❌ Stub | Returns 0 by default. Define `-DNOVAGL_GL2_RETURN_DUMMY=1` to return a fake non-zero id so code that does `if (glCreateProgram())` doesn't fall into the wrong branch. |
| `glShaderSource` / `glCompileShader` / `glGetShaderiv` / `glGetShaderInfoLog` | ❌ Stub | |
| `glCreateProgram` / `glAttachShader` / `glLinkProgram` / `glUseProgram` / `glDeleteProgram` | ❌ Stub | |
| `glGetProgramiv` / `glGetProgramInfoLog` | ❌ Stub | |
| `glGetUniformLocation` / `glGetAttribLocation` | ❌ Stub | Always returns -1. |
| `glUniform1i/1f/2f/3f/4f/Matrix4fv` | ❌ Stub | |

---

## EGL

NovaGL doesn't need EGL on 3DS. These exist purely so engines that initialise via EGL don't crash before they get to the GL calls.

| Function | Status | Notes |
|---|---|---|
| `eglBindAPI` / `eglQueryAPI` | ❌ Stub | Accepts OPENGL_ES_API, returns it back. |
| `eglGetDisplay` / `eglGetError` | ❌ Stub | GetDisplay returns a fake non-null. GetError returns EGL_SUCCESS. |
| `eglSwapInterval` / `eglSwapBuffers` | ❌ Stub | SwapBuffers calls novaSwapBuffers internally so some ports work without changes. |
| `eglGetSystemTimeNV` / `eglGetSystemTimeFrequencyNV` | ❌ Stub | Returns 0. Use libctru's timer functions instead. |
| `eglGetProcAddress` | ⚠️ Partial | Forwards to `novaglGetProcAddress`. |

---

## Compatibility Shims

| Function | Status | Notes |
|---|---|---|
| `gladLoadGLLoader` / `gladLoadGLES1Loader` / `gladLoadGLES2Loader` | ❌ Stub | Reports success (returns non-zero) so glad-based init code proceeds. NovaGL links its symbols directly. |
| `glDebugMessageCallback` / `glDebugMessageCallbackKHR` | ❌ Stub | Stores the callback pointer, never calls it. PICA has no async debug stream. |
| `glBlendEquationOES` / `glBlendEquationSeparateOES` | ✅ Full | OES extension aliases, identical to core. |
