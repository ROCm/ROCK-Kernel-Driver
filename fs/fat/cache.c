/*
 *  linux/fs/fat/cache.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *
 *  Mar 1999. AV. Changed cache, so that it uses the starting cluster instead
 *	of inode number.
 *  May 1999. AV. Fixed the bogosity with FAT32 (read "FAT28"). Fscking lusers.
 */

#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/buffer_head.h>

#if 0
#define debug_pr(fmt, args...)	printk(fmt, ##args)
#else
#define debug_pr(fmt, args...)
#endif

/* this must be > 0. */
#define FAT_MAX_CACHE	8

struct fat_cache {
	struct list_head cache_list;
	int nr_contig;	/* number of contiguous clusters */
	int fcluster;	/* cluster number in the file. */
	int dcluster;	/* cluster number on disk. */
};

static inline int fat_max_cache(struct inode *inode)
{
	return FAT_MAX_CACHE;
}

static kmem_cache_t *fat_cache_cachep;

static void init_once(void *foo, kmem_cache_t *cachep, unsigned long flags)
{
	struct fat_cache *cache = (struct fat_cache *)foo;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR)
		INIT_LIST_HEAD(&cache->cache_list);
}

int __init fat_cache_init(void)
{
	fat_cache_cachep = kmem_cache_create("fat_cache",
				sizeof(struct fat_cache),
				0, SLAB_RECLAIM_ACCOUNT,
				init_once, NULL);
	if (fat_cache_cachep == NULL)
		return -ENOMEM;
	return 0;
}

void __exit fat_cache_destroy(void)
{
	if (kmem_cache_destroy(fat_cache_cachep))
		printk(KERN_INFO "fat_cache: not all structures were freed\n");
}

static inline struct fat_cache *fat_cache_alloc(struct inode *inode)
{
	return kmem_cache_alloc(fat_cache_cachep, SLAB_KERNEL);
}

static inline void fat_cache_free(struct fat_cache *cache)
{
	BUG_ON(!list_empty(&cache->cache_list));
	kmem_cache_free(fat_cache_cachep, cache);
}

static inline void fat_cache_update_lru(struct inode *inode,
					struct fat_cache *cache)
{
	if (MSDOS_I(inode)->cache_lru.next != &cache->cache_list)
		list_move(&cache->cache_list, &MSDOS_I(inode)->cache_lru);
}

static int fat_cache_lookup(struct inode *inode, int fclus,
			    struct fat_cache *cache,
			    int *cached_fclus, int *cached_dclus)
{
	static struct fat_cache nohit = { .fcluster = 0, };

	struct fat_cache *hit = &nohit, *p;
	int offset = -1;

	spin_lock(&MSDOS_I(inode)->cache_lru_lock);
	debug_pr("FAT: %s, fclus %d", __FUNCTION__, fclus);
	list_for_each_entry(p, &MSDOS_I(inode)->cache_lru, cache_list) {
		if (p->fcluster <= fclus && hit->fcluster < p->fcluster) {
			hit = p;
			debug_pr(", fclus %d, dclus %d, cont %d",
				 p->fcluster, p->dcluster, p->nr_contig);
			if ((hit->fcluster + hit->nr_contig) < fclus) {
				offset = hit->nr_contig;
				debug_pr(" (off %d, hit)", offset);
			} else {
				offset = fclus - hit->fcluster;
				debug_pr(" (off %d, full hit)", offset);
				break;
			}
		}
	}
	if (hit != &nohit) {
		fat_cache_update_lru(inode, hit);
		*cache = *hit;
		*cached_fclus = cache->fcluster + offset;
		*cached_dclus = cache->dcluster + offset;
	}
	debug_pr("\n");
	spin_unlock(&MSDOS_I(inode)->cache_lru_lock);

	return offset;
}

static struct fat_cache *fat_cache_merge(struct inode *inode,
					 struct fat_cache *new)
{
	struct fat_cache *p, *hit = NULL;

	list_for_each_entry(p, &MSDOS_I(inode)->cache_lru, cache_list) {
		if (p->fcluster == new->fcluster) {
			BUG_ON(p->dcluster != new->dcluster);
			debug_pr("FAT: %s: merged fclus %d, dclus %d, "
				 "cur cont %d => new cont %d\n", __FUNCTION__,
				 p->fcluster, p->dcluster, p->nr_contig,
				 new->nr_contig);
			if (new->nr_contig > p->nr_contig)
				p->nr_contig = new->nr_contig;
			hit = p;
			break;
		}
	}
	return hit;
}

