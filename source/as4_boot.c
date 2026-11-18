/* as4_boot.c -- plays the part of the game's Android activity and its
 * GLSurfaceView.
 *
 * GameActivity.onCreate, in Android's order, and what happens here:
 *   System.loadLibrary          the engine's constructors (as4_loader.c), then
 *                               JNI_OnLoad
 *   Cocos2dxHelper.init         nativeSetApkPath, nativeSetWriteablePath,
 *                               nativeSetCachePath, nativeSetExternalAssetPath
 *   GL thread: onSurfaceCreated EGL (as4_egl.c)
 *              onSurfaceChanged Cocos2dxRenderer.nativeInit(w, h)
 *              onDrawFrame      Cocos2dxRenderer.nativeRender(), then the swap
 *   Touches                     Cocos2dxRenderer.nativeTouchesBegin/Move/End
 *   Back                        Cocos2dxRenderer.nativeKeyDown(KEYCODE_BACK)
 *   HOME / sleep                nativeOnPause / nativeOnResume
 * This (the main) thread IS the GL thread: it presents a frame per display
 * refresh. Closing the game from HOME is Android's onDestroy, which ends the
 * process: pause (the game saves), then exit. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "as4.h"
#include "config.h"
#include "dcr_config.h"
#include "dcr_path.h"
#include "error.h"
#include "gl_layer.h"
#include "jni.h"
#include "rt_applet.h"
#include "util.h"
#include "watchdog.h"

void dcr_watchdog_start(void);
void dcr_boost_poll(void);
void dcr_boost_launch_end(void);

#define A_FILES DCR_ANDROID_FILES "/"
#define A_CACHE DCR_ANDROID_CACHE "/"
#define A_EXT   DCR_ANDROID_EXT_FILES "/"

#define HELPER   "Java_org_cocos2dx_lib_Cocos2dxHelper_"
#define RENDERER "Java_org_cocos2dx_lib_Cocos2dxRenderer_"

typedef jint (*fn_onload)(void *vm, void *reserved);
typedef void (*fn_s)(void *env, void *thiz, void *s);
typedef void (*fn_v)(void *env, void *thiz);
typedef void (*fn_ii)(void *env, void *thiz, jint a, jint b);
typedef void (*fn_ifl)(void *env, void *thiz, jint id, jfloat x, jfloat y);
typedef void (*fn_arr)(void *env, void *thiz, void *ids, void *xs, void *ys);
typedef jboolean (*fn_key)(void *env, void *thiz, jint key);

static struct {
  fn_s set_apk, set_writable, set_cache, set_ext;
  fn_ii init;
  fn_v render, on_pause, on_resume, did_active;
  fn_ifl begin, end;
  fn_arr move;
  fn_key key;
} N;

static JObj *g_renderer, *g_helper;
static int g_w, g_h;
static int g_resumed;

int as4_surface_w(void) { return g_w ? g_w : RT_SCREEN_W; }
int as4_surface_h(void) { return g_h ? g_h : RT_SCREEN_H; }

uint64_t dcr_boot_frames(void) { return dcr_gl_frames(); }

static void *need(const char *sym) {
  void *p = as4_native(sym);
  if (!p)
    debugPrintf("[boot] MISSING native %s\n", sym);
  return p;
}

static void set_path(fn_s fn, const char *path) {
  if (!fn)
    return;
  JObj *s = jni_str(path);
  fn(g_jni_env, g_helper, s);
  jni_release(s);
  jni_exception_report("Cocos2dxHelper.nativeSet*Path");
}

/* ------------------------------------------------------------- the input */
void as4_touch_begin(int id, float x, float y) {
  if (g_resumed && N.begin)
    N.begin(g_jni_env, g_renderer, id, x, y);
}

void as4_touch_end(int id, float x, float y) {
  if (g_resumed && N.end)
    N.end(g_jni_env, g_renderer, id, x, y);
}

void as4_touch_move(int id, float x, float y) {
  if (!g_resumed || !N.move)
    return;
  /* nativeTouchesMove takes arrays: ids, xs, ys */
  JObj *ids = jni_array('I', 1), *xs = jni_array('F', 1), *ys = jni_array('F', 1);
  ((jint *)ids->a.data)[0] = id;
  ((jfloat *)xs->a.data)[0] = x;
  ((jfloat *)ys->a.data)[0] = y;
  N.move(g_jni_env, g_renderer, ids, xs, ys);
  jni_release(ids);
  jni_release(xs);
  jni_release(ys);
}

void as4_key_down(int key) {
  if (g_resumed && N.key)
    N.key(g_jni_env, g_renderer, key);
}

/* ------------------------------------------------------------- lifecycle */
static void do_resume(void) {
  if (!g_resumed) {
    debugPrintf("[boot] nativeOnResume\n");
    if (N.on_resume)
      N.on_resume(g_jni_env, g_renderer);
    if (N.did_active)
      N.did_active(g_jni_env, g_renderer);
    g_resumed = 1;
  }
}

static void do_pause(void) {
  if (g_resumed) {
    debugPrintf("[boot] nativeOnPause\n");
    if (N.on_pause)
      N.on_pause(g_jni_env, g_renderer);
    g_resumed = 0;
  }
}

/* HOME, sleep, an overlay (rt_applet.c calls these from rt_applet_poll in the
 * frame loop; it then writes out the log ring and stops the game's clocks,
 * and starts them again before port_focus_gained). */
void port_focus_lost(void) { do_pause(); }
void port_focus_gained(void) { do_resume(); }

