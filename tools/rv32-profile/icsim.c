/* Reads a QEMU "-d in_asm,exec,nochain" log on stdin and simulates an 8 KB
 * instruction cache over the fetch stream between the two sim_marker() calls.
 * Fetches inside [bram_lo, bram_hi) bypass the cache (BRAM).  Usage:
 *   icsim MARKER_ADDR BRAM_LO BRAM_HI SYMS_FILE   (syms: "addr size name" sorted) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define MAXTB (1 << 20)
typedef struct { uint32_t pc; uint32_t n; uint32_t *addr; } tb_t;
static tb_t *tbs; static uint32_t ntb;
static uint32_t hidx[1 << 22];
static tb_t *find(uint32_t pc) { uint32_t h = (pc * 2654435761u) >> 10; for (;;) { h &= (1 << 22) - 1; if (!hidx[h]) return 0; if (tbs[hidx[h]].pc == pc) return &tbs[hidx[h]]; ++h; } }
static void put(uint32_t pc, uint32_t *a, uint32_t n) { uint32_t h = (pc * 2654435761u) >> 10; for (;;) { h &= (1 << 22) - 1; if (!hidx[h] || tbs[hidx[h]].pc == pc) break; ++h; } if (!hidx[h]) hidx[h] = ++ntb; tb_t *t = &tbs[hidx[h]]; t->pc = pc; free(t->addr); t->addr = malloc(n * 4); memcpy(t->addr, a, n * 4); t->n = n; }
#define NCFG 3
static int ways[NCFG] = {1, 2, 4};
static uint32_t tag[NCFG][128][4]; static uint32_t age[NCFG][128][4]; static uint64_t miss[NCFG], tick;
static uint32_t pcnt_lo, pcnt_hi; static uint64_t *pcnt;
static uint32_t nsym; static struct { uint32_t a, s; char name[96]; uint64_t miss2, fetch; } *sym;
static int symof(uint32_t a) { int lo = 0, hi = (int)nsym - 1, r = -1; while (lo <= hi) { int m = (lo + hi) / 2; if (sym[m].a <= a) { r = m; lo = m + 1; } else hi = m - 1; } if (r >= 0 && a < sym[r].a + sym[r].s) return r; return -1; }
int main(int argc, char **argv) {
  uint32_t marker = strtoul(argv[1], 0, 16), blo = strtoul(argv[2], 0, 16), bhi = strtoul(argv[3], 0, 16);
  FILE *sf = fopen(argv[4], "r"); sym = calloc(200000, sizeof *sym);
  while (fscanf(sf, "%x %x %95s", &sym[nsym].a, &sym[nsym].s, sym[nsym].name) == 3) ++nsym;
  tbs = calloc(MAXTB, sizeof *tbs);
  if (argc > 6) { pcnt_lo = strtoul(argv[5], 0, 16); pcnt_hi = strtoul(argv[6], 0, 16); pcnt = calloc((pcnt_hi - pcnt_lo) / 2 + 1, 8); }
  static char line[4096]; static uint32_t cur[4096]; uint32_t ncur = 0, curpc = 0; int inblk = 0, phase = 0;
  uint64_t fetch = 0, bramfetch = 0, lines = 0; uint32_t lastline = ~0u;
  while (fgets(line, sizeof line, stdin)) {
    if (!strncmp(line, "IN:", 3)) { inblk = 1; ncur = 0; continue; }
    if (inblk && line[0] == '0' && line[1] == 'x') { uint32_t a = strtoul(line, 0, 16); if (!ncur) curpc = a; cur[ncur++] = a; continue; }
    if (inblk && (line[0] == '\n' || line[0] == '-')) { if (ncur) put(curpc, cur, ncur); inblk = 0; continue; }
    if (!strncmp(line, "Trace ", 6)) {
      char *s = strchr(line, '['); if (!s) continue; s = strchr(s, '/'); if (!s) continue;
      uint32_t pc = (uint32_t)strtoull(s + 1, 0, 16);
      if (pc == marker) { ++phase; continue; }
      if (phase != 1) continue;
      tb_t *t = find(pc); if (!t) continue;
      for (uint32_t k = 0; k < t->n; ++k) {
        uint32_t a = t->addr[k]; ++fetch;
        int si = symof(a); if (si >= 0) sym[si].fetch++;
        if (pcnt && a >= pcnt_lo && a < pcnt_hi) pcnt[(a - pcnt_lo) >> 1]++;
        if (a >= blo && a < bhi) { ++bramfetch; lastline = ~0u; continue; }
        uint32_t ln = a >> 6; if (ln == lastline) continue; lastline = ln; ++lines;
        ++tick;
        for (int c = 0; c < NCFG; ++c) {
          int nsets = 128 / ways[c]; uint32_t set = ln % nsets; int hit = -1, victim = 0;
          for (int w = 0; w < ways[c]; ++w) { if (tag[c][set][w] == ln + 1) hit = w; if (age[c][set][w] < age[c][set][victim]) victim = w; }
          if (hit >= 0) age[c][set][hit] = tick; else { ++miss[c]; tag[c][set][victim] = ln + 1; age[c][set][victim] = tick; if (c == 1 && si >= 0) sym[si].miss2++; }
        }
      }
    }
  }
  printf("fetches %llu (bram %llu) line-changes %llu\n", (unsigned long long)fetch, (unsigned long long)bramfetch, (unsigned long long)lines);
  for (int c = 0; c < NCFG; ++c) printf("%d-way misses %llu\n", ways[c], (unsigned long long)miss[c]);
  for (uint32_t i = 0; i < nsym; ++i) if (sym[i].fetch * 1000 > fetch) printf("%10llu fetch %6u B %s\n", (unsigned long long)sym[i].fetch, sym[i].s, sym[i].name);
  if (pcnt) { FILE *o = fopen("pcnt.txt", "w"); for (uint32_t a = pcnt_lo; a < pcnt_hi; a += 2) if (pcnt[(a - pcnt_lo) >> 1]) fprintf(o, "%x %llu\n", a, (unsigned long long)pcnt[(a - pcnt_lo) >> 1]); fclose(o); }
  return 0;
}
