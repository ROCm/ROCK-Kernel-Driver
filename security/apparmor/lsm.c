/*
 *	Copyright (C) 2002-2005 Novell/SUSE
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 *
 *	http://forge.novell.com/modules/xfmod/project/?apparmor
 *
 *	Immunix AppArmor LSM interface
 */

#include <linux/security.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/mount.h>
#include <linux/namei.h>

#include "apparmor.h"
#include "inline.h"

/* Flag values, also controllable via apparmorfs/control.
 * We explicitly do not allow these to be modifiable when exported via
 * /sys/modules/parameters, as we want to do additional mediation and
 * don't want to add special path code. */

/* Complain mode -- in complain mode access failures result in auditing only
 * and task is allowed access.  audit events are processed by userspace to
 * generate policy.  Default is 'enforce' (0).
 * Value is also togglable per profile and referenced when global value is
 * enforce.
 */
int apparmor_complain = 0;
module_param_named(complain, apparmor_complain, int, S_IRUSR);
MODULE_PARM_DESC(apparmor_complain, "Toggle AppArmor complain mode");

/* Debug mode */
int apparmor_debug = 0;
module_param_named(debug, apparmor_debug, int, S_IRUSR);
MODULE_PARM_DESC(apparmor_debug, "Toggle AppArmor debug mode");

/* Audit mode */
int apparmor_audit = 0;
module_param_named(audit, apparmor_audit, int, S_IRUSR);
MODULE_PARM_DESC(apparmor_audit, "Toggle AppArmor audit mode");

/* Syscall logging mode */
int apparmor_logsyscall = 0;
module_param_named(logsyscall, apparmor_logsyscall, int, S_IRUSR);
MODULE_PARM_DESC(apparmor_logsyscall, "Toggle AppArmor logsyscall mode");

/* Maximum pathname length before accesses will start getting rejected */
int apparmor_path_max = 2 * PATH_MAX;
module_param_named(path_max, apparmor_path_max, int, S_IRUSR);
MODULE_PARM_DESC(apparmor_path_max, "Maximum pathname length allowed");


static int aa_reject_syscall(struct task_struct *task, gfp_t flags,
			     const char *name)
{
	struct aa_profile *profile = aa_get_profile(task);
	int error = 0;

	if (profile) {
		error = aa_audit_syscallreject(profile, flags, name);
		aa_put_profile(profile);
	}

	return error;
}

static int apparmor_ptrace(struct task_struct *parent,
			    struct task_struct *child)
{
	int error;

	/**
	 * Right now, we only allow confined processes to ptrace other
	 * processes if they have CAP_SYS_PTRACE. We could allow ptrace
	 * under the rules that the kernel normally permits if the two
	 * processes are running under the same profile, but then we
	 * would probably have to reject profile changes for processes
	 * that are being ptraced as well as for processes ptracing
	 * others.
	 */

	error = cap_ptrace(parent, child);
	if (!error) {
		struct aa_task_context *cxt;

		rcu_read_lock();
		cxt = aa_task_context(parent);
		if (cxt)
			error = aa_capability(cxt, CAP_SYS_PTRACE);
		rcu_read_unlock();
	}

	return error;
}

static int apparmor_capget(struct task_struct *task,
			    kernel_cap_t *effective,
			    kernel_cap_t *inheritable,
			    kernel_cap_t *permitted)
{
	return cap_capget(task, effective, inheritable, permitted);
}

static int apparmor_capset_check(struct task_struct *task,
				  kernel_cap_t *effective,
				  kernel_cap_t *inheritable,
				  kernel_cap_t *permitted)
{
	return cap_capset_check(task, effective, inheritable, permitted);
}

static void apparmor_capset_set(struct task_struct *task,
				 kernel_cap_t *effective,
				 kernel_cap_t *inheritable,
				 kernel_cap_t *permitted)
{
	cap_capset_set(task, effective, inheritable, permitted);
}

static int apparmor_capable(struct task_struct *task, int cap)
{
	int error;

	/* cap_capable returns 0 on success, else -EPERM */
	error = cap_capable(task, cap);

	if (!error) {
		struct aa_task_context *cxt;

		rcu_read_lock();
		cxt = aa_task_context(task);
		if (cxt)
			error = aa_capability(cxt, cap);
		rcu_read_unlock();
	}

	return error;
}

