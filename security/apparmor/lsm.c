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

#include "apparmor.h"
#include "inline.h"

/* struct subdomain write update lock (read side is RCU). */
spinlock_t sd_lock = SPIN_LOCK_UNLOCKED;

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

#ifndef MODULE
static int __init aa_getopt_complain(char *str)
{
	get_option(&str, &apparmor_complain);
	return 1;
}
__setup("apparmor_complain=", aa_getopt_complain);

static int __init aa_getopt_debug(char *str)
{
	get_option(&str, &apparmor_debug);
	return 1;
}
__setup("apparmor_debug=", aa_getopt_debug);

static int __init aa_getopt_audit(char *str)
{
	get_option(&str, &apparmor_audit);
	return 1;
}
__setup("apparmor_audit=", aa_getopt_audit);

static int __init aa_getopt_logsyscall(char *str)
{
	get_option(&str, &apparmor_logsyscall);
	return 1;
}
__setup("apparmor_logsyscall=", aa_getopt_logsyscall);
#endif

static int apparmor_ptrace(struct task_struct *parent,
			    struct task_struct *child)
{
	int error;
	struct aaprofile *active;

	error = cap_ptrace(parent, child);

	active = get_task_active_aaprofile(parent);

	if (!error && active) {
		error = aa_audit_syscallreject(active, GFP_KERNEL, "ptrace");
		WARN_ON(error != -EPERM);
	}

	put_aaprofile(active);

	return error;
}

static int apparmor_capget(struct task_struct *target,
			    kernel_cap_t *effective,
			    kernel_cap_t *inheritable,
			    kernel_cap_t *permitted)
{
	return cap_capget(target, effective, inheritable, permitted);
}

static int apparmor_capset_check(struct task_struct *target,
				  kernel_cap_t *effective,
				  kernel_cap_t *inheritable,
				  kernel_cap_t *permitted)
{
	return cap_capset_check(target, effective, inheritable, permitted);
}

static void apparmor_capset_set(struct task_struct *target,
				 kernel_cap_t *effective,
				 kernel_cap_t *inheritable,
				 kernel_cap_t *permitted)
{
	cap_capset_set(target, effective, inheritable, permitted);
	return;
}

static int apparmor_capable(struct task_struct *tsk, int cap)
{
	int error;

	/* cap_capable returns 0 on success, else -EPERM */
	error = cap_capable(tsk, cap);

	if (error == 0) {
		struct aaprofile *active;

		active = get_task_active_aaprofile(tsk);

		if (active)
			error = aa_capability(active, cap);

		put_aaprofile(active);
	}

	return error;
}

static int apparmor_sysctl(struct ctl_table *table, int op)
{
	int error = 0;
	struct aaprofile *active;

	active = get_active_aaprofile();

	if ((op & 002) && active && !capable(CAP_SYS_ADMIN)) {
		error = aa_audit_syscallreject(active, GFP_KERNEL,
					       "sysctl (write)");
		WARN_ON(error != -EPERM);
	}

	put_aaprofile(active);

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
	return;
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

	if (ret == 0 &&
	    (unsigned long)bprm->security & AA_SECURE_EXEC_NEEDED) {
		AA_DEBUG("%s: secureexec required for %s\n",
			 __FUNCTION__, bprm->filename);
		ret = 1;
	}

	return ret;
}

static int apparmor_sb_mount(char *dev_name, struct nameidata *nd, char *type,
			      unsigned long flags, void *data)
{
	int error = 0;
	struct aaprofile *active;

	active = get_active_aaprofile();

	if (active) {
		error = aa_audit_syscallreject(active, GFP_KERNEL, "mount");
		WARN_ON(error != -EPERM);
	}

	put_aaprofile(active);

	return error;
}

static int apparmor_umount(struct vfsmount *mnt, int flags)
{
	int error = 0;
	struct aaprofile *active;

	active = get_active_aaprofile();

	if (active) {
		error = aa_audit_syscallreject(active, GFP_ATOMIC, "umount");
		WARN_ON(error != -EPERM);
	}

	put_aaprofile(active);

	return error;
}

static int apparmor_inode_mkdir(struct inode *inode, struct dentry *dentry,
				 int mask)
{
	struct aaprofile *active;
	int error = 0;

	active = get_active_aaprofile();

	if (active)
		error = aa_perm_dir(active, dentry, aa_dir_mkdir);

	put_aaprofile(active);

	return error;
}

static int apparmor_inode_rmdir(struct inode *inode, struct dentry *dentry)
{
	struct aaprofile *active;
	int error = 0;

	active = get_active_aaprofile();

	if (active)
		error = aa_perm_dir(active, dentry, aa_dir_rmdir);

	put_aaprofile(active);

	return error;
}

