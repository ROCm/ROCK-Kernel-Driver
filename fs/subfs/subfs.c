/*
 *  subfs.c
 *
 *  Copyright (C) 2003-2004 Eugene S. Weiss <eweiss@sbclobal.net>
 *
 *  Distributed under the terms of the GNU General Public License version 2
 *  or above.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <asm/atomic.h>
#include <asm/uaccess.h>
#include <linux/list.h>
#include <linux/mount.h>
#include <linux/namespace.h>
#include <linux/namei.h>
#include <linux/dcache.h>
#include <linux/sysfs.h>
#include <asm/semaphore.h>
#include <asm/signal.h>
#include <linux/signal.h>
#include <linux/sched.h>

#include "subfs.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eugene S. Weiss");


/* get_subfs_vfsmount tries to find the vfsmount structure assigned to
 * the pending mount.  It looks for a vfsmount structure matching the
 * superblock pointer sent.  This is not ideal, but I don't know of
 * another way to find the structure without altering the code in the
 * mount routines.
 */
static struct vfsmount *get_subfs_vfsmount(struct super_block *sb)
{
	struct fs_struct *init_fs;
	struct list_head *entry, *head, *lh;
	struct vfsmount *mnt;

	/* Get the head of the global mount list from the init process. */
	/* Is there a better way? */
	init_fs = init_task.fs;
	head = &init_fs->rootmnt->mnt_list;

	/* Go through the list and look for a superblock pointer match. */
	list_for_each_safe(entry, lh, head) {
		mnt = list_entry(entry, struct vfsmount, mnt_list);
		if (mnt->mnt_sb == sb)
			return mnt;
	}
	ERR("subfs: Unable to find mount structure for superblock.\n");
	return NULL;		/* If there was no match */
}


/* Same as set_fs_pwd from namespace.c.  There's a problem with the
 * symbol.  When it is fixed, discard this.
 * Replace the fs->{pwdmnt,pwd} with {mnt,dentry}. Put the old values.
 * It can block. Requires the big lock held.
 */
static void subfs_set_fs_pwd(struct fs_struct *fs, struct vfsmount *mnt,
			     struct dentry *dentry)
{
	struct dentry *old_pwd;
	struct vfsmount *old_pwdmnt;

	write_lock(&fs->lock);
	old_pwd = fs->pwd;
	old_pwdmnt = fs->pwdmnt;
	fs->pwdmnt = mntget(mnt);
	fs->pwd = dget(dentry);
	write_unlock(&fs->lock);

	if (old_pwd) {
		dput(old_pwd);
		mntput(old_pwdmnt);
	}
}


/* Quickly sends an ignored signal to the signal handling system. This
 * causes the system to restart the system call when it receives the
 * -ERESTARTSYS error.
 */
static void subfs_send_signal(void)
{
	struct task_struct *task = current;
	int signal = SIGCONT;

	read_lock(&tasklist_lock);
	spin_lock_irq(&task->sighand->siglock);
	sigaddset(&task->pending.signal, signal);
	spin_unlock_irq(&task->sighand->siglock);
	read_unlock(&tasklist_lock);
	set_tsk_thread_flag(task, TIF_SIGPENDING);
	return;
}


/* If the option "procuid" is chosen when subfs is mounted, the uid
 * and gid numbers for the current process will  bea dded to the mount
 * option line.  Hence, non-unix filesystems will be mounted with
 * that ownership.
 */
static void add_procuid(struct subfs_mount *sfs_mnt)
{
	struct task_struct *task = current;

	char *o = kmalloc(strlen(sfs_mnt->options) + 1 + 32 + 1, GFP_KERNEL);

	if (sfs_mnt->options[0] == '\0')
		sprintf(o, "uid=%d,gid=%d", task->uid, task->gid);
	else
		sprintf(o, "%s,uid=%d,gid=%d", sfs_mnt->options, task->uid, task->gid);

	kfree(sfs_mnt->options);
	sfs_mnt->options = o;
}


/* This routine calls the /sbin/submountd program to mount the
 * appropriate filesystem on top of the subfs mount.  Returns
 * 0 if the userspace program exited normally, or an error if
 * it did not.
 */
