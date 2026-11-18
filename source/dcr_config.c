/* dcr_config.c -- Amateur Surgeon 4's config.ini, on the runtime's INI engine
 * (runtime/source/rt_cfg.c). MIT. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <switch.h>

#include "dcr_config.h"
#include "rt_cfg.h"
#include "util.h"

static DcrConfig g_cfg = {.touch = 1, .pointer_speed = 14.0f, .res_w = 1280, .res_h = 720, .boost = 1};

const DcrConfig *dcr_config(void) { return &g_cfg; }

static const CfgOpt k_opts[] = {
    {"game", "language", "auto",
     "auto (the console's language) or a code such as en, fr, de, es, it, pt, ja, ko, zh.",
     CFG_TEXT, NULL, NULL, 0, 0, 0, 0},
    CFG_ROW_SWAP_AB("Swap A and B (true: B touches, A goes back).", &g_cfg.swap_ab),
    {"controls", "touchscreen", "true", "The touchscreen plays the game (handheld).",
     CFG_BOOL, NULL, &g_cfg.touch, 0, 0, 0, 0},
    {"controls", "pointer_speed", "14",
     "How fast the left stick moves the pointer (pixels per frame at full tilt).",
     CFG_FLOAT, NULL, &g_cfg.pointer_speed, 4.0f, 60.0f, 0, 0},
    CFG_ROW_RESOLUTION("720", CFG_HELP_RESOLUTION),
    CFG_ROW_BOOST(CFG_HELP_BOOST, &g_cfg.boost),
    CFG_ROW_GL_SELFTEST(&g_cfg.gl_selftest),
    CFG_ROW_BOOT_LOG(CFG_HELP_BOOT_LOG, &g_cfg.boot_log),
    CFG_ROW_LOG_JNI(CFG_HELP_LOG_JNI, &g_cfg.log_jni),
};

static void apply(void) {
  const RtConfig *rt = rt_config();
  g_cfg.res_w = rt->res_w;
  g_cfg.res_h = rt->res_h;
  const char *lang = rt_config_get("game", "language");
  g_cfg.locale[0] = 0;
  if (lang && strcasecmp(lang, "auto"))
    snprintf(g_cfg.locale, sizeof g_cfg.locale, "%s", lang);
  debugPrintf("[config] language %s; %dx%d; A/B %s; touch %s; pointer %.0f; CPU boost %s\n",
              g_cfg.locale[0] ? g_cfg.locale : "auto", g_cfg.res_w, g_cfg.res_h,
              g_cfg.swap_ab ? "swapped" : "normal", g_cfg.touch ? "on" : "off",
              (double)g_cfg.pointer_speed, g_cfg.boost ? "on" : "off");
}

static const CfgTable k_table = {
    .opts = k_opts,
    .nopts = CFG_COUNT(k_opts),
    .version = 1,
    .apply = apply,
};

void dcr_config_load(void) { rt_config_load(&k_table); }
