/*
 *	fs/libfs.c
 *	Library for filesystems writers.
 */

#include <linux/pagemap.h>

int simple_statfs(struct super_block *sb, struct statfs *buf)
{
	buf->f_type = sb->s_magic;
	buf->f_bsize = PAGE_CACHE_SIZE;
	buf->f_namelen = NAME_MAX;
	return 0;
}

/*
 * Lookup the data. This is trivial - if the dentry didn't already
 * exist, we know it is negative.
 */

struct dentry *simple_lookup(struct inode *dir, struct dentry *dentry)
{
	d_add(dentry, NULL);
	return NULL;
}

int simple_sync_file(struct file * file, struct dentry *dentry, int datasync)
{
	return 0;
}

/*
 * Directory is locked and all positive dentries in it are safe, since
 * for ramfs-type trees they can't go away without unlink() or rmdir(),
 * both impossible due to the lock on directory.
 */

int dcache_readdir(struct file * filp, void * dirent, filldir_t filldir)
{
	int i;
	struct dentry *dentry = filp->f_dentry;

	i = filp->f_pos;
	switch (i) {
		case 0:
			if (filldir(dirent, ".", 1, i, dentry->d_inode->i_ino, DT_DIR) < 0)
				break;
			i++;
			filp->f_pos++;
			/* fallthrough */
		case 1:
			if (filldir(dirent, "..", 2, i, parent_ino(dentry), DT_DIR) < 0)
				break;
			i++;
			filp->f_pos++;
			/* fallthrough */
		default: {
			struct list_head *list;
			int j = i-2;

			spin_lock(&dcache_lock);
			list = dentry->d_subdirs.next;

			for (;;) {
				if (list == &dentry->d_subdirs) {
					spin_unlock(&dcache_lock);
					return 0;
				}
				if (!j)
					break;
				j--;
				list = list->next;
			}

			while(1) {
				struct dentry *de = list_entry(list, struct dentry, d_child);

				/*
				 * See comment on top of function on why we
				 * can just drop the lock here..
				 */
				if (!list_empty(&de->d_hash) && de->d_inode) {
					spin_unlock(&dcache_lock);
					if (filldir(dirent, de->d_name.name, de->d_name.len, filp->f_pos, de->d_inode->i_ino, DT_UNKNOWN) < 0)
						break;
					spin_lock(&dcache_lock);
				}
				filp->f_pos++;
				list = list->next;
				if (list != &dentry->d_subdirs)
					continue;
				spin_unlock(&dcache_lock);
				break;
			}
		}
	}
	return 0;
}

ssize_t generic_read_dir(struct file *filp, char *buf, size_t siz, loff_t *ppos)
{
	return -EISDIR;
}

struct file_operations simple_dir_operations = {
	read:		generic_read_dir,
	readdir:	dcache_readdir,
};

struct inode_operations simple_dir_inode_operations = {
	lookup:		simple_lookup,
};
