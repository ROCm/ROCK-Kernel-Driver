/*
 * sysfs.c - The filesystem to export kernel objects.
 *
 * Copyright (c) 2001, 2002 Patrick Mochel
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * This is a simple, ramfs-based filesystem, designed to export kernel 
 * objects, their attributes, and the linkgaes between each other.
 *
 * Please see Documentation/filesystems/sysfs.txt for more information.
 */

#undef DEBUG 

#include <linux/list.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/backing-dev.h>
#include <linux/kobject.h>
#include <linux/mount.h>
#include <linux/dnotify.h>
#include <asm/uaccess.h>

/* Random magic number */
#define SYSFS_MAGIC 0x62656572

static struct super_operations sysfs_ops;
static struct file_operations sysfs_file_operations;
static struct address_space_operations sysfs_aops;

static struct vfsmount *sysfs_mount;

static struct backing_dev_info sysfs_backing_dev_info = {
	.ra_pages	= 0,	/* No readahead */
	.memory_backed	= 1,	/* Does not contribute to dirty memory */
};

static struct inode *sysfs_get_inode(struct super_block *sb, int mode, int dev)
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

static int sysfs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
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

static int sysfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int res;
	mode = (mode & (S_IRWXUGO|S_ISVTX)) | S_IFDIR;
 	res = sysfs_mknod(dir, dentry, mode, 0);
 	if (!res)
 		dir->i_nlink++;
	return res;
}

static int sysfs_create(struct inode *dir, struct dentry *dentry, int mode)
{
	int res;
	mode = (mode & S_IALLUGO) | S_IFREG;
 	res = sysfs_mknod(dir, dentry, mode, 0);
	return res;
}

static int sysfs_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
	struct inode *inode;
	int error = -ENOSPC;

	if (dentry->d_inode)
		return -EEXIST;

	inode = sysfs_get_inode(dir->i_sb, S_IFLNK|S_IRWXUGO, 0);
	if (inode) {
		int l = strlen(symname)+1;
		error = page_symlink(inode, symname, l);
		if (!error) {
			d_instantiate(dentry, inode);
			dget(dentry);
		} else
			iput(inode);
	}
	return error;
}

#define to_subsys(k) container_of(k,struct subsystem,kset.kobj)
#define to_sattr(a) container_of(a,struct subsys_attribute,attr)

/**
 * Subsystem file operations.
 * These operations allow subsystems to have files that can be 
 * read/written. 
 */
static ssize_t 
subsys_attr_show(struct kobject * kobj, struct attribute * attr, char * page)
{
	struct subsystem * s = to_subsys(kobj);
	struct subsys_attribute * sattr = to_sattr(attr);
	ssize_t ret = 0;

	if (sattr->show)
		ret = sattr->show(s,page);
	return ret;
}

static ssize_t 
subsys_attr_store(struct kobject * kobj, struct attribute * attr, 
		  const char * page, size_t count)
{
	struct subsystem * s = to_subsys(kobj);
	struct subsys_attribute * sattr = to_sattr(attr);
	ssize_t ret = 0;

	if (sattr->store)
		ret = sattr->store(s,page,count);
	return ret;
}

static struct sysfs_ops subsys_sysfs_ops = {
	.show	= subsys_attr_show,
	.store	= subsys_attr_store,
};


struct sysfs_buffer {
	size_t			count;
	loff_t			pos;
	char			* page;
	struct sysfs_ops	* ops;
};


/**
 *	fill_read_buffer - allocate and fill buffer from object.
 *	@file:		file pointer.
 *	@buffer:	data buffer for file.
 *
 *	Allocate @buffer->page, if it hasn't been already, then call the
 *	kobject's show() method to fill the buffer with this attribute's 
 *	data. 
 *	This is called only once, on the file's first read. 
 */
static int fill_read_buffer(struct file * file, struct sysfs_buffer * buffer)
{
	struct attribute * attr = file->f_dentry->d_fsdata;
	struct kobject * kobj = file->f_dentry->d_parent->d_fsdata;
	struct sysfs_ops * ops = buffer->ops;
	int ret = 0;
	ssize_t count;

	if (!buffer->page)
		buffer->page = (char *) __get_free_page(GFP_KERNEL);
	if (!buffer->page)
		return -ENOMEM;

	count = ops->show(kobj,attr,buffer->page);
	if (count >= 0)
		buffer->count = count;
	else
		ret = count;
	return ret;
}


