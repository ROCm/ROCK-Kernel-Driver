/*
 *	Copyright (C) 2002-2005 Novell/SUSE
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 *
 *	Immunix SubDomain LSM interface
 */

#include <linux/security.h>
#include <linux/module.h>

/* superblock types */

/* PIPEFS_MAGIC */
#include <linux/pipe_fs_i.h>
/* from net/socket.c */
#define SOCKFS_MAGIC 0x534F434B
/* from inotify.c  */
#define INOTIFYFS_MAGIC 0xBAD1DEA

#define VALID_FSTYPE(inode) ((inode)->i_sb->s_magic != PIPEFS_MAGIC && \
                             (inode)->i_sb->s_magic != SOCKFS_MAGIC && \
                             (inode)->i_sb->s_magic != INOTIFYFS_MAGIC)

#include <asm/mman.h>

#include "subdomain.h"
#include "inline.h"

/* main SD lock, manipulated by SD_[RW]LOCK and SD_[RW]UNLOCK (see subdomain.h) */
rwlock_t sd_lock = RW_LOCK_UNLOCKED;

/* Flag values, also controllable via subdomainfs/control. We
 * explicitly do not allow these to be modifiable when exported via
 * /sys/modules/parameters, as we want to do additional mediation and
 * don't want to add special path code. */

/* Complain mode (used to be 'bitch' mode) */
int subdomain_complain = 0;
/* Debug mode */
int subdomain_debug = 0;
/* Audit mode */
int subdomain_audit = 0;

module_param_named(complain, subdomain_complain, int, S_IRUSR);
MODULE_PARM_DESC(subdomain_complain, "Toggle SubDomain Complain Mode");
module_param_named(debug, subdomain_debug, int, S_IRUSR);
MODULE_PARM_DESC(subdomain_debug, "Toggle SubDomain Debug Mode");
module_param_named(audit, subdomain_audit, int, S_IRUSR);
MODULE_PARM_DESC(subdomain_audit, "Toggle SubDomain Audit Mode");
#ifndef MODULE
static int __init sd_complainmode(char *str)
{
	get_option(&str, &subdomain_complain);
	return 1;
}

__setup("subdomain_complain=", sd_complainmode);

static int __init sd_debugmode(char *str)
{
	get_option(&str, &subdomain_debug);
	return 1;
}

__setup("subdomain_debug=", sd_debugmode);

static int __init sd_auditmode(char *str)
{
	get_option(&str, &subdomain_audit);
	return 1;
}

__setup("subdomain_audit=", sd_auditmode);
#endif

static int subdomain_ptrace(struct task_struct *parent,
			    struct task_struct *child)
{
	/* XXX This only protects TRACEME and ATTACH.  We used to guard
	 * all of sys_ptrace(2)...
	 */
	int error;
	struct subdomain *sd;

	error = cap_ptrace(parent, child);

	SD_RLOCK;

	sd = SD_SUBDOMAIN(current->security);

	if (!error && __sd_is_confined(sd)) {
		SD_WARN("REJECTING access to syscall 'ptrace' (%s(%d) profile %s active %s)\n",
			current->comm, current->pid,
			sd->profile->name, sd->active->name);
		error = -EPERM;
	}

	SD_RUNLOCK;

	return error;
}

static int subdomain_capget(struct task_struct *target,
			    kernel_cap_t * effective,
			    kernel_cap_t * inheritable,
			    kernel_cap_t * permitted)
{
	return cap_capget(target, effective, inheritable, permitted);
}

static int subdomain_capset_check(struct task_struct *target,
				  kernel_cap_t *effective,
				  kernel_cap_t *inheritable,
				  kernel_cap_t *permitted)
{
	return cap_capset_check(target, effective, inheritable, permitted);
}

static void subdomain_capset_set(struct task_struct *target,
				 kernel_cap_t *effective,
				 kernel_cap_t *inheritable,
				 kernel_cap_t *permitted)
{
	cap_capset_set(target, effective, inheritable, permitted);
	return;
}

