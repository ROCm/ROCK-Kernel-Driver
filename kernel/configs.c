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

static char *IKCONFIG_NAME = "ikconfig";
static char *IKCONFIG_VERSION = "0.5";

static int ikconfig_current_size = 0;
static struct proc_dir_entry *ikconfig_dir, *current_config, *built_with;

static int
ikconfig_permission_current(struct inode *inode, int op, struct nameidata *nd)
{
	/* anyone can read the device, no one can write to it */
	return (op == MAY_READ) ? 0 : -EACCES;
}

static ssize_t
ikconfig_output_current(struct file *file, char *buf,
			 size_t len, loff_t * offset)
{
	int i, limit;
	int cnt;

	limit = (ikconfig_current_size > len) ? len : ikconfig_current_size;
	for (i = file->f_pos, cnt = 0;
	     i < ikconfig_current_size && cnt < limit; i++, cnt++) {
		if (put_user(ikconfig_config[i], buf + cnt))
			return -EFAULT;
	}
	file->f_pos = i;
	return cnt;
}

static int
ikconfig_open_current(struct inode *inode, struct file *file)
{
	if (file->f_mode & FMODE_READ) {
		inode->i_size = ikconfig_current_size;
		file->f_pos = 0;
	}
	return 0;
}

static int
ikconfig_close_current(struct inode *inode, struct file *file)
{
	return 0;
}

static struct file_operations ikconfig_file_ops = {
	.read = ikconfig_output_current,
	.open = ikconfig_open_current,
	.release = ikconfig_close_current,
};

static struct inode_operations ikconfig_inode_ops = {
	.permission = ikconfig_permission_current,
};

/***************************************************/
/* proc_read_built_with: let people read the info  */
/* we have on the tools used to build this kernel  */

static int
proc_read_built_with(char *page, char **start,
		     off_t off, int count, int *eof, void *data)
{
	*eof = 1;
	return sprintf(page,
			"Kernel:    %s\nCompiler:  %s\nVersion_in_Makefile: %s\n",
			ikconfig_built_with, LINUX_COMPILER, UTS_RELEASE);
}

/***************************************************/
/* ikconfig_init: start up everything we need to */

int __init
ikconfig_init(void)
{
	int result = 0;

	printk(KERN_INFO "ikconfig %s with /proc/ikconfig\n",
	       IKCONFIG_VERSION);

	/* create the ikconfig directory */
	ikconfig_dir = proc_mkdir(IKCONFIG_NAME, NULL);
	if (ikconfig_dir == NULL) {
		result = -ENOMEM;
		goto leave;
	}
	ikconfig_dir->owner = THIS_MODULE;

	/* create the current config file */
	current_config = create_proc_entry("config", S_IFREG | S_IRUGO,
					   ikconfig_dir);
	if (current_config == NULL) {
		result = -ENOMEM;
		goto leave2;
	}
	current_config->proc_iops = &ikconfig_inode_ops;
	current_config->proc_fops = &ikconfig_file_ops;
	current_config->owner = THIS_MODULE;
	ikconfig_current_size = strlen(ikconfig_config);
	current_config->size = ikconfig_current_size;

	/* create the "built with" file */
	built_with = create_proc_read_entry("built_with", 0444, ikconfig_dir,
					    proc_read_built_with, NULL);
	if (built_with == NULL) {
		result = -ENOMEM;
		goto leave3;
	}
	built_with->owner = THIS_MODULE;
	goto leave;

leave3:
	/* remove the file from proc */
	remove_proc_entry("config", ikconfig_dir);

leave2:
	/* remove the ikconfig directory */
	remove_proc_entry(IKCONFIG_NAME, NULL);

leave:
	return result;
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