/**
 *	flush_read_buffer - push buffer to userspace.
 *	@buffer:	data buffer for file.
 *	@userbuf:	user-passed buffer.
 *	@count:		number of bytes requested.
 *	@ppos:		file position.
 *
 *	Copy the buffer we filled in fill_read_buffer() to userspace.
 *	This is done at the reader's leisure, copying and advancing 
 *	the amount they specify each time.
 *	This may be called continuously until the buffer is empty.
 */
static int flush_read_buffer(struct sysfs_buffer * buffer, char * buf, 
			     size_t count, loff_t * ppos)
{
	int error;

	if (count > (buffer->count - *ppos))
		count = buffer->count - *ppos;

	error = copy_to_user(buf,buffer->page + *ppos,count);
	if (!error)
		*ppos += count;
	return error ? -EFAULT : count;
}

/**
 *	sysfs_read_file - read an attribute. 
 *	@file:	file pointer.
 *	@buf:	buffer to fill.
 *	@count:	number of bytes to read.
 *	@ppos:	starting offset in file.
 *
 *	Userspace wants to read an attribute file. The attribute descriptor
 *	is in the file's ->d_fsdata. The target object is in the directory's
 *	->d_fsdata.
 *
 *	We call fill_read_buffer() to allocate and fill the buffer from the
 *	object's show() method exactly once (if the read is happening from
 *	the beginning of the file). That should fill the entire buffer with
 *	all the data the object has to offer for that attribute.
 *	We then call flush_read_buffer() to copy the buffer to userspace
 *	in the increments specified.
 */

static ssize_t
sysfs_read_file(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	struct sysfs_buffer * buffer = file->private_data;
	ssize_t retval = 0;

	if (!*ppos) {
		if ((retval = fill_read_buffer(file,buffer)))
			return retval;
	}
	pr_debug("%s: count = %d, ppos = %lld, buf = %s\n",
		 __FUNCTION__,count,*ppos,buffer->page);
	return flush_read_buffer(buffer,buf,count,ppos);
}


/**
 *	fill_write_buffer - copy buffer from userspace.
 *	@buffer:	data buffer for file.
 *	@userbuf:	data from user.
 *	@count:		number of bytes in @userbuf.
 *
 *	Allocate @buffer->page if it hasn't been already, then
 *	copy the user-supplied buffer into it.
 */

static int 
fill_write_buffer(struct sysfs_buffer * buffer, const char * buf, size_t count)
{
	int error;

	if (!buffer->page)
		buffer->page = (char *)__get_free_page(GFP_KERNEL);
	if (!buffer->page)
		return -ENOMEM;

	if (count >= PAGE_SIZE)
		count = PAGE_SIZE - 1;
	error = copy_from_user(buffer->page,buf,count);
	return error ? -EFAULT : count;
}


/**
 *	flush_write_buffer - push buffer to kobject.
 *	@file:		file pointer.
 *	@buffer:	data buffer for file.
 *
 *	Get the correct pointers for the kobject and the attribute we're
 *	dealing with, then call the store() method for the attribute, 
 *	passing the buffer that we acquired in fill_write_buffer().
 */

static int 
flush_write_buffer(struct file * file, struct sysfs_buffer * buffer, size_t count)
{
	struct attribute * attr = file->f_dentry->d_fsdata;
	struct kobject * kobj = file->f_dentry->d_parent->d_fsdata;
	struct sysfs_ops * ops = buffer->ops;

	return ops->store(kobj,attr,buffer->page,count);
}


/**
 *	sysfs_write_file - write an attribute.
 *	@file:	file pointer
 *	@buf:	data to write
 *	@count:	number of bytes
 *	@ppos:	starting offset
 *
 *	Similar to sysfs_read_file(), though working in the opposite direction.
 *	We allocate and fill the data from the user in fill_write_buffer(),
 *	then push it to the kobject in flush_write_buffer().
 *	There is no easy way for us to know if userspace is only doing a partial
 *	write, so we don't support them. We expect the entire buffer to come
 *	on the first write. 
 *	Hint: if you're writing a value, first read the file, modify only the
 *	the value you're changing, then write entire buffer back. 
 */

static ssize_t
sysfs_write_file(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	struct sysfs_buffer * buffer = file->private_data;

	count = fill_write_buffer(buffer,buf,count);
	if (count > 0)
		count = flush_write_buffer(file,buffer,count);
	if (count > 0)
		*ppos += count;
	return count;
}

