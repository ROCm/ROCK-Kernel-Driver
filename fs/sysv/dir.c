/*
 *  linux/fs/sysv/dir.c
 *
 *  minix/dir.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  coh/dir.c
 *  Copyright (C) 1993  Pascal Haible, Bruno Haible
 *
 *  sysv/dir.c
 *  Copyright (C) 1993  Bruno Haible
 *
 *  SystemV/Coherent directory handling functions
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/sysv_fs.h>
#include <linux/stat.h>
#include <linux/string.h>

static int sysv_readdir(struct file *, void *, filldir_t);

struct file_operations sysv_dir_operations = {
	read:		generic_read_dir,
	readdir:	sysv_readdir,
	fsync:		file_fsync,
};

static int sysv_readdir(struct file * filp, void * dirent, filldir_t filldir)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct super_block * sb = inode->i_sb;
	unsigned int offset,i;
	struct buffer_head * bh;
	char* bh_data;
	struct sysv_dir_entry * de, sde;

	if ((unsigned long)(filp->f_pos) % SYSV_DIRSIZE)
		return -EBADF;
	while (filp->f_pos < inode->i_size) {
		offset = filp->f_pos & sb->sv_block_size_1;
		bh = sysv_file_bread(inode, filp->f_pos >> sb->sv_block_size_bits, 0);
		if (!bh) {
			filp->f_pos += sb->sv_block_size - offset;
			continue;
		}
		bh_data = bh->b_data;
		while (offset < sb->sv_block_size && filp->f_pos < inode->i_size) {
			de = (struct sysv_dir_entry *) (offset + bh_data);
			if (de->inode) {
				/* Copy the directory entry first, because the directory
				 * might be modified while we sleep in filldir()...
				 */
				memcpy(&sde, de, sizeof(struct sysv_dir_entry));

				if (sde.inode > inode->i_sb->sv_ninodes)
					printk("sysv_readdir: Bad inode number on dev "
					       "%s, ino %ld, offset 0x%04lx: %d is out of range\n",
					       kdevname(inode->i_dev),
					       inode->i_ino, (off_t) filp->f_pos, sde.inode);

				i = strnlen(sde.name, SYSV_NAMELEN);
				if (filldir(dirent, sde.name, i, filp->f_pos, sde.inode, DT_UNKNOWN) < 0) {
					brelse(bh);
					return 0;
				}
			}
			offset += SYSV_DIRSIZE;
			filp->f_pos += SYSV_DIRSIZE;
		}
		brelse(bh);
	}
	return 0;
}
