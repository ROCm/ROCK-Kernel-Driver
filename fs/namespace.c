/*
 *  linux/fs/namespace.c
 *
 * (C) Copyright Al Viro 2000, 2001
 *	Released under GPL v2.
 *
 * Based on code from fs/super.c, copyright Linus Torvalds and others.
 * Heavily rewritten.
 */

#include <linux/config.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/quotaops.h>
#include <linux/acct.h>
#include <linux/module.h>
#include <linux/devfs_fs_kernel.h>

#include <asm/uaccess.h>

#include <linux/nfs_fs.h>
#include <linux/nfs_fs_sb.h>
#include <linux/nfs_mount.h>

struct vfsmount *do_kern_mount(char *type, int flags, char *name, void *data);
int do_remount_sb(struct super_block *sb, int flags, void * data);
void kill_super(struct super_block *sb);

static struct list_head *mount_hashtable;
static int hash_mask, hash_bits;
static kmem_cache_t *mnt_cache; 

static LIST_HEAD(vfsmntlist);
static DECLARE_MUTEX(mount_sem);

/* Will be static */
struct vfsmount *root_vfsmnt;

static inline unsigned long hash(struct vfsmount *mnt, struct dentry *dentry)
{
	unsigned long tmp = ((unsigned long) mnt / L1_CACHE_BYTES);
	tmp += ((unsigned long) dentry / L1_CACHE_BYTES);
	tmp = tmp + (tmp >> hash_bits);
	return tmp & hash_mask;
}

struct vfsmount *alloc_vfsmnt(void)
{
	struct vfsmount *mnt = kmem_cache_alloc(mnt_cache, GFP_KERNEL); 
	if (mnt) {
		memset(mnt, 0, sizeof(struct vfsmount));
		atomic_set(&mnt->mnt_count,1);
		INIT_LIST_HEAD(&mnt->mnt_hash);
		INIT_LIST_HEAD(&mnt->mnt_child);
		INIT_LIST_HEAD(&mnt->mnt_mounts);
		INIT_LIST_HEAD(&mnt->mnt_list);
	}
	return mnt;
}

void free_vfsmnt(struct vfsmount *mnt)
{
	if (mnt->mnt_devname)
		kfree(mnt->mnt_devname);
	kmem_cache_free(mnt_cache, mnt);
}

void set_devname(struct vfsmount *mnt, const char *name)
{
	if (name) {
		int size = strlen(name)+1;
		char * newname = kmalloc(size, GFP_KERNEL);
		if (newname) {
			memcpy(newname, name, size);
			mnt->mnt_devname = newname;
		}
	}
}

struct vfsmount *lookup_mnt(struct vfsmount *mnt, struct dentry *dentry)
{
	struct list_head * head = mount_hashtable + hash(mnt, dentry);
	struct list_head * tmp = head;
	struct vfsmount *p;

	for (;;) {
		tmp = tmp->next;
		p = NULL;
		if (tmp == head)
			break;
		p = list_entry(tmp, struct vfsmount, mnt_hash);
		if (p->mnt_parent == mnt && p->mnt_mountpoint == dentry)
			break;
	}
	return p;
}

static int check_mnt(struct vfsmount *mnt)
{
	spin_lock(&dcache_lock);
	while (mnt->mnt_parent != mnt)
		mnt = mnt->mnt_parent;
	spin_unlock(&dcache_lock);
	return mnt == root_vfsmnt;
}

static void detach_mnt(struct vfsmount *mnt, struct nameidata *old_nd)
{
	old_nd->dentry = mnt->mnt_mountpoint;
	old_nd->mnt = mnt->mnt_parent;
	mnt->mnt_parent = mnt;
	mnt->mnt_mountpoint = mnt->mnt_root;
	list_del_init(&mnt->mnt_child);
	list_del_init(&mnt->mnt_hash);
	old_nd->dentry->d_mounted--;
}

static void attach_mnt(struct vfsmount *mnt, struct nameidata *nd)
{
	mnt->mnt_parent = mntget(nd->mnt);
	mnt->mnt_mountpoint = dget(nd->dentry);
	list_add(&mnt->mnt_hash, mount_hashtable+hash(nd->mnt, nd->dentry));
	list_add(&mnt->mnt_child, &nd->mnt->mnt_mounts);
	nd->dentry->d_mounted++;
}

