/*
 *  linux/fs/ext3/ialloc.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  BSD ufs-inspired inode and directory allocation by
 *  Stephen Tweedie (sct@redhat.com), 1993
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 */

#include <linux/time.h>
#include <linux/fs.h>
#include <linux/jbd.h>
#include <linux/ext3_fs.h>
#include <linux/ext3_jbd.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/quotaops.h>
#include <linux/buffer_head.h>

#include <asm/bitops.h>
#include <asm/byteorder.h>

/*
 * ialloc.c contains the inodes allocation and deallocation routines
 */

/*
 * The free inodes are managed by bitmaps.  A file system contains several
 * blocks groups.  Each group contains 1 bitmap block for blocks, 1 bitmap
 * block for inodes, N blocks for the inode table and data blocks.
 *
 * The file system contains group descriptors which are located after the
 * super block.  Each descriptor contains the number of the bitmap block and
 * the free blocks count in the block.
 */


/*
 * Read the inode allocation bitmap for a given block_group, reading
 * into the specified slot in the superblock's bitmap cache.
 *
 * Return buffer_head of bitmap on success or NULL.
 */
static struct buffer_head *
read_inode_bitmap(struct super_block * sb, unsigned long block_group)
{
	struct ext3_group_desc *desc;
	struct buffer_head *bh = NULL;

	desc = ext3_get_group_desc(sb, block_group, NULL);
	if (!desc)
		goto error_out;

	bh = sb_bread(sb, le32_to_cpu(desc->bg_inode_bitmap));
	if (!bh)
		ext3_error(sb, "read_inode_bitmap",
			    "Cannot read inode bitmap - "
			    "block_group = %lu, inode_bitmap = %lu",
			    block_group, (unsigned long) desc->bg_inode_bitmap);
error_out:
	return bh;
}

/*
 * NOTE! When we get the inode, we're the only people
 * that have access to it, and as such there are no
 * race conditions we have to worry about. The inode
 * is not on the hash-lists, and it cannot be reached
 * through the filesystem because the directory entry
 * has been deleted earlier.
 *
 * HOWEVER: we must make sure that we get no aliases,
 * which means that we have to call "clear_inode()"
 * _before_ we mark the inode not in use in the inode
 * bitmaps. Otherwise a newly created file might use
 * the same inode number (not actually the same pointer
 * though), and then we'd have two inodes sharing the
 * same inode number and space on the harddisk.
 */
