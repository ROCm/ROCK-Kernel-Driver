/* 
 * fs/rcfs/magic_shares.c 
 *
 * Copyright (C) Shailabh Nagar,  IBM Corp. 2004
 *           
 * 
 * virtual file for setting/getting share values of a 
 * task class. Part of resource class file system (rcfs) 
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
 * 06 Mar 2004
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


/* Token matching for parsing input to this magic file */

//NOTNEEDED #define SHARE_UNCHANGED -1

/* Next few are dependent on number of share types */

//NOTNEEDED #define NUM_SHAREVAL 4
#define SHARES_MAX_INPUT_SIZE  300

/* The enums for the share types should match the indices expected by
   array parameter to ckrm_set_resshare */

/* Note only the first NUM_SHAREVAL enums correspond to share types,
   the remaining ones are for token matching purposes */

enum share_token_t {
        MY_GUAR, MY_LIM, TOT_GUAR, TOT_LIM, RES_TYPE, SHARE_ERR
};

static match_table_t tokens = {
	{RES_TYPE, "res=%s"},
        {MY_GUAR, "guarantee=%d"},
        {MY_LIM,  "limit=%d"},
	{TOT_GUAR,"tot_guarantee=%d"},
	{TOT_LIM, "tot_limit=%d"},
        {SHARE_ERR, NULL}
};


static int shares_parse(char *options, int *resid, struct ckrm_shares *shares)
{
	char *p,resname[CKRM_MAX_RES_NAME];
	int option;


	if (!options)
		return 1;
	
	//printk(KERN_ERR "options |%s|\n",options);
	while ((p = strsep(&options, ",")) != NULL) {
		
		substring_t args[MAX_OPT_ARGS];
		int token;
		
		//printk(KERN_ERR "p |%s| options |%s|\n",p,options);

		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		//printk(KERN_ERR "Token %d\n",token);
		switch (token) {
			
		case RES_TYPE:
			
			match_strcpy(resname,args);
			printk(KERN_ERR "resname %s tried\n",resname);
			*resid = ckrm_resid_lookup(resname);
			break;
			
		case MY_GUAR:
			if (match_int(args, &option))
				return 0;
			shares->my_guarantee = option;
			break;
		case MY_LIM:
			if (match_int(args, &option))
				return 0;
			shares->my_limit = option;
			break;
		case TOT_GUAR:
			if (match_int(args, &option))
				return 0;
			shares->total_guarantee = option;
			break;
		case TOT_LIM:
			if (match_int(args, &option))
				return 0;
			shares->total_limit = option;
			break;
		default:
			return 0;
		}

	}
	return 1;
}	


static ssize_t
shares_write(struct file *file, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct inode *inode = file->f_dentry->d_inode;
	struct rcfs_inode_info *ri;
	char *optbuf;
	int i,done, resid, retval;

	//newval[NUM_SHAREVAL]; ;
	struct ckrm_shares newshares = {
		CKRM_SHARE_UNCHANGED,
		CKRM_SHARE_UNCHANGED,
		CKRM_SHARE_UNCHANGED,
		CKRM_SHARE_UNCHANGED,
		CKRM_SHARE_UNCHANGED,
		CKRM_SHARE_UNCHANGED
	};

	if ((ssize_t) count < 0 || (ssize_t) count > SHARES_MAX_INPUT_SIZE)
		return -EINVAL;
	
	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT;

	ri = RCFS_I(file->f_dentry->d_parent->d_inode);

	if (!ri || !is_core_valid((ckrm_core_class_t *)(ri->core))) {
		printk(KERN_ERR "shares_write: Error accessing core class\n");
		return -EFAULT;
	}
	
	down(&inode->i_sem);
	
	optbuf = kmalloc(SHARES_MAX_INPUT_SIZE, GFP_KERNEL);
	__copy_from_user(optbuf, buf, count);

	/* cat > shares puts in an extra newline */
	if (optbuf[count-1] == '\n')
		optbuf[count-1]='\0';

	/* Set default values */
	//for (i=0; i<NUM_SHAREVAL; i++)
	//	newval[i] = SHARE_UNCHANGED;

	done = shares_parse(optbuf, &resid, &newshares);
	if (!done) {
		printk(KERN_ERR "Error parsing shares\n");
		retval = -EINVAL;
		goto write_out;
	}

	if (resid && is_res_regd(resid)) {

#if 1
		ckrm_res_callback_t *rcbs = &ckrm_res_ctlrs[resid];
		
		if (rcbs->set_share_values) {
			retval = (*rcbs->set_share_values)
				(((ckrm_core_class_t *)(ri->core))->res_class[resid],&newshares);
			if (retval) {
				printk(KERN_ERR "shares_write: resctlr share set error\n");
				goto write_out;
			}
		}
#endif
	}
	
	printk(KERN_ERR "Set %s shares to %d %d %d %d\n",
	       ckrm_res_ctlrs[resid].res_name, 
	       newshares.my_guarantee, 
	       newshares.my_limit, 
	       newshares.total_guarantee,
	       newshares.total_limit);
      
	retval = count ;

write_out:	

	up(&inode->i_sem);
	kfree(optbuf);
	return count;
}


