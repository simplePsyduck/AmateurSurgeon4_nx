/* as4_input.c -- Switch controllers and the touchscreen as the game's input.
 *
 * The game is played by touch (drag tools, tap buttons). The touchscreen is
 * passed straight through as Cocos2dxRenderer.nativeTouches*. A controller
 * moves a pointer with the left stick (a small square is drawn before each
 * present, over the game's picture) and A touches with it, held to drag; B is
 * Android's BACK key. MIT.
 */
#include <math.h>
#include <string.h>
#include <switch.h>

#include "as4.h"
#include "dcr_config.h"
#include "gl_layer.h"
#include "rt_pad.h"
#include "util.h"

static PadState g_pad;
static float g_px, g_py;     /* the pointer, in surface pixels */
static int g_pad_active;     /* a controller moved the pointer: draw it */
static int g_ptr_down;       /* A is holding the pointer's touch (id 9) */
static u64 g_prev;
static int g_fingers[16];    /* touch ids down from the screen */
static float g_lx[16], g_ly[16];

#define ID_PAD 9

static void draw_pointer(void) {
  if (!g_pad_active)
    return;
  /* A filled square by scissor + clear: no shader, no buffers. The game's
   * scissor box, test flag, clear colour and colour mask are put back. */
  typedef void (*fn_en)(unsigned);
  typedef unsigned char (*fn_is)(unsigned);
  typedef void (*fn_geti)(unsigned, int *);
  typedef void (*fn_getf)(unsigned, float *);
  typedef void (*fn_getb)(unsigned, unsigned char *);
  typedef void (*fn_sc)(int, int, int, int);
  typedef void (*fn_cc)(float, float, float, float);
  typedef void (*fn_cm)(unsigned char, unsigned char, unsigned char, unsigned char);
  typedef void (*fn_cl)(unsigned);
  static fn_en en, dis;
  static fn_is is;
  static fn_geti geti;
  static fn_getf getf;
  static fn_getb getb;
  static fn_sc sc;
  static fn_cc cc;
  static fn_cm cm;
  static fn_cl cl;
  if (!cl) {
    en = (fn_en)dcr_gl_lookup("glEnable");
    dis = (fn_en)dcr_gl_lookup("glDisable");
    is = (fn_is)dcr_gl_lookup("glIsEnabled");
    geti = (fn_geti)dcr_gl_lookup("glGetIntegerv");
    getf = (fn_getf)dcr_gl_lookup("glGetFloatv");
    getb = (fn_getb)dcr_gl_lookup("glGetBooleanv");
    sc = (fn_sc)dcr_gl_lookup("glScissor");
    cc = (fn_cc)dcr_gl_lookup("glClearColor");
    cm = (fn_cm)dcr_gl_lookup("glColorMask");
    cl = (fn_cl)dcr_gl_lookup("glClear");
  }
  if (!(en && dis && is && geti && getf && getb && sc && cc && cm && cl))
    return;
  unsigned char was = is(0x0C11); /* GL_SCISSOR_TEST */
  int box[4];
  float col[4];
  unsigned char mask[4];
  geti(0x0C10, box);  /* GL_SCISSOR_BOX */
  getf(0x0C22, col);  /* GL_COLOR_CLEAR_VALUE */
  getb(0x0C23, mask); /* GL_COLOR_WRITEMASK */
  int size = g_ptr_down ? 12 : 18;
  int x = (int)g_px - size / 2, y = as4_surface_h() - (int)g_py - size / 2; /* GL's y is up */
  en(0x0C11);
  cm(1, 1, 1, 1);
  sc(x - 2, y - 2, size + 4, size + 4);
  cc(0.0f, 0.0f, 0.0f, 1.0f);
  cl(0x4000);
  sc(x, y, size, size);
  cc(1.0f, 1.0f, 1.0f, 1.0f);
  cl(0x4000);
  sc(box[0], box[1], box[2], box[3]);
  cc(col[0], col[1], col[2], col[3]);
  cm(mask[0], mask[1], mask[2], mask[3]);
  if (!was)
    dis(0x0C11);
}

