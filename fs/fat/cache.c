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
		next = CF_LE_L(((__u32 *) bh->b_data)[(first &
		    (sb->s_blocksize - 1)) >> 2]);
		/* Fscking Microsoft marketing department. Their "32" is 28. */
		next &= 0x0fffffff;
	} else if (sbi->fat_bits == 16) {
		p_first = p_last = NULL; /* GCC needs that stuff */
		next = CF_LE_W(((__u16 *) bh->b_data)[(first &
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
			((__u32 *)bh->b_data)[(first & (sb->s_blocksize - 1)) >> 2]
				= CT_LE_L(new_value);
		} else if (sbi->fat_bits == 16) {
			((__u16 *)bh->b_data)[(first & (sb->s_blocksize - 1)) >> 1]
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

void fat_cache_init(struct super_block *sb)
{
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	int count;

	spin_lock_init(&sbi->cache_lock);

	for (count = 0; count < FAT_CACHE_NR - 1; count++) {
		sbi->cache_array[count].start_cluster = 0;
		sbi->cache_array[count].next = &sbi->cache_array[count + 1];
	}
	sbi->cache_array[count].start_cluster = 0;
	sbi->cache_array[count].next = NULL;
	sbi->cache = sbi->cache_array;
}

void fat_cache_lookup(struct inode *inode, int cluster, int *f_clu, int *d_clu)
{
	struct msdos_sb_info *sbi = MSDOS_SB(inode->i_sb);
	struct fat_cache *walk;
	int first;

	BUG_ON(cluster == 0);
	
	first = MSDOS_I(inode)->i_start;
	if (!first)
		return;

	spin_lock(&sbi->cache_lock);

	if (MSDOS_I(inode)->disk_cluster &&
	    MSDOS_I(inode)->file_cluster <= cluster) {
		*d_clu = MSDOS_I(inode)->disk_cluster;
		*f_clu = MSDOS_I(inode)->file_cluster;
	}

	for (walk = sbi->cache; walk; walk = walk->next) {
		if (walk->start_cluster == first
		    && walk->file_cluster <= cluster
		    && walk->file_cluster > *f_clu) {
			*d_clu = walk->disk_cluster;
			*f_clu = walk->file_cluster;
#ifdef DEBUG
			printk("cache hit: %d (%d)\n", *f_clu, *d_clu);
#endif
			if (*f_clu == cluster)
				goto out;
		}
	}
#ifdef DEBUG
	printk("cache miss\n");
#endif
out:
	spin_unlock(&sbi->cache_lock);
}

#ifdef DEBUG
static void list_cache(struct super_block *sb)
{
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	struct fat_cache *walk;

	for (walk = sbi->cache; walk; walk = walk->next) {
		if (walk->start_cluster)
			printk("<%s,%d>(%d,%d) ", sb->s_id,
			       walk->start_cluster, walk->file_cluster,
			       walk->disk_cluster);
		else
			printk("-- ");
	}
	printk("\n");
}
#endif

/*
 * Cache invalidation occurs rarely, thus the LRU chain is not updated. It
 * fixes itself after a while.
 */
static void __fat_cache_inval_inode(struct inode *inode)
{
	struct fat_cache *walk;
	int first = MSDOS_I(inode)->i_start;
	MSDOS_I(inode)->file_cluster = MSDOS_I(inode)->disk_cluster = 0;
	for (walk = MSDOS_SB(inode->i_sb)->cache; walk; walk = walk->next)
		if (walk->start_cluster == first)
			walk->start_cluster = 0;
}

void fat_cache_inval_inode(struct inode *inode)
{
	struct msdos_sb_info *sbi = MSDOS_SB(inode->i_sb);
	spin_lock(&sbi->cache_lock);
	__fat_cache_inval_inode(inode);
	spin_unlock(&sbi->cache_lock);
}

void fat_cache_add(struct inode *inode, int f_clu, int d_clu)
{
	struct msdos_sb_info *sbi = MSDOS_SB(inode->i_sb);
	struct fat_cache *walk, *last;
	int first, prev_f_clu, prev_d_clu;

	if (f_clu == 0)
		return;
	first = MSDOS_I(inode)->i_start;
	if (!first)
		return;

	last = NULL;
	spin_lock(&sbi->cache_lock);

	if (MSDOS_I(inode)->file_cluster == f_clu)
		goto out;
	else {
		prev_f_clu = MSDOS_I(inode)->file_cluster;
		prev_d_clu = MSDOS_I(inode)->disk_cluster;
		MSDOS_I(inode)->file_cluster = f_clu;
		MSDOS_I(inode)->disk_cluster = d_clu;
		if (prev_f_clu == 0)
			goto out;
		f_clu = prev_f_clu;
		d_clu = prev_d_clu;
	}
	
	for (walk = sbi->cache; walk->next; walk = (last = walk)->next) {
		if (walk->start_cluster == first &&
		    walk->file_cluster == f_clu) {
			if (walk->disk_cluster != d_clu) {
				printk(KERN_ERR "FAT: cache corruption "
				       "(i_pos %lld)\n", MSDOS_I(inode)->i_pos);
				__fat_cache_inval_inode(inode);
				goto out;
			}
			if (last == NULL)
				goto out;

			/* update LRU */
			last->next = walk->next;
			walk->next = sbi->cache;
			sbi->cache = walk;
#ifdef DEBUG
			list_cache();
#endif
			goto out;
		}
	}
	walk->start_cluster = first;
	walk->file_cluster = f_clu;
	walk->disk_cluster = d_clu;
	last->next = NULL;
	walk->next = sbi->cache;
	sbi->cache = walk;
#ifdef DEBUG
	list_cache();
#endif
out:
	spin_unlock(&sbi->cache_lock);
}

int fat_get_cluster(struct inode *inode, int cluster, int *fclus, int *dclus)
{
	struct super_block *sb = inode->i_sb;
	const int limit = sb->s_maxbytes >> MSDOS_SB(sb)->cluster_bits;
	int nr;

	BUG_ON(MSDOS_I(inode)->i_start == 0);
	
	*fclus = 0;
	*dclus = MSDOS_I(inode)->i_start;
	if (cluster == 0)
		return 0;

	fat_cache_lookup(inode, cluster, fclus, dclus);
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
			fat_cache_add(inode, *fclus, *dclus);
			return FAT_ENT_EOF;
		}
		(*fclus)++;
		*dclus = nr;
	}
	fat_cache_add(inode, *fclus, *dclus);
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
