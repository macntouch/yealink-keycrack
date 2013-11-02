#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <openssl/aes.h>

static char keymap[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

#define MS_RAND_MAX ((1U << 31) - 1)
static int ms_rseed = 0;
inline void ms_srand(int x) {
  ms_rseed = x;
}
inline int ms_rand() {
  return (ms_rseed = (ms_rseed * 214013 + 2531011) & MS_RAND_MAX) >> 16;
}

static char rchar() {
  int i = ms_rand() % 62;
  return keymap[i];
}

static char* rkey(char *key) {
  int i;
  for (i=0; i<16; i++)
    key[i] = rchar();
  return key;
}

static char* test_key(char *key, char *obuf, char *ibuf, size_t buf_len) {
  char *bp=ibuf, *be=ibuf+buf_len;
  char *obp=obuf, *obe=obuf;
  AES_KEY akey;
  AES_set_decrypt_key(key, 128, &akey);
  for (; bp<be; bp+=16) {
    AES_decrypt(bp, obp, &akey); obe+=16;
    for (; obp<obe; obp++)
      if (*obp > 127 || *obp < 9 || (*obp > 13 && *obp < 32)) return 0;
  }
  obuf[buf_len] = 0;
  return obuf;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <cfg>\n", argv[0]);
    return 255;
  }
  char *cfg_p = argv[1];
  struct stat cfg_s;
  if (stat(cfg_p, &cfg_s)) {
    perror("Error"); return 254; }
  char *cfg_ib, *cfg_ob;
  if (!(cfg_ib = malloc(cfg_s.st_size+1))) {
    perror("Error"); return 254; }
  if (!(cfg_ob = malloc(cfg_s.st_size+1))) {
    perror("Error"); return 254; }
  FILE *cfg_f;
  if (!(cfg_f = fopen(cfg_p, "r"))) {
    perror("Error"); return 254; }
  if (1 != fread(cfg_ib, cfg_s.st_size, 1, cfg_f)) {
    fprintf(stderr, "Read failed\n"); return 254; }
  cfg_ib[cfg_s.st_size] = 0;
  if (fclose(cfg_f)) {
    perror("Error"); return 254; }
  time_t t = time(NULL);
  unsigned int tu = (unsigned int)t, stop=++tu;
  char key[17] = "";
  uint32_t c = 0;
  int found = 0;
  while (1) {
    ms_srand(tu); rkey(key);
    if (test_key(key, cfg_ob, cfg_ib, cfg_s.st_size)) {
      found++; break; }
    tu--; c++;
    if (!(c & 0x00ffffff))
      fprintf(stderr,"%d/256 done\n", (c>>24));
    if (tu==stop) break;
  }
  if (found) {
    fprintf(stderr, "Found key: %s generated at %u\n", key, tu);
    printf("%s",cfg_ob);
    return 0;
  } else {
    printf("Couldn't find key, giving up\n");
    return 1;
  }
}