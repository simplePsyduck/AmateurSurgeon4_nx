/* as4_java.c -- the Java side of Amateur Surgeon 4, answered in C.
 *
 * The game's Java (com.android.game.GameActivity, the managers for Facebook,
 * Google Play, billing, ads and analytics, org.cocos2dx.lib.*) does not run
 * here. The engine calls back into it through JNI (a JNIHelper that finds
 * each class by name and calls its static methods), and every answer comes
 * from the tables below, matched by class and method name (a NULL signature:
 * any) and read by the signature the engine asked for (jni_core.c decodes
 * them).
 *
 * What each does is what the Java does on a device with no network, no store,
 * no social login, no ads, no notifications: neutral answers (false, 0, "",
 * null) for the services, real answers for the device. Anything the engine
 * asks for that has no handler is logged once ("[jni] unhandled ..."): that
 * list is the to-do list for this file. MIT.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "as4.h"
#include "config.h"
#include "dcr_config.h"
#include "dcr_manifest.h"
#include "jni.h"
#include "util.h"

#define PKG PORT_PACKAGE
#define H(fn) static jvalue fn(JObj *self, const jvalue *a, const JMethod *m)

/* What an unhandled getter of this return type answers without complaint. */
H(h_neutral) {
  (void)self; (void)a;
  switch (m->ret) {
  case 'L': {
    const char *r = strchr(m->sig, ')');
    if (r && !strcmp(r + 1, "Ljava/lang/String;"))
      return jv_l(jni_str(""));
    return jv_l(NULL);
  }
  case '[': return jv_l(NULL);
  case 'J': return jv_j(0);
  case 'F': return jv_f(0.0f);
  case 'D': return jv_d(0.0);
  default: return jv_none();
  }
}

/* ---- answers by the type the engine asked for ----
 * The engine reaches some getters through CallStatic<Object>Method even when
 * they carry a number (PlatformUtils.getFreeMB is "()Ljava/lang/String;" and
 * the engine atoll()s it). Answering a String request with jv_i(4096) hands
 * the engine 0x1000 as a jstring: GetStringUTFChars reads it and the process
 * faults (the second hardware run). So every answer goes through these. */
static jvalue ret_num(const JMethod *m, long long v) {
  switch (m->ret) {
  case 'V': return jv_none();
  case 'Z': return jv_z(v != 0);
  case 'J': return jv_j(v);
  case 'F': return jv_f((float)v);
  case 'D': return jv_d((double)v);
  case 'L': {
    const char *r = strchr(m->sig, ')');
    if (r && !strcmp(r + 1, "Ljava/lang/String;"))
      return jv_l(jni_str_fmt("%lld", v)); /* a number, as Java's String.valueOf */
    return jv_l(NULL);
  }
  case '[': return jv_l(NULL);
  default: return jv_i((jint)v); /* I, B, C, S */
  }
}

static jvalue ret_str(const JMethod *m, const char *s) {
  if (m->ret == 'L')
    return jv_l(jni_str(s ? s : ""));
  return ret_num(m, s ? atoll(s) : 0);
}

/* ---- the locale: config.ini's [game] language, else the console's ---- */
static char g_lang[4] = "en", g_country[4] = "US";

static void locale_init(void) {
  char code[16] = {0};
  const char *cfg = dcr_config()->locale;
  if (cfg && cfg[0]) {
    snprintf(code, sizeof code, "%s", cfg);
  } else if (R_SUCCEEDED(setInitialize())) {
    u64 lc = 0;
    if (R_SUCCEEDED(setGetSystemLanguage(&lc)))
      memcpy(code, &lc, sizeof lc); /* "en-US", "ja", "zh-Hans", "es-419"... */
    setExit();
  }
  if (strlen(code) >= 2 && isalpha((unsigned char)code[0]) && isalpha((unsigned char)code[1])) {
    g_lang[0] = (char)tolower((unsigned char)code[0]);
    g_lang[1] = (char)tolower((unsigned char)code[1]);
    g_lang[2] = 0;
  }
  /* the region: from the code when it has one ("en-GB", "pt_BR"), else the
   * language's usual country */
  const char *sep = strpbrk(code, "-_");
  if (sep && strlen(sep + 1) == 2 && isalpha((unsigned char)sep[1]) && isalpha((unsigned char)sep[2])) {
    g_country[0] = (char)toupper((unsigned char)sep[1]);
    g_country[1] = (char)toupper((unsigned char)sep[2]);
    g_country[2] = 0;
  } else {
    static const char *const map[][2] = {
        {"en", "US"}, {"ja", "JP"}, {"fr", "FR"}, {"de", "DE"}, {"it", "IT"}, {"es", "ES"},
        {"nl", "NL"}, {"pt", "PT"}, {"ru", "RU"}, {"ko", "KR"}, {"zh", "CN"}, {NULL, NULL}};
    snprintf(g_country, sizeof g_country, "US");
    for (int i = 0; map[i][0]; i++)
      if (!strcmp(map[i][0], g_lang))
        snprintf(g_country, sizeof g_country, "%s", map[i][1]);
    if (!strcmp(code, "zh-Hant"))
      snprintf(g_country, sizeof g_country, "TW");
  }
  debugPrintf("[java] locale %s_%s (from %s)\n", g_lang, g_country,
              cfg && cfg[0] ? "config.ini" : code[0] ? code : "the default");
}

