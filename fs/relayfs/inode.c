/*
 * VFS-related code for RelayFS, a high-speed data relay filesystem.
 *
 * Copyright (C) 2003 - Tom Zanussi <zanussi@us.ibm.com>, IBM Corp
 * Copyright (C) 2003 - Karim Yaghmour <karim@opersys.com>
 *
 * Based on ramfs, Copyright (C) 2002 - Linus Torvalds
 *
 * This file is released under the GPL.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/smp_lock.h>
#include <linux/backing-dev.h>
#include <linux/namei.h>
#include <linux/poll.h>
#include <asm/uaccess.h>
#include <asm/relay.h>

#define RELAYFS_MAGIC			0x26F82121

static struct super_operations		relayfs_ops;
static struct address_space_operations	relayfs_aops;
static struct inode_operations		relayfs_file_inode_operations;
static struct file_operations		relayfs_file_operations;
static struct inode_operations		relayfs_dir_inode_operations;

static struct vfsmount *		relayfs_mount;
static int				relayfs_mount_count;

static struct backing_dev_info		relayfs_backing_dev_info = {
	.ra_pages	= 0,	/* No readahead */
	.memory_backed	= 1,	/* Does not contribute to dirty memory */
};

static struct inode *
relayfs_get_inode(struct super_block *sb, int mode, dev_t dev)
{
	struct inode * inode;
	
	inode = new_inode(sb);

	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = current->fsuid;
		inode->i_gid = current->fsgid;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		inode->i_mapping->a_ops = &relayfs_aops;
		inode->i_mapping->backing_dev_info = &relayfs_backing_dev_info;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_op = &relayfs_file_inode_operations;
			inode->i_fop = &relayfs_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &relayfs_dir_inode_operations;
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

/*
 * File creation. Allocate an inode, and we're done..
 */
/* SMP-safe */
static int 
relayfs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	struct inode * inode;
	int error = -ENOSPC;

	inode = relayfs_get_inode(dir->i_sb, mode, dev);

	if (inode) {
		d_instantiate(dentry, inode);
		dget(dentry);	/* Extra count - pin the dentry in core */
		error = 0;
	}
	return error;
}

static int 
relayfs_mkdir(struct inode * dir, struct dentry * dentry, int mode)
{
	int retval;

	retval = relayfs_mknod(dir, dentry, mode | S_IFDIR, 0);

	if (!retval)
		dir->i_nlink++;
	return retval;
}