static int apparmor_sysctl(struct ctl_table *table, int op)
{
	int error = 0;

	if ((op & 002) && !capable(CAP_SYS_ADMIN))
		error = aa_reject_syscall(current, GFP_KERNEL,
					  "sysctl (write)");

	return error;
}

static int apparmor_syslog(int type)
{
	return cap_syslog(type);
}

static int apparmor_netlink_send(struct sock *sk, struct sk_buff *skb)
{
	return cap_netlink_send(sk, skb);
}

static int apparmor_netlink_recv(struct sk_buff *skb, int cap)
{
	return cap_netlink_recv(skb, cap);
}

static void apparmor_bprm_apply_creds(struct linux_binprm *bprm, int unsafe)
{
	cap_bprm_apply_creds(bprm, unsafe);
}

static int apparmor_bprm_set_security(struct linux_binprm *bprm)
{
	/* handle capability bits with setuid, etc */
	cap_bprm_set_security(bprm);
	/* already set based on script name */
	if (bprm->sh_bang)
		return 0;
	return aa_register(bprm);
}

static int apparmor_bprm_secureexec(struct linux_binprm *bprm)
{
	int ret = cap_bprm_secureexec(bprm);

	if (!ret && (unsigned long)bprm->security & AA_SECURE_EXEC_NEEDED) {
		AA_DEBUG("%s: secureexec required for %s\n",
			 __FUNCTION__, bprm->filename);
		ret = 1;
	}

	return ret;
}

static int apparmor_sb_mount(char *dev_name, struct nameidata *nd, char *type,
			      unsigned long flags, void *data)
{
	return aa_reject_syscall(current, GFP_KERNEL, "mount");
}

static int apparmor_umount(struct vfsmount *mnt, int flags)
{
	return aa_reject_syscall(current, GFP_KERNEL, "umount");
}

static int apparmor_inode_mkdir(struct inode *dir, struct dentry *dentry,
				struct vfsmount *mnt, int mask)
{
	struct aa_profile *profile;
	int error = 0;

	if (!mnt || !mediated_filesystem(dir))
		goto out;

	profile = aa_get_profile(current);

	if (profile)
		error = aa_perm_dir(profile, dentry, mnt, "mkdir", MAY_WRITE);

	aa_put_profile(profile);

out:
	return error;
}

static int apparmor_inode_rmdir(struct inode *dir, struct dentry *dentry,
				struct vfsmount *mnt)
{
	struct aa_profile *profile;
	int error = 0;

	if (!mnt || !mediated_filesystem(dir))
		goto out;

	profile = aa_get_profile(current);

	if (profile)
		error = aa_perm_dir(profile, dentry, mnt, "rmdir", MAY_WRITE);

	aa_put_profile(profile);

out:
	return error;
}

static int aa_permission(struct inode *inode, struct dentry *dentry,
			 struct vfsmount *mnt, int mask, int check)
{
	int error = 0;

	if (mnt && mediated_filesystem(inode)) {
		struct aa_profile *profile;

		profile = aa_get_profile(current);
		if (profile)
			error = aa_perm(profile, dentry, mnt, mask, check);
		aa_put_profile(profile);
	}
	return error;
}

static int apparmor_inode_create(struct inode *dir, struct dentry *dentry,
				 struct vfsmount *mnt, int mask)
{
	return aa_permission(dir, dentry, mnt, MAY_WRITE, AA_CHECK_LEAF);
}

static int apparmor_inode_link(struct dentry *old_dentry,
			       struct vfsmount *old_mnt, struct inode *dir,
			       struct dentry *new_dentry,
			       struct vfsmount *new_mnt)
{
	int error = 0;
	struct aa_profile *profile;

	if (!old_mnt || !new_mnt || !mediated_filesystem(dir))
		goto out;

	profile = aa_get_profile(current);

	if (profile)
		error = aa_link(profile, new_dentry, new_mnt,
				old_dentry, old_mnt);

	aa_put_profile(profile);

out:
	return error;
}

