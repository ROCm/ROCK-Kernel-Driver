/*  linux/fs/ext3/namei.h
 *
 * Copyright (C) 2005 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
*/

extern int ext3_permission (struct inode *, int);
extern struct dentry *ext3_get_parent(struct dentry *child);
