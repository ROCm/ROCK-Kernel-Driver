/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* vfs_ops.c's exported symbols */

#if !defined( __FS_REISER4_VFS_OPS_H__ )
#define __FS_REISER4_VFS_OPS_H__

#include "forward.h"
#include "coord.h"
#include "seal.h"
#include "type_safe_list.h"
#include "plugin/dir/dir.h"
#include "plugin/file/file.h"
#include "super.h"
#include "readahead.h"

#include <linux/types.h>	/* for loff_t */
#include <linux/fs.h>		/* for struct address_space */
#include <linux/dcache.h>	/* for struct dentry */
#include <linux/mm.h>
#include <linux/backing-dev.h>

extern int reiser4_mark_inode_dirty(struct inode *object);
extern int reiser4_update_sd(struct inode *object);
extern int reiser4_add_nlink(struct inode *, struct inode *, int);
extern int reiser4_del_nlink(struct inode *, struct inode *, int);

extern struct file_operations reiser4_file_operations;
extern struct inode_operations reiser4_inode_operations;
extern struct inode_operations reiser4_symlink_inode_operations;
extern struct inode_operations reiser4_special_inode_operations;
extern struct super_operations reiser4_super_operations;
extern struct export_operations reiser4_export_operations;
extern struct address_space_operations reiser4_as_operations;
extern struct dentry_operations reiser4_dentry_operations;
extern int reiser4_invalidatepage(struct page *page, unsigned long offset);
extern int reiser4_releasepage(struct page *page, int gfp);
extern int reiser4_writepages(struct address_space *, struct writeback_control *wbc);
extern int reiser4_start_up_io(struct page *page);
extern void move_inode_out_from_sync_inodes_loop(struct address_space * mapping);
extern void reiser4_clear_page_dirty(struct page *);
extern void reiser4_throttle_write(struct inode*);
/*
 * this is used to speed up lookups for directory entry: on initial call to
 * ->lookup() seal and coord of directory entry (if found, that is) are stored
 * in struct dentry and reused later to avoid tree traversals.
 */
typedef struct de_location {
	/* seal covering directory entry */
	seal_t entry_seal;
	/* coord of directory entry */
	coord_t entry_coord;
	/* ordinal number of directory entry among all entries with the same
	   key. (Starting from 0.) */
	int pos;
} de_location;

/* &reiser4_dentry_fsdata - reiser4-specific data attached to dentries.

   This is allocated dynamically and released in d_op->d_release()

   Currently it only contains cached location (hint) of directory entry, but
   it is expected that other information will be accumulated here.
*/
typedef struct reiser4_dentry_fsdata {
	/* here will go fields filled by ->lookup() to speedup next
	   create/unlink, like blocknr of znode with stat-data, or key
	   of stat-data.
	*/
	de_location dec;
	int stateless; /* created through reiser4_decode_fh, needs special
			* treatment in readdir. */
} reiser4_dentry_fsdata;

/* declare data types and manipulation functions for readdir list. */
TYPE_SAFE_LIST_DECLARE(readdir);

struct dir_cursor;

/* &reiser4_dentry_fsdata - reiser4-specific data attached to files.

   This is allocated dynamically and released in reiser4_release() */
struct reiser4_file_fsdata {
	/* pointer back to the struct file which this reiser4_file_fsdata is
	 * part of */
	struct file *back;
	/* detached cursor for stateless readdir. */
	struct dir_cursor *cursor;
	/* We need both directory and regular file parts here, because there
	   are file system objects that are files and directories. */
	struct {
		readdir_pos readdir;
		readdir_list_link linkage;
	} dir;
	/* hints to speed up operations with regular files: read and write. */
	struct {
		hint_t hint;
		/* this is set by read_extent before calling
		 * page_cache_readahead */
		void *coord;
	} reg;
	struct reiser4_file_ra_state ra;
};

TYPE_SAFE_LIST_DEFINE(readdir, reiser4_file_fsdata, dir.linkage);

extern reiser4_dentry_fsdata *reiser4_get_dentry_fsdata(struct dentry *dentry);
extern reiser4_file_fsdata *reiser4_get_file_fsdata(struct file *f);
extern void reiser4_free_dentry_fsdata(struct dentry *dentry);
extern void reiser4_free_file_fsdata(struct file *f);
extern void reiser4_free_fsdata(reiser4_file_fsdata *fsdata);

extern reiser4_file_fsdata *create_fsdata(struct file *file, int gfp);

extern void reiser4_handle_error(void);
extern int reiser4_parse_options (struct super_block *, char *);

/* __FS_REISER4_VFS_OPS_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