static int mount_real_fs(struct subfs_mount *sfs_mnt)
{
	char *argv[7] =
	    { sfs_mnt->helper_prog, NULL, NULL, NULL, NULL, NULL, NULL };
	char *envp[2] = { "HOME=/", NULL };
	char *path_buf;
	int result, len = 0;

	argv[1] = sfs_mnt->device;
	path_buf = (char *) __get_free_page(GFP_KERNEL);
	if (!path_buf)
		return -ENOMEM;
	argv[2] =
	    d_path(sfs_mnt->mount->mnt_mountpoint,
		   sfs_mnt->mount->mnt_parent, path_buf, PAGE_SIZE);
	argv[3] = sfs_mnt->req_fs;
	if (!(argv[4] = kmalloc(17, GFP_KERNEL))) {
		free_page((unsigned long) path_buf);
		return -ENOMEM;	/* 64 bits on some platforms */
	}
	sprintf(argv[4], "%lx", sfs_mnt->flags);
	len = strlen(sfs_mnt->options);
	if (sfs_mnt->procuid) 
		add_procuid(sfs_mnt);
	argv[5] = sfs_mnt->options;
	result = call_usermodehelper(sfs_mnt->helper_prog, argv, envp, 1);
	free_page((unsigned long) path_buf);
	kfree(argv[4]);
	if (sfs_mnt->procuid)
		sfs_mnt->options[len] = '\0';
	return result;
}


/*  This routine returns a pointer to the filesystem mounted on top
 *	of the subfs mountpoint, or an error pointer if it was unable to.
 */
static struct vfsmount *get_child_mount(struct subfs_mount *sfs_mnt)
{
	struct vfsmount *child;
	int result;
        /* First time: find the vfsmount structure that matches sfs_mnt. */
	if (!sfs_mnt->mount) {
		if(!(sfs_mnt->mount = get_subfs_vfsmount(sfs_mnt->sb)))
			return ERR_PTR(-ENOMEDIUM);
		sfs_mnt->flags = sfs_mnt->sb->s_flags;
		if (sfs_mnt->mount->mnt_flags & MNT_NOSUID)
			sfs_mnt->flags |= MS_NOSUID;
		if (sfs_mnt->mount->mnt_flags & MNT_NODEV)
			sfs_mnt->flags |= MS_NODEV;
		if (sfs_mnt->mount->mnt_flags & MNT_NOEXEC)
			sfs_mnt->flags |= MS_NOEXEC;
	}
	/* Check to see if a child mount does not already exist. */
	if (&sfs_mnt->mount->mnt_mounts == sfs_mnt->mount->mnt_mounts.next) {
		result = mount_real_fs(sfs_mnt);
		if (result) {
			ERR("subfs: unsuccessful attempt to mount media (%d)\n",
			       result);
			/* Workaround for call_usermodehelper return value bug. */
			if(result < 0)
				return ERR_PTR(result);
			return ERR_PTR(-ENOMEDIUM);
		}
		if (&sfs_mnt->mount->mnt_mounts
				== sfs_mnt->mount->mnt_mounts.next) {
			ERR("subfs: submountd mount failure.\n");
			return ERR_PTR(-ENOMEDIUM);
		}
	}
	child = list_entry(sfs_mnt->mount->mnt_mounts.next,
			   struct vfsmount, mnt_child);
	return child;
}


/* Implements the lookup method for subfs.  Tries to get the child
 * mount.  If it succeeds, it emits a signal and returns
 * -ERESTARSYS.  If it receives an error, it passes it on to the
 * system. It raises the semaphore in the directory inode before mounting
 * because the mount routine also calls lookup, and hence a function is
 * calling itself from within semaphore protected code.  Only the semaphore
 * on the subfs pseudo-directory is effected, so this isn't deadly.
 */
static struct dentry *subfs_lookup(struct inode *dir,
				struct dentry *dentry, struct nameidata *nd)
{
	struct subfs_mount *sfs_mnt = dir->i_sb->s_fs_info;
	struct vfsmount *child;

