/*
 *  coda_fs_i.h
 *
 *  Copyright (C) 1998 Carnegie Mellon University
 *
 */

#ifndef _LINUX_CODA_FS_I
#define _LINUX_CODA_FS_I

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/list.h>
#include <linux/coda.h>

/*
 * coda fs inode data
 */
struct coda_inode_info {
        struct ViceFid     c_fid;	/* Coda identifier */
        u_short	           c_flags;     /* flags (see below) */
	struct list_head   c_cilist;    /* list of all coda inodes */
	struct file	  *c_container; /* container file for this cnode */
	unsigned int       c_contcount; /* refcount for container file */
        struct coda_cred   c_cached_cred; /* credentials of cached perms */
        unsigned int       c_cached_perm; /* cached access permissions */
};

/* flags */
#define C_VATTR       0x1   /* Validity of vattr in inode */
#define C_FLUSH       0x2   /* used after a flush */
#define C_DYING       0x4   /* from venus (which died) */
#define C_PURGE       0x8

int coda_cnode_make(struct inode **, struct ViceFid *, struct super_block *);
int coda_cnode_makectl(struct inode **inode, struct super_block *sb);
struct inode *coda_fid_to_inode(ViceFid *fid, struct super_block *sb);
void coda_replace_fid(struct inode *, ViceFid *, ViceFid *);

#endif
#endif
