/*
 *  linux/fs/ext2/dir.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/dir.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext2 directory handling functions
 *
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 */

#include <linux/fs.h>
#include <linux/ext2_fs.h>

static unsigned char ext2_filetype_table[] = {
	DT_UNKNOWN, DT_REG, DT_DIR, DT_CHR, DT_BLK, DT_FIFO, DT_SOCK, DT_LNK
};

static int ext2_readdir(struct file *, void *, filldir_t);

struct file_operations ext2_dir_operations = {
	read:		generic_read_dir,
	readdir:	ext2_readdir,
	ioctl:		ext2_ioctl,
	fsync:		ext2_sync_file,
};

int ext2_check_dir_entry (const char * function, struct inode * dir,
			  struct ext2_dir_entry_2 * de,
			  struct buffer_head * bh,
			  unsigned long offset)
{
	const char * error_msg = NULL;

	if (le16_to_cpu(de->rec_len) < EXT2_DIR_REC_LEN(1))
		error_msg = "rec_len is smaller than minimal";
	else if (le16_to_cpu(de->rec_len) % 4 != 0)
		error_msg = "rec_len % 4 != 0";
	else if (le16_to_cpu(de->rec_len) < EXT2_DIR_REC_LEN(de->name_len))
		error_msg = "rec_len is too small for name_len";
	else if (dir && ((char *) de - bh->b_data) + le16_to_cpu(de->rec_len) >
		 dir->i_sb->s_blocksize)
		error_msg = "directory entry across blocks";
	else if (dir && le32_to_cpu(de->inode) > le32_to_cpu(dir->i_sb->u.ext2_sb.s_es->s_inodes_count))
		error_msg = "inode out of bounds";

	if (error_msg != NULL)
		ext2_error (dir->i_sb, function, "bad entry in directory #%lu: %s - "
			    "offset=%lu, inode=%lu, rec_len=%d, name_len=%d",
			    dir->i_ino, error_msg, offset,
			    (unsigned long) le32_to_cpu(de->inode),
			    le16_to_cpu(de->rec_len), de->name_len);
	return error_msg == NULL ? 1 : 0;
}

static int ext2_readdir(struct file * filp,
			 void * dirent, filldir_t filldir)
{
	int error = 0;
	unsigned long offset, blk;
	int i, num, stored;
	struct buffer_head * bh, * tmp, * bha[16];
	struct ext2_dir_entry_2 * de;
	struct super_block * sb;
	int err;
	struct inode *inode = filp->f_dentry->d_inode;

	sb = inode->i_sb;

	stored = 0;
	bh = NULL;
	offset = filp->f_pos & (sb->s_blocksize - 1);

	while (!error && !stored && filp->f_pos < inode->i_size) {
		blk = (filp->f_pos) >> EXT2_BLOCK_SIZE_BITS(sb);
		bh = ext2_bread (inode, blk, 0, &err);
		if (!bh) {
			ext2_error (sb, "ext2_readdir",
				    "directory #%lu contains a hole at offset %lu",
				    inode->i_ino, (unsigned long)filp->f_pos);
			filp->f_pos += sb->s_blocksize - offset;
			continue;
		}

		/*
		 * Do the readahead
		 */
		if (!offset) {
			for (i = 16 >> (EXT2_BLOCK_SIZE_BITS(sb) - 9), num = 0;
			     i > 0; i--) {
				tmp = ext2_getblk (inode, ++blk, 0, &err);
				if (tmp && !buffer_uptodate(tmp) && !buffer_locked(tmp))
					bha[num++] = tmp;
				else
					brelse (tmp);
			}
			if (num) {
				ll_rw_block (READA, num, bha);
				for (i = 0; i < num; i++)
					brelse (bha[i]);
			}
		}
		
revalidate:
		/* If the dir block has changed since the last call to
		 * readdir(2), then we might be pointing to an invalid
		 * dirent right now.  Scan from the start of the block
		 * to make sure. */
		if (filp->f_version != inode->i_version) {
			for (i = 0; i < sb->s_blocksize && i < offset; ) {
				de = (struct ext2_dir_entry_2 *) 
					(bh->b_data + i);
				/* It's too expensive to do a full
				 * dirent test each time round this
				 * loop, but we do have to test at
				 * least that it is non-zero.  A
				 * failure will be detected in the
				 * dirent test below. */
				if (le16_to_cpu(de->rec_len) < EXT2_DIR_REC_LEN(1))
					break;
				i += le16_to_cpu(de->rec_len);
			}
			offset = i;
			filp->f_pos = (filp->f_pos & ~(sb->s_blocksize - 1))
				| offset;
			filp->f_version = inode->i_version;
		}
		
		while (!error && filp->f_pos < inode->i_size 
		       && offset < sb->s_blocksize) {
			de = (struct ext2_dir_entry_2 *) (bh->b_data + offset);
			if (!ext2_check_dir_entry ("ext2_readdir", inode, de,
						   bh, offset)) {
				/* On error, skip the f_pos to the
                                   next block. */
				filp->f_pos = (filp->f_pos | (sb->s_blocksize - 1))
					      + 1;
				brelse (bh);
				return stored;
			}
			offset += le16_to_cpu(de->rec_len);
			if (le32_to_cpu(de->inode)) {
				/* We might block in the next section
				 * if the data destination is
				 * currently swapped out.  So, use a
				 * version stamp to detect whether or
				 * not the directory has been modified
				 * during the copy operation.
				 */
				unsigned long version = filp->f_version;
				unsigned char d_type = DT_UNKNOWN;

				if (EXT2_HAS_INCOMPAT_FEATURE(sb, EXT2_FEATURE_INCOMPAT_FILETYPE)
				    && de->file_type < EXT2_FT_MAX)
					d_type = ext2_filetype_table[de->file_type];
				error = filldir(dirent, de->name,
						de->name_len,
						filp->f_pos, le32_to_cpu(de->inode),
						d_type);
				if (error)
					break;
				if (version != filp->f_version)
					goto revalidate;
				stored ++;
			}
			filp->f_pos += le16_to_cpu(de->rec_len);
		}
		offset = 0;
		brelse (bh);
	}
	UPDATE_ATIME(inode);
	return 0;
}
