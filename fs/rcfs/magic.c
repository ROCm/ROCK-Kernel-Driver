/* 
 * fs/rcfs/magic.c 
 *
 * Copyright (C) Shailabh Nagar,      IBM Corp. 2004
 *           (C) Vivek Kashyap,       IBM Corp. 2004
 *           (C) Chandra Seetharaman, IBM Corp. 2004
 *           (C) Hubertus Franke,     IBM Corp. 2004
 * 
 * File operations for common magic files in rcfs, 
 * the user interface for CKRM. 
 * 
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
 * 23 Apr 2004
 *        Created from code kept earlier in fs/rcfs/magic_*.c
 *
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <asm/namei.h>
#include <linux/namespace.h>
#include <linux/dcache.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/smp_lock.h>
#include <linux/parser.h>
#include <asm/uaccess.h>

#include <linux/rcfs.h>




/******************************************************
 * Macros
 *
 * generic macros to assist in writing magic fileops
 *
 *****************************************************/


#define MAGIC_SHOW(FUNC)                                               \
static int                                                             \
FUNC ## _show(struct seq_file *s, void *v)			       \
{								       \
	int rc=0;						       \
	ckrm_core_class_t *core ;				       \
								       \
	core = (ckrm_core_class_t *)                                   \
		(((struct rcfs_inode_info *)s->private)->core);	       \
								       \
	if (!ckrm_is_core_valid(core)) {			       \
		return -EINVAL;					       \
        }                                                              \
                                                                       \
	if (core->classtype->show_ ## FUNC)			       \
		rc = (* core->classtype->show_ ## FUNC)(core, s);      \
								       \
	return rc;						       \
};                                                                      
 

#define MAGIC_OPEN(FUNC)                                               \
static int                                                             \
FUNC ## _open(struct inode *inode, struct file *file)                  \
{                                                                      \
	struct rcfs_inode_info *ri;                                    \
	int ret=-EINVAL;                                               \
								       \
	if (file->f_dentry && file->f_dentry->d_parent) {	       \
								       \
		ri = RCFS_I(file->f_dentry->d_parent->d_inode);	       \
		ret = single_open(file,FUNC ## _show, (void *)ri);     \
	}							       \
	return ret;						       \
}								       
								       
#define MAGIC_CLOSE(FUNC)                                              \
static int                                                             \
FUNC ## _close(struct inode *inode, struct file *file)		       \
{								       \
	return single_release(inode,file);			       \
}
								       


#define MAGIC_PARSE(FUNC)                                              \
static int                                                             \
FUNC ## _parse(char *options, char **resstr, char **otherstr)	       \
{								       \
	char *p;						       \
								       \
	if (!options)						       \
		return 1;					       \
								       \
	while ((p = strsep(&options, ",")) != NULL) {		       \
		substring_t args[MAX_OPT_ARGS];			       \
		int token;					       \
								       \
		if (!*p)					       \
			continue;				       \
								       \
		token = match_token(p, FUNC##_tokens, args);           \
		switch (token) {				       \
		case FUNC ## _res_type:			               \
			*resstr = match_strdup(args);		       \
			break;					       \
		case FUNC ## _str:			               \
			*otherstr = match_strdup(args);		       \
			break;					       \
		default:					       \
			return 0;				       \
		}                                                      \
	}                                                              \
	return 1;                                                      \
}

#define MAGIC_WRITE(FUNC,CLSTYPEFUN)                                   \
static ssize_t                                                         \
FUNC ## _write(struct file *file, const char __user *buf,	       \
			   size_t count, loff_t *ppos)		       \
{								       \
	struct rcfs_inode_info *ri = 				       \
		RCFS_I(file->f_dentry->d_parent->d_inode);	       \
	char *optbuf, *otherstr=NULL, *resname=NULL;		       \
	int done, rc = 0;					       \
	ckrm_core_class_t *core ;				       \
								       \
	core = ri->core;					       \
	if (!ckrm_is_core_valid(core)) 				       \
		return -EINVAL;					       \
								       \
	if ((ssize_t) count < 0 				       \
	    || (ssize_t) count > FUNC ## _max_input_size)              \
		return -EINVAL;					       \
								       \
	if (!access_ok(VERIFY_READ, buf, count))		       \
		return -EFAULT;					       \
								       \
	down(&(ri->vfs_inode.i_sem));				       \
								       \
	optbuf = kmalloc(FUNC ## _max_input_size, GFP_KERNEL);         \
	__copy_from_user(optbuf, buf, count);			       \
	if (optbuf[count-1] == '\n')				       \
		optbuf[count-1]='\0';				       \
								       \
	done = FUNC ## _parse(optbuf, &resname, &otherstr);            \
								       \
	if (!done) {						       \
		printk(KERN_ERR "Error parsing FUNC \n");	       \
		goto FUNC ## _write_out;			       \
	}							       \
								       \
	if (core->classtype-> CLSTYPEFUN) {		               \
		rc = (*core->classtype->CLSTYPEFUN)	               \
			(core, resname, otherstr);		       \
		if (rc) {					       \
			printk(KERN_ERR "FUNC_write: CLSTYPEFUN error\n");   \
			goto FUNC ## _write_out; 	               \
		}						       \
	}							       \
								       \
FUNC ## _write_out:						       \
	up(&(ri->vfs_inode.i_sem));				       \
	kfree(optbuf);						       \
	kfree(otherstr);					       \
	kfree(resname);						       \
	return rc ? rc : count;					       \
}
								       
								       
#define MAGIC_RD_FILEOPS(FUNC)                                         \
struct file_operations FUNC ## _fileops = {                            \
	.open           = FUNC ## _open,			       \
	.read           = seq_read,				       \
	.llseek         = seq_lseek,				       \
	.release        = FUNC ## _close,			       \
};                                                                     \
EXPORT_SYMBOL(FUNC ## _fileops);

								       
#define MAGIC_RDWR_FILEOPS(FUNC)                                       \
struct file_operations FUNC ## _fileops = {                            \
	.open           = FUNC ## _open,			       \
	.read           = seq_read,				       \
	.llseek         = seq_lseek,				       \
	.release        = FUNC ## _close,			       \
	.write          = FUNC ## _write,	                       \
};                                                                     \
EXPORT_SYMBOL(FUNC ## _fileops);


/********************************************************************************
 * Target
 *
 * pseudo file for manually reclassifying members to a class
 *
 *******************************************************************************/

#define TARGET_MAX_INPUT_SIZE 100

static ssize_t
target_write(struct file *file, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct rcfs_inode_info *ri= RCFS_I(file->f_dentry->d_inode);
	char *optbuf;
	int rc = -EINVAL;
	ckrm_classtype_t *clstype;


	if ((ssize_t) count < 0 || (ssize_t) count > TARGET_MAX_INPUT_SIZE)
		return -EINVAL;
	
	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT;
	
	down(&(ri->vfs_inode.i_sem));
	
	optbuf = kmalloc(TARGET_MAX_INPUT_SIZE, GFP_KERNEL);
	__copy_from_user(optbuf, buf, count);
	if (optbuf[count-1] == '\n')
		optbuf[count-1]='\0';

	clstype = ri->core->classtype;
	if (clstype->forced_reclassify)
		rc = (* clstype->forced_reclassify)(ri->core,optbuf);

	up(&(ri->vfs_inode.i_sem));
	kfree(optbuf);
	return !rc ? count : rc;

}

struct file_operations target_fileops = {
	.write          = target_write,
};
EXPORT_SYMBOL(target_fileops);



/********************************************************************************
 * Config
 *
 * Set/get configuration parameters of a class. 
 *
 *******************************************************************************/

/* Currently there are no per-class config parameters defined.
 * Use existing code as a template
 */
								       
#define config_max_input_size  300

enum config_token_t {
         config_str, config_res_type, config_err
};

static match_table_t config_tokens = {
	{config_res_type,"res=%s"},
	{config_str, "config=%s"},
        {config_err, NULL},
};


MAGIC_PARSE(config);
MAGIC_WRITE(config,set_config);
MAGIC_SHOW(config);
MAGIC_OPEN(config);
MAGIC_CLOSE(config);

MAGIC_RDWR_FILEOPS(config);


/********************************************************************************
 * Members
 *
 * List members of a class
 *
 *******************************************************************************/

MAGIC_SHOW(members);
MAGIC_OPEN(members);
MAGIC_CLOSE(members);

MAGIC_RD_FILEOPS(members);


/********************************************************************************
 * Stats
 *
 * Get/reset class statistics
 * No standard set of stats defined. Each resource controller chooses
 * its own set of statistics to maintain and export.
 *
 *******************************************************************************/

#define stats_max_input_size  50

enum stats_token_t {
         stats_res_type, stats_str,stats_err
};

static match_table_t stats_tokens = {
	{stats_res_type,"res=%s"},
	{stats_str, NULL},
        {stats_err, NULL},
};


MAGIC_PARSE(stats);
MAGIC_WRITE(stats,reset_stats);
MAGIC_SHOW(stats);
MAGIC_OPEN(stats);
MAGIC_CLOSE(stats);

MAGIC_RDWR_FILEOPS(stats);


/********************************************************************************
 * Shares
 *
 * Set/get shares of a taskclass.
 * Share types and semantics are defined by rcfs and ckrm core 
 * 
 *******************************************************************************/


#define SHARES_MAX_INPUT_SIZE  300

/* The enums for the share types should match the indices expected by
   array parameter to ckrm_set_resshare */

/* Note only the first NUM_SHAREVAL enums correspond to share types,
   the remaining ones are for token matching purposes */

enum share_token_t {
        MY_GUAR, MY_LIM, TOT_GUAR, MAX_LIM, SHARE_RES_TYPE, SHARE_ERR
};

/* Token matching for parsing input to this magic file */
static match_table_t shares_tokens = {
	{SHARE_RES_TYPE, "res=%s"},
        {MY_GUAR, "guarantee=%d"},
        {MY_LIM,  "limit=%d"},
	{TOT_GUAR,"total_guarantee=%d"},
	{MAX_LIM, "max_limit=%d"},
        {SHARE_ERR, NULL}
};


static int
shares_parse(char *options, char **resstr, struct ckrm_shares *shares)
{
	char *p;
	int option;

	if (!options)
		return 1;
	
	while ((p = strsep(&options, ",")) != NULL) {
		
		substring_t args[MAX_OPT_ARGS];
		int token;
		
		if (!*p)
			continue;

		token = match_token(p, shares_tokens, args);
		switch (token) {
		case SHARE_RES_TYPE:
			*resstr = match_strdup(args);
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
		case MAX_LIM:
			if (match_int(args, &option))
				return 0;
			shares->max_limit = option;
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
	int rc = 0;
	struct ckrm_core_class *core;
	int done;
	char *resname;

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

	if (!ri || !ckrm_is_core_valid((ckrm_core_class_t *)(ri->core))) {
		printk(KERN_ERR "shares_write: Error accessing core class\n");
		return -EFAULT;
	}
	
	down(&inode->i_sem);
	
	core = ri->core; 
	optbuf = kmalloc(SHARES_MAX_INPUT_SIZE, GFP_KERNEL);
	__copy_from_user(optbuf, buf, count);
	if (optbuf[count-1] == '\n')
		optbuf[count-1]='\0';

	done = shares_parse(optbuf, &resname, &newshares);
	if (!done) {
		printk(KERN_ERR "Error parsing shares\n");
		rc = -EINVAL;
		goto write_out;
	}

	if (core->classtype->set_shares) {
		rc = (*core->classtype->set_shares)(core,resname,&newshares);
		if (rc) {
			printk(KERN_ERR "shares_write: resctlr share set error\n");
			goto write_out;
		}
	}
	
	printk(KERN_ERR "Set %s shares to %d %d %d %d\n",
	       resname,
	       newshares.my_guarantee, 
	       newshares.my_limit, 
	       newshares.total_guarantee,
	       newshares.max_limit);
      
	rc = count ;

write_out:	

	up(&inode->i_sem);
	kfree(optbuf);
	kfree(resname);
	return rc;
}


MAGIC_SHOW(shares);
MAGIC_OPEN(shares);
MAGIC_CLOSE(shares);

MAGIC_RDWR_FILEOPS(shares);



/*
 * magic file creation/deletion
 *
 */


int 
rcfs_clear_magic(struct dentry *parent)
{
	struct dentry *mftmp, *mfdentry ;

	list_for_each_entry_safe(mfdentry, mftmp, &parent->d_subdirs, d_child) {
		
		if (!rcfs_is_magic(mfdentry))
			continue ;

		if (rcfs_delete_internal(mfdentry)) 
			printk(KERN_ERR "rcfs_clear_magic: error deleting one\n");
	}

	return 0;
  
}
EXPORT_SYMBOL(rcfs_clear_magic);


int 
rcfs_create_magic(struct dentry *parent, struct rcfs_magf magf[], int count)
{
	int i;
	struct dentry *mfdentry;

	for (i=0; i<count; i++) {
		mfdentry = rcfs_create_internal(parent, &magf[i],0);
		if (IS_ERR(mfdentry)) {
			rcfs_clear_magic(parent);
			return -ENOMEM;
		}
		RCFS_I(mfdentry->d_inode)->core = RCFS_I(parent->d_inode)->core;
		mfdentry->d_fsdata = &RCFS_IS_MAGIC;
		if (magf[i].i_fop)
			mfdentry->d_inode->i_fop = magf[i].i_fop;
		if (magf[i].i_op)
			mfdentry->d_inode->i_op = magf[i].i_op;
	}
	return 0;
}
EXPORT_SYMBOL(rcfs_create_magic);