void ext3_free_inode (handle_t *handle, struct inode * inode)
{
	struct super_block * sb = inode->i_sb;
	int is_directory;
	unsigned long ino;
	struct buffer_head *bitmap_bh = NULL;
	struct buffer_head *bh2;
	unsigned long block_group;
	unsigned long bit;
	struct ext3_group_desc * gdp;
	struct ext3_super_block * es;
	int fatal = 0, err;

	if (atomic_read(&inode->i_count) > 1) {
		printk ("ext3_free_inode: inode has count=%d\n",
					atomic_read(&inode->i_count));
		return;
	}
	if (inode->i_nlink) {
		printk ("ext3_free_inode: inode has nlink=%d\n",
			inode->i_nlink);
		return;
	}
	if (!sb) {
		printk("ext3_free_inode: inode on nonexistent device\n");
		return;
	}

	ino = inode->i_ino;
	ext3_debug ("freeing inode %lu\n", ino);

	/*
	 * Note: we must free any quota before locking the superblock,
	 * as writing the quota to disk may need the lock as well.
	 */
	DQUOT_INIT(inode);
	DQUOT_FREE_INODE(inode);
	DQUOT_DROP(inode);

	is_directory = S_ISDIR(inode->i_mode);

	/* Do this BEFORE marking the inode not in use or returning an error */
	clear_inode (inode);

	lock_super (sb);
	es = EXT3_SB(sb)->s_es;
	if (ino < EXT3_FIRST_INO(sb) || ino > le32_to_cpu(es->s_inodes_count)) {
		ext3_error (sb, "ext3_free_inode",
			    "reserved or nonexistent inode %lu", ino);
		goto error_return;
	}
	block_group = (ino - 1) / EXT3_INODES_PER_GROUP(sb);
	bit = (ino - 1) % EXT3_INODES_PER_GROUP(sb);
	bitmap_bh = read_inode_bitmap(sb, block_group);
	if (!bitmap_bh)
		goto error_return;

	BUFFER_TRACE(bitmap_bh, "get_write_access");
	fatal = ext3_journal_get_write_access(handle, bitmap_bh);
	if (fatal)
		goto error_return;

	/* Ok, now we can actually update the inode bitmaps.. */
	if (!ext3_clear_bit(bit, bitmap_bh->b_data))
		ext3_error (sb, "ext3_free_inode",
			      "bit already cleared for inode %lu", ino);
	else {
		gdp = ext3_get_group_desc (sb, block_group, &bh2);

		BUFFER_TRACE(bh2, "get_write_access");
		fatal = ext3_journal_get_write_access(handle, bh2);
		if (fatal) goto error_return;

		BUFFER_TRACE(EXT3_SB(sb)->s_sbh, "get write access");
		fatal = ext3_journal_get_write_access(handle, EXT3_SB(sb)->s_sbh);
		if (fatal) goto error_return;

		if (gdp) {
			gdp->bg_free_inodes_count = cpu_to_le16(
				le16_to_cpu(gdp->bg_free_inodes_count) + 1);
			if (is_directory)
				gdp->bg_used_dirs_count = cpu_to_le16(
				  le16_to_cpu(gdp->bg_used_dirs_count) - 1);
		}
		BUFFER_TRACE(bh2, "call ext3_journal_dirty_metadata");
		err = ext3_journal_dirty_metadata(handle, bh2);
		if (!fatal) fatal = err;
		es->s_free_inodes_count =
			cpu_to_le32(le32_to_cpu(es->s_free_inodes_count) + 1);
		BUFFER_TRACE(EXT3_SB(sb)->s_sbh,
					"call ext3_journal_dirty_metadata");
		err = ext3_journal_dirty_metadata(handle, EXT3_SB(sb)->s_sbh);
		if (!fatal) fatal = err;
	}
	BUFFER_TRACE(bitmap_bh, "call ext3_journal_dirty_metadata");
	err = ext3_journal_dirty_metadata(handle, bitmap_bh);
	if (!fatal)
		fatal = err;
	sb->s_dirt = 1;
error_return:
	brelse(bitmap_bh);
	ext3_std_error(sb, fatal);
	unlock_super(sb);
}

/*
 * There are two policies for allocating an inode.  If the new inode is
 * a directory, then a forward search is made for a block group with both
 * free space and a low directory-to-inode ratio; if that fails, then of
 * the groups with above-average free space, that group with the fewest
 * directories already is chosen.
 *
 * For other inodes, search forward from the parent directory's block
 * group to find a free inode.
 */
struct inode *ext3_new_inode(handle_t *handle, struct inode * dir, int mode)
{
	struct super_block *sb;
	struct buffer_head *bitmap_bh = NULL;
	struct buffer_head *bh2;
	int i, j, avefreei;
	struct inode * inode;
	struct ext3_group_desc * gdp;
	struct ext3_group_desc * tmp;
	struct ext3_super_block * es;
	struct ext3_inode_info *ei;
	int err = 0;
	struct inode *ret;

	/* Cannot create files in a deleted directory */
	if (!dir || !dir->i_nlink)
		return ERR_PTR(-EPERM);

