#ifndef COMPILER_UTILS_H
#define COMPILER_UTILS_H

#if ( defined(__GNUC__) && (__GNUC__ >= 3) ) || defined(__IBMC__) || defined(__INTEL_COMPILER) || defined(__clang__)
#  define unlikely(x_) __builtin_expect(!!(x_),0)
#  define likely(x_)   __builtin_expect(!!(x_),1)
#else
#  define unlikely(x_) (x_)
#  define likely(x_)   (x_)
#endif

/* Intel and GCC pre-4.5 do not support deprecated messages... */
#if ( defined(__GNUC__) && (__GNUC__ >= 3) ) || defined(__IBMC__) || defined(__INTEL_COMPILER) || defined(__clang__)
#  define OSHMPI_DEPRECATED __attribute__((deprecated))
#else
#  define OSHMPI_DEPRECATED
#endif

#endif // COMPILER_UTILS_H
