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
#include <linux/malloc.h>
#include <linux/locks.h>
#include <linux/smp_lock.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/fd.h>
#include <linux/init.h>
#include <linux/quotaops.h>
#include <linux/acct.h>

#include <asm/uaccess.h>

#include <linux/nfs_fs.h>
#include <linux/nfs_fs_sb.h>
#include <linux/nfs_mount.h>

#include <linux/kmod.h>
#define __NO_VERSION__
#include <linux/module.h>

/*
 * We use a semaphore to synchronize all mount/umount
 * activity - imagine the mess if we have a race between
 * unmounting a filesystem and re-mounting it (or something
 * else).
 */
static DECLARE_MUTEX(mount_sem);

extern void wait_for_keypress(void);

extern int root_mountflags;

static int do_remount_sb(struct super_block *sb, int flags, char * data);

/* this is initialized in init/main.c */
kdev_t ROOT_DEV;

int nr_super_blocks;
int max_super_blocks = NR_SUPER;
LIST_HEAD(super_blocks);

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

static LIST_HEAD(vfsmntlist);

/**
 *	add_vfsmnt - add a new mount node
 *	@nd: location of mountpoint or %NULL if we want a root node
 *	@root: root of (sub)tree to be mounted
 *	@dev_name: device name to show in /proc/mounts or %NULL (for "none").
 *
 *	This is VFS idea of mount. New node is allocated, bound to a tree
 *	we are mounting and optionally (OK, usually) registered as mounted
 *	on a given mountpoint. Returns a pointer to new node or %NULL in
 *	case of failure.
 *
 *	Potential reason for failure (aside of trivial lack of memory) is a
 *	deleted mountpoint. Caller must hold ->i_zombie on mountpoint
 *	dentry (if any).
 *
 *	Node is marked as MNT_VISIBLE (visible in /proc/mounts) unless both
 *	@nd and @devname are %NULL. It works since we pass non-%NULL @devname
 *	when we are mounting root and kern_mount() filesystems are deviceless.
 *	If we will get a kern_mount() filesystem with nontrivial @devname we
 *	will have to pass the visibility flag explicitly, so if we will add
 *	support for such beasts we'll have to change prototype.
 */

static struct vfsmount *add_vfsmnt(struct nameidata *nd,
				struct dentry *root,
				const char *dev_name)
{
	struct vfsmount *mnt;
	struct super_block *sb = root->d_inode->i_sb;
	char *name;

	mnt = kmalloc(sizeof(struct vfsmount), GFP_KERNEL);
	if (!mnt)
		goto out;
	memset(mnt, 0, sizeof(struct vfsmount));

	if (nd || dev_name)
		mnt->mnt_flags = MNT_VISIBLE;

	/* It may be NULL, but who cares? */
	if (dev_name) {
		name = kmalloc(strlen(dev_name)+1, GFP_KERNEL);
		if (name) {
			strcpy(name, dev_name);
			mnt->mnt_devname = name;
		}
	}
	mnt->mnt_owner = current->uid;
	atomic_set(&mnt->mnt_count,1);
	mnt->mnt_sb = sb;

	spin_lock(&dcache_lock);
	if (nd && !IS_ROOT(nd->dentry) && d_unhashed(nd->dentry))
		goto fail;
	mnt->mnt_root = dget(root);
	mnt->mnt_mountpoint = nd ? dget(nd->dentry) : dget(root);
	mnt->mnt_parent = nd ? mntget(nd->mnt) : mnt;

	if (nd) {
		list_add(&mnt->mnt_child, &nd->mnt->mnt_mounts);
		list_add(&mnt->mnt_clash, &nd->dentry->d_vfsmnt);
	} else {
		INIT_LIST_HEAD(&mnt->mnt_child);
		INIT_LIST_HEAD(&mnt->mnt_clash);
	}
	INIT_LIST_HEAD(&mnt->mnt_mounts);
	list_add(&mnt->mnt_instances, &sb->s_mounts);
	list_add(&mnt->mnt_list, vfsmntlist.prev);
	spin_unlock(&dcache_lock);
out:
	return mnt;
fail:
	spin_unlock(&dcache_lock);
	if (mnt->mnt_devname)
		kfree(mnt->mnt_devname);
	kfree(mnt);
	return NULL;
}

static void move_vfsmnt(struct vfsmount *mnt,
			struct dentry *mountpoint,
			struct vfsmount *parent,
			const char *dev_name)
{
	struct dentry *old_mountpoint;
	struct vfsmount *old_parent;
	char *new_devname = NULL;

	if (dev_name) {
		new_devname = kmalloc(strlen(dev_name)+1, GFP_KERNEL);
		if (new_devname)
			strcpy(new_devname, dev_name);
	}

	spin_lock(&dcache_lock);
	old_mountpoint = mnt->mnt_mountpoint;
	old_parent = mnt->mnt_parent;

	/* flip names */
	if (new_devname) {
		if (mnt->mnt_devname)
			kfree(mnt->mnt_devname);
		mnt->mnt_devname = new_devname;
	}

	/* flip the linkage */
	mnt->mnt_mountpoint = dget(mountpoint);
	mnt->mnt_parent = parent ? mntget(parent) : mnt;
	list_del(&mnt->mnt_clash);
	list_del(&mnt->mnt_child);
	if (parent) {
		list_add(&mnt->mnt_child, &parent->mnt_mounts);
		list_add(&mnt->mnt_clash, &mountpoint->d_vfsmnt);
	} else {
		INIT_LIST_HEAD(&mnt->mnt_child);
		INIT_LIST_HEAD(&mnt->mnt_clash);
	}
	spin_unlock(&dcache_lock);

	/* put the old stuff */
	dput(old_mountpoint);
	if (old_parent != mnt)
		mntput(old_parent);
}

