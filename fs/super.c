/*
 *  linux/fs/super.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  super.c contains code to handle: - mount structures
 *                                   - super-block tables
 *                                   - filesystem drivers list
 *                                   - mount system call
 *                                   - umount system call
 *                                   - ustat system call
 *
 * GK 2/5/95  -  Changed to support mounting the root fs via NFS
 *
 *  Added kerneld support: Jacques Gelinas and Bjorn Ekwall
 *  Added change_root: Werner Almesberger & Hans Lermen, Feb '96
 *  Added options to /proc/mounts:
 *    Torbjörn Lindh (torbjorn.lindh@gopta.se), April 14, 1996.
 *  Added devfs support: Richard Gooch <rgooch@atnf.csiro.au>, 13-JAN-1998
 *  Heavily rewritten for 'one fs - one tree' dcache architecture. AV, Mar 2000
 */

#include <linux/config.h>
#include <linux/slab.h>
#include <linux/locks.h>
#include <linux/smp_lock.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/major.h>
#include <linux/acct.h>

#include <asm/uaccess.h>

#include <linux/kmod.h>
#define __NO_VERSION__
#include <linux/module.h>

int do_remount_sb(struct super_block *sb, int flags, void * data);

LIST_HEAD(super_blocks);
spinlock_t sb_lock = SPIN_LOCK_UNLOCKED;

/*
 * Handling of filesystem drivers list.
 * Rules:
 *	Inclusion to/removals from/scanning of list are protected by spinlock.
 *	During the unload module must call unregister_filesystem().
 *	We can access the fields of list element if:
 *		1) spinlock is held or
 *		2) we hold the reference to the module.
 *	The latter can be guaranteed by call of try_inc_mod_count(); if it
 *	returned 0 we must skip the element, otherwise we got the reference.
 *	Once the reference is obtained we can drop the spinlock.
 */

static struct file_system_type *file_systems;
static rwlock_t file_systems_lock = RW_LOCK_UNLOCKED;

/* WARNING: This can be used only if we _already_ own a reference */
static void get_filesystem(struct file_system_type *fs)
{
	if (fs->owner)
		__MOD_INC_USE_COUNT(fs->owner);
}

static void put_filesystem(struct file_system_type *fs)
{
	if (fs->owner)
		__MOD_DEC_USE_COUNT(fs->owner);
}

static struct file_system_type **find_filesystem(const char *name)
{
	struct file_system_type **p;
	for (p=&file_systems; *p; p=&(*p)->next)
		if (strcmp((*p)->name,name) == 0)
			break;
	return p;
}

/**
 *	register_filesystem - register a new filesystem
 *	@fs: the file system structure
 *
 *	Adds the file system passed to the list of file systems the kernel
 *	is aware of for mount and other syscalls. Returns 0 on success,
 *	or a negative errno code on an error.
 *
 *	The &struct file_system_type that is passed is linked into the kernel 
 *	structures and must not be freed until the file system has been
 *	unregistered.
 */
 
int register_filesystem(struct file_system_type * fs)
{
	int res = 0;
	struct file_system_type ** p;

	if (!fs)
		return -EINVAL;
	if (fs->next)
		return -EBUSY;
	INIT_LIST_HEAD(&fs->fs_supers);
	write_lock(&file_systems_lock);
	p = find_filesystem(fs->name);
	if (*p)
		res = -EBUSY;
	else
		*p = fs;
	write_unlock(&file_systems_lock);
	return res;
}

/**
 *	unregister_filesystem - unregister a file system
 *	@fs: filesystem to unregister
 *
 *	Remove a file system that was previously successfully registered
 *	with the kernel. An error is returned if the file system is not found.
 *	Zero is returned on a success.
 *	
 *	Once this function has returned the &struct file_system_type structure
 *	may be freed or reused.
 */
 
int unregister_filesystem(struct file_system_type * fs)
{
	struct file_system_type ** tmp;

	write_lock(&file_systems_lock);
	tmp = &file_systems;
	while (*tmp) {
		if (fs == *tmp) {
			*tmp = fs->next;
			fs->next = NULL;
			write_unlock(&file_systems_lock);
			return 0;
		}
		tmp = &(*tmp)->next;
	}
	write_unlock(&file_systems_lock);
	return -EINVAL;
}

