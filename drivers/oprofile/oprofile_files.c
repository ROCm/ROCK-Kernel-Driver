/**
 * @file oprofile_files.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#include <linux/oprofile.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
 
#include "oprof.h"
#include "event_buffer.h"
#include "oprofile_stats.h"
 
unsigned long fs_buffer_size = 131072;
unsigned long fs_cpu_buffer_size = 8192;
unsigned long fs_buffer_watershed = 32768; /* FIXME: tune */

 
static int simple_open(struct inode * inode, struct file * filp)
{
	return 0;
}


static ssize_t cpu_type_read(struct file * file, char * buf, size_t count, loff_t * offset)
{
	unsigned long cpu_type = oprofile_cpu_type;

	return oprofilefs_ulong_to_user(&cpu_type, buf, count, offset);
}
 
 
static struct file_operations cpu_type_fops = {
	.open		= simple_open,
	.read		= cpu_type_read,
};
 
 
static ssize_t enable_read(struct file * file, char * buf, size_t count, loff_t * offset)
{
	return oprofilefs_ulong_to_user(&oprofile_started, buf, count, offset);
}


static ssize_t enable_write(struct file *file, char const * buf, size_t count, loff_t * offset)
{
	unsigned long val;
	int retval;

	if (*offset)
		return -EINVAL;

	retval = oprofilefs_ulong_from_user(&val, buf, count);
	if (retval)
		return retval;
 
	if (val)
		retval = oprofile_start();
	else
		oprofile_stop();

	if (retval)
		return retval;
	return count;
}

 
static struct file_operations enable_fops = {
	.open		= simple_open,
	.read		= enable_read,
	.write		= enable_write,
};

 
void oprofile_create_files(struct super_block * sb, struct dentry * root)
{
	oprofilefs_create_file(sb, root, "enable", &enable_fops);
	oprofilefs_create_file(sb, root, "buffer", &event_buffer_fops);
	oprofilefs_create_ulong(sb, root, "buffer_size", &fs_buffer_size);
	oprofilefs_create_ulong(sb, root, "buffer_watershed", &fs_buffer_watershed);
	oprofilefs_create_ulong(sb, root, "cpu_buffer_size", &fs_cpu_buffer_size);
	oprofilefs_create_file(sb, root, "cpu_type", &cpu_type_fops); 
	oprofile_create_stats_files(sb, root);
	if (oprofile_ops->create_files)
		oprofile_ops->create_files(sb, root);
}