static void exit_guard(void *arg) {
  (void)arg;
  svcSleepThread(5000000000ll);
  debugPrintf("[boot] the game did not close within 5 s: ending the process\n");
  log_flush_ring();
  svcExitProcess();
}

static void exit_guard_start(void) {
  static Thread t;
  if (R_FAILED(threadCreate(&t, exit_guard, NULL, NULL, 0x4000, 0x2B, -2)) || R_FAILED(threadStart(&t)))
    debugPrintf("[boot] no exit backstop thread\n");
}

static void report(void) {
  static u64 last_tick;
  static unsigned long last_presented;
  u64 tick = armGetSystemTick();
  unsigned long presented = (unsigned long)dcr_gl_frames();
  double fps = last_tick ? (double)(presented - last_presented) * 1e9 /
                               (double)armTicksToNs(tick - last_tick)
                         : 0.0;
  last_tick = tick;
  last_presented = presented;
  debugPrintf("[boot] %lu frames presented (%.1f fps), %d Java objects\n", presented, fps,
              jni_live_objects());
}

static void housekeeping(int *launch_done, unsigned long *quiet_at, u64 *last_report) {
  dcr_boost_poll();
  unsigned long frames = (unsigned long)dcr_gl_frames();
  if (!*launch_done && frames > 0) {
    *launch_done = 1;
    dcr_boost_launch_end();
    debugPrintf("[boot] first frame presented\n");
    *quiet_at = frames + 180;
  }
  /* From ~3 s after the first picture the log goes to a RAM ring (util.c),
   * written out every 10 s and by the watchdog. */
  if (*quiet_at && frames >= *quiet_at) {
    *quiet_at = 0;
    log_set_quiet(1);
  }
  u64 now = armGetSystemTick();
  if (armTicksToNs(now - *last_report) >= 10000000000ull) {
    *last_report = now;
    report();
    log_flush_ring();
  }
}

int as4_boot_run(void) {
  N.set_apk = need(HELPER "nativeSetApkPath");
  N.set_writable = need(HELPER "nativeSetWriteablePath");
  N.set_cache = as4_native(HELPER "nativeSetCachePath");
  N.set_ext = as4_native(HELPER "nativeSetExternalAssetPath");
  N.init = need(RENDERER "nativeInit");
  N.render = need(RENDERER "nativeRender");
  N.on_pause = need(RENDERER "nativeOnPause");
  N.on_resume = need(RENDERER "nativeOnResume");
  N.did_active = as4_native(RENDERER "didBecomeActive");
  N.begin = need(RENDERER "nativeTouchesBegin");
  N.move = need(RENDERER "nativeTouchesMove");
  N.end = need(RENDERER "nativeTouchesEnd");
  N.key = as4_native(RENDERER "nativeKeyDown");
  if (!N.init || !N.render || !N.set_apk)
    fatal_error("%s has no cocos2d-x natives: is the APK Amateur Surgeon 4 (com.turner.amateursurgeon4)?",
                AS4_LIB_GAME);

  /* the surface size first: the engine may ask for it from its first call */
  g_w = dcr_config()->res_w;
  g_h = dcr_config()->res_h;
  as4_java_init();
  g_renderer = jni_singleton("org/cocos2dx/lib/Cocos2dxRenderer");
  g_helper = jni_singleton("org/cocos2dx/lib/Cocos2dxHelper");
  dcr_watchdog_start();

  /* ---- System.loadLibrary's JNI_OnLoad ---- */
  fn_onload onload = (fn_onload)as4_native("JNI_OnLoad");
  if (onload)
    debugPrintf("[boot] JNI_OnLoad -> 0x%lx\n", (unsigned long)onload(g_jni_vm, NULL));

  /* ---- Cocos2dxHelper.init: where things are ---- */
  set_path(N.set_apk, DCR_ANDROID_APK);
  set_path(N.set_writable, A_FILES);
  set_path(N.set_cache, A_CACHE);
  set_path(N.set_ext, A_EXT);

  /* ---- the GL thread: onSurfaceCreated, onSurfaceChanged ---- */
  if (as4_egl_init() != 0)
    fatal_error("Could not set up the graphics (EGL): see debug.log.");
  as4_input_init();

  debugPrintf("[boot] nativeInit(%d, %d)\n", g_w, g_h);
  N.init(g_jni_env, g_renderer, g_w, g_h);
  jni_exception_report("nativeInit");
  do_resume();
  debugPrintf("[boot] activity up; this thread draws the frames now\n");
  log_flush_ring();

  /* ---- the frames ---- */
  u64 last_report = armGetSystemTick();
  int launch_done = 0;
  unsigned long quiet_at = 0;
  while (!rt_exit_requested() && appletMainLoop()) {
    rt_applet_poll(); /* focus (port_focus_lost/gained); the watchdog starts at the first */
    if (!rt_focused()) {
      svcSleepThread(50000000ll);
      continue;
    }
    as4_input_update();
    N.render(g_jni_env, g_renderer); /* onDrawFrame */
    as4_egl_swap();
    housekeeping(&launch_done, &quiet_at, &last_report);
  }

  /* ---- the way out: HOME > Close is onPause (the game saves), then the process ends ---- */
  log_set_quiet(0);
  rt_applet_stop();
  exit_guard_start();
  debugPrintf("[boot] leaving: onPause, then the process ends\n");
  do_pause();
  debugPrintf("[boot] the game has closed\n");
  log_flush_ring();
  return 0;
}
