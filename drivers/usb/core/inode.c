/*****************************************************************************/

/*
 *	inode.c  --  Inode/Dentry functions for the USB device file system.
 *
 *	Copyright (C) 2000 Thomas Sailer (sailer@ife.ee.ethz.ch)
 *	Copyright (c) 2001 Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  History:
 *   0.1  04.01.2000  Created
 *   0.2  10.12.2001  converted to use the vfs layer better
 */

/*****************************************************************************/

#define __NO_VERSION__
#include <linux/config.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/usb.h>
#include <linux/namei.h>
#include <linux/usbdevice_fs.h>
#include <linux/smp_lock.h>
#include <asm/byteorder.h>

static struct super_operations usbfs_ops;
static struct file_operations default_file_operations;
static struct inode_operations usbfs_dir_inode_operations;
static struct vfsmount *usbfs_mount;
static spinlock_t mount_lock = SPIN_LOCK_UNLOCKED;
static int mount_count;	/* = 0 */

static struct dentry *devices_dentry;
static int num_buses;	/* = 0 */

static uid_t devuid;	/* = 0 */
static uid_t busuid;	/* = 0 */
static uid_t listuid;	/* = 0 */
static gid_t devgid;	/* = 0 */
static gid_t busgid;	/* = 0 */
static gid_t listgid;	/* = 0 */
static umode_t devmode = S_IWUSR | S_IRUGO;
static umode_t busmode = S_IXUGO | S_IRUGO;
static umode_t listmode = S_IRUGO;

static int parse_options(struct super_block *s, char *data)
{
	char *curopt = NULL, *value;

	while ((curopt = strsep(&data, ",")) != NULL) {
		if (!*curopt)
			continue;
		if ((value = strchr(curopt, '=')) != NULL)
			*value++ = 0;
		if (!strcmp(curopt, "devuid")) {
			if (!value || !value[0])
				return -EINVAL;
			devuid = simple_strtoul(value, &value, 0);
			if (*value)
				return -EINVAL;
		}
		if (!strcmp(curopt, "devgid")) {
			if (!value || !value[0])
				return -EINVAL;
			devgid = simple_strtoul(value, &value, 0);
			if (*value)
				return -EINVAL;
		}
		if (!strcmp(curopt, "devmode")) {
			if (!value || !value[0])
				return -EINVAL;
			devmode = simple_strtoul(value, &value, 0) & S_IRWXUGO;
			if (*value)
				return -EINVAL;
		}
		if (!strcmp(curopt, "busuid")) {
			if (!value || !value[0])
				return -EINVAL;
			busuid = simple_strtoul(value, &value, 0);
			if (*value)
				return -EINVAL;
		}
		if (!strcmp(curopt, "busgid")) {
			if (!value || !value[0])
				return -EINVAL;
			busgid = simple_strtoul(value, &value, 0);
			if (*value)
				return -EINVAL;
		}
		if (!strcmp(curopt, "busmode")) {
			if (!value || !value[0])
				return -EINVAL;
			busmode = simple_strtoul(value, &value, 0) & S_IRWXUGO;
			if (*value)
				return -EINVAL;
		}
		if (!strcmp(curopt, "listuid")) {
			if (!value || !value[0])
				return -EINVAL;
			listuid = simple_strtoul(value, &value, 0);
			if (*value)
				return -EINVAL;
		}
		if (!strcmp(curopt, "listgid")) {
			if (!value || !value[0])
				return -EINVAL;
			listgid = simple_strtoul(value, &value, 0);
			if (*value)
				return -EINVAL;
		}
		if (!strcmp(curopt, "listmode")) {
			if (!value || !value[0])
				return -EINVAL;
			listmode = simple_strtoul(value, &value, 0) & S_IRWXUGO;
			if (*value)
				return -EINVAL;
		}
	}

	return 0;
}


/* --------------------------------------------------------------------- */

static struct inode *usbfs_get_inode (struct super_block *sb, int mode, int dev)
{
	struct inode *inode = new_inode(sb);

	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = current->fsuid;
		inode->i_gid = current->fsgid;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		inode->i_rdev = NODEV;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_fop = &default_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &usbfs_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;
			break;
		}
	}
	return inode; 
}

/* SMP-safe */
static int usbfs_mknod (struct inode *dir, struct dentry *dentry, int mode,
			int dev)
{
	struct inode *inode = usbfs_get_inode(dir->i_sb, mode, dev);
	int error = -ENOSPC;

	if (inode) {
		d_instantiate(dentry, inode);
		dget(dentry);
		error = 0;
	}
	return error;
}