static int apparmor_inode_create(struct inode *inode, struct dentry *dentry,
				  int mask)
{
	struct aaprofile *active;
	int error = 0;

	active = get_active_aaprofile();

	/* At a minimum, need write perm to create */
	if (active)
		error = aa_perm_dentry(active, dentry, MAY_WRITE);

	put_aaprofile(active);

	return error;
}

static int apparmor_inode_link(struct dentry *old_dentry, struct inode *inode,
				struct dentry *new_dentry)
{
	int error = 0;
	struct aaprofile *active;

	active = get_active_aaprofile();

	if (active)
		error = aa_link(active, new_dentry, old_dentry);

	put_aaprofile(active);

	return error;
}

static int apparmor_inode_unlink(struct inode *inode, struct dentry *dentry)
{
	struct aaprofile *active;
	int error = 0;

	active = get_active_aaprofile();

	if (active)
		error = aa_perm_dentry(active, dentry, MAY_WRITE);

	put_aaprofile(active);

	return error;
}

static int apparmor_inode_mknod(struct inode *inode, struct dentry *dentry,
				 int mode, dev_t dev)
{
	struct aaprofile *active;
	int error = 0;

	active = get_active_aaprofile();

	if (active)
		error = aa_perm_dentry(active, dentry, MAY_WRITE);

	put_aaprofile(active);

	return error;
}

static int apparmor_inode_rename(struct inode *old_inode,
				  struct dentry *old_dentry,
				  struct inode *new_inode,
				  struct dentry *new_dentry)
{
	struct aaprofile *active;
	int error = 0;

	active = get_active_aaprofile();

	if (active) {
		error = aa_perm_dentry(active, old_dentry, MAY_READ |
				       MAY_WRITE);

		if (!error)
			error = aa_perm_dentry(active, new_dentry,
					       MAY_WRITE);
	}

	put_aaprofile(active);

	return error;
}

static int apparmor_inode_permission(struct inode *inode, int mask,
				      struct nameidata *nd)
{
	int error = 0;

	/* Do not perform check on pipes or sockets
	 * Same as apparmor_file_permission
	 */
	if (VALID_FSTYPE(inode)) {
		struct aaprofile *active;

		active = get_active_aaprofile();
		if (active)
			error = aa_perm_nameidata(active, nd, mask);
		put_aaprofile(active);
	}

	return error;
}

static int apparmor_inode_setattr(struct dentry *dentry, struct iattr *iattr)
{
	int error = 0;

	if (VALID_FSTYPE(dentry->d_inode)) {
		struct aaprofile *active;

		active = get_active_aaprofile();
		/*
		 * Mediate any attempt to change attributes of a file
		 * (chmod, chown, chgrp, etc)
		 */
		if (active)
			error = aa_attr(active, dentry, iattr);

		put_aaprofile(active);
	}

	return error;
}

static int apparmor_inode_setxattr(struct dentry *dentry, char *name,
				    void *value, size_t size, int flags)
{
	int error = 0;

	if (VALID_FSTYPE(dentry->d_inode)) {
		struct aaprofile *active;

		active = get_active_aaprofile();
		if (active)
			error = aa_xattr(active, dentry, name, aa_xattr_set);
		put_aaprofile(active);
	}

	return error;
}

static int apparmor_inode_getxattr(struct dentry *dentry, char *name)
{
	int error = 0;

	if (VALID_FSTYPE(dentry->d_inode)) {
		struct aaprofile *active;

		active = get_active_aaprofile();
		if (active)
			error = aa_xattr(active, dentry, name, aa_xattr_get);
		put_aaprofile(active);
	}

	return error;
}
static int apparmor_inode_listxattr(struct dentry *dentry)
{
	int error = 0;

	if (VALID_FSTYPE(dentry->d_inode)) {
		struct aaprofile *active;

		active = get_active_aaprofile();
		if (active)
			error = aa_xattr(active, dentry, NULL, aa_xattr_list);
		put_aaprofile(active);
	}

	return error;
}

static int apparmor_inode_removexattr(struct dentry *dentry, char *name)
{
	int error = 0;

	if (VALID_FSTYPE(dentry->d_inode)) {
		struct aaprofile *active;

		active = get_active_aaprofile();
		if (active)
			error = aa_xattr(active, dentry, name,
					 aa_xattr_remove);
		put_aaprofile(active);
	}

	return error;
}

