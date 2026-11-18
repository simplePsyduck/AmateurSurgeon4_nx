/* config.h -- Amateur Surgeon 4's own constants. The runtime's settings are in
 * port_config.h (runtime/source/rt_settings.h). MIT.
 *
 * The game: com.turner.amateursurgeon4, cocos2d-x 2.x driven from Java
 * (org.cocos2dx.lib.Cocos2dxRenderer), GLES2, one native library of its own
 * (libgame.so, highest vaddr+memsz about 0x98b000, in PORT_SO_REGION_BYTES).
 * libcomScore.so and libImmEndpointWarpJ.so are analytics SDKs: not loaded.
 */
#ifndef AS4_CONFIG_H
#define AS4_CONFIG_H

#include "rt_settings.h"

#define AS4_LIB_GAME "libgame.so"

#endif
