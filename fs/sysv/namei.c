/*
 *  linux/fs/sysv/namei.c
 *
 *  minix/namei.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  coh/namei.c
 *  Copyright (C) 1993  Pascal Haible, Bruno Haible
 *
 *  sysv/namei.c
 *  Copyright (C) 1993  Bruno Haible
 *  Copyright (C) 1997, 1998  Krzysztof G. Baranowski
 */


#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sysv_fs.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>

/* compare strings: name[0..len-1] (not zero-terminated) and
 * buffer[0..] (filled with zeroes up to buffer[0..maxlen-1])
 */
static inline int namecompare(int len, int maxlen,
	const char * name, const char * buffer)
{
	if (len > maxlen)
		return 0;
	if (len < maxlen && buffer[len])
		return 0;
	return !memcmp(name, buffer, len);
}

/*
 * ok, we cannot use strncmp, as the name is not in our data space. [Now it is!]
 * Thus we'll have to use sysv_match. No big problem. Match also makes
 * some sanity tests.
 *
 * NOTE! unlike strncmp, sysv_match returns 1 for success, 0 for failure.
 */
static int sysv_match(int len, const char * name, struct sysv_dir_entry * de)
{
	if (!de->inode || len > SYSV_NAMELEN)
		return 0;
	/* "" means "." ---> so paths like "/usr/lib//libc.a" work */
	if (!len && (de->name[0]=='.') && (de->name[1]=='\0'))
		return 1;
	return namecompare(len, SYSV_NAMELEN, name, de->name);
}

/*
 *	sysv_find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the cache buffer in which the entry was found, and the entry
 * itself (as a parameter - res_dir). It does NOT read the inode of the
 * entry - you'll have to do that yourself if you want to.
 */
static struct buffer_head * sysv_find_entry(struct inode * dir,
	const char * name, int namelen, struct sysv_dir_entry ** res_dir)
{
	struct super_block * sb;
	unsigned long pos, block, offset; /* pos = block * block_size + offset */
	struct buffer_head * bh;

	*res_dir = NULL;
	sb = dir->i_sb;
	if (namelen > SYSV_NAMELEN) {
		if (sb->sv_truncate)
			namelen = SYSV_NAMELEN;
		else
			return NULL;
	}
	bh = NULL;
	pos = block = offset = 0;
	while (pos < dir->i_size) {
		if (!bh) {
			bh = sysv_file_bread(dir, block, 0);
			if (!bh) {
				/* offset = 0; */ block++;
				pos += sb->sv_block_size;
				continue;
			}
		}
		if (sysv_match(namelen, name,
			       *res_dir = (struct sysv_dir_entry *) (bh->b_data + offset) ))
			return bh;
		pos += SYSV_DIRSIZE;
		offset += SYSV_DIRSIZE;
		if (offset < sb->sv_block_size)
			continue;
		brelse(bh);
		bh = NULL;
		offset = 0; block++;
	}
	brelse(bh);
	*res_dir = NULL;
	return NULL;
}

static struct dentry *sysv_lookup(struct inode * dir, struct dentry * dentry)
{
	struct inode * inode = NULL;
	struct sysv_dir_entry * de;
	struct buffer_head * bh;

	bh = sysv_find_entry(dir, dentry->d_name.name, dentry->d_name.len, &de);

	if (bh) {
		int ino = de->inode;
		brelse(bh);
		inode = iget(dir->i_sb, ino);
	
		if (!inode) 
			return ERR_PTR(-EACCES);
	}
	d_add(dentry, inode);
	return NULL;
}

/*
 *	sysv_add_entry()
 *
 * adds a file entry to the specified directory, returning a possible
 * error value if it fails.
 *
 * NOTE!! The inode part of 'de' is left at 0 - which means you
 * may not sleep between calling this and putting something into
 * the entry, as someone else might have used it while you slept.
 */
static int sysv_add_entry(struct inode * dir,
	const char * name, int namelen,
	struct buffer_head ** res_buf,
	struct sysv_dir_entry ** res_dir)
{
	struct super_block * sb;
	int i;
	unsigned long pos, block, offset; /* pos = block * block_size + offset */
	struct buffer_head * bh;
	struct sysv_dir_entry * de;