static const char *app_version(void) {
  const char *v = dcr_manifest_loaded() ? dcr_manifest_version_name() : NULL;
  return v && v[0] ? v : "1.0";
}

/* ---- arguments, read by the signature the engine used ---- */
static char arg_type(const JMethod *m, int idx) {
  const char *p = strchr(m->sig, '(');
  if (!p)
    return 0;
  p++;
  for (int i = 0; *p && *p != ')'; i++) {
    char t = *p;
    if (t == '[') {
      while (*p == '[')
        p++;
      t = '[';
    }
    if (*p == 'L') {
      p = strchr(p, ';');
      if (!p)
        return 0;
    }
    if (i == idx)
      return t;
    p++;
  }
  return 0;
}

static double arg_num(const JMethod *m, const jvalue *a, int idx) {
  if (!a)
    return 0;
  switch (arg_type(m, idx)) {
  case 'F': return a[idx].f;
  case 'D': return a[idx].d;
  case 'J': return (double)a[idx].j;
  case 'Z': return a[idx].z;
  case 'I': case 'S': case 'B': case 'C': return a[idx].i;
  default: return 0;
  }
}

static const char *arg_str(const JMethod *m, const jvalue *a, int idx) {
  if (!a || arg_type(m, idx) != 'L')
    return "";
  return jni_utf(a[idx].l);
}

/* org.cocos2dx.lib.Cocos2dxHelper (signatures read from libgame.so):
 *   getCocos2dxPackageName / getCurrentLanguage / getCurrentCountry /
 *   getDeviceModel ()Ljava/lang/String;
 * and the sound (SimpleAudioEngineJni -> Cocos2dxMusic / Cocos2dxSound on a
 * phone), answered by as4_audio.c:
 *   playBackgroundMusic (Ljava/lang/String;Z)V   playEffect (Ljava/lang/String;ZFFF)I
 *   preloadBackgroundMusic / preloadEffect / unloadEffect (Ljava/lang/String;)V
 *   stop/pause/resume/rewindBackgroundMusic ()V   isBackgroundMusicPlaying ()Z
 *   get/setBackgroundMusicVolume, get/setEffectsVolume  ()F / (F)V
 *   stopEffect / pauseEffect / resumeEffect (I)V   isEffectPlaying (I)Z
 *   setPitchForEffect / setVolumeForEffect (IF)V   getVolumeForEffect (I)F
 *   stopAllEffects / pauseAllEffects / resumeAllEffects / end ()V */