static int check_perm(struct inode * inode, struct file * file)
{
	struct kobject * kobj = kobject_get(file->f_dentry->d_parent->d_fsdata);
	struct attribute * attr = file->f_dentry->d_fsdata;
	struct sysfs_buffer * buffer;
	struct sysfs_ops * ops = NULL;
	int error = 0;

	if (!kobj || !attr)
		goto Einval;

	/* if the kobject has no ktype, then we assume that it is a subsystem
	 * itself, and use ops for it.
	 */
	if (kobj->kset && kobj->kset->ktype)
		ops = kobj->kset->ktype->sysfs_ops;
	else if (kobj->ktype)
		ops = kobj->ktype->sysfs_ops;
	else
		ops = &subsys_sysfs_ops;

	/* No sysfs operations, either from having no subsystem,
	 * or the subsystem have no operations.
	 */
	if (!ops)
		goto Eaccess;

	/* File needs write support.
	 * The inode's perms must say it's ok, 
	 * and we must have a store method.
	 */
	if (file->f_mode & FMODE_WRITE) {

		if (!(inode->i_mode & S_IWUGO) || !ops->store)
			goto Eaccess;

	}

	/* File needs read support.
	 * The inode's perms must say it's ok, and we there
	 * must be a show method for it.
	 */
	if (file->f_mode & FMODE_READ) {
		if (!(inode->i_mode & S_IRUGO) || !ops->show)
			goto Eaccess;
	}

	/* No error? Great, allocate a buffer for the file, and store it
	 * it in file->private_data for easy access.
	 */
	buffer = kmalloc(sizeof(struct sysfs_buffer),GFP_KERNEL);
	if (buffer) {
		memset(buffer,0,sizeof(struct sysfs_buffer));
		buffer->ops = ops;
		file->private_data = buffer;
	} else
		error = -ENOMEM;
	goto Done;

 Einval:
	error = -EINVAL;
	goto Done;
 Eaccess:
	error = -EACCES;
 Done:
	if (error && kobj)
		kobject_put(kobj);
	return error;
}

static int sysfs_open_file(struct inode * inode, struct file * filp)
{
	return check_perm(inode,filp);
}

static int sysfs_release(struct inode * inode, struct file * filp)
{
	struct kobject * kobj = filp->f_dentry->d_parent->d_fsdata;
	struct sysfs_buffer * buffer = filp->private_data;

	if (kobj) 
		kobject_put(kobj);

	if (buffer) {
		if (buffer->page)
			free_page((unsigned long)buffer->page);
		kfree(buffer);
	}
	return 0;
}

static struct file_operations sysfs_file_operations = {
	.read		= sysfs_read_file,
	.write		= sysfs_write_file,
	.llseek		= generic_file_llseek,
	.open		= sysfs_open_file,
	.release	= sysfs_release,
};

static struct address_space_operations sysfs_aops = {
	.readpage	= simple_readpage,
	.prepare_write	= simple_prepare_write,
	.commit_write	= simple_commit_write
};

static struct super_operations sysfs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
};

static int sysfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *inode;
	struct dentry *root;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = SYSFS_MAGIC;
	sb->s_op = &sysfs_ops;
	inode = sysfs_get_inode(sb, S_IFDIR | 0755, 0);

	if (!inode) {
		pr_debug("%s: could not get inode!\n",__FUNCTION__);
		return -ENOMEM;
	}

	root = d_alloc_root(inode);
	if (!root) {
		pr_debug("%s: could not get root dentry!\n",__FUNCTION__);
		iput(inode);
		return -ENOMEM;
	}
	sb->s_root = root;
	return 0;
}

static struct super_block *sysfs_get_sb(struct file_system_type *fs_type,
	int flags, char *dev_name, void *data)
{
	return get_sb_single(fs_type, flags, data, sysfs_fill_super);
}

static struct file_system_type sysfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "sysfs",
	.get_sb		= sysfs_get_sb,
	.kill_sb	= kill_litter_super,
};

static int __init sysfs_init(void)
{
	int err;

	err = register_filesystem(&sysfs_fs_type);
	if (!err) {
		sysfs_mount = kern_mount(&sysfs_fs_type);
		if (IS_ERR(sysfs_mount)) {
			printk(KERN_ERR "sysfs: could not mount!\n");
			err = PTR_ERR(sysfs_mount);
			sysfs_mount = NULL;
		}
	}
	return err;
}

core_initcall(sysfs_init);


static struct dentry * get_dentry(struct dentry * parent, const char * name)
{
	struct qstr qstr;

	qstr.name = name;
	qstr.len = strlen(name);
	qstr.hash = full_name_hash(name,qstr.len);
	return lookup_hash(&qstr,parent);
}


