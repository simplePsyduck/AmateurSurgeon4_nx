/* as4_egl.c -- the game's EGL, as its GLSurfaceView made it.
 *
 * The engine imports no EGL: on Android the GLSurfaceView (ES 2, a depth
 * buffer) creates the display, the window surface and the context, and the
 * Cocos2dxRenderer runs on its GL thread. This does the same with Mesa's EGL
 * on the Switch's default window. The null renderer (DCR_GL_MESA 0) does
 * nothing and every call succeeds. MIT.
 */
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "as4.h"
#include "util.h"

#if DCR_GL_MESA
#include <EGL/egl.h>
#include <EGL/eglext.h>

void dcr_window_prepare(void);                       /* android_ndk.c */
EGLBoolean b_eglSwapBuffers(EGLDisplay d, EGLSurface s); /* gl_mesa.c: frame count, hooks */

static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLConfig g_cfg;
static EGLSurface g_win = EGL_NO_SURFACE;
static EGLContext g_ctx = EGL_NO_CONTEXT;

static int choose(const EGLint *attrs) {
  EGLint n = 0;
  return eglChooseConfig(g_dpy, attrs, &g_cfg, 1, &n) && n > 0;
}

int as4_egl_init(void) {
  if (log_console_active())
    debugPrintf("[egl] handing the screen from the boot log to the game\n");
  log_console_close();
  dcr_window_prepare();
  g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  EGLint maj = 0, min = 0;
  if (!eglInitialize(g_dpy, &maj, &min)) {
    debugPrintf("[egl] eglInitialize failed 0x%x\n", eglGetError());
    return -1;
  }
  eglBindAPI(EGL_OPENGL_ES_API);
  static const EGLint full[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE};
  static const EGLint depth16[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                   EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                                   EGL_DEPTH_SIZE, 16, EGL_NONE};
  static const EGLint any[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE};
  if (!choose(full) && !choose(depth16) && !choose(any)) {
    debugPrintf("[egl] no ES2 config (0x%x)\n", eglGetError());
    return -1;
  }
  g_win = eglCreateWindowSurface(g_dpy, g_cfg, (EGLNativeWindowType)nwindowGetDefault(), NULL);
  static const EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  g_ctx = eglCreateContext(g_dpy, g_cfg, EGL_NO_CONTEXT, ctx_attrs);
  if (g_win == EGL_NO_SURFACE || g_ctx == EGL_NO_CONTEXT || !eglMakeCurrent(g_dpy, g_win, g_win, g_ctx)) {
    debugPrintf("[egl] surface %p / context %p / make current failed (0x%x)\n", g_win, g_ctx,
                eglGetError());
    return -1;
  }
  eglSwapInterval(g_dpy, 1);
  const char *(*gs)(unsigned) = (const char *(*)(unsigned))eglGetProcAddress("glGetString");
  debugPrintf("[egl] EGL %d.%d; %s | %s\n", maj, min, gs ? gs(0x1F01) : "?", gs ? gs(0x1F02) : "?");
  return 0;
}

void as4_egl_swap(void) { b_eglSwapBuffers(g_dpy, g_win); }

#else /* the null renderer: every call succeeds, nothing is drawn */
int as4_egl_init(void) { return 0; }
void as4_egl_swap(void) {}
#endif