	sb = dir->i_sb;
	inode = new_inode(sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	ei = EXT3_I(inode);

	lock_super (sb);
	es = EXT3_SB(sb)->s_es;
repeat:
	gdp = NULL;
	i = 0;

	if (S_ISDIR(mode)) {
		avefreei = le32_to_cpu(es->s_free_inodes_count) /
			EXT3_SB(sb)->s_groups_count;
		if (!gdp) {
			for (j = 0; j < EXT3_SB(sb)->s_groups_count; j++) {
				struct buffer_head *temp_buffer;
				tmp = ext3_get_group_desc (sb, j, &temp_buffer);
				if (tmp &&
				    le16_to_cpu(tmp->bg_free_inodes_count) &&
				    le16_to_cpu(tmp->bg_free_inodes_count) >=
							avefreei) {
					if (!gdp || (le16_to_cpu(tmp->bg_free_blocks_count) >
						le16_to_cpu(gdp->bg_free_blocks_count))) {
						i = j;
						gdp = tmp;
						bh2 = temp_buffer;
					}
				}
			}
		}
	} else {
		/*
		 * Try to place the inode in its parent directory
		 */
		i = EXT3_I(dir)->i_block_group;
		tmp = ext3_get_group_desc (sb, i, &bh2);
		if (tmp && le16_to_cpu(tmp->bg_free_inodes_count))
			gdp = tmp;
		else
		{
			/*
			 * Use a quadratic hash to find a group with a
			 * free inode
			 */
			for (j = 1; j < EXT3_SB(sb)->s_groups_count; j <<= 1) {
				i += j;
				if (i >= EXT3_SB(sb)->s_groups_count)
					i -= EXT3_SB(sb)->s_groups_count;
				tmp = ext3_get_group_desc (sb, i, &bh2);
				if (tmp &&
				    le16_to_cpu(tmp->bg_free_inodes_count)) {
					gdp = tmp;
					break;
				}
			}
		}
		if (!gdp) {
			/*
			 * That failed: try linear search for a free inode
			 */
			i = EXT3_I(dir)->i_block_group + 1;
			for (j = 2; j < EXT3_SB(sb)->s_groups_count; j++) {
				if (++i >= EXT3_SB(sb)->s_groups_count)
					i = 0;
				tmp = ext3_get_group_desc (sb, i, &bh2);
				if (tmp &&
				    le16_to_cpu(tmp->bg_free_inodes_count)) {
					gdp = tmp;
					break;
				}
			}
		}
	}

	err = -ENOSPC;
	if (!gdp)
		goto out;

	err = -EIO;
	brelse(bitmap_bh);
	bitmap_bh = read_inode_bitmap(sb, i);
	if (!bitmap_bh)
		goto fail;

	if ((j = ext3_find_first_zero_bit((unsigned long *)bitmap_bh->b_data,
				      EXT3_INODES_PER_GROUP(sb))) <
	    EXT3_INODES_PER_GROUP(sb)) {
		BUFFER_TRACE(bitmap_bh, "get_write_access");
		err = ext3_journal_get_write_access(handle, bitmap_bh);
		if (err) goto fail;
		
		if (ext3_set_bit(j, bitmap_bh->b_data)) {
			ext3_error (sb, "ext3_new_inode",
				      "bit already set for inode %d", j);
			goto repeat;
		}
		BUFFER_TRACE(bitmap_bh, "call ext3_journal_dirty_metadata");
		err = ext3_journal_dirty_metadata(handle, bitmap_bh);
		if (err) goto fail;
	} else {
		if (le16_to_cpu(gdp->bg_free_inodes_count) != 0) {
			ext3_error (sb, "ext3_new_inode",
				    "Free inodes count corrupted in group %d",
				    i);
			/* Is it really ENOSPC? */
			err = -ENOSPC;
			if (sb->s_flags & MS_RDONLY)
				goto fail;

			BUFFER_TRACE(bh2, "get_write_access");
			err = ext3_journal_get_write_access(handle, bh2);
			if (err) goto fail;
			gdp->bg_free_inodes_count = 0;
			BUFFER_TRACE(bh2, "call ext3_journal_dirty_metadata");
			err = ext3_journal_dirty_metadata(handle, bh2);
			if (err) goto fail;
		}
		goto repeat;
	}
	j += i * EXT3_INODES_PER_GROUP(sb) + 1;
	if (j < EXT3_FIRST_INO(sb) || j > le32_to_cpu(es->s_inodes_count)) {
		ext3_error (sb, "ext3_new_inode",
			    "reserved inode or inode > inodes count - "
			    "block_group = %d,inode=%d", i, j);
		err = -EIO;
		goto fail;
	}

	BUFFER_TRACE(bh2, "get_write_access");
	err = ext3_journal_get_write_access(handle, bh2);
	if (err) goto fail;
	gdp->bg_free_inodes_count =
		cpu_to_le16(le16_to_cpu(gdp->bg_free_inodes_count) - 1);
	if (S_ISDIR(mode))
		gdp->bg_used_dirs_count =
			cpu_to_le16(le16_to_cpu(gdp->bg_used_dirs_count) + 1);
	BUFFER_TRACE(bh2, "call ext3_journal_dirty_metadata");
	err = ext3_journal_dirty_metadata(handle, bh2);
	if (err) goto fail;
	
	BUFFER_TRACE(EXT3_SB(sb)->s_sbh, "get_write_access");
	err = ext3_journal_get_write_access(handle, EXT3_SB(sb)->s_sbh);
	if (err) goto fail;
	es->s_free_inodes_count =
		cpu_to_le32(le32_to_cpu(es->s_free_inodes_count) - 1);
	BUFFER_TRACE(EXT3_SB(sb)->s_sbh, "call ext3_journal_dirty_metadata");
	err = ext3_journal_dirty_metadata(handle, EXT3_SB(sb)->s_sbh);
	sb->s_dirt = 1;
	if (err) goto fail;

	inode->i_uid = current->fsuid;
	if (test_opt (sb, GRPID))
		inode->i_gid = dir->i_gid;
	else if (dir->i_mode & S_ISGID) {
		inode->i_gid = dir->i_gid;
		if (S_ISDIR(mode))
			mode |= S_ISGID;
	} else
		inode->i_gid = current->fsgid;
	inode->i_mode = mode;

	inode->i_ino = j;
	/* This is the optimal IO size (for stat), not the fs block size */
	inode->i_blksize = PAGE_SIZE;
	inode->i_blocks = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;

	memset(ei->i_data, 0, sizeof(ei->i_data));
	ei->i_next_alloc_block = 0;
	ei->i_next_alloc_goal = 0;
	ei->i_dir_start_lookup = 0;
	ei->i_disksize = 0;

	ei->i_flags = EXT3_I(dir)->i_flags & ~EXT3_INDEX_FL;
	if (S_ISLNK(mode))
		ei->i_flags &= ~(EXT3_IMMUTABLE_FL|EXT3_APPEND_FL);
	/* dirsync only applies to directories */
	if (!S_ISDIR(mode))
		ei->i_flags &= ~EXT3_DIRSYNC_FL;
#ifdef EXT3_FRAGMENTS
	ei->i_faddr = 0;
	ei->i_frag_no = 0;
	ei->i_frag_size = 0;
#endif
	ei->i_file_acl = 0;
	ei->i_dir_acl = 0;
	ei->i_dtime = 0;
#ifdef EXT3_PREALLOCATE
	ei->i_prealloc_block = 0;
	ei->i_prealloc_count = 0;
#endif
	ei->i_block_group = i;
	
	if (ei->i_flags & EXT3_SYNC_FL)
		inode->i_flags |= S_SYNC;
	if (ei->i_flags & EXT3_DIRSYNC_FL)
		inode->i_flags |= S_DIRSYNC;
	if (IS_DIRSYNC(inode))
		handle->h_sync = 1;
	insert_inode_hash(inode);
	inode->i_generation = EXT3_SB(sb)->s_next_generation++;

	ei->i_state = EXT3_STATE_NEW;
	err = ext3_mark_inode_dirty(handle, inode);
	if (err) goto fail;
	
	unlock_super(sb);
	ret = inode;
	if(DQUOT_ALLOC_INODE(inode)) {
		DQUOT_DROP(inode);
		inode->i_flags |= S_NOQUOTA;
		inode->i_nlink = 0;
		iput(inode);
		ret = ERR_PTR(-EDQUOT);
	} else {
		ext3_debug("allocating inode %lu\n", inode->i_ino);
	}
	goto really_out;
fail:
	ext3_std_error(sb, err);
out:
	unlock_super(sb);
	iput(inode);
	ret = ERR_PTR(err);
really_out:
	brelse(bitmap_bh);
	return ret;
}

/* Verify that we are loading a valid orphan from disk */
struct inode *ext3_orphan_get (struct super_block * sb, ino_t ino)
{
	ino_t max_ino = le32_to_cpu(EXT3_SB(sb)->s_es->s_inodes_count);
	unsigned long block_group;
	int bit;
	struct buffer_head *bitmap_bh = NULL;
	struct inode *inode = NULL;
	