static struct vfsmount *next_mnt(struct vfsmount *p, struct vfsmount *root)
{
	struct list_head *next = p->mnt_mounts.next;
	if (next == &p->mnt_mounts) {
		while (1) {
			if (p == root)
				return NULL;
			next = p->mnt_child.next;
			if (next != &p->mnt_parent->mnt_mounts)
				break;
			p = p->mnt_parent;
		}
	}
	return list_entry(next, struct vfsmount, mnt_child);
}

static struct vfsmount *
clone_mnt(struct vfsmount *old, struct dentry *root)
{
	struct super_block *sb = old->mnt_sb;
	struct vfsmount *mnt = alloc_vfsmnt();

	if (mnt) {
		mnt->mnt_flags = old->mnt_flags;
		set_devname(mnt, old->mnt_devname);
		atomic_inc(&sb->s_active);
		mnt->mnt_sb = sb;
		mnt->mnt_root = dget(root);
	}
	return mnt;
}

void __mntput(struct vfsmount *mnt)
{
	struct super_block *sb = mnt->mnt_sb;
	dput(mnt->mnt_root);
	free_vfsmnt(mnt);
	kill_super(sb);
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
	{ MS_SYNCHRONOUS, ",sync" },
	{ MS_MANDLOCK, ",mand" },
	{ MS_NOATIME, ",noatime" },
	{ MS_NODIRATIME, ",nodiratime" },
	{ 0, NULL }
};

