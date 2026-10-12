/* SHMEM_SYMMETRIC_SIZE parser, from the specification's rules: k/m/g/t in either case are
 * powers of 1024, floating-point values are allowed, only one suffix is recognized and the
 * rest is ignored, the result is the ceiling, and anything else is invalid.  No MPI needed. */
#include <stdio.h>
#include "../src/oshmpi-env.h"

static int errs;

static void ok(const char * s, long want)
{
    long got = -7;
    int rc = oshmpi_parse_size(s, &got);
    if (rc != 0 || got != want) { printf("FAIL \"%s\": rc=%d got=%ld want=%ld\n", s, rc, got, want); errs++; }
}

static void bad(const char * s)
{
    long got = -7;
    if (oshmpi_parse_size(s, &got) == 0) { printf("FAIL \"%s\": accepted as %ld\n", s, got); errs++; }
}

int main(void)
{
    ok("0", 0);
    ok("1", 1);
    ok("4096", 4096);
    ok("  12", 12);
    ok("1k", 1024);          ok("1K", 1024);
    ok("1m", 1L << 20);      ok("1M", 1L << 20);
    ok("1g", 1L << 30);      ok("1G", 1L << 30);
    ok("1t", 1L << 40);      ok("1T", 1L << 40);
    ok("20m", 20971520);                    /* the specification's example */
    ok("3.1M", 3250586);                    /* ... and this one (3250585.6, rounded up) */
    ok(".5m", 524288);
    ok("0.5m", 524288);
    ok("5.", 5);
    ok("5.k", 5120);
    ok("1.5G", 1610612736L);
    ok("2.5", 3);                           /* ceiling of a plain byte count */
    ok("0.1", 1);
    ok("20kk", 20480);                      /* the second k is ignored */
    ok("20m junk", 20971520);
    ok("100000000", 100000000L);
    ok("1000G", 1000L << 30);
    ok("1000T", 1000L << 40);
    ok("1.25k", 1280);
    ok("0.75m", 786432);
    ok("2.5g", 2684354560L);
    ok("0.3k", 308);
    ok("1.0000000000000000000000001", 2);   /* a digit past the precision still rounds up */
    ok("123456789012", 123456789012L);

    bad("");
    bad("   ");
    bad("k");
    bad(".");
    bad(".m");
    bad("-1");
    bad("+1");
    bad("abc");
    bad("1x");
    bad("1 m");                             /* space before the suffix */
    bad("1.2.3");
    bad("1e3");
    bad("0x10");
    bad("99999999999999999999999999");      /* overflow */
    bad("2000000T");                        /* larger than a heap can be */
    bad("1.5.m");

    long keep = 42;
    (void) oshmpi_parse_size("x", &keep);
    if (keep != 42) { printf("FAIL: result modified on error\n"); errs++; }
    if (oshmpi_parse_size(NULL, &keep) == 0) { printf("FAIL: NULL accepted\n"); errs++; }

    printf("test_env_parse: %s\n", errs ? "FAILED" : "OK");
    return errs != 0;
}
