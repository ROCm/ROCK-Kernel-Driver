/* Copyright 2001, 2002, 2003, 2004 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Directory plugin using hashes (see fs/reiser4/plugin/hash.c) to map
   file names to to files. See hashed_dir.c */

#if !defined( __HASHED_DIR_H__ )
#define __HASHED_DIR_H__

#include "../../forward.h"

#include <linux/fs.h>		/* for struct inode */
#include <linux/dcache.h>	/* for struct dentry */

/* create sd for directory file. Create stat-data, dot, and dotdot. */
extern int init_hashed(struct inode *object, struct inode *parent, reiser4_object_create_data *);
extern int done_hashed(struct inode *object);
extern int detach_hashed(struct inode *object, struct inode *parent);
extern int owns_item_hashed(const struct inode *inode, const coord_t * coord);
extern int lookup_hashed(struct inode *inode, struct dentry **dentry);
extern int rename_hashed(struct inode *old_dir,
			 struct dentry *old_name, struct inode *new_dir, struct dentry *new_name);
extern int add_entry_hashed(struct inode *object,
			    struct dentry *where, reiser4_object_create_data *, reiser4_dir_entry_desc * entry);
extern int rem_entry_hashed(struct inode *object, struct dentry *where, reiser4_dir_entry_desc * entry);
extern reiser4_block_nr	estimate_rename_hashed(struct inode  *old_dir,
					       struct dentry *old_name,
					       struct inode  *new_dir,
					       struct dentry *new_name);
extern reiser4_block_nr estimate_unlink_hashed(struct inode *parent,
					       struct inode *object);

extern struct dentry *get_parent_hashed(struct inode *child);

/* __HASHED_DIR_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
