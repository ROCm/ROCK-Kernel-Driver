#ifndef __LINUX_COMPILER_H
#define __LINUX_COMPILER_H

#ifdef __CHECKER__
  #define __user	__attribute__((address_space(1)))
  #define __kernel	/* default address space */
#else
  #define __user
  #define __kernel
#endif

#if (__GNUC__ > 3) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1)
#define inline		__inline__ __attribute__((always_inline))
#define __inline__	__inline__ __attribute__((always_inline))
#define __inline	__inline__ __attribute__((always_inline))
#endif

/* Somewhere in the middle of the GCC 2.96 development cycle, we implemented
   a mechanism by which the user can annotate likely branch directions and
   expect the blocks to be reordered appropriately.  Define __builtin_expect
   to nothing for earlier compilers.  */

#if __GNUC__ == 2 && __GNUC_MINOR__ < 96
#define __builtin_expect(x, expected_value) (x)
#endif

#define likely(x)	__builtin_expect((x),1)
#define unlikely(x)	__builtin_expect((x),0)

/*
 * Allow us to mark functions as 'deprecated' and have gcc emit a nice
 * warning for each use, in hopes of speeding the functions removal.
 * Usage is:
 * 		int __deprecated foo(void)
 */
#if ( __GNUC__ == 3 && __GNUC_MINOR__ > 0 ) || __GNUC__ > 3
#define __deprecated	__attribute__((deprecated))
#else
#define __deprecated
#endif

/*
 * Allow us to avoid 'defined but not used' warnings on functions and data,
 * as well as force them to be emitted to the assembly file.
 *
 * As of gcc 3.3, static functions that are not marked with attribute((used))
 * may be elided from the assembly file.  As of gcc 3.3, static data not so
 * marked will not be elided, but this may change in a future gcc version.
 *
 * In prior versions of gcc, such functions and data would be emitted, but
 * would be warned about except with attribute((unused)).
 */
#if __GNUC__ == 3 && __GNUC_MINOR__ >= 3 || __GNUC__ > 3
#define __attribute_used__	__attribute__((__used__))
#else
#define __attribute_used__	__attribute__((__unused__))
#endif

/* This macro obfuscates arithmetic on a variable address so that gcc
   shouldn't recognize the original var, and make assumptions about it */
#define RELOC_HIDE(ptr, off)					\
  ({ unsigned long __ptr;					\
    __asm__ ("" : "=g"(__ptr) : "0"(ptr));		\
    (typeof(ptr)) (__ptr + (off)); })
#endif /* __LINUX_COMPILER_H */