H(h_helper) {
  const char *n = m->name;
  if (!strcmp(n, "getCocos2dxPackageName")) return ret_str(m, PKG);
  if (!strcmp(n, "getCurrentLanguage")) return ret_str(m, g_lang);
  if (!strcmp(n, "getCurrentCountry")) return ret_str(m, g_country);
  if (!strcmp(n, "getDeviceModel")) return ret_str(m, "Nintendo Switch");
  if (!strcmp(n, "terminateProcess")) {
    debugPrintf("[java] Cocos2dxHelper.terminateProcess (ignored)\n");
    return h_neutral(self, a, m);
  }

  /* ---- music ---- */
  if (!strcmp(n, "preloadBackgroundMusic")) { as4_music_preload(arg_str(m, a, 0)); return jv_none(); }
  if (!strcmp(n, "playBackgroundMusic")) {
    as4_music_play(arg_str(m, a, 0), arg_num(m, a, 1) != 0);
    return jv_none();
  }
  if (!strcmp(n, "stopBackgroundMusic")) { as4_music_stop(); return jv_none(); }
  if (!strcmp(n, "pauseBackgroundMusic")) { as4_music_pause(1); return jv_none(); }
  if (!strcmp(n, "resumeBackgroundMusic")) { as4_music_pause(0); return jv_none(); }
  if (!strcmp(n, "rewindBackgroundMusic")) { as4_music_rewind(); return jv_none(); }
  if (!strcmp(n, "isBackgroundMusicPlaying")) return ret_num(m, as4_music_playing());
  if (!strcmp(n, "getBackgroundMusicVolume")) return m->ret == 'F' ? jv_f(as4_music_get_volume()) : ret_num(m, 0);
  if (!strcmp(n, "setBackgroundMusicVolume")) { as4_music_set_volume((float)arg_num(m, a, 0)); return jv_none(); }
  if (!strcmp(n, "getBackgroundMusicOffset")) return m->ret == 'F' ? jv_f(as4_music_get_offset()) : ret_num(m, (long long)as4_music_get_offset());
  if (!strcmp(n, "setBackgroundMusicOffset")) { as4_music_set_offset((float)arg_num(m, a, 0)); return jv_none(); }

  /* ---- effects ---- */
  if (!strcmp(n, "playEffect")) {
    /* (path, loop, pitch, pan, gain) */
    int id = as4_fx_play(arg_str(m, a, 0), arg_num(m, a, 1) != 0,
                         arg_type(m, 2) ? (float)arg_num(m, a, 2) : 1.0f,
                         arg_type(m, 3) ? (float)arg_num(m, a, 3) : 0.0f,
                         arg_type(m, 4) ? (float)arg_num(m, a, 4) : 1.0f);
    return ret_num(m, id);
  }
  if (!strcmp(n, "preloadEffect")) { as4_fx_preload(arg_str(m, a, 0)); return jv_none(); }
  if (!strcmp(n, "unloadEffect")) { as4_fx_unload(arg_str(m, a, 0)); return jv_none(); }
  if (!strcmp(n, "stopEffect")) { as4_fx_stop((int)arg_num(m, a, 0)); return jv_none(); }
  if (!strcmp(n, "pauseEffect")) { as4_fx_pause((int)arg_num(m, a, 0), 1); return jv_none(); }
  if (!strcmp(n, "resumeEffect")) { as4_fx_pause((int)arg_num(m, a, 0), 0); return jv_none(); }
  if (!strcmp(n, "stopAllEffects")) { as4_fx_all(0); return jv_none(); }
  if (!strcmp(n, "pauseAllEffects")) { as4_fx_all(1); return jv_none(); }
  if (!strcmp(n, "resumeAllEffects")) { as4_fx_all(2); return jv_none(); }
  if (!strcmp(n, "isEffectPlaying")) return ret_num(m, as4_fx_playing((int)arg_num(m, a, 0)));
  if (!strcmp(n, "getEffectsVolume")) return m->ret == 'F' ? jv_f(as4_fx_get_master()) : ret_num(m, 0);
  if (!strcmp(n, "setEffectsVolume")) { as4_fx_set_master((float)arg_num(m, a, 0)); return jv_none(); }
  if (!strcmp(n, "setPitchForEffect")) { as4_fx_set_pitch((int)arg_num(m, a, 0), (float)arg_num(m, a, 1)); return jv_none(); }
  if (!strcmp(n, "setVolumeForEffect")) { as4_fx_set_volume((int)arg_num(m, a, 0), (float)arg_num(m, a, 1)); return jv_none(); }
  if (!strcmp(n, "getVolumeForEffect")) return m->ret == 'F' ? jv_f(as4_fx_get_volume((int)arg_num(m, a, 0))) : ret_num(m, 0);
  if (!strcmp(n, "end")) { as4_audio_end(); return jv_none(); }
  /* getSoundIdForFile, lock/unlock/unsetSoundSourceIndexForSoundId,
   * unlockAllSources: bookkeeping of the game's own Java: neutral */
  return h_neutral(self, a, m);
}

/* com.android.game.PlatformUtils / PermissionsManager / GameActivity:
 *   getFreeMB ()Ljava/lang/String;  getOSVersion ()I
 *   getApplicationVersion ()Ljava/lang/String;  getHasSarsaparilla ()Z
 *   hasUserGrantedPermission (I)Z   doFreeSpaceCheck / doLowMemoryCheck ()V ... */