static int subdomain_capable(struct task_struct *tsk, int cap)
{
	int error;

	/* cap_capable returns 0 on success, else -EPERM */
	error = cap_capable(tsk, cap);

	if (error == 0) {
		struct subdomain *sd, sdcopy;

		SD_RLOCK;
		sd = __get_sdcopy(&sdcopy, tsk);
		SD_RUNLOCK;

		error = sd_capability(sd, cap);

		put_sdcopy(sd);
	}

	return error;
}

static int subdomain_sysctl(struct ctl_table *table, int op)
{
	int error = 0;
	struct subdomain *sd;

	SD_RLOCK;

	sd = SD_SUBDOMAIN(current->security);

	if ((op & 002) && __sd_is_confined(sd) && !capable(CAP_SYS_ADMIN)) {
		SD_WARN("REJECTING access to syscall 'sysctl (write)' (%s(%d) profile %s active %s)\n",
			current->comm, current->pid,
			sd->profile->name, sd->active->name);
		error = -EPERM;
	}

	SD_RUNLOCK;

	return error;
}

static int subdomain_syslog(int type)
{
	return cap_syslog(type);
}

static int subdomain_netlink_send(struct sock *sk, struct sk_buff *skb)
{
	return cap_netlink_send(sk, skb);
}

static int subdomain_netlink_recv(struct sk_buff *skb)
{
	return cap_netlink_recv(skb);
}

static void subdomain_bprm_apply_creds(struct linux_binprm *bprm, int unsafe)
{
	cap_bprm_apply_creds(bprm, unsafe);
	return;
}

static int subdomain_bprm_set_security(struct linux_binprm *bprm)
{
	/* handle capability bits with setuid, etc */
	cap_bprm_set_security(bprm);
	/* already set based on script name */
	if (bprm->sh_bang)
		return 0;
	return sd_register(bprm->file);
}

static int subdomain_sb_mount(char *dev_name, struct nameidata *nd, char *type,
			      unsigned long flags, void *data)
{
	int error = 0;
	struct subdomain *sd;

	SD_RLOCK;

	sd = SD_SUBDOMAIN(current->security);

	if (__sd_is_confined(sd)) {
		SD_WARN("REJECTING access to syscall 'mount' (%s(%d) profile %s active %s)\n",
			current->comm, current->pid,
			sd->profile->name, sd->active->name);
		error = -EPERM;
	}

	SD_RUNLOCK;

	return error;
}

static int subdomain_umount(struct vfsmount *mnt, int flags)
{
	int error = 0;
	struct subdomain *sd;

	SD_RLOCK;

	sd = SD_SUBDOMAIN(current->security);

	if (__sd_is_confined(sd)) {
		SD_WARN("REJECTING access to syscall 'umount' (%s(%d) profile %s active %s)\n",
			current->comm, current->pid,
			sd->profile->name, sd->active->name);
		error = -EPERM;
	}

	SD_RUNLOCK;

	return error;
}

static int subdomain_inode_mkdir(struct inode *inode, struct dentry *dentry,
				 int mask)
{
	struct subdomain sdcopy, *sd;
	int error;

	sd = get_sdcopy(&sdcopy);

	error = sd_perm_dentry(sd, dentry, MAY_WRITE);

	put_sdcopy(sd);

	return error;
}

static int subdomain_inode_rmdir(struct inode *inode, struct dentry *dentry)
{
	struct subdomain sdcopy, *sd;
	int error;

	sd = get_sdcopy(&sdcopy);

	error = sd_perm_dentry(sd, dentry, MAY_WRITE | SD_MAY_LINK);

	put_sdcopy(sd);

	return error;
}

static int subdomain_inode_create(struct inode *inode, struct dentry *dentry,
				  int mask)
{
	struct subdomain sdcopy, *sd;
	int error;

	sd = get_sdcopy(&sdcopy);

	/* At a minimum, need write perm to create */
	error = sd_perm_dentry(sd, dentry, MAY_WRITE);

	put_sdcopy(sd);

	return error;
}

