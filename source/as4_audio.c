/* as4_audio.c -- the sound of Amateur Surgeon 4: what Cocos2dxMusic and
 * Cocos2dxSound (Android's MediaPlayer and SoundPool) do on a phone.
 *
 * This build of cocos2d-x's SimpleAudioEngine sends ALL of its sound to Java:
 * playEffect -> playEffectJNI, playBackgroundMusic -> playBackgroundMusicJNI,
 * both static methods of org.cocos2dx.lib.Cocos2dxHelper (the OpenSL engine
 * in the library is never used). With no Java here, the game was silent.
 * as4_java.c routes those methods here.
 *
 *   effects   the file (WAV or MP3, from the APK's assets/ or a downloaded
 *             file) decoded once to 16-bit PCM and kept; up to MAX_VOICES
 *             play at once, each with loop, pitch, pan and gain
 *   music     one MP3 (or WAV) streamed: decoded as it plays
 *
 * A mixer thread (rt_audout_pump_start) resamples every voice to the
 * device's rate (48 kHz, linear interpolation) and mixes them. Decoders:
 * dr_mp3 and dr_wav (third_party/, public domain). MIT.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <switch.h>

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#define DR_MP3_NO_SIMD
#include "third_party/dr_mp3.h"
#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#include "third_party/dr_wav.h"

#include "as4.h"
#include "rt_audout.h"
#include "util.h"

#define MAX_SAMPLES 512 /* decoded effects kept (the APK has ~130 sounds) */
#define MAX_VOICES 32
#define FIX 16          /* 16.16 fixed-point resampler position */
#define MP3_CHUNK 1152

typedef struct {
  char path[256];
  int16_t *pcm; /* interleaved, ch channels */
  uint32_t frames;
  uint32_t rate;
  int ch;       /* 1 or 2 */
} Sample;

typedef struct {
  int id;
  int active, paused, loop;
  int music;          /* the background-music voice */
  /* source: a decoded sample, or an MP3 stream */
  Sample *smp;
  uint64_t pos;       /* next frame of smp */
  drmp3 *mp3;
  void *mp3_data;
  int16_t chunk[MP3_CHUNK * 2];
  uint32_t chunk_n, chunk_i;
  int src_ch;
  /* resampler */
  uint32_t step, frac;
  int16_t cur[2], nxt[2];
  int primed;
  float pitch, pan, gain;
} Voice;

static Mutex g_lock;
static Sample g_samples[MAX_SAMPLES];
static int g_nsamples;
static Voice g_voices[MAX_VOICES];
static Voice g_music;
static char g_music_path[256];
static float g_fx_volume = 1.0f, g_music_volume = 1.0f;
static int g_next_id = 1;
static int g_started;
static unsigned g_out_rate = 48000;

/* ------------------------------------------------------------ decoding */
static int ends_with(const char *s, const char *ext) {
  size_t n = strlen(s), e = strlen(ext);
  return n >= e && !strcasecmp(s + n - e, ext);
}

static int16_t *decode_whole(const void *data, size_t size, const char *path, uint32_t *frames,
                             uint32_t *rate, int *ch) {
  int16_t *pcm = NULL;
  drmp3_uint64 nf = 0;
  if (!ends_with(path, ".mp3")) {
    unsigned int c = 0, r = 0;
    drwav_uint64 wf = 0;
    pcm = drwav_open_memory_and_read_pcm_frames_s16(data, size, &c, &r, &wf, NULL);
    if (pcm) {
      *frames = (uint32_t)wf, *rate = r, *ch = (int)c;
    }
  }
  if (!pcm) {
    drmp3_config cfg = {0};
    pcm = drmp3_open_memory_and_read_pcm_frames_s16(data, size, &cfg, &nf, NULL);
    if (pcm) {
      *frames = (uint32_t)nf, *rate = cfg.sampleRate, *ch = (int)cfg.channels;
    }
  }
  if (pcm && (*ch < 1 || *ch > 2 || !*rate)) {
    /* more than two channels: keep the first two */
    if (*ch > 2 && *rate) {
      for (uint32_t i = 0; i < *frames; i++) {
        pcm[i * 2] = pcm[i * *ch];
        pcm[i * 2 + 1] = pcm[i * *ch + 1];
      }
      *ch = 2;
    } else {
      drmp3_free(pcm, NULL);
      pcm = NULL;
    }
  }
  return pcm;
}

