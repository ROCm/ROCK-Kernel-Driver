/* 
 * fs/rcfs/magic_members.c 
 *
 * Copyright (C) Shailabh Nagar,  IBM Corp. 2004
 *           
 * 
 * virtual file for getting pids belonging to a class
 * Part of resource class file system (rcfs) 
 * interface to Class-based Kernel Resource Management (CKRM).
 *
 * Latest version, more details at http://ckrm.sf.net
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

/* Changes
 *
 * 12 Mar 2004
 *        Created.
 *
 */

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/namei.h>
#include <linux/namespace.h>
#include <linux/dcache.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/smp_lock.h>
#include <linux/parser.h>
#include <asm/uaccess.h>

#include "rcfs.h"



static int 
members_show(struct seq_file *s, void *v)
{
	int retval;
	ckrm_core_class_t *core ;
	struct list_head *lh;
	struct task_struct *tsk;

	/* Get and "display" statistics for each registered resource.
	 * Data from each resource is "atomic" (depends on RC) 
	 * but not across resources
	 */

	core = (ckrm_core_class_t *)(((struct rcfs_inode_info *)s->private)->core);

	if (!is_core_valid(core))
		return -EINVAL;

	seq_printf(s,"Printing tasks in members of %p\n",core);
	
	spin_lock(&core->ckrm_lock);
//	read_lock(&tasklist_lock);
//	for_each_process(tsk) {
	list_for_each(lh, &core->tasklist) {	
		tsk = container_of(lh, struct task_struct, ckrm_link);
		seq_printf(s,"%ld\n", (long)tsk->pid);
	}
//	read_unlock(&tasklist_lock);
	spin_unlock(&core->ckrm_lock);

	return 0;
}	

static int 
members_open(struct inode *inode, struct file *file)
{
	struct rcfs_inode_info *ri;
	int ret=-EINVAL;

	if (file->f_dentry && file->f_dentry->d_parent) {

		ri = RCFS_I(file->f_dentry->d_parent->d_inode);
		printk(KERN_ERR "file %s parent %s %p %p\n",
		       file->f_dentry->d_name.name, 
		       file->f_dentry->d_parent->d_name.name,
		       (void *)ri->core, (void *)&ckrm_dflt_class);

		ret = single_open(file, members_show, (void *)ri);
	}
	return ret;
}


static int 
members_close(struct inode *inode, struct file *file)
{
	return single_release(inode,file);
}

struct file_operations members_fileops = {
	.open           = members_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = members_close,
};