static void fat_cache_add(struct inode *inode, struct fat_cache *new)
{
	struct fat_cache *cache, *tmp;

	debug_pr("FAT: %s: fclus %d, dclus %d, cont %d\n", __FUNCTION__,
		 new->fcluster, new->dcluster, new->nr_contig);

	if (new->fcluster == -1) /* dummy cache */
		return;

	spin_lock(&MSDOS_I(inode)->cache_lru_lock);
	cache = fat_cache_merge(inode, new);
	if (cache == NULL) {
		if (MSDOS_I(inode)->nr_caches < fat_max_cache(inode)) {
			MSDOS_I(inode)->nr_caches++;
			spin_unlock(&MSDOS_I(inode)->cache_lru_lock);

			tmp = fat_cache_alloc(inode);
			spin_lock(&MSDOS_I(inode)->cache_lru_lock);
			cache = fat_cache_merge(inode, new);
			if (cache != NULL) {
				MSDOS_I(inode)->nr_caches--;
				fat_cache_free(tmp);
				goto out;
			}
			cache = tmp;
		} else {
			struct list_head *p = MSDOS_I(inode)->cache_lru.prev;
			cache = list_entry(p, struct fat_cache, cache_list);
		}
		cache->fcluster = new->fcluster;
		cache->dcluster = new->dcluster;
		cache->nr_contig = new->nr_contig;
	}
out:
	fat_cache_update_lru(inode, cache);

	debug_pr("FAT: ");
	list_for_each_entry(cache, &MSDOS_I(inode)->cache_lru, cache_list) {
		debug_pr("(fclus %d, dclus %d, cont %d), ",
		       cache->fcluster, cache->dcluster, cache->nr_contig);
	}
	debug_pr("\n");
	spin_unlock(&MSDOS_I(inode)->cache_lru_lock);
}

/*
 * Cache invalidation occurs rarely, thus the LRU chain is not updated. It
 * fixes itself after a while.
 */
static void __fat_cache_inval_inode(struct inode *inode)
{
	struct msdos_inode_info *i = MSDOS_I(inode);
	struct fat_cache *cache;
	while (!list_empty(&i->cache_lru)) {
		cache = list_entry(i->cache_lru.next, struct fat_cache, cache_list);
		list_del_init(&cache->cache_list);
		MSDOS_I(inode)->nr_caches--;
		fat_cache_free(cache);
	}
	debug_pr("FAT: %s\n", __FUNCTION__);
}

void fat_cache_inval_inode(struct inode *inode)
{
	spin_lock(&MSDOS_I(inode)->cache_lru_lock);
	__fat_cache_inval_inode(inode);
	spin_unlock(&MSDOS_I(inode)->cache_lru_lock);
}

