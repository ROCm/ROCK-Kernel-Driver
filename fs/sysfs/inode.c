/*
 * inode.c - basic inode and dentry operations.
 *
 * sysfs is Copyright (c) 2001-3 Patrick Mochel
 *
 * Please see Documentation/filesystems/sysfs.txt for more information.
 */

#undef DEBUG 

#include <linux/pagemap.h>
#include <linux/namei.h>
#include <linux/backing-dev.h>

extern struct file_operations sysfs_file_operations;

static struct address_space_operations sysfs_aops = {
	.readpage	= simple_readpage,
	.prepare_write	= simple_prepare_write,
	.commit_write	= simple_commit_write
};



static struct backing_dev_info sysfs_backing_dev_info = {
	.ra_pages	= 0,	/* No readahead */
	.memory_backed	= 1,	/* Does not contribute to dirty memory */
};

struct inode *sysfs_get_inode(struct super_block *sb, int mode, int dev)
{
	struct inode *inode = new_inode(sb);

	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = current->fsuid;
		inode->i_gid = current->fsgid;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		inode->i_rdev = NODEV;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		inode->i_mapping->a_ops = &sysfs_aops;
		inode->i_mapping->backing_dev_info = &sysfs_backing_dev_info;
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_size = PAGE_SIZE;
			inode->i_fop = &sysfs_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &simple_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;

			/* directory inodes start off with i_nlink == 2 (for "." entry) */
			inode->i_nlink++;
			break;
		case S_IFLNK:
			inode->i_op = &page_symlink_inode_operations;
			break;
		}
	}
	return inode;
}

int sysfs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	struct inode *inode;
	int error = 0;

	if (!dentry->d_inode) {
		inode = sysfs_get_inode(dir->i_sb, mode, dev);
		if (inode) {
			d_instantiate(dentry, inode);
			dget(dentry);
		} else
			error = -ENOSPC;
	} else
		error = -EEXIST;
	return error;
}

struct dentry * sysfs_get_dentry(struct dentry * parent, const char * name)
{
	struct qstr qstr;

	qstr.name = name;
	qstr.len = strlen(name);
	qstr.hash = full_name_hash(name,qstr.len);
	return lookup_hash(&qstr,parent);
}

void sysfs_hash_and_remove(struct dentry * dir, const char * name)
{
	struct dentry * victim;

	down(&dir->d_inode->i_sem);
	victim = sysfs_get_dentry(dir,name);
	if (!IS_ERR(victim)) {
		/* make sure dentry is really there */
		if (victim->d_inode && 
		    (victim->d_parent->d_inode == dir->d_inode)) {
			simple_unlink(dir->d_inode,victim);
			d_delete(victim);

			pr_debug("sysfs: Removing %s (%d)\n", victim->d_name.name,
				 atomic_read(&victim->d_count));
			/**
			 * Drop reference from initial sysfs_get_dentry().
			 */
			dput(victim);
		}
		
		/**
		 * Drop the reference acquired from sysfs_get_dentry() above.
		 */
		dput(victim);
	}
	up(&dir->d_inode->i_sem);
}