static int fs_index(const char * __name)
{
	struct file_system_type * tmp;
	char * name;
	int err, index;

	name = getname(__name);
	err = PTR_ERR(name);
	if (IS_ERR(name))
		return err;

	err = -EINVAL;
	read_lock(&file_systems_lock);
	for (tmp=file_systems, index=0 ; tmp ; tmp=tmp->next, index++) {
		if (strcmp(tmp->name,name) == 0) {
			err = index;
			break;
		}
	}
	read_unlock(&file_systems_lock);
	putname(name);
	return err;
}

static int fs_name(unsigned int index, char * buf)
{
	struct file_system_type * tmp;
	int len, res;

	read_lock(&file_systems_lock);
	for (tmp = file_systems; tmp; tmp = tmp->next, index--)
		if (index <= 0 && try_inc_mod_count(tmp->owner))
				break;
	read_unlock(&file_systems_lock);
	if (!tmp)
		return -EINVAL;

	/* OK, we got the reference, so we can safely block */
	len = strlen(tmp->name) + 1;
	res = copy_to_user(buf, tmp->name, len) ? -EFAULT : 0;
	put_filesystem(tmp);
	return res;
}

static int fs_maxindex(void)
{
	struct file_system_type * tmp;
	int index;

	read_lock(&file_systems_lock);
	for (tmp = file_systems, index = 0 ; tmp ; tmp = tmp->next, index++)
		;
	read_unlock(&file_systems_lock);
	return index;
}

/*
 * Whee.. Weird sysv syscall. 
 */
asmlinkage long sys_sysfs(int option, unsigned long arg1, unsigned long arg2)
{
	int retval = -EINVAL;

	switch (option) {
		case 1:
			retval = fs_index((const char *) arg1);
			break;

		case 2:
			retval = fs_name(arg1, (char *) arg2);
			break;

		case 3:
			retval = fs_maxindex();
			break;
	}
	return retval;
}

int get_filesystem_list(char * buf)
{
	int len = 0;
	struct file_system_type * tmp;

	read_lock(&file_systems_lock);
	tmp = file_systems;
	while (tmp && len < PAGE_SIZE - 80) {
		len += sprintf(buf+len, "%s\t%s\n",
			(tmp->fs_flags & FS_REQUIRES_DEV) ? "" : "nodev",
			tmp->name);
		tmp = tmp->next;
	}
	read_unlock(&file_systems_lock);
	return len;
}

struct file_system_type *get_fs_type(const char *name)
{
	struct file_system_type *fs;
	
	read_lock(&file_systems_lock);
	fs = *(find_filesystem(name));
	if (fs && !try_inc_mod_count(fs->owner))
		fs = NULL;
	read_unlock(&file_systems_lock);
	if (!fs && (request_module(name) == 0)) {
		read_lock(&file_systems_lock);
		fs = *(find_filesystem(name));
		if (fs && !try_inc_mod_count(fs->owner))
			fs = NULL;
		read_unlock(&file_systems_lock);
	}
	return fs;
}

/**
 *	alloc_super	-	create new superblock
 *
 *	Allocates and initializes a new &struct super_block.  alloc_super()
 *	returns a pointer new superblock or %NULL if allocation had failed.
 */
static struct super_block *alloc_super(void)
{
	struct super_block *s = kmalloc(sizeof(struct super_block),  GFP_USER);
	if (s) {
		memset(s, 0, sizeof(struct super_block));
		INIT_LIST_HEAD(&s->s_dirty);
		INIT_LIST_HEAD(&s->s_locked_inodes);
		INIT_LIST_HEAD(&s->s_files);
		INIT_LIST_HEAD(&s->s_instances);
		init_rwsem(&s->s_umount);
		sema_init(&s->s_lock, 1);
		down_write(&s->s_umount);
		s->s_count = S_BIAS;
		atomic_set(&s->s_active, 1);
		sema_init(&s->s_vfs_rename_sem,1);
		sema_init(&s->s_nfsd_free_path_sem,1);
		sema_init(&s->s_dquot.dqio_sem, 1);
		sema_init(&s->s_dquot.dqoff_sem, 1);
		s->s_maxbytes = MAX_NON_LFS;
	}
	return s;
}

/**
 *	destroy_super	-	frees a superblock
 *	@s: superblock to free
 *
 *	Frees a superblock.
 */
