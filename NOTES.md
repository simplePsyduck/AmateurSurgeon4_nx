# amatuersurgeon4_nx: notes

## The game, from the libraries (build supplied for analysis)

- `libgame.so`: 32-bit ARM, ARMv7 + VFPv3 + NEON, soft-float ABI (softfp, like
  the runtime). Highest vaddr+memsz about 0x98b000 (about 9.6 MB), well inside
  `PORT_SO_REGION_BYTES` (32 MB). 344 dynamic imports; 73 are `gl*` (the GL
  layer), the other 271 are in `source/imports.c`.
- cocos2d-x driven from Java: no NativeActivity. Natives are
  `Java_org_cocos2dx_lib_Cocos2dxRenderer_*` (`nativeInit(w,h)`, `nativeRender`,
  `nativeTouchesBegin/Move/End/Cancel`, `nativeKeyDown`, `nativeOnPause/OnResume`,
  `didBecomeActive`) and `Cocos2dxHelper_nativeSet{Apk,Writeable,Cache,ExternalAsset,Obb}Path`.
- The engine imports no EGL: the port creates the context (`as4_egl.c`).
- Assets are read by the engine from the APK path `getApkPath()` (a zip,
  `assets/...`). The runtime serves `/data/app/<pkg>-1/base.apk` as the
  player's own APK, so `nativeSetApkPath(DCR_ANDROID_APK)` is enough.
- Audio: the engine has its own `OpenSLEngine` (effects) and calls Java
  `playBackgroundMusic`/`playEffect`... for music. `RT_OPENSLES 1`.
- Java classes it calls: `com.android.game.{GameActivity, PlatformUtils,
  PermissionsManager, IabManager, ASFacebookManager, KGooglePlayManager,
  KFGooglePlayManager, FyberManager(JNI), AmplitudeManager, UpsightManager,
  EULAManager}` and `org.cocos2dx.lib.{Cocos2dxHelper, Cocos2dxBitmap,
  Cocos2dxRenderer, Cocos2dxAccelerometer}`.
- `libcomScore.so` and `libImmEndpointWarpJ.so` are analytics SDKs: not loaded.
- The APK's ABI folder is assumed to be `lib/armeabi-v7a/` (`PORT_ABI_DIR`, the
  runtime's default). If the player's APK keeps `libgame.so` under `lib/armeabi/`,
  set `PORT_ABI_DIR` and the `.need` entry in `port_config.h`.

## Fixed: abort() at libgame+0x804586 (std::logic_error at nativeInit)

The first hardware run ended with "The game called abort() from 0x33aec58b"
(libgame.so loaded at 0x332e8000, so libgame+0x804586:
`__gnu_cxx::__verbose_terminate_handler`). debug.log showed an uncaught
`std::logic_error: basic_string::_S_construct null not valid`, after
`NoClassDefFoundError` for `Cocos2dxRenderer`, `Cocos2dxHelper` and
`PlatformUtils`. Cause: APK 2.7.10 ships **45** `classesN.dex` files (the
cocos2d-x and game classes are in `classes38.dex`), but the runtime's setup
(`runtime/source/dcr_setup.c`, `ensure_classes`) read only the first 32 when
writing `classes.txt`. FindClass then refused those classes, the JNI getters
returned NULL, and the engine built a `std::string` from NULL. The cap is now
512 (`DCR_MAX_DEX`). `classes.txt` is rebuilt by itself on the next launch,
because its stamp (the dex count and CRCs) no longer matches. 10054 classes
instead of 7026.

## Fixed: fault in nativeInit (getFreeMB answered as an int)

