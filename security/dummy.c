/*
 * Stub functions for the default security function pointers in case no
 * security model is loaded.
 *
 * Copyright (C) 2001 WireX Communications, Inc <chris@wirex.com>
 * Copyright (C) 2001 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2001 Networks Associates Technology, Inc <ssmalley@nai.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/security.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>

static int dummy_ptrace (struct task_struct *parent, struct task_struct *child)
{
	return 0;
}

static int dummy_capget (struct task_struct *target, kernel_cap_t * effective,
			 kernel_cap_t * inheritable, kernel_cap_t * permitted)
{
	return 0;
}

static int dummy_capset_check (struct task_struct *target,
			       kernel_cap_t * effective,
			       kernel_cap_t * inheritable,
			       kernel_cap_t * permitted)
{
	return 0;
}

static void dummy_capset_set (struct task_struct *target,
			      kernel_cap_t * effective,
			      kernel_cap_t * inheritable,
			      kernel_cap_t * permitted)
{
	return;
}

static int dummy_acct (struct file *file)
{
	return 0;
}

static int dummy_capable (struct task_struct *tsk, int cap)
{
	if (cap_is_fs_cap (cap) ? tsk->fsuid == 0 : tsk->euid == 0)
		/* capability granted */
		return 0;

	/* capability denied */
	return -EPERM;
}

static int dummy_sys_security (unsigned int id, unsigned int call,
			       unsigned long *args)
{
	return -ENOSYS;
}

static int dummy_quotactl (int cmds, int type, int id, struct super_block *sb)
{
	return 0;
}

static int dummy_quota_on (struct file *f)
{
	return 0;
}

static int dummy_bprm_alloc_security (struct linux_binprm *bprm)
{
	return 0;
}

static void dummy_bprm_free_security (struct linux_binprm *bprm)
{
	return;
}

static void dummy_bprm_compute_creds (struct linux_binprm *bprm)
{
	return;
}

static int dummy_bprm_set_security (struct linux_binprm *bprm)
{
	return 0;
}

static int dummy_bprm_check_security (struct linux_binprm *bprm)
{
	return 0;
}

static int dummy_sb_alloc_security (struct super_block *sb)
{
	return 0;
}

static void dummy_sb_free_security (struct super_block *sb)
{
	return;
}

static int dummy_sb_statfs (struct super_block *sb)
{
	return 0;
}

static int dummy_mount (char *dev_name, struct nameidata *nd, char *type,
			unsigned long flags, void *data)
{
	return 0;
}

static int dummy_check_sb (struct vfsmount *mnt, struct nameidata *nd)
{
	return 0;
}

static int dummy_umount (struct vfsmount *mnt, int flags)
{
	return 0;
}

static void dummy_umount_close (struct vfsmount *mnt)
{
	return;
}

static void dummy_umount_busy (struct vfsmount *mnt)
{
	return;
}

static void dummy_post_remount (struct vfsmount *mnt, unsigned long flags,
				void *data)
{
	return;
}


static void dummy_post_mountroot (void)
{
	return;
}

static void dummy_post_addmount (struct vfsmount *mnt, struct nameidata *nd)
{
	return;
}

static int dummy_pivotroot (struct nameidata *old_nd, struct nameidata *new_nd)
{
	return 0;
}

static void dummy_post_pivotroot (struct nameidata *old_nd, struct nameidata *new_nd)
{
	return;
}

static int dummy_inode_alloc_security (struct inode *inode)
{
	return 0;
}

static void dummy_inode_free_security (struct inode *inode)
{
	return;
}

static int dummy_inode_create (struct inode *inode, struct dentry *dentry,
			       int mask)
{
	return 0;
}

static void dummy_inode_post_create (struct inode *inode, struct dentry *dentry,
				     int mask)
{
	return;
}

static int dummy_inode_link (struct dentry *old_dentry, struct inode *inode,
			     struct dentry *new_dentry)
{
	return 0;
}

static void dummy_inode_post_link (struct dentry *old_dentry,
				   struct inode *inode,
				   struct dentry *new_dentry)
{
	return;
}

static int dummy_inode_unlink (struct inode *inode, struct dentry *dentry)
{
	return 0;
}

static int dummy_inode_symlink (struct inode *inode, struct dentry *dentry,
				const char *name)
{
	return 0;
}

static void dummy_inode_post_symlink (struct inode *inode,
				      struct dentry *dentry, const char *name)
{
	return;
}

static int dummy_inode_mkdir (struct inode *inode, struct dentry *dentry,
			      int mask)
{
	return 0;
}

static void dummy_inode_post_mkdir (struct inode *inode, struct dentry *dentry,
				    int mask)
{
	return;
}

static int dummy_inode_rmdir (struct inode *inode, struct dentry *dentry)
{
	return 0;
}

static int dummy_inode_mknod (struct inode *inode, struct dentry *dentry,
			      int major, dev_t minor)
{
	return 0;
}

