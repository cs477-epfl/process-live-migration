/*
 * This file provides hints for the compiler w.r.t. branch and unused variable.
 */
#ifndef UTIL_H
#define UTIL_H

#define DEBUG

/// Use-space logging
#ifdef DEBUG
  #define PRINTF(fmt, ...)  printf(fmt, ##__VA_ARGS__)
  #define FPRINTF(dest, fmt, ...) fprintf(dest, fmt, ##__VA_ARGS__)
#else
  #define PRINTF(fmt, ...)
  #define FPRINTF(dest, fmt, ...)
#endif

/// Kernel logging
/*
#ifdef DEBUG
  #define PRINTK(fmt, ...) printk(fmt, ##__VA_ARGS__)
#else
  #define PRINTK(fmt, ...)
#endif
*/

/// A condition is likely to be true.
#undef likely
#ifdef __GNUC__
  #define likely(x) __builtin_expect(!!(x), 1)
#else
  #define likely(x) (x)
#endif

/// A condition is unlikely to be true.
#undef unlikely
#ifdef __GNUC__
  #define unlikely(x) __builtin_expect(!!(x), 0)
#else
  #define unlikely(x) (x)
#endif

/// A variable may be defined but not used.
#undef unused
#ifdef __GNUC__
  #define unused(x) x __attribute__((unused))
#else
  #define unused(x) x
  #warning "Unused attribute is not supported."
#endif

#endif