static int apparmor_inode_unlink(struct inode *dir, struct dentry *dentry,
				 struct vfsmount *mnt)
{
	int check = AA_CHECK_LEAF;

	if (S_ISDIR(dentry->d_inode->i_mode))
		check |= AA_CHECK_DIR;
	return aa_permission(dir, dentry, mnt, MAY_WRITE, check);
}

static int apparmor_inode_symlink(struct inode *dir, struct dentry *dentry,
				  struct vfsmount *mnt, const char *old_name)
{
	return aa_permission(dir, dentry, mnt, MAY_WRITE, AA_CHECK_LEAF);
}

static int apparmor_inode_mknod(struct inode *dir, struct dentry *dentry,
				struct vfsmount *mnt, int mode, dev_t dev)
{
	return aa_permission(dir, dentry, mnt, MAY_WRITE, AA_CHECK_LEAF);
}

static int apparmor_inode_rename(struct inode *old_dir,
				 struct dentry *old_dentry,
				 struct vfsmount *old_mnt,
				 struct inode *new_dir,
				 struct dentry *new_dentry,
				 struct vfsmount *new_mnt)
{
	struct aa_profile *profile;
	int error = 0;

	if ((!old_mnt && !new_mnt) || !mediated_filesystem(old_dir))
		goto out;

	profile = aa_get_profile(current);

	if (profile) {
		struct inode *inode = old_dentry->d_inode;
		int check = AA_CHECK_LEAF;

		if (inode && S_ISDIR(inode->i_mode))
			check |= AA_CHECK_DIR;
		if (old_mnt)
			error = aa_perm(profile, old_dentry, old_mnt,
					MAY_READ | MAY_WRITE, check);

		if (!error && new_mnt) {
			error = aa_perm(profile, new_dentry, new_mnt,
					MAY_WRITE, check);
		}
	}

	aa_put_profile(profile);

out:
	return error;
}

static int apparmor_inode_permission(struct inode *inode, int mask,
				     struct nameidata *nd)
{
	int check = 0;

	if (!nd)
		return 0;
	if (S_ISDIR(inode->i_mode))
		check |= AA_CHECK_DIR;
	mask &= (MAY_READ | MAY_WRITE | MAY_EXEC);

	/* Assume we are not checking a leaf directory. */
	return aa_permission(inode, nd->dentry, nd->mnt, mask, check);
}

static int apparmor_inode_setattr(struct dentry *dentry, struct vfsmount *mnt,
				  struct iattr *iattr)
{
	int error = 0;

	if (!mnt)
		goto out;

	if (mediated_filesystem(dentry->d_inode)) {
		struct aa_profile *profile;

		profile = aa_get_profile(current);
		/*
		 * Mediate any attempt to change attributes of a file
		 * (chmod, chown, chgrp, etc)
		 */
		if (profile)
			error = aa_attr(profile, dentry, mnt, iattr);

		aa_put_profile(profile);
	}

out:
	return error;
}

static int aa_xattr_permission(struct dentry *dentry, struct vfsmount *mnt,
			       const char *name, const char *operation,
			       int mask, struct file *file)
{
	int error = 0;

	if (mnt && mediated_filesystem(dentry->d_inode)) {
		struct aa_profile *profile = aa_get_profile(current);
		int check = file ? AA_CHECK_FD : 0;

		if (profile)
			error = aa_perm_xattr(profile, dentry, mnt, name,
					      operation, mask, check);
		aa_put_profile(profile);
	}

	return error;
}

static int apparmor_inode_setxattr(struct dentry *dentry, struct vfsmount *mnt,
				   char *name, void *value, size_t size,
				   int flags, struct file *file)
{
	return aa_xattr_permission(dentry, mnt, name, "xattr set", MAY_WRITE,
				   file);
}

static int apparmor_inode_getxattr(struct dentry *dentry, struct vfsmount *mnt,
				   char *name, struct file *file)
{
	return aa_xattr_permission(dentry, mnt, name, "xattr get", MAY_READ,
				   file);
}

static int apparmor_inode_listxattr(struct dentry *dentry, struct vfsmount *mnt,
				    struct file *file)
{
	return aa_xattr_permission(dentry, mnt, NULL, "xattr list", MAY_READ,
				   file);
}

