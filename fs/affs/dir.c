/*
 *  linux/fs/affs/dir.c
 *
 *  (c) 1996  Hans-Joachim Widmaier - Rewritten
 *
 *  (C) 1993  Ray Burr - Modified for Amiga FFS filesystem.
 *
 *  (C) 1992  Eric Youngdale Modified for ISO 9660 filesystem.
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 *
 *  affs directory handling functions
 *
 */

#define DEBUG 0
#include <asm/uaccess.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/affs_fs.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/amigaffs.h>

static int affs_readdir(struct file *, void *, filldir_t);

struct file_operations affs_dir_operations = {
	read:		generic_read_dir,
	readdir:	affs_readdir,
	fsync:		file_fsync,
};

/*
 * directories can handle most operations...
 */
struct inode_operations affs_dir_inode_operations = {
	create:		affs_create,
	lookup:		affs_lookup,
	link:		affs_link,
	unlink:		affs_unlink,
	symlink:	affs_symlink,
	mkdir:		affs_mkdir,
	rmdir:		affs_rmdir,
	rename:		affs_rename,
	setattr:	affs_notify_change,
};

static int
affs_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	int			 j, namelen;
	s32			 i;
	int			 hash_pos;
	int			 chain_pos;
	unsigned long		 ino;
	int			 stored;
	unsigned char		*name;
	struct buffer_head	*dir_bh;
	struct buffer_head	*fh_bh;
	struct inode		*dir;
	struct inode		*inode = filp->f_dentry->d_inode;

	pr_debug("AFFS: readdir(ino=%lu,f_pos=%lu)\n",inode->i_ino,(unsigned long)filp->f_pos);

	stored = 0;
	dir_bh = NULL;
	fh_bh  = NULL;
	dir    = NULL;
	ino    = inode->i_ino;

	if (filp->f_pos == 0) {
		filp->private_data = (void *)0;
		if (filldir(dirent,".",1,filp->f_pos,inode->i_ino,DT_DIR) < 0) {
			return 0;
		}
		++filp->f_pos;
		stored++;
	}
	if (filp->f_pos == 1) {
		if (filldir(dirent,"..",2,filp->f_pos,affs_parent_ino(inode),DT_DIR) < 0) {
			return stored;
		}
		filp->f_pos = 2;
		stored++;
	}
	chain_pos = (filp->f_pos - 2) & 0xffff;
	hash_pos  = (filp->f_pos - 2) >> 16;
	if (chain_pos == 0xffff) {
		affs_warning(inode->i_sb,"readdir","More than 65535 entries in chain");
		chain_pos = 0;
		hash_pos++;
		filp->f_pos = ((hash_pos << 16) | chain_pos) + 2;
	}
	if (!(dir_bh = affs_bread(inode->i_dev,inode->i_ino,
				  AFFS_I2BSIZE(inode))))
		goto readdir_done;

	while (1) {
		while (hash_pos < AFFS_I2HSIZE(inode) &&
		     !((struct dir_front *)dir_bh->b_data)->hashtable[hash_pos])
			hash_pos++;
		if (hash_pos >= AFFS_I2HSIZE(inode))
			break;
		
		i = be32_to_cpu(((struct dir_front *)dir_bh->b_data)->hashtable[hash_pos]);
		j = chain_pos;

		/* If the directory hasn't changed since the last call to readdir(),
		 * we can jump directly to where we left off.
		 */
		if (filp->private_data && filp->f_version == inode->i_version) {
			i = (s32)(unsigned long)filp->private_data;
			j = 0;
			pr_debug("AFFS: readdir() left off=%d\n",i);
		}
		filp->f_version = inode->i_version;
		pr_debug("AFFS: hash_pos=%d chain_pos=%d\n",hash_pos,chain_pos);
		while (i) {
			if (!(fh_bh = affs_bread(inode->i_dev,i,AFFS_I2BSIZE(inode)))) {
				affs_error(inode->i_sb,"readdir","Cannot read block %d",i);
				goto readdir_done;
			}
			ino = i;
			i   = be32_to_cpu(FILE_END(fh_bh->b_data,inode)->hash_chain);
			if (j == 0)
				break;
			affs_brelse(fh_bh);
			fh_bh = NULL;
			j--;
		}
		if (fh_bh) {
			namelen = affs_get_file_name(AFFS_I2BSIZE(inode),fh_bh->b_data,&name);
			pr_debug("AFFS: readdir(): filldir(\"%.*s\",ino=%lu), i=%d\n",
				 namelen,name,ino,i);
			filp->private_data = (void *)ino;
			if (filldir(dirent,name,namelen,filp->f_pos,ino,DT_UNKNOWN) < 0)
				goto readdir_done;
			filp->private_data = (void *)(unsigned long)i;
			affs_brelse(fh_bh);
			fh_bh = NULL;
			stored++;
		}
		if (i == 0) {
			hash_pos++;
			chain_pos = 0;
		} else
			chain_pos++;
		filp->f_pos = ((hash_pos << 16) | chain_pos) + 2;
	}

readdir_done:
	affs_brelse(dir_bh);
	affs_brelse(fh_bh);
	pr_debug("AFFS: readdir()=%d\n",stored);
	return stored;
}