void as4_input_init(void) {
  rt_pad_setup(RT_PAD_MAX_PLAYERS, 1);
  padInitializeDefault(&g_pad);
  hidInitializeTouchScreen();
  g_px = as4_surface_w() / 2.0f;
  g_py = as4_surface_h() / 2.0f;
  dcr_present_hook = draw_pointer;
}

static void touchscreen(void) {
  if (!dcr_config()->touch)
    return;
  HidTouchScreenState ts = {0};
  if (hidGetTouchScreenStates(&ts, 1) == 0)
    ts.count = 0;
  const float sx = (float)as4_surface_w() / 1280.0f, sy = (float)as4_surface_h() / 720.0f;
  int now[16] = {0};
  for (int i = 0; i < ts.count && i < 16; i++) {
    int id = (int)(ts.touches[i].finger_id % 8); /* 0-7: the pointer has 9 */
    float x = ts.touches[i].x * sx, y = ts.touches[i].y * sy;
    now[id] = 1;
    g_pad_active = 0; /* a finger: the pointer is hidden */
    if (!g_fingers[id])
      as4_touch_begin(id, x, y);
    else if (x != g_lx[id] || y != g_ly[id])
      as4_touch_move(id, x, y);
    g_lx[id] = x, g_ly[id] = y;
    g_fingers[id] = 1;
  }
  for (int id = 0; id < 8; id++)
    if (g_fingers[id] && !now[id]) {
      as4_touch_end(id, g_lx[id], g_ly[id]);
      g_fingers[id] = 0;
    }
}

void as4_input_update(void) {
  padUpdate(&g_pad);
  float st[4] = {0, 0, 0, 0};
  u64 b = rt_pad_read(&g_pad, st);
  u64 down = b & ~g_prev, up = g_prev & ~b;
  g_prev = b;

  u64 touch_btn = dcr_config()->swap_ab ? HidNpadButton_B : HidNpadButton_A;
  u64 back_btn = dcr_config()->swap_ab ? HidNpadButton_A : HidNpadButton_B;

  /* the pointer: the left stick (and the D-pad, slowly) */
  float dx = st[0], dy = -st[1]; /* the stick's y is up, the screen's down */
  if (b & HidNpadButton_Left) dx -= 0.4f;
  if (b & HidNpadButton_Right) dx += 0.4f;
  if (b & HidNpadButton_Up) dy -= 0.4f;
  if (b & HidNpadButton_Down) dy += 0.4f;
  float mag = sqrtf(dx * dx + dy * dy);
  if (mag > 0.15f) {
    float speed = dcr_config()->pointer_speed * ((b & (HidNpadButton_ZL | HidNpadButton_ZR)) ? 0.35f : 1.0f);
    g_px += dx * speed;
    g_py += dy * speed;
    if (g_px < 0) g_px = 0;
    if (g_py < 0) g_py = 0;
    if (g_px > as4_surface_w() - 1) g_px = (float)as4_surface_w() - 1;
    if (g_py > as4_surface_h() - 1) g_py = (float)as4_surface_h() - 1;
    g_pad_active = 1;
  }
  if (down & touch_btn) {
    g_pad_active = 1;
    g_ptr_down = 1;
    as4_touch_begin(ID_PAD, g_px, g_py);
  } else if (g_ptr_down && (up & touch_btn)) {
    g_ptr_down = 0;
    as4_touch_end(ID_PAD, g_px, g_py);
  } else if (g_ptr_down && mag > 0.15f) {
    as4_touch_move(ID_PAD, g_px, g_py);
  }
  if (down & back_btn)
    as4_key_down(4); /* KEYCODE_BACK */
  if (down & HidNpadButton_Plus)
    as4_key_down(82); /* KEYCODE_MENU */

  touchscreen();
}
