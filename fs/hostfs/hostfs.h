/* 
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#ifndef __UM_FS_HOSTFS
#define __UM_FS_HOSTFS

#include "linux/fs.h"
#include "filehandle.h"
#include "os.h"

/* These are exactly the same definitions as in fs.h, but the names are 
 * changed so that this file can be included in both kernel and user files.
 */

#define EXTERNFS_ATTR_MODE	1
#define EXTERNFS_ATTR_UID 	2
#define EXTERNFS_ATTR_GID 	4
#define EXTERNFS_ATTR_SIZE	8
#define EXTERNFS_ATTR_ATIME	16
#define EXTERNFS_ATTR_MTIME	32
#define EXTERNFS_ATTR_CTIME	64
#define EXTERNFS_ATTR_ATIME_SET	128
#define EXTERNFS_ATTR_MTIME_SET	256
#define EXTERNFS_ATTR_FORCE	512	/* Not a change, but a change it */
#define EXTERNFS_ATTR_ATTR_FLAG	1024

struct externfs_iattr {
	unsigned int	ia_valid;
	mode_t		ia_mode;
	uid_t		ia_uid;
	gid_t		ia_gid;
	loff_t		ia_size;
	time_t		ia_atime;
	time_t		ia_mtime;
	time_t		ia_ctime;
	unsigned int	ia_attr_flags;
};

struct externfs_data {
	struct externfs_file_ops *file_ops;
	struct externfs_mount_ops *mount_ops;
};

struct externfs_inode {
	struct inode vfs_inode;
	struct externfs_file_ops *ops;
};

struct externfs_mount_ops {
	struct externfs_data *(*mount)(char *mount_arg);
	struct externfs_inode *(*init_file)(struct externfs_data *ed);
};

struct externfs_file_ops {
	int (*stat_file)(const char *path, struct externfs_data *ed, 
			 dev_t *dev_out, unsigned long long *inode_out, 
			 int *mode_out, int *nlink_out, int *uid_out, 
			 int *gid_out, unsigned long long *size_out, 
			 unsigned long *atime_out, unsigned long *mtime_out,
			 unsigned long *ctime_out, int *blksize_out, 
			 unsigned long long *blocks_out);
	int (*file_type)(const char *path, int *rdev, 
			 struct externfs_data *ed);
	int (*access_file)(char *path, int r, int w, int x, int uid, int gid, 
			   struct externfs_data *ed);
	int (*open_file)(struct externfs_inode *ext, char *file, 
			 int uid, int gid, struct inode *inode, 
			 struct externfs_data *ed);
	void (*close_file)(struct externfs_inode *ext, 
			   unsigned long long size);
	void *(*open_dir)(char *path, int uid, int gid, 
			  struct externfs_data *ed);
	char *(*read_dir)(void *stream, unsigned long long *pos, 
			  unsigned long long *ino_out, int *len_out, 
			  struct externfs_data *ed);
	int (*read_file)(struct externfs_inode *ext, 
			 unsigned long long offset, char *buf, int len, 
			 int ignore_start, int ignore_end,
			 void (*completion)(char *, int, void *), void *arg, 
			 struct externfs_data *ed);
	int (*write_file)(struct externfs_inode *ext, 
			  unsigned long long offset, const char *buf, 
			  int start, int len, 
			  void (*completion)(char *, int, void *), void *arg, 
			  struct externfs_data *ed);
	int (*map_file_page)(struct externfs_inode *ext, 
			     unsigned long long offset, char *buf, int w, 
			     struct externfs_data *ed);
	void (*close_dir)(void *stream, struct externfs_data *ed);
	void (*invisible)(struct externfs_inode *ext);
	int (*create_file)(struct externfs_inode *ext, char *path, 
			   int mode, int uid, int gid, struct inode *inode, 
			   struct externfs_data *ed);
	int (*set_attr)(const char *path, struct externfs_iattr *attrs, 
			struct externfs_data *ed);
	int (*make_symlink)(const char *from, const char *to, int uid, int gid,
			    struct externfs_data *ed);
	int (*unlink_file)(const char *path, struct externfs_data *ed);
	int (*make_dir)(const char *path, int mode, int uid, int gid, 
			struct externfs_data *ed);
	int (*remove_dir)(const char *path, int uid, int gid, 
			  struct externfs_data *ed);
	int (*make_node)(const char *path, int mode, int uid, int gid, 
			 int type, int maj, int min, struct externfs_data *ed);
	int (*link_file)(const char *to, const char *from, int uid, int gid, 
			 struct externfs_data *ed);
	int (*read_link)(char *path, int uid, int gid, char *buf, int size, 
			 struct externfs_data *ed);
	int (*rename_file)(char *from, char *to, struct externfs_data *ed);
	int (*statfs)(long *bsize_out, long long *blocks_out, 
		      long long *bfree_out, long long *bavail_out, 
		      long long *files_out, long long *ffree_out,
		      void *fsid_out, int fsid_size, long *namelen_out, 
		      long *spare_out, struct externfs_data *ed);
	int (*truncate_file)(struct externfs_inode *ext, __u64 size, 
			     struct externfs_data *ed);
};

