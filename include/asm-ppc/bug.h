#ifndef _PPC_BUG_H
#define _PPC_BUG_H

#include <linux/config.h>
#include <asm/xmon.h>

#ifdef CONFIG_XMON
extern void xmon(struct pt_regs *);
#define BUG() do { \
	printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
	xmon(0); \
} while (0)
#else
#define BUG() do { \
	printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
	__asm__ __volatile__(".long 0x0"); \
} while (0)
#endif
#define PAGE_BUG(page) do { BUG(); } while (0)

#endif
