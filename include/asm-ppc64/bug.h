#ifndef _PPC64_BUG_H
#define _PPC64_BUG_H

#include <linux/config.h>

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

#define PAGE_BUG(page) do { BUG(); } while (0)

#endif
