#ifndef _ALPHA_BUG_H
#define _ALPHA_BUG_H

#include <asm/pal.h>

/* ??? Would be nice to use .gprel32 here, but we can't be sure that the
   function loaded the GP, so this could fail in modules.  */
#define BUG() \
  __asm__ __volatile__("call_pal %0  # bugchk\n\t"".long %1\n\t.8byte %2" \
		       : : "i" (PAL_bugchk), "i"(__LINE__), "i"(__FILE__))

#define BUG_ON(condition) do { if (unlikely((condition)!=0)) BUG(); } while(0)

#define PAGE_BUG(page)	BUG()

#define WARN_ON(condition) do { \
	if (unlikely((condition)!=0)) { \
		printk("Badness in %s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__); \
		dump_stack(); \
	} \
} while (0)

#endif