static int apparmor_inode_removexattr(struct dentry *dentry,
				      struct vfsmount *mnt, char *name,
				      struct file *file)
{
	return aa_xattr_permission(dentry, mnt, name, "xattr remove",
				   MAY_WRITE, file);
}

static int apparmor_file_permission(struct file *file, int mask)
{
	struct aa_profile *profile;
	struct aa_profile *file_profile = (struct aa_profile*)file->f_security;
	int error = 0;

	if (!file_profile)
		goto out;

	/*
	 * If this file was opened under a different profile, we
	 * revalidate the access against the current profile.
	 */
	profile = aa_get_profile(current);
	if (profile && file_profile != profile) {
		struct dentry *dentry = file->f_dentry;
		struct vfsmount *mnt = file->f_vfsmnt;
		struct inode *inode = dentry->d_inode;
		int check = AA_CHECK_LEAF | AA_CHECK_FD;

		/*
		 * FIXME: We should remember which profiles we revalidated
		 *	  against.
		 */
		if (S_ISDIR(inode->i_mode))
			check |= AA_CHECK_DIR;
		mask &= (MAY_READ | MAY_WRITE | MAY_EXEC);
		error = aa_permission(inode, dentry, mnt, mask, check);
	}
	aa_put_profile(profile);

out:
	return error;
}

static int apparmor_task_create(unsigned long clone_flags)
{
	struct aa_profile *profile;
	int error = 0;

	profile = aa_get_profile(current);
	if (profile) {
		/* Don't allow to create new namespaces. */
		if (clone_flags & CLONE_NEWNS)
			error = -EPERM;
	}
	aa_put_profile(profile);

	return error;
}

static int apparmor_file_alloc_security(struct file *file)
{
	struct aa_profile *profile;

	profile = aa_get_profile(current);
	if (profile)
		file->f_security = profile;

	return 0;
}

static void apparmor_file_free_security(struct file *file)
{
	struct aa_profile *file_profile = (struct aa_profile*)file->f_security;

	aa_put_profile(file_profile);
}

static inline int aa_mmap(struct file *file, unsigned long prot,
			  unsigned long flags)
{
	struct dentry *dentry;
	int mask = 0;

	if (!file || !file->f_security)
		return 0;

	if (prot & PROT_READ)
		mask |= MAY_READ;
	/* Private mappings don't require write perms since they don't
	 * write back to the files */
	if ((prot & PROT_WRITE) && !(flags & MAP_PRIVATE))
		mask |= MAY_WRITE;
	if (prot & PROT_EXEC)
		mask |= AA_EXEC_MMAP;

	dentry = file->f_dentry;
	return aa_permission(dentry->d_inode, dentry, file->f_vfsmnt, mask,
			     AA_CHECK_LEAF | AA_CHECK_FD);
}

static int apparmor_file_mmap(struct file *file, unsigned long reqprot,
			       unsigned long prot, unsigned long flags)
{
	return aa_mmap(file, prot, flags);
}

static int apparmor_file_mprotect(struct vm_area_struct *vma,
				  unsigned long reqprot, unsigned long prot)
{
	return aa_mmap(vma->vm_file, prot,
		       !(vma->vm_flags & VM_SHARED) ? MAP_PRIVATE : 0);
}

static int apparmor_task_alloc_security(struct task_struct *task)
{
	return aa_clone(task);
}

/*
 * Called from IRQ context from RCU callback.
 */
static void apparmor_task_free_security(struct task_struct *task)
{
	aa_release(task);
}

static int apparmor_task_post_setuid(uid_t id0, uid_t id1, uid_t id2,
				     int flags)
{
	return cap_task_post_setuid(id0, id1, id2, flags);
}

static void apparmor_task_reparent_to_init(struct task_struct *task)
{
	cap_task_reparent_to_init(task);
}

