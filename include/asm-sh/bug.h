#ifndef __ASM_SH_BUG_H
#define __ASM_SH_BUG_H

/*
 * Tell the user there is some problem.
 */
#define BUG() do { \
	printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
	asm volatile("nop"); \
} while (0)

#define PAGE_BUG(page) do { \
	BUG(); \
} while (0)

#endif