/* The decoded sound for path (loaded on first use), or NULL. Game thread. */
static Sample *sample_get(const char *path) {
  if (!path || !*path)
    return NULL;
  for (int i = 0; i < g_nsamples; i++)
    if (g_samples[i].pcm && !strcmp(g_samples[i].path, path))
      return &g_samples[i];
  size_t size = 0;
  void *data = as4_file_read(path, &size);
  if (!data) {
    static int logged;
    if (logged++ < 32)
      debugPrintf("[sound] %s: not found\n", path);
    return NULL;
  }
  uint32_t frames = 0, rate = 0;
  int ch = 0;
  int16_t *pcm = decode_whole(data, size, path, &frames, &rate, &ch);
  free(data);
  if (!pcm || !frames) {
    debugPrintf("[sound] %s: could not decode (%u bytes)\n", path, (unsigned)size);
    if (pcm)
      drmp3_free(pcm, NULL);
    return NULL;
  }
  int slot = -1;
  for (int i = 0; i < g_nsamples; i++)
    if (!g_samples[i].pcm) {
      slot = i;
      break;
    }
  if (slot < 0) {
    if (g_nsamples == MAX_SAMPLES) {
      debugPrintf("[sound] sample table full: %s not kept\n", path);
      drmp3_free(pcm, NULL);
      return NULL;
    }
    slot = g_nsamples++;
  }
  Sample *s = &g_samples[slot];
  snprintf(s->path, sizeof s->path, "%s", path);
  s->frames = frames, s->rate = rate, s->ch = ch;
  mutexLock(&g_lock);
  s->pcm = pcm; /* published under the lock: the mixer sees it whole */
  mutexUnlock(&g_lock);
  return s;
}

/* ------------------------------------------------------------ the mixer */
/* The voice's next source frame (stereo). 0 at its end. Mixer lock held. */
static int src_frame(Voice *v, int16_t out[2]) {
  if (v->smp) {
    if (v->pos >= v->smp->frames) {
      if (!v->loop)
        return 0;
      v->pos = 0;
    }
    const int16_t *p = v->smp->pcm + v->pos * (uint64_t)v->smp->ch;
    out[0] = p[0];
    out[1] = v->smp->ch == 2 ? p[1] : p[0];
    v->pos++;
    return 1;
  }
  if (v->mp3) {
    if (v->chunk_i >= v->chunk_n) {
      v->chunk_n = (uint32_t)drmp3_read_pcm_frames_s16(v->mp3, MP3_CHUNK, v->chunk);
      v->chunk_i = 0;
      if (!v->chunk_n) {
        if (!v->loop || !drmp3_seek_to_pcm_frame(v->mp3, 0))
          return 0;
        v->chunk_n = (uint32_t)drmp3_read_pcm_frames_s16(v->mp3, MP3_CHUNK, v->chunk);
        if (!v->chunk_n)
          return 0;
      }
    }
    const int16_t *p = v->chunk + v->chunk_i * (uint32_t)v->src_ch;
    out[0] = p[0];
    out[1] = v->src_ch == 2 ? p[1] : p[0];
    v->chunk_i++;
    return 1;
  }
  return 0;
}

static void voice_set_step(Voice *v) {
  unsigned rate = v->smp ? v->smp->rate : v->mp3 ? v->mp3->sampleRate : g_out_rate;
  double st = (double)rate / (double)g_out_rate * (v->pitch > 0.05f ? v->pitch : 1.0f);
  if (st > 8.0)
    st = 8.0;
  v->step = (uint32_t)(st * (1 << FIX) + 0.5);
}

