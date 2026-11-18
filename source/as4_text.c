/* as4_text.c -- cocos2d-x's TTF labels: what org.cocos2dx.lib.Cocos2dxBitmap
 * does on a phone, in C.
 *
 * CCLabelTTF -> CCImage::initWithString -> BitmapDC::getBitmapFromJava calls
 * the static Java method Cocos2dxBitmap.createTextBitmap(text, fontName,
 * fontSize, alignment, width, height). Java draws the text with Android's
 * Canvas and hands the pixels back through the native method
 * Cocos2dxBitmap.nativeInitBitmapDC(width, height, byte[] pixels). Without
 * that call the engine logs "_updateTexture: oops" and the label stays empty
 * (the third hardware run: every label of the game).
 *
 * Here the text is laid out the way Cocos2dxBitmap.java does it (its
 * splitString / computeTextProperty / computeX / computeY), drawn with
 * stb_truetype (third_party/, public domain) in the fonts of the player's own
 * APK (assets/ *.ttf files: Typeface.createFromAsset on a phone), and handed to
 * nativeInitBitmapDC. Like Android's Paint, the text is white on transparent
 * (premultiplied); the engine tints it. MIT.
 */
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <switch.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "third_party/stb_truetype.h"

#include "as4.h"
#include "jni.h"
#include "util.h"

/* ------------------------------------------------------------ fonts */
#define MAX_FONTS 16

typedef struct {
  char entry[160];   /* APK entry: assets/Verdana-Bold.ttf */
  char base[64];     /* normalized names to match: "verdanabold" */
  char family[64], full[64], ps[64];
  unsigned char *data;
  stbtt_fontinfo info;
  int ok;
} Font;

static Font g_fonts[MAX_FONTS];
static int g_nfonts = -1; /* -1: the APK not listed yet */
static int g_default = -1;

/* lowercase letters and digits only: "Verdana-Bold.ttf" -> "verdanabold" */
static void norm(const char *in, char *out, size_t cap) {
  const char *s = strrchr(in, '/');
  s = s ? s + 1 : in;
  size_t n = 0;
  const char *dot = strrchr(s, '.');
  if (dot && (!strcasecmp(dot, ".ttf") || !strcasecmp(dot, ".otf") || !strcasecmp(dot, ".ttc")))
    ;
  else
    dot = NULL;
  for (; *s && s != dot && n + 1 < cap; s++)
    if (isalnum((unsigned char)*s))
      out[n++] = (char)tolower((unsigned char)*s);
  out[n] = 0;
}

static void font_name(const stbtt_fontinfo *fi, int id, char *out, size_t cap) {
  out[0] = 0;
  int len = 0;
  /* Microsoft, Unicode BMP, US English: UTF-16BE */
  const char *s = stbtt_GetFontNameString(fi, &len, STBTT_PLATFORM_ID_MICROSOFT,
                                          STBTT_MS_EID_UNICODE_BMP, STBTT_MS_LANG_ENGLISH, id);
  char tmp[128];
  size_t n = 0;
  if (s) {
    for (int i = 1; i < len && n + 1 < sizeof tmp; i += 2)
      tmp[n++] = s[i];
  } else {
    s = stbtt_GetFontNameString(fi, &len, STBTT_PLATFORM_ID_MAC, STBTT_MAC_EID_ROMAN,
                                STBTT_MAC_LANG_ENGLISH, id);
    for (int i = 0; s && i < len && n + 1 < sizeof tmp; i++)
      tmp[n++] = s[i];
  }
  tmp[n] = 0;
  norm(tmp, out, cap);
}

static int list_cb(const char *entry, void *ud) {
  (void)ud;
  size_t n = strlen(entry);
  if (n < 5 || strncmp(entry, "assets/", 7) ||
      (strcasecmp(entry + n - 4, ".ttf") && strcasecmp(entry + n - 4, ".otf")))
    return 0;
  if (g_nfonts >= MAX_FONTS)
    return 1;
  Font *f = &g_fonts[g_nfonts++];
  snprintf(f->entry, sizeof f->entry, "%s", entry);
  norm(entry, f->base, sizeof f->base);
  return 0;
}

