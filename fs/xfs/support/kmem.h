/*
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
#ifndef __XFS_SUPPORT_KMEM_H__
#define __XFS_SUPPORT_KMEM_H__

#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

/*
 * Cutoff point to use vmalloc instead of kmalloc.
 */
#define MAX_SLAB_SIZE	0x10000

/*
 * XFS uses slightly different names for these due to the
 * IRIX heritage.
 */
#define	kmem_zone	kmem_cache_s
#define kmem_zone_t	kmem_cache_t

#define KM_SLEEP	0x0001
#define KM_NOSLEEP	0x0002
#define KM_NOFS		0x0004


/*
 * XXX get rid of the unconditional  __GFP_NOFAIL by adding
 * a KM_FAIL flag and using it where we're allowed to fail.
 */
static __inline unsigned int
flag_convert(int flags)
{
#if DEBUG
	if (unlikely(flags & ~(KM_SLEEP|KM_NOSLEEP|KM_NOFS))) {
		printk(KERN_WARNING
		    "XFS: memory allocation with wrong flags (%x)\n", flags);
		BUG();
	}
#endif

	if (flags & KM_NOSLEEP)
		return GFP_ATOMIC;
	/* If we're in a transaction, FS activity is not ok */
	else if ((current->flags & PF_FSTRANS) || (flags & KM_NOFS))
		return GFP_NOFS | __GFP_NOFAIL;
	return GFP_KERNEL | __GFP_NOFAIL;
}

static __inline void *
kmem_alloc(size_t size, int flags)
{
	if (unlikely(MAX_SLAB_SIZE < size))
		/* Avoid doing filesystem sensitive stuff to get this */
		return __vmalloc(size, flag_convert(flags), PAGE_KERNEL);
	return kmalloc(size, flag_convert(flags));
}

static __inline void *
kmem_zalloc(size_t size, int flags)
{
	void *ptr = kmem_alloc(size, flags);
	if (likely(ptr != NULL))
		memset(ptr, 0, size);
	return ptr;
}

static __inline void
kmem_free(void *ptr, size_t size)
{
	if (unlikely((unsigned long)ptr < VMALLOC_START ||
		     (unsigned long)ptr >= VMALLOC_END))
		kfree(ptr);
	else
		vfree(ptr);
}

static __inline void *
kmem_realloc(void *ptr, size_t newsize, size_t oldsize, int flags)
{
	void *new = kmem_alloc(newsize, flags);

	if (likely(ptr != NULL)) {
		if (likely(new != NULL))
			memcpy(new, ptr, min(oldsize, newsize));
		kmem_free(ptr, oldsize);
	}

	return new;
}

static __inline kmem_zone_t *
kmem_zone_init(int size, char *zone_name)
{
	return kmem_cache_create(zone_name, size, 0, 0, NULL, NULL);
}

static __inline void *
kmem_zone_alloc(kmem_zone_t *zone, int flags)
{
	return kmem_cache_alloc(zone, flag_convert(flags));
}

static __inline void *
kmem_zone_zalloc(kmem_zone_t *zone, int flags)
{
	void *ptr = kmem_zone_alloc(zone, flags);
	if (likely(ptr != NULL))
		memset(ptr, 0, kmem_cache_size(zone));
	return ptr;
}

static __inline void
kmem_zone_free(kmem_zone_t *zone, void *ptr)
{
	kmem_cache_free(zone, ptr);
}

#endif /* __XFS_SUPPORT_KMEM_H__ */
