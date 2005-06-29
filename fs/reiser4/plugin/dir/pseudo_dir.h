/* Copyright 2003, 2004 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Directory plugin for pseudo files. See pseudo_dir.c for details. */

#if !defined( __PSEUDO_DIR_H__ )
#define __PSEUDO_DIR_H__

#include "../../forward.h"

#include <linux/fs.h>		/* for struct inode */
#include <linux/dcache.h>	/* for struct dentry */

extern int lookup_pseudo(struct inode * parent, struct dentry **dentry);
extern int readdir_pseudo(struct file *f, void *dirent, filldir_t filld);
extern struct dentry *get_parent_pseudo(struct inode *child);

/* __PSEUDO_DIR_H__ */
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