static int usbfs_mkdir (struct inode *dir, struct dentry *dentry, int mode)
{
	return usbfs_mknod (dir, dentry, mode | S_IFDIR, 0);
}

static int usbfs_create (struct inode *dir, struct dentry *dentry, int mode)
{
 	return usbfs_mknod (dir, dentry, mode | S_IFREG, 0);
}

static inline int usbfs_positive (struct dentry *dentry)
{
	return dentry->d_inode && !d_unhashed(dentry);
}

static int usbfs_empty (struct dentry *dentry)
{
	struct list_head *list;

	spin_lock(&dcache_lock);

	list_for_each(list, &dentry->d_subdirs) {
		struct dentry *de = list_entry(list, struct dentry, d_child);
		if (usbfs_positive(de)) {
			spin_unlock(&dcache_lock);
			return 0;
		}
	}

	spin_unlock(&dcache_lock);
	return 1;
}

static int usbfs_unlink (struct inode *dir, struct dentry *dentry)
{
	int error = -ENOTEMPTY;

	if (usbfs_empty(dentry)) {
		struct inode *inode = dentry->d_inode;

		lock_kernel();
		inode->i_nlink--;
		unlock_kernel();
		dput(dentry);
		error = 0;
	}
	return error;
}

#define usbfs_rmdir usbfs_unlink

/* default file operations */
static ssize_t default_read_file (struct file *file, char *buf,
				  size_t count, loff_t *ppos)
{
	return 0;
}

static ssize_t default_write_file (struct file *file, const char *buf,
				   size_t count, loff_t *ppos)
{
	return count;
}

static loff_t default_file_lseek (struct file *file, loff_t offset, int orig)
{
	loff_t retval = -EINVAL;

	lock_kernel();
	switch(orig) {
	case 0:
		if (offset > 0) {
			file->f_pos = offset;
			retval = file->f_pos;
		} 
		break;
	case 1:
		if ((offset + file->f_pos) > 0) {
			file->f_pos += offset;
			retval = file->f_pos;
		} 
		break;
	default:
		break;
	}
	unlock_kernel();
	return retval;
}

static int default_open (struct inode *inode, struct file *filp)
{
	if (inode->u.generic_ip)
		filp->private_data = inode->u.generic_ip;

	return 0;
}

static struct file_operations default_file_operations = {
	read:		default_read_file,
	write:		default_write_file,
	open:		default_open,
	llseek:		default_file_lseek,
};

static struct inode_operations usbfs_dir_inode_operations = {
	create:		usbfs_create,
	lookup:		simple_lookup,
	unlink:		usbfs_unlink,
	mkdir:		usbfs_mkdir,
	rmdir:		usbfs_rmdir,
};

static struct super_operations usbfs_ops = {
	statfs:		simple_statfs,
	drop_inode:	generic_delete_inode,
};

static int usbfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *inode;
	struct dentry *root;

	if (parse_options(sb, data)) {
		warn("usbfs: mount parameter error:");
		return -EINVAL;
	}

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = USBDEVICE_SUPER_MAGIC;
	sb->s_op = &usbfs_ops;
	inode = usbfs_get_inode(sb, S_IFDIR | 0755, 0);

	if (!inode) {
		dbg("%s: could not get inode!",__FUNCTION__);
		return -ENOMEM;
	}

	root = d_alloc_root(inode);
	if (!root) {
		dbg("%s: could not get root dentry!",__FUNCTION__);
		iput(inode);
		return -ENOMEM;
	}
	sb->s_root = root;
	return 0;
}

/**
 * fs_create_by_name - create a file, given a name
 * @name:	name of file
 * @mode:	type of file
 * @parent:	dentry of directory to create it in
 * @dentry:	resulting dentry of file
 *
 * There is a bit of overhead in creating a file - basically, we 
 * have to hash the name of the file, then look it up. This will
 * prevent files of the same name. 
 * We then call the proper vfs_ function to take care of all the 
 * file creation details. 
 * This function handles both regular files and directories.
 */
static int fs_create_by_name (const char *name, mode_t mode,
			      struct dentry *parent, struct dentry **dentry)
{
	struct dentry *d = NULL;
	struct qstr qstr;
	int error;

	/* If the parent is not specified, we create it in the root.
	 * We need the root dentry to do this, which is in the super 
	 * block. A pointer to that is in the struct vfsmount that we
	 * have around.
	 */
	if (!parent ) {
		if (usbfs_mount && usbfs_mount->mnt_sb) {
			parent = usbfs_mount->mnt_sb->s_root;
		}
	}