static int 
relayfs_create(struct inode *dir, struct dentry *dentry, int mode, struct nameidata *nd)
{
	return relayfs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static int 
relayfs_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = relayfs_get_inode(dir->i_sb, S_IFLNK|S_IRWXUGO, 0);

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

/**
 *	relayfs_create_entry - create a relayfs directory or file
 *	@name: the name of the file to create
 *	@parent: parent directory
 *	@dentry: result dentry
 *	@entry_type: type of file to create (S_IFREG, S_IFDIR)
 *	@mode: mode
 *	@data: data to associate with the file
 *
 *	Creates a file or directory with the specifed permissions.
 */
static int 
relayfs_create_entry(const char * name, struct dentry * parent, struct dentry **dentry, int entry_type, int mode, void * data)
{
	struct qstr qname;
	struct dentry * d;
	
	int error = 0;

	error = simple_pin_fs("relayfs", &relayfs_mount, &relayfs_mount_count);
	if (error) {
		printk(KERN_ERR "Couldn't mount relayfs: errcode %d\n", error);
		return error;
	}

	qname.name = name;
	qname.len = strlen(name);
	qname.hash = full_name_hash(name, qname.len);

	if (parent == NULL)
		if (relayfs_mount && relayfs_mount->mnt_sb)
			parent = relayfs_mount->mnt_sb->s_root;

	if (parent == NULL) {
		simple_release_fs(&relayfs_mount, &relayfs_mount_count);
 		return -EINVAL;
	}

	parent = dget(parent);
	down(&parent->d_inode->i_sem);
	d = lookup_hash(&qname, parent);
	if (IS_ERR(d)) {
		error = PTR_ERR(d);
		goto release_mount;
	}
	
	if (d->d_inode) {
		error = -EEXIST;
		goto release_mount;
	}

	if (entry_type == S_IFREG)
		error = relayfs_create(parent->d_inode, d, entry_type | mode, NULL);
	else
		error = relayfs_mkdir(parent->d_inode, d, entry_type | mode);
	if (error)
		goto release_mount;

	if ((entry_type == S_IFREG) && data) {
		d->d_inode->u.generic_ip = data;
		goto exit; /* don't release mount for regular files */
	}

release_mount:
	simple_release_fs(&relayfs_mount, &relayfs_mount_count);
exit:	
	*dentry = d;
	up(&parent->d_inode->i_sem);
	dput(parent);

	return error;
}

/**
 *	relayfs_create_file - create a file in the relay filesystem
 *	@name: the name of the file to create
 *	@parent: parent directory
 *	@dentry: result dentry
 *	@data: data to associate with the file
 *	@mode: mode, if not specied the default perms are used
 *
 *	The file will be created user rw on behalf of current user.
 */
int 
relayfs_create_file(const char * name, struct dentry * parent, struct dentry **dentry, void * data, int mode)
{
	if (!mode)
		mode = S_IRUSR | S_IWUSR;
	
	return relayfs_create_entry(name, parent, dentry, S_IFREG,
				    mode, data);
}

/**
 *	relayfs_create_dir - create a directory in the relay filesystem
 *	@name: the name of the directory to create
 *	@parent: parent directory
 *	@dentry: result dentry
 *
 *	The directory will be created world rwx on behalf of current user.
 */
int 
relayfs_create_dir(const char * name, struct dentry * parent, struct dentry **dentry)
{
	return relayfs_create_entry(name, parent, dentry, S_IFDIR,
				    S_IRWXU | S_IRUGO | S_IXUGO, NULL);
}

/**
 *	relayfs_remove_file - remove a file in the relay filesystem
 *	@dentry: file dentry
 *
 *	Remove a file previously created by relayfs_create_file.
 */
int 
relayfs_remove_file(struct dentry *dentry)
{
	struct dentry *parent;
	int is_reg;
	
	parent = dentry->d_parent;
	if (parent == NULL)
		return -EINVAL;

	is_reg = S_ISREG(dentry->d_inode->i_mode);

	parent = dget(parent);
	down(&parent->d_inode->i_sem);
	if (dentry->d_inode) {
		simple_unlink(parent->d_inode, dentry);
		d_delete(dentry);
	}
	dput(dentry);
	up(&parent->d_inode->i_sem);
	dput(parent);

	if(is_reg)
		simple_release_fs(&relayfs_mount, &relayfs_mount_count);

	return 0;
}

/**
 *	relayfs_open - open file op for relayfs files
 *	@inode: the inode
 *	@filp: the file
 *
 *	Associates the channel with the file, and increments the
 *	channel refcount.  Reads will be 'auto-consuming'.
 */
int
relayfs_open(struct inode *inode, struct file *filp)
{
	struct rchan *rchan;
	struct rchan_reader *reader;
	int retval = 0;

	if (inode->u.generic_ip) {
		rchan = (struct rchan *)inode->u.generic_ip;
		if (rchan == NULL)
			return -EACCES;
		reader = __add_rchan_reader(rchan, filp, 1, 0);
		if (reader == NULL)
			return -ENOMEM;
		filp->private_data = reader;
		retval = rchan->callbacks->fileop_notify(rchan->id, filp,
							 RELAY_FILE_OPEN);
		if (retval == 0)
			/* Inc relay channel refcount for file */
			rchan_get(rchan->id);
		else {
			__remove_rchan_reader(reader);
			retval = -EPERM;
		}
	}

	return retval;
}

/**
 *	relayfs_mmap - mmap file op for relayfs files
 *	@filp: the file
 *	@vma: the vma describing what to map
 *
 *	Calls upon relay_mmap_buffer to map the file into user space.
 */
int 
relayfs_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct rchan *rchan;
	
	rchan = ((struct rchan_reader *)filp->private_data)->rchan;

	return __relay_mmap_buffer(rchan, vma);
}