static inline void destroy_super(struct super_block *s)
{
	kfree(s);
}

/* Superblock refcounting  */

/**
 *	deactivate_super	-	turn an active reference into temporary
 *	@s: superblock to deactivate
 *
 *	Turns an active reference into temporary one.  Returns 0 if there are
 *	other active references, 1 if we had deactivated the last one.
 */
static inline int deactivate_super(struct super_block *s)
{
	if (!atomic_dec_and_lock(&s->s_active, &sb_lock))
		return 0;
	s->s_count -= S_BIAS-1;
	spin_unlock(&sb_lock);
	return 1;
}

/**
 *	put_super	-	drop a temporary reference to superblock
 *	@s: superblock in question
 *
 *	Drops a temporary reference, frees superblock if there's no
 *	references left.
 */
static inline void put_super(struct super_block *s)
{
	spin_lock(&sb_lock);
	if (!--s->s_count)
		destroy_super(s);
	spin_unlock(&sb_lock);
}

/**
 *	grab_super	- acquire an active reference
 *	@s	- reference we are trying to make active
 *
 *	Tries to acquire an active reference.  grab_super() is used when we
 * 	had just found a superblock in super_blocks or fs_type->fs_supers
 *	and want to turn it into a full-blown active reference.  grab_super()
 *	is called with sb_lock held and drops it.  Returns 1 in case of
 *	success, 0 if we had failed (superblock contents was already dead or
 *	dying when grab_super() had been called).
 */
static int grab_super(struct super_block *s)
{
	s->s_count++;
	spin_unlock(&sb_lock);
	down_write(&s->s_umount);
	if (s->s_root) {
		spin_lock(&sb_lock);
		if (s->s_count > S_BIAS) {
			atomic_inc(&s->s_active);
			s->s_count--;
			spin_unlock(&sb_lock);
			return 1;
		}
		spin_unlock(&sb_lock);
	}
	up_write(&s->s_umount);
	put_super(s);
	return 0;
}
 
/**
 *	insert_super	-	put superblock on the lists
 *	@s:	superblock in question
 *	@type:	filesystem type it will belong to
 *
 *	Associates superblock with fs type and puts it on per-type and global
 *	superblocks' lists.  Should be called with sb_lock held; drops it.
 */
static void insert_super(struct super_block *s, struct file_system_type *type)
{
	s->s_type = type;
	list_add(&s->s_list, super_blocks.prev);
	list_add(&s->s_instances, &type->fs_supers);
	spin_unlock(&sb_lock);
	get_filesystem(type);
}

void put_unnamed_dev(kdev_t dev);	/* should become static */

/**
 *	remove_super	-	makes superblock unreachable
 *	@s:	superblock in question
 *
 *	Removes superblock from the lists, unlocks it, drop the reference
 *	and releases the hosting device.  @s should have no active
 *	references by that time and after remove_super() it's essentially
 *	in rundown mode - all remaining references are temporary, no new
 *	reference of any sort are going to appear and all holders of
 *	temporary ones will eventually drop them.  At that point superblock
 *	itself will be destroyed; all its contents is already gone.
 */
static void remove_super(struct super_block *s)
{
	kdev_t dev = s->s_dev;
	struct block_device *bdev = s->s_bdev;
	struct file_system_type *fs = s->s_type;

	spin_lock(&sb_lock);
	list_del(&s->s_list);
	list_del(&s->s_instances);
	spin_unlock(&sb_lock);
	up_write(&s->s_umount);
	put_super(s);
	put_filesystem(fs);
	if (bdev)
		blkdev_put(bdev, BDEV_FS);
	else
		put_unnamed_dev(dev);
}

struct vfsmount *alloc_vfsmnt(void);
void free_vfsmnt(struct vfsmount *mnt);
void set_devname(struct vfsmount *mnt, const char *name);

static inline struct super_block * find_super(kdev_t dev)
{
	struct list_head *p;

	list_for_each(p, &super_blocks) {
		struct super_block * s = sb_entry(p);
		if (s->s_dev == dev) {
			s->s_count++;
			return s;
		}
	}
	return NULL;
}

void drop_super(struct super_block *sb)
{
	up_read(&sb->s_umount);
	put_super(sb);
}

