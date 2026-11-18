/* port_config.h -- Amateur Surgeon 4's settings for the android32 runtime.
 *
 * Macros only: the runtime's C files, its assembly and the launcher all read
 * this (runtime/source/rt_settings.h). MIT.
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

/* ------------------------------------------------------------------ the game */
#define PORT_TITLE    "Amateur Surgeon 4"
#define PORT_NAME     "amatuersurgeon4_nx"
#define PORT_PACKAGE  "com.turner.amateursurgeon4"
#define PORT_BANNER   "amatuersurgeon4_nx: Amateur Surgeon 4 (cocos2d-x, armeabi-v7a)"

/* The player's own APK (any name). It is found by what is in it: the game's
 * library, and the package. No version code is required: the port was made
 * from the libraries of one build, and logs the APK it finds. */
#define PORT_APK_DESC "Amateur Surgeon 4 (com.turner.amateursurgeon4, armeabi-v7a)"
#define PORT_APK_ROLES                                                                   \
  {.what = "the game", .need = (const char *const[]){"lib/armeabi-v7a/libgame.so", NULL}, \
   .package = PORT_PACKAGE, .flags = RT_APK_PACKAGE_BONUS}
#define PORT_LAUNCHER_START_NOTE "(the first start unpacks the game's library from the APK)"

/* ------------------------------------------------------------------ audio */
/* The engine's OpenSLEngine class loads OpenSL ES itself: the runtime's
 * OpenSL over audout. */
#define RT_OPENSLES 1

/* ------------------------------------------------------------------ input */
#define RT_PAD_MAX_PLAYERS 1

#endif
