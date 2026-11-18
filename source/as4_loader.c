/* as4_loader.c -- loading Amateur Surgeon 4's engine.
 *
 * The APK carries three armeabi-v7a libraries; only one is the game:
 *   libgame.so            cocos2d-x + the game (OpenSSL, libpng... statically
 *                         linked); imports bionic libc/m, GLES2, a few
 *                         AAssetManager/ASensor calls and liblog
 *   libcomScore.so, libImmEndpointWarpJ.so   analytics SDKs: not loaded
 * Java loads it with System.loadLibrary: its constructors run then, JNI_OnLoad
 * right after (as4_boot.c). Nothing here writes code at run time, so the module
 * is mapped and sealed in one go. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "as4.h"
#include "codespace.h"
#include "config.h"
#include "dcr_net.h"
#include "dcr_path.h"
#include "error.h"
#include "imports.h"
#include "so_util.h"
#include "util.h"

so_module g_mod_game;

void *as4_native(const char *symbol) { return (void *)so_try_find_addr_rx(&g_mod_game, symbol); }

int as4_load_module(void) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s", dcr_game_root(), AS4_LIB_GAME);
  int rc = so_load(&g_mod_game, path, NULL, PORT_SO_REGION_BYTES);
  if (rc < 0) {
    const char *why = rc == -1 ? "cannot open it, or it is not a 32-bit ARM ELF"
                    : rc == -2 ? "out of memory"
                    : rc == -3 ? "larger than PORT_SO_REGION_BYTES"
                    : rc == -4 ? "too many program headers" : "?";
    debugPrintf("[boot] so_load(%s) failed rc=%d: %s\n", path, rc, why);
    return -1;
  }
  so_relocate(&g_mod_game);
  int missing = so_resolve(&g_mod_game, dcr_imports, dcr_imports_count, 1);
  debugPrintf("[boot] %s %u KB staged %p -> %p (%d unresolved imports)\n", g_mod_game.base_name,
              (unsigned)(g_mod_game.load_size >> 10), g_mod_game.load_base, g_mod_game.load_virtbase,
              missing);
  so_finalize(&g_mod_game);
  so_flush_caches(&g_mod_game);
  return 0;
}

/* System.loadLibrary runs the library's constructors. */
void as4_run_constructors(void) {
  u64 t0 = armGetSystemTick();
  so_execute_init_array(&g_mod_game);
  debugPrintf("[boot] %s constructors done in %llu ms\n", g_mod_game.base_name,
              (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ull));
}
