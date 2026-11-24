/* Stand-in for freerouting-cpp.exe in the plugin's native-mode tests.
 *
 *   x86_64-w64-mingw32-gcc -O2 -static -o freerouting-cpp.exe stub_router.c
 *
 * Prints its arguments ("arg: ..."), then log lines like the real router's.
 * The input file name picks the behaviour:
 *   *slow*  routes "forever" (2 minutes), for the cancel test;
 *   *fail*  writes an empty SES and exits with 1, like a failed real run;
 *   other   writes a small SES and exits with 0.
 */
#include <stdio.h>
#include <string.h>
#include <windows.h>

int main(int argc, char** argv) {
  const char* in = NULL;
  const char* out = NULL;
  for (int i = 1; i < argc; ++i) {
    printf("arg: %s\n", argv[i]);
    if (!strcmp(argv[i], "-de") && i + 1 < argc) in = argv[i + 1];
    if (!strcmp(argv[i], "-do") && i + 1 < argc) out = argv[i + 1];
  }
  if (!in || !out) {
    fprintf(stderr, "usage: freerouting-cpp -de <input.dsn> -do <output.ses>\n");
    return 2;
  }
  FILE* f = fopen(in, "rb");
  if (!f) {
    fprintf(stderr, "cannot read input file '%s'\n", in);
    return 1;
  }
  fclose(f);
  printf("2026-11-24 20:58:01.359 INFO  [STUB01] Auto-routing stage started on board 'x' with baseline score 1.00 for 10 unrouted items.\n");
  fflush(stdout);
  if (strstr(in, "slow")) {
    for (int pass = 1; pass <= 1200; ++pass) {
      Sleep(100);
      printf("2026-11-24 20:58:02.000 INFO  [STUB01] Auto-routing pass #%d on board 'x' was completed in 0.10 seconds with score 900.00 (5 unrouted and 0 violations).\n", pass);
      fflush(stdout);
    }
  }
  if (strstr(in, "fail")) {
    f = fopen(out, "wb");
    if (f) fclose(f);
    fprintf(stderr, "routing failed: stub failure\n");
    return 1;
  }
  printf("2026-11-24 20:58:04.203 INFO  [STUB01] Auto-routing pass #1 on board 'x' was completed in 0.10 seconds with score 1000.00 (0 unrouted and 0 violations).\n");
  printf("2026-11-24 20:58:04.497 INFO  [STUB01] Optimization stage started on board 'x'.\n");
  printf("2026-11-24 20:58:06.272 INFO  [STUB01] Optimizer pass #1: optimizer score 844.95 -> 848.00 (IMPROVED, 0.36%%), router score: 1000.00.\n");
  printf("2026-11-24 20:58:12.354 INFO  [STUB01] Optimization stage completed:.\n");
  f = fopen(out, "wb");
  if (!f) {
    fprintf(stderr, "cannot write '%s'\n", out);
    return 1;
  }
  fputs("(session stub (routes))\n", f);
  fclose(f);
  printf("stub.dsn: 0 unrouted, 0 clearance violations, 0.5 s -> %s (24 bytes)\n", out);
  return 0;
}
