/* Copyright 2003 by Hans Reiser, licensing governed by reiser4/README */

#if !defined(__REISER4_PSEUDO_FILE_H__)
#define __REISER4_PSEUDO_FILE_H__

#include "../plugin.h"

#include <linux/fs.h>

extern int open_pseudo(struct inode * inode, struct file * file);
extern ssize_t read_pseudo(struct file *file,
			   char __user *buf, size_t size, loff_t *ppos);
extern ssize_t write_pseudo(struct file *file,
			    const char __user *buf, size_t size, loff_t *ppos);
extern loff_t seek_pseudo(struct file *file, loff_t offset, int origin);
extern int release_pseudo(struct inode *inode, struct file *file);
extern void drop_pseudo(struct inode * object);

extern int wire_size_pseudo(struct inode *inode);
extern char *wire_write_pseudo(struct inode *inode, char *start);
extern char *wire_read_pseudo(char *addr, reiser4_object_on_wire *obj);
extern void wire_done_pseudo(reiser4_object_on_wire *obj);
extern struct dentry *wire_get_pseudo(struct super_block *sb,
				      reiser4_object_on_wire *obj);

/* __REISER4_PSEUDO_FILE_H__ */
#endif

/*
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/

