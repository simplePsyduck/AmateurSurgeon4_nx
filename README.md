# AmatuerSurgeon4_nx

An unofficial Nintendo Switch wrapper for the Android version of **Amateur
Surgeon 4** (`com.turner.amateursurgeon4`). It loads the game's own 32-bit ARM
`libgame.so` (cocos2d-x, GLES2) and supplies what that library expects from
Android: libc, JNI, the asset APK, EGL/GLES, OpenSL ES, input. It runs in
AArch32 mode like the game's code, on the shared **android32** runtime
(`runtime/`), the way the Ducktales-NX, abspace_nx and sonic_allstars_nx ports do.

**It ships no game code and no game data.** You supply the APK of a copy you
own. `.gitignore` excludes `*.apk`, `*.obb`, `lib*.so`, `gamefiles/` and
`assets/`

## Using it (players)

1. Install the NRO launcher (`amatuersurgeon4_nx.nro`) to `sdmc:/switch/amatuersurgeon4_nx/`.
2. Copy your own APK of version 2.7.10 of the game into that same folder (any name).
3. Create a forwarder in Sphaira then start it. The first launch unpacks `libgame.so` from the APK and lists its Java
   classes; later launches start the game directly. Logs: `debug.log`; settings: `config.ini`.

Controls: touchscreen as on a phone. Left stick (or D-pad) moves a pointer;
**A** touches (hold to drag); **ZL/ZR** slow the pointer; **B** is Back; **+** is Menu.

## Building (developers)

Needs Docker with the AArch32 toolchain image, libnx32 4.12.0+ (next to this
folder, or `DCR_LIBNX32`), Mesa for AArch32 in `./portlibs32/` (see
`runtime/README.md`), and `devkitpro/devkita64` for the launcher.

```sh
./build.sh              # amatuersurgeon4_nx.nsp  (the 32-bit program)
launcher/build.sh       # amatuersurgeon4_nx.nro  (carries the .nsp)
./build.sh DCR_GL_MESA=0   # null renderer, to check the build without Mesa
python3 runtime/tools/gen_imports.py --check   # verify source/imports.c
```

`PORT_NPDM_PROGRAM_ID` in the `Makefile` (`0x01000000000010A4`) must be unique
among your ports: change it if it clashes.

## Layout

| Path | What |
| --- | --- |
| `runtime/` | the android32 runtime (MIT), unmodified |
| `source/port_config.h` | the game's name, package, APK rules, settings |
| `source/as4_main.c` | `port_load` / `port_run` |
| `source/as4_loader.c` | maps `libgame.so`, binds imports, runs constructors |
| `source/as4_boot.c` | the activity and GL thread: JNI_OnLoad, `Cocos2dxHelper` paths, `nativeInit`, the frame loop, touches, pause/resume |
| `source/as4_java.c` | the Java side, answered in C (JNI tables) |
| `source/as4_input.c` | touchscreen, pointer, buttons |
| `source/as4_egl.c` | EGL context for the game |
| `source/as4_shims.c` | libc/zlib imports the runtime lacks |
| `source/imports.c` | generated import table (`tools/gen_imports.py`) |
| `tools/` | `imports.cfg`, `imports_needed.txt` (symbol names only) |
| `launcher/` | the NRO that installs the 32-bit program |

## License

MIT for this wrapper and the runtime. The game belongs to its owners; this
project contains none of it.
