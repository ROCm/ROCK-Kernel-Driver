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
#include <linux/exportfs.h>
#include <linux/module.h>
#include "tmem.h"

static int precache_auto_allocate; /* set to 1 to auto_allocate */

union precache_filekey {
	struct tmem_oid oid;
	u32 fh[0];
};

/*
 * If the filesystem uses exportable filehandles, use the filehandle as
 * the key, else use the inode number.
 */
static int precache_get_key(struct inode *inode, union precache_filekey *key)
{
#define PRECACHE_KEY_MAX (sizeof(key->oid) / sizeof(*key->fh))
	struct super_block *sb = inode->i_sb;

	memset(key, 0, sizeof(key));
	if (sb->s_export_op) {
		int (*fhfn)(struct dentry *, __u32 *fh, int *, int);

		fhfn = sb->s_export_op->encode_fh;
		if (fhfn) {
			struct dentry *d;
			int ret, maxlen = PRECACHE_KEY_MAX;

			d = list_first_entry(&inode->i_dentry,
					     struct dentry, d_alias);
			ret = fhfn(d, key->fh, &maxlen, 0);
			if (ret < 0)
				return ret;
			if (ret >= 255 || maxlen > PRECACHE_KEY_MAX)
				return -EPERM;
			if (maxlen > 0)
				return 0;
		}
	}
	key->oid.oid[0] = inode->i_ino;
	key->oid.oid[1] = inode->i_generation;
	return 0;
#undef PRECACHE_KEY_MAX
}

int precache_put(struct address_space *mapping, unsigned long index,
		 struct page *page)
{
	u32 tmem_pool = mapping->host->i_sb->precache_poolid;
	union precache_filekey key;
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
		pr_info("Mapping superblock for s_id=%s to precache_id=%d\n",
			mapping->host->i_sb->s_id, tmem_pool);
		mapping->host->i_sb->precache_poolid = tmem_pool;
	}
	if (ind != index || precache_get_key(mapping->host, &key))
		return 0;
	mb(); /* ensure page is quiescent; tmem may address it with an alias */
	return tmem_put_page(tmem_pool, key.oid, ind, mfn);
}

int precache_get(struct address_space *mapping, unsigned long index,
		 struct page *empty_page)
{
	u32 tmem_pool = mapping->host->i_sb->precache_poolid;
	union precache_filekey key;
	u32 ind = (u32) index;
	unsigned long mfn = pfn_to_mfn(page_to_pfn(empty_page));

	if ((s32)tmem_pool < 0)
		return 0;
	if (ind != index || precache_get_key(mapping->host, &key))
		return 0;

	return tmem_get_page(tmem_pool, key.oid, ind, mfn);
}
EXPORT_SYMBOL(precache_get);

int precache_flush(struct address_space *mapping, unsigned long index)
{
	u32 tmem_pool = mapping->host->i_sb->precache_poolid;
	union precache_filekey key;
	u32 ind = (u32) index;

	if ((s32)tmem_pool < 0)
		return 0;
	if (ind != index || precache_get_key(mapping->host, &key))
		return 0;

	return tmem_flush_page(tmem_pool, key.oid, ind);
}
EXPORT_SYMBOL(precache_flush);

int precache_flush_inode(struct address_space *mapping)
{
	u32 tmem_pool = mapping->host->i_sb->precache_poolid;
	union precache_filekey key;

	if ((s32)tmem_pool < 0 || precache_get_key(mapping->host, &key))
		return 0;

	return tmem_flush_object(tmem_pool, key.oid);
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
	pr_info("Unmapping superblock for s_id=%s from precache_id=%d\n",
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