static int apparmor_file_permission(struct file *file, int mask)
{
	struct aaprofile *active;
	struct aafile *aaf;
	int error = 0;

	aaf = (struct aafile *)file->f_security;
	/* bail out early if this isn't a mediated file */
	if (!aaf || !VALID_FSTYPE(file->f_dentry->d_inode))
		goto out;

	active = get_active_aaprofile();
	if (active && aaf->profile != active)
		error = aa_perm(active, file->f_dentry, file->f_vfsmnt,
				mask & (MAY_EXEC | MAY_WRITE | MAY_READ));
	put_aaprofile(active);

out:
	return error;
}

static int apparmor_file_alloc_security(struct file *file)
{
	struct aaprofile *active;
	int error = 0;

	active = get_active_aaprofile();
	if (active) {
		struct aafile *aaf;
		aaf = kmalloc(sizeof(struct aafile), GFP_KERNEL);

		if (aaf) {
			aaf->type = aa_file_default;
			aaf->profile = get_aaprofile(active);
		} else {
			error = -ENOMEM;
		}
		file->f_security = aaf;
	}
	put_aaprofile(active);

	return error;
}

static void apparmor_file_free_security(struct file *file)
{
	struct aafile *aaf = (struct aafile *)file->f_security;

	if (aaf) {
		put_aaprofile(aaf->profile);
		kfree(aaf);
	}
}

