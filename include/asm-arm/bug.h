#ifndef _ASMARM_BUG_H
#define _ASMARM_BUG_H

#include <linux/config.h>

#ifdef CONFIG_DEBUG_BUGVERBOSE
extern volatile void __bug(const char *file, int line, void *data);

/* give file/line information */
#define BUG()		__bug(__FILE__, __LINE__, NULL)
#define PAGE_BUG(page)	__bug(__FILE__, __LINE__, page)

#else

/* these just cause an oops */
#define BUG()		(*(int *)0 = 0)
#define PAGE_BUG(page)	(*(int *)0 = 0)

#endif

#define BUG_ON(condition) do { if (unlikely((condition)!=0)) BUG(); } while(0)

#define WARN_ON(condition) do { \
	if (unlikely((condition)!=0)) { \
		printk("Badness in %s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__); \
		dump_stack(); \
	} \
} while (0)

#endif