static void fonts_init(void) {
  if (g_nfonts >= 0)
    return;
  g_nfonts = 0;
  as4_apk_list(list_cb, NULL);
  for (int i = 0; i < g_nfonts; i++) {
    Font *f = &g_fonts[i];
    size_t size = 0;
    f->data = as4_apk_read(f->entry, &size);
    if (f->data && stbtt_InitFont(&f->info, f->data, stbtt_GetFontOffsetForIndex(f->data, 0))) {
      f->ok = 1;
      font_name(&f->info, 1, f->family, sizeof f->family);
      font_name(&f->info, 4, f->full, sizeof f->full);
      font_name(&f->info, 6, f->ps, sizeof f->ps);
    }
    debugPrintf("[text] font %s%s (family '%s', full '%s')\n", f->entry, f->ok ? "" : ": unreadable",
                f->family, f->full);
  }
  /* the default face: Verdana Bold when the APK has it (the game's body text) */
  for (int i = 0; i < g_nfonts; i++)
    if (g_fonts[i].ok && strstr(g_fonts[i].base, "verdana")) {
      g_default = i;
      if (strstr(g_fonts[i].base, "bold"))
        break;
    }
  for (int i = 0; g_default < 0 && i < g_nfonts; i++)
    if (g_fonts[i].ok)
      g_default = i;
  if (g_default < 0)
    debugPrintf("[text] no TrueType font in the APK: labels stay blank\n");
}

static size_t common_prefix(const char *a, const char *b) {
  size_t n = 0;
  while (a[n] && a[n] == b[n])
    n++;
  return n;
}

/* fontName as the engine names it: a file ("Verdana-Bold.ttf",
 * "fonts/x.ttf"), a family ("Verdana Bold", "Londrina Solid"), or a face
 * the phone would have had ("Marker Felt", "trebuc.ttf"): the closest font
 * of the APK, else the default. */
static Font *font_for(const char *name) {
  fonts_init();
  if (g_default < 0)
    return NULL;
  if (!name || !*name)
    return &g_fonts[g_default];
  static struct { char name[96]; int font; } memo[48];
  static int nmemo;
  for (int i = 0; i < nmemo; i++)
    if (!strcmp(memo[i].name, name))
      return &g_fonts[memo[i].font];
  char q[96];
  norm(name, q, sizeof q);
  int best = -1;
  size_t best_score = 0;
  for (int i = 0; i < g_nfonts && q[0]; i++) {
    Font *f = &g_fonts[i];
    if (!f->ok)
      continue;
    const char *names[4] = {f->base, f->family, f->full, f->ps};
    for (int k = 0; k < 4; k++) {
      if (!names[k][0])
        continue;
      size_t score;
      if (!strcmp(names[k], q))
        score = 1000;
      else if (strstr(names[k], q) == names[k] || strstr(q, names[k]) == q)
        score = 500 + common_prefix(names[k], q);
      else
        score = common_prefix(names[k], q);
      if (score > best_score) {
        best_score = score;
        best = i;
      }
    }
  }
  int pick = best >= 0 && best_score >= 5 ? best : g_default;
  if (nmemo < (int)(sizeof memo / sizeof memo[0])) {
    snprintf(memo[nmemo].name, sizeof memo[nmemo].name, "%s", name);
    memo[nmemo++].font = pick;
  }
  debugPrintf("[text] font '%s' -> %s\n", name, g_fonts[pick].entry);
  return &g_fonts[pick];
}