static inline void write_super(struct super_block *sb)
{
	lock_super(sb);
	if (sb->s_root && sb->s_dirt)
		if (sb->s_op && sb->s_op->write_super)
			sb->s_op->write_super(sb);
	unlock_super(sb);
}

/*
 * Note: check the dirty flag before waiting, so we don't
 * hold up the sync while mounting a device. (The newly
 * mounted device won't need syncing.)
 */
void sync_supers(kdev_t dev)
{
	struct super_block * sb;

	if (dev) {
		sb = get_super(dev);
		if (sb) {
			if (sb->s_dirt)
				write_super(sb);
			drop_super(sb);
		}
		return;
	}
restart:
	spin_lock(&sb_lock);
	sb = sb_entry(super_blocks.next);
	while (sb != sb_entry(&super_blocks))
		if (sb->s_dirt) {
			sb->s_count++;
			spin_unlock(&sb_lock);
			down_read(&sb->s_umount);
			write_super(sb);
			drop_super(sb);
			goto restart;
		} else
			sb = sb_entry(sb->s_list.next);
	spin_unlock(&sb_lock);
}

/**
 *	get_super	-	get the superblock of a device
 *	@dev: device to get the superblock for
 *	
 *	Scans the superblock list and finds the superblock of the file system
 *	mounted on the device given. %NULL is returned if no match is found.
 */
 
struct super_block * get_super(kdev_t dev)
{
	struct super_block * s;

	if (!dev)
		return NULL;

	while (1) {
		spin_lock(&sb_lock);
		s = find_super(dev);
		spin_unlock(&sb_lock);
		if (!s)
			break;
		down_read(&s->s_umount);
		if (s->s_root)
			break;
		drop_super(s);
	}
	return s;
}

asmlinkage long sys_ustat(dev_t dev, struct ustat * ubuf)
{
        struct super_block *s;
        struct ustat tmp;
        struct statfs sbuf;
	int err = -EINVAL;

        s = get_super(to_kdev_t(dev));
        if (s == NULL)
                goto out;
	err = vfs_statfs(s, &sbuf);
	drop_super(s);
	if (err)
		goto out;

        memset(&tmp,0,sizeof(struct ustat));
        tmp.f_tfree = sbuf.f_bfree;
        tmp.f_tinode = sbuf.f_ffree;

        err = copy_to_user(ubuf,&tmp,sizeof(struct ustat)) ? -EFAULT : 0;
out:
	return err;
}

/*
 * Unnamed block devices are dummy devices used by virtual
 * filesystems which don't use real block-devices.  -- jrs
 */

static unsigned long unnamed_dev_in_use[256/(8*sizeof(unsigned long))];

kdev_t get_unnamed_dev(void)
{
	int i;

	for (i = 1; i < 256; i++) {
		if (!test_and_set_bit(i,unnamed_dev_in_use))
			return MKDEV(UNNAMED_MAJOR, i);
	}
	return 0;
}

void put_unnamed_dev(kdev_t dev)
{
	if (!dev || MAJOR(dev) != UNNAMED_MAJOR)
		return;
	if (test_and_clear_bit(MINOR(dev), unnamed_dev_in_use))
		return;
	printk("VFS: put_unnamed_dev: freeing unused device %s\n",
			kdevname(dev));
}