	if (!parent) {
		dbg("Ah! can not find a parent!");
		return -EFAULT;
	}

	*dentry = NULL;
	qstr.name = name;
	qstr.len = strlen(name);
 	qstr.hash = full_name_hash(name,qstr.len);

	parent = dget(parent);

	down(&parent->d_inode->i_sem);

	d = lookup_hash(&qstr,parent);

	error = PTR_ERR(d);
	if (!IS_ERR(d)) {
		switch(mode & S_IFMT) {
			case 0: 
			case S_IFREG:
				error = vfs_create(parent->d_inode,d,mode);
				break;
			case S_IFDIR:
				error = vfs_mkdir(parent->d_inode,d,mode);
				break;
			default:
				err("cannot create special files");
		}
		*dentry = d;
	}
	up(&parent->d_inode->i_sem);

	dput(parent);
	return error;
}

static struct dentry *fs_create_file (const char *name, mode_t mode,
				      struct dentry *parent, void *data,
				      struct file_operations *fops,
				      uid_t uid, gid_t gid)
{
	struct dentry *dentry;
	int error;

	dbg("creating file '%s'",name);

	error = fs_create_by_name(name,mode,parent,&dentry);
	if (error) {
		dentry = NULL;
	} else {
		if (dentry->d_inode) {
			if (data)
				dentry->d_inode->u.generic_ip = data;
			if (fops)
				dentry->d_inode->i_fop = fops;
			dentry->d_inode->i_uid = uid;
			dentry->d_inode->i_gid = gid;
		}
	}

	return dentry;
}

static void fs_remove_file (struct dentry *dentry)
{
	struct dentry *parent = dentry->d_parent;
	
	if (!parent || !parent->d_inode)
		return;

	down(&parent->d_inode->i_sem);
	if (usbfs_positive(dentry)) {
		if (dentry->d_inode) {
			if (S_ISDIR(dentry->d_inode->i_mode))
				vfs_rmdir(parent->d_inode,dentry);
			else
				vfs_unlink(parent->d_inode,dentry);
		}

		dput(dentry);
	}
	up(&parent->d_inode->i_sem);
}

/* --------------------------------------------------------------------- */



/*
 * The usbdevfs name is now deprecated (as of 2.5.1).
 * It will be removed when the 2.7.x development cycle is started.
 * You have been warned :)
 */

static struct super_block *usb_get_sb(struct file_system_type *fs_type,
	int flags, char *dev_name, void *data)
{
	return get_sb_single(fs_type, flags, data, usbfs_fill_super);
}

static struct file_system_type usbdevice_fs_type = {
	owner:		THIS_MODULE,
	name:		"usbdevfs",
	get_sb:		usb_get_sb,
	kill_sb:	kill_anon_super,
};

static struct file_system_type usb_fs_type = {
	owner:		THIS_MODULE,
	name:		"usbfs",
	get_sb:		usb_get_sb,
	kill_sb:	kill_anon_super,
};

/* --------------------------------------------------------------------- */
static int get_mount (void)
{
	struct vfsmount *mnt;

	spin_lock (&mount_lock);
	if (usbfs_mount) {
		mntget(usbfs_mount);
		++mount_count;
		spin_unlock (&mount_lock);
		goto go_ahead;
	}

	spin_unlock (&mount_lock);
	mnt = kern_mount (&usbdevice_fs_type);
	if (IS_ERR(mnt)) {
		err ("could not mount the fs...erroring out!\n");
		return -ENODEV;
	}
	spin_lock (&mount_lock);
	if (!usbfs_mount) {
		usbfs_mount = mnt;
		++mount_count;
		spin_unlock (&mount_lock);
		goto go_ahead;
	}
	mntget(usbfs_mount);
	++mount_count;
	spin_unlock (&mount_lock);
	mntput(mnt);

go_ahead:
	dbg("mount_count = %d", mount_count);
	return 0;
}

static void remove_mount (void)
{
	struct vfsmount *mnt;

	spin_lock (&mount_lock);
	mnt = usbfs_mount;
	--mount_count;
	if (!mount_count)
		usbfs_mount = NULL;

	spin_unlock (&mount_lock);
	mntput(mnt);
	dbg("mount_count = %d", mount_count);
}

static int create_special_files (void)
{
	int retval;

	/* create the devices and drivers special files */
	retval = get_mount ();
	if (retval)
		return retval;
	devices_dentry = fs_create_file ("devices",
					 listmode | S_IFREG,
					 NULL, NULL,
					 &usbdevfs_devices_fops,
					 listuid, listgid);
	if (devices_dentry == NULL) {
		err ("Unable to create devices usbfs file");
		return -ENODEV;
	}

	return 0;
}