int __fat_access(struct super_block *sb, int nr, int new_value)
{
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	struct buffer_head *bh, *bh2, *c_bh, *c_bh2;
	unsigned char *p_first, *p_last;
	int copy, first, last, next, b;

	if (sbi->fat_bits == 32) {
		first = last = nr*4;
	} else if (sbi->fat_bits == 16) {
		first = last = nr*2;
	} else {
		first = nr*3/2;
		last = first+1;
	}
	b = sbi->fat_start + (first >> sb->s_blocksize_bits);
	if (!(bh = sb_bread(sb, b))) {
		printk(KERN_ERR "FAT: bread(block %d) in"
		       " fat_access failed\n", b);
		return -EIO;
	}
	if ((first >> sb->s_blocksize_bits) == (last >> sb->s_blocksize_bits)) {
		bh2 = bh;
	} else {
		if (!(bh2 = sb_bread(sb, b + 1))) {
			brelse(bh);
			printk(KERN_ERR "FAT: bread(block %d) in"
			       " fat_access failed\n", b + 1);
			return -EIO;
		}
	}
	if (sbi->fat_bits == 32) {
		p_first = p_last = NULL; /* GCC needs that stuff */
		next = CF_LE_L(((__le32 *) bh->b_data)[(first &
		    (sb->s_blocksize - 1)) >> 2]);
		/* Fscking Microsoft marketing department. Their "32" is 28. */
		next &= 0x0fffffff;
	} else if (sbi->fat_bits == 16) {
		p_first = p_last = NULL; /* GCC needs that stuff */
		next = CF_LE_W(((__le16 *) bh->b_data)[(first &
		    (sb->s_blocksize - 1)) >> 1]);
	} else {
		p_first = &((__u8 *)bh->b_data)[first & (sb->s_blocksize - 1)];
		p_last = &((__u8 *)bh2->b_data)[(first + 1) & (sb->s_blocksize - 1)];
		if (nr & 1)
			next = ((*p_first >> 4) | (*p_last << 4)) & 0xfff;
		else
			next = (*p_first+(*p_last << 8)) & 0xfff;
	}
	if (new_value != -1) {
		if (sbi->fat_bits == 32) {
			((__le32 *)bh->b_data)[(first & (sb->s_blocksize - 1)) >> 2]
				= CT_LE_L(new_value);
		} else if (sbi->fat_bits == 16) {
			((__le16 *)bh->b_data)[(first & (sb->s_blocksize - 1)) >> 1]
				= CT_LE_W(new_value);
		} else {
			if (nr & 1) {
				*p_first = (*p_first & 0xf) | (new_value << 4);
				*p_last = new_value >> 4;
			}
			else {
				*p_first = new_value & 0xff;
				*p_last = (*p_last & 0xf0) | (new_value >> 8);
			}
			mark_buffer_dirty(bh2);
		}
		mark_buffer_dirty(bh);
		for (copy = 1; copy < sbi->fats; copy++) {
			b = sbi->fat_start + (first >> sb->s_blocksize_bits)
				+ sbi->fat_length * copy;
			if (!(c_bh = sb_bread(sb, b)))
				break;
			if (bh != bh2) {
				if (!(c_bh2 = sb_bread(sb, b+1))) {
					brelse(c_bh);
					break;
				}
				memcpy(c_bh2->b_data, bh2->b_data, sb->s_blocksize);
				mark_buffer_dirty(c_bh2);
				brelse(c_bh2);
			}
			memcpy(c_bh->b_data, bh->b_data, sb->s_blocksize);
			mark_buffer_dirty(c_bh);
			brelse(c_bh);
		}
	}
	brelse(bh);
	if (bh != bh2)
		brelse(bh2);
	return next;
}

/* 
 * Returns the this'th FAT entry, -1 if it is an end-of-file entry. If
 * new_value is != -1, that FAT entry is replaced by it.
 */
int fat_access(struct super_block *sb, int nr, int new_value)
{
	int next;

	next = -EIO;
	if (nr < 2 || MSDOS_SB(sb)->clusters + 2 <= nr) {
		fat_fs_panic(sb, "invalid access to FAT (entry 0x%08x)", nr);
		goto out;
	}
	if (new_value == FAT_ENT_EOF)
		new_value = EOF_FAT(sb);

	next = __fat_access(sb, nr, new_value);
	if (next < 0)
		goto out;
	if (next >= BAD_FAT(sb))
		next = FAT_ENT_EOF;
out:
	return next;
}

static inline int cache_contiguous(struct fat_cache *cache, int dclus)
{
	cache->nr_contig++;
	return ((cache->dcluster + cache->nr_contig) == dclus);
}

static inline void cache_init(struct fat_cache *cache, int fclus, int dclus)
{
	cache->fcluster = fclus;
	cache->dcluster = dclus;
	cache->nr_contig = 0;
}

int fat_get_cluster(struct inode *inode, int cluster, int *fclus, int *dclus)
{
	struct super_block *sb = inode->i_sb;
	const int limit = sb->s_maxbytes >> MSDOS_SB(sb)->cluster_bits;
	struct fat_cache cache;
	int nr;

	BUG_ON(MSDOS_I(inode)->i_start == 0);
	
	*fclus = 0;
	*dclus = MSDOS_I(inode)->i_start;
	if (cluster == 0)
		return 0;

	if (fat_cache_lookup(inode, cluster, &cache, fclus, dclus) < 0)
		cache_init(&cache, -1, -1); /* dummy, always not contiguous */

	while (*fclus < cluster) {
		/* prevent the infinite loop of cluster chain */
		if (*fclus > limit) {
			fat_fs_panic(sb, "%s: detected the cluster chain loop"
				     " (i_pos %lld)", __FUNCTION__,
				     MSDOS_I(inode)->i_pos);
			return -EIO;
		}

		nr = fat_access(sb, *dclus, -1);
		if (nr < 0)
 			return nr;
		else if (nr == FAT_ENT_FREE) {
			fat_fs_panic(sb, "%s: invalid cluster chain"
				     " (i_pos %lld)", __FUNCTION__,
				     MSDOS_I(inode)->i_pos);
			return -EIO;
		} else if (nr == FAT_ENT_EOF) {
			fat_cache_add(inode, &cache);
			return FAT_ENT_EOF;
		}
		(*fclus)++;
		*dclus = nr;
		if (!cache_contiguous(&cache, *dclus))
			cache_init(&cache, *fclus, *dclus);
	}
	fat_cache_add(inode, &cache);
	return 0;
}