/*
 * Called with spinlock held, releases it.
 */
static void remove_vfsmnt(struct vfsmount *mnt)
{
	/* First of all, remove it from all lists */
	list_del(&mnt->mnt_instances);
	list_del(&mnt->mnt_clash);
	list_del(&mnt->mnt_list);
	list_del(&mnt->mnt_child);
	spin_unlock(&dcache_lock);
	/* Now we can work safely */
	if (mnt->mnt_parent != mnt)
		mntput(mnt->mnt_parent);

	dput(mnt->mnt_mountpoint);
	dput(mnt->mnt_root);
	if (mnt->mnt_devname)
		kfree(mnt->mnt_devname);
	kfree(mnt);
}


/* Use octal escapes, like mount does, for embedded spaces etc. */
static unsigned char need_escaping[] = { ' ', '\t', '\n', '\\' };

static int
mangle(const unsigned char *s, char *buf, int len) {
        char *sp;
        int n;

        sp = buf;
        while(*s && sp-buf < len-3) {
                for (n = 0; n < sizeof(need_escaping); n++) {
                        if (*s == need_escaping[n]) {
                                *sp++ = '\\';
                                *sp++ = '0' + ((*s & 0300) >> 6);
                                *sp++ = '0' + ((*s & 070) >> 3);
                                *sp++ = '0' + (*s & 07);
                                goto next;
                        }
                }
                *sp++ = *s;
        next:
                s++;
        }
        return sp - buf;	/* no trailing NUL */
}

static struct proc_fs_info {
	int flag;
	char *str;
} fs_info[] = {
	{ MS_NOEXEC, ",noexec" },
	{ MS_NOSUID, ",nosuid" },
	{ MS_NODEV, ",nodev" },
	{ MS_SYNCHRONOUS, ",sync" },
	{ MS_MANDLOCK, ",mand" },
	{ MS_NOATIME, ",noatime" },
	{ MS_NODIRATIME, ",nodiratime" },
#ifdef MS_NOSUB			/* Can't find this except in mount.c */
	{ MS_NOSUB, ",nosub" },
#endif
	{ 0, NULL }
};

static struct proc_nfs_info {
	int flag;
	char *str;
	char *nostr;
} nfs_info[] = {
	{ NFS_MOUNT_SOFT, ",soft", ",hard" },
	{ NFS_MOUNT_INTR, ",intr", "" },
	{ NFS_MOUNT_POSIX, ",posix", "" },
	{ NFS_MOUNT_TCP, ",tcp", ",udp" },
	{ NFS_MOUNT_NOCTO, ",nocto", "" },
	{ NFS_MOUNT_NOAC, ",noac", "" },
	{ NFS_MOUNT_NONLM, ",nolock", ",lock" },
	{ NFS_MOUNT_BROKEN_SUID, ",broken_suid", "" },
	{ 0, NULL, NULL }
};

int get_filesystem_info( char *buf )
{
	struct list_head *p;
	struct proc_fs_info *fs_infop;
	struct proc_nfs_info *nfs_infop;
	struct nfs_server *nfss;
	int len, prevlen;
	char *path, *buffer = (char *) __get_free_page(GFP_KERNEL);

	if (!buffer) return 0;
	len = prevlen = 0;

#define FREEROOM	((int)PAGE_SIZE-200-len)
#define MANGLE(s)	len += mangle((s), buf+len, FREEROOM);

	for (p = vfsmntlist.next; p != &vfsmntlist; p = p->next) {
		struct vfsmount *tmp = list_entry(p, struct vfsmount, mnt_list);
		if (!(tmp->mnt_flags & MNT_VISIBLE))
			continue;
		path = d_path(tmp->mnt_root, tmp, buffer, PAGE_SIZE);
		if (!path)
			continue;
		MANGLE(tmp->mnt_devname ? tmp->mnt_devname : "none");
		buf[len++] = ' ';
		MANGLE(path);
		buf[len++] = ' ';
		MANGLE(tmp->mnt_sb->s_type->name);
		len += sprintf(buf+len, " %s",
			       tmp->mnt_sb->s_flags & MS_RDONLY ? "ro" : "rw");
		for (fs_infop = fs_info; fs_infop->flag; fs_infop++) {
			if (tmp->mnt_sb->s_flags & fs_infop->flag)
				MANGLE(fs_infop->str);
		}
		if (!strcmp("nfs", tmp->mnt_sb->s_type->name)) {
			nfss = &tmp->mnt_sb->u.nfs_sb.s_server;
			len += sprintf(buf+len, ",v%d", nfss->rpc_ops->version);

			len += sprintf(buf+len, ",rsize=%d", nfss->rsize);

			len += sprintf(buf+len, ",wsize=%d", nfss->wsize);
#if 0
			if (nfss->timeo != 7*HZ/10) {
				len += sprintf(buf+len, ",timeo=%d",
					       nfss->timeo*10/HZ);
			}
			if (nfss->retrans != 3) {
				len += sprintf(buf+len, ",retrans=%d",
					       nfss->retrans);
			}
#endif
			if (nfss->acregmin != 3*HZ) {
				len += sprintf(buf+len, ",acregmin=%d",
					       nfss->acregmin/HZ);
			}
			if (nfss->acregmax != 60*HZ) {
				len += sprintf(buf+len, ",acregmax=%d",
					       nfss->acregmax/HZ);
			}
			if (nfss->acdirmin != 30*HZ) {
				len += sprintf(buf+len, ",acdirmin=%d",
					       nfss->acdirmin/HZ);
			}
			if (nfss->acdirmax != 60*HZ) {
				len += sprintf(buf+len, ",acdirmax=%d",
					       nfss->acdirmax/HZ);
			}
			for (nfs_infop = nfs_info; nfs_infop->flag; nfs_infop++) {
				char *str;
				if (nfss->flags & nfs_infop->flag)
					str = nfs_infop->str;
				else
					str = nfs_infop->nostr;
				MANGLE(str);
			}
			len += sprintf(buf+len, ",addr=");
			MANGLE(nfss->hostname);
		}
		len += sprintf(buf + len, " 0 0\n");
		if (FREEROOM <= 3) {
			len = prevlen;
			len += sprintf(buf+len, "# truncated\n");
			break;
		}
		prevlen = len;
	}

	free_page((unsigned long) buffer);
	return len;
#undef MANGLE
#undef FREEROOM
}