With the classes found, the next run died silently inside `nativeInit`. The
engine calls `PlatformUtils.getFreeMB` with `CallStaticObjectMethod`: its
signature is `()Ljava/lang/String;`, and the engine passes the result to
`jstring2string` and then `atoll`. `h_device` answered `jv_i(4096)`
whatever the signature was, so 0x1000 reached `GetStringUTFChars` as a
jstring, and `is_obj()` read unmapped memory. Now:
- `as4_java.c` answers by the requested return type (`ret_num`/`ret_str`),
  using the 93 static-method signatures read from libgame.so. Cocos2dxHelper
  has its own handler: the package name, the language and country (config.ini's
  `[game] language`, else the console's), the device model. Cocos2dxBitmap
  gets a font size and passes the string through. `getApplicationVersion` is
  the manifest's versionName.
- `runtime/source/jni_core.c` `is_obj()` no longer dereferences pointers
  below 64 KB: it logs `[jni] 0x... used as a Java object` and treats them as
  null.

## Done: text and sound (as4_text.c, as4_audio.c, as4_apk.c)

The third hardware run played with no labels (`_updateTexture: oops` once per
CCLabelTTF) and no sound. Both were Java's job on a phone:

- **Text.** CCLabelTTF calls the static Java method `Cocos2dxBitmap.createTextBitmap
  (text, font, size, alignment, w, h)`, which draws with Canvas and answers
  through the native `Cocos2dxBitmap.nativeInitBitmapDC(w, h, byte[] RGBA)`.
  `as4_text.c` does the same: the layout of Cocos2dxBitmap.java (line breaks,
  word wrap to the box width, line height from the font's bounding box,
  horizontal/vertical alignment 0x11..0x33), glyphs drawn with stb_truetype in
  the APK's own fonts (assets/*.ttf), white premultiplied, handed to the
  engine's nativeInitBitmapDC. Font names are matched to the APK's fonts by file
  name and by the TrueType name table ("Verdana Bold", "Londrina Solid",
  "Motter Corpus Cond OS ITC TT SL"). Faces the APK lacks ("Marker Felt",
  "trebuc.ttf") fall back to Verdana-Bold. `getFontSizeAccordingHeight` and
  `getStringWithEllipsis` are answered too.
- **Sound.** This build's SimpleAudioEngine sends every sound to static methods of
  `org.cocos2dx.lib.Cocos2dxHelper` (playEffect -> playEffectJNI, music ->
  playBackgroundMusicJNI). The OpenSL engine in the library is never used.
  `as4_audio.c` implements those methods (all 39 names the library uses): a
  mixer thread on `rt_audout_pump_start`, 32 effect voices (loop, pitch, pan,
  gain, pause/resume/stop) plus streamed MP3 music, resampled to 48 kHz.
  Effects (120 WAV, some MP3) are decoded once and kept; music (14 MP3) is
  decoded as it plays. Decoders: dr_wav, dr_mp3.
- `as4_apk.c` reads APK entries (`assets/` + the path, as Android's
  AssetManager) and absolute Android paths (downloaded files) for both.
- Third-party code: `source/third_party/` (stb_truetype, dr_mp3, dr_wav), single
  headers in the public domain (see its README). No game data.
- Tested on the host (x86-64, ASan + UBSan) against the 2.7.10 APK: all five
  fonts render, wrap and align; WAV + MP3 decode, mix, loop, pause and stop.
  The libnx parts (the mixer thread, `setGetSystemLanguage`) are untested on hardware.

## Known gaps, most important first

1. **Text and sound: done in C** (the third hardware run had neither; see below).
   Left to check on hardware: font choices that look wrong (debug.log lists
   `[text] font 'X' -> Y` for every name the game asks for), and whether
   `setBackgroundMusicOffset` is in seconds (assumed: its JNI call passes a float).
3. **GameActivity natives not called yet.** `nativeSendVersion` and
   `nativeSetUseLargeScale` take arguments not yet confirmed from the
   disassembly; the game's defaults are used meanwhile.
4. **JNI to-do list.** The first run logs `[jni] unhandled ...` once per
   method the engine asks for that has no handler: those lines are the work
   list for `as4_java.c`.
5. **Unverified build.** Not compiled against libnx32/Mesa yet. `gz*` and
   `deflateParams` are minimal stubs in `as4_shims.c`.
6. **Multi-touch move** sends one pointer per call (`nativeTouchesMove` with
   one-element arrays); fine for dragging tools, not tuned for two fingers.
7. **Program id** `0x01000000000010A4` is a guess at a free one.

## Legal

No game file is in this project, and `.gitignore` keeps them out. The import
cache `tools/imports_needed.txt` holds symbol names only (an interface, not game
content).
