/*
 * sysfs.h - definitions for the device driver filesystem
 *
 * Copyright (c) 2001,2002 Patrick Mochel
 *
 * Please see Documentation/filesystems/sysfs.txt for more information.
 */

#ifndef _SYSFS_H_
#define _SYSFS_H_

struct driver_dir_entry;
struct attribute;

struct sysfs_ops {
	int	(*open)(struct driver_dir_entry *);
	int	(*close)(struct driver_dir_entry *);
	ssize_t	(*show)(struct driver_dir_entry *, struct attribute *,char *, size_t, loff_t);
	ssize_t	(*store)(struct driver_dir_entry *,struct attribute *,const char *, size_t, loff_t);
};

struct driver_dir_entry {
	char			* name;
	struct dentry		* dentry;
	mode_t			mode;
	struct sysfs_ops	* ops;
};

struct attribute {
	char			* name;
	mode_t			mode;
};

extern int
sysfs_create_dir(struct driver_dir_entry *, struct driver_dir_entry *);

extern void
sysfs_remove_dir(struct driver_dir_entry * entry);

extern int
sysfs_create_file(struct attribute * attr,
		     struct driver_dir_entry * parent);

extern int 
sysfs_create_symlink(struct driver_dir_entry * parent, 
			char * name, char * target);

extern void
sysfs_remove_file(struct driver_dir_entry *, const char * name);

#endif /* _SYSFS_H_ */
