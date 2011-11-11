/* Copyright (c) 2001-2004, Roger Dingledine.
 * Copyright (c) 2004-2006, Roger Dingledine, Nick Mathewson.
 * Copyright (c) 2007-2011, The Tor Project, Inc. */
/* See LICENSE for licensing information */

/* Ordinarily defined in tor_main.c; this bit is just here to provide one
 * since we're not linking to tor_main.c */
const char tor_git_revision[] = "";

/**
 * \file bench.c
 * \brief Benchmarks for lower level Tor modules.
 **/

#include "orconfig.h"

#include "or.h"

#if defined(HAVE_CLOCK_GETTIME) && defined(CLOCK_PROCESS_CPUTIME_ID)
static uint64_t nanostart;
static inline uint64_t
timespec_to_nsec(const struct timespec *ts)
{
  return ((uint64_t)ts->tv_sec)*1000000000 + ts->tv_nsec;
}

static void
reset_perftime(void)
{
  struct timespec ts;
  int r;
  r = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
  tor_assert(r == 0);
  nanostart = timespec_to_nsec(&ts);
}

static uint64_t
perftime(void)
{
  struct timespec ts;
  int r;
  r = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
  tor_assert(r == 0);
  return timespec_to_nsec(&ts) - nanostart;
}

#else
static struct timeval tv_start = { 0, 0 };
static void
reset_perftime(void)
{
  tor_gettimeofday(&tv_start);
}
static uint64_t
perftime(void)
{
  struct timeval now, out;
  tor_gettimeofday(&now);
  timersub(&now, &tv_start, &out);
  return ((uint64_t)out.tv_sec)*1000000000 + out.tv_usec*1000;
}
#endif

/** Run AES performance benchmarks. */
static void
bench_aes(void)
{
  int len, i;
  char *b1, *b2;
  crypto_cipher_env_t *c;
  uint64_t start, end;
  const int bytes_per_iter = (1<<24);
  reset_perftime();
  c = crypto_new_cipher_env();
  crypto_cipher_generate_key(c);
  crypto_cipher_encrypt_init_cipher(c);
  for (len = 1; len <= 8192; len *= 2) {
    int iters = bytes_per_iter / len;
    b1 = tor_malloc_zero(len);
    b2 = tor_malloc_zero(len);
    start = perftime();
    for (i = 0; i < iters; ++i) {
      crypto_cipher_encrypt(c, b1, b2, len);
    }
    end = perftime();
    tor_free(b1);
    tor_free(b2);
    printf("%d bytes: %.2f nsec per byte\n", len,
           ((double)(end-start))/(iters*len));
  }
  crypto_free_cipher_env(c);
}

static void
bench_cell_aes(void)
{
  uint64_t start, end;
  const int len = 509;
  const int iters = (1<<16);
  const int max_misalign = 15;
  char *b = tor_malloc(len+max_misalign);
  crypto_cipher_env_t *c;
  int i, misalign;

  c = crypto_new_cipher_env();
  crypto_cipher_generate_key(c);
  crypto_cipher_encrypt_init_cipher(c);

  reset_perftime();
  for (misalign = 0; misalign <= max_misalign; ++misalign) {
    start = perftime();
    for (i = 0; i < iters; ++i) {
      crypto_cipher_crypt_inplace(c, b+misalign, len);
    }
    end = perftime();
    printf("%d bytes, misaligned by %d: %.2f nsec per byte\n", len, misalign,
           ((double)(end-start))/(iters*len));
  }

  crypto_free_cipher_env(c);
  tor_free(b);
}

/** Run digestmap_t performance benchmarks. */
static void
bench_dmap(void)
{
  smartlist_t *sl = smartlist_create();
  smartlist_t *sl2 = smartlist_create();
  struct timeval start, end, pt2, pt3, pt4;
  const int iters = 10000;
  const int elts = 4000;
  const int fpostests = 1000000;
  char d[20];
  int i,n=0, fp = 0;
  digestmap_t *dm = digestmap_new();
  digestset_t *ds = digestset_new(elts);

  for (i = 0; i < elts; ++i) {
    crypto_rand(d, 20);
    smartlist_add(sl, tor_memdup(d, 20));
  }
  for (i = 0; i < elts; ++i) {
    crypto_rand(d, 20);
    smartlist_add(sl2, tor_memdup(d, 20));
  }
  printf("nbits=%d\n", ds->mask+1);

  tor_gettimeofday(&start);
  for (i = 0; i < iters; ++i) {
    SMARTLIST_FOREACH(sl, const char *, cp, digestmap_set(dm, cp, (void*)1));
  }
  tor_gettimeofday(&pt2);
  for (i = 0; i < iters; ++i) {
    SMARTLIST_FOREACH(sl, const char *, cp, digestmap_get(dm, cp));
    SMARTLIST_FOREACH(sl2, const char *, cp, digestmap_get(dm, cp));
  }
  tor_gettimeofday(&pt3);
  for (i = 0; i < iters; ++i) {
    SMARTLIST_FOREACH(sl, const char *, cp, digestset_add(ds, cp));
  }
  tor_gettimeofday(&pt4);
  for (i = 0; i < iters; ++i) {
    SMARTLIST_FOREACH(sl, const char *, cp, n += digestset_isin(ds, cp));
    SMARTLIST_FOREACH(sl2, const char *, cp, n += digestset_isin(ds, cp));
  }
  tor_gettimeofday(&end);

  for (i = 0; i < fpostests; ++i) {
    crypto_rand(d, 20);
    if (digestset_isin(ds, d)) ++fp;
  }

  printf("%ld\n",(unsigned long)tv_udiff(&start, &pt2));
  printf("%ld\n",(unsigned long)tv_udiff(&pt2, &pt3));
  printf("%ld\n",(unsigned long)tv_udiff(&pt3, &pt4));
  printf("%ld\n",(unsigned long)tv_udiff(&pt4, &end));
  printf("-- %d\n", n);
  printf("++ %f\n", fp/(double)fpostests);
  digestmap_free(dm, NULL);
  digestset_free(ds);
  SMARTLIST_FOREACH(sl, char *, cp, tor_free(cp));
  SMARTLIST_FOREACH(sl2, char *, cp, tor_free(cp));
  smartlist_free(sl);
  smartlist_free(sl2);
}

typedef void (*bench_fn)(void);

typedef struct benchmark_t {
  const char *name;
  bench_fn fn;
  int enabled;
} benchmark_t;

#define ENT(s) { #s , bench_##s, 0 }

static struct benchmark_t benchmarks[] = {
  ENT(dmap),
  ENT(aes),
  ENT(cell_aes),
  {NULL,NULL,0}
};

static benchmark_t *
find_benchmark(const char *name)
{
  benchmark_t *b;
  for (b = benchmarks; b->name; ++b) {
    if (!strcmp(name, b->name)) {
      return b;
    }
  }
  return NULL;
}

/** Main entry point for benchmark code: parse the command line, and run
 * some benchmarks. */
int
main(int argc, const char **argv)
{
  int i;
  int list=0, n_enabled=0;
  benchmark_t *b;

  tor_threads_init();

  for (i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--list")) {
      list = 1;
    } else {
      benchmark_t *b = find_benchmark(argv[i]);
      ++n_enabled;
      if (b) {
        b->enabled = 1;
      } else {
        printf("No such benchmark as %s\n", argv[i]);
      }
    }
  }

  reset_perftime();

  for (b = benchmarks; b->name; ++b) {
    if (b->enabled || n_enabled == 0) {
      printf("===== %s =====\n", b->name);
      if (!list)
        b->fn();
    }
  }

  return 0;
}