/**
 *	__wait_on_super	- wait on a superblock
 *	@sb: superblock to wait on
 *
 *	Waits for a superblock to become unlocked and then returns. It does
 *	not take the lock. This is an internal function. See wait_on_super().
 */
 
void __wait_on_super(struct super_block * sb)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&sb->s_wait, &wait);
repeat:
	set_current_state(TASK_UNINTERRUPTIBLE);
	if (sb->s_lock) {
		schedule();
		goto repeat;
	}
	remove_wait_queue(&sb->s_wait, &wait);
	current->state = TASK_RUNNING;
}

/*
 * Note: check the dirty flag before waiting, so we don't
 * hold up the sync while mounting a device. (The newly
 * mounted device won't need syncing.)
 */
void sync_supers(kdev_t dev)
{
	struct super_block * sb;

	for (sb = sb_entry(super_blocks.next);
	     sb != sb_entry(&super_blocks); 
	     sb = sb_entry(sb->s_list.next)) {
		if (!sb->s_dev)
			continue;
		if (dev && sb->s_dev != dev)
			continue;
		if (!sb->s_dirt)
			continue;
		lock_super(sb);
		if (sb->s_dev && sb->s_dirt && (!dev || dev == sb->s_dev))
			if (sb->s_op && sb->s_op->write_super)
				sb->s_op->write_super(sb);
		unlock_super(sb);
	}
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
	s = sb_entry(super_blocks.next);
	while (s != sb_entry(&super_blocks))
		if (s->s_dev == dev) {
			wait_on_super(s);
			if (s->s_dev == dev)
				return s;
			goto restart;
		} else
			s = sb_entry(s->s_list.next);
	return NULL;
}

asmlinkage long sys_ustat(dev_t dev, struct ustat * ubuf)
{
        struct super_block *s;
        struct ustat tmp;
        struct statfs sbuf;
	int err = -EINVAL;

	lock_kernel();
        s = get_super(to_kdev_t(dev));
	unlock_kernel();
        if (s == NULL)
                goto out;
	err = vfs_statfs(s, &sbuf);
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
 
struct super_block *get_empty_super(void)
{
	struct super_block *s;

	for (s  = sb_entry(super_blocks.next);
	     s != sb_entry(&super_blocks); 
	     s  = sb_entry(s->s_list.next)) {
		if (s->s_dev)
			continue;
		if (!s->s_lock)
			return s;
		printk("VFS: empty superblock %p locked!\n", s);
	}
	/* Need a new one... */
	if (nr_super_blocks >= max_super_blocks)
		return NULL;
	s = kmalloc(sizeof(struct super_block),  GFP_USER);
	if (s) {
		nr_super_blocks++;
		memset(s, 0, sizeof(struct super_block));
		INIT_LIST_HEAD(&s->s_dirty);
		list_add (&s->s_list, super_blocks.prev);
		init_waitqueue_head(&s->s_wait);
		INIT_LIST_HEAD(&s->s_files);
		INIT_LIST_HEAD(&s->s_mounts);
	}
	return s;
}

static struct super_block * read_super(kdev_t dev, struct block_device *bdev,
				       struct file_system_type *type, int flags,
				       void *data, int silent)
{
	struct super_block * s;
	s = get_empty_super();
	if (!s)
		goto out;
	s->s_dev = dev;
	s->s_bdev = bdev;
	s->s_flags = flags;
	s->s_dirt = 0;
	sema_init(&s->s_vfs_rename_sem,1);
	sema_init(&s->s_nfsd_free_path_sem,1);
	s->s_type = type;
	sema_init(&s->s_dquot.dqio_sem, 1);
	sema_init(&s->s_dquot.dqoff_sem, 1);
	s->s_dquot.flags = 0;
	lock_super(s);
	if (!type->read_super(s, data, silent))
		goto out_fail;
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
	return NULL;
}

/*
 * Unnamed block devices are dummy devices used by virtual
 * filesystems which don't use real block-devices.  -- jrs
 */

static unsigned int unnamed_dev_in_use[256/(8*sizeof(unsigned int))];

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
	struct super_block * sb;
	struct nameidata nd;
	kdev_t dev;
	int error = 0;
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
	if (IS_NODEV(inode))
		goto out;
	bdev = inode->i_bdev;
	bdops = devfs_get_ops ( devfs_get_handle_from_inode (inode) );
	if (bdops) bdev->bd_op = bdops;
	/* Done with lookups, semaphore down */
	down(&mount_sem);
	dev = to_kdev_t(bdev->bd_dev);
	sb = get_super(dev);
	if (sb) {
		if (fs_type == sb->s_type &&
		    ((flags ^ sb->s_flags) & MS_RDONLY) == 0) {
			path_release(&nd);
			return sb;
		}
	} else {
		mode_t mode = FMODE_READ; /* we always need it ;-) */
		if (!(flags & MS_RDONLY))
			mode |= FMODE_WRITE;
		error = blkdev_get(bdev, mode, 0, BDEV_FS);
		if (error)
			goto out;
		check_disk_change(dev);
		error = -EACCES;
		if (!(flags & MS_RDONLY) && is_read_only(dev))
			goto out1;
		error = -EINVAL;
		sb = read_super(dev, bdev, fs_type, flags, data, 0);
		if (sb) {
			get_filesystem(fs_type);
			path_release(&nd);
			return sb;
		}
out1:
		blkdev_put(bdev, BDEV_FS);
	}
out:
	path_release(&nd);
	up(&mount_sem);
	return ERR_PTR(error);
}