static void dummy_inode_post_mknod (struct inode *inode, struct dentry *dentry,
				    int major, dev_t minor)
{
	return;
}

static int dummy_inode_rename (struct inode *old_inode,
			       struct dentry *old_dentry,
			       struct inode *new_inode,
			       struct dentry *new_dentry)
{
	return 0;
}

static void dummy_inode_post_rename (struct inode *old_inode,
				     struct dentry *old_dentry,
				     struct inode *new_inode,
				     struct dentry *new_dentry)
{
	return;
}

static int dummy_inode_readlink (struct dentry *dentry)
{
	return 0;
}

static int dummy_inode_follow_link (struct dentry *dentry,
				    struct nameidata *nameidata)
{
	return 0;
}

static int dummy_inode_permission (struct inode *inode, int mask)
{
	return 0;
}

static int dummy_inode_permission_lite (struct inode *inode, int mask)
{
	return 0;
}

static int dummy_inode_setattr (struct dentry *dentry, struct iattr *iattr)
{
	return 0;
}

static int dummy_inode_getattr (struct vfsmount *mnt, struct dentry *dentry)
{
	return 0;
}

static void dummy_post_lookup (struct inode *ino, struct dentry *d)
{
	return;
}

static void dummy_delete (struct inode *ino)
{
	return;
}

static int dummy_inode_setxattr (struct dentry *dentry, char *name, void *value,
				size_t size, int flags)
{
	return 0;
}

static int dummy_inode_getxattr (struct dentry *dentry, char *name)
{
	return 0;
}

static int dummy_inode_listxattr (struct dentry *dentry)
{
	return 0;
}

static int dummy_inode_removexattr (struct dentry *dentry, char *name)
{
	return 0;
}

static int dummy_file_permission (struct file *file, int mask)
{
	return 0;
}

static int dummy_file_alloc_security (struct file *file)
{
	return 0;
}

static void dummy_file_free_security (struct file *file)
{
	return;
}

static int dummy_file_llseek (struct file *file)
{
	return 0;
}

static int dummy_file_ioctl (struct file *file, unsigned int command,
			     unsigned long arg)
{
	return 0;
}

static int dummy_file_mmap (struct file *file, unsigned long prot,
			    unsigned long flags)
{
	return 0;
}

static int dummy_file_mprotect (struct vm_area_struct *vma, unsigned long prot)
{
	return 0;
}

static int dummy_file_lock (struct file *file, unsigned int cmd, int blocking)
{
	return 0;
}

static int dummy_file_fcntl (struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	return 0;
}

static int dummy_file_set_fowner (struct file *file)
{
	return 0;
}

static int dummy_file_send_sigiotask (struct task_struct *tsk,
				      struct fown_struct *fown, int fd,
				      int reason)
{
	return 0;
}

static int dummy_file_receive (struct file *file)
{
	return 0;
}

static int dummy_task_create (unsigned long clone_flags)
{
	return 0;
}

static int dummy_task_alloc_security (struct task_struct *p)
{
	return 0;
}

static void dummy_task_free_security (struct task_struct *p)
{
	return;
}

static int dummy_task_setuid (uid_t id0, uid_t id1, uid_t id2, int flags)
{
	return 0;
}

static int dummy_task_post_setuid (uid_t id0, uid_t id1, uid_t id2, int flags)
{
	return 0;
}

static int dummy_task_setgid (gid_t id0, gid_t id1, gid_t id2, int flags)
{
	return 0;
}

static int dummy_task_setpgid (struct task_struct *p, pid_t pgid)
{
	return 0;
}

static int dummy_task_getpgid (struct task_struct *p)
{
	return 0;
}

static int dummy_task_getsid (struct task_struct *p)
{
	return 0;
}

static int dummy_task_setgroups (int gidsetsize, gid_t * grouplist)
{
	return 0;
}

static int dummy_task_setnice (struct task_struct *p, int nice)
{
	return 0;
}

static int dummy_task_setrlimit (unsigned int resource, struct rlimit *new_rlim)
{
	return 0;
}

static int dummy_task_setscheduler (struct task_struct *p, int policy,
				    struct sched_param *lp)
{
	return 0;
}

static int dummy_task_getscheduler (struct task_struct *p)
{
	return 0;
}

static int dummy_task_wait (struct task_struct *p)
{
	return 0;
}

static int dummy_task_kill (struct task_struct *p, struct siginfo *info,
			    int sig)
{
	return 0;
}

static int dummy_task_prctl (int option, unsigned long arg2, unsigned long arg3,
			     unsigned long arg4, unsigned long arg5)
{
	return 0;
}

static void dummy_task_kmod_set_label (void)
{
	return;
}

static void dummy_task_reparent_to_init (struct task_struct *p)
{
	p->euid = p->fsuid = 0;
	return;
}

