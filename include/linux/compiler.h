#ifndef __LINUX_COMPILER_H
#define __LINUX_COMPILER_H

#ifdef __CHECKER__
# define __user		__attribute__((noderef, address_space(1)))
# define __kernel	/* default address space */
#else
# define __user
# define __kernel
#endif

#ifndef __ASSEMBLY__
#if __GNUC__ > 3
# include <linux/compiler-gcc+.h>	/* catch-all for GCC 4, 5, etc. */
#elif __GNUC__ == 3
# include <linux/compiler-gcc3.h>
#elif __GNUC__ == 2
# include <linux/compiler-gcc2.h>
#else
# error Sorry, your compiler is too old/not recognized.
#endif
#endif

/* Intel compiler defines __GNUC__. So we will overwrite implementations
 * coming from above header files here
 */
#ifdef __INTEL_COMPILER
# include <linux/compiler-intel.h>
#endif

/*
 * Generic compiler-dependent macros required for kernel
 * build go below this comment. Actual compiler/compiler version
 * specific implementations come from the above header files
 */

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

/*
 * Allow us to mark functions as 'deprecated' and have gcc emit a nice
 * warning for each use, in hopes of speeding the functions removal.
 * Usage is:
 * 		int __deprecated foo(void)
 */
#ifndef __deprecated
# define __deprecated		/* unimplemented */
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
#ifndef __attribute_used__
# define __attribute_used__	/* unimplemented */
#endif

/*
 * From the GCC manual:
 *
 * Many functions have no effects except the return value and their
 * return value depends only on the parameters and/or global
 * variables.  Such a function can be subject to common subexpression
 * elimination and loop optimization just as an arithmetic operator
 * would be.
 * [...]
 */
#ifndef __attribute_pure__
# define __attribute_pure__	/* unimplemented */
#endif

/* Optimization barrier */
#ifndef barrier
# define barrier() __memory_barrier()
#endif

#ifndef RELOC_HIDE
# define RELOC_HIDE(ptr, off)					\
  ({ unsigned long __ptr;					\
     __ptr = (unsigned long) (ptr);				\
    (typeof(ptr)) (__ptr + (off)); })
#endif

#endif /* __LINUX_COMPILER_H */
