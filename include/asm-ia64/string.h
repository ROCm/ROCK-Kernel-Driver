#ifndef _ASM_IA64_STRING_H
#define _ASM_IA64_STRING_H

/*
 * Here is where we want to put optimized versions of the string
 * routines.
 *
 * Copyright (C) 1998-2000, 2002 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/config.h>	/* remove this once we remove the A-step workaround... */

#define __HAVE_ARCH_STRLEN	1 /* see arch/ia64/lib/strlen.S */
#define __HAVE_ARCH_MEMSET	1 /* see arch/ia64/lib/memset.S */
#define __HAVE_ARCH_MEMCPY	1 /* see arch/ia64/lib/memcpy.S */
#define __HAVE_ARCH_BCOPY	1 /* see arch/ia64/lib/memcpy.S */

extern __kernel_size_t strlen (const char *);
extern void *memcpy (void *, const void *, __kernel_size_t);

extern void *__memset_generic (void *, int, __kernel_size_t);
extern void __bzero (void *, __kernel_size_t);

#define memset(s, c, count)				\
({							\
	void *_s = (s);					\
	int _c = (c);					\
	__kernel_size_t _count = (count);		\
							\
	if (__builtin_constant_p(_c) && _c == 0)	\
		__bzero(_s, _count);			\
	else						\
		__memset_generic(_s, _c, _count);	\
})

#endif /* _ASM_IA64_STRING_H */