static struct proc_fs_info mnt_info[] = {
	{ MNT_NOSUID, ",nosuid" },
	{ MNT_NODEV, ",nodev" },
	{ MNT_NOEXEC, ",noexec" },
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
		for (fs_infop = mnt_info; fs_infop->flag; fs_infop++) {
			if (tmp->mnt_flags & fs_infop->flag)
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

void umount_tree(struct vfsmount *mnt)
{
	struct vfsmount *p;
	LIST_HEAD(kill);

	for (p = mnt; p; p = next_mnt(p, mnt)) {
		list_del(&p->mnt_list);
		list_add(&p->mnt_list, &kill);
	}

	while (!list_empty(&kill)) {
		mnt = list_entry(kill.next, struct vfsmount, mnt_list);
		list_del_init(&mnt->mnt_list);
		if (mnt->mnt_parent == mnt) {
			spin_unlock(&dcache_lock);
		} else {
			struct nameidata old_nd;
			detach_mnt(mnt, &old_nd);
			spin_unlock(&dcache_lock);
			path_release(&old_nd);
		}
		mntput(mnt);
		spin_lock(&dcache_lock);
	}
}

static int do_umount(struct vfsmount *mnt, int flags)
{
	struct super_block * sb = mnt->mnt_sb;
	int retval = 0;

	/*
	 * If we may have to abort operations to get out of this
	 * mount, and they will themselves hold resources we must
	 * allow the fs to do things. In the Unix tradition of
	 * 'Gee thats tricky lets do it in userspace' the umount_begin
	 * might fail to complete on the first run through as other tasks
	 * must return, and the like. Thats for the mount program to worry
	 * about for the moment.
	 */

	lock_kernel();
	if( (flags&MNT_FORCE) && sb->s_op->umount_begin)
		sb->s_op->umount_begin(sb);
	unlock_kernel();

	/*
	 * No sense to grab the lock for this test, but test itself looks
	 * somewhat bogus. Suggestions for better replacement?
	 * Ho-hum... In principle, we might treat that as umount + switch
	 * to rootfs. GC would eventually take care of the old vfsmount.
	 * Actually it makes sense, especially if rootfs would contain a
	 * /reboot - static binary that would close all descriptors and
	 * call reboot(9). Then init(8) could umount root and exec /reboot.
	 */
	if (mnt == current->fs->rootmnt && !(flags & MNT_DETACH)) {
		/*
		 * Special case for "unmounting" root ...
		 * we just try to remount it readonly.
		 */
		down_write(&sb->s_umount);
		if (!(sb->s_flags & MS_RDONLY)) {
			lock_kernel();
			retval = do_remount_sb(sb, MS_RDONLY, 0);
			unlock_kernel();
		}
		up_write(&sb->s_umount);
		return retval;
	}

	down(&mount_sem);
	spin_lock(&dcache_lock);

	if (atomic_read(&sb->s_active) == 1) {
		/* last instance - try to be smart */
		spin_unlock(&dcache_lock);
		lock_kernel();
		DQUOT_OFF(sb);
		acct_auto_close(sb->s_dev);
		unlock_kernel();
		spin_lock(&dcache_lock);
	}
	retval = -EBUSY;
	if (atomic_read(&mnt->mnt_count) == 2 || flags & MNT_DETACH) {
		if (!list_empty(&mnt->mnt_list))
			umount_tree(mnt);
		retval = 0;
	}
	spin_unlock(&dcache_lock);
	up(&mount_sem);
	return retval;
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
	if (!check_mnt(nd.mnt))
		goto dput_and_out;

	retval = -EPERM;
	if (!capable(CAP_SYS_ADMIN))
		goto dput_and_out;

	retval = do_umount(nd.mnt, flags);
dput_and_out:
	path_release(&nd);
out:
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

static struct vfsmount *copy_tree(struct vfsmount *mnt, struct dentry *dentry)
{
	struct vfsmount *p, *next, *q, *res;
	struct nameidata nd;

	p = mnt;
	res = nd.mnt = q = clone_mnt(p, dentry);
	if (!q)
		goto Enomem;
	q->mnt_parent = q;
	q->mnt_mountpoint = p->mnt_mountpoint;

	while ( (next = next_mnt(p, mnt)) != NULL) {
		while (p != next->mnt_parent) {
			p = p->mnt_parent;
			q = q->mnt_parent;
		}
		p = next;
		nd.mnt = q;
		nd.dentry = p->mnt_mountpoint;
		q = clone_mnt(p, p->mnt_root);
		if (!q)
			goto Enomem;
		spin_lock(&dcache_lock);
		list_add_tail(&q->mnt_list, &res->mnt_list);
		attach_mnt(q, &nd);
		spin_unlock(&dcache_lock);
	}
	return res;
Enomem:
	if (res) {
		spin_lock(&dcache_lock);
		umount_tree(res);
		spin_unlock(&dcache_lock);
	}
	return NULL;
}

/* Will become static */
int graft_tree(struct vfsmount *mnt, struct nameidata *nd)
{
	int err;
	if (mnt->mnt_sb->s_flags & MS_NOUSER)
		return -EINVAL;

	if (S_ISDIR(nd->dentry->d_inode->i_mode) !=
	      S_ISDIR(mnt->mnt_root->d_inode->i_mode))
		return -ENOTDIR;

	err = -ENOENT;
	down(&nd->dentry->d_inode->i_zombie);
	if (IS_DEADDIR(nd->dentry->d_inode))
		goto out_unlock;

	spin_lock(&dcache_lock);
	if (IS_ROOT(nd->dentry) || !d_unhashed(nd->dentry)) {
		struct list_head head;
		attach_mnt(mnt, nd);
		list_add_tail(&head, &mnt->mnt_list);
		list_splice(&head, vfsmntlist.prev);
		mntget(mnt);
		err = 0;
	}
	spin_unlock(&dcache_lock);
out_unlock:
	up(&nd->dentry->d_inode->i_zombie);
	return err;
}

/*
 * do loopback mount.
 */
static int do_loopback(struct nameidata *nd, char *old_name, int recurse)
{
	struct nameidata old_nd;
	struct vfsmount *mnt = NULL;
	int err = mount_is_safe(nd);
	if (err)
		return err;
	if (!old_name || !*old_name)
		return -EINVAL;
	if (path_init(old_name, LOOKUP_POSITIVE|LOOKUP_FOLLOW, &old_nd))
		err = path_walk(old_name, &old_nd);
	if (err)
		return err;

	down(&mount_sem);
	err = -EINVAL;
	if (check_mnt(nd->mnt) && (!recurse || check_mnt(old_nd.mnt))) {
		err = -ENOMEM;
		if (recurse)
			mnt = copy_tree(old_nd.mnt, old_nd.dentry);
		else
			mnt = clone_mnt(old_nd.mnt, old_nd.dentry);
	}

	if (mnt) {
		err = graft_tree(mnt, nd);
		if (err)
			umount_tree(mnt);
		else
			mntput(mnt);
	}

	up(&mount_sem);
	path_release(&old_nd);
	return err;
}

/*
 * change filesystem flags. dir should be a physical root of filesystem.
 * If you've mounted a non-root directory somewhere and want to do remount
 * on it - tough luck.
 */

static int do_remount(struct nameidata *nd,int flags,int mnt_flags,void *data)
{
	int err;
	struct super_block * sb = nd->mnt->mnt_sb;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!check_mnt(nd->mnt))
		return -EINVAL;

	if (nd->dentry != nd->mnt->mnt_root)
		return -EINVAL;

	down_write(&sb->s_umount);
	err = do_remount_sb(sb, flags, data);
	if (!err)
		nd->mnt->mnt_flags=mnt_flags;
	up_write(&sb->s_umount);
	return err;
}

static int do_add_mount(struct nameidata *nd, char *type, int flags,
			int mnt_flags, char *name, void *data)
{
	struct vfsmount *mnt = do_kern_mount(type, flags, name, data);
	int err = PTR_ERR(mnt);

	if (IS_ERR(mnt))
		goto out;

	down(&mount_sem);
	/* Something was mounted here while we slept */
	while(d_mountpoint(nd->dentry) && follow_down(&nd->mnt, &nd->dentry))
		;
	err = -EINVAL;
	if (!check_mnt(nd->mnt))
		goto unlock;

	/* Refuse the same filesystem on the same mount point */
	err = -EBUSY;
	if (nd->mnt->mnt_sb == mnt->mnt_sb && nd->mnt->mnt_root == nd->dentry)
		goto unlock;

	mnt->mnt_flags = mnt_flags;
	err = graft_tree(mnt, nd);
unlock:
	up(&mount_sem);
	mntput(mnt);
out:
	return err;
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
 * Flags is a 32-bit value that allows up to 31 non-fs dependent flags to
 * be given to the mount() call (ie: read-only, no-dev, no-suid etc).
 *
 * data is a (void *) that can point to any structure up to
 * PAGE_SIZE-1 bytes, which can contain arbitrary fs-dependent
 * information (or be NULL).
 *
 * Pre-0.97 versions of mount() didn't have a flags word.
 * When the flags word was introduced its top half was required
 * to have the magic value 0xC0ED, and this remained so until 2.4.0-test9.
 * Therefore, if this magic number is present, it carries no information
 * and must be discarded.
 */
long do_mount(char * dev_name, char * dir_name, char *type_page,
		  unsigned long flags, void *data_page)
{
	struct nameidata nd;
	int retval = 0;
	int mnt_flags = 0;

	/* Discard magic */
	if ((flags & MS_MGC_MSK) == MS_MGC_VAL)
		flags &= ~MS_MGC_MSK;

	/* Basic sanity checks */

	if (!dir_name || !*dir_name || !memchr(dir_name, 0, PAGE_SIZE))
		return -EINVAL;
	if (dev_name && !memchr(dev_name, 0, PAGE_SIZE))
		return -EINVAL;

	/* Separate the per-mountpoint flags */
	if (flags & MS_NOSUID)
		mnt_flags |= MNT_NOSUID;
	if (flags & MS_NODEV)
		mnt_flags |= MNT_NODEV;
	if (flags & MS_NOEXEC)
		mnt_flags |= MNT_NOEXEC;
	flags &= ~(MS_NOSUID|MS_NOEXEC|MS_NODEV);

	/* ... and get the mountpoint */
	if (path_init(dir_name, LOOKUP_FOLLOW|LOOKUP_POSITIVE, &nd))
		retval = path_walk(dir_name, &nd);
	if (retval)
		return retval;

	if (flags & MS_REMOUNT)
		retval = do_remount(&nd, flags & ~MS_REMOUNT, mnt_flags,
				    data_page);
	else if (flags & MS_BIND)
		retval = do_loopback(&nd, dev_name, flags & MS_REC);
	else
		retval = do_add_mount(&nd, type_page, flags, mnt_flags,
				      dev_name, data_page);
	path_release(&nd);
	return retval;
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

static void chroot_fs_refs(struct nameidata *old_nd, struct nameidata *new_nd)
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
			if (fs->root==old_nd->dentry&&fs->rootmnt==old_nd->mnt)
				set_fs_root(fs, new_nd->mnt, new_nd->dentry);
			if (fs->pwd==old_nd->dentry&&fs->pwdmnt==old_nd->mnt)
				set_fs_pwd(fs, new_nd->mnt, new_nd->dentry);
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
 *    /nfs/my_root where /nfs is the mount point. It must be a mountpoint,
 *    though, so you may need to say mount --bind /nfs/my_root /nfs/my_root
 *    first.
 */

asmlinkage long sys_pivot_root(const char *new_root, const char *put_old)
{
	struct vfsmount *tmp;
	struct nameidata new_nd, old_nd, parent_nd, root_parent, user_nd;
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
	error = -EINVAL;
	if (!check_mnt(new_nd.mnt))
		goto out1;

	name = getname(put_old);
	error = PTR_ERR(name);
	if (IS_ERR(name))
		goto out1;
	error = 0;
	if (path_init(name, LOOKUP_POSITIVE|LOOKUP_FOLLOW|LOOKUP_DIRECTORY, &old_nd))
		error = path_walk(name, &old_nd);
	putname(name);
	if (error)
		goto out1;

	read_lock(&current->fs->lock);
	user_nd.mnt = mntget(current->fs->rootmnt);
	user_nd.dentry = dget(current->fs->root);
	read_unlock(&current->fs->lock);
	down(&mount_sem);
	down(&old_nd.dentry->d_inode->i_zombie);
	error = -EINVAL;
	if (!check_mnt(user_nd.mnt))
		goto out2;
	error = -ENOENT;
	if (IS_DEADDIR(new_nd.dentry->d_inode))
		goto out2;
	if (d_unhashed(new_nd.dentry) && !IS_ROOT(new_nd.dentry))
		goto out2;
	if (d_unhashed(old_nd.dentry) && !IS_ROOT(old_nd.dentry))
		goto out2;
	error = -EBUSY;
	if (new_nd.mnt == user_nd.mnt || old_nd.mnt == user_nd.mnt)
		goto out2; /* loop */
	error = -EINVAL;
	if (user_nd.mnt->mnt_root != user_nd.dentry)
		goto out2;
	if (new_nd.mnt->mnt_root != new_nd.dentry)
		goto out2; /* not a mountpoint */
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
	detach_mnt(new_nd.mnt, &parent_nd);
	detach_mnt(user_nd.mnt, &root_parent);
	attach_mnt(user_nd.mnt, &old_nd);
	attach_mnt(new_nd.mnt, &root_parent);
	spin_unlock(&dcache_lock);
	chroot_fs_refs(&user_nd, &new_nd);
	error = 0;
	path_release(&root_parent);
	path_release(&parent_nd);
out2:
	up(&old_nd.dentry->d_inode->i_zombie);
	up(&mount_sem);
	path_release(&user_nd);
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

/*
 * Absolutely minimal fake fs - only empty root directory and nothing else.
 * In 2.5 we'll use ramfs or tmpfs, but for now it's all we need - just
 * something to go with root vfsmount.
 */
static struct dentry *rootfs_lookup(struct inode *dir, struct dentry *dentry)
{
	d_add(dentry, NULL);
	return NULL;
}
static struct file_operations rootfs_dir_operations = {
	read:		generic_read_dir,
	readdir:	dcache_readdir,
};
static struct inode_operations rootfs_dir_inode_operations = {
	lookup:		rootfs_lookup,
};
static struct super_block *rootfs_read_super(struct super_block * sb, void * data, int silent)
{
	struct inode * inode;
	struct dentry * root;
	static struct super_operations s_ops = {};
	sb->s_op = &s_ops;
	inode = new_inode(sb);
	if (!inode)
		return NULL;
	inode->i_mode = S_IFDIR|0555;
	inode->i_uid = inode->i_gid = 0;
	inode->i_op = &rootfs_dir_inode_operations;
	inode->i_fop = &rootfs_dir_operations;
	root = d_alloc_root(inode);
	if (!root) {
		iput(inode);
		return NULL;
	}
	sb->s_root = root;
	return sb;
}
static DECLARE_FSTYPE(root_fs_type, "rootfs", rootfs_read_super, FS_NOMOUNT);

static void __init init_mount_tree(void)
{
	register_filesystem(&root_fs_type);
	root_vfsmnt = do_kern_mount("rootfs", 0, "rootfs", NULL);
	if (IS_ERR(root_vfsmnt))
		panic("can't allocate root vfsmount");
}

void __init mnt_init(unsigned long mempages)
{
	struct list_head *d;
	unsigned long order;
	unsigned int nr_hash;
	int i;

	mnt_cache = kmem_cache_create("mnt_cache", sizeof(struct vfsmount),
					0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!mnt_cache)
		panic("Cannot create vfsmount cache");

	mempages >>= (16 - PAGE_SHIFT);
	mempages *= sizeof(struct list_head);
	for (order = 0; ((1UL << order) << PAGE_SHIFT) < mempages; order++)
		;

	do {
		mount_hashtable = (struct list_head *)
			__get_free_pages(GFP_ATOMIC, order);
	} while (mount_hashtable == NULL && --order >= 0);

	if (!mount_hashtable)
		panic("Failed to allocate mount hash table\n");

	/*
	 * Find the power-of-two list-heads that can fit into the allocation..
	 * We don't guarantee that "sizeof(struct list_head)" is necessarily
	 * a power-of-two.
	 */
	nr_hash = (1UL << order) * PAGE_SIZE / sizeof(struct list_head);
	hash_bits = 0;
	do {
		hash_bits++;
	} while ((nr_hash >> hash_bits) != 0);
	hash_bits--;

	/*
	 * Re-calculate the actual number of entries and the mask
	 * from the number of bits we can fit.
	 */
	nr_hash = 1UL << hash_bits;
	hash_mask = nr_hash-1;

	printk("Mount-cache hash table entries: %d (order: %ld, %ld bytes)\n",
			nr_hash, order, (PAGE_SIZE << order));

	/* And initialize the newly allocated array */
	d = mount_hashtable;
	i = nr_hash;
	do {
		INIT_LIST_HEAD(d);
		d++;
		i--;
	} while (i);
	init_mount_tree();
}

#ifdef CONFIG_BLK_DEV_INITRD

int __init change_root(kdev_t new_root_dev,const char *put_old)
{
	struct vfsmount *old_rootmnt;
	struct nameidata devfs_nd, nd;
	struct nameidata parent_nd;
	char *new_devname = kmalloc(strlen("/dev/root.old")+1, GFP_KERNEL);
	int error = 0;

	if (new_devname)
		strcpy(new_devname, "/dev/root.old");

	read_lock(&current->fs->lock);
	old_rootmnt = mntget(current->fs->rootmnt);
	read_unlock(&current->fs->lock);
	/*  First unmount devfs if mounted  */
	if (path_init("/dev", LOOKUP_FOLLOW|LOOKUP_POSITIVE, &devfs_nd))
		error = path_walk("/dev", &devfs_nd);
	if (!error) {
		if (devfs_nd.mnt->mnt_sb->s_magic == DEVFS_SUPER_MAGIC &&
		    devfs_nd.dentry == devfs_nd.mnt->mnt_root) {
			do_umount(devfs_nd.mnt, 0);
		}
		path_release(&devfs_nd);
	}
	spin_lock(&dcache_lock);
	detach_mnt(old_rootmnt, &parent_nd);
	spin_unlock(&dcache_lock);
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
		struct block_device *ramdisk = old_rootmnt->mnt_sb->s_bdev;

		atomic_inc(&ramdisk->bd_count);
		blivet = blkdev_get(ramdisk, FMODE_READ, 0, BDEV_FS);
		printk(KERN_NOTICE "Trying to unmount old root ... ");
		if (!blivet) {
			spin_lock(&dcache_lock);
			list_del(&old_rootmnt->mnt_list);
 			spin_unlock(&dcache_lock);
 			mntput(old_rootmnt);
			mntput(old_rootmnt);
			blivet = ioctl_by_bdev(ramdisk, BLKFLSBUF, 0);
			path_release(&parent_nd);
			blkdev_put(ramdisk, BDEV_FS);
		}
		if (blivet) {
			printk(KERN_ERR "error %d\n", blivet);
		} else {
			printk("okay\n");
			error = 0;
		}			
		kfree(new_devname);
		return error;
	}

	spin_lock(&dcache_lock);
	attach_mnt(old_rootmnt, &nd);
	if (new_devname) {
		if (old_rootmnt->mnt_devname)
			kfree(old_rootmnt->mnt_devname);
		old_rootmnt->mnt_devname = new_devname;
	}
	spin_unlock(&dcache_lock);

	/* put the old stuff */
	path_release(&parent_nd);
	mntput(old_rootmnt);
	path_release(&nd);
	return 0;
}

#endif
