/*
 *  linux/fs/fat/file.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *
 *  regular file handling primitives for fat-based filesystems
 */

#include <linux/time.h>
#include <linux/msdos_fs.h>
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>

static ssize_t fat_file_write(struct file *filp, const char __user *buf,
			      size_t count, loff_t *ppos);

struct file_operations fat_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_file_read,
	.write		= fat_file_write,
	.mmap		= generic_file_mmap,
	.fsync		= file_fsync,
	.readv		= generic_file_readv,
	.writev		= generic_file_writev,
	.sendfile	= generic_file_sendfile,
};

struct inode_operations fat_file_inode_operations = {
	.truncate	= fat_truncate,
	.setattr	= fat_notify_change,
};

int fat_get_block(struct inode *inode, sector_t iblock,
		  struct buffer_head *bh_result, int create)
{
	struct super_block *sb = inode->i_sb;
	sector_t phys;
	int err;

	err = fat_bmap(inode, iblock, &phys);
	if (err)
		return err;
	if (phys) {
		map_bh(bh_result, sb, phys);
		return 0;
	}
	if (!create)
		return 0;
	if (iblock != MSDOS_I(inode)->mmu_private >> sb->s_blocksize_bits) {
		fat_fs_panic(sb, "corrupted file size (i_pos %lld, %lld)",
			     MSDOS_I(inode)->i_pos, MSDOS_I(inode)->mmu_private);
		return -EIO;
	}
	if (!((unsigned long)iblock & (MSDOS_SB(sb)->sec_per_clus - 1))) {
		int error;

		error = fat_add_cluster(inode);
		if (error < 0)
			return error;
	}
	MSDOS_I(inode)->mmu_private += sb->s_blocksize;
	err = fat_bmap(inode, iblock, &phys);
	if (err)
		return err;
	if (!phys)
		BUG();
	set_buffer_new(bh_result);
	map_bh(bh_result, sb, phys);
	return 0;
}

static ssize_t fat_file_write(struct file *filp, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct inode *inode = filp->f_dentry->d_inode;
	int retval;

	retval = generic_file_write(filp, buf, count, ppos);
	if (retval > 0) {
		inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		MSDOS_I(inode)->i_attrs |= ATTR_ARCH;
		mark_inode_dirty(inode);
	}
	return retval;
}

void fat_truncate(struct inode *inode)
{
	struct msdos_sb_info *sbi = MSDOS_SB(inode->i_sb);
	const unsigned int cluster_size = sbi->cluster_size;
	int nr_clusters;

	/* Why no return value?  Surely the disk could fail... */
	if (IS_RDONLY (inode))
		return /* -EPERM */;
	if (IS_IMMUTABLE(inode))
		return /* -EPERM */;

	/* 
	 * This protects against truncating a file bigger than it was then
	 * trying to write into the hole.
	 */
	if (MSDOS_I(inode)->mmu_private > inode->i_size)
		MSDOS_I(inode)->mmu_private = inode->i_size;

	nr_clusters = (inode->i_size + (cluster_size - 1)) >> sbi->cluster_bits;

	lock_kernel();
	fat_free(inode, nr_clusters);
	MSDOS_I(inode)->i_attrs |= ATTR_ARCH;
	unlock_kernel();
	inode->i_ctime = inode->i_mtime = CURRENT_TIME;
	mark_inode_dirty(inode);
}