static struct super_block *get_sb_nodev(struct file_system_type *fs_type,
	int flags, void * data)
{
	kdev_t dev;
	int error = -EMFILE;
	down(&mount_sem);
	dev = get_unnamed_dev();
	if (dev) {
		struct super_block * sb;
		error = -EINVAL;
		sb = read_super(dev, NULL, fs_type, flags, data, 0);
		if (sb) {
			get_filesystem(fs_type);
			return sb;
		}
		put_unnamed_dev(dev);
	}
	up(&mount_sem);
	return ERR_PTR(error);
}

static struct super_block *get_sb_single(struct file_system_type *fs_type,
	int flags, void *data)
{
	struct super_block * sb;
	/*
	 * Get the superblock of kernel-wide instance, but
	 * keep the reference to fs_type.
	 */
	down(&mount_sem);
	sb = fs_type->kern_mnt->mnt_sb;
	if (!sb)
		BUG();
	get_filesystem(fs_type);
	do_remount_sb(sb, flags, data);
	return sb;
}

static void kill_super(struct super_block *sb, int umount_root)
{
	struct block_device *bdev;
	kdev_t dev;
	struct dentry *root = sb->s_root;
	struct file_system_type *fs = sb->s_type;
	struct super_operations *sop = sb->s_op;

	sb->s_root = NULL;
	/* Need to clean after the sucker */
	if (fs->fs_flags & FS_LITTER)
		d_genocide(root);
	if (fs->fs_flags & (FS_SINGLE|FS_LITTER))
		shrink_dcache_parent(root);
	dput(root);
	lock_super(sb);
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
	if (umount_root) {
		/* special: the old device driver is going to be
		   a ramdisk and the point of this call is to free its
		   protected memory (even if dirty). */
		destroy_buffers(dev);
	}
	if (bdev) {
		blkdev_put(bdev, BDEV_FS);
		bdput(bdev);
	} else
		put_unnamed_dev(dev);
}

/*
 * Alters the mount flags of a mounted file system. Only the mount point
 * is used as a reference - file system type and the device are ignored.
 */

