/*
 * kernel/configs.c
 * Echo the kernel .config file used to build the kernel
 *
 * Copyright (C) 2002 Khalid Aziz <khalid_aziz@hp.com>
 * Copyright (C) 2002 Randy Dunlap <rddunlap@osdl.org>
 * Copyright (C) 2002 Al Stone <ahs3@fc.hp.com>
 * Copyright (C) 2002 Hewlett-Packard Company
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/compile.h>
#include <linux/version.h>
#include <asm/uaccess.h>

/**************************************************/
/* the actual current config file                 */

#include "ikconfig.h"

#ifdef CONFIG_IKCONFIG_PROC

/**************************************************/
/* globals and useful constants                   */

static const char IKCONFIG_NAME[] = "ikconfig";
static const char IKCONFIG_VERSION[] = "0.6";

static int ikconfig_size;
static struct proc_dir_entry *ikconfig_dir;

static ssize_t
ikconfig_read(struct file *file, char __user *buf, 
		   size_t len, loff_t *offset)
{
	loff_t pos = *offset;
	ssize_t count;
	
	if (pos >= ikconfig_size)
		return 0;

	count = min(len, (size_t)(ikconfig_size - pos));
	if(copy_to_user(buf, ikconfig_config + pos, count))
		return -EFAULT;

	*offset += count;
	return count;
}

static struct file_operations config_fops = {
	.owner = THIS_MODULE,
	.read  = ikconfig_read,
};

/***************************************************/
/* built_with_show: let people read the info  */
/* we have on the tools used to build this kernel  */

static int builtwith_show(struct seq_file *seq, void *v)
{
	seq_printf(seq, 
		   "Kernel:    %s\nCompiler:  %s\nVersion_in_Makefile: %s\n",
		   ikconfig_built_with, LINUX_COMPILER, UTS_RELEASE);
	return 0;
}

static int built_with_open(struct inode *inode, struct file *file)
{
	return single_open(file, builtwith_show, PDE(inode)->data);
}
	
static struct file_operations builtwith_fops = {
	.owner = THIS_MODULE,
	.open  = built_with_open,
	.read  = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};	

/***************************************************/
/* ikconfig_init: start up everything we need to */

int __init
ikconfig_init(void)
{
	struct proc_dir_entry *entry;

	printk(KERN_INFO "ikconfig %s with /proc/ikconfig\n",
	       IKCONFIG_VERSION);

	/* create the ikconfig directory */
	ikconfig_dir = proc_mkdir(IKCONFIG_NAME, NULL);
	if (ikconfig_dir == NULL) 
		goto leave;
	ikconfig_dir->owner = THIS_MODULE;

	/* create the current config file */
	entry = create_proc_entry("config", S_IFREG | S_IRUGO, ikconfig_dir);
	if (!entry)
		goto leave2;

	entry->proc_fops = &config_fops;
	entry->size = ikconfig_size = strlen(ikconfig_config);

	/* create the "built with" file */
	entry = create_proc_entry("built_with", S_IFREG | S_IRUGO,
				  ikconfig_dir);
	if (!entry)
		goto leave3;
	entry->proc_fops = &builtwith_fops;

	return 0;

leave3:
	/* remove the file from proc */
	remove_proc_entry("config", ikconfig_dir);

leave2:
	/* remove the ikconfig directory */
	remove_proc_entry(IKCONFIG_NAME, NULL);

leave:
	return -ENOMEM;
}

/***************************************************/
/* cleanup_ikconfig: clean up our mess           */

static void
cleanup_ikconfig(void)
{
	/* remove the files */
	remove_proc_entry("config", ikconfig_dir);
	remove_proc_entry("built_with", ikconfig_dir);

	/* remove the ikconfig directory */
	remove_proc_entry(IKCONFIG_NAME, NULL);
}

module_init(ikconfig_init);
module_exit(cleanup_ikconfig);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Randy Dunlap");
MODULE_DESCRIPTION("Echo the kernel .config file used to build the kernel");

#endif /* CONFIG_IKCONFIG_PROC */
