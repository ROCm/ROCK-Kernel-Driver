/* 
 * fs/rcfs/magic_target.c 
 *
 * Copyright (C) Shailabh Nagar,  IBM Corp. 2004
 *           
 * 
 * virtual file assisting in reclassification in rcfs. 
 * 
 * Writing a pid to a class's target file reclassifies the corresponding
 * task to the class.
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

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/parser.h>
#include <asm/uaccess.h>

#include "rcfs.h"
#include "MOVETOCORE.h"
#include "magic.h"


#define TARGET_MAX_INPUT_SIZE  300

/* The enums for the share types should match the indices expected by
   array parameter to ckrm_set_resshare */

/* Note only the first NUM_SHAREVAL enums correspond to share types,
   the remaining ones are for token matching purposes */

enum target_token_t {
        PID, RES_TYPE, TARGET_ERR
};

static match_table_t tokens = {
        {PID, "pid=%u"},
        {TARGET_ERR, NULL},
};


static int target_parse(char *options, pid_t *mpid )
{
	char *p;
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
			
		case PID:
			if (match_int(args, &option))
				return 0;
			*mpid = (pid_t)(option);
			break;
		default:
			return 0;
		}

	}
	return 1;
}	


static ssize_t
target_write(struct file *file, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct rcfs_inode_info *ri= RCFS_I(file->f_dentry->d_inode);
	char *optbuf;
	int done;
	pid_t mpid;
	struct task_struct *mtsk;
	

	if ((ssize_t) count < 0 || (ssize_t) count > TARGET_MAX_INPUT_SIZE)
		return -EINVAL;
	
	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT;
	
	down(&(ri->vfs_inode.i_sem));
	
	optbuf = kmalloc(TARGET_MAX_INPUT_SIZE, GFP_KERNEL);
	__copy_from_user(optbuf, buf, count);

	/* cat > shares puts in an extra newline */
	if (optbuf[count-1] == '\n')
		optbuf[count-1]='\0';

	done = target_parse(optbuf, &mpid);

	if (!done) {
		printk(KERN_ERR "Error parsing target \n");
		goto target_out;
	}

	/* USEMELATER */
	   
	mtsk = find_task_by_pid(mpid);
	if (!mtsk) {
		printk(KERN_ERR "No such pid \n");
		goto target_out;
	}

#if 0
	/* Error control ? */
	if (callbacks_active)
		if (ckrm_eng_callbacks.relinquish_tsk)
			(*ckrm_eng_callbacks.relinquish_tsk)(mtsk) ;
				
	/* Error control ? */
	/* Do manual reclassification after CE relinquishes control to
	   avoid races with other operations which might reclassify task
	 */
	ckrm_reclassify_task(mtsk, ri->core);
#endif
	
	
	printk(KERN_ERR "Reclassifying task pid %d cmd %s \n",
	       (int) mpid, mtsk->comm);
	

target_out:	

	up(&(ri->vfs_inode.i_sem));
	kfree(optbuf);
	return count;
}



struct file_operations target_fileops = {
	.write          = target_write,
};