static int do_remount_sb(struct super_block *sb, int flags, char *data)
{
	int retval;
	
	if (!(flags & MS_RDONLY) && sb->s_dev && is_read_only(sb->s_dev))
		return -EACCES;
		/*flags |= MS_RDONLY;*/
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

struct vfsmount *kern_mount(struct file_system_type *type)
{
	kdev_t dev = get_unnamed_dev();
	struct super_block *sb;
	struct vfsmount *mnt;
	if (!dev)
		return ERR_PTR(-EMFILE);
	sb = read_super(dev, NULL, type, 0, NULL, 0);
	if (!sb) {
		put_unnamed_dev(dev);
		return ERR_PTR(-EINVAL);
	}
	mnt = add_vfsmnt(NULL, sb->s_root, NULL);
	if (!mnt) {
		kill_super(sb, 0);
		return ERR_PTR(-ENOMEM);
	}
	type->kern_mnt = mnt;
	return mnt;
}

/* Call only after unregister_filesystem() - it's a final cleanup */

void kern_umount(struct vfsmount *mnt)
{
	struct super_block *sb = mnt->mnt_sb;
	spin_lock(&dcache_lock);
	remove_vfsmnt(mnt);
	kill_super(sb, 0);
}

/*
 * Doesn't take quota and stuff into account. IOW, in some cases it will
 * give false negatives. The main reason why it's here is that we need
 * a non-destructive way to look for easily umountable filesystems.
 */
int may_umount(struct vfsmount *mnt)
{
	if (atomic_read(&mnt->mnt_count) > 2)
		return -EBUSY;
	return 0;
}

static int do_umount(struct vfsmount *mnt, int umount_root, int flags)
{
	struct super_block * sb = mnt->mnt_sb;

	/*
	 * No sense to grab the lock for this test, but test itself looks
	 * somewhat bogus. Suggestions for better replacement?
	 * Ho-hum... In principle, we might treat that as umount + switch
	 * to rootfs. GC would eventually take care of the old vfsmount.
	 * The problem being: we have to implement rootfs and GC for that ;-)
	 * Actually it makes sense, especially if rootfs would contain a
	 * /reboot - static binary that would close all descriptors and
	 * call reboot(9). Then init(8) could umount root and exec /reboot.
	 */
	if (mnt == current->fs->rootmnt && !umount_root) {
		int retval = 0;
		/*
		 * Special case for "unmounting" root ...
		 * we just try to remount it readonly.
		 */
		mntput(mnt);
		if (!(sb->s_flags & MS_RDONLY))
			retval = do_remount_sb(sb, MS_RDONLY, 0);
		return retval;
	}

	spin_lock(&dcache_lock);

	if (mnt->mnt_instances.next != mnt->mnt_instances.prev) {
		if (atomic_read(&mnt->mnt_count) > 2) {
			spin_unlock(&dcache_lock);
			mntput(mnt);
			return -EBUSY;
		}
		if (sb->s_type->fs_flags & FS_SINGLE)
			put_filesystem(sb->s_type);
		/* We hold two references, so mntput() is safe */
		mntput(mnt);
		remove_vfsmnt(mnt);
		return 0;
	}
	spin_unlock(&dcache_lock);

	/*
	 * Before checking whether the filesystem is still busy,
	 * make sure the kernel doesn't hold any quota files open
	 * on the device. If the umount fails, too bad -- there
	 * are no quotas running any more. Just turn them on again.
	 */
	DQUOT_OFF(sb);
	acct_auto_close(sb->s_dev);

	/*
	 * If we may have to abort operations to get out of this
	 * mount, and they will themselves hold resources we must
	 * allow the fs to do things. In the Unix tradition of
	 * 'Gee thats tricky lets do it in userspace' the umount_begin
	 * might fail to complete on the first run through as other tasks
	 * must return, and the like. Thats for the mount program to worry
	 * about for the moment.
	 */

	if( (flags&MNT_FORCE) && sb->s_op->umount_begin)
		sb->s_op->umount_begin(sb);

	/*
	 * Shrink dcache, then fsync. This guarantees that if the
	 * filesystem is quiescent at this point, then (a) only the
	 * root entry should be in use and (b) that root entry is
	 * clean.
	 */
	shrink_dcache_sb(sb);
	fsync_dev(sb->s_dev);

	if (sb->s_root->d_inode->i_state) {
		mntput(mnt);
		return -EBUSY;
	}

	/* Something might grab it again - redo checks */

	spin_lock(&dcache_lock);
	if (atomic_read(&mnt->mnt_count) > 2) {
		spin_unlock(&dcache_lock);
		mntput(mnt);
		return -EBUSY;
	}

	/* OK, that's the point of no return */
	mntput(mnt);
	remove_vfsmnt(mnt);

	kill_super(sb, umount_root);
	return 0;
}

/*
 * Now umount can handle mount points as well as block devices.
 * This is important for filesystems which use unnamed block devices.
 *
 * We now support a flag for forced unmount like the other 'big iron'
 * unixes. Our API is identical to OSF/1 to avoid making a mess of AMD
 */

asmlinkage long sys_umount(char * name, int flags)
{
	struct nameidata nd;
	char *kname;
	int retval;

	lock_kernel();
	kname = getname(name);
	retval = PTR_ERR(kname);
	if (IS_ERR(kname))
		goto out;
	retval = 0;
	if (path_init(kname, LOOKUP_POSITIVE|LOOKUP_FOLLOW, &nd))
		retval = path_walk(kname, &nd);
	putname(kname);
	if (retval)
		goto out;
	retval = -EINVAL;
	if (nd.dentry != nd.mnt->mnt_root)
		goto dput_and_out;

	retval = -EPERM;
	if (!capable(CAP_SYS_ADMIN) && current->uid!=nd.mnt->mnt_owner)
		goto dput_and_out;

	dput(nd.dentry);
	/* puts nd.mnt */
	down(&mount_sem);
	retval = do_umount(nd.mnt, 0, flags);
	up(&mount_sem);
	goto out;
dput_and_out:
	path_release(&nd);
out:
	unlock_kernel();
	return retval;
}

/*
 *	The 2.0 compatible umount. No flags. 
 */
 
asmlinkage long sys_oldumount(char * name)
{
	return sys_umount(name,0);
}

static int mount_is_safe(struct nameidata *nd)
{
	if (capable(CAP_SYS_ADMIN))
		return 0;
	return -EPERM;
#ifdef notyet
	if (S_ISLNK(nd->dentry->d_inode->i_mode))
		return -EPERM;
	if (nd->dentry->d_inode->i_mode & S_ISVTX) {
		if (current->uid != nd->dentry->d_inode->i_uid)
			return -EPERM;
	}
	if (permission(nd->dentry->d_inode, MAY_WRITE))
		return -EPERM;
	return 0;
#endif
}

/*
 * do loopback mount.
 */
static int do_loopback(char *old_name, char *new_name)
{
	struct nameidata old_nd, new_nd;
	int err = 0;
	if (!old_name || !*old_name)
		return -EINVAL;
	if (path_init(old_name, LOOKUP_POSITIVE, &old_nd))
		err = path_walk(old_name, &old_nd);
	if (err)
		goto out;
	if (path_init(new_name, LOOKUP_POSITIVE, &new_nd))
		err = path_walk(new_name, &new_nd);
	if (err)
		goto out1;
	err = mount_is_safe(&new_nd);
	if (err)
		goto out2;
	err = -EINVAL;
	if (S_ISDIR(new_nd.dentry->d_inode->i_mode) !=
	      S_ISDIR(old_nd.dentry->d_inode->i_mode))
		goto out2;

	err = -ENOMEM;
	if (old_nd.mnt->mnt_sb->s_type->fs_flags & FS_SINGLE)
		get_filesystem(old_nd.mnt->mnt_sb->s_type);
		
	down(&mount_sem);
	/* there we go */
	down(&new_nd.dentry->d_inode->i_zombie);
	if (IS_DEADDIR(new_nd.dentry->d_inode))
		err = -ENOENT;
	else if (add_vfsmnt(&new_nd, old_nd.dentry, old_nd.mnt->mnt_devname))
		err = 0;
	up(&new_nd.dentry->d_inode->i_zombie);
	up(&mount_sem);
	if (err && old_nd.mnt->mnt_sb->s_type->fs_flags & FS_SINGLE)
		put_filesystem(old_nd.mnt->mnt_sb->s_type);
out2:
	path_release(&new_nd);
out1:
	path_release(&old_nd);
out:
	return err;
}

/*
 * change filesystem flags. dir should be a physical root of filesystem.
 * If you've mounted a non-root directory somewhere and want to do remount
 * on it - tough luck.
 */

static int do_remount(const char *dir,int flags,char *data)
{
	struct nameidata nd;
	int retval = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (path_init(dir, LOOKUP_FOLLOW|LOOKUP_POSITIVE, &nd))
		retval = path_walk(dir, &nd);
	if (!retval) {
		struct super_block * sb = nd.dentry->d_inode->i_sb;
		retval = -ENODEV;
		if (sb) {
			retval = -EINVAL;
			if (nd.dentry == sb->s_root) {
				/*
				 * Shrink the dcache and sync the device.
				 */
				shrink_dcache_sb(sb);
				fsync_dev(sb->s_dev);
				if (flags & MS_RDONLY)
					acct_auto_close(sb->s_dev);
				retval = do_remount_sb(sb, flags, data);
			}
		}
		path_release(&nd);
	}
	return retval;
}

static int copy_mount_options (const void *data, unsigned long *where)
{
	int i;
	unsigned long page;
	unsigned long size;
	
	*where = 0;
	if (!data)
		return 0;

	if (!(page = __get_free_page(GFP_KERNEL)))
		return -ENOMEM;

	/* We only care that *some* data at the address the user
	 * gave us is valid.  Just in case, we'll zero
	 * the remainder of the page.
	 */
	/* copy_from_user cannot cross TASK_SIZE ! */
	size = TASK_SIZE - (unsigned long)data;
	if (size > PAGE_SIZE)
		size = PAGE_SIZE;

	i = size - copy_from_user((void *)page, data, size);
	if (!i) {
		free_page(page); 
		return -EFAULT;
	}
	if (i != PAGE_SIZE)
		memset((char *)page + i, 0, PAGE_SIZE - i);
	*where = page;
	return 0;
}

/*
 * Flags is a 16-bit value that allows up to 16 non-fs dependent flags to
 * be given to the mount() call (ie: read-only, no-dev, no-suid etc).
 *
 * data is a (void *) that can point to any structure up to
 * PAGE_SIZE-1 bytes, which can contain arbitrary fs-dependent
 * information (or be NULL).
 *
 * NOTE! As pre-0.97 versions of mount() didn't use this setup, the
 * flags used to have a special 16-bit magic number in the high word:
 * 0xC0ED. If this magic number is present, the high word is discarded.
 */
long do_mount(char * dev_name, char * dir_name, char *type_page,
		  unsigned long flags, void *data_page)
{
	struct file_system_type * fstype;
	struct nameidata nd;
	struct vfsmount *mnt = NULL;
	struct super_block *sb;
	int retval = 0;

	/* Discard magic */
	if ((flags & MS_MGC_MSK) == MS_MGC_VAL)
		flags &= ~MS_MGC_MSK;
 
	/* Basic sanity checks */

	if (!dir_name || !*dir_name || !memchr(dir_name, 0, PAGE_SIZE))
		return -EINVAL;
	if (dev_name && !memchr(dev_name, 0, PAGE_SIZE))
		return -EINVAL;

	/* OK, looks good, now let's see what do they want */

	/* just change the flags? - capabilities are checked in do_remount() */
	if (flags & MS_REMOUNT)
		return do_remount(dir_name, flags & ~MS_REMOUNT,
				  (char *) data_page);

	/* "mount --bind"? Equivalent to older "mount -t bind" */
	/* No capabilities? What if users do thousands of these? */
	if (flags & MS_BIND)
		return do_loopback(dev_name, dir_name);

	/* For the rest we need the type */

	if (!type_page || !memchr(type_page, 0, PAGE_SIZE))
		return -EINVAL;

#if 0	/* Can be deleted again. Introduced in patch-2.3.99-pre6 */
	/* loopback mount? This is special - requires fewer capabilities */
	if (strcmp(type_page, "bind")==0)
		return do_loopback(dev_name, dir_name);
#endif

	/* for the rest we _really_ need capabilities... */
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	/* ... filesystem driver... */
	fstype = get_fs_type(type_page);
	if (!fstype)		
		return -ENODEV;

	/* ... and mountpoint. Do the lookup first to force automounting. */
	if (path_init(dir_name,
		      LOOKUP_FOLLOW|LOOKUP_POSITIVE|LOOKUP_DIRECTORY, &nd))
		retval = path_walk(dir_name, &nd);
	if (retval)
		goto fs_out;

	/* get superblock, locks mount_sem on success */
	if (fstype->fs_flags & FS_NOMOUNT)
		sb = ERR_PTR(-EINVAL);
	else if (fstype->fs_flags & FS_REQUIRES_DEV)
		sb = get_sb_bdev(fstype, dev_name, flags, data_page);
	else if (fstype->fs_flags & FS_SINGLE)
		sb = get_sb_single(fstype, flags, data_page);
	else
		sb = get_sb_nodev(fstype, flags, data_page);

	retval = PTR_ERR(sb);
	if (IS_ERR(sb))
		goto dput_out;

	/* Something was mounted here while we slept */
	while(d_mountpoint(nd.dentry) && follow_down(&nd.mnt, &nd.dentry))
		;

	/* Refuse the same filesystem on the same mount point */
	retval = -EBUSY;
	if (nd.mnt && nd.mnt->mnt_sb == sb
	    	   && nd.mnt->mnt_root == nd.dentry)
		goto fail;

	retval = -ENOENT;
	if (!nd.dentry->d_inode)
		goto fail;
	down(&nd.dentry->d_inode->i_zombie);
	if (!IS_DEADDIR(nd.dentry->d_inode)) {
		retval = -ENOMEM;
		mnt = add_vfsmnt(&nd, sb->s_root, dev_name);
	}
	up(&nd.dentry->d_inode->i_zombie);
	if (!mnt)
		goto fail;
	retval = 0;
unlock_out:
	up(&mount_sem);
dput_out:
	path_release(&nd);
fs_out:
	put_filesystem(fstype);
	return retval;

fail:
	if (list_empty(&sb->s_mounts))
		kill_super(sb, 0);
	goto unlock_out;
}

asmlinkage long sys_mount(char * dev_name, char * dir_name, char * type,
			  unsigned long flags, void * data)
{
	int retval;
	unsigned long data_page;
	unsigned long type_page;
	unsigned long dev_page;
	char *dir_page;

	retval = copy_mount_options (type, &type_page);
	if (retval < 0)
		return retval;

	dir_page = getname(dir_name);
	retval = PTR_ERR(dir_page);
	if (IS_ERR(dir_page))
		goto out1;

	retval = copy_mount_options (dev_name, &dev_page);
	if (retval < 0)
		goto out2;

	retval = copy_mount_options (data, &data_page);
	if (retval < 0)
		goto out3;

	lock_kernel();
	retval = do_mount((char*)dev_page, dir_page, (char*)type_page,
			  flags, (void*)data_page);
	unlock_kernel();
	free_page(data_page);

out3:
	free_page(dev_page);
out2:
	putname(dir_page);
out1:
	free_page(type_page);
	return retval;
}

void __init mount_root(void)
{
	struct file_system_type * fs_type;
	struct super_block * sb;
	struct vfsmount *vfsmnt;
	struct block_device *bdev = NULL;
	mode_t mode;
	int retval;
	void *handle;
	char path[64];
	int path_start = -1;

#ifdef CONFIG_ROOT_NFS
	void *data;
	if (MAJOR(ROOT_DEV) != UNNAMED_MAJOR)
		goto skip_nfs;
	fs_type = get_fs_type("nfs");
	if (!fs_type)
		goto no_nfs;
	ROOT_DEV = get_unnamed_dev();
	if (!ROOT_DEV)
		/*
		 * Your /linuxrc sucks worse than MSExchange - that's the
		 * only way you could run out of anon devices at that point.
		 */
		goto no_anon;
	data = nfs_root_data();
	if (!data)
		goto no_server;
	sb = read_super(ROOT_DEV, NULL, fs_type, root_mountflags, data, 1);
	if (sb)
		/*
		 * We _can_ fail there, but if that will happen we have no
		 * chance anyway (no memory for vfsmnt and we _will_ need it,
		 * no matter which fs we try to mount).
		 */
		goto mount_it;
no_server:
	put_unnamed_dev(ROOT_DEV);
no_anon:
	put_filesystem(fs_type);
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
		retval = blkdev_get(bdev, FMODE_READ, 0, BDEV_FS);
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
		fs_type = sb->s_type;
		goto mount_it;
	}

	read_lock(&file_systems_lock);
	for (fs_type = file_systems ; fs_type ; fs_type = fs_type->next) {
  		if (!(fs_type->fs_flags & FS_REQUIRES_DEV))
  			continue;
		if (!try_inc_mod_count(fs_type->owner))
			continue;
		read_unlock(&file_systems_lock);
  		sb = read_super(ROOT_DEV,bdev,fs_type,root_mountflags,NULL,1);
		if (sb) 
			goto mount_it;
		read_lock(&file_systems_lock);
		put_filesystem(fs_type);
	}
	read_unlock(&file_systems_lock);
	panic("VFS: Unable to mount root fs on %s", kdevname(ROOT_DEV));

mount_it:
	printk ("VFS: Mounted root (%s filesystem)%s.\n",
		fs_type->name,
		(sb->s_flags & MS_RDONLY) ? " readonly" : "");
	if (path_start >= 0) {
		devfs_mk_symlink (NULL, "root", DEVFS_FL_DEFAULT,
				  path + 5 + path_start, NULL, NULL);
		memcpy (path + path_start, "/dev/", 5);
		vfsmnt = add_vfsmnt(NULL, sb->s_root, path + path_start);
	}
	else
		vfsmnt = add_vfsmnt(NULL, sb->s_root, "/dev/root");
	/* FIXME: if something will try to umount us right now... */
	if (vfsmnt) {
		set_fs_root(current->fs, vfsmnt, sb->s_root);
		set_fs_pwd(current->fs, vfsmnt, sb->s_root);
		if (bdev)
			bdput(bdev); /* sb holds a reference */
		return;
	}
	panic("VFS: add_vfsmnt failed for root fs");
}


