/*
 * include/asm-v850/bug.h -- Bug reporting
 *
 *  Copyright (C) 2003  NEC Electronics Corporation
 *  Copyright (C) 2003  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_BUG_H__
#define __V850_BUG_H__

extern void __bug (void) __attribute__ ((noreturn));
#define BUG()		__bug()
#define PAGE_BUG(page)	__bug()

#define BUG_ON(condition) do { if (unlikely((condition)!=0)) BUG(); } while(0)

#define WARN_ON(condition) do { \
	if (unlikely((condition)!=0)) { \
		printk("Badness in %s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__); \
		dump_stack(); \
	} \
} while (0)

#endif /* __V850_BUG_H__ */
