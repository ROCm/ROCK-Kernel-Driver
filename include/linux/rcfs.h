#ifndef _LINUX_RCFS_H
#define _LINUX_RCFS_H

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/ckrm.h>
#include <linux/ckrm_rc.h>
#include <linux/ckrm_ce.h>



/* The following declarations cannot be included in any of ckrm*.h files without 
   jumping hoops. Remove later when rearrangements done */

// Hubertus .. taken out 
//extern ckrm_res_callback_t ckrm_res_ctlrs[CKRM_MAX_RES_CTLRS];

#define RCFS_MAGIC	0x4feedbac
#define RCFS_MAGF_NAMELEN 20
extern int RCFS_IS_MAGIC;

#define rcfs_is_magic(dentry)  ((dentry)->d_fsdata == &RCFS_IS_MAGIC)

typedef struct rcfs_inode_info {
	ckrm_core_class_t *core;
	char *name;
	struct inode vfs_inode;
} rcfs_inode_info_t;

#define RCFS_DEFAULT_DIR_MODE	(S_IFDIR | S_IRUGO | S_IXUGO)
#define RCFS_DEFAULT_FILE_MODE	(S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP |S_IROTH)


struct rcfs_magf {
	char name[RCFS_MAGF_NAMELEN];
	int mode;
	struct inode_operations *i_op;
	struct file_operations *i_fop;
};

struct rcfs_mfdesc {
	struct rcfs_magf *rootmf;     // Root directory and its magic files
	int              rootmflen;   // length of above array
	// Can have a different magf describing magic files for non-root entries too
};

extern struct rcfs_mfdesc *genmfdesc[];

inline struct rcfs_inode_info *RCFS_I(struct inode *inode);

int rcfs_empty(struct dentry *);
struct inode *rcfs_get_inode(struct super_block *, int, dev_t);
int rcfs_mknod(struct inode *, struct dentry *, int, dev_t);
int _rcfs_mknod(struct inode *, struct dentry *, int , dev_t);
int rcfs_mkdir(struct inode *, struct dentry *, int);
ckrm_core_class_t *rcfs_make_core(struct dentry *, struct ckrm_core_class *);
struct dentry *rcfs_set_magf_byname(char *, void *);

struct dentry * rcfs_create_internal(struct dentry *, struct rcfs_magf *, int);
int rcfs_delete_internal(struct dentry *);
int rcfs_create_magic(struct dentry *, struct rcfs_magf *, int);
int rcfs_clear_magic(struct dentry *);


extern struct super_operations rcfs_super_ops;
extern struct address_space_operations rcfs_aops;

extern struct inode_operations rcfs_dir_inode_operations;
extern struct inode_operations rcfs_rootdir_inode_operations;
extern struct inode_operations rcfs_file_inode_operations;


extern struct file_operations target_fileops;
extern struct file_operations shares_fileops;
extern struct file_operations stats_fileops;
extern struct file_operations config_fileops;
extern struct file_operations members_fileops;
extern struct file_operations rcfs_file_operations;

// Callbacks into rcfs from ckrm 

typedef struct rcfs_functions {
	int  (* mkroot)(struct rcfs_magf *,int, struct dentry **);
	int  (* rmroot)(struct dentry *);
	int  (* register_classtype)(ckrm_classtype_t *);
	int  (* deregister_classtype)(ckrm_classtype_t *);
} rcfs_fn_t;

int rcfs_register_classtype(ckrm_classtype_t *);
int rcfs_deregister_classtype(ckrm_classtype_t *);
int rcfs_mkroot(struct rcfs_magf *, int , struct dentry **);
int rcfs_rmroot(struct dentry *);

#define RCFS_ROOT "/rcfs"         // Hubertus .. we should use the mount point instead of hardcoded
extern struct dentry *rcfs_rootde;


#endif /* _LINUX_RCFS_H */ 
