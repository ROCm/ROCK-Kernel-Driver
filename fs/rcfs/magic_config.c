/* 
 * fs/rcfs/magic_config.c 
 *
 * Copyright (C) Shailabh Nagar,  IBM Corp. 2004
 *           
 * 
 * virtual file for setting/getting configuration values of a 
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

#include <linux/rcfs.h>
#include "magic.h"

/* Currently there are no per-class config parameters defined.
 * 
 */


#define CONFIG_MAX_INPUT_SIZE  300

enum config_token_t {
         CONFIG_STR, RES_TYPE, CONFIG_ERR
};

static match_table_t tokens = {
	{RES_TYPE, "res=%s"},
	{CONFIG_STR, "config=%s"},
        {CONFIG_ERR, NULL},
};

static int config_parse(char *options, int *resid, char **cfgstr)
{
	char *p,resname[CKRM_MAX_RES_NAME];

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
			*resid = ckrm_resid_lookup(resname);
			break;
			
		case CONFIG_STR:
			
			*cfgstr = match_strdup(args);
			break;

		default:
			return 0;
		}

	}
	return 1;
}	

/* Poorly written currently. Strings with spaces will not be accepted */
/* Parser.c isn't buying us much here. Manual parsing might be better */

static ssize_t
config_write(struct file *file, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct rcfs_inode_info *ri = RCFS_I(file->f_dentry->d_inode);
	char *optbuf, *cfgstr;
	int done;
	int resid ;

	if ((ssize_t) count < 0 || (ssize_t) count > CONFIG_MAX_INPUT_SIZE)
		return -EINVAL;
	
	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT;
	
	down(&(ri->vfs_inode.i_sem));
	
	optbuf = kmalloc(CONFIG_MAX_INPUT_SIZE, GFP_KERNEL);
	__copy_from_user(optbuf, buf, count);

	/* cat > shares puts in an extra newline */
	if (optbuf[count-1] == '\n')
		optbuf[count-1]='\0';

	done = config_parse(optbuf, &resid, &cfgstr);

	if (!done) {
		printk(KERN_ERR "Error parsing config \n");
		goto config_out;
	}

#if 0
	/* Error control ? */
	if (resid && ckrm_isregd(resid)) {
		if (ckrm_res_ctlrs[resid].set_config)
			(*ckrm_res_ctlrs[resid].set_config)(ri->core->resclass[resid],cfgstr) ;

	}				
#endif	
	
	printk(KERN_ERR "Set %d's config using %s\n", resid, cfgstr);
	
config_out:
	
	up(&(ri->vfs_inode.i_sem));
	kfree(optbuf);
	kfree(cfgstr);
	return count;
}

static int 
config_show(struct seq_file *s, void *v)
{
	int resid;

	// USEME struct rcfs_inode_info *rinfo = RCFS_I(s->private) ;

	/* Get and "display" config data for each registered resource.
	 * Data from each resource is atomic but not across resources
	 */
	
	for_each_resid(resid) {

/*		if (ckrm_isregd(resid) && 
		    ckrm_get_res_ctrlrs[resid].get_config) {
		    if ((*(ckrm_get_res_ctrlrs[resid].get_config)(s)) < 0)
		           goto show_out;
                }
*/
		seq_printf(s, "Showing configs 1 2 3 for res %d\n",resid);
	}

	return 0;

//show_out:

	seq_printf(s,"Error retrieving contents of next RC. Aborting\n");
	return 0;
}	

static int 
config_open(struct inode *inode, struct file *file)
{
	int ret = single_open(file, config_show, file);

	/* Store inode in seq_file->private to allow  core class retrieval later
	   in seq_file start
	   If seq_file not used, inode is directly available on read */

	/* USEME
	if (ret)
		((struct seq_file *)(file->private_data))->private = inode;
	*/

	return ret;
}

static int 
config_close(struct inode *inode, struct file *file)
{
	return single_release(inode,file);
}

struct file_operations config_fileops = {
	.open           = config_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = config_close,
	.write          = config_write,
};