static void chroot_fs_refs(struct dentry *old_root,
			   struct vfsmount *old_rootmnt,
			   struct dentry *new_root,
			   struct vfsmount *new_rootmnt)
{
	struct task_struct *p;
	struct fs_struct *fs;

	read_lock(&tasklist_lock);
	for_each_task(p) {
		task_lock(p);
		fs = p->fs;
		if (fs) {
			atomic_inc(&fs->count);
			task_unlock(p);
			if (fs->root==old_root && fs->rootmnt==old_rootmnt)
				set_fs_root(fs, new_rootmnt, new_root);
			if (fs->pwd==old_root && fs->pwdmnt==old_rootmnt)
				set_fs_pwd(fs, new_rootmnt, new_root);
			put_fs_struct(fs);
		} else
			task_unlock(p);
	}
	read_unlock(&tasklist_lock);
}

/*
 * Moves the current root to put_root, and sets root/cwd of all processes
 * which had them on the old root to new_root.
 *
 * Note:
 *  - we don't move root/cwd if they are not at the root (reason: if something
 *    cared enough to change them, it's probably wrong to force them elsewhere)
 *  - it's okay to pick a root that isn't the root of a file system, e.g.
 *    /nfs/my_root where /nfs is the mount point. Better avoid creating
 *    unreachable mount points this way, though.
 */