	/* This is ugly, but prevents a lockup during mount. */
	up(&dir->i_sem);
	if (down_interruptible(&sfs_mnt->sem)) {
		down(&dir->i_sem);/*put the dir sem back down if interrupted*/
		return ERR_PTR(-ERESTARTSYS);
	}
	child = get_child_mount(sfs_mnt);
	up(&sfs_mnt->sem);
	down(&dir->i_sem);
	if (IS_ERR(child))
		return (void *) child;
	subfs_send_signal();
	if (sfs_mnt->mount == current->fs->pwdmnt)
		subfs_set_fs_pwd(current->fs, child, child->mnt_root);
	return ERR_PTR(-ERESTARTSYS);
}


/* Implements the open method for subfs.  Tries to get the child mount
 * for the subfs mountpoint which is being opened.  Returns -ERESTARTSYS
 * and emits an ignored signal to the calling process if it succeeds,
 * or passes the error message received if it fails.
 */
static int subfs_open(struct inode *inode, struct file *filp)
{
	struct subfs_mount *sfs_mnt = filp->f_dentry->d_sb->s_fs_info;
	struct vfsmount *child;
	if (down_interruptible(&sfs_mnt->sem))
		return -ERESTARTSYS;
	child = get_child_mount(sfs_mnt);
	up(&sfs_mnt->sem);
	if (IS_ERR(child))
		return PTR_ERR(child);
	subfs_send_signal();
	if (sfs_mnt->mount == current->fs->pwdmnt)
		subfs_set_fs_pwd(current->fs, child, child->mnt_root);
	return -ERESTARTSYS;
}


/*  Implements the statfs method so df and such will work on the mountpoint.
 */
static int subfs_statfs(struct super_block *sb, struct kstatfs *buf)
{
#if 1
/* disable stafs, so "df" and other tools do not trigger to mount
   the media, which might cause error messages or hang, if the block
   device driver hangs */
	return 0;
#else
	struct subfs_mount *sfs_mnt = sb->s_fs_info;
	struct vfsmount *child;
	if (down_interruptible(&sfs_mnt->sem))
		return -ERESTARTSYS;
	child = get_child_mount(sfs_mnt);
	up(&sfs_mnt->sem);
	if (IS_ERR(child))
		return PTR_ERR(child);
	subfs_send_signal();
	return -ERESTARTSYS;
#endif
}


/*  Creates the inodes for subfs superblocks.
 */
static struct inode *subfs_make_inode(struct super_block *sb, int mode)
{
	struct inode *ret = new_inode(sb);

	if (ret) {
		ret->i_mode = mode;
		ret->i_uid = ret->i_gid = 0;
		ret->i_blksize = PAGE_CACHE_SIZE;
		ret->i_blocks = 0;
		ret->i_atime = ret->i_mtime = ret->i_ctime = CURRENT_TIME;
		ret->i_fop = &subfs_file_ops;
	}
	return ret;
}


/*  Fills the fields for the superblock created when subfs is mounted.
 */
static int subfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *root;
	struct dentry *root_dentry;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = SUBFS_MAGIC;
	sb->s_op = &subfs_s_ops;
	root = subfs_make_inode(sb, S_IFDIR|ROOT_MODE);
	if (!root)
		goto out;
	root->i_op = &subfs_dir_inode_operations;
	root_dentry = d_alloc_root(root);
	if (!root_dentry)
		goto out_iput;
	sb->s_root = root_dentry;
	return 0;
      out_iput:
	iput(root);
      out:
	return -ENOMEM;
}


/* Parse the options string and remove submount specific options
 * and store the appropriate data.
 */
