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
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/locks.h>
#include <linux/smp_lock.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/fd.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/quotaops.h>
#include <linux/acct.h>

#include <asm/uaccess.h>

#include <linux/nfs_fs.h>
#include <linux/nfs_fs_sb.h>
#include <linux/nfs_mount.h>

#include <linux/kmod.h>
#define __NO_VERSION__
#include <linux/module.h>

extern void wait_for_keypress(void);

extern int root_mountflags;

int do_remount_sb(struct super_block *sb, int flags, void * data);

/* this is initialized in init/main.c */
kdev_t ROOT_DEV;

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

struct vfsmount *alloc_vfsmnt(void);
void free_vfsmnt(struct vfsmount *mnt);
void set_devname(struct vfsmount *mnt, const char *name);

/* Will go away */
extern struct vfsmount *root_vfsmnt;
extern int graft_tree(struct vfsmount *mnt, struct nameidata *nd);

static inline void __put_super(struct super_block *sb)
{
	spin_lock(&sb_lock);
	if (!--sb->s_count)
		kfree(sb);
	spin_unlock(&sb_lock);
}

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
	__put_super(sb);
}

static void put_super(struct super_block *sb)
{
	atomic_dec(&sb->s_active);
	up_write(&sb->s_umount);
	__put_super(sb);
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
restart:
	spin_lock(&sb_lock);
	s = find_super(dev);
	if (s) {
		spin_unlock(&sb_lock);
		down_read(&s->s_umount);
		if (s->s_root)
			return s;
		drop_super(s);
		goto restart;
	}
	spin_unlock(&sb_lock);
	return NULL;
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

/**
 *	get_empty_super	-	find empty superblocks
 *
 *	Find a superblock with no device assigned. A free superblock is 
 *	found and returned. If neccessary new superblocks are allocated.
 *	%NULL is returned if there are insufficient resources to complete
 *	the request.
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
		s->s_count = 1;
		atomic_set(&s->s_active, 1);
		sema_init(&s->s_vfs_rename_sem,1);
		sema_init(&s->s_nfsd_free_path_sem,1);
		sema_init(&s->s_dquot.dqio_sem, 1);
		sema_init(&s->s_dquot.dqoff_sem, 1);
		s->s_maxbytes = MAX_NON_LFS;
	}
	return s;
}

static struct super_block * read_super(kdev_t dev, struct block_device *bdev,
				       struct file_system_type *type, int flags,
				       void *data)
{
	struct super_block * s;
	s = alloc_super();
	if (!s)
		goto out;
	s->s_dev = dev;
	s->s_bdev = bdev;
	s->s_flags = flags;
	s->s_type = type;
	spin_lock(&sb_lock);
	list_add (&s->s_list, super_blocks.prev);
	list_add (&s->s_instances, &type->fs_supers);
	s->s_count += S_BIAS;
	spin_unlock(&sb_lock);
	down_write(&s->s_umount);
	lock_super(s);
	if (!type->read_super(s, data, flags & MS_VERBOSE ? 1 : 0))
		goto out_fail;
	s->s_flags |= MS_ACTIVE;
	unlock_super(s);
	/* tell bdcache that we are going to keep this one */
	if (bdev)
		atomic_inc(&bdev->bd_count);
out:
	return s;

out_fail:
	s->s_dev = 0;
	s->s_bdev = 0;
	s->s_type = NULL;
	unlock_super(s);
	spin_lock(&sb_lock);
	list_del(&s->s_list);
	list_del(&s->s_instances);
	s->s_count -= S_BIAS;
	spin_unlock(&sb_lock);
	put_super(s);
	return NULL;
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

static int grab_super(struct super_block *sb)
{
	sb->s_count++;
	atomic_inc(&sb->s_active);
	spin_unlock(&sb_lock);
	down_write(&sb->s_umount);
	if (sb->s_root) {
		spin_lock(&sb_lock);
		if (sb->s_count > S_BIAS) {
			sb->s_count--;
			spin_unlock(&sb_lock);
			return 1;
		}
		spin_unlock(&sb_lock);
	}
	put_super(sb);
	return 0;
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
	if (!(flags & MS_RDONLY) && is_read_only(dev))
		goto out1;

	error = -ENOMEM;
	s = alloc_super();
	if (!s)
		goto out1;
	down_write(&s->s_umount);

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
			put_super(s);
			goto out1;
		}
		if (!grab_super(old))
			goto restart;
		put_super(s);
		blkdev_put(bdev, BDEV_FS);
		path_release(&nd);
		return old;
	}
	s->s_dev = dev;
	s->s_bdev = bdev;
	s->s_flags = flags;
	s->s_type = fs_type;
	list_add (&s->s_list, super_blocks.prev);
	list_add (&s->s_instances, &fs_type->fs_supers);
	s->s_count += S_BIAS;

	spin_unlock(&sb_lock);

	error = -EINVAL;
	lock_super(s);
	if (!fs_type->read_super(s, data, flags & MS_VERBOSE ? 1 : 0))
		goto out_fail;
	s->s_flags |= MS_ACTIVE;
	unlock_super(s);
	get_filesystem(fs_type);
	path_release(&nd);
	return s;