/**
 *	sysfs_create_dir - create a directory for an object.
 *	@parent:	parent parent object.
 *	@kobj:		object we're creating directory for. 
 */

int sysfs_create_dir(struct kobject * kobj)
{
	struct dentry * dentry = NULL;
	struct dentry * parent;
	int error = 0;

	if (!kobj)
		return -EINVAL;

	if (kobj->parent)
		parent = kobj->parent->dentry;
	else if (sysfs_mount && sysfs_mount->mnt_sb)
		parent = sysfs_mount->mnt_sb->s_root;
	else
		return -EFAULT;

	down(&parent->d_inode->i_sem);
	dentry = get_dentry(parent,kobj->name);
	if (!IS_ERR(dentry)) {
		dentry->d_fsdata = (void *)kobj;
		kobj->dentry = dentry;
		error = sysfs_mkdir(parent->d_inode,dentry,
				    (S_IFDIR| S_IRWXU | S_IRUGO | S_IXUGO));
	} else
		error = PTR_ERR(dentry);
	up(&parent->d_inode->i_sem);

	return error;
}


/**
 *	sysfs_create_file - create an attribute file for an object.
 *	@kobj:	object we're creating for. 
 *	@attr:	atrribute descriptor.
 */

int sysfs_create_file(struct kobject * kobj, struct attribute * attr)
{
	struct dentry * dentry;
	struct dentry * parent;
	int error = 0;

	if (!kobj || !attr)
		return -EINVAL;

	parent = kobj->dentry;

	down(&parent->d_inode->i_sem);
	dentry = get_dentry(parent,attr->name);
	if (!IS_ERR(dentry)) {
		dentry->d_fsdata = (void *)attr;
		error = sysfs_create(parent->d_inode,dentry,attr->mode);
	} else
		error = PTR_ERR(dentry);
	up(&parent->d_inode->i_sem);
	return error;
}


static int object_depth(struct kobject * kobj)
{
	struct kobject * p = kobj;
	int depth = 0;
	do { depth++; } while ((p = p->parent));
	return depth;
}

static int object_path_length(struct kobject * kobj)
{
	struct kobject * p = kobj;
	int length = 1;
	do {
		length += strlen(p->name) + 1;
		p = p->parent;
	} while (p);
	return length;
}

static void fill_object_path(struct kobject * kobj, char * buffer, int length)
{
	struct kobject * p;

	--length;
	for (p = kobj; p; p = p->parent) {
		int cur = strlen(p->name);

		/* back up enough to print this bus id with '/' */
		length -= cur;
		strncpy(buffer + length,p->name,cur);
		*(buffer + --length) = '/';
	}
}

/**
 *	sysfs_create_link - create symlink between two objects.
 *	@kobj:	object whose directory we're creating the link in.
 *	@target:	object we're pointing to.
 *	@name:		name of the symlink.
 */
int sysfs_create_link(struct kobject * kobj, struct kobject * target, char * name)
{
	struct dentry * dentry = kobj->dentry;
	struct dentry * d;
	int error = 0;
	int size;
	int depth;
	char * path;
	char * s;

	depth = object_depth(kobj);
	size = object_path_length(target) + depth * 3 - 1;
	if (size > PATH_MAX)
		return -ENAMETOOLONG;
	pr_debug("%s: depth = %d, size = %d\n",__FUNCTION__,depth,size);

	path = kmalloc(size,GFP_KERNEL);
	if (!path)
		return -ENOMEM;
	memset(path,0,size);

	for (s = path; depth--; s += 3)
		strcpy(s,"../");

	fill_object_path(target,path,size);
	pr_debug("%s: path = '%s'\n",__FUNCTION__,path);

	down(&dentry->d_inode->i_sem);
	d = get_dentry(dentry,name);
	if (!IS_ERR(d))
		error = sysfs_symlink(dentry->d_inode,d,path);
	else
		error = PTR_ERR(d);
	up(&dentry->d_inode->i_sem);
	kfree(path);
	return error;
}

static void hash_and_remove(struct dentry * dir, const char * name)
{
	struct dentry * victim;

	down(&dir->d_inode->i_sem);
	victim = get_dentry(dir,name);
	if (!IS_ERR(victim)) {
		/* make sure dentry is really there */
		if (victim->d_inode && 
		    (victim->d_parent->d_inode == dir->d_inode)) {
			simple_unlink(dir->d_inode,victim);
			d_delete(victim);

			pr_debug("sysfs: Removing %s (%d)\n", victim->d_name.name,
				 atomic_read(&victim->d_count));
			/**
			 * Drop reference from initial get_dentry().
			 */
			dput(victim);
		}
		
		/**
		 * Drop the reference acquired from get_dentry() above.
		 */
		dput(victim);
	}
	up(&dir->d_inode->i_sem);
}