static int subdomain_inode_link(struct dentry *old_dentry, struct inode *inode,
				struct dentry *new_dentry)
{
	int error = 0;
	struct subdomain sdcopy, *sd;

	sd = get_sdcopy(&sdcopy);
	error = sd_link(sd, new_dentry, old_dentry);
	put_sdcopy(sd);

	return error;
}

static int subdomain_inode_unlink(struct inode *inode, struct dentry *dentry)
{
	struct subdomain sdcopy, *sd;
	int error;

	sd = get_sdcopy(&sdcopy);

	error = sd_perm_dentry(sd, dentry, MAY_WRITE | SD_MAY_LINK);

	put_sdcopy(sd);

	return error;
}

static int subdomain_inode_mknod(struct inode *inode, struct dentry *dentry,
				 int mode, dev_t dev)
{
	struct subdomain sdcopy, *sd;
	int error = 0;

	sd = get_sdcopy(&sdcopy);

	error = sd_perm_dentry(sd, dentry, MAY_WRITE);

	put_sdcopy(sd);

	return error;
}

static int subdomain_inode_rename(struct inode *old_inode,
				  struct dentry *old_dentry,
				  struct inode *new_inode,
				  struct dentry *new_dentry)
{
	struct subdomain sdcopy, *sd;
	int error = 0;

	sd = get_sdcopy(&sdcopy);

	error = sd_perm_dentry(sd, old_dentry,
			       MAY_READ | MAY_WRITE | SD_MAY_LINK);

	if (!error) {
		error = sd_perm_dentry(sd, new_dentry, MAY_WRITE);
	}

	put_sdcopy(sd);

	return error;
}

static int subdomain_inode_permission(struct inode *inode, int mask,
				      struct nameidata *nd)
{
	int error = 0;

	/* Do not perform check on pipes or sockets
	 * Same as subdomain_file_permission
	 */
	if (VALID_FSTYPE(inode)) {
		struct subdomain sdcopy, *sd;

		sd = get_sdcopy(&sdcopy);
		error = sd_perm_nameidata(sd, nd, mask);
		put_sdcopy(sd);
	}

	return error;
}

static int subdomain_inode_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct subdomain sdcopy, *sd;
	int error = 0;

	if (VALID_FSTYPE(dentry->d_inode)) {

		sd = get_sdcopy(&sdcopy);

		/*
		 * Mediate any attempt to change attributes of a file
		 * (chmod, chown, chgrp, etc)
		 */
		error = sd_attr(sd, dentry, iattr);

		put_sdcopy(sd);
	}

	return error;
}

static int subdomain_inode_setxattr(struct dentry *dentry, char *name,
				    void *value, size_t size, int flags)
{
	int error = 0;

	if (VALID_FSTYPE(dentry->d_inode)) {
		struct subdomain sdcopy, *sd;

		sd = get_sdcopy(&sdcopy);
		error = sd_xattr(sd, dentry, name, MAY_WRITE);
		put_sdcopy(sd);
	}

	return error;
}

static int subdomain_inode_getxattr(struct dentry *dentry, char *name)
{
	int error = 0;

	if (VALID_FSTYPE(dentry->d_inode)) {
		struct subdomain sdcopy, *sd;

		sd = get_sdcopy(&sdcopy);
		error = sd_xattr(sd, dentry, name, MAY_READ);
		put_sdcopy(sd);
	}

	return error;
}
static int subdomain_inode_listxattr(struct dentry *dentry)
{
	int error = 0;

	if (VALID_FSTYPE(dentry->d_inode)) {
		struct subdomain sdcopy, *sd;

		sd = get_sdcopy(&sdcopy);
		error = sd_xattr(sd, dentry, NULL, MAY_READ);
		put_sdcopy(sd);
	}

	return error;
}

