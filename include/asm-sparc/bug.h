/* $Id$ */
#ifndef _SPARC_BUG_H
#define _SPARC_BUG_H

/*
 * XXX I am hitting compiler bugs with __builtin_trap. This has
 * hit me before and rusty was blaming his netfilter bugs on
 * this so lets disable it. - Anton
 */
#if 0
/* We need the mb()'s so we don't trigger a compiler bug - Anton */
#define BUG() do { \
	mb(); \
	__builtin_trap(); \
	mb(); \
} while(0)
#else
#define BUG() do { \
	printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); *(int *)0=0; \
} while (0)
#endif

#define PAGE_BUG(page)	BUG()

#endif