	/* Error cases - e2fsck has already cleaned up for us */
	if (ino > max_ino) {
		ext3_warning(sb, __FUNCTION__,
			     "bad orphan ino %ld!  e2fsck was run?\n", ino);
		goto out;
	}

	block_group = (ino - 1) / EXT3_INODES_PER_GROUP(sb);
	bit = (ino - 1) % EXT3_INODES_PER_GROUP(sb);
	bitmap_bh = read_inode_bitmap(sb, block_group);
	if (!bitmap_bh) {
		ext3_warning(sb, __FUNCTION__,
			     "inode bitmap error for orphan %ld\n", ino);
		goto out;
	}

	/* Having the inode bit set should be a 100% indicator that this
	 * is a valid orphan (no e2fsck run on fs).  Orphans also include
	 * inodes that were being truncated, so we can't check i_nlink==0.
	 */
	if (!ext3_test_bit(bit, bitmap_bh->b_data) ||
			!(inode = iget(sb, ino)) || is_bad_inode(inode) ||
			NEXT_ORPHAN(inode) > max_ino) {
		ext3_warning(sb, __FUNCTION__,
			     "bad orphan inode %lu!  e2fsck was run?\n", (unsigned long)ino);
		printk(KERN_NOTICE "ext3_test_bit(bit=%d, block=%llu) = %d\n",
		       bit, 
			(unsigned long long)bitmap_bh->b_blocknr, 
			ext3_test_bit(bit, bitmap_bh->b_data));
		printk(KERN_NOTICE "inode=%p\n", inode);
		if (inode) {
			printk(KERN_NOTICE "is_bad_inode(inode)=%d\n",
			       is_bad_inode(inode));
			printk(KERN_NOTICE "NEXT_ORPHAN(inode)=%d\n",
			       NEXT_ORPHAN(inode));
			printk(KERN_NOTICE "max_ino=%ld\n", max_ino);
		}
		/* Avoid freeing blocks if we got a bad deleted inode */
		if (inode && inode->i_nlink == 0)
			inode->i_blocks = 0;
		iput(inode);
		inode = NULL;
	}
out:
	brelse(bitmap_bh);
	return inode;
}

unsigned long ext3_count_free_inodes (struct super_block * sb)
{
#ifdef EXT3FS_DEBUG
	struct ext3_super_block *es;
	unsigned long desc_count, bitmap_count, x;
	struct ext3_group_desc *gdp;
	struct buffer_head *bitmap_bh = NULL;
	int i;

	lock_super (sb);
	es = EXT3_SB(sb)->s_es;
	desc_count = 0;
	bitmap_count = 0;
	gdp = NULL;
	for (i = 0; i < EXT3_SB(sb)->s_groups_count; i++) {
		gdp = ext3_get_group_desc (sb, i, NULL);
		if (!gdp)
			continue;
		desc_count += le16_to_cpu(gdp->bg_free_inodes_count);
		brelse(bitmap_bh);
		bitmap_bh = read_inode_bitmap(sb, i);
		if (!bitmap_bh)
			continue;

		x = ext3_count_free(bitmap_bh, EXT3_INODES_PER_GROUP(sb) / 8);
		printk("group %d: stored = %d, counted = %lu\n",
			i, le16_to_cpu(gdp->bg_free_inodes_count), x);
		bitmap_count += x;
	}
	brelse(bitmap_bh);
	printk("ext3_count_free_inodes: stored = %lu, computed = %lu, %lu\n",
		le32_to_cpu(es->s_free_inodes_count), desc_count, bitmap_count);
	unlock_super(sb);
	return desc_count;
#else
	return le32_to_cpu(EXT3_SB(sb)->s_es->s_free_inodes_count);
#endif
}

#ifdef CONFIG_EXT3_CHECK
/* Called at mount-time, super-block is locked */
void ext3_check_inodes_bitmap (struct super_block * sb)
{
	struct ext3_super_block * es;
	unsigned long desc_count, bitmap_count, x;
	struct buffer_head *bitmap_bh = NULL;
	struct ext3_group_desc * gdp;
	int i;

	es = EXT3_SB(sb)->s_es;
	desc_count = 0;
	bitmap_count = 0;
	gdp = NULL;
	for (i = 0; i < EXT3_SB(sb)->s_groups_count; i++) {
		gdp = ext3_get_group_desc (sb, i, NULL);
		if (!gdp)
			continue;
		desc_count += le16_to_cpu(gdp->bg_free_inodes_count);
		brelse(bitmap_bh);
		bitmap_bh = read_inode_bitmap(sb, i);
		if (!bitmap_bh)
			continue;

		x = ext3_count_free(bitmap_bh, EXT3_INODES_PER_GROUP(sb) / 8);
		if (le16_to_cpu(gdp->bg_free_inodes_count) != x)
			ext3_error (sb, "ext3_check_inodes_bitmap",
				    "Wrong free inodes count in group %d, "
				    "stored = %d, counted = %lu", i,
				    le16_to_cpu(gdp->bg_free_inodes_count), x);
		bitmap_count += x;
	}
	brelse(bitmap_bh);
	if (le32_to_cpu(es->s_free_inodes_count) != bitmap_count)
		ext3_error (sb, "ext3_check_inodes_bitmap",
			    "Wrong free inodes count in super block, "
			    "stored = %lu, counted = %lu",
			    (unsigned long)le32_to_cpu(es->s_free_inodes_count),
			    bitmap_count);
}
#endif