static inline int aa_mmap(struct file *file, unsigned long prot,
			  unsigned long flags)
{
	int error = 0, mask = 0;
	struct aaprofile *active;
	struct aafile *aaf;

	active = get_active_aaprofile();
	if (!active || !file ||
	    !(aaf = (struct aafile *)file->f_security) ||
	    aaf->type == aa_file_shmem)
		goto out;

	if (prot & PROT_READ)
		mask |= MAY_READ;

	/* Private mappings don't require write perms since they don't
	 * write back to the files */
	if (prot & PROT_WRITE && !(flags & MAP_PRIVATE))
		mask |= MAY_WRITE;
	if (prot & PROT_EXEC)
		mask |= AA_EXEC_MMAP;

	AA_DEBUG("%s: 0x%x\n", __FUNCTION__, mask);

	if (mask)
		error = aa_perm(active, file->f_dentry, file->f_vfsmnt, mask);

	put_aaprofile(active);

out:
	return error;
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

static int apparmor_task_alloc_security(struct task_struct *p)
{
	return aa_fork(p);
}

static void apparmor_task_free_security(struct task_struct *p)
{
	aa_release(p);
}

static int apparmor_task_post_setuid(uid_t id0, uid_t id1, uid_t id2,
				     int flags)
{
	return cap_task_post_setuid(id0, id1, id2, flags);
}

static void apparmor_task_reparent_to_init(struct task_struct *p)
{
	cap_task_reparent_to_init(p);
	return;
}

static int apparmor_shm_shmat(struct shmid_kernel *shp, char __user *shmaddr,
			      int shmflg)
{
	struct aafile *aaf = (struct aafile *)shp->shm_file->f_security;

	if (aaf)
		aaf->type = aa_file_shmem;

	return 0;
}

static int apparmor_getprocattr(struct task_struct *p, char *name, void *value,
				size_t size)
{
	int error;
	struct aaprofile *active;
	char *str = value;

	/* AppArmor only supports the "current" process attribute */
	if (strcmp(name, "current") != 0) {
		error = -EINVAL;
		goto out;
	}

	/* must be task querying itself or admin */
	if (current != p && !capable(CAP_SYS_ADMIN)) {
		error = -EPERM;
		goto out;
	}

	active = get_task_active_aaprofile(p);
	error = aa_getprocattr(active, str, size);
	put_aaprofile(active);

out:
	return error;
}

static int apparmor_setprocattr(struct task_struct *p, char *name, void *value,
				 size_t size)
{
	const char *cmd_changehat = "changehat ",
		   *cmd_setprofile = "setprofile ";

	int error = -EACCES;	/* default to a perm denied */
	char *cmd = (char *)value;

	/* only support messages to current */
	if (strcmp(name, "current") != 0) {
		error = -EINVAL;
		goto out;
	}

	if (!size) {
		error = -ERANGE;
		goto out;
	}

	/* CHANGE HAT -- switch task into a subhat (subprofile) if defined */
	if (size > strlen(cmd_changehat) &&
	    strncmp(cmd, cmd_changehat, strlen(cmd_changehat)) == 0) {
		char *hatinfo = cmd + strlen(cmd_changehat);
		size_t infosize = size - strlen(cmd_changehat);

		/* Only the current process may change it's hat */
		if (current != p) {
			AA_WARN("%s: Attempt by foreign task %s(%d) "
				"[user %d] to changehat of task %s(%d)\n",
				__FUNCTION__,
				current->comm,
				current->pid,
				current->uid,
				p->comm,
				p->pid);

			error = -EACCES;
			goto out;
		}

		error = aa_setprocattr_changehat(hatinfo, infosize);
		if (error == 0)
			/* success, set return to #bytes in orig request */
			error = size;

	/* SET NEW PROFILE */
	} else if (size > strlen(cmd_setprofile) &&
		   strncmp(cmd, cmd_setprofile, strlen(cmd_setprofile)) == 0) {
		struct aaprofile *active;

		/* only an unconfined process with admin capabilities
		 * may change the profile of another task
		 */

		if (!capable(CAP_SYS_ADMIN)) {
			AA_WARN("%s: Unprivileged attempt by task %s(%d) "
				"[user %d] to assign profile to task %s(%d)\n",
				__FUNCTION__,
				current->comm,
				current->pid,
				current->uid,
				p->comm,
				p->pid);
			error = -EACCES;
			goto out;
		}

		active = get_active_aaprofile();
		if (!active) {
			char *profile = cmd + strlen(cmd_setprofile);
			size_t profilesize = size - strlen(cmd_setprofile);

			error = aa_setprocattr_setprofile(p, profile, profilesize);
			if (error == 0)
				/* success,
				 * set return to #bytes in orig request
				 */
				error = size;
		} else {
			AA_WARN("%s: Attempt by confined task %s(%d) "
				"[user %d] to assign profile to task %s(%d)\n",
				__FUNCTION__,
				current->comm,
				current->pid,
				current->uid,
				p->comm,
				p->pid);

			error = -EACCES;
		}
		put_aaprofile(active);
	} else {
		/* unknown operation */
		AA_WARN("%s: Unknown setprocattr command '%.*s' by task %s(%d) "
			"[user %d] for task %s(%d)\n",
			__FUNCTION__,
			size < 16 ? (int)size : 16,
			cmd,
			current->comm,
			current->pid,
			current->uid,
			p->comm,
			p->pid);

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

	.task_alloc_security =		apparmor_task_alloc_security,
	.task_free_security =		apparmor_task_free_security,
	.task_post_setuid =		apparmor_task_post_setuid,
	.task_reparent_to_init =	apparmor_task_reparent_to_init,

	.shm_shmat =			apparmor_shm_shmat,

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

	AA_INFO("AppArmor initialized%s\n",
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

static int apparmor_exit_removeall_iter(struct subdomain *sd, void *cookie)
{
	/* spin_lock(&sd_lock) held here */

	if (__aa_is_confined(sd)) {
		AA_DEBUG("%s: Dropping profiles %s(%d) "
			 "profile %s(%p) active %s(%p)\n",
			 __FUNCTION__,
			 sd->task->comm, sd->task->pid,
			 BASE_PROFILE(sd->active)->name,
			 BASE_PROFILE(sd->active),
			 sd->active->name, sd->active);
		aa_switch_unconfined(sd);
	}

	return 0;
}

static void __exit apparmor_exit(void)
{
	unsigned long flags;

	/* Remove profiles from the global profile list.
	 * This is just for tidyness as there is no way to reference this
	 * list once the AppArmor lsm hooks are detached (below)
	 */
	aa_profilelist_release();

	/* Remove profiles from active tasks
	 * If this is not done,  if module is reloaded after being removed,
	 * old profiles (still refcounted in memory) will become 'magically'
	 * reattached
	 */

	spin_lock_irqsave(&sd_lock, flags);
	aa_subdomainlist_iterate(apparmor_exit_removeall_iter, NULL);
	spin_unlock_irqrestore(&sd_lock, flags);

	/* Free up list of active subdomain */
	aa_subdomainlist_release();

	free_null_complain_profile();

	destroy_apparmorfs();

	if (unregister_security(&apparmor_ops))
		AA_WARN("Unable to properly unregister AppArmor\n");

	/* delay for an rcu cycle to make ensure that profiles pending
	 * destruction in the rcu callback are freed.
	 */
	synchronize_rcu();

	AA_INFO("AppArmor protection removed\n");
	aa_audit_message(NULL, GFP_KERNEL, 0,
		"AppArmor protection removed\n");
}

module_init(apparmor_init);
module_exit(apparmor_exit);

MODULE_VERSION(APPARMOR_VERSION);
MODULE_DESCRIPTION("AppArmor process confinement");
MODULE_AUTHOR("Tony Jones <tonyj@suse.de>");
MODULE_LICENSE("GPL");
