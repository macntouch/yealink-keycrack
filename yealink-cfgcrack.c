#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <openssl/aes.h>

typedef unsigned char uchar;

static void errout() { perror("Error"); exit(254); }
static void errout1(const char *msg) { fprintf(stderr, "%s\n", msg); exit(254); }

static unsigned int MS_RAND_MAX = ((1U << 31) - 1);
static unsigned int ms_rseed = 0;
static inline void ms_srand(unsigned int x) {
  ms_rseed = x;
}
static inline int ms_rand() {
  return (ms_rseed = (ms_rseed * 214013 + 2531011) & MS_RAND_MAX) >> 16;
}

static const uchar keymap[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
static uchar rchar() {
  int i = ms_rand() % 62;
  return keymap[i];
}

static uchar* rkey(uchar *key) {
  int i;
  for (i=0; i<16; i++)
    key[i] = rchar();
  return key;
}

static uchar* decrypt(const uchar *key, uchar *obuf, const uchar *ibuf, const size_t buf_len) {
  const uchar *ibp=ibuf, *ibe=ibuf+buf_len;
  uchar *obp=obuf;
  AES_KEY akey;
  AES_set_decrypt_key(key, 128, &akey);
  for (; ibp<ibe; ibp+=16, obp+=16)
    AES_decrypt(ibp, obp, &akey);
  obuf[buf_len] = 0;
  return obuf;
}

static const uint8_t max_invalid_bytes_per_block = 0;
static const uchar utf8_bom[] = "\xef\xbb\xbf";
static uchar* test_key(const uchar *key, uchar *obuf, const uchar *ibuf, const size_t buf_len) {
  const uchar *ibp=ibuf, *ibe=ibuf+buf_len;
  uchar *obp=obuf, *obe=obuf;
  AES_KEY akey;
  AES_set_decrypt_key(key, 128, &akey);
  uint8_t ni=0, ui=0, bi=0;
  static const uint8_t mi = max_invalid_bytes_per_block;
  for (; ibp<ibe; ibp+=16) {
    AES_decrypt(ibp, obp, &akey); obe+=16;
    for (; bi && (obp<obe); bi--, obp++) // handle remaining BOM bytes
      if (!(*obp == utf8_bom[3-bi])) {
        if (++ni>mi) return 0;
        else { bi=0; break; }}
    for (; ui && (obp<obe); ui--, obp++) // handle remaining UTF-8 bytes
      if (!((*obp & 0xc0) == 0x80)) {
        if (++ni>mi) return 0;
        else { ui=0; break; }}
    for (; obp<obe;) {
      if (!(*obp >> 7)) { // ASCII character
        for (; (obp<obe); obp++) {
          if (*obp >> 7) break; // non-ASCII
          else if (*obp > 127 || *obp < 9 || (*obp > 13 && *obp < 32)) { // non-printable
            if (++ni>mi) return 0; else break; }}
      } else if (*obp == 0xef) { // byte-order mark
        for (obp++, bi=2; bi && (obp<obe); bi--, obp++)
          if (!(*obp == utf8_bom[3-bi])) { // Invalid byte where BOM expected
            if (++ni>mi) return 0;
            else { bi=0; break; }}
      } else if ((*obp & 0xe0) == 0xc0 // UTF-8 first byte
                 || (*obp & 0xf0) == 0xe0
                 || (*obp & 0xf8) == 0xf0
                 || (*obp & 0xfc) == 0xf8
                 || (*obp & 0xfe) == 0xfc) {
        int b=*obp;
        for (; b & 0x80; b<<= 1, ui++); // Expect ui UTF-8 bytes
        for (ui--, obp++; ui && (obp<obe); ui--, obp++)
          if (!((*obp & 0xc0) == 0x80)) { // Invalid byte where UTF-8 expected
            if (++ni>mi) return 0;
            else { ui=0; break; }}
      } else { // Invalid byte
        if (++ni>mi) return 0;
        obp++;
      }
    }
  }
  obuf[buf_len] = 0;
  return obuf;
}

int main(int argc, const char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <cfg>\n", argv[0]);
    return 255;
  }
  const char *cfg_p = argv[1];
  struct stat cfg_s;
  if (stat(cfg_p, &cfg_s)) errout();
  uchar *cfg_ib, *cfg_ob, *cfg_obc;
  if (!(cfg_ib = malloc(cfg_s.st_size+1))) errout();
  if (!(cfg_ob = malloc(cfg_s.st_size+1))) errout();
  if (!(cfg_obc = malloc(cfg_s.st_size+1))) errout();
  FILE *cfg_f;
  if (!(cfg_f = fopen(cfg_p, "r"))) errout();
  if (1 != fread(cfg_ib, cfg_s.st_size, 1, cfg_f)) errout1("Read failed");
  cfg_ib[cfg_s.st_size] = 0;
  if (fclose(cfg_f)) errout();
  time_t t = time(NULL) & MS_RAND_MAX;
  unsigned int tu = (unsigned int)t, stop=(tu+1) & MS_RAND_MAX;
  uchar key[17] = "";
  uint32_t c = 0;
  struct timeval tv0, tvn, tvl;
  if (gettimeofday(&tv0, NULL)) errout();
  memcpy(&tvl,&tv0,sizeof(struct timeval));
  int found = 0;
  while (1) {
    ms_srand(tu); rkey(key);
    if (test_key(key, cfg_ob, cfg_ib, cfg_s.st_size)) {
      found++; break; }
    tu=(tu-1)&MS_RAND_MAX; c++;
    if (!(c & 0x00ffffff)) {
      if (gettimeofday(&tvn, NULL)) errout();
      double tdiff_total = (tvn.tv_sec-tv0.tv_sec) + (double)(tvn.tv_usec-tv0.tv_usec)/1000000;
      double tdiff_last = (tvn.tv_sec-tvl.tv_sec) + (double)(tvn.tv_usec-tvl.tv_usec)/1000000;
      double tps_total = c/tdiff_total, tps_last = 0x00ffffff/tdiff_last;
      fprintf(stderr,"%d/128 done @ %.0f keys/sec, %.0f keys/sec avg\n",
              (c>>24), tps_last, tps_total);
      tvl=tvn;
    }
    if (tu==stop) break;
  }
  if (gettimeofday(&tvn, NULL)) errout();
  double tdiff = (tvn.tv_sec-tv0.tv_sec) + (double)(tvn.tv_usec-tv0.tv_usec)/1000000;
  double tps = c/tdiff;
  fprintf(stderr, "Searched %d keys in %.3f seconds at %.3f keys/second\n", c, tdiff, tps);
  if (found) {
    unsigned int tu1 = tu, tu2 = tu+(MS_RAND_MAX+1); struct tm *tm;
    tm = gmtime((time_t*)&tu1);
    char tu1_hm[128]; strftime(tu1_hm, sizeof(tu1_hm), "%Y-%m-%dT%H:%M:%S +0000", tm);
    tm = gmtime((time_t*)&tu2);
    char tu2_hm[128]; strftime(tu2_hm, sizeof(tu2_hm), "%Y-%m-%dT%H:%M:%S +0000", tm);
    fprintf(stderr, "Found key \"%s\" generated at %u (%s) or %u (%s)\n\n",
            key, tu1, tu1_hm, tu2, tu2_hm);
    decrypt(key, cfg_obc, cfg_ib, cfg_s.st_size);
    if (memcmp(cfg_ob, cfg_obc, cfg_s.st_size))
      errout1("Internal error: decryption mismatch");
    fwrite(cfg_ob, cfg_s.st_size, 1, stdout);
    return 0;
  } else {
    fprintf(stderr, "Couldn't find key; giving up\n");
    return 1;
  }
}
