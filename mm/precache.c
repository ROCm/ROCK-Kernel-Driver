/*
 * linux/mm/precache.c
 *
 * Implements "precache" for filesystems/pagecache on top of transcendent
 * memory ("tmem") API.  A filesystem creates an "ephemeral tmem pool"
 * and retains the returned pool_id in its superblock.  Clean pages evicted
 * from pagecache may be "put" into the pool and associated with a "handle"
 * consisting of the pool_id, an object (inode) id, and an index (page offset).
 * Note that the page is copied to tmem; no kernel mappings are changed.
 * If the page is later needed, the filesystem (or VFS) issues a "get", passing
 * the same handle and an empty pageframe.  If successful, the page is copied
 * into the pageframe and a disk read is avoided.  But since the tmem pool
 * is of indeterminate size, a "put" page has indeterminate longevity
 * ("ephemeral"), and the "get" may fail, in which case the filesystem must
 * read the page from disk as before.  Note that the filesystem/pagecache are
 * responsible for maintaining coherency between the pagecache, precache,
 * and the disk, for which "flush page" and "flush object" actions are
 * provided.  And when a filesystem is unmounted, it must "destroy" the pool.
 *
 * Two types of pools may be created for a precache: "private" or "shared".
 * For a private pool, a successful "get" always flushes, implementing
 * exclusive semantics; for a "shared" pool (which is intended for use by
 * co-resident nodes of a cluster filesystem), the "flush" is not guaranteed.
 * In either case, a failed "duplicate" put (overwrite) always guarantee
 * the old data is flushed.
 *
 * Note also that multiple accesses to a tmem pool may be concurrent and any
 * ordering must be guaranteed by the caller.
 *
 * Copyright (C) 2008,2009 Dan Magenheimer, Oracle Corp.
 */

#include <linux/precache.h>
#include <linux/module.h>
#include "tmem.h"

static int precache_auto_allocate; /* set to 1 to auto_allocate */

int precache_put(struct address_space *mapping, unsigned long index,
 struct page *page)
{
	u32 tmem_pool = mapping->host->i_sb->precache_poolid;
	u64 obj = (unsigned long) mapping->host->i_ino;
	u32 ind = (u32) index;
	unsigned long mfn = pfn_to_mfn(page_to_pfn(page));
	int ret;

	if ((s32)tmem_pool < 0) {
		if (!precache_auto_allocate)
			return 0;
		/* a put on a non-existent precache may auto-allocate one */
		ret = tmem_new_pool(0, 0, 0);
		if (ret < 0)
			return 0;
		printk(KERN_INFO
			"Mapping superblock for s_id=%s to precache_id=%d\n",
			mapping->host->i_sb->s_id, tmem_pool);
		mapping->host->i_sb->precache_poolid = tmem_pool;
	}
	if (ind != index)
		return 0;
	mb(); /* ensure page is quiescent; tmem may address it with an alias */
	return tmem_put_page(tmem_pool, obj, ind, mfn);
}

int precache_get(struct address_space *mapping, unsigned long index,
 struct page *empty_page)
{
	u32 tmem_pool = mapping->host->i_sb->precache_poolid;
	u64 obj = (unsigned long) mapping->host->i_ino;
	u32 ind = (u32) index;
	unsigned long mfn = pfn_to_mfn(page_to_pfn(empty_page));

	if ((s32)tmem_pool < 0)
		return 0;
	if (ind != index)
		return 0;

	return tmem_get_page(tmem_pool, obj, ind, mfn);
}
EXPORT_SYMBOL(precache_get);

int precache_flush(struct address_space *mapping, unsigned long index)
{
	u32 tmem_pool = mapping->host->i_sb->precache_poolid;
	u64 obj = (unsigned long) mapping->host->i_ino;
	u32 ind = (u32) index;

	if ((s32)tmem_pool < 0)
		return 0;
	if (ind != index)
		return 0;

	return tmem_flush_page(tmem_pool, obj, ind);
}
EXPORT_SYMBOL(precache_flush);

int precache_flush_inode(struct address_space *mapping)
{
	u32 tmem_pool = mapping->host->i_sb->precache_poolid;
	u64 obj = (unsigned long) mapping->host->i_ino;

	if ((s32)tmem_pool < 0)
		return 0;

	return tmem_flush_object(tmem_pool, obj);
}
EXPORT_SYMBOL(precache_flush_inode);

int precache_flush_filesystem(struct super_block *sb)
{
	u32 tmem_pool = sb->precache_poolid;
	int ret;

	if ((s32)tmem_pool < 0)
		return 0;
	ret = tmem_destroy_pool(tmem_pool);
	if (!ret)
		return 0;
	printk(KERN_INFO
		"Unmapping superblock for s_id=%s from precache_id=%d\n",
		sb->s_id, ret);
	sb->precache_poolid = 0;
	return 1;
}
EXPORT_SYMBOL(precache_flush_filesystem);

void precache_init(struct super_block *sb)
{
	sb->precache_poolid = tmem_new_pool(0, 0, 0);
}
EXPORT_SYMBOL(precache_init);

void shared_precache_init(struct super_block *sb, char *uuid)
{
	u64 uuid_lo = *(u64 *)uuid;
	u64 uuid_hi = *(u64 *)(&uuid[8]);
	sb->precache_poolid = tmem_new_pool(uuid_lo, uuid_hi, TMEM_POOL_SHARED);
}
EXPORT_SYMBOL(shared_precache_init);
