/*
 *  linux/fs/minix/namei.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/sched.h>
#include <linux/minix_fs.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/errno.h>
#include <linux/quotaops.h>

#include <asm/uaccess.h>

/*
 * comment out this line if you want names > info->s_namelen chars to be
 * truncated. Else they will be disallowed (ENAMETOOLONG).
 */
/* #define NO_TRUNCATE */

static inline int namecompare(int len, int maxlen,
	const char * name, const char * buffer)
{
	if (len < maxlen && buffer[len])
		return 0;
	return !memcmp(name, buffer, len);
}

/*
 *	minix_find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the cache buffer in which the entry was found, and the entry
 * itself (as a parameter - res_dir). It does NOT read the inode of the
 * entry - you'll have to do that yourself if you want to.
 */
static struct buffer_head * minix_find_entry(struct inode * dir,
	const char * name, int namelen, struct minix_dir_entry ** res_dir)
{
	unsigned long block, offset;
	struct buffer_head * bh;
	struct minix_sb_info * info;
	struct minix_dir_entry *de;

	*res_dir = NULL;
	info = &dir->i_sb->u.minix_sb;
	if (namelen > info->s_namelen) {
#ifdef NO_TRUNCATE
		return NULL;
#else
		namelen = info->s_namelen;
#endif
	}
	bh = NULL;
	block = offset = 0;
	while (block*BLOCK_SIZE+offset < dir->i_size) {
		if (!bh) {
			bh = minix_bread(dir,block,0);
			if (!bh) {
				block++;
				continue;
			}
		}
		de = (struct minix_dir_entry *) (bh->b_data + offset);
		offset += info->s_dirsize;
		if (de->inode && namecompare(namelen,info->s_namelen,name,de->name)) {
			*res_dir = de;
			return bh;
		}
		if (offset < bh->b_size)
			continue;
		brelse(bh);
		bh = NULL;
		offset = 0;
		block++;
	}
	brelse(bh);
	return NULL;
}

#ifndef NO_TRUNCATE

static int minix_hash(struct dentry *dentry, struct qstr *qstr)
{
	unsigned long hash;
	int i;
	const unsigned char *name;

	i = dentry->d_inode->i_sb->u.minix_sb.s_namelen;
	if (i >= qstr->len)
		return 0;
	/* Truncate the name in place, avoids having to define a compare
	   function. */
	qstr->len = i;
	name = qstr->name;
	hash = init_name_hash();
	while (i--)
		hash = partial_name_hash(*name++, hash);
	qstr->hash = end_name_hash(hash);
	return 0;
}

#endif

struct dentry_operations minix_dentry_operations = {
#ifndef NO_TRUNCATE
	d_hash:		minix_hash,
#endif
};

static struct dentry *minix_lookup(struct inode * dir, struct dentry *dentry)
{
	struct inode * inode = NULL;
	struct minix_dir_entry * de;
	struct buffer_head * bh;

#ifndef NO_TRUNCATE
	dentry->d_op = &minix_dentry_operations;
#endif
	bh = minix_find_entry(dir, dentry->d_name.name, dentry->d_name.len, &de);
	if (bh) {
		int ino = de->inode;
		brelse (bh);
		inode = iget(dir->i_sb, ino);
 
		if (!inode)
			return ERR_PTR(-EACCES);
	}
	d_add(dentry, inode);
	return NULL;
}

/*
 *	minix_add_entry()
 *
 * adds a file entry to the specified directory, returning a possible
 * error value if it fails.
 *
 * NOTE!! The inode part of 'de' is left at 0 - which means you
 * may not sleep between calling this and putting something into
 * the entry, as someone else might have used it while you slept.
 */
static int minix_add_entry(struct inode * dir,
	const char * name, int namelen,
	struct buffer_head ** res_buf,
	struct minix_dir_entry ** res_dir)
{
	int i;
	unsigned long block, offset;
	struct buffer_head * bh;
	struct minix_dir_entry * de;
	struct minix_sb_info * info;