static int 
shares_show(struct seq_file *s, void *v)
{
	int resid,retval;
	struct ckrm_shares curshares;
	struct rcfs_inode_info *ri = s->private;

	// USEME struct rcfs_inode_info *rinfo = RCFS_I(s->private) ;

	/* Get and "display" share data for each registered resource.
	 * Data from each resource is atomic but not across resources
	 */

	if (!ri || !is_core_valid((ckrm_core_class_t *)(ri->core))) {
		printk(KERN_ERR "shares_show: Error accessing core class\n");
		return -EFAULT;
	}


	for_each_resid(resid) {
		if (is_res_regd(resid)) {

#if 1
			ckrm_res_callback_t *rcbs = &ckrm_res_ctlrs[resid];
			
			printk(KERN_ERR "Showing %s's shares %p %p\n",rcbs->res_name,ri->core,(void *)((ckrm_core_class_t *)(ri->core))->res_class[resid]);
		
			if (rcbs->get_share_values) {
				/* Copy into curshares can be removed if all RC's 
				   keep ckrm_shares allocated - getting back the ptr
				   is sufficient then. 
				*/
				retval = (*rcbs->get_share_values)
					((void *)((ckrm_core_class_t *)(ri->core))->res_class[resid],&curshares);
				if (retval) {
					printk(KERN_ERR "shares_show: resctlr share get error\n");
					goto show_out;
				}
				seq_printf(s,"res=%s %d %d %d %d\n",
					   rcbs->res_name,
					   curshares.my_guarantee, 
					   curshares.my_limit, 
					   curshares.total_guarantee,
					   curshares.total_limit);
			}
#endif
//			seq_printf(s,"Fake res output for %d\n",resid);
		} /* is_res_regd(resid) */
	} /* for_each_resid(resid) */

	return 0;

 show_out:

	seq_printf(s,"Error retrieving contents of next RC. Aborting\n");
	return 0;
}	

static int 
shares_open(struct inode *inode, struct file *file)
{
	struct rcfs_inode_info *ri;
	int ret=-EINVAL;

	if (file->f_dentry && file->f_dentry->d_parent) {
		printk(KERN_ERR "file %s parent %s\n",file->f_dentry->d_name.name, file->f_dentry->d_parent->d_name.name);

		ri = RCFS_I(file->f_dentry->d_parent->d_inode);

		ret = single_open(file, shares_show, (void *)ri);
	}

	/* Allow core class retrieval using seq_file */
	/*
	if (ret)
		((struct seq_file *)(file->private_data))->private = RCFS_I(file->f_dentry->d_parent->d_inode);
	*/

	return ret;
}

static int 
shares_close(struct inode *inode, struct file *file)
{
	return single_release(inode,file);
}

struct file_operations shares_fileops = {
	.open           = shares_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = shares_close,
	.write          = shares_write,
};


