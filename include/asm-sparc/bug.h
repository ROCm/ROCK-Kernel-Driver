/* $Id$ */
#ifndef _SPARC_BUG_H
#define _SPARC_BUG_H

#ifdef CONFIG_DEBUG_BUGVERBOSE
extern void do_BUG(const char *file, int line);
#define BUG() do {					\
	do_BUG(__FILE__, __LINE__);			\
	__builtin_trap();				\
} while (0)
#else
#define BUG()		__builtin_trap()
#endif

#define PAGE_BUG(page) do { \
	BUG(); \
} while (0)

#endif