/**
 * sysfs_update_file - update the modified timestamp on an object attribute.
 * @kobj: object we're acting for.
 * @attr: attribute descriptor.
 *
 * Also call dnotify for the dentry, which lots of userspace programs
 * use.
 */
int sysfs_update_file(struct kobject * kobj, struct attribute * attr)
{
	struct dentry * dir = kobj->dentry;
	struct dentry * victim;
	int res = -ENOENT;

	down(&dir->d_inode->i_sem);
	victim = get_dentry(dir, attr->name);
	if (!IS_ERR(victim)) {
		/* make sure dentry is really there */
		if (victim->d_inode && 
		    (victim->d_parent->d_inode == dir->d_inode)) {
			victim->d_inode->i_mtime = CURRENT_TIME;
			dnotify_parent(victim, DN_MODIFY);

			/**
			 * Drop reference from initial get_dentry().
			 */
			dput(victim);
			res = 0;
		}
		
		/**
		 * Drop the reference acquired from get_dentry() above.
		 */
		dput(victim);
	}
	up(&dir->d_inode->i_sem);

	return res;
}


/**
 *	sysfs_remove_file - remove an object attribute.
 *	@kobj:	object we're acting for.
 *	@attr:	attribute descriptor.
 *
 *	Hash the attribute name and kill the victim.
 */

void sysfs_remove_file(struct kobject * kobj, struct attribute * attr)
{
	hash_and_remove(kobj->dentry,attr->name);
}


/**
 *	sysfs_remove_link - remove symlink in object's directory.
 *	@kobj:	object we're acting for.
 *	@name:	name of the symlink to remove.
 */

void sysfs_remove_link(struct kobject * kobj, char * name)
{
	hash_and_remove(kobj->dentry,name);
}


/**
 *	sysfs_remove_dir - remove an object's directory.
 *	@kobj:	object. 
 *
 *	The only thing special about this is that we remove any files in 
 *	the directory before we remove the directory, and we've inlined
 *	what used to be sysfs_rmdir() below, instead of calling separately.
 */

void sysfs_remove_dir(struct kobject * kobj)
{
	struct list_head * node, * next;
	struct dentry * dentry = dget(kobj->dentry);
	struct dentry * parent;

	if (!dentry)
		return;

	pr_debug("sysfs %s: removing dir\n",dentry->d_name.name);
	parent = dget(dentry->d_parent);
	down(&parent->d_inode->i_sem);
	down(&dentry->d_inode->i_sem);

	list_for_each_safe(node,next,&dentry->d_subdirs) {
		struct dentry * d = dget(list_entry(node,struct dentry,d_child));
		/** 
		 * Make sure dentry is still there 
		 */
		pr_debug(" o %s: ",d->d_name.name);
		if (d->d_inode) {

			pr_debug("removing");
			/**
			 * Unlink and unhash.
			 */
			simple_unlink(dentry->d_inode,d);
			d_delete(d);

			/**
			 * Drop reference from initial get_dentry().
			 */
			dput(d);
		}
		pr_debug(" done (%d)\n",atomic_read(&d->d_count));
		/**
		 * drop reference from dget() above.
		 */
		dput(d);
	}

	up(&dentry->d_inode->i_sem);
	d_invalidate(dentry);
	simple_rmdir(parent->d_inode,dentry);
	d_delete(dentry);

	pr_debug(" o %s removing done (%d)\n",dentry->d_name.name,
		 atomic_read(&dentry->d_count));
	/**
	 * Drop reference from initial get_dentry().
	 */
	dput(dentry);

	/**
	 * Drop reference from dget() on entrance.
	 */
	dput(dentry);
	up(&parent->d_inode->i_sem);
	dput(parent);
}

EXPORT_SYMBOL(sysfs_create_file);
EXPORT_SYMBOL(sysfs_update_file);
EXPORT_SYMBOL(sysfs_remove_file);
EXPORT_SYMBOL(sysfs_create_link);
EXPORT_SYMBOL(sysfs_remove_link);
EXPORT_SYMBOL(sysfs_create_dir);
EXPORT_SYMBOL(sysfs_remove_dir);