/* ------------------------------------------------------------ layout */
static uint32_t utf8_next(const char **ps) {
  const unsigned char *s = (const unsigned char *)*ps;
  uint32_t c = *s++;
  if (c >= 0xF0 && (s[0] & 0xC0) == 0x80 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
    c = ((c & 7) << 18) | ((s[0] & 0x3F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    s += 3;
  } else if (c >= 0xE0 && (s[0] & 0xC0) == 0x80 && (s[1] & 0xC0) == 0x80) {
    c = ((c & 0xF) << 12) | ((s[0] & 0x3F) << 6) | (s[1] & 0x3F);
    s += 2;
  } else if (c >= 0xC0 && (s[0] & 0xC0) == 0x80) {
    c = ((c & 0x1F) << 6) | (s[0] & 0x3F);
    s += 1;
  } else if (c >= 0x80) {
    c = 0xFFFD;
  }
  *ps = (const char *)s;
  return c;
}

typedef struct {
  const stbtt_fontinfo *fi;
  float scale;
} Face;

/* Paint.measureText: the advances of [s, e) in pixels */
static float measure(const Face *f, const char *s, const char *e) {
  float w = 0;
  while (s < e && *s) {
    uint32_t c = utf8_next(&s);
    int adv, lsb;
    stbtt_GetCodepointHMetrics(f->fi, (int)c, &adv, &lsb);
    w += adv * f->scale;
  }
  return w;
}

#define MAX_LINES 256
typedef struct {
  const char *s, *e;
} Line;

/* Cocos2dxBitmap.divideStringWithMaxWidth: wrap one line at maxWidth,
 * breaking after the last space when there is one. */
static int divide(const Face *f, const char *s, const char *e, int max_w, Line *out, int n,
                  int cap) {
  const char *start = s, *p = s;
  while (p < e && n < cap) {
    const char *q = p;
    utf8_next(&q);
    if (q > e)
      q = e;
    if (ceilf(measure(f, start, q)) > (float)max_w && p > start) {
      const char *sp = NULL;
      for (const char *k = p - 1; k > start; k--)
        if (*k == ' ') {
          sp = k;
          break;
        }
      if (sp) {
        out[n].s = start, out[n].e = sp, n++;
        start = sp + 1;
      } else {
        out[n].s = start, out[n].e = p, n++;
        start = p;
      }
      while (start < e && *start == ' ')
        start++;
      p = start;
      continue;
    }
    p = q;
  }
  if (start < e && n < cap)
    out[n].s = start, out[n].e = e, n++;
  return n;
}

/* ------------------------------------------------------------ the bitmap */
typedef void (*fn_init_dc)(void *env, void *thiz, jint w, jint h, void *pixels);

int as4_text_create(const char *text, const char *font_name, int font_size, int alignment,
                    int width, int height) {
  static fn_init_dc init_dc;
  static int looked;
  if (!looked) {
    looked = 1;
    init_dc = (fn_init_dc)as4_native("Java_org_cocos2dx_lib_Cocos2dxBitmap_nativeInitBitmapDC");
    if (!init_dc)
      debugPrintf("[text] the engine has no Cocos2dxBitmap.nativeInitBitmapDC: no labels\n");
  }
  Font *font = font_for(font_name);
  if (!init_dc || !font || !text)
    return -1;
  if (font_size <= 0)
    font_size = 12;
  if (width < 0)
    width = 0;
  if (height < 0)
    height = 0;
  Face face = {&font->info, stbtt_ScaleForMappingEmToPixels(&font->info, (float)font_size)};

  /* Paint.getFontMetricsInt: top/bottom from the font's bounding box */
  int bx0, by0, bx1, by1;
  stbtt_GetFontBoundingBox(&font->info, &bx0, &by0, &bx1, &by1);
  int top = (int)ceilf(by1 * face.scale);     /* -fm.top */
  int bottom = (int)ceilf(-by0 * face.scale); /* fm.bottom */
  int line_h = top + bottom;
  if (line_h <= 0)
    line_h = font_size;

  /* splitString */
  static Line lines[MAX_LINES];
  int n = 0;
  int max_lines = height > 0 ? height / line_h : 0;
  const char *s = text, *end = text + strlen(text);
  while (s <= end && n < MAX_LINES) {
    const char *nl = memchr(s, '\n', (size_t)(end - s));
    const char *le = nl ? nl : end;
    const char *le2 = le;
    if (le2 > s && le2[-1] == '\r')
      le2--;
    if (width > 0 && ceilf(measure(&face, s, le2)) > (float)width)
      n = divide(&face, s, le2, width, lines, n, MAX_LINES);
    else
      lines[n].s = s, lines[n].e = le2, n++;
    if (max_lines > 0 && n >= max_lines)
      break;
    if (!nl)
      break;
    s = nl + 1;
  }
  if (max_lines > 0 && n > max_lines)
    n = max_lines;
  if (n == 0)
    lines[n].s = lines[n].e = text, n = 1;

  /* computeTextProperty */
  int content_w = 0;
  for (int i = 0; i < n; i++) {
    int w = (int)ceilf(measure(&face, lines[i].s, lines[i].e));
    if (w > content_w)
      content_w = w;
  }
  int total_h = line_h * n;
  int bw = width > 0 ? width : content_w;
  int bh = height > 0 ? height : total_h;
  if (bw <= 0)
    bw = 1;
  if (bh <= 0)
    bh = 1;
  if (bw > 4096 || bh > 4096) {
    debugPrintf("[text] %dx%d label clipped to 4096\n", bw, bh);
    bw = bw > 4096 ? 4096 : bw;
    bh = bh > 4096 ? 4096 : bh;
  }

  /* computeY: the first baseline */
  int h_align = alignment & 0x0F, v_align = (alignment >> 4) & 0x0F;
  int y = top;
  if (height > total_h) {
    if (v_align == 3) /* center */
      y += (height - total_h) / 2;
    else if (v_align == 2) /* bottom */
      y += height - total_h;
  }

  uint8_t *cov = calloc((size_t)bw * bh, 1);
  if (!cov)
    return -1;
  static uint8_t glyph[256 * 256];
  for (int i = 0; i < n; i++, y += line_h) {
    float lw = measure(&face, lines[i].s, lines[i].e);
    float x = 0;
    if (h_align == 3) /* center */
      x = (bw - lw) / 2.0f;
    else if (h_align == 2) /* right */
      x = bw - lw;
    const char *p = lines[i].s;
    while (p < lines[i].e && *p) {
      uint32_t c = utf8_next(&p);
      int g = stbtt_FindGlyphIndex(&font->info, (int)c);
      int adv, lsb;
      stbtt_GetGlyphHMetrics(&font->info, g, &adv, &lsb);
      float fx = floorf(x), sub = x - fx;
      int x0, y0, x1, y1;
      stbtt_GetGlyphBitmapBoxSubpixel(&font->info, g, face.scale, face.scale, sub, 0, &x0, &y0, &x1,
                                      &y1);
      int gw = x1 - x0, gh = y1 - y0;
      if (gw > 0 && gh > 0 && gw <= 256 && gh <= 256) {
        stbtt_MakeGlyphBitmapSubpixel(&font->info, glyph, gw, gh, gw, face.scale, face.scale, sub, 0,
                                      g);
        int ox = (int)fx + x0, oy = y + y0;
        for (int r = 0; r < gh; r++) {
          int yy = oy + r;
          if (yy < 0 || yy >= bh)
            continue;
          for (int k = 0; k < gw; k++) {
            int xx = ox + k;
            if (xx < 0 || xx >= bw)
              continue;
            uint8_t v = glyph[r * gw + k];
            uint8_t *d = &cov[(size_t)yy * bw + xx];
            if (v > *d)
              *d = v;
          }
        }
      }
      x += adv * face.scale;
    }
  }

  /* RGBA, white premultiplied by coverage (Android's ARGB_8888 bitmap of
   * white text: every channel the same, so the byte order does not matter) */
  JObj *arr = jni_array('B', bw * bh * 4);
  if (!arr) {
    free(cov);
    return -1;
  }
  uint8_t *px = arr->a.data;
  for (size_t i = 0; i < (size_t)bw * bh; i++)
    memset(px + i * 4, cov[i], 4);
  free(cov);
  JObj *cls = jni_class("org/cocos2dx/lib/Cocos2dxBitmap")->obj;
  init_dc(g_jni_env, cls, bw, bh, arr);
  jni_release(arr);
  static int logged;
  if (logged++ < 12)
    debugPrintf("[text] '%.40s' %s %dpx -> %dx%d\n", text, font->entry, font_size, bw, bh);
  return 0;
}

/* Cocos2dxBitmap.getFontSizeAccordingHeight: the text size whose "SghMNy"
 * is as tall as height (the Java loops textSize up until it is). */
int as4_text_font_size_for_height(int height) {
  Font *font = font_for(NULL);
  if (!font || height <= 0)
    return height > 0 ? height : 0;
  float unit = stbtt_ScaleForMappingEmToPixels(&font->info, 1.0f); /* px per unit at 1 px */
  int top = 0, bot = 0, first = 1;
  for (const char *p = "SghMNy"; *p; p++) {
    int x0, y0, x1, y1;
    if (!stbtt_GetCodepointBox(&font->info, *p, &x0, &y0, &x1, &y1))
      continue;
    if (first || y1 > top)
      top = y1;
    if (first || y0 < bot)
      bot = y0;
    first = 0;
  }
  float h1 = (top - bot) * unit; /* height at text size 1 */
  if (h1 <= 0)
    return height;
  int size = (int)ceilf(height / h1);
  return size > 0 ? size : 1;
}

/* Cocos2dxBitmap.getStringWithEllipsis(text, width, fontSize): the text cut
 * to fit width, ending in "...". The result is malloc'd. */
char *as4_text_ellipsis(const char *text, float width, float font_size) {
  if (!text)
    return NULL;
  Font *font = font_for(NULL);
  if (!font || width <= 0 || font_size <= 0)
    return strdup(text);
  Face face = {&font->info, stbtt_ScaleForMappingEmToPixels(&font->info, font_size)};
  const char *end = text + strlen(text);
  if (measure(&face, text, end) <= width)
    return strdup(text);
  static const char k_dots[] = "...";
  float dots = measure(&face, k_dots, k_dots + 3);
  const char *p = text, *fit = text;
  while (p < end) {
    const char *q = p;
    utf8_next(&q);
    if (measure(&face, text, q) + dots > width)
      break;
    fit = p = q;
  }
  size_t n = (size_t)(fit - text);
  char *out = malloc(n + 4);
  if (!out)
    return strdup(text);
  memcpy(out, text, n);
  memcpy(out + n, "...", 4);
  return out;
}
