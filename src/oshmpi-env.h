/* Parsing of SHMEM_SYMMETRIC_SIZE and its legacy aliases.  Kept free of MPI so it can be
 * tested on its own. */

#ifndef OSHMPI_ENV_H
#define OSHMPI_ENV_H

#include <ctype.h>
#include <limits.h>
#include <stddef.h>

/* Parse a heap size as specified for SHMEM_SYMMETRIC_SIZE: a non-negative integer or
 * floating-point number (".5" and "5." are allowed) with an optional scaling suffix of k, m,
 * g or t in either case, meaning powers of 1024.  Only one suffix is recognized and anything
 * after it is ignored, so "20kk" is 20 KiB.  The result is the ceiling of number * scale.
 * Returns 0 and sets *bytes on success, -1 if the string is not a valid size (empty, a sign,
 * junk where the number or the suffix should be, or too large for a heap). */
static inline int oshmpi_parse_size(const char * s, long * bytes)
{
    if (s == NULL) return -1;
    while (isspace((unsigned char) *s)) s++;

    /* Exact arithmetic: the digits as an integer 'num' and a power of ten 'den', so that
     * 1.25k is exactly 1280 and not 1280.0000001 rounded up.  Fraction digits beyond what
     * fits are dropped, but remembered so that the ceiling is still right. */
    unsigned __int128 num = 0, den = 1;
    int digits = 0, dropped = 0;
    for (; isdigit((unsigned char) *s); s++, digits++) {
        if (num > ((unsigned __int128) 1 << 100)) return -1;
        num = num * 10 + (unsigned) (*s - '0');
    }
    if (*s == '.') {
        for (s++; isdigit((unsigned char) *s); s++, digits++) {
            if (den < ((unsigned __int128) 1 << 60)) { num = num * 10 + (unsigned) (*s - '0'); den *= 10; }
            else if (*s != '0') dropped = 1;
        }
    }
    if (digits == 0) return -1;

    unsigned shift = 0;
    switch (*s) {
        case 'k': case 'K': shift = 10; break;
        case 'm': case 'M': shift = 20; break;
        case 'g': case 'G': shift = 30; break;
        case 't': case 'T': shift = 40; break;
        case '\0':          break;
        default:            return -1;
    }

    /* Keep the result within what a heap can be (and a long can hold) with room to spare. */
    unsigned __int128 scaled = num << shift;                 /* num < 2^101, shift <= 40 */
    unsigned __int128 r = scaled / den + (scaled % den != 0 || dropped);
    if (r > ((unsigned __int128) 1 << 60)) return -1;
    *bytes = (long) r;
    return 0;
}

#endif /* OSHMPI_ENV_H */
