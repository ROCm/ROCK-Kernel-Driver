#ifndef __LINUX_COMPILER_H
#define __LINUX_COMPILER_H

/* Somewhere in the middle of the GCC 2.96 development cycle, we implemented
   a mechanism by which the user can annotate likely branch directions and
   expect the blocks to be reordered appropriately.  Define __builtin_expect
   to nothing for earlier compilers.  */

#if __GNUC__ == 2 && __GNUC_MINOR__ < 96
#define __builtin_expect(x, expected_value) (x)
#endif

#define likely(x)	__builtin_expect((x),1)
#define unlikely(x)	__builtin_expect((x),0)

/* This macro obfuscates arithmetic on a variable address so that gcc
   shouldn't recognize the original var, and make assumptions about it */
/*	strcpy(s, "xxx"+X) => memcpy(s, "xxx"+X, 4-X) */
#define RELOC_HIDE(var, off)						\
  ({ __typeof__(&(var)) __ptr;					\
    __asm__ ("" : "=g"(__ptr) : "0"((void *)&(var) + (off)));	\
    *__ptr; })
#endif /* __LINUX_COMPILER_H */