asmlinkage long sys_pivot_root(const char *new_root, const char *put_old)
{
	struct dentry *root;
	struct vfsmount *root_mnt;
	struct vfsmount *tmp;
	struct nameidata new_nd, old_nd;
	char *name;
	int error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	lock_kernel();

	name = getname(new_root);
	error = PTR_ERR(name);
	if (IS_ERR(name))
		goto out0;
	error = 0;
	if (path_init(name, LOOKUP_POSITIVE|LOOKUP_FOLLOW|LOOKUP_DIRECTORY, &new_nd))
		error = path_walk(name, &new_nd);
	putname(name);
	if (error)
		goto out0;

	name = getname(put_old);
	error = PTR_ERR(name);
	if (IS_ERR(name))
		goto out0;
	error = 0;
	if (path_init(name, LOOKUP_POSITIVE|LOOKUP_FOLLOW|LOOKUP_DIRECTORY, &old_nd))
		error = path_walk(name, &old_nd);
	putname(name);
	if (error)
		goto out1;

	read_lock(&current->fs->lock);
	root_mnt = mntget(current->fs->rootmnt);
	root = dget(current->fs->root);
	read_unlock(&current->fs->lock);
	down(&mount_sem);
	down(&old_nd.dentry->d_inode->i_zombie);
	error = -ENOENT;
	if (IS_DEADDIR(new_nd.dentry->d_inode))
		goto out2;
	if (d_unhashed(new_nd.dentry) && !IS_ROOT(new_nd.dentry))
		goto out2;
	if (d_unhashed(old_nd.dentry) && !IS_ROOT(old_nd.dentry))
		goto out2;
	error = -EBUSY;
	if (new_nd.mnt == root_mnt || old_nd.mnt == root_mnt)
		goto out2; /* loop */
	error = -EINVAL;
	tmp = old_nd.mnt; /* make sure we can reach put_old from new_root */
	spin_lock(&dcache_lock);
	if (tmp != new_nd.mnt) {
		for (;;) {
			if (tmp->mnt_parent == tmp)
				goto out3;
			if (tmp->mnt_parent == new_nd.mnt)
				break;
			tmp = tmp->mnt_parent;
		}
		if (!is_subdir(tmp->mnt_mountpoint, new_nd.dentry))
			goto out3;
	} else if (!is_subdir(old_nd.dentry, new_nd.dentry))
		goto out3;
	spin_unlock(&dcache_lock);

	move_vfsmnt(new_nd.mnt, new_nd.dentry, NULL, NULL);
	move_vfsmnt(root_mnt, old_nd.dentry, old_nd.mnt, NULL);
	chroot_fs_refs(root,root_mnt,new_nd.dentry,new_nd.mnt);
	error = 0;
out2:
	up(&old_nd.dentry->d_inode->i_zombie);
	up(&mount_sem);
	dput(root);
	mntput(root_mnt);
	path_release(&old_nd);
out1:
	path_release(&new_nd);
out0:
	unlock_kernel();
	return error;
out3:
	spin_unlock(&dcache_lock);
	goto out2;
}