	*res_buf = NULL;
	*res_dir = NULL;
	sb = dir->i_sb;
	if (namelen > SYSV_NAMELEN) {
		if (sb->sv_truncate)
			namelen = SYSV_NAMELEN;
		else
			return -ENAMETOOLONG;
	}
	if (!namelen)
		return -ENOENT;
	bh = NULL;
	pos = block = offset = 0;
	while (1) {
		if (!bh) {
			bh = sysv_file_bread(dir, block, 1);
			if (!bh)
				return -ENOSPC;
		}
		de = (struct sysv_dir_entry *) (bh->b_data + offset);
		pos += SYSV_DIRSIZE;
		offset += SYSV_DIRSIZE;
		if (pos > dir->i_size) {
			de->inode = 0;
			dir->i_size = pos;
			mark_inode_dirty(dir);
		}
		if (de->inode) {
			if (namecompare(namelen, SYSV_NAMELEN, name, de->name)) {
				brelse(bh);
				return -EEXIST;
			}
		} else {
			dir->i_mtime = dir->i_ctime = CURRENT_TIME;
			mark_inode_dirty(dir);
			for (i = 0; i < SYSV_NAMELEN ; i++)
				de->name[i] = (i < namelen) ? name[i] : 0;
			mark_buffer_dirty(bh);
			*res_dir = de;
			break;
		}
		if (offset < sb->sv_block_size)
			continue;
		brelse(bh);
		bh = NULL;
		offset = 0; block++;
	}
	*res_buf = bh;
	return 0;
}

