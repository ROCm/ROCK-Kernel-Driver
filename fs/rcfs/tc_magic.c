/* 
 * fs/rcfs/tc_magic.c 
 *
 * Copyright (C) Shailabh Nagar,      IBM Corp. 2004
 *           (C) Vivek Kashyap,       IBM Corp. 2004
 *           (C) Chandra Seetharaman, IBM Corp. 2004
 *           (C) Hubertus Franke,     IBM Corp. 2004
 *           
 * 
 * define magic fileops for taskclass classtype
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
 *        Created.
 *
 */

#include <linux/rcfs.h>
#include <linux/ckrm_tc.h>


/*******************************************************************************
 * Taskclass general
 *
 * Define structures for taskclass root directory and its magic files 
 * In taskclasses, there is one set of magic files, created automatically under
 * the taskclass root (upon classtype registration) and each directory (class) 
 * created subsequently. However, classtypes can also choose to have different 
 * sets of magic files created under their root and other directories under root
 * using their mkdir function. RCFS only provides helper functions for creating 
 * the root directory and its magic files
 * 
 *******************************************************************************/

#define TC_FILE_MODE (S_IFREG | S_IRUGO | S_IWUSR) 
	
#define NR_TCROOTMF  6
struct rcfs_magf tc_rootdesc[NR_TCROOTMF] = {
	/* First entry must be root */
	{ 
//		.name    = should not be set, copy from classtype name
		.mode    = RCFS_DEFAULT_DIR_MODE,
		.i_op    = &rcfs_dir_inode_operations,
		.i_fop   = &simple_dir_operations,
	},
	/* Rest are root's magic files */
	{ 
		.name    =  "target", 
		.mode    = TC_FILE_MODE, 
		.i_fop   = &target_fileops,
		.i_op    = &rcfs_file_inode_operations,
	},
	{ 
		.name    =  "config", 
		.mode    = TC_FILE_MODE, 
		.i_fop   = &config_fileops, 
		.i_op    = &rcfs_file_inode_operations,
	},
	{ 
		.name    =  "members", 
		.mode    = TC_FILE_MODE, 
		.i_fop   = &members_fileops,
		.i_op    = &rcfs_file_inode_operations,
	},
	{ 
		.name    =  "stats", 
		.mode    = TC_FILE_MODE, 
		.i_fop   = &stats_fileops, 
		.i_op    = &rcfs_file_inode_operations,
	},
	{ 
		.name    =  "shares", 
		.mode    = TC_FILE_MODE,
		.i_fop   = &shares_fileops, 
		.i_op    = &rcfs_file_inode_operations,
	},
};

struct rcfs_mfdesc tc_mfdesc = {
	.rootmf          = tc_rootdesc,
	.rootmflen       = NR_TCROOTMF,
};


