/* 
 * fs/rcfs/magic_stats.c 
 *
 * Copyright (C) Shailabh Nagar,  IBM Corp. 2004
 *           
 * 
 * virtual file for getting all statistics for a task class 
 * in rcfs.
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
 * 08 Mar 2004
 *        Created.
 *
 */

#include <linux/fs.h>
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
#include "MOVETOCORE.h"
#include "magic.h"

static int 
stats_show(struct seq_file *s, void *v)
{
	int resid,retval;
	struct rcfs_inode_info *ri = s->private;

	/* Get and "display" statistics for each registered resource.
	 * Data from each resource is "atomic" (depends on RC) 
	 * but not across resources
	 */

	if (!ri || !is_core_valid((ckrm_core_class_t *)(ri->core))) {
		printk(KERN_ERR "stats_show: Error accessing core class\n");
		return -EFAULT;
	}

	for_each_resid(resid) {
		if (is_res_regd(resid)) {

#if 1
			ckrm_res_callback_t *rcbs = &ckrm_res_ctlrs[resid];
			
			printk(KERN_ERR "Showing %s's stats %p %p\n",rcbs->res_name,ri->core,(void *)((ckrm_core_class_t *)(ri->core))->res_class[resid]);
		
			if (rcbs->get_stats) {
				retval = (*rcbs->get_stats)
					((void *)((ckrm_core_class_t *)(ri->core))->res_class[resid],s);
				if (retval) {
					printk(KERN_ERR "stats_show: resctlr share get error\n");
					goto show_out;
				}
			}
#endif
		} /* is_res_regd(resid) */
	} /* for_each_resid(resid) */

	return 0;

show_out:

	seq_printf(s,"Error retrieving stats of next RC. Aborting\n");
	return 0;
}	

static int 
stats_open(struct inode *inode, struct file *file)
{
	struct rcfs_inode_info *ri;
	int ret=-EINVAL;

	if (file->f_dentry && file->f_dentry->d_parent) {

		printk(KERN_ERR "file %s parent %s\n",
		       file->f_dentry->d_name.name, 
		       file->f_dentry->d_parent->d_name.name);

		ri = RCFS_I(file->f_dentry->d_parent->d_inode);
		ret = single_open(file, stats_show, (void *)ri);
	}
	return ret;
}

static int 
stats_close(struct inode *inode, struct file *file)
{
	return single_release(inode,file);
}

struct file_operations stats_fileops = {
	.open           = stats_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = stats_close,
};


