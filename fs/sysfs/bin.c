/*
 * bin.c - binary file operations for sysfs.
 */

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/kobject.h>

#include <asm/uaccess.h>

#include "sysfs.h"

static struct file_operations bin_fops;

static int fill_read(struct file * file, struct sysfs_bin_buffer * buffer)
{
	struct bin_attribute * attr = file->f_dentry->d_fsdata;
	struct kobject * kobj = file->f_dentry->d_parent->d_fsdata;

	if (!buffer->data)
		attr->read(kobj,buffer);
	return buffer->size ? 0 : -ENOENT;
}

static int flush_read(struct file * file, char * userbuf, 
		      struct sysfs_bin_buffer * buffer)
{
	return copy_to_user(userbuf,buffer->data + buffer->offset,buffer->count) ? 
		-EFAULT : 0;
}

static ssize_t
read(struct file * file, char * userbuf, size_t count, loff_t * off)
{
	struct sysfs_bin_buffer * buffer = file->private_data;
	int ret;

	ret = fill_read(file,buffer);
	if (ret) 
		goto Done;

	buffer->offset = *off;

	if (count > (buffer->size - *off))
		count = buffer->size - *off;

	buffer->count = count;

	ret = flush_read(file,userbuf,buffer);
	if (!ret) {
		*off += count;
		ret = count;
	}
 Done:
	if (buffer && buffer->data) {
		kfree(buffer->data);
		buffer->data = NULL;
	}
	return ret;
}

int alloc_buf_data(struct sysfs_bin_buffer * buffer)
{
	buffer->data = kmalloc(buffer->count,GFP_KERNEL);
	if (buffer->data) {
		memset(buffer->data,0,buffer->count);
		return 0;
	} else
		return -ENOMEM;
}

static int fill_write(struct file * file, const char * userbuf, 
		      struct sysfs_bin_buffer * buffer)
{
	return copy_from_user(buffer->data,userbuf,buffer->count) ?
		-EFAULT : 0;
}

static int flush_write(struct file * file, const char * userbuf, 
		       struct sysfs_bin_buffer * buffer)
{
	struct bin_attribute * attr = file->f_dentry->d_fsdata;
	struct kobject * kobj = file->f_dentry->d_parent->d_fsdata;

	return attr->write(kobj,buffer);
}

static ssize_t write(struct file * file, const char * userbuf,
		     size_t count, loff_t * off)
{
	struct sysfs_bin_buffer * buffer = file->private_data;
	int ret;

	if (count > PAGE_SIZE)
		count = PAGE_SIZE;
	buffer->count = count;

	ret = alloc_buf_data(buffer);
	if (ret)
		goto Done;

	ret = fill_write(file,userbuf,buffer);
	if (ret)
		goto Done;

	ret = flush_write(file,userbuf,buffer);
	if (ret > 0)
		*off += count;
 Done:
	if (buffer->data) {
		kfree(buffer->data);
		buffer->data = NULL;
	}
	return ret;
}

static int check_perm(struct inode * inode, struct file * file)
{
	struct kobject * kobj = kobject_get(file->f_dentry->d_parent->d_fsdata);
	struct bin_attribute * attr = file->f_dentry->d_fsdata;
	struct sysfs_bin_buffer * buffer;
	int error = 0;

	if (!kobj || !attr)
		goto Einval;

	/* File needs write support.
	 * The inode's perms must say it's ok, 
	 * and we must have a store method.
	 */
	if (file->f_mode & FMODE_WRITE) {
		if (!(inode->i_mode & S_IWUGO) || !attr->write)
			goto Eaccess;
	}

	/* File needs read support.
	 * The inode's perms must say it's ok, and we there
	 * must be a show method for it.
	 */
	if (file->f_mode & FMODE_READ) {
		if (!(inode->i_mode & S_IRUGO) || !attr->read)
			goto Eaccess;
	}

	buffer = kmalloc(sizeof(struct sysfs_bin_buffer),GFP_KERNEL);
	if (buffer) {
		memset(buffer,0,sizeof(struct sysfs_bin_buffer));
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

static int open(struct inode * inode, struct file * file)
{
	return check_perm(inode,file);
}

static int release(struct inode * inode, struct file * file)
{
	struct kobject * kobj = file->f_dentry->d_parent->d_fsdata;
	u8 * buffer = file->private_data;

	if (kobj) 
		kobject_put(kobj);
	if (buffer)
		kfree(buffer);
	return 0;
}

static struct file_operations bin_fops = {
	.read		= read,
	.write		= write,
	.llseek		= generic_file_llseek,
	.open		= open,
	.release	= release,
};

/**
 *	sysfs_create_bin_file - create binary file for object.
 *	@kobj:	object.
 *	@attr:	attribute descriptor.
 *
 */

int sysfs_create_bin_file(struct kobject * kobj, struct bin_attribute * attr)
{
	struct dentry * dentry;
	struct dentry * parent;
	int error = 0;

	if (!kobj || !attr)
		return -EINVAL;

	parent = kobj->dentry;

	down(&parent->d_inode->i_sem);
	dentry = sysfs_get_dentry(parent,attr->attr.name);
	if (!IS_ERR(dentry)) {
		dentry->d_fsdata = (void *)attr;
		error = sysfs_create(dentry,
				     (attr->attr.mode & S_IALLUGO) | S_IFREG,
				     NULL);
		if (!error) {
			dentry->d_inode->i_size = attr->size;
			dentry->d_inode->i_fop = &bin_fops;
		}
	} else
		error = PTR_ERR(dentry);
	up(&parent->d_inode->i_sem);
	return error;
}


/**
 *	sysfs_remove_bin_file - remove binary file for object.
 *	@kobj:	object.
 *	@attr:	attribute descriptor.
 *
 */

int sysfs_remove_bin_file(struct kobject * kobj, struct bin_attribute * attr)
{
	sysfs_hash_and_remove(kobj->dentry,attr->attr.name);
	return 0;
}

EXPORT_SYMBOL(sysfs_create_bin_file);
EXPORT_SYMBOL(sysfs_remove_bin_file);
