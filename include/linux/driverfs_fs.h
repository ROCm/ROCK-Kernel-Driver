/*
 * driverfs_fs.h - definitions for the device driver filesystem
 *
 * Copyright (c) 2001 Patrick Mochel <mochel@osdl.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * This is a simple, ram-based filesystem, which allows kernel
 * callbacks for read/write of files.
 *
 * Please see Documentation/filesystems/driverfs.txt for more information.
 */

#ifndef _DRIVER_FS_H_
#define _DRIVER_FS_H_

struct driverfs_operations {
	ssize_t	(*read) (char *, size_t, loff_t, void *);
	ssize_t (*write)(const char *, size_t, loff_t, void*);
};

struct driver_dir_entry {
	char			* name;
	struct dentry		* dentry;
	mode_t			mode;
	struct list_head	files;
};

struct driver_file_entry {
	struct driver_dir_entry * parent;
	struct list_head	node;
	char			* name;
	mode_t			mode;
	struct dentry		* dentry;
	void			* data;
	struct driverfs_operations	* ops;
};

extern struct driver_dir_entry *
driverfs_create_dir_entry(const char * name, mode_t mode);

extern int
driverfs_create_dir(struct driver_dir_entry *, struct driver_dir_entry *);

extern void
driverfs_remove_dir(struct driver_dir_entry * entry);

extern struct driver_file_entry *
driverfs_create_entry (const char * name, mode_t mode,
		       struct driverfs_operations * ops, void * data);

extern int
driverfs_create_file(struct driver_file_entry * entry,
		     struct driver_dir_entry * parent);

extern void
driverfs_remove_file(struct driver_dir_entry *, const char * name);

extern int init_driverfs_fs(void);

#endif /* _DDFS_H_ */