/**
 *	relayfs_file_read - read file op for relayfs files
 *	@filp: the file
 *	@buf: user buf to read into
 *	@count: bytes requested
 *	@offset: offset into file
 *
 *	Reads count bytes from the channel, or as much as is available within
 *	the sub-buffer currently being read.  Reads are 'auto-consuming'.
 *	See relay_read() for details.
 *
 *	Returns bytes read on success, 0 or -EAGAIN if nothing available,
 *	negative otherwise.
 */
ssize_t 
relayfs_file_read(struct file *filp, char * buf, size_t count, loff_t *offset)
{
	size_t read_count;
	struct rchan_reader *reader;
	u32 dummy; /* all VFS readers are auto-consuming */

	if (offset != &filp->f_pos) /* pread, seeking not supported */
		return -ESPIPE;

	if (count == 0)
		return 0;

	reader = (struct rchan_reader *)filp->private_data;
	read_count = relay_read(reader, buf, count,
		filp->f_flags & (O_NDELAY | O_NONBLOCK) ? 0 : 1, &dummy);

	return read_count;
}

/**
 *	relayfs_file_write - write file op for relayfs files
 *	@filp: the file
 *	@buf: user buf to write from
 *	@count: bytes to write
 *	@offset: offset into file
 *
 *	Reserves a slot in the relay buffer and writes count bytes
 *	into it.  The current limit for a single write is 2 pages
 *	worth.  The user_deliver() channel callback will be invoked on
 *	
 *	Returns bytes written on success, 0 or -EAGAIN if nothing available,
 *	negative otherwise.
 */
ssize_t 
relayfs_file_write(struct file *filp, const char *buf, size_t count, loff_t *offset)
{
	int write_count;
	char * write_buf;
	struct rchan *rchan;
	int err = 0;
	void *wrote_pos;
	struct rchan_reader *reader;

	reader = (struct rchan_reader *)filp->private_data;
	if (reader == NULL)
		return -EPERM;

	rchan = reader->rchan;
	if (rchan == NULL)
		return -EPERM;

	if (count == 0)
		return 0;

	/* Change this if need to write more than 2 pages at once */
	if (count > 2 * PAGE_SIZE)
		return -EINVAL;
	
	write_buf = (char *)__get_free_pages(GFP_KERNEL, 1);
	if (write_buf == NULL)
		return -ENOMEM;

	if (copy_from_user(write_buf, buf, count))
		return -EFAULT;

	if (filp->f_flags & (O_NDELAY | O_NONBLOCK)) {
		write_count = relay_write(rchan->id, write_buf, count, -1, &wrote_pos);
		if (write_count == 0)
			return -EAGAIN;
	} else {
		err = wait_event_interruptible(rchan->write_wait,
	         (write_count = relay_write(rchan->id, write_buf, count, -1, &wrote_pos)));
		if (err)
			return err;
	}
	
	free_pages((unsigned long)write_buf, 1);
	
        rchan->callbacks->user_deliver(rchan->id, wrote_pos, write_count);

	return write_count;
}

/**
 *	relayfs_ioctl - ioctl file op for relayfs files
 *	@inode: the inode
 *	@filp: the file
 *	@cmd: the command
 *	@arg: command arg
 *
 *	Passes the specified cmd/arg to the kernel client.  arg may be a 
 *	pointer to user-space data, in which case the kernel client is 
 *	responsible for copying the data to/from user space appropriately.
 *	The kernel client is also responsible for returning a meaningful
 *	return value for ioctl calls.
 *	
 *	Returns result of relay channel callback, -EPERM if unsuccessful.
 */
int
relayfs_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct rchan *rchan;
	struct rchan_reader *reader;

	reader = (struct rchan_reader *)filp->private_data;
	if (reader == NULL)
		return -EPERM;

	rchan = reader->rchan;
	if (rchan == NULL)
		return -EPERM;

	return rchan->callbacks->ioctl(rchan->id, cmd, arg);
}

/**
 *	relayfs_poll - poll file op for relayfs files
 *	@filp: the file
 *	@wait: poll table
 *
 *	Poll implemention.
 */
