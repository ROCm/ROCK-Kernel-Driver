#ifndef _ASM_IA64_BUG_H
#define _ASM_IA64_BUG_H

#if (__GNUC__ > 3) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1)
# define ia64_abort()	__builtin_trap()
#else
# define ia64_abort()	(*(volatile int *) 0 = 0)
#endif
#define BUG() do { printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); ia64_abort(); } while (0)

#define BUG_ON(condition) do { if (unlikely((condition)!=0)) BUG(); } while(0)

#define PAGE_BUG(page) do { BUG(); } while (0)

#define WARN_ON(condition) do { \
	if (unlikely((condition)!=0)) { \
		printk("Badness in %s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__); \
		dump_stack(); \
	} \
} while (0)

#endif
