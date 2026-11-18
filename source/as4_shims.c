/* as4_shims.c -- libc and zlib imports of libgame.so that the runtime's bionic
 * layer does not cover. Each is a bionic-ABI shim named b_<symbol> (found by
 * tools/gen_imports.py). They are the smallest honest answers: where the
 * engine can live without a feature, the call fails the way a phone would
 * fail it. MIT.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz/miniz.h>

#include "bionic.h"

int b_inflateReset(mz_streamp strm); /* bionic_zlib.c */

/* ---- the asset manager: the game reads its files from the APK itself ---- */
static uint32_t g_manager[4] = {0x41533452u}; /* 'AS4R' */
void *b_AAssetManager_fromJava(void *env, void *asset_manager) {
  (void)env; (void)asset_manager;
  return g_manager;
}

/* ---- math ---- */
int b___isnanf(float x) { return x != x; }

/* ---- random ---- */
uint32_t b_arc4random(void) {
  uint32_t v = 0;
  randomGet(&v, sizeof v);
  return v;
}

/* ---- stdio: the descriptor of a FILE (a real one; bionic's std streams are
 * b___sF's three entries) ---- */
int b_fileno(void *fp) {
  if (!fp) {
    b_set_errno(L_EINVAL);
    return -1;
  }
  unsigned char *p = fp;
  if (p == &b___sF[0]) return 0;
  if (p == &b___sF[B_FILE_SIZE]) return 1;
  if (p == &b___sF[2 * B_FILE_SIZE]) return 2;
  return fileno((FILE *)fp);
}

/* ---- the user database and the network interfaces: none here ---- */
int b_getpwuid_r(unsigned uid, void *pwd, char *buf, size_t buflen, void **result) {
  (void)uid; (void)pwd; (void)buf; (void)buflen;
  if (result)
    *result = NULL;
  return L_ENOENT;
}

unsigned b_if_nametoindex(const char *name) {
  (void)name;
  return 0;
}

int b_socketpair(int domain, int type, int protocol, int sv[2]) {
  (void)domain; (void)type; (void)protocol; (void)sv;
  b_set_errno(L_ENOSYS);
  return -1;
}

/* ---- zlib, beyond bionic_zlib.c ---- */
unsigned long b_adler32(unsigned long adler, const unsigned char *buf, unsigned len) {
  return mz_adler32(adler, buf, len);
}

/* Changing the level or strategy mid-stream: accepted, not applied (the
 * stream keeps what deflateInit chose; the output is still valid). */
int b_deflateParams(mz_streamp strm, int level, int strategy) {
  (void)strm; (void)level; (void)strategy;
  return 0; /* Z_OK */
}

int b_inflateReset2(mz_streamp strm, int window_bits) {
  (void)window_bits;
  return b_inflateReset(strm);
}

int b_inflateSync(mz_streamp strm) {
  (void)strm;
  return -3; /* Z_DATA_ERROR: no sync point to find */
}

/* gz*: the engine's own file layer does not use them on this path; a gz file
 * does not open, which callers treat as "no file". */
void *b_gzopen(const char *path, const char *mode) {
  (void)path; (void)mode;
  b_set_errno(L_ENOSYS);
  return NULL;
}
int b_gzread(void *file, void *buf, unsigned len) {
  (void)file; (void)buf; (void)len;
  return -1;
}
int b_gzclose(void *file) {
  (void)file;
  return -2; /* Z_STREAM_ERROR */
}