#ifdef CONFIG_BLK_DEV_INITRD

int __init change_root(kdev_t new_root_dev,const char *put_old)
{
	struct vfsmount *old_rootmnt;
	struct nameidata devfs_nd, nd;
	int error = 0;

	read_lock(&current->fs->lock);
	old_rootmnt = mntget(current->fs->rootmnt);
	read_unlock(&current->fs->lock);
	/*  First unmount devfs if mounted  */
	if (path_init("/dev", LOOKUP_FOLLOW|LOOKUP_POSITIVE, &devfs_nd))
		error = path_walk("/dev", &devfs_nd);
	if (!error) {
		if (devfs_nd.mnt->mnt_sb->s_magic == DEVFS_SUPER_MAGIC &&
		    devfs_nd.dentry == devfs_nd.mnt->mnt_root) {
			dput(devfs_nd.dentry);
			down(&mount_sem);
			/* puts devfs_nd.mnt */
			do_umount(devfs_nd.mnt, 0, 0);
			up(&mount_sem);
		} else 
			path_release(&devfs_nd);
	}
	ROOT_DEV = new_root_dev;
	mount_root();
#if 1
	shrink_dcache();
	printk("change_root: old root has d_count=%d\n", 
	       atomic_read(&old_rootmnt->mnt_root->d_count));
#endif
	mount_devfs_fs ();
	/*
	 * Get the new mount directory
	 */
	error = 0;
	if (path_init(put_old, LOOKUP_FOLLOW|LOOKUP_POSITIVE|LOOKUP_DIRECTORY, &nd))
		error = path_walk(put_old, &nd);
	if (error) {
		int blivet;

		printk(KERN_NOTICE "Trying to unmount old root ... ");
		blivet = do_umount(old_rootmnt, 1, 0);
		if (!blivet) {
			printk("okay\n");
			return 0;
		}
		printk(KERN_ERR "error %d\n", blivet);
		return error;
	}
	/* FIXME: we should hold i_zombie on nd.dentry */
	move_vfsmnt(old_rootmnt, nd.dentry, nd.mnt, "/dev/root.old");
	mntput(old_rootmnt);
	path_release(&nd);
	return 0;
}

#endif
