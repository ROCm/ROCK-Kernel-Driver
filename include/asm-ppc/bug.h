#ifndef _PPC_BUG_H
#define _PPC_BUG_H

#define BUG() do { \
	printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
	__asm__ __volatile__(".long 0x0"); \
} while (0)

#define PAGE_BUG(page) do { BUG(); } while (0)

#endif
