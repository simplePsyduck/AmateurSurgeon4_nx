/* as4_apk.c -- files the Java side would open: entries of the player's own
 * APK (AssetManager), and files on the "phone" (absolute Android paths, such
 * as the content the game downloads into its files folder).
 *
 * The engine's own reads of the APK go through the runtime (bionic_io.c and
 * its RAM cache). This is for the parts of the game that Java did on a phone:
 * sounds (Cocos2dxMusic / Cocos2dxSound) and fonts (Typeface.createFromAsset).
 * Nothing of the game is in this program: everything is read at run time from
 * the APK the player supplies. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz/miniz.h>

#include "as4.h"
#include "dcr_path.h"
#include "util.h"

static Mutex g_zip_lock;
static mz_zip_archive g_zip;
static int g_zip_state; /* 0 not tried, 1 open, -1 failed */

static int zip_open_locked(void) {
  if (g_zip_state == 0) {
    const char *apk = dcr_apk_path();
    memset(&g_zip, 0, sizeof g_zip);
    if (apk && *apk && mz_zip_reader_init_file(&g_zip, apk, 0)) {
      g_zip_state = 1;
      debugPrintf("[apk] Java-side reader: %s (%u entries)\n", apk,
                  (unsigned)mz_zip_reader_get_num_files(&g_zip));
    } else {
      g_zip_state = -1;
      debugPrintf("[apk] Java-side reader: cannot open the APK (%s)\n", apk ? apk : "?");
    }
  }
  return g_zip_state == 1;
}

void *as4_apk_read(const char *entry, size_t *size) {
  if (size)
    *size = 0;
  if (!entry || !*entry)
    return NULL;
  void *p = NULL;
  size_t n = 0;
  mutexLock(&g_zip_lock);
  if (zip_open_locked()) {
    int i = mz_zip_reader_locate_file(&g_zip, entry, NULL, 0);
    if (i >= 0)
      p = mz_zip_reader_extract_to_heap(&g_zip, (mz_uint)i, &n, 0);
  }
  mutexUnlock(&g_zip_lock);
  if (p && size)
    *size = n;
  return p;
}

int as4_apk_list(int (*cb)(const char *entry, void *ud), void *ud) {
  int n = 0;
  mutexLock(&g_zip_lock);
  if (zip_open_locked()) {
    mz_uint count = mz_zip_reader_get_num_files(&g_zip);
    char name[512];
    for (mz_uint i = 0; i < count; i++) {
      if (!mz_zip_reader_get_filename(&g_zip, i, name, sizeof name))
        continue;
      n++;
      if (cb(name, ud))
        break;
    }
  }
  mutexUnlock(&g_zip_lock);
  return n;
}

static void *read_real_file(const char *path, size_t *size) {
  char buf[DCR_PATH_MAX];
  const char *real = dcr_translate_path(path, buf, sizeof buf);
  FILE *f = fopen(real, "rb");
  if (!f)
    return NULL;
  void *p = NULL;
  long n = -1;
  if (fseek(f, 0, SEEK_END) == 0)
    n = ftell(f);
  if (n > 0 && fseek(f, 0, SEEK_SET) == 0 && (p = malloc((size_t)n))) {
    if (fread(p, 1, (size_t)n, f) != (size_t)n) {
      free(p);
      p = NULL;
    } else if (size) {
      *size = (size_t)n;
    }
  }
  fclose(f);
  return p;
}

/* A path as the engine hands it to Java: absolute ("/data/data/<pkg>/files/
 * x.mp3": a file on the phone), or relative to the APK's assets/ (cocos2d-x
 * strips that prefix before calling Java), or with it. */
void *as4_file_read(const char *path, size_t *size) {
  if (size)
    *size = 0;
  if (!path || !*path)
    return NULL;
  if (path[0] == '/')
    return read_real_file(path, size);
  char entry[512];
  while (path[0] == '.' && path[1] == '/')
    path += 2;
  if (strncmp(path, "assets/", 7)) {
    snprintf(entry, sizeof entry, "assets/%s", path);
    void *p = as4_apk_read(entry, size);
    if (p)
      return p;
  }
  return as4_apk_read(path, size);
}
