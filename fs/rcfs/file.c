/* 
 * fs/rcfs/inode.c 
 *
 * Copyright (C) Shailabh Nagar,  IBM Corp. 2004
 *           
 * 
 * Resource class filesystem (rcfs) forming the 
 * user interface to Class-based Kernel Resource Management (CKRM).
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
 * 05 Mar 2004
 *        Created.
 * 06 Mar 2004
 *        Parsing for shares added
 */


#include <linux/module.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/namespace.h>
#include <linux/dcache.h>
#include <linux/seq_file.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/smp_lock.h>
#include <linux/backing-dev.h>
#include <linux/parser.h>

#include <asm/uaccess.h>

#include <linux/rcfs.h>
#include "magic.h"



/* Magic file info */

#define MAG_FILE_MODE (S_IFREG | S_IRUGO | S_IWUSR) 
#define MAG_DIR_MODE  (S_IFDIR | S_IRUGO | S_IXUGO) 
 	

struct magf_t magf[NR_MAGF] = {
	{ 
		.name    =  "target", 
		.mode    = MAG_FILE_MODE, 
		.i_fop    = &target_fileops, 
	},
	{ 
		.name    =  "shares", 
		.mode    = MAG_FILE_MODE, 
		.i_fop    = &shares_fileops, 
	},
	{ 
		.name    =  "stats", 
		.mode    = MAG_FILE_MODE, 
		.i_fop    = &stats_fileops, 
	},
	{ 
		.name    =  "config", 
		.mode    = MAG_FILE_MODE, 
		.i_fop    = &config_fileops, 
	},
	{ 
		.name    =  "members", 
		.mode    = MAG_FILE_MODE, 
		.i_fop   = &members_fileops,
	}
};


/* rcfs has no files to handle except magic files  */
/* So the generic file ops here can be deleted once the code is working */


/*******************************Generic file ops ********************/


struct address_space_operations rcfs_aops = {
	.readpage	= simple_readpage,
	.prepare_write	= simple_prepare_write,
	.commit_write	= simple_commit_write
};

struct file_operations rcfs_file_operations = {
//	.open           = rcfs_open,
//	.read           = seq_read,
//	.llseek         = seq_lseek,
//	.release        = seq_release,
//	.write          = generic_file_write,
//	.read		= generic_file_read,
//	.write		= generic_file_write,
//	.mmap		= generic_file_mmap,
//	.fsync		= simple_sync_file,
//	.sendfile	= generic_file_sendfile,
//	.llseek		= generic_file_llseek,
};