static int subdomain_inode_removexattr(struct dentry *dentry, char *name)
{
	int error = 0;

	if (VALID_FSTYPE(dentry->d_inode)) {
		struct subdomain sdcopy, *sd;

		sd = get_sdcopy(&sdcopy);
		error = sd_xattr(sd, dentry, name, MAY_WRITE);
		put_sdcopy(sd);
	}

	return error;
}

static int subdomain_file_permission(struct file *file, int mask)
{
	struct subdomain sdcopy, *sd;
	struct sdprofile *f_profile;
	int error = 0;

	sd = get_sdcopy(&sdcopy);

	f_profile = SD_PROFILE(file->f_security);

	if (__sd_is_confined(sd) && f_profile && f_profile != sd->active &&
	    VALID_FSTYPE(file->f_dentry->d_inode))
		error = sd_perm(sd, file->f_dentry, file->f_vfsmnt,
				mask & (MAY_EXEC | MAY_WRITE | MAY_READ));

	put_sdcopy(sd);

	return error;
}

static int subdomain_file_alloc_security(struct file *file)
{
	struct subdomain sdcopy, *sd;

	sd = get_sdcopy(&sdcopy);

	if (__sd_is_confined(sd)) {
		file->f_security = get_sdprofile(sd->active);
	}

	put_sdcopy(sd);

	return 0;
}

static void subdomain_file_free_security(struct file *file)
{
	struct sdprofile *p = SD_PROFILE(file->f_security);
	put_sdprofile(p);
}

static int subdomain_file_mmap (struct file *file, unsigned long reqprot,
 				unsigned long prot, unsigned long flags)
{
	int error = 0, mask = 0;
	struct subdomain sdcopy, *sd;
	struct sdprofile *f_profile;

	sd = get_sdcopy(&sdcopy);

	f_profile = file ? SD_PROFILE(file->f_security) : NULL;

	if (prot & PROT_READ)
		mask |= MAY_READ;
	if (prot & PROT_WRITE)
		mask |= MAY_WRITE;
	if (prot & PROT_EXEC)
		mask |= MAY_EXEC;

	SD_DEBUG("%s: 0x%x\n", __FUNCTION__, mask);

	/* Don't check if no subdomain's, profiles haven't changed, or
	 * mapping in the executable
	 */
	if (file && __sd_sub_defined(sd) &&
	    f_profile != sd->active &&
	    !(flags & MAP_EXECUTABLE))
		error = sd_perm(sd, file->f_dentry, file->f_vfsmnt, mask);

	put_sdcopy(sd);

	return error;
}

static int subdomain_task_alloc_security(struct task_struct *p)
{
	return sd_fork(p);
}

static void subdomain_task_free_security(struct task_struct *p)
{
	sd_release(p);
}

static int subdomain_task_post_setuid(uid_t id0, uid_t id1, uid_t id2,
				      int flags)
{
	return cap_task_post_setuid(id0, id1, id2, flags);
}

static void subdomain_task_reparent_to_init(struct task_struct *p)
{
	cap_task_reparent_to_init(p);
	return;
}

static int subdomain_getprocattr(struct task_struct *p, char *name, void *value,
				 size_t size)
{
	int error;
	struct subdomain sdcopy, *sd;
	char *str = value;

	/* Subdomain only supports the "current" process attribute */
	if (strcmp(name, "current") != 0) {
		error = -EINVAL;
		goto out;
	}

	if (!size) {
		error = -ERANGE;
		goto out;
	}

	/* must be task querying itself or admin */
	if (current != p && !capable(CAP_SYS_ADMIN)) {
		error = -EPERM;
		goto out;
	}

	SD_RLOCK;

	sd = __get_sdcopy(&sdcopy, p);

	SD_RUNLOCK;

	error = sd_getprocattr(sd, str, size);
	put_sdcopy(sd);

out:
	return error;
}