static int apparmor_getprocattr(struct task_struct *task, char *name,
				char **value)
{
	unsigned len;
	int error;
	struct aa_profile *profile;

	/* AppArmor only supports the "current" process attribute */
	if (strcmp(name, "current") != 0) {
		error = -EINVAL;
		goto out;
	}

	/* must be task querying itself or admin */
	if (current != task && !capable(CAP_SYS_ADMIN)) {
		error = -EPERM;
		goto out;
	}

	profile = aa_get_profile(task);
	error = aa_getprocattr(profile, value, &len);
	aa_put_profile(profile);
	if (!error)
		error = len;

out:
	return error;
}

static int apparmor_setprocattr(struct task_struct *task, char *name,
				void *value, size_t size)
{
	const char *cmd_changehat = "changehat ",
		   *cmd_setprofile = "setprofile ";

	int error;
	char *cmd = (char *)value;

	error = -EINVAL;
	if (strcmp(name, "current") != 0)
		goto out;
	error = -ERANGE;
	if (!size)
		goto out;

	/* CHANGE HAT -- switch task into a subhat (subprofile) if defined */
	if (size > strlen(cmd_changehat) &&
	    strncmp(cmd, cmd_changehat, strlen(cmd_changehat)) == 0) {
		char *hatinfo = cmd + strlen(cmd_changehat);
		size_t infosize = size - strlen(cmd_changehat);

		/* Only the current process may change it's hat */
		if (current != task) {
			AA_WARN(GFP_KERNEL,
				"%s: Attempt by foreign task %s(%d) "
				"[user %d] to changehat of task %s(%d)\n",
				__FUNCTION__,
				current->comm,
				current->pid,
				current->uid,
				task->comm,
				task->pid);

			error = -EACCES;
			goto out;
		}

		error = aa_setprocattr_changehat(hatinfo, infosize);
		if (!error)
			error = size;

	/* SET NEW PROFILE */
	} else if (size > strlen(cmd_setprofile) &&
		   strncmp(cmd, cmd_setprofile, strlen(cmd_setprofile)) == 0) {
		struct aa_profile *profile;

		/* only an unconfined process with admin capabilities
		 * may change the profile of another task
		 */

		if (!capable(CAP_SYS_ADMIN)) {
			AA_WARN(GFP_KERNEL,
				"%s: Unprivileged attempt by task %s(%d) "
				"[user %d] to assign profile to task %s(%d)\n",
				__FUNCTION__,
				current->comm,
				current->pid,
				current->uid,
				task->comm,
				task->pid);
			error = -EACCES;
			goto out;
		}

		profile = aa_get_profile(current);
		if (!profile) {
			char *profile = cmd + strlen(cmd_setprofile);
			size_t profilesize = size - strlen(cmd_setprofile);

			error = aa_setprocattr_setprofile(task, profile, profilesize);
			if (!error)
				/* success,
				 * set return to #bytes in orig request
				 */
				error = size;
		} else {
			AA_WARN(GFP_KERNEL,
				"%s: Attempt by confined task %s(%d) "
				"[user %d] to assign profile to task %s(%d)\n",
				__FUNCTION__,
				current->comm,
				current->pid,
				current->uid,
				task->comm,
				task->pid);

			error = -EACCES;
		}
		aa_put_profile(profile);
	} else {
		/* unknown operation */
		AA_WARN(GFP_KERNEL,
			"%s: Unknown setprocattr command '%.*s' by task %s(%d)"
			" [user %d] for task %s(%d)\n",
			__FUNCTION__,
			size < 16 ? (int)size : 16,
			cmd,
			current->comm,
			current->pid,
			current->uid,
			task->comm,
			task->pid);

		error = -EINVAL;
	}

out:
	return error;
}

struct security_operations apparmor_ops = {
	.ptrace =			apparmor_ptrace,
	.capget =			apparmor_capget,
	.capset_check =			apparmor_capset_check,
	.capset_set =			apparmor_capset_set,
	.sysctl =			apparmor_sysctl,
	.capable =			apparmor_capable,
	.syslog =			apparmor_syslog,

	.netlink_send =			apparmor_netlink_send,
	.netlink_recv =			apparmor_netlink_recv,

	.bprm_apply_creds =		apparmor_bprm_apply_creds,
	.bprm_set_security =		apparmor_bprm_set_security,
	.bprm_secureexec =		apparmor_bprm_secureexec,