#define HOSTFS_BUFSIZE 64

extern int register_externfs(char *name, struct externfs_mount_ops *mount_ops);
extern void unregister_externfs(char *name);
extern void init_externfs(struct externfs_data *ed, 
			  struct externfs_file_ops *ops);
struct externfs_data *inode_externfs_info(struct inode *inode);

extern char *generic_root_filename(char *mount_arg);
extern void host_close_file(void *stream);
extern int host_read_file(int fd, unsigned long long offset, char *buf, 
			  int len);
extern int host_open_file(const char *path[], int r, int w,
			  struct file_handle *fh);
extern void *host_open_dir(const char *path[]);
extern char *host_read_dir(void *stream, unsigned long long *pos, 
			   unsigned long long *ino_out, int *len_out);
extern int host_file_type(const char *path[], int *rdev);
extern char *host_root_filename(char *mount_arg);
extern char *get_path(const char *path[], char *buf, int size);
extern void free_path(const char *buf, char *tmp);
extern int host_create_file(const char *path[], int mode, 
			    struct file_handle *fh);
extern int host_set_attr(const char *path[], struct externfs_iattr *attrs);
extern int host_make_symlink(const char *from[], const char *to);
extern int host_unlink_file(const char *path[]);
extern int host_make_dir(const char *path[], int mode);
extern int host_remove_dir(const char *path[]);
extern int host_link_file(const char *to[], const char *from[]);
extern int host_read_link(const char *path[], char *buf, int size);
extern int host_rename_file(const char *from[], const char *to[]);
extern int host_stat_fs(const char *path[], long *bsize_out, 
			long long *blocks_out, long long *bfree_out, 
			long long *bavail_out, long long *files_out, 
			long long *ffree_out, void *fsid_out, int fsid_size, 
			long *namelen_out, long *spare_out);
extern int host_stat_file(const char *path[], int *dev_out, 
			  unsigned long long *inode_out, int *mode_out, 
			  int *nlink_out, int *uid_out, int *gid_out, 
			  unsigned long long *size_out, 
			  unsigned long *atime_out, unsigned long *mtime_out,
			  unsigned long *ctime_out, int *blksize_out,
			  unsigned long long *blocks_out);

extern char *generic_host_read_dir(void *stream, unsigned long long *pos, 
			      unsigned long long *ino_out, int *len_out, 
			      void *mount);
extern int generic_host_read_file(int fd, unsigned long long offset, char *buf,
			     int len, void *mount);
extern void generic_host_close_file(void *stream, unsigned long long size,
				    void *mount);
extern int generic_host_truncate_file(struct file_handle *fh, __u64 size, 
				      void *m);

extern char *inode_name_prefix(struct inode *inode, char *prefix);

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
