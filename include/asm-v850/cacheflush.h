/*
 * include/asm-v850/cacheflush.h
 *
 *  Copyright (C) 2001,02  NEC Corporation
 *  Copyright (C) 2001,02  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_CACHEFLUSH_H__
#define __V850_CACHEFLUSH_H__

/* Somebody depends on this; sigh...  */
#include <linux/mm.h>

#include <asm/setup.h>
#include <asm/machdep.h>


#ifndef flush_cache_all
/* If there's no flush_cache_all macro defined by <asm/machdep.h>, then
   this processor has no cache, so just define these as nops.  */

#define flush_cache_all()			((void)0)
#define flush_cache_mm(mm)			((void)0)
#define flush_cache_range(vma, start, end)	((void)0)
#define flush_cache_page(vma, vmaddr)		((void)0)
#define flush_dcache_page(page)			((void)0)
#define flush_icache()				((void)0)
#define flush_icache_range(start, end)		((void)0)
#define flush_icache_page(vma,pg)		((void)0)
#define flush_icache_user_range(vma,pg,adr,len)	((void)0)
#define flush_cache_sigtramp(vaddr)		((void)0)

#endif /* !flush_cache_all */

#endif /* __V850_CACHEFLUSH_H__ */
