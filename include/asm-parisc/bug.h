#ifndef _PARISC_BUG_H
#define _PARISC_BUG_H

/*
 * Tell the user there is some problem.
 */
#define BUG() do { \
	extern void dump_stack(void); \
	printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
	dump_stack(); \
} while (0)

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