static int dummy_register (const char *name, struct security_operations *ops)
{
	return -EINVAL;
}

static int dummy_unregister (const char *name, struct security_operations *ops)
{
	return -EINVAL;
}

struct security_operations dummy_security_ops = {
	.ptrace =			dummy_ptrace,
	.capget =			dummy_capget,
	.capset_check =			dummy_capset_check,
	.capset_set =			dummy_capset_set,
	.acct =				dummy_act,
	.capable =			dummy_capable,
	.sys_security =			dummy_sys_security,
	quotactl:			dummy_quotactl,
	quota_on:			dummy_quota_on,

	.bprm_alloc_security =		dummy_bprm_alloc_security,
	.bprm_free_security =		dummy_bprm_free_security,
	.bprm_compute_creds =		dummy_bprm_compute_creds,
	.bprm_set_security =		dummy_bprm_set_security,
	.bprm_check_security =		dummy_bprm_check_security,

	sb_alloc_security:		dummy_sb_alloc_security,
	sb_free_security:		dummy_sb_free_security,
	sb_statfs:			dummy_sb_statfs,
	sb_mount:			dummy_mount,
	sb_check_sb:			dummy_check_sb,
	sb_umount:			dummy_umount,
	sb_umount_close:		dummy_umount_close,
	sb_umount_busy:			dummy_umount_busy,
	sb_post_remount:		dummy_post_remount,
	sb_post_mountroot:		dummy_post_mountroot,
	sb_post_addmount:		dummy_post_addmount,
	sb_pivotroot:			dummy_pivotroot,
	sb_post_pivotroot:		dummy_post_pivotroot,
	
	inode_alloc_security:		dummy_inode_alloc_security,
	inode_free_security:		dummy_inode_free_security,
	inode_create:			dummy_inode_create,
	inode_post_create:		dummy_inode_post_create,
	inode_link:			dummy_inode_link,
	inode_post_link:		dummy_inode_post_link,
	inode_unlink:			dummy_inode_unlink,
	inode_symlink:			dummy_inode_symlink,
	inode_post_symlink:		dummy_inode_post_symlink,
	inode_mkdir:			dummy_inode_mkdir,
	inode_post_mkdir:		dummy_inode_post_mkdir,
	inode_rmdir:			dummy_inode_rmdir,
	inode_mknod:			dummy_inode_mknod,
	inode_post_mknod:		dummy_inode_post_mknod,
	inode_rename:			dummy_inode_rename,
	inode_post_rename:		dummy_inode_post_rename,
	inode_readlink:			dummy_inode_readlink,
	inode_follow_link:		dummy_inode_follow_link,
	inode_permission:		dummy_inode_permission,
	inode_permission_lite:		dummy_inode_permission_lite,
	inode_setattr:			dummy_inode_setattr,
	inode_getattr:			dummy_inode_getattr,
	inode_post_lookup:		dummy_post_lookup,
	inode_delete:			dummy_delete,
	inode_setxattr:			dummy_inode_setxattr,
	inode_getxattr:			dummy_inode_getxattr,
	inode_listxattr:		dummy_inode_listxattr,
	inode_removexattr:		dummy_inode_removexattr,

	file_permission:		dummy_file_permission,
	file_alloc_security:		dummy_file_alloc_security,
	file_free_security:		dummy_file_free_security,
	file_llseek:			dummy_file_llseek,
	file_ioctl:			dummy_file_ioctl,
	file_mmap:			dummy_file_mmap,
	file_mprotect:			dummy_file_mprotect,
	file_lock:			dummy_file_lock,
	file_fcntl:			dummy_file_fcntl,
	file_set_fowner:		dummy_file_set_fowner,
	file_send_sigiotask:		dummy_file_send_sigiotask,
	file_receive:			dummy_file_receive,

	.task_create =			dummy_task_create,
	.task_alloc_security =		dummy_task_alloc_security,
	.task_free_security =		dummy_task_free_security,
	.task_setuid =			dummy_task_setuid,
	.task_post_setuid =		dummy_task_post_setuid,
	.task_setgid =			dummy_task_setgid,
	.task_setpgid =			dummy_task_setpgid,
	.task_getpgid =			dummy_task_getpgid,
	.task_getsid =			dummy_task_getsid,
	.task_setgroups =		dummy_task_setgroups,
	.task_setnice =			dummy_task_setnice,
	.task_setrlimit =		dummy_task_setrlimit,
	.task_setscheduler =		dummy_task_setscheduler,
	.task_getscheduler =		dummy_task_getscheduler,
	.task_wait =			dummy_task_wait,
	.task_kill =			dummy_task_kill,
	.task_prctl =			dummy_task_prctl,
	.task_kmod_set_label =		dummy_task_kmod_set_label,
	.task_reparent_to_init =	dummy_task_reparent_to_init,

	.register_security =		dummy_register,
	.unregister_security =		dummy_unregister,
};