static unsigned int
relayfs_poll(struct file *filp, poll_table *wait)
{
	struct rchan_reader *reader;
	unsigned int mask = 0;
	
	reader = (struct rchan_reader *)filp->private_data;

	if (reader->rchan->finalized)
		return POLLERR;

	if (filp->f_mode & FMODE_READ) {
		poll_wait(filp, &reader->rchan->read_wait, wait);
		if (!rchan_empty(reader))
			mask |= POLLIN | POLLRDNORM;
	}
	
	if (filp->f_mode & FMODE_WRITE) {
		poll_wait(filp, &reader->rchan->write_wait, wait);
		if (!rchan_full(reader))
			mask |= POLLOUT | POLLWRNORM;
	}
	
	return mask;
}

/**
 *	relayfs_release - release file op for relayfs files
 *	@inode: the inode
 *	@filp: the file
 *
 *	Decrements the channel refcount, as the filesystem is
 *	no longer using it.
 */
int
relayfs_release(struct inode *inode, struct file *filp)
{
	struct rchan_reader *reader;
	struct rchan *rchan;

	reader = (struct rchan_reader *)filp->private_data;
	if (reader == NULL || reader->rchan == NULL)
		return 0;
	rchan = reader->rchan;
	
        rchan->callbacks->fileop_notify(reader->rchan->id, filp,
					RELAY_FILE_CLOSE);
	__remove_rchan_reader(reader);
	/* The channel is no longer in use as far as this file is concerned */
	rchan_put(rchan);

	return 0;
}

static struct address_space_operations relayfs_aops = {
	.readpage	= simple_readpage,
	.prepare_write	= simple_prepare_write,
	.commit_write	= simple_commit_write
};

static struct file_operations relayfs_file_operations = {
	.open		= relayfs_open,
	.read		= relayfs_file_read,
	.write		= relayfs_file_write,
	.ioctl		= relayfs_ioctl,
	.poll		= relayfs_poll,
	.mmap		= relayfs_mmap,
	.fsync		= simple_sync_file,
	.release	= relayfs_release,
};

static struct inode_operations relayfs_file_inode_operations = {
	.getattr	= simple_getattr,
};

static struct inode_operations relayfs_dir_inode_operations = {
	.create		= relayfs_create,
	.lookup		= simple_lookup,
	.link		= simple_link,
	.unlink		= simple_unlink,
	.symlink	= relayfs_symlink,
	.mkdir		= relayfs_mkdir,
	.rmdir		= simple_rmdir,
	.mknod		= relayfs_mknod,
	.rename		= simple_rename,
};

static struct super_operations relayfs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
};

static int 
relayfs_fill_super(struct super_block * sb, void * data, int silent)
{
	struct inode * inode;
	struct dentry * root;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = RELAYFS_MAGIC;
	sb->s_op = &relayfs_ops;
	inode = relayfs_get_inode(sb, S_IFDIR | 0755, 0);

	if (!inode)
		return -ENOMEM;

	root = d_alloc_root(inode);
	if (!root) {
		iput(inode);
		return -ENOMEM;
	}
	sb->s_root = root;

	return 0;
}

static struct super_block *
relayfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return get_sb_single(fs_type, flags, data, relayfs_fill_super);
}

static struct file_system_type relayfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "relayfs",
	.get_sb		= relayfs_get_sb,
	.kill_sb	= kill_litter_super,
};

static int __init 
init_relayfs_fs(void)
{
	int err = register_filesystem(&relayfs_fs_type);
#ifdef CONFIG_KLOG_CHANNEL
	if (!err)
		create_klog_channel();
#endif
	return err;
}

static void __exit 
exit_relayfs_fs(void)
{
#ifdef CONFIG_KLOG_CHANNEL
	remove_klog_channel();
#endif
	unregister_filesystem(&relayfs_fs_type);
}

module_init(init_relayfs_fs)
module_exit(exit_relayfs_fs)

MODULE_AUTHOR("Tom Zanussi <zanussi@us.ibm.com> and Karim Yaghmour <karim@opersys.com>");
MODULE_DESCRIPTION("Relay Filesystem");
MODULE_LICENSE("GPL");

