/* Copyright 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Declaration of object plugin functions. */

#if !defined( __FS_REISER4_PLUGIN_OBJECT_H__ )
#define __FS_REISER4_PLUGIN_OBJECT_H__

#include "../forward.h"

#include <linux/fs.h>		/* for struct inode */
#include <linux/types.h>

extern int locate_inode_sd(struct inode *inode,
			   reiser4_key *key, coord_t *coord, lock_handle *lh);
extern int lookup_sd(struct inode *inode, znode_lock_mode lock_mode,
		     coord_t * coord, lock_handle * lh, const reiser4_key * key,
		     int silent);
extern int guess_plugin_by_mode(struct inode *inode);

extern int write_sd_by_inode_common(struct inode *inode);
extern int owns_item_common(const struct inode *inode,
			    const coord_t * coord);
extern reiser4_block_nr estimate_update_common(const struct inode *inode);
extern int safelink_common(struct inode *object,
			   reiser4_safe_link_t link, __u64 value);
extern int prepare_write_common (struct file *, struct page *, unsigned, unsigned);
extern int key_by_inode_and_offset_common(struct inode *, loff_t, reiser4_key *);
extern int setattr_reserve_common(reiser4_tree *);
extern int setattr_common(struct inode *, struct iattr *);

extern reiser4_plugin_ops cryptcompress_plugin_ops;

/* __FS_REISER4_PLUGIN_OBJECT_H__ */
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