static int fat_bmap_cluster(struct inode *inode, int cluster)
{
	struct super_block *sb = inode->i_sb;
	int ret, fclus, dclus;

	if (MSDOS_I(inode)->i_start == 0)
		return 0;

	ret = fat_get_cluster(inode, cluster, &fclus, &dclus);
	if (ret < 0)
		return ret;
	else if (ret == FAT_ENT_EOF) {
		fat_fs_panic(sb, "%s: request beyond EOF (i_pos %lld)",
			     __FUNCTION__, MSDOS_I(inode)->i_pos);
		return -EIO;
	}
	return dclus;
}

int fat_bmap(struct inode *inode, sector_t sector, sector_t *phys)
{
	struct super_block *sb = inode->i_sb;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	sector_t last_block;
	int cluster, offset;

	*phys = 0;
	if ((sbi->fat_bits != 32) &&
	    (inode->i_ino == MSDOS_ROOT_INO || (S_ISDIR(inode->i_mode) &&
	     !MSDOS_I(inode)->i_start))) {
		if (sector < (sbi->dir_entries >> sbi->dir_per_block_bits))
			*phys = sector + sbi->dir_start;
		return 0;
	}
	last_block = (MSDOS_I(inode)->mmu_private + (sb->s_blocksize - 1))
		>> sb->s_blocksize_bits;
	if (sector >= last_block)
		return 0;

	cluster = sector >> (sbi->cluster_bits - sb->s_blocksize_bits);
	offset  = sector & (sbi->sec_per_clus - 1);
	cluster = fat_bmap_cluster(inode, cluster);
	if (cluster < 0)
		return cluster;
	else if (cluster) {
		*phys = ((sector_t)cluster - 2) * sbi->sec_per_clus
			+ sbi->data_start + offset;
	}
	return 0;
}

/* Free all clusters after the skip'th cluster. */
int fat_free(struct inode *inode, int skip)
{
	struct super_block *sb = inode->i_sb;
	int nr, ret, fclus, dclus;

	if (MSDOS_I(inode)->i_start == 0)
		return 0;

	if (skip) {
		ret = fat_get_cluster(inode, skip - 1, &fclus, &dclus);
		if (ret < 0)
			return ret;
		else if (ret == FAT_ENT_EOF)
			return 0;

		nr = fat_access(sb, dclus, -1);
		if (nr == FAT_ENT_EOF)
			return 0;
		else if (nr > 0) {
			/*
			 * write a new EOF, and get the remaining cluster
			 * chain for freeing.
			 */
			nr = fat_access(sb, dclus, FAT_ENT_EOF);
		}
		if (nr < 0)
			return nr;

		fat_cache_inval_inode(inode);
	} else {
		fat_cache_inval_inode(inode);

		nr = MSDOS_I(inode)->i_start;
		MSDOS_I(inode)->i_start = 0;
		MSDOS_I(inode)->i_logstart = 0;
		mark_inode_dirty(inode);
	}

	lock_fat(sb);
	do {
		nr = fat_access(sb, nr, FAT_ENT_FREE);
		if (nr < 0)
			goto error;
		else if (nr == FAT_ENT_FREE) {
			fat_fs_panic(sb, "%s: deleting beyond EOF (i_pos %lld)",
				     __FUNCTION__, MSDOS_I(inode)->i_pos);
			nr = -EIO;
			goto error;
		}
		if (MSDOS_SB(sb)->free_clusters != -1)
			MSDOS_SB(sb)->free_clusters++;
		inode->i_blocks -= MSDOS_SB(sb)->cluster_size >> 9;
	} while (nr != FAT_ENT_EOF);
	fat_clusters_flush(sb);
	nr = 0;
error:
	unlock_fat(sb);

	return nr;
}