	*res_buf = NULL;
	*res_dir = NULL;
	info = &dir->i_sb->u.minix_sb;
	if (namelen > info->s_namelen) {
#ifdef NO_TRUNCATE
		return -ENAMETOOLONG;
#else
		namelen = info->s_namelen;
#endif
	}
	if (!namelen)
		return -ENOENT;
	bh = NULL;
	block = offset = 0;
	while (1) {
		if (!bh) {
			bh = minix_bread(dir,block,1);
			if (!bh)
				return -ENOSPC;
		}
		de = (struct minix_dir_entry *) (bh->b_data + offset);
		offset += info->s_dirsize;
		if (block*bh->b_size + offset > dir->i_size) {
			de->inode = 0;
			dir->i_size = block*bh->b_size + offset;
			mark_inode_dirty(dir);
		}
		if (!de->inode) {
			dir->i_mtime = dir->i_ctime = CURRENT_TIME;
			mark_inode_dirty(dir);
			for (i = 0; i < info->s_namelen ; i++)
				de->name[i] = (i < namelen) ? name[i] : 0;
			dir->i_version = ++event;
			mark_buffer_dirty(bh);
			*res_dir = de;
			break;
		}
		if (offset < bh->b_size)
			continue;
		brelse(bh);
		bh = NULL;
		offset = 0;
		block++;
	}
	*res_buf = bh;
	return 0;
}

static int minix_create(struct inode * dir, struct dentry *dentry, int mode)
{
	int error;
	struct inode * inode;
	struct buffer_head * bh;
	struct minix_dir_entry * de;

	inode = minix_new_inode(dir, &error);
	if (!inode)
		return error;
	inode->i_op = &minix_file_inode_operations;
	inode->i_fop = &minix_file_operations;
	inode->i_mapping->a_ops = &minix_aops;
	inode->i_mode = mode;
	mark_inode_dirty(inode);
	error = minix_add_entry(dir, dentry->d_name.name,
				dentry->d_name.len, &bh ,&de);
	if (error) {
		inode->i_nlink--;
		mark_inode_dirty(inode);
		iput(inode);
		return error;
	}
	de->inode = inode->i_ino;
	mark_buffer_dirty(bh);
	brelse(bh);
	d_instantiate(dentry, inode);
	return 0;
}

static int minix_mknod(struct inode * dir, struct dentry *dentry, int mode, int rdev)
{
	int error;
	struct inode * inode;
	struct buffer_head * bh;
	struct minix_dir_entry * de;

	inode = minix_new_inode(dir, &error);
	if (!inode)
		return error;
	inode->i_uid = current->fsuid;
	init_special_inode(inode, mode, rdev);
	mark_inode_dirty(inode);
	error = minix_add_entry(dir, dentry->d_name.name, dentry->d_name.len, &bh, &de);
	if (error) {
		inode->i_nlink--;
		mark_inode_dirty(inode);
		iput(inode);
		return error;
	}
	de->inode = inode->i_ino;
	mark_buffer_dirty(bh);
	brelse(bh);
	d_instantiate(dentry, inode);
	return 0;
}