static void remove_special_files (void)
{
	if (devices_dentry)
		fs_remove_file (devices_dentry);
	devices_dentry = NULL;
	remove_mount();
}

void usbfs_update_special (void)
{
	struct inode *inode;

	if (devices_dentry) {
		inode = devices_dentry->d_inode;
		if (inode)
			inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	}
}

void usbfs_add_bus(struct usb_bus *bus)
{
	char name[8];
	int retval;

	/* create the special files if this is the first bus added */
	if (num_buses == 0) {
		retval = create_special_files();
		if (retval)
			return;
	}
	++num_buses;

	sprintf (name, "%03d", bus->busnum);
	bus->dentry = fs_create_file (name,
				      busmode | S_IFDIR,
				      NULL, bus, NULL,
				      busuid, busgid);
	if (bus->dentry == NULL)
		return;

	usbfs_update_special();
	usbdevfs_conn_disc_event();
}


void usbfs_remove_bus(struct usb_bus *bus)
{
	if (bus->dentry) {
		fs_remove_file (bus->dentry);
		bus->dentry = NULL;
	}

	--num_buses;
	if (num_buses <= 0) {
		remove_special_files();
		num_buses = 0;
	}

	usbfs_update_special();
	usbdevfs_conn_disc_event();
}

void usbfs_add_device(struct usb_device *dev)
{
	char name[8];
	int i;
	int i_size;

	sprintf (name, "%03d", dev->devnum);
	dev->dentry = fs_create_file (name,
				      devmode | S_IFREG,
				      dev->bus->dentry, dev,
				      &usbdevfs_device_file_operations,
				      devuid, devgid);
	if (dev->dentry == NULL)
		return;

	/* Set the size of the device's file to be
	 * equal to the size of the device descriptors. */
	i_size = sizeof (struct usb_device_descriptor);
	for (i = 0; i < dev->descriptor.bNumConfigurations; ++i) {
		struct usb_config_descriptor *config =
			(struct usb_config_descriptor *)dev->rawdescriptors[i];
		i_size += le16_to_cpu (config->wTotalLength);
	}
	if (dev->dentry->d_inode)
		dev->dentry->d_inode->i_size = i_size;

	usbfs_update_special();
	usbdevfs_conn_disc_event();
}

void usbfs_remove_device(struct usb_device *dev)
{
	struct dev_state *ds;
	struct siginfo sinfo;

	if (dev->dentry) {
		fs_remove_file (dev->dentry);
		dev->dentry = NULL;
	}
	while (!list_empty(&dev->filelist)) {
		ds = list_entry(dev->filelist.next, struct dev_state, list);
		list_del(&ds->list);
		INIT_LIST_HEAD(&ds->list);
		down_write(&ds->devsem);
		ds->dev = NULL;
		up_write(&ds->devsem);
		if (ds->discsignr) {
			sinfo.si_signo = SIGPIPE;
			sinfo.si_errno = EPIPE;
			sinfo.si_code = SI_ASYNCIO;
			sinfo.si_addr = ds->disccontext;
			send_sig_info(ds->discsignr, &sinfo, ds->disctask);
		}
	}
	usbfs_update_special();
	usbdevfs_conn_disc_event();
}

/* --------------------------------------------------------------------- */

#ifdef CONFIG_PROC_FS		
static struct proc_dir_entry *usbdir = NULL;
#endif	

int __init usbfs_init(void)
{
	int retval;

	retval = usb_register(&usbdevfs_driver);
	if (retval)
		return retval;

	retval = register_filesystem(&usb_fs_type);
	if (retval) {
		usb_deregister(&usbdevfs_driver);
		return retval;
	}
	retval = register_filesystem(&usbdevice_fs_type);
	if (retval) {
		unregister_filesystem(&usb_fs_type);
		usb_deregister(&usbdevfs_driver);
		return retval;
	}

#ifdef CONFIG_PROC_FS		
	/* create mount point for usbdevfs */
	usbdir = proc_mkdir("usb", proc_bus);
#endif	

	return 0;
}

void __exit usbfs_cleanup(void)
{
	usb_deregister(&usbdevfs_driver);
	unregister_filesystem(&usb_fs_type);
	unregister_filesystem(&usbdevice_fs_type);
#ifdef CONFIG_PROC_FS	
	if (usbdir)
		remove_proc_entry("usb", proc_bus);
#endif
}

