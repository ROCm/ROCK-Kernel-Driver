/* 
 * Copyright (C) 2004 Piotr Neuman (sikkh@wp.pl) and 
 * Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#ifndef __UM_FS_METADATA
#define __UM_FS_METADATA

#include "linux/fs.h"
#include "linux/list.h"
#include "os.h"
#include "hostfs.h"

struct humfs {
	struct externfs_data ext;
	__u64 used;
	__u64 total;
	char *data;
	int mmap;
	int direct;
	struct humfs_meta_ops *meta;
};

struct humfs_file {
	struct humfs *mount;
	struct file_handle data;
	struct externfs_inode ext;
};

struct humfs_meta_ops {
	struct list_head list;
	char *name;
	struct humfs_file *(*init_file)(void);
	int (*open_file)(struct humfs_file *hf, const char *path, 
			 struct inode *inode, struct humfs *humfs);
	int (*create_file)(struct humfs_file *hf, const char *path, int mode, 
			   int uid, int gid, struct inode *inode, 
			   struct humfs *humfs);
	void (*close_file)(struct humfs_file *humfs);
	int (*ownerships)(const char *path, int *mode_out, int *uid_out, 
			  int *gid_out, char *type_out, int *maj_out, 
			  int *min_out, struct humfs *humfs);
	int (*make_node)(const char *path, int mode, int uid, int gid,
			 int type, int major, int minor, struct humfs *humfs);
	int (*create_link)(const char *to, const char *from, 
			   struct humfs *humfs);
	int (*remove_file)(const char *path, struct humfs *humfs);
	int (*create_dir)(const char *path, int mode, int uid, int gid, 
			  struct humfs *humfs);
	int (*remove_dir)(const char *path, struct humfs *humfs);
	int (*change_ownerships)(const char *path, int mode, int uid, int gid,
				 struct humfs *humfs);
	int (*rename_file)(const char *from, const char *to, 
			   struct humfs *humfs);
	void (*invisible)(struct humfs_file *hf);
	struct humfs *(*init_mount)(char *root);
	void (*free_mount)(struct humfs *humfs);
};

extern void register_meta(struct humfs_meta_ops *ops);
extern void unregister_meta(struct humfs_meta_ops *ops);

extern char *humfs_path(char *dir, char *file);
extern char *humfs_name(struct inode *inode, char *prefix);
extern struct humfs *inode_humfs_info(struct inode *inode);

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
