#ifndef _PPC64_BUG_H
#define _PPC64_BUG_H

#include <linux/config.h>

/*
 * Define an illegal instr to trap on the bug.
 * We don't use 0 because that marks the end of a function
 * in the ELF ABI.  That's "Boo Boo" in case you wonder...
 */
#define BUG_OPCODE .long 0x00b00b00  /* For asm */
#define BUG_ILLEGAL_INSTR "0x00b00b00" /* For BUG macro */

#ifndef __ASSEMBLY__

#ifdef CONFIG_XMON
struct pt_regs;
extern void xmon(struct pt_regs *excp);
#define BUG() do { \
	printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
	xmon(0); \
} while (0)
#else
#define BUG() do { \
	printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
	__asm__ __volatile__(".long " BUG_ILLEGAL_INSTR); \
} while (0)
#endif

#define BUG_ON(condition) do { if (unlikely((condition)!=0)) BUG(); } while(0)

#define PAGE_BUG(page) do { BUG(); } while (0)

#define WARN_ON(condition) do { \
	if (unlikely((condition)!=0)) { \
		printk("Badness in %s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__); \
		dump_stack(); \
	} \
} while (0)

#endif
#endif
