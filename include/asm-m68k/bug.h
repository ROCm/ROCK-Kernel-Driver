#ifndef _M68K_BUG_H
#define _M68K_BUG_H

#include <linux/config.h>

#ifdef CONFIG_DEBUG_BUGVERBOSE
#ifndef CONFIG_SUN3
#define BUG() do { \
	printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
	asm volatile("illegal"); \
} while (0)
#else
#define BUG() do { \
	printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
	panic("BUG!"); \
} while (0)
#endif
#else
#define BUG() do { \
	asm volatile("illegal"); \
} while (0)
#endif

#define BUG_ON(condition) do { \
	if (unlikely((condition)!=0)) \
		BUG(); \
} while(0)

#define PAGE_BUG(page) do { \
	BUG(); \
} while (0)

#define WARN_ON(condition) do { \
	if (unlikely((condition)!=0)) { \
		printk("Badness in %s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__); \
		dump_stack(); \
	} \
} while (0)

#endif