static struct super_block *get_sb_bdev(struct file_system_type *fs_type,
	char *dev_name, int flags, void * data)
{
	struct inode *inode;
	struct block_device *bdev;
	struct block_device_operations *bdops;
	struct super_block * s;
	struct nameidata nd;
	struct list_head *p;
	kdev_t dev;
	int error = 0;
	mode_t mode = FMODE_READ; /* we always need it ;-) */

	/* What device it is? */
	if (!dev_name || !*dev_name)
		return ERR_PTR(-EINVAL);
	if (path_init(dev_name, LOOKUP_FOLLOW|LOOKUP_POSITIVE, &nd))
		error = path_walk(dev_name, &nd);
	if (error)
		return ERR_PTR(error);
	inode = nd.dentry->d_inode;
	error = -ENOTBLK;
	if (!S_ISBLK(inode->i_mode))
		goto out;
	error = -EACCES;
	if (nd.mnt->mnt_flags & MNT_NODEV)
		goto out;
	bd_acquire(inode);
	bdev = inode->i_bdev;
	bdops = devfs_get_ops ( devfs_get_handle_from_inode (inode) );
	if (bdops) bdev->bd_op = bdops;
	/* Done with lookups, semaphore down */
	dev = to_kdev_t(bdev->bd_dev);
	if (!(flags & MS_RDONLY))
		mode |= FMODE_WRITE;
	error = blkdev_get(bdev, mode, 0, BDEV_FS);
	if (error)
		goto out;
	check_disk_change(dev);
	error = -EACCES;
	if (!(flags & MS_RDONLY) && is_read_only(dev)) {
		blkdev_put(bdev, BDEV_FS);
		goto out;
	}

	error = -ENOMEM;
	s = alloc_super();
	if (!s) {
		blkdev_put(bdev, BDEV_FS);
		goto out;
	}

	error = -EBUSY;
restart:
	spin_lock(&sb_lock);

	list_for_each(p, &super_blocks) {
		struct super_block *old = sb_entry(p);
		if (old->s_dev != dev)
			continue;
		if (old->s_type != fs_type ||
		    ((flags ^ old->s_flags) & MS_RDONLY)) {
			spin_unlock(&sb_lock);
			destroy_super(s);
			blkdev_put(bdev, BDEV_FS);
			goto out;
		}
		if (!grab_super(old))
			goto restart;
		destroy_super(s);
		blkdev_put(bdev, BDEV_FS);
		path_release(&nd);
		return old;
	}
	s->s_dev = dev;
	s->s_bdev = bdev;
	s->s_flags = flags;
	insert_super(s, fs_type);

	error = -EINVAL;
	lock_super(s);
	if (!fs_type->read_super(s, data, flags & MS_VERBOSE ? 1 : 0))
		goto out_fail;
	s->s_flags |= MS_ACTIVE;
	unlock_super(s);
	path_release(&nd);
	return s;

out_fail:
	unlock_super(s);
	deactivate_super(s);
	remove_super(s);
out:
	path_release(&nd);
	return ERR_PTR(error);
}

static struct super_block *get_sb_nodev(struct file_system_type *fs_type,
	int flags, void * data)
{
	struct super_block *s = alloc_super();

	if (!s)
		return ERR_PTR(-ENOMEM);
	s->s_dev = get_unnamed_dev();
	if (!s->s_dev) {
		destroy_super(s);
		return ERR_PTR(-EMFILE);
	}
	s->s_flags = flags;
	spin_lock(&sb_lock);
	insert_super(s, fs_type);
	lock_super(s);
	if (!fs_type->read_super(s, data, flags & MS_VERBOSE ? 1 : 0))
		goto out_fail;
	s->s_flags |= MS_ACTIVE;
	unlock_super(s);
	return s;

out_fail:
	unlock_super(s);
	deactivate_super(s);
	remove_super(s);
	return ERR_PTR(-EINVAL);
}

static struct super_block *get_sb_single(struct file_system_type *fs_type,
	int flags, void *data)
{
	struct super_block * s = alloc_super();
	if (!s)
		return ERR_PTR(-ENOMEM);
	/*
	 * Get the superblock of kernel-wide instance, but
	 * keep the reference to fs_type.
	 */
retry:
	spin_lock(&sb_lock);
	if (!list_empty(&fs_type->fs_supers)) {
		struct super_block *old;
		old = list_entry(fs_type->fs_supers.next, struct super_block,
				s_instances);
		if (!grab_super(old))
			goto retry;
		destroy_super(s);
		do_remount_sb(old, flags, data);
		return old;
	} else {
		s->s_dev = get_unnamed_dev();
		if (!s->s_dev) {
			spin_unlock(&sb_lock);
			destroy_super(s);
			return ERR_PTR(-EMFILE);
		}
		s->s_flags = flags;
		insert_super(s, fs_type);
		lock_super(s);
		if (!fs_type->read_super(s, data, flags & MS_VERBOSE ? 1 : 0))
			goto out_fail;
		s->s_flags |= MS_ACTIVE;
		unlock_super(s);
		return s;

	out_fail:
		unlock_super(s);
		deactivate_super(s);
		remove_super(s);
		return ERR_PTR(-EINVAL);
	}
}