static int sysv_create(struct inode * dir, struct dentry * dentry, int mode)
{
	int error;
	struct inode * inode;
	struct buffer_head * bh;
	struct sysv_dir_entry * de;

	inode = sysv_new_inode(dir);
	if (!inode) 
		return -ENOSPC;
	inode->i_op = &sysv_file_inode_operations;
	inode->i_fop = &sysv_file_operations;
	inode->i_mapping->a_ops = &sysv_aops;
	inode->i_mode = mode;
	mark_inode_dirty(inode);
	error = sysv_add_entry(dir, dentry->d_name.name,
			       dentry->d_name.len, &bh, &de);
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

static int sysv_mknod(struct inode * dir, struct dentry * dentry, int mode, int rdev)
{
	int error;
	struct inode * inode;
	struct buffer_head * bh;
	struct sysv_dir_entry * de;

	bh = sysv_find_entry(dir, dentry->d_name.name,
			     dentry->d_name.len, &de);
	if (bh) {
		brelse(bh);
		return -EEXIST;
	}
	inode = sysv_new_inode(dir);
	if (!inode)
		return -ENOSPC;
	inode->i_uid = current->fsuid;
	init_special_inode(inode, mode, rdev);
	mark_inode_dirty(inode);
	error = sysv_add_entry(dir, dentry->d_name.name, 
			       dentry->d_name.len, &bh, &de);
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

static int sysv_mkdir(struct inode * dir, struct dentry *dentry, int mode)
{
	int error;
	struct inode * inode;
	struct buffer_head * bh, *dir_block;
	struct sysv_dir_entry * de;

	bh = sysv_find_entry(dir, dentry->d_name.name,
                              dentry->d_name.len, &de);
	if (bh) {
		brelse(bh);
		return -EEXIST;
	}
	if (dir->i_nlink >= dir->i_sb->sv_link_max) 
		return -EMLINK;
	inode = sysv_new_inode(dir);
	if (!inode)
		return -ENOSPC;
	inode->i_op = &sysv_dir_inode_operations;
	inode->i_fop = &sysv_dir_operations;
	inode->i_size = 2 * SYSV_DIRSIZE;
	dir_block = sysv_file_bread(inode,0,1);
	if (!dir_block) {
		inode->i_nlink--;
		mark_inode_dirty(inode);
		iput(inode);
		return -ENOSPC;
	}
	de = (struct sysv_dir_entry *) (dir_block->b_data + 0*SYSV_DIRSIZE);
	de->inode = inode->i_ino;
	strcpy(de->name,"."); /* rest of de->name is zero, see sysv_new_block */
	de = (struct sysv_dir_entry *) (dir_block->b_data + 1*SYSV_DIRSIZE);
	de->inode = dir->i_ino;
	strcpy(de->name,".."); /* rest of de->name is zero, see sysv_new_block */
	inode->i_nlink = 2;
	mark_buffer_dirty(dir_block);
	brelse(dir_block);
	inode->i_mode = S_IFDIR | mode;
	if (dir->i_mode & S_ISGID)
		inode->i_mode |= S_ISGID;
	mark_inode_dirty(inode);
	error = sysv_add_entry(dir, dentry->d_name.name,
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
	struct super_block * sb;
	unsigned long pos, block, offset; /* pos = block * block_size + offset */
	struct buffer_head * bh;
	struct sysv_dir_entry * de;

	block = 0;
	bh = NULL;
	pos = offset = 2*SYSV_DIRSIZE;
	if ((unsigned long)(inode->i_size) % SYSV_DIRSIZE)
		goto bad_dir;
	if (inode->i_size < pos)
		goto bad_dir;
	bh = sysv_file_bread(inode, 0, 0);
	if (!bh)
		goto bad_dir;
	de = (struct sysv_dir_entry *) (bh->b_data + 0*SYSV_DIRSIZE);
	if (!de->inode || strcmp(de->name,"."))
		goto bad_dir;
	de = (struct sysv_dir_entry *) (bh->b_data + 1*SYSV_DIRSIZE);
	if (!de->inode || strcmp(de->name,".."))
		goto bad_dir;
	sb = inode->i_sb;
	while (pos < inode->i_size) {
		if (!bh) {
			bh = sysv_file_bread(inode, block, 0);
			if (!bh) {
				/* offset = 0; */ block++;
				pos += sb->sv_block_size;
				continue;
			}
		}
		de = (struct sysv_dir_entry *) (bh->b_data + offset);
		pos += SYSV_DIRSIZE;
		offset += SYSV_DIRSIZE;
		if (de->inode) {
			brelse(bh);
			return 0;
		}
		if (offset < sb->sv_block_size)
			continue;
		brelse(bh);
		bh = NULL;
		offset = 0; block++;
	}
	brelse(bh);
	return 1;
bad_dir:
	brelse(bh);
	printk("Bad directory on device %s\n",
	       kdevname(inode->i_dev));
	return 1;
}

static int sysv_rmdir(struct inode * dir, struct dentry * dentry)
{
	int retval;
	struct inode * inode;
	struct buffer_head * bh;
	struct sysv_dir_entry * de;

	inode = dentry->d_inode;
	bh = sysv_find_entry(dir, dentry->d_name.name, dentry->d_name.len, &de);
	retval = -ENOENT;
	if (!bh || de->inode != inode->i_ino)
		goto end_rmdir;

	if (!empty_dir(inode)) {
		retval = -ENOTEMPTY;
		goto end_rmdir;
	}
	if (inode->i_nlink != 2)
		printk("empty directory has nlink!=2 (%d)\n", inode->i_nlink);
	de->inode = 0;
	mark_buffer_dirty(bh);
	inode->i_nlink=0;
	dir->i_nlink--;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	mark_inode_dirty(inode);
	mark_inode_dirty(dir);
	retval = 0;
end_rmdir:
	brelse(bh);
	return retval;
}

static int sysv_unlink(struct inode * dir, struct dentry * dentry)
{
	int retval;
	struct inode * inode;
	struct buffer_head * bh;
	struct sysv_dir_entry * de;

	retval = -ENOENT;
	inode = dentry->d_inode;
	bh = sysv_find_entry(dir, dentry->d_name.name, dentry->d_name.len, &de);
	if (!bh || de->inode != inode->i_ino)
		goto end_unlink;
	if (!inode->i_nlink) {
		printk("Deleting nonexistent file (%s:%lu), %d\n",
		        kdevname(inode->i_dev), inode->i_ino, inode->i_nlink);
		inode->i_nlink=1;
	}
	de->inode = 0;
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

static int sysv_symlink(struct inode * dir, struct dentry * dentry, 
		 const char * symname)
{
	struct inode * inode;
	struct sysv_dir_entry * de;
	struct buffer_head * bh;
	int err;
	int l;

	err = -ENAMETOOLONG;
	l = strlen(symname)+1;
	if (l > dir->i_sb->sv_block_size_1)
		goto out;
	err = -ENOSPC;
	if (!(inode = sysv_new_inode(dir)))
		goto out;

	inode->i_mode = S_IFLNK | 0777;
	inode->i_op = &sysv_symlink_inode_operations;
	inode->i_mapping->a_ops = &sysv_aops;
	err = block_symlink(inode, symname, l);
	if (err)
		goto out_no_entry;
	mark_inode_dirty(inode);
	err = sysv_add_entry(dir, dentry->d_name.name,
                           dentry->d_name.len, &bh, &de);
	if (err)
		goto out_no_entry;
	de->inode = inode->i_ino;
	mark_buffer_dirty(bh);
	brelse(bh);
        d_instantiate(dentry, inode);
out:
	return err;
out_no_entry:
	inode->i_nlink--;
	mark_inode_dirty(inode);
	iput(inode);
	goto out;
}

static int sysv_link(struct dentry * old_dentry, struct inode * dir, 
	      struct dentry * dentry)
{
	struct inode *oldinode = old_dentry->d_inode;
	int error;
	struct sysv_dir_entry * de;
	struct buffer_head * bh;

	if (S_ISDIR(oldinode->i_mode)) {
		return -EPERM;
	}
	if (oldinode->i_nlink >= oldinode->i_sb->sv_link_max) {
		return -EMLINK;
	}
	bh = sysv_find_entry(dir, dentry->d_name.name,
                             dentry->d_name.len, &de);
	if (bh) {
		brelse(bh);
		return -EEXIST;
	}
	error = sysv_add_entry(dir, dentry->d_name.name,
                               dentry->d_name.len, &bh, &de);
	if (error) {
		brelse(bh);
		return error;
	}
	de->inode = oldinode->i_ino;
	mark_buffer_dirty(bh);
	brelse(bh);
	oldinode->i_nlink++;
	oldinode->i_ctime = CURRENT_TIME;
	mark_inode_dirty(oldinode);
	atomic_inc(&oldinode->i_count);
        d_instantiate(dentry, oldinode);
	return 0;
}

#define PARENT_INO(buffer) \
(((struct sysv_dir_entry *) ((buffer) + 1*SYSV_DIRSIZE))->inode)

/*
 * Anybody can rename anything with this: the permission checks are left to the
 * higher-level routines.
 */
static int sysv_rename(struct inode * old_dir, struct dentry * old_dentry,
		  struct inode * new_dir, struct dentry * new_dentry)
{
	struct inode * old_inode, * new_inode;
	struct buffer_head * old_bh, * new_bh, * dir_bh;
	struct sysv_dir_entry * old_de, * new_de;
	int retval;

	old_inode = old_dentry->d_inode;
	new_inode = new_dentry->d_inode;
	new_bh = dir_bh = NULL;
	old_bh = sysv_find_entry(old_dir, old_dentry->d_name.name,
				old_dentry->d_name.len, &old_de);
	retval = -ENOENT;
	if (!old_bh || old_de->inode != old_inode->i_ino)
		goto end_rename;
	retval = -EPERM;
	new_bh = sysv_find_entry(new_dir, new_dentry->d_name.name,
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
		dir_bh = sysv_file_bread(old_inode, 0, 0);
		if (!dir_bh)
			goto end_rename;
		if (PARENT_INO(dir_bh->b_data) != old_dir->i_ino)
			goto end_rename;
		retval = -EMLINK;
		if (!new_inode && new_dir != old_dir &&
				new_dir->i_nlink >= new_dir->i_sb->sv_link_max)
			goto end_rename;
	}
	if (!new_bh) {
		retval = sysv_add_entry(new_dir, new_dentry->d_name.name,
					new_dentry->d_name.len, &new_bh, &new_de);
		if (retval)
			goto end_rename;
	}
	new_de->inode = old_inode->i_ino;
	old_de->inode = 0;
	old_dir->i_ctime = old_dir->i_mtime = CURRENT_TIME;
	mark_inode_dirty(old_dir);
	new_dir->i_ctime = new_dir->i_mtime = CURRENT_TIME;
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
struct inode_operations sysv_dir_inode_operations = {
	create:		sysv_create,
	lookup:		sysv_lookup,
	link:		sysv_link,
	unlink:		sysv_unlink,
	symlink:	sysv_symlink,
	mkdir:		sysv_mkdir,
	rmdir:		sysv_rmdir,
	mknod:		sysv_mknod,
	rename:		sysv_rename,
	setattr:	sysv_notify_change,
};
