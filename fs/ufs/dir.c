/*
 *  linux/fs/ufs/ufs_dir.c
 *
 * Copyright (C) 1996
 * Adrian Rodriguez (adrian@franklins-tower.rutgers.edu)
 * Laboratory for Computer Science Research Computing Facility
 * Rutgers, The State University of New Jersey
 *
 * swab support by Francois-Rene Rideau <fare@tunes.org> 19970406
 *
 * 4.4BSD (FreeBSD) support added on February 1st 1998 by
 * Niels Kristian Bech Jensen <nkbj@image.dk> partially based
 * on code by Martin von Loewis <martin@mira.isdn.cs.tu-berlin.de>.
 */

#include <linux/fs.h>
#include <linux/ufs_fs.h>

#include "swab.h"
#include "util.h"

#undef UFS_DIR_DEBUG

#ifdef UFS_DIR_DEBUG
#define UFSD(x) printk("(%s, %d), %s: ", __FILE__, __LINE__, __FUNCTION__); printk x;
#else
#define UFSD(x)
#endif

/*
 * This is blatantly stolen from ext2fs
 */
static int
ufs_readdir (struct file * filp, void * dirent, filldir_t filldir)
{
	struct inode *inode = filp->f_dentry->d_inode;
	int error = 0;
	unsigned long offset, lblk, blk;
	int i, stored;
	struct buffer_head * bh;
	struct ufs_dir_entry * de;
	struct super_block * sb;
	int de_reclen;
	unsigned flags, swab;

	sb = inode->i_sb;
	swab = sb->u.ufs_sb.s_swab;
	flags = sb->u.ufs_sb.s_flags;

	UFSD(("ENTER, ino %lu  f_pos %lu\n", inode->i_ino, (unsigned long) filp->f_pos))

	stored = 0;
	bh = NULL;
	offset = filp->f_pos & (sb->s_blocksize - 1);

	while (!error && !stored && filp->f_pos < inode->i_size) {
		lblk = (filp->f_pos) >> sb->s_blocksize_bits;
		blk = ufs_frag_map(inode, lblk);
		if (!blk || !(bh = bread (sb->s_dev, blk, sb->s_blocksize))) {
			/* XXX - error - skip to the next block */
			printk("ufs_readdir: "
			       "dir inode %lu has a hole at offset %lu\n",
			       inode->i_ino, (unsigned long int)filp->f_pos);
			filp->f_pos += sb->s_blocksize - offset;
			continue;
		}

revalidate:
		/* If the dir block has changed since the last call to
		 * readdir(2), then we might be pointing to an invalid
		 * dirent right now.  Scan from the start of the block
		 * to make sure. */
		if (filp->f_version != inode->i_version) {
			for (i = 0; i < sb->s_blocksize && i < offset; ) {
				de = (struct ufs_dir_entry *)(bh->b_data + i);
				/* It's too expensive to do a full
				 * dirent test each time round this
				 * loop, but we do have to test at
				 * least that it is non-zero.  A
				 * failure will be detected in the
				 * dirent test below. */
				de_reclen = SWAB16(de->d_reclen);
				if (de_reclen < 1)
					break;
				i += de_reclen;
			}
			offset = i;
			filp->f_pos = (filp->f_pos & ~(sb->s_blocksize - 1))
				| offset;
			filp->f_version = inode->i_version;
		}

		while (!error && filp->f_pos < inode->i_size
		       && offset < sb->s_blocksize) {
			de = (struct ufs_dir_entry *) (bh->b_data + offset);
			/* XXX - put in a real ufs_check_dir_entry() */
			if ((de->d_reclen == 0) || (ufs_get_de_namlen(de) == 0)) {
			/* SWAB16() was unneeded -- compare to 0 */
				filp->f_pos = (filp->f_pos &
				              (sb->s_blocksize - 1)) +
				               sb->s_blocksize;
				brelse(bh);
				return stored;
			}
			if (!ufs_check_dir_entry ("ufs_readdir", inode, de,
						   bh, offset)) {
				/* On error, skip the f_pos to the
				   next block. */
				filp->f_pos = (filp->f_pos |
				              (sb->s_blocksize - 1)) +
					       1;
				brelse (bh);
				return stored;
			}
			offset += SWAB16(de->d_reclen);
			if (de->d_ino) {
			/* SWAB16() was unneeded -- compare to 0 */
				/* We might block in the next section
				 * if the data destination is
				 * currently swapped out.  So, use a
				 * version stamp to detect whether or
				 * not the directory has been modified
				 * during the copy operation. */
				unsigned long version = filp->f_version;
				unsigned char d_type = DT_UNKNOWN;

				UFSD(("filldir(%s,%u)\n", de->d_name, SWAB32(de->d_ino)))
				UFSD(("namlen %u\n", ufs_get_de_namlen(de)))
				if ((flags & UFS_DE_MASK) == UFS_DE_44BSD)
					d_type = de->d_u.d_44.d_type;
				error = filldir(dirent, de->d_name, ufs_get_de_namlen(de),
						filp->f_pos, SWAB32(de->d_ino), d_type);
				if (error)
					break;
				if (version != filp->f_version)
					goto revalidate;
				stored ++;
			}
			filp->f_pos += SWAB16(de->d_reclen);
		}
		offset = 0;
		brelse (bh);
	}
	UPDATE_ATIME(inode);
	return 0;
}

int ufs_check_dir_entry (const char * function,	struct inode * dir,
	struct ufs_dir_entry * de, struct buffer_head * bh, 
	unsigned long offset)
{
	struct super_block * sb;
	const char * error_msg;
	unsigned flags, swab;
	
	sb = dir->i_sb;
	flags = sb->u.ufs_sb.s_flags;
	swab = sb->u.ufs_sb.s_swab;
	error_msg = NULL;
			
	if (SWAB16(de->d_reclen) < UFS_DIR_REC_LEN(1))
		error_msg = "reclen is smaller than minimal";
	else if (SWAB16(de->d_reclen) % 4 != 0)
		error_msg = "reclen % 4 != 0";
	else if (SWAB16(de->d_reclen) < UFS_DIR_REC_LEN(ufs_get_de_namlen(de)))
		error_msg = "reclen is too small for namlen";
	else if (dir && ((char *) de - bh->b_data) + SWAB16(de->d_reclen) >
		 dir->i_sb->s_blocksize)
		error_msg = "directory entry across blocks";
	else if (dir && SWAB32(de->d_ino) > (sb->u.ufs_sb.s_uspi->s_ipg * sb->u.ufs_sb.s_uspi->s_ncg))
		error_msg = "inode out of bounds";

	if (error_msg != NULL)
		ufs_error (sb, function, "bad entry in directory #%lu, size %Lu: %s - "
			    "offset=%lu, inode=%lu, reclen=%d, namlen=%d",
			    dir->i_ino, dir->i_size, error_msg, offset,
			    (unsigned long) SWAB32(de->d_ino),
			    SWAB16(de->d_reclen), ufs_get_de_namlen(de));
	
	return (error_msg == NULL ? 1 : 0);
}

struct file_operations ufs_dir_operations = {
	read:		generic_read_dir,
	readdir:	ufs_readdir,
	fsync:		file_fsync,
};