void kill_super(struct super_block *sb)
{
	struct dentry *root = sb->s_root;
	struct file_system_type *fs = sb->s_type;
	struct super_operations *sop = sb->s_op;

	if (!deactivate_super(sb))
		return;

	down_write(&sb->s_umount);
	lock_kernel();
	sb->s_root = NULL;
	/* Need to clean after the sucker */
	if (fs->fs_flags & FS_LITTER)
		d_genocide(root);
	shrink_dcache_parent(root);
	dput(root);
	fsync_super(sb);
	lock_super(sb);
	sb->s_flags &= ~MS_ACTIVE;
	invalidate_inodes(sb);	/* bad name - it should be evict_inodes() */
	if (sop) {
		if (sop->write_super && sb->s_dirt)
			sop->write_super(sb);
		if (sop->put_super)
			sop->put_super(sb);
	}

	/* Forget any remaining inodes */
	if (invalidate_inodes(sb)) {
		printk("VFS: Busy inodes after unmount. "
			"Self-destruct in 5 seconds.  Have a nice day...\n");
	}

	unlock_kernel();
	unlock_super(sb);
	remove_super(sb);
}

/*
 * Alters the mount flags of a mounted file system. Only the mount point
 * is used as a reference - file system type and the device are ignored.
 */

int do_remount_sb(struct super_block *sb, int flags, void *data)
{
	int retval;
	
	if (!(flags & MS_RDONLY) && sb->s_dev && is_read_only(sb->s_dev))
		return -EACCES;
		/*flags |= MS_RDONLY;*/
	if (flags & MS_RDONLY)
		acct_auto_close(sb->s_dev);
	shrink_dcache_sb(sb);
	fsync_super(sb);
	/* If we are remounting RDONLY, make sure there are no rw files open */
	if ((flags & MS_RDONLY) && !(sb->s_flags & MS_RDONLY))
		if (!fs_may_remount_ro(sb))
			return -EBUSY;
	if (sb->s_op && sb->s_op->remount_fs) {
		lock_super(sb);
		retval = sb->s_op->remount_fs(sb, &flags, data);
		unlock_super(sb);
		if (retval)
			return retval;
	}
	sb->s_flags = (sb->s_flags & ~MS_RMT_MASK) | (flags & MS_RMT_MASK);

	/*
	 * We can't invalidate inodes as we can loose data when remounting
	 * (someone might manage to alter data while we are waiting in lock_super()
	 * or in foo_remount_fs()))
	 */

	return 0;
}

struct vfsmount *do_kern_mount(char *type, int flags, char *name, void *data)
{
	struct file_system_type * fstype;
	struct vfsmount *mnt = NULL;
	struct super_block *sb;

	if (!type || !memchr(type, 0, PAGE_SIZE))
		return ERR_PTR(-EINVAL);

	/* we need capabilities... */
	if (!capable(CAP_SYS_ADMIN))
		return ERR_PTR(-EPERM);

	/* ... filesystem driver... */
	fstype = get_fs_type(type);
	if (!fstype)		
		return ERR_PTR(-ENODEV);

	/* ... allocated vfsmount... */
	mnt = alloc_vfsmnt();
	if (!mnt) {
		mnt = ERR_PTR(-ENOMEM);
		goto fs_out;
	}
	set_devname(mnt, name);
	/* get locked superblock */
	if (fstype->fs_flags & FS_REQUIRES_DEV)
		sb = get_sb_bdev(fstype, name, flags, data);
	else if (fstype->fs_flags & FS_SINGLE)
		sb = get_sb_single(fstype, flags, data);
	else
		sb = get_sb_nodev(fstype, flags, data);

	if (IS_ERR(sb)) {
		free_vfsmnt(mnt);
		mnt = (struct vfsmount *)sb;
		goto fs_out;
	}
	if (fstype->fs_flags & FS_NOMOUNT)
		sb->s_flags |= MS_NOUSER;

	mnt->mnt_sb = sb;
	mnt->mnt_root = dget(sb->s_root);
	mnt->mnt_mountpoint = mnt->mnt_root;
	mnt->mnt_parent = mnt;
	up_write(&sb->s_umount);
fs_out:
	put_filesystem(fstype);
	return mnt;
}

struct vfsmount *kern_mount(struct file_system_type *type)
{
	return do_kern_mount((char *)type->name, 0, (char *)type->name, NULL);
}
