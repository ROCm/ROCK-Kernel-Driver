#ifndef _ASMARM_BUG_H
#define _ASMARM_BUG_H

#include <linux/config.h>

#ifdef CONFIG_DEBUG_BUGVERBOSE
extern void __bug(const char *file, int line, void *data);

/* give file/line information */
#define BUG()		__bug(__FILE__, __LINE__, NULL)
#define PAGE_BUG(page)	__bug(__FILE__, __LINE__, page)

#else

/* these just cause an oops */
#define BUG()		(*(int *)0 = 0)
#define PAGE_BUG(page)	(*(int *)0 = 0)

#endif

#endif
