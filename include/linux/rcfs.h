#ifndef _LINUX_RCFS_H
#define _LINUX_RCFS_H

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/ckrm.h>
#include <linux/ckrm_rc.h>
#include <linux/ckrm_res.h>
#include <linux/ckrm_ce.h>



/* The following declarations cannot be included in any of ckrm*.h files without 
   jumping hoops. Remove later when rearrangements done */

extern ckrm_res_callback_t ckrm_res_ctlrs[CKRM_MAX_RES_CTLRS];

#define RCFS_MAGIC	0x4feedbac


typedef struct rcfs_inode_info {
	/* USEME ckrm_core_class_t *core */
	void *core;
	struct inode vfs_inode;
} rcfs_inode_info_t;


inline struct rcfs_inode_info *RCFS_I(struct inode *inode);


struct inode *rcfs_get_inode(struct super_block *sb, int mode, dev_t dev);
int rcfs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev);
int rcfs_mkdir(struct inode *dir, struct dentry *dentry, int mode);
struct dentry * rcfs_create_internal(struct dentry *parent, const char *name, int mfmode, 
				     int magic);
void rcfs_make_core(struct dentry *sp, struct ckrm_core_class *core);


int rcfs_delete_internal(struct dentry *mfdentry);
int rcfs_clear_magic(struct dentry *parent);


extern struct super_operations rcfs_super_ops;
extern struct address_space_operations rcfs_aops;

extern struct inode_operations rcfs_dir_inode_operations;
extern struct inode_operations rcfs_file_inode_operations;

extern struct file_operations target_fileops;
extern struct file_operations shares_fileops;
extern struct file_operations stats_fileops;
extern struct file_operations config_fileops;
extern struct file_operations members_fileops;
extern struct file_operations rcfs_file_operations;

#endif /* _LINUX_RCFS_H */ 