	.sb_mount =			apparmor_sb_mount,
	.sb_umount =			apparmor_umount,

	.inode_mkdir =			apparmor_inode_mkdir,
	.inode_rmdir =			apparmor_inode_rmdir,
	.inode_create =			apparmor_inode_create,
	.inode_link =			apparmor_inode_link,
	.inode_unlink =			apparmor_inode_unlink,
	.inode_symlink =		apparmor_inode_symlink,
	.inode_mknod =			apparmor_inode_mknod,
	.inode_rename =			apparmor_inode_rename,
	.inode_permission =		apparmor_inode_permission,
	.inode_setattr =		apparmor_inode_setattr,
	.inode_setxattr =		apparmor_inode_setxattr,
	.inode_getxattr =		apparmor_inode_getxattr,
	.inode_listxattr =		apparmor_inode_listxattr,
	.inode_removexattr =		apparmor_inode_removexattr,
	.file_permission =		apparmor_file_permission,
	.file_alloc_security =		apparmor_file_alloc_security,
	.file_free_security =		apparmor_file_free_security,
	.file_mmap =			apparmor_file_mmap,
	.file_mprotect =		apparmor_file_mprotect,

	.task_create =			apparmor_task_create,
	.task_alloc_security =		apparmor_task_alloc_security,
	.task_free_security =		apparmor_task_free_security,
	.task_post_setuid =		apparmor_task_post_setuid,
	.task_reparent_to_init =	apparmor_task_reparent_to_init,

	.getprocattr =			apparmor_getprocattr,
	.setprocattr =			apparmor_setprocattr,
};

static int __init apparmor_init(void)
{
	int error;
	const char *complainmsg = ": complainmode enabled";

	if ((error = create_apparmorfs())) {
		AA_ERROR("Unable to activate AppArmor filesystem\n");
		goto createfs_out;
	}

	if ((error = alloc_null_complain_profile())){
		AA_ERROR("Unable to allocate null complain profile\n");
		goto alloc_out;
	}

	if ((error = register_security(&apparmor_ops))) {
		AA_ERROR("Unable to load AppArmor\n");
		goto register_security_out;
	}

	AA_INFO(GFP_KERNEL, "AppArmor initialized%s\n",
		apparmor_complain ? complainmsg : "");
	aa_audit_message(NULL, GFP_KERNEL, 0,
		"AppArmor initialized%s\n",
		apparmor_complain ? complainmsg : "");

	return error;

register_security_out:
	free_null_complain_profile();

alloc_out:
	(void)destroy_apparmorfs();

createfs_out:
	return error;

}

static void __exit apparmor_exit(void)
{
	/* Remove and release all the profiles on the profile list. */
	mutex_lock(&aa_interface_lock);
	write_lock(&profile_list_lock);
	while (!list_empty(&profile_list)) {
		struct aa_profile *profile =
			list_entry(profile_list.next, struct aa_profile, list);

		/* Remove the profile from each task context it is on. */
		lock_profile(profile);
		profile->isstale = 1;
		aa_unconfine_tasks(profile);
		unlock_profile(profile);

		/* Release the profile itself. */
		list_del_init(&profile->list);
		aa_put_profile(profile);
	}
	write_unlock(&profile_list_lock);

	/* FIXME: cleanup profiles references on files */

	free_null_complain_profile();

	/**
	 * Delay for an rcu cycle to make sure that all active task
	 * context readers have finished, and all profiles have been
	 * freed by their rcu callbacks.
	 */
	synchronize_rcu();

	destroy_apparmorfs();
	mutex_unlock(&aa_interface_lock);

	if (unregister_security(&apparmor_ops))
		AA_INFO(GFP_KERNEL, "Unable to properly unregister "
			"AppArmor\n");

	AA_INFO(GFP_KERNEL, "AppArmor protection removed\n");
	aa_audit_message(NULL, GFP_KERNEL, 0,
		"AppArmor protection removed\n");
}

module_init(apparmor_init);
module_exit(apparmor_exit);

MODULE_DESCRIPTION("AppArmor process confinement");
MODULE_AUTHOR("Tony Jones <tonyj@suse.de>");
MODULE_LICENSE("GPL");