static int subdomain_setprocattr(struct task_struct *p, char *name, void *value,
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

	/* CHANGE HAT */
	if (size > strlen(cmd_changehat) &&
	    strncmp(cmd, cmd_changehat, strlen(cmd_changehat)) == 0) {
		char *hatinfo = cmd + strlen(cmd_changehat);
		size_t infosize = size - strlen(cmd_changehat);

		/* Only the current process may change it's hat */
		if (current != p) {
			SD_WARN("%s: Attempt by foreign task %s(%d) [user %d] to changehat of task %s(%d)\n",
				__FUNCTION__,
				current->comm,
				current->pid,
				current->uid,
				p->comm,
				p->pid);

			error = -EACCES;
			goto out;
		}

		error = sd_setprocattr_changehat(hatinfo, infosize);
		if (error == 0) {
			/* success, set return to #bytes in orig request */
			error = size;
		}

	/* SET NEW PROFILE */
	} else if (size > strlen(cmd_setprofile) &&
		   strncmp(cmd, cmd_setprofile, strlen(cmd_setprofile)) == 0) {
		int confined;

		/* only an unconfined process with admin capabilities
		 * may change the profile of another task
		 */

		if (!capable(CAP_SYS_ADMIN)) {
			SD_WARN("%s: Unprivileged attempt by task %s(%d) [user %d] to assign new profile to task %s(%d)\n",
				__FUNCTION__,
				current->comm,
				current->pid,
				current->uid,
				p->comm,
				p->pid);
			error = -EACCES;
			goto out;
		}

		SD_RLOCK;
		confined = sd_is_confined();
		SD_RUNLOCK;

		if (!confined) {
			char *profile = cmd + strlen(cmd_setprofile);
			size_t profilesize = size - strlen(cmd_setprofile);

			error = sd_setprocattr_setprofile(p, profile, profilesize);
			if (error == 0) {
				/* success,
				 * set return to #bytes in orig request
				 */
				error = size;
			}
		} else {
			SD_WARN("%s: Attempt by confined task %s(%d) [user %d] to assign new profile to task %s(%d)\n",
				__FUNCTION__,
				current->comm,
				current->pid,
				current->uid,
				p->comm,
				p->pid);

			error = -EACCES;
		}
	} else {
		/* unknown operation */
		SD_WARN("%s: Unknown setprocattr command '%.*s' by task %s(%d) [user %d] for task %s(%d)\n",
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

struct security_operations subdomain_ops = {
	.ptrace =			subdomain_ptrace,
	.capget =			subdomain_capget,
	.capset_check =			subdomain_capset_check,
	.capset_set =			subdomain_capset_set,
	.sysctl =			subdomain_sysctl,
	.capable =			subdomain_capable,
	.syslog =			subdomain_syslog,

	.netlink_send =			subdomain_netlink_send,
	.netlink_recv =			subdomain_netlink_recv,

	.bprm_apply_creds =		subdomain_bprm_apply_creds,
	.bprm_set_security =		subdomain_bprm_set_security,

	.sb_mount =			subdomain_sb_mount,
	.sb_umount =			subdomain_umount,

	.inode_mkdir =			subdomain_inode_mkdir,
	.inode_rmdir =			subdomain_inode_rmdir,
	.inode_create =			subdomain_inode_create,
	.inode_link =			subdomain_inode_link,
	.inode_unlink =			subdomain_inode_unlink,
	.inode_mknod =			subdomain_inode_mknod,
	.inode_rename =			subdomain_inode_rename,
	.inode_permission =		subdomain_inode_permission,
	.inode_setattr =		subdomain_inode_setattr,
	.inode_setxattr =		subdomain_inode_setxattr,
	.inode_getxattr =		subdomain_inode_getxattr,
	.inode_listxattr =		subdomain_inode_listxattr,
	.inode_removexattr =		subdomain_inode_removexattr,
	.file_permission =		subdomain_file_permission,
	.file_alloc_security =		subdomain_file_alloc_security,
	.file_free_security =		subdomain_file_free_security,
	.file_mmap =			subdomain_file_mmap,

	.task_alloc_security =		subdomain_task_alloc_security,
	.task_free_security =		subdomain_task_free_security,
	.task_post_setuid =		subdomain_task_post_setuid,
	.task_reparent_to_init =	subdomain_task_reparent_to_init,

	.getprocattr =			subdomain_getprocattr,
	.setprocattr =			subdomain_setprocattr,
};

static int __init subdomain_init(void)
{
	int error = 0;
	const char *complainmsg = ": complainmode enabled";

	/*
	 * CREATE SUBDOMAINFS
	 */
	if (!create_subdomainfs()) {
		SD_ERROR("Unable to activate SubDomain filesystem\n");
		error = -ENOENT;
		goto createfs_out;
	}

	/*
	 * CREATE NULL PROFILES
	 */
	null_profile = alloc_sdprofile();
	null_complain_profile = alloc_sdprofile();
	if (!null_profile || !null_complain_profile) {
		SD_ERROR("Unable to allocate null profiles\n");
		error = -ENOMEM;
		goto dealloc_out;
	}

	null_profile->name = kstrdup("null-profile", GFP_KERNEL);
	null_complain_profile->name = kstrdup("null-complain-profile", GFP_KERNEL);
	if (!null_profile->name || !null_complain_profile->name) {
		error = -ENOMEM;
		goto dealloc_out;
	}

	get_sdprofile(null_profile);
	get_sdprofile(null_complain_profile);
	null_complain_profile->flags.complain = 1;

	/*
	 * REGISTER SubDomain WITH LSM
	 */
	if ((error = register_security(&subdomain_ops))) {
		SD_WARN("Unable to load SubDomain\n");
		goto dealloc_out;
	}
	SD_INFO("SubDomain (version %s) initialized%s\n",
		subdomain_version(),
		subdomain_complain ? complainmsg : "");

	/* DONE */
	return error;

dealloc_out:
	if (!null_complain_profile) {
		free_sdprofile(null_complain_profile);
		null_complain_profile = NULL;
	}

	if (!null_profile) {
		free_sdprofile(null_profile);
		null_profile = NULL;
	}
	(void)destroy_subdomainfs();

createfs_out:
	return error;

}

static int subdomain_exit_removeall_iter(struct subdomain *sd, void *cookie)
{
	/* SD_WLOCK held here */

	if (__sd_is_confined(sd)) {
		SD_DEBUG("%s: Dropping profiles %s(%d) profile %s(%p) active %s(%p)\n",
			 __FUNCTION__,
			 sd->task->comm, sd->task->pid,
			 sd->profile->name, sd->profile,
			 sd->active->name, sd->active);
		sd_switch_unconfined(sd);
	}

	return 0;
}

static void __exit subdomain_exit(void)
{
	/* Remove profiles from the global profile list.
	 * This is just for tidyness as there is no way to reference this
	 * list once the SubDomain lsm hooks are detached (below)
	 */
	sd_profilelist_release();

	/* Remove profiles from active tasks
	 * If this is not done,  if module is reloaded after being removed,
	 * old profiles (still refcounted in memory) will become 'magically'
	 * reattached
	 */

	SD_WLOCK;
	sd_subdomainlist_iterate(subdomain_exit_removeall_iter, NULL);
	SD_WUNLOCK;

	/* Free up list of active subdomain */
	sd_subdomainlist_release();

	put_sdprofile(null_complain_profile);
	put_sdprofile(null_profile);
	if (!destroy_subdomainfs())
		SD_WARN("Unable to properly deactivate SubDomain fs\n");

	if (unregister_security(&subdomain_ops))
		SD_WARN("Unable to properly unregister SubDomain\n");
	SD_INFO("SubDomain protection removed\n");
}

security_initcall(subdomain_init);
module_exit(subdomain_exit);

MODULE_DESCRIPTION("SubDomain process confinement");
MODULE_AUTHOR("SuSE/Novell <apparmor-general@forge.novell.com>");
MODULE_LICENSE("GPL");