static int proc_opts(struct subfs_mount *sfs_mnt, void *data)
{
	char *opts = data, *opt, *ptr, *fs = NULL, *prog;
	int len;

	if (!opts) {
		if (!(data = opts = kmalloc(PAGE_SIZE, GFP_KERNEL)))
			return -EINVAL;
		strcat(opts, "fs=auto");
	}
	len = strnlen(opts, PAGE_SIZE - 1) + 1;
	if (strstr(opts, "procuid"))
		sfs_mnt->procuid = 1;
	if (!(sfs_mnt->options = kmalloc(len, GFP_KERNEL)))
		return -ENOMEM;
	sfs_mnt->options[0] = '\0';
	while ((opt = strsep(&opts, ","))) {
        	if ((ptr = strstr(opt, "program="))) {
                	if (!(prog = kmalloc((strlen(ptr) - 7), GFP_KERNEL)))
                        	return -ENOMEM;
			strcpy(prog, (ptr + 8));
                        kfree(sfs_mnt->helper_prog);
                        sfs_mnt->helper_prog = prog;
		}
		else if ((ptr = strstr(opt, "fs="))) {
			if (!(fs = kmalloc((strlen(ptr) - 2), GFP_KERNEL)))
				return -ENOMEM;
			strcpy(fs, (ptr + 3));
			sfs_mnt->req_fs = fs;
		}
		else if ((ptr = strstr(opt, "procuid"))) {
		} else {
			strncat(sfs_mnt->options, opt, 32);
			strcat(sfs_mnt->options, ",");
		}
	}
	if((len = strlen(sfs_mnt->options)) && (sfs_mnt->options[len-1] == ','))
		sfs_mnt->options[len-1] = '\0';
	if ( !sfs_mnt->req_fs ){
		if (!(sfs_mnt->req_fs = kmalloc(5, GFP_KERNEL)))
			return -ENOMEM;	/* 64 bits on some platforms */
		strcpy(sfs_mnt->req_fs, "auto" );
	}
	return 0;
}



/* subfs_get_super is the subfs implementation of the get_sb method on
 * the file_system_type structure.  It should only be called in the
 * case of a mount.  It creates a new subfs_mount structure, fills
 * the fields of the structure, except for the mount structure, and then
 * calls a generic get_sb function.  The superblock pointer is stored on
 * the subfs_mount structure, and returned to the calling function.  The
 * subfs_mount structure is pointed to by the s_fs_info field of the
 * superblock structure.
 */
static struct super_block *subfs_get_super(struct file_system_type *fst,
				int flags, const char *devname, void *data)
{
	char *device;
	struct subfs_mount *newmount;
	int ret;

	if (!(newmount = kmalloc(sizeof(struct subfs_mount), GFP_KERNEL)))
		return ERR_PTR(-ENOMEM);
	newmount->req_fs = NULL;
	newmount->sb = NULL;
	newmount->mount = NULL;
	newmount->procuid = 0;
	sema_init(&newmount->sem, 1);
	if (!(device = kmalloc((strlen(devname) + 1), GFP_KERNEL)))
		return ERR_PTR(-ENOMEM);
	strcpy(device, devname);
	newmount->device = device;
        if (!(newmount->helper_prog =
        		kmalloc(sizeof(SUBMOUNTD_PATH), GFP_KERNEL)))
        	return ERR_PTR(-ENOMEM);
	strcpy(newmount->helper_prog, SUBMOUNTD_PATH);
	if ((ret = proc_opts(newmount, data)))
		return ERR_PTR(ret);
	newmount->sb = get_sb_nodev(fst, flags, data, subfs_fill_super);
	newmount->sb->s_fs_info = newmount;
	return newmount->sb;
}


/* subfs_kill_super is the subfs implementation of the kill_sb method.
 * It should be called only on umount.  It cleans up the appropriate
 * subfs_mount structure and then calls a generic function to actually
 * clean up the superblock structure.
 */
static void subfs_kill_super(struct super_block *sb)
{
	struct subfs_mount *sfs_mnt = sb->s_fs_info;

        if(sfs_mnt) {
		if (sfs_mnt->device)
			kfree(sfs_mnt->device);
		if (sfs_mnt->options)
			kfree(sfs_mnt->options);
		if (sfs_mnt->req_fs)
			kfree(sfs_mnt->req_fs);
		if (sfs_mnt->helper_prog)
                	kfree(sfs_mnt->helper_prog);
		kfree(sfs_mnt);
        	sb->s_fs_info = NULL;
	}
	kill_litter_super(sb);
	return;
}


static int __init subfs_init(void)
{
	printk(KERN_INFO "subfs %s\n", SUBFS_VER);
	return register_filesystem(&subfs_type);
}

static void __exit subfs_exit(void)
{
	printk(KERN_INFO "subfs exiting.\n");
	unregister_filesystem(&subfs_type);
}

module_init(subfs_init);
module_exit(subfs_exit);
