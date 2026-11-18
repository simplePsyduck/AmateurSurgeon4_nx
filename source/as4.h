/* as4.h -- what Amateur Surgeon 4's files share. MIT. */
#ifndef AS4_H
#define AS4_H

#include <stddef.h>

#include "so_util.h"

extern so_module g_mod_game;

/* as4_loader.c */
int as4_load_module(void);
void as4_run_constructors(void);
void *as4_native(const char *symbol);

/* as4_egl.c: the GLSurfaceView's EGL */
int as4_egl_init(void);
void as4_egl_swap(void);

/* as4_java.c: the Java side, answered in C */
void as4_java_init(void);

/* as4_boot.c: the activity, the renderer and the frames */
int as4_boot_run(void);
int as4_surface_w(void);
int as4_surface_h(void);

/* as4_input.c */
void as4_input_init(void);
void as4_input_update(void);

/* as4_boot.c, for as4_input.c: Cocos2dxRenderer.nativeTouches*, nativeKeyDown */
void as4_touch_begin(int id, float x, float y);
void as4_touch_move(int id, float x, float y);
void as4_touch_end(int id, float x, float y);
void as4_key_down(int android_keycode);

/* as4_apk.c: files the Java side would open (malloc'd; NULL if absent) */
void *as4_apk_read(const char *entry, size_t *size);
int as4_apk_list(int (*cb)(const char *entry, void *ud), void *ud);
void *as4_file_read(const char *path, size_t *size); /* absolute Android path, or an asset */

/* as4_text.c: Cocos2dxBitmap (TTF labels) */
int as4_text_create(const char *text, const char *font_name, int font_size, int alignment,
                    int width, int height);
int as4_text_font_size_for_height(int height);
char *as4_text_ellipsis(const char *text, float width, float font_size);

/* as4_audio.c: Cocos2dxMusic / Cocos2dxSound */
void as4_audio_init(void);
void as4_audio_end(void);
int as4_fx_play(const char *path, int loop, float pitch, float pan, float gain);
void as4_fx_stop(int id);
void as4_fx_pause(int id, int paused);
void as4_fx_all(int what); /* 0 stop, 1 pause, 2 resume */
int as4_fx_playing(int id);
void as4_fx_set_pitch(int id, float pitch);
void as4_fx_set_volume(int id, float gain);
float as4_fx_get_volume(int id);
void as4_fx_set_master(float vol);
float as4_fx_get_master(void);
void as4_fx_preload(const char *path);
void as4_fx_unload(const char *path);
void as4_music_preload(const char *path);
void as4_music_play(const char *path, int loop);
void as4_music_stop(void);
void as4_music_pause(int paused);
void as4_music_rewind(void);
int as4_music_playing(void);
void as4_music_set_volume(float vol);
float as4_music_get_volume(void);
float as4_music_get_offset(void);
void as4_music_set_offset(float seconds);

#endif
