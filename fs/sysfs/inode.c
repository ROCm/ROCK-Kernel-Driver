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
extern struct super_block * sysfs_sb;

static struct address_space_operations sysfs_aops = {
	.readpage	= simple_readpage,
	.prepare_write	= simple_prepare_write,
	.commit_write	= simple_commit_write
};

static struct backing_dev_info sysfs_backing_dev_info = {
	.ra_pages	= 0,	/* No readahead */
	.memory_backed	= 1,	/* Does not contribute to dirty memory */
};

struct inode * sysfs_new_inode(mode_t mode)
{
	struct inode * inode = new_inode(sysfs_sb);
	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = current->fsuid;
		inode->i_gid = current->fsgid;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		inode->i_mapping->a_ops = &sysfs_aops;
		inode->i_mapping->backing_dev_info = &sysfs_backing_dev_info;
	}
	return inode;
}

int sysfs_create(struct dentry * dentry, int mode, int (*init)(struct inode *))
{
	int error = 0;
	struct inode * inode = NULL;
	if (dentry) {
		if (!dentry->d_inode) {
			if ((inode = sysfs_new_inode(mode)))
				goto Proceed;
			else 
				error = -ENOMEM;
		} else
			error = -EEXIST;
	} else 
		error = -ENOENT;
	goto Done;

 Proceed:
	if (init)
		error = init(inode);
	if (!error) {
		d_instantiate(dentry, inode);
		dget(dentry); /* Extra count - pin the dentry in core */
	} else
		iput(inode);
 Done:
	return error;
}

int sysfs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	return sysfs_create(dentry, mode, NULL);
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
			pr_debug("sysfs: Removing %s (%d)\n", victim->d_name.name,
				 atomic_read(&victim->d_count));

			d_drop(victim);
			/* release the target kobject in case of 
			 * a symlink
			 */
			if (S_ISLNK(victim->d_inode->i_mode))
				kobject_put(victim->d_fsdata);
			simple_unlink(dir->d_inode,victim);
		}
		/*
		 * Drop reference from sysfs_get_dentry() above.
		 */
		dput(victim);
	}
	up(&dir->d_inode->i_sem);
}