out_fail:
	s->s_dev = 0;
	s->s_bdev = 0;
	s->s_type = NULL;
	unlock_super(s);
	spin_lock(&sb_lock);
	list_del(&s->s_list);
	list_del(&s->s_instances);
	s->s_count -= S_BIAS;
	spin_unlock(&sb_lock);
	put_super(s);
out1:
	blkdev_put(bdev, BDEV_FS);
out:
	path_release(&nd);
	return ERR_PTR(error);
}

static struct super_block *get_sb_nodev(struct file_system_type *fs_type,
	int flags, void * data)
{
	kdev_t dev;
	int error = -EMFILE;
	dev = get_unnamed_dev();
	if (dev) {
		struct super_block * sb;
		error = -EINVAL;
		sb = read_super(dev, NULL, fs_type, flags, data);
		if (sb) {
			get_filesystem(fs_type);
			return sb;
		}
		put_unnamed_dev(dev);
	}
	return ERR_PTR(error);
}

static struct super_block *get_sb_single(struct file_system_type *fs_type,
	int flags, void *data)
{
	struct super_block * s = alloc_super();
	if (!s)
		return ERR_PTR(-ENOMEM);
	down_write(&s->s_umount);
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
		put_super(s);
		do_remount_sb(old, flags, data);
		return old;
	} else {
		kdev_t dev = get_unnamed_dev();
		if (!dev) {
			spin_unlock(&sb_lock);
			put_super(s);
			return ERR_PTR(-EMFILE);
		}
		s->s_dev = dev;
		s->s_flags = flags;
		s->s_type = fs_type;
		list_add (&s->s_list, super_blocks.prev);
		list_add (&s->s_instances, &fs_type->fs_supers);
		s->s_count += S_BIAS;
		spin_unlock(&sb_lock);
		lock_super(s);
		if (!fs_type->read_super(s, data, flags & MS_VERBOSE ? 1 : 0))
			goto out_fail;
		s->s_flags |= MS_ACTIVE;
		unlock_super(s);
		get_filesystem(fs_type);
		return s;

	out_fail:
		s->s_dev = 0;
		s->s_bdev = 0;
		s->s_type = NULL;
		unlock_super(s);
		spin_lock(&sb_lock);
		list_del(&s->s_list);
		list_del(&s->s_instances);
		s->s_count -= S_BIAS;
		spin_unlock(&sb_lock);
		put_super(s);
		put_unnamed_dev(dev);
		return ERR_PTR(-EINVAL);
	}
}

