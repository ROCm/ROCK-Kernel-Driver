#ifndef __ASM_BUG_H
#define __ASM_BUG_H

#include <asm/break.h>

#define BUG()								\
do {									\
	__asm__ __volatile__("break %0" : : "i" (BRK_BUG));		\
} while (0)
#define BUG_ON(condition) do { if (unlikely((condition)!=0)) BUG(); } while(0)
#define PAGE_BUG(page) do {  BUG(); } while (0)

#define WARN_ON(condition) do { \
	if (unlikely((condition)!=0)) { \
		printk("Badness in %s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__); \
	dump_stack(); \
	} \
} while (0)

#endif