static void voice_release(Voice *v) {
  if (v->mp3) {
    drmp3_uninit(v->mp3);
    free(v->mp3);
  }
  free(v->mp3_data);
  memset(v, 0, sizeof *v);
}

static void mix_voice(Voice *v, int32_t *acc, int frames, float volume) {
  if (!v->active || v->paused)
    return;
  if (!v->primed) {
    if (!src_frame(v, v->cur) || !src_frame(v, v->nxt)) {
      v->nxt[0] = v->cur[0], v->nxt[1] = v->cur[1];
    }
    v->frac = 0;
    v->primed = 1;
  }
  float g = v->gain * volume;
  float gl = g * (v->pan > 0 ? 1.0f - v->pan : 1.0f);
  float gr = g * (v->pan < 0 ? 1.0f + v->pan : 1.0f);
  int32_t il = (int32_t)(gl * 256.0f), ir = (int32_t)(gr * 256.0f);
  for (int i = 0; i < frames; i++) {
    int32_t f = (int32_t)(v->frac & 0xFFFF);
    int32_t l = v->cur[0] + (((v->nxt[0] - v->cur[0]) * f) >> FIX);
    int32_t r = v->cur[1] + (((v->nxt[1] - v->cur[1]) * f) >> FIX);
    acc[i * 2] += (l * il) >> 8;
    acc[i * 2 + 1] += (r * ir) >> 8;
    v->frac += v->step;
    while (v->frac >= (1u << FIX)) {
      v->frac -= 1u << FIX;
      v->cur[0] = v->nxt[0], v->cur[1] = v->nxt[1];
      if (!src_frame(v, v->nxt)) {
        /* the end: the last frame plays out, then the voice is free */
        if (v->music) {
          v->active = 0;
        } else {
          v->active = 0;
          v->smp = NULL;
        }
        return;
      }
    }
  }
}

static int32_t g_acc[RT_AUDOUT_FRAMES * 2];