void kill_super(struct super_block *sb)
{
	struct block_device *bdev;
	kdev_t dev;
	struct dentry *root = sb->s_root;
	struct file_system_type *fs = sb->s_type;
	struct super_operations *sop = sb->s_op;

	if (!atomic_dec_and_lock(&sb->s_active, &sb_lock))
		return;

	sb->s_count -= S_BIAS;
	spin_unlock(&sb_lock);

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

	dev = sb->s_dev;
	sb->s_dev = 0;		/* Free the superblock */
	bdev = sb->s_bdev;
	sb->s_bdev = NULL;
	put_filesystem(fs);
	sb->s_type = NULL;
	unlock_super(sb);
	unlock_kernel();
	if (bdev)
		blkdev_put(bdev, BDEV_FS);
	else
		put_unnamed_dev(dev);
	spin_lock(&sb_lock);
	list_del(&sb->s_list);
	list_del(&sb->s_instances);
	spin_unlock(&sb_lock);
	atomic_inc(&sb->s_active);
	put_super(sb);
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

static char * __initdata root_mount_data;
static int __init root_data_setup(char *str)
{
	root_mount_data = str;
	return 1;
}

static char * __initdata root_fs_names;
static int __init fs_names_setup(char *str)
{
	root_fs_names = str;
	return 1;
}

__setup("rootflags=", root_data_setup);
__setup("rootfstype=", fs_names_setup);

static void __init get_fs_names(char *page)
{
	char *s = page;

	if (root_fs_names) {
		strcpy(page, root_fs_names);
		while (*s++) {
			if (s[-1] == ',')
				s[-1] = '\0';
		}
	} else {
		int len = get_filesystem_list(page);
		char *p, *next;

		page[len] = '\0';
		for (p = page-1; p; p = next) {
			next = strchr(++p, '\n');
			if (*p++ != '\t')
				continue;
			while ((*s++ = *p++) != '\n')
				;
			s[-1] = '\0';
		}
	}
	*s = '\0';
}

void __init mount_root(void)
{
	struct nameidata root_nd;
	struct super_block * sb;
	struct vfsmount *vfsmnt;
	struct block_device *bdev = NULL;
	mode_t mode;
	int retval;
	void *handle;
	char path[64];
	int path_start = -1;
	char *name = "/dev/root";
	char *fs_names, *p;
#ifdef CONFIG_ROOT_NFS
	void *data;
#endif
	root_mountflags |= MS_VERBOSE;

#ifdef CONFIG_ROOT_NFS
	if (MAJOR(ROOT_DEV) != UNNAMED_MAJOR)
		goto skip_nfs;
	data = nfs_root_data();
	if (!data)
		goto no_nfs;
	vfsmnt = do_kern_mount("nfs", root_mountflags, "/dev/root", data);
	if (!IS_ERR(vfsmnt)) {
		printk ("VFS: Mounted root (%s filesystem).\n", "nfs");
		ROOT_DEV = vfsmnt->mnt_sb->s_dev;
		goto attach_it;
	}
no_nfs:
	printk(KERN_ERR "VFS: Unable to mount root fs via NFS, trying floppy.\n");
	ROOT_DEV = MKDEV(FLOPPY_MAJOR, 0);
skip_nfs:
#endif

#ifdef CONFIG_BLK_DEV_FD
	if (MAJOR(ROOT_DEV) == FLOPPY_MAJOR) {
#ifdef CONFIG_BLK_DEV_RAM
		extern int rd_doload;
		extern void rd_load_secondary(void);
#endif
		floppy_eject();
#ifndef CONFIG_BLK_DEV_RAM
		printk(KERN_NOTICE "(Warning, this kernel has no ramdisk support)\n");
#else
		/* rd_doload is 2 for a dual initrd/ramload setup */
		if(rd_doload==2)
			rd_load_secondary();
		else
#endif
		{
			printk(KERN_NOTICE "VFS: Insert root floppy and press ENTER\n");
			wait_for_keypress();
		}
	}
#endif

	fs_names = __getname();
	get_fs_names(fs_names);

	devfs_make_root (root_device_name);
	handle = devfs_find_handle (NULL, ROOT_DEVICE_NAME,
	                            MAJOR (ROOT_DEV), MINOR (ROOT_DEV),
				    DEVFS_SPECIAL_BLK, 1);
	if (handle)  /*  Sigh: bd*() functions only paper over the cracks  */
	{
	    unsigned major, minor;

	    devfs_get_maj_min (handle, &major, &minor);
	    ROOT_DEV = MKDEV (major, minor);
	}

	/*
	 * Probably pure paranoia, but I'm less than happy about delving into
	 * devfs crap and checking it right now. Later.
	 */
	if (!ROOT_DEV)
		panic("I have no root and I want to scream");

retry:
	bdev = bdget(kdev_t_to_nr(ROOT_DEV));
	if (!bdev)
		panic(__FUNCTION__ ": unable to allocate root device");
	bdev->bd_op = devfs_get_ops (handle);
	path_start = devfs_generate_path (handle, path + 5, sizeof (path) - 5);
	mode = FMODE_READ;
	if (!(root_mountflags & MS_RDONLY))
		mode |= FMODE_WRITE;
	retval = blkdev_get(bdev, mode, 0, BDEV_FS);
	if (retval == -EROFS) {
		root_mountflags |= MS_RDONLY;
		goto retry;
	}
	if (retval) {
	        /*
		 * Allow the user to distinguish between failed open
		 * and bad superblock on root device.
		 */
		printk ("VFS: Cannot open root device \"%s\" or %s\n",
			root_device_name, kdevname (ROOT_DEV));
		printk ("Please append a correct \"root=\" boot option\n");
		panic("VFS: Unable to mount root fs on %s",
			kdevname(ROOT_DEV));
	}

	check_disk_change(ROOT_DEV);
	sb = get_super(ROOT_DEV);
	if (sb) {
		/* FIXME */
		p = (char *)sb->s_type->name;
		atomic_inc(&sb->s_active);
		up_read(&sb->s_umount);
		down_write(&sb->s_umount);
		goto mount_it;
	}

	for (p = fs_names; *p; p += strlen(p)+1) {
		struct file_system_type * fs_type = get_fs_type(p);
		if (!fs_type)
  			continue;
  		sb = read_super(ROOT_DEV, bdev, fs_type,
				root_mountflags, root_mount_data);
		if (sb) 
			goto mount_it;
		put_filesystem(fs_type);
	}
	panic("VFS: Unable to mount root fs on %s", kdevname(ROOT_DEV));

mount_it:
	/* FIXME */
	up_write(&sb->s_umount);
	printk ("VFS: Mounted root (%s filesystem)%s.\n", p,
		(sb->s_flags & MS_RDONLY) ? " readonly" : "");
	putname(fs_names);
	if (path_start >= 0) {
		name = path + path_start;
		devfs_mk_symlink (NULL, "root", DEVFS_FL_DEFAULT,
				  name + 5, NULL, NULL);
		memcpy (name, "/dev/", 5);
	}
	vfsmnt = alloc_vfsmnt();
	if (!vfsmnt)
		panic("VFS: alloc_vfsmnt failed for root fs");

	set_devname(vfsmnt, name);
	vfsmnt->mnt_sb = sb;
	vfsmnt->mnt_root = dget(sb->s_root);
	bdput(bdev); /* sb holds a reference */


#ifdef CONFIG_ROOT_NFS
attach_it:
#endif
	root_nd.mnt = root_vfsmnt;
	root_nd.dentry = root_vfsmnt->mnt_sb->s_root;
	graft_tree(vfsmnt, &root_nd);

	set_fs_root(current->fs, vfsmnt, vfsmnt->mnt_root);
	set_fs_pwd(current->fs, vfsmnt, vfsmnt->mnt_root);

	mntput(vfsmnt);
}
