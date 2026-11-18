/* as4_main.c -- Amateur Surgeon 4's part of the boot (the runtime's main.c runs
 * the rest: runtime/source/main.c).
 *
 * port_load() goes from the APK to the game's first code: the library and
 * classes.txt unpacked when needed, the library loaded and bound.
 * port_run() runs the engine's constructors, then JNI_OnLoad and the
 * activity (as4_boot.c). The game's assets are read from the APK by the
 * engine itself: nothing of the game is in this program. MIT.
 */
#include <stdio.h>

#include "as4.h"
#include "config.h"
#include "dcr_path.h"
#include "dcr_setup.h"
#include "error.h"
#include "rt_boot.h"

const char *port_apk_help(void) {
  return "Copy the APK of your own Amateur Surgeon 4 (com.turner.amateursurgeon4,\n"
         "armeabi-v7a) into that folder, under any name ending in .apk:\n"
         "the game reads its data from it, and its library is unpacked from it\n"
         "on the first launch.";
}

int port_load(const char *apk) {
  /* libgame.so and classes.txt, from the APK when they are missing or it has
   * changed (as4_setup_plan.c) */
  dcr_setup_from_apk(apk);
  if (as4_load_module() != 0)
    fatal_error("Could not load the game library from %s.\n\n"
                "It is unpacked from the APK (lib/armeabi-v7a/) on launch: delete\n"
                AS4_LIB_GAME " and .setup there to unpack it again.",
                dcr_game_root());
  return 0;
}

/* System.loadLibrary: the engine's constructors, then (as4_boot.c)
 * JNI_OnLoad and the activity. */
void port_run(void) {
  as4_run_constructors();
  as4_boot_run();
}