static void fill(int16_t *out, int frames, void *ud) {
  (void)ud;
  if (frames > RT_AUDOUT_FRAMES)
    frames = RT_AUDOUT_FRAMES;
  memset(g_acc, 0, sizeof(int32_t) * 2 * (size_t)frames);
  mutexLock(&g_lock);
  for (int i = 0; i < MAX_VOICES; i++)
    mix_voice(&g_voices[i], g_acc, frames, g_fx_volume);
  mix_voice(&g_music, g_acc, frames, g_music_volume);
  mutexUnlock(&g_lock);
  for (int i = 0; i < frames * 2; i++) {
    int32_t s = g_acc[i];
    out[i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
  }
}

void as4_audio_init(void) {
  if (g_started)
    return;
  g_started = 1;
  if (rt_audout_pump_start(fill, NULL, 0x2A, -2) != 0) {
    debugPrintf("[sound] no mixer thread: the game is silent\n");
    return;
  }
  g_out_rate = rt_audout_rate();
  debugPrintf("[sound] Cocos2dxMusic/Cocos2dxSound in C: %u Hz, %d effect voices, WAV + MP3\n",
              g_out_rate, MAX_VOICES);
}

/* ------------------------------------------------------------ effects */
int as4_fx_play(const char *path, int loop, float pitch, float pan, float gain) {
  as4_audio_init();
  Sample *s = sample_get(path);
  if (!s)
    return 0;
  mutexLock(&g_lock);
  Voice *v = NULL;
  for (int i = 0; i < MAX_VOICES; i++)
    if (!g_voices[i].active) {
      v = &g_voices[i];
      break;
    }
  if (!v) { /* every voice busy: the oldest non-looping one makes room */
    int best = -1;
    for (int i = 0; i < MAX_VOICES; i++)
      if (!g_voices[i].loop && (best < 0 || g_voices[i].id < g_voices[best].id))
        best = i;
    v = &g_voices[best < 0 ? 0 : best];
  }
  memset(v, 0, sizeof *v);
  v->id = g_next_id++;
  if (g_next_id <= 0)
    g_next_id = 1;
  v->smp = s;
  v->loop = loop;
  v->pitch = pitch > 0 ? pitch : 1.0f;
  v->pan = pan < -1 ? -1 : pan > 1 ? 1 : pan;
  v->gain = gain > 0 ? gain : gain == 0 ? 0.0f : 1.0f;
  voice_set_step(v);
  v->active = 1;
  int id = v->id;
  mutexUnlock(&g_lock);
  return id;
}

static Voice *fx_find(int id) {
  for (int i = 0; i < MAX_VOICES; i++)
    if (g_voices[i].active && g_voices[i].id == id)
      return &g_voices[i];
  return NULL;
}

void as4_fx_stop(int id) {
  mutexLock(&g_lock);
  Voice *v = fx_find(id);
  if (v)
    memset(v, 0, sizeof *v);
  mutexUnlock(&g_lock);
}

void as4_fx_pause(int id, int paused) {
  mutexLock(&g_lock);
  Voice *v = fx_find(id);
  if (v)
    v->paused = paused;
  mutexUnlock(&g_lock);
}

void as4_fx_all(int what) { /* 0 stop, 1 pause, 2 resume */
  mutexLock(&g_lock);
  for (int i = 0; i < MAX_VOICES; i++) {
    if (!g_voices[i].active)
      continue;
    if (what == 0)
      memset(&g_voices[i], 0, sizeof g_voices[i]);
    else
      g_voices[i].paused = what == 1;
  }
  mutexUnlock(&g_lock);
}

int as4_fx_playing(int id) {
  mutexLock(&g_lock);
  Voice *v = fx_find(id);
  int r = v && !v->paused;
  mutexUnlock(&g_lock);
  return r;
}

void as4_fx_set_pitch(int id, float pitch) {
  mutexLock(&g_lock);
  Voice *v = fx_find(id);
  if (v) {
    v->pitch = pitch > 0 ? pitch : 1.0f;
    voice_set_step(v);
  }
  mutexUnlock(&g_lock);
}

void as4_fx_set_volume(int id, float gain) {
  mutexLock(&g_lock);
  Voice *v = fx_find(id);
  if (v)
    v->gain = gain < 0 ? 0 : gain;
  mutexUnlock(&g_lock);
}

float as4_fx_get_volume(int id) {
  mutexLock(&g_lock);
  Voice *v = fx_find(id);
  float g = v ? v->gain : 0.0f;
  mutexUnlock(&g_lock);
  return g;
}

void as4_fx_set_master(float vol) { g_fx_volume = vol < 0 ? 0 : vol > 1 ? 1 : vol; }
float as4_fx_get_master(void) { return g_fx_volume; }

void as4_fx_preload(const char *path) { sample_get(path); }

void as4_fx_unload(const char *path) {
  if (!path)
    return;
  mutexLock(&g_lock);
  for (int i = 0; i < g_nsamples; i++) {
    Sample *s = &g_samples[i];
    if (!s->pcm || strcmp(s->path, path))
      continue;
    for (int k = 0; k < MAX_VOICES; k++)
      if (g_voices[k].smp == s)
        memset(&g_voices[k], 0, sizeof g_voices[k]);
    if (g_music.smp == s)
      voice_release(&g_music);
    drmp3_free(s->pcm, NULL);
    memset(s, 0, sizeof *s);
  }
  mutexUnlock(&g_lock);
}

/* ------------------------------------------------------------ music */
static void music_stop_locked(void) {
  voice_release(&g_music);
}

void as4_music_preload(const char *path) {
  (void)path; /* the stream opens when it plays: nothing to gain here */
}

void as4_music_play(const char *path, int loop) {
  as4_audio_init();
  if (!path || !*path)
    return;
  /* load outside the lock (file read), swap in under it */
  Voice nv;
  memset(&nv, 0, sizeof nv);
  nv.music = 1, nv.loop = loop, nv.pitch = 1.0f, nv.gain = 1.0f;
  if (ends_with(path, ".mp3")) {
    size_t size = 0;
    void *data = as4_file_read(path, &size);
    drmp3 *mp3 = data ? calloc(1, sizeof *mp3) : NULL;
    if (!mp3 || !drmp3_init_memory(mp3, data, size, NULL)) {
      debugPrintf("[music] %s: %s\n", path, data ? "could not decode" : "not found");
      free(mp3);
      free(data);
      return;
    }
    nv.mp3 = mp3, nv.mp3_data = data;
    nv.src_ch = (int)(mp3->channels >= 2 ? 2 : 1);
    if (mp3->channels > 2) {
      /* the chunk buffer holds two channels: such a file is not expected */
      debugPrintf("[music] %s: %u channels: not played\n", path, (unsigned)mp3->channels);
      drmp3_uninit(mp3);
      free(mp3);
      free(data);
      return;
    }
  } else {
    nv.smp = sample_get(path);
    if (!nv.smp)
      return;
  }
  voice_set_step(&nv);
  nv.active = 1;
  mutexLock(&g_lock);
  music_stop_locked();
  g_music = nv;
  snprintf(g_music_path, sizeof g_music_path, "%s", path);
  mutexUnlock(&g_lock);
  static int logged;
  if (logged++ < 16)
    debugPrintf("[music] %s%s\n", path, loop ? " (loop)" : "");
}

void as4_music_stop(void) {
  mutexLock(&g_lock);
  music_stop_locked();
  g_music_path[0] = 0;
  mutexUnlock(&g_lock);
}

void as4_music_pause(int paused) {
  mutexLock(&g_lock);
  g_music.paused = paused;
  mutexUnlock(&g_lock);
}

void as4_music_rewind(void) {
  mutexLock(&g_lock);
  if (g_music.mp3) {
    drmp3_seek_to_pcm_frame(g_music.mp3, 0);
    g_music.chunk_n = g_music.chunk_i = 0;
  }
  g_music.pos = 0;
  g_music.primed = 0;
  if (g_music.mp3 || g_music.smp) {
    g_music.active = 1;
    g_music.paused = 0;
  }
  mutexUnlock(&g_lock);
}

int as4_music_playing(void) {
  mutexLock(&g_lock);
  int r = g_music.active && !g_music.paused;
  mutexUnlock(&g_lock);
  return r;
}

void as4_music_set_volume(float vol) { g_music_volume = vol < 0 ? 0 : vol > 1 ? 1 : vol; }
float as4_music_get_volume(void) { return g_music_volume; }

/* the position in seconds (setBackgroundMusicOffset / get...) */
float as4_music_get_offset(void) {
  mutexLock(&g_lock);
  float t = 0;
  if (g_music.mp3 && g_music.mp3->sampleRate)
    t = (float)((double)g_music.mp3->currentPCMFrame / g_music.mp3->sampleRate);
  else if (g_music.smp && g_music.smp->rate)
    t = (float)((double)g_music.pos / g_music.smp->rate);
  mutexUnlock(&g_lock);
  return t;
}

void as4_music_set_offset(float seconds) {
  if (seconds < 0)
    seconds = 0;
  mutexLock(&g_lock);
  if (g_music.mp3) {
    drmp3_seek_to_pcm_frame(g_music.mp3, (drmp3_uint64)(seconds * g_music.mp3->sampleRate));
    g_music.chunk_n = g_music.chunk_i = 0;
    g_music.primed = 0;
  } else if (g_music.smp) {
    uint64_t f = (uint64_t)(seconds * g_music.smp->rate);
    g_music.pos = f < g_music.smp->frames ? f : 0;
    g_music.primed = 0;
  }
  mutexUnlock(&g_lock);
}

/* Cocos2dxHelper.end(): the engine shuts its sound down. */
void as4_audio_end(void) {
  as4_fx_all(0);
  as4_music_stop();
}