static int minix_mkdir(struct inode * dir, struct dentry *dentry, int mode)
{
	int error;
	struct inode * inode;
	struct buffer_head * bh, *dir_block;
	struct minix_dir_entry * de;
	struct minix_sb_info * info;

	info = &dir->i_sb->u.minix_sb;
	if (dir->i_nlink >= info->s_link_max)
		return -EMLINK;
	inode = minix_new_inode(dir, &error);
	if (!inode)
		return error;
	inode->i_op = &minix_dir_inode_operations;
	inode->i_fop = &minix_dir_operations;
	inode->i_size = 2 * info->s_dirsize;
	dir_block = minix_bread(inode,0,1);
	if (!dir_block) {
		inode->i_nlink--;
		mark_inode_dirty(inode);
		iput(inode);
		return -ENOSPC;
	}
	de = (struct minix_dir_entry *) dir_block->b_data;
	de->inode=inode->i_ino;
	strcpy(de->name,".");
	de = (struct minix_dir_entry *) (dir_block->b_data + info->s_dirsize);
	de->inode = dir->i_ino;
	strcpy(de->name,"..");
	inode->i_nlink = 2;
	mark_buffer_dirty(dir_block);
	brelse(dir_block);
	inode->i_mode = S_IFDIR | mode;
	if (dir->i_mode & S_ISGID)
		inode->i_mode |= S_ISGID;
	mark_inode_dirty(inode);
	error = minix_add_entry(dir, dentry->d_name.name,
				dentry->d_name.len, &bh, &de);
	if (error) {
		inode->i_nlink=0;
		iput(inode);
		return error;
	}
	de->inode = inode->i_ino;
	mark_buffer_dirty(bh);
	dir->i_nlink++;
	mark_inode_dirty(dir);
	brelse(bh);
	d_instantiate(dentry, inode);
	return 0;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
static int empty_dir(struct inode * inode)
{
	unsigned int block, offset;
	struct buffer_head * bh;
	struct minix_dir_entry * de;
	struct minix_sb_info * info;

	info = &inode->i_sb->u.minix_sb;
	block = 0;
	bh = NULL;
	offset = 2*info->s_dirsize;
	if (inode->i_size & (info->s_dirsize-1))
		goto bad_dir;
	if (inode->i_size < offset)
		goto bad_dir;
	bh = minix_bread(inode,0,0);
	if (!bh)
		goto bad_dir;
	de = (struct minix_dir_entry *) bh->b_data;
	if (!de->inode || strcmp(de->name,"."))
		goto bad_dir;
	de = (struct minix_dir_entry *) (bh->b_data + info->s_dirsize);
	if (!de->inode || strcmp(de->name,".."))
		goto bad_dir;
	while (block*BLOCK_SIZE+offset < inode->i_size) {
		if (!bh) {
			bh = minix_bread(inode,block,0);
			if (!bh) {
				block++;
				continue;
			}
		}
		de = (struct minix_dir_entry *) (bh->b_data + offset);
		offset += info->s_dirsize;
		if (de->inode) {
			brelse(bh);
			return 0;
		}
		if (offset < bh->b_size)
			continue;
		brelse(bh);
		bh = NULL;
		offset = 0;
		block++;
	}
	brelse(bh);
	return 1;
bad_dir:
	brelse(bh);
	printk("Bad directory on device %s\n",
	       kdevname(inode->i_dev));
	return 1;
}

static int minix_rmdir(struct inode * dir, struct dentry *dentry)
{
	int retval;
	struct inode * inode;
	struct buffer_head * bh;
	struct minix_dir_entry * de;

	inode = NULL;
	bh = minix_find_entry(dir, dentry->d_name.name,
			      dentry->d_name.len, &de);
	retval = -ENOENT;
	if (!bh)
		goto end_rmdir;
	inode = dentry->d_inode;

	if (!empty_dir(inode)) {
		retval = -ENOTEMPTY;
		goto end_rmdir;
	}
	if (de->inode != inode->i_ino) {
		retval = -ENOENT;
		goto end_rmdir;
	}
	if (inode->i_nlink != 2)
		printk("empty directory has nlink!=2 (%d)\n",inode->i_nlink);
	de->inode = 0;
	dir->i_version = ++event;
	mark_buffer_dirty(bh);
	inode->i_nlink=0;
	mark_inode_dirty(inode);
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	dir->i_nlink--;
	mark_inode_dirty(dir);
	retval = 0;
end_rmdir:
	brelse(bh);
	return retval;
}

static int minix_unlink(struct inode * dir, struct dentry *dentry)
{
	int retval;
	struct inode * inode;
	struct buffer_head * bh;
	struct minix_dir_entry * de;

	retval = -ENOENT;
	inode = dentry->d_inode;
	bh = minix_find_entry(dir, dentry->d_name.name,
			      dentry->d_name.len, &de);
	if (!bh || de->inode != inode->i_ino)
		goto end_unlink;
	if (!inode->i_nlink) {
		printk("Deleting nonexistent file (%s:%lu), %d\n",
			kdevname(inode->i_dev),
		       inode->i_ino, inode->i_nlink);
		inode->i_nlink=1;
	}
	de->inode = 0;
	dir->i_version = ++event;
	mark_buffer_dirty(bh);
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	mark_inode_dirty(dir);
	inode->i_nlink--;
	inode->i_ctime = dir->i_ctime;
	mark_inode_dirty(inode);
	retval = 0;
end_unlink:
	brelse(bh);
	return retval;
}

static int minix_symlink(struct inode * dir, struct dentry *dentry,
		  const char * symname)
{
	struct minix_dir_entry * de;
	struct inode * inode = NULL;
	struct buffer_head * bh = NULL;
	int i;
	int err;

	err = -ENAMETOOLONG;
	i = strlen(symname)+1;
	if (i>1024)
		goto out;
	inode = minix_new_inode(dir, &err);
	if (!inode)
		goto out;

	inode->i_mode = S_IFLNK | 0777;
	inode->i_op = &page_symlink_inode_operations;
	inode->i_mapping->a_ops = &minix_aops;
	err = block_symlink(inode, symname, i);
	if (err)
		goto fail;

	err = minix_add_entry(dir, dentry->d_name.name,
			    dentry->d_name.len, &bh, &de);
	if (err)
		goto fail;

	de->inode = inode->i_ino;
	mark_buffer_dirty(bh);
	brelse(bh);
	d_instantiate(dentry, inode);
out:
	return err;
fail:
	inode->i_nlink--;
	mark_inode_dirty(inode);
	iput(inode);
	goto out;
}

static int minix_link(struct dentry * old_dentry, struct inode * dir,
	       struct dentry *dentry)
{
	int error;
	struct inode *inode = old_dentry->d_inode;
	struct minix_dir_entry * de;
	struct buffer_head * bh;

	if (S_ISDIR(inode->i_mode))
		return -EPERM;

	if (inode->i_nlink >= inode->i_sb->u.minix_sb.s_link_max)
		return -EMLINK;

	error = minix_add_entry(dir, dentry->d_name.name,
				dentry->d_name.len, &bh, &de);
	if (error) {
		brelse(bh);
		return error;
	}
	de->inode = inode->i_ino;
	mark_buffer_dirty(bh);
	brelse(bh);
	inode->i_nlink++;
	inode->i_ctime = CURRENT_TIME;
	mark_inode_dirty(inode);
	atomic_inc(&inode->i_count);
	d_instantiate(dentry, inode);
	return 0;
}

#define PARENT_INO(buffer) \
(((struct minix_dir_entry *) ((buffer)+info->s_dirsize))->inode)

/*
 * Anybody can rename anything with this: the permission checks are left to the
 * higher-level routines.
 */
static int minix_rename(struct inode * old_dir, struct dentry *old_dentry,
			   struct inode * new_dir, struct dentry *new_dentry)
{
	struct inode * old_inode, * new_inode;
	struct buffer_head * old_bh, * new_bh, * dir_bh;
	struct minix_dir_entry * old_de, * new_de;
	struct minix_sb_info * info;
	int retval;

	info = &old_dir->i_sb->u.minix_sb;
	new_bh = dir_bh = NULL;
	old_inode = old_dentry->d_inode;
	new_inode = new_dentry->d_inode;
	old_bh = minix_find_entry(old_dir, old_dentry->d_name.name,
				  old_dentry->d_name.len, &old_de);
	retval = -ENOENT;
	if (!old_bh || old_de->inode != old_inode->i_ino)
		goto end_rename;
	retval = -EPERM;
	new_bh = minix_find_entry(new_dir, new_dentry->d_name.name,
				  new_dentry->d_name.len, &new_de);
	if (new_bh) {
		if (!new_inode) {
			brelse(new_bh);
			new_bh = NULL;
		}
	}
	if (S_ISDIR(old_inode->i_mode)) {
		if (new_inode) {
			retval = -ENOTEMPTY;
			if (!empty_dir(new_inode))
				goto end_rename;
		}
		retval = -EIO;
		dir_bh = minix_bread(old_inode,0,0);
		if (!dir_bh)
			goto end_rename;
		if (PARENT_INO(dir_bh->b_data) != old_dir->i_ino)
			goto end_rename;
		retval = -EMLINK;
		if (!new_inode && new_dir != old_dir &&
				new_dir->i_nlink >= info->s_link_max)
			goto end_rename;
	}
	if (!new_bh) {
		retval = minix_add_entry(new_dir,
					 new_dentry->d_name.name,
					 new_dentry->d_name.len,
					 &new_bh, &new_de);
		if (retval)
			goto end_rename;
	}
/* ok, that's it */
	new_de->inode = old_inode->i_ino;
	old_de->inode = 0;
	old_dir->i_ctime = old_dir->i_mtime = CURRENT_TIME;
	old_dir->i_version = ++event;
	mark_inode_dirty(old_dir);
	new_dir->i_ctime = new_dir->i_mtime = CURRENT_TIME;
	new_dir->i_version = ++event;
	mark_inode_dirty(new_dir);
	if (new_inode) {
		new_inode->i_nlink--;
		new_inode->i_ctime = CURRENT_TIME;
		mark_inode_dirty(new_inode);
	}
	mark_buffer_dirty(old_bh);
	mark_buffer_dirty(new_bh);
	if (dir_bh) {
		PARENT_INO(dir_bh->b_data) = new_dir->i_ino;
		mark_buffer_dirty(dir_bh);
		old_dir->i_nlink--;
		mark_inode_dirty(old_dir);
		if (new_inode) {
			new_inode->i_nlink--;
			mark_inode_dirty(new_inode);
		} else {
			new_dir->i_nlink++;
			mark_inode_dirty(new_dir);
		}
	}
	retval = 0;
end_rename:
	brelse(dir_bh);
	brelse(old_bh);
	brelse(new_bh);
	return retval;
}

/*
 * directories can handle most operations...
 */
struct inode_operations minix_dir_inode_operations = {
	create:		minix_create,
	lookup:		minix_lookup,
	link:		minix_link,
	unlink:		minix_unlink,
	symlink:	minix_symlink,
	mkdir:		minix_mkdir,
	rmdir:		minix_rmdir,
	mknod:		minix_mknod,
	rename:		minix_rename,
};
