/* Never include this file directly.  Include <linux/compiler.h> instead.  */

/*
 * These definitions are for Ueber-GCC: always newer than the latest
 * version and hence sporting everything plus a kitchen-sink.
 */
#include <linux/compiler-gcc.h>

#define inline			__inline__ __attribute__((always_inline))
#define __inline__		__inline__ __attribute__((always_inline))
#define __inline		__inline__ __attribute__((always_inline))
#define __deprecated		__attribute__((deprecated))
#define __attribute_used__	__attribute__((__used__))
#define __attribute_pure__	__attribute__((pure))