H(h_device) {
  const char *n = m->name;
  if (!strcmp(n, "getDeviceModel")) return ret_str(m, "Nintendo Switch");
  if (!strcmp(n, "getOSVersion")) return ret_num(m, 22); /* Android 5.1, as Build.VERSION */
  if (!strcmp(n, "getFreeMB")) return ret_num(m, 4096);
  if (!strcmp(n, "hasUserGrantedPermission")) return ret_num(m, 1);
  if (!strcmp(n, "getApplicationVersion")) return ret_str(m, app_version());
  /* getHasSarsaparilla, isAuthenticated, isLoggedIn, isPurchasing, isAdReadyToShow,
   * hasBillboardForScope, isUsingCloudSave, getIsRestoring, hasMetadataForFile...:
   * false; the rest: void */
  return h_neutral(self, a, m);
}

/* org.cocos2dx.lib.Cocos2dxBitmap: TTF labels, drawn by as4_text.c.
 *   createTextBitmap (Ljava/lang/String;Ljava/lang/String;IIII)V
 *       text, font, size, alignment, width, height; answers through the
 *       engine's nativeInitBitmapDC
 *   getFontSizeAccordingHeight (I)I
 *   getStringWithEllipsis (Ljava/lang/String;FF)Ljava/lang/String; */
H(h_bitmap) {
  const char *n = m->name;
  if (!strcmp(n, "createTextBitmap")) {
    as4_text_create(arg_str(m, a, 0), arg_str(m, a, 1), (int)arg_num(m, a, 2),
                    (int)arg_num(m, a, 3), (int)arg_num(m, a, 4), (int)arg_num(m, a, 5));
    return jv_none();
  }
  if (!strcmp(n, "getFontSizeAccordingHeight"))
    return ret_num(m, as4_text_font_size_for_height((int)arg_num(m, a, 0)));
  if (!strcmp(n, "getStringWithEllipsis")) {
    char *t = as4_text_ellipsis(arg_str(m, a, 0), (float)arg_num(m, a, 1), (float)arg_num(m, a, 2));
    jvalue r = ret_str(m, t ? t : "");
    free(t);
    return r;
  }
  return h_neutral(self, a, m);
}

#define COC "org/cocos2dx/lib/"
#define G "com/android/game/"

const JMethodDef jni_method_defs[] = {
    {COC "Cocos2dxHelper", NULL, NULL, h_helper},
    {COC "Cocos2dxBitmap", NULL, NULL, h_bitmap},
    {G "PlatformUtils", NULL, NULL, h_device},
    {G "PermissionsManager", NULL, NULL, h_device},
    {G "GameActivity", NULL, NULL, h_device},
    /* the services: Facebook, Google Play, billing, ads, analytics, cloud save */
    {G "ASFacebookManager", NULL, NULL, h_neutral},
    {G "AmplitudeManager", NULL, NULL, h_neutral},
    {G "FyberManagerJNI", NULL, NULL, h_neutral},
    {G "FyberManager", NULL, NULL, h_neutral},
    {G "IabManager", NULL, NULL, h_neutral},
    {G "KGooglePlayManager", NULL, NULL, h_neutral},
    {G "KFGooglePlayManager", NULL, NULL, h_neutral},
    {G "UpsightManager", NULL, NULL, h_neutral},
    {G "EULAManager", NULL, NULL, h_neutral},
    {NULL, NULL, NULL, NULL},
};

const JFieldDef jni_field_defs[] = {
    {"android/os/Build$VERSION", "RELEASE", NULL, 0, "5.1"},
    {"android/os/Build$VERSION", "SDK_INT", NULL, 22, NULL},
    {"android/os/Build", "MODEL", NULL, 0, "Switch"},
    {"android/os/Build", "MANUFACTURER", NULL, 0, "Nintendo"},
    {"android/os/Build", "BRAND", NULL, 0, "Nintendo"},
    {"android/os/Build", "DEVICE", NULL, 0, "switch"},
    {"android/os/Build", "CPU_ABI", NULL, 0, "armeabi-v7a"},
    {NULL, NULL, NULL, 0, NULL},
};

const char *const jni_class_supers[][2] = {
    {G "GameActivity", "android/app/Activity"},
    {"android/app/Activity", "android/view/ContextThemeWrapper"},
    {"android/view/ContextThemeWrapper", "android/content/ContextWrapper"},
    {"android/content/ContextWrapper", "android/content/Context"},
    {NULL, NULL},
};

/* Classes the engine asks for that a phone with this APK does not have: none
 * known yet (classes.txt, written by the first-launch setup from the APK's
 * dex files, answers FindClass). */
const char *const jni_missing_classes[] = {NULL};

void as4_java_init(void) {
  jni_init();
  g_jni_log = dcr_config()->log_jni;
  locale_init();
  as4_audio_init();
  debugPrintf("[java] ready (package %s)\n", PKG);
}
