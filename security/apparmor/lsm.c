/*
 *	Copyright (C) 1998-2007 Novell/SUSE
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 *
 *	AppArmor LSM interface
 */

#include <linux/security.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/ctype.h>
#include <linux/sysctl.h>
#include <linux/audit.h>
#include <net/sock.h>

#include "apparmor.h"
#include "inline.h"

/* point to the apparmor module */
struct module *aa_module = NULL;

/* secondary ops if apparmor is stacked */
static struct security_operations *aa_secondary_ops = NULL;
static DEFINE_MUTEX(aa_secondary_lock);

#define AA_SECONDARY(FN, ARGS...) \
       ({ \
               struct security_operations *__f1; \
               __f1 = rcu_dereference(aa_secondary_ops); \
               (unlikely(__f1) && __f1->FN) ? __f1->FN(ARGS) : 0; \
       })

static int param_set_aabool(const char *val, struct kernel_param *kp);
static int param_get_aabool(char *buffer, struct kernel_param *kp);
#define param_check_aabool(name, p) __param_check(name, p, int)

static int param_set_aauint(const char *val, struct kernel_param *kp);
static int param_get_aauint(char *buffer, struct kernel_param *kp);
#define param_check_aauint(name, p) __param_check(name, p, int)

/* Flag values, also controllable via /sys/module/apparmor/parameters
 * We define special types as we want to do additional mediation.
 *
 * Complain mode -- in complain mode access failures result in auditing only
 * and task is allowed access.  audit events are processed by userspace to
 * generate policy.  Default is 'enforce' (0).
 * Value is also togglable per profile and referenced when global value is
 * enforce.
 */
int apparmor_complain = 0;
module_param_named(complain, apparmor_complain, aabool, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(apparmor_complain, "Toggle AppArmor complain mode");

/* Debug mode */
int apparmor_debug = 0;
module_param_named(debug, apparmor_debug, aabool, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(apparmor_debug, "Toggle AppArmor debug mode");

/* Audit mode */
int apparmor_audit = 0;
module_param_named(audit, apparmor_audit, aabool, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(apparmor_audit, "Toggle AppArmor audit mode");

/* Syscall logging mode */
int apparmor_logsyscall = 0;
module_param_named(logsyscall, apparmor_logsyscall, aabool, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(apparmor_logsyscall, "Toggle AppArmor logsyscall mode");

/* Maximum pathname length before accesses will start getting rejected */
unsigned int apparmor_path_max = 2 * PATH_MAX;
module_param_named(path_max, apparmor_path_max, aauint, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(apparmor_path_max, "Maximum pathname length allowed");

static int param_set_aabool(const char *val, struct kernel_param *kp)
{
	if (aa_task_context(current))
		return -EPERM;
	return param_set_bool(val, kp);
}

static int param_get_aabool(char *buffer, struct kernel_param *kp)
{
	if (aa_task_context(current))
		return -EPERM;
	return param_get_bool(buffer, kp);
}

static int param_set_aauint(const char *val, struct kernel_param *kp)
{
	if (aa_task_context(current))
		return -EPERM;
	return param_set_uint(val, kp);
}

static int param_get_aauint(char *buffer, struct kernel_param *kp)
{
	if (aa_task_context(current))
		return -EPERM;
	return param_get_uint(buffer, kp);
}

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
	struct aa_task_context *cxt;
	int error = 0;

	/*
	 * parent can ptrace child when
	 * - parent is unconfined
	 * - parent & child are in the same namespace &&
	 *   - parent is in complain mode
	 *   - parent and child are confined by the same profile
	 *   - parent profile has CAP_SYS_PTRACE
	 */

	rcu_read_lock();
	cxt = aa_task_context(parent);
	if (cxt) {
		if (parent->nsproxy != child->nsproxy) {
			struct aa_audit sa;
			memset(&sa, 0, sizeof(sa));
			sa.operation = "ptrace";
			sa.gfp_mask = GFP_ATOMIC;
			sa.parent = parent->pid;
			sa.task = child->pid;
			sa.info = "different namespaces";
			aa_audit_reject(cxt->profile, &sa);
			error = -EPERM;
		} else {
			struct aa_task_context *child_cxt =
				aa_task_context(child);

			error = aa_may_ptrace(cxt, child_cxt ?
						   child_cxt->profile : NULL);
			if (PROFILE_COMPLAIN(cxt->profile)) {
				struct aa_audit sa;
				memset(&sa, 0, sizeof(sa));
				sa.operation = "ptrace";
				sa.gfp_mask = GFP_ATOMIC;
				sa.parent = parent->pid;
				sa.task = child->pid;
				aa_audit_hint(cxt->profile, &sa);
			}
		}
	}
	rcu_read_unlock();

	return error;
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
	struct aa_profile *profile = aa_get_profile(current);
	int error = 0;

	if (profile) {
		char *buffer, *name;
		int mask;

		mask = 0;
		if (op & 4)
			mask |= MAY_READ;
		if (op & 2)
			mask |= MAY_WRITE;

		error = -ENOMEM;
		buffer = (char*)__get_free_page(GFP_KERNEL);
		if (!buffer)
			goto out;
		name = sysctl_pathname(table, buffer, PAGE_SIZE);
		if (name && name - buffer >= 5) {
			name -= 5;
			memcpy(name, "/proc", 5);
			error = aa_perm_path(profile, "sysctl", name, mask);
		}
		free_page((unsigned long)buffer);
	}

out:
	return error;
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
		error = aa_perm_dir(profile, "inode_mkdir", dentry, mnt,
				    MAY_WRITE);

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
		error = aa_perm_dir(profile, "inode_rmdir", dentry, mnt,
				    MAY_WRITE);

	aa_put_profile(profile);

out:
	return error;
}

static int aa_permission(const char *operation, struct inode *inode,
			 struct dentry *dentry, struct vfsmount *mnt,
			 int mask, int check)
{
	int error = 0;

	if (mnt && mediated_filesystem(inode)) {
		struct aa_profile *profile;

		profile = aa_get_profile(current);
		if (profile)
			error = aa_perm(profile, operation, dentry, mnt, mask,
					check);
		aa_put_profile(profile);
	}
	return error;
}

static inline int aa_mask_permissions(int mask)
{
	if (mask & MAY_APPEND)
		mask &= (MAY_READ | MAY_APPEND | MAY_EXEC);
	else
		mask &= (MAY_READ | MAY_WRITE | MAY_EXEC);
	return mask;
}

static int apparmor_inode_create(struct inode *dir, struct dentry *dentry,
				 struct vfsmount *mnt, int mask)
{
	/* FIXME: may move to MAY_APPEND later */
	return aa_permission("inode_create", dir, dentry, mnt, MAY_WRITE, 0);
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
	int check = 0;

	if (S_ISDIR(dentry->d_inode->i_mode))
		check |= AA_CHECK_DIR;
	return aa_permission("inode_unlink", dir, dentry, mnt, MAY_WRITE,
			     check);
}

static int apparmor_inode_symlink(struct inode *dir, struct dentry *dentry,
				  struct vfsmount *mnt, const char *old_name)
{
	return aa_permission("inode_symlink", dir, dentry, mnt, MAY_WRITE, 0);
}

static int apparmor_inode_mknod(struct inode *dir, struct dentry *dentry,
				struct vfsmount *mnt, int mode, dev_t dev)
{
	return aa_permission("inode_mknod", dir, dentry, mnt, MAY_WRITE, 0);
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
		int check = 0;

		if (inode && S_ISDIR(inode->i_mode))
			check |= AA_CHECK_DIR;
		if (old_mnt)
			error = aa_perm(profile, "inode_rename", old_dentry,
					old_mnt, MAY_READ | MAY_WRITE, check);

		if (!error && new_mnt) {
			error = aa_perm(profile, "inode_rename", new_dentry,
					new_mnt, MAY_WRITE, check);
		}
	}

	aa_put_profile(profile);

out:
	return error;
}

static int apparmor_inode_permission(struct inode *inode, int mask,
				     struct nameidata *nd)
{
	int check = 0, error = 0;

	if (!nd || nd->flags & (LOOKUP_PARENT | LOOKUP_CONTINUE))
		goto out;
	mask = aa_mask_permissions(mask);
	if (S_ISDIR(inode->i_mode)) {
		check |= AA_CHECK_DIR;
		/* allow traverse accesses to directories */
		mask &= ~MAY_EXEC;
	}
	error = aa_permission("inode_permission", inode, nd->dentry, nd->mnt,
			      mask, check);
out:
	if (!error)
		error = AA_SECONDARY(inode_permission, inode, mask, nd);

	return error;
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
			       const char *operation, int mask,
			       struct file *file)
{
	int error = 0;

	if (mnt && mediated_filesystem(dentry->d_inode)) {
		struct aa_profile *profile = aa_get_profile(current);
		int check = file ? AA_CHECK_FD : 0;

		if (profile)
			error = aa_perm_xattr(profile, operation, dentry, mnt,
					      mask, check);
		aa_put_profile(profile);
	}

	return error;
}

static int apparmor_inode_setxattr(struct dentry *dentry, struct vfsmount *mnt,
				   char *name, void *value, size_t size,
				   int flags, struct file *file)
{
	return aa_xattr_permission(dentry, mnt, "xattr set", MAY_WRITE, file);
}

static int apparmor_inode_getxattr(struct dentry *dentry, struct vfsmount *mnt,
				   char *name, struct file *file)
{
	return aa_xattr_permission(dentry, mnt, "xattr get", MAY_READ, file);
}

static int apparmor_inode_listxattr(struct dentry *dentry, struct vfsmount *mnt,
				    struct file *file)
{
	return aa_xattr_permission(dentry, mnt, "xattr list", MAY_READ, file);
}

static int apparmor_inode_removexattr(struct dentry *dentry,
				      struct vfsmount *mnt, char *name,
				      struct file *file)
{
	return aa_xattr_permission(dentry, mnt, "xattr remove", MAY_WRITE,
				   file);
}

static int aa_file_permission(const char *op, struct file *file, int mask)
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
	if (profile && (file_profile != profile || mask & AA_MAY_LOCK)) {
		struct dentry *dentry = file->f_dentry;
		struct vfsmount *mnt = file->f_vfsmnt;
		struct inode *inode = dentry->d_inode;
		int check = AA_CHECK_FD;

		/*
		 * FIXME: We should remember which profiles we revalidated
		 *	  against.
		 */
		if (S_ISDIR(inode->i_mode))
			check |= AA_CHECK_DIR;
		error = aa_permission(op, inode, dentry, mnt, mask, check);
	}
	aa_put_profile(profile);

out:
	return error;
}

static int apparmor_file_permission(struct file *file, int mask)
{
	return aa_file_permission("file_permission", file,
				  aa_mask_permissions(mask));
}

static inline int apparmor_file_lock (struct file *file, unsigned int cmd)
{
	int mask = AA_MAY_LOCK;
	if (cmd == F_WRLCK)
		mask |= MAY_WRITE;
	return aa_file_permission("file_lock", file, mask);
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

static inline int aa_mmap(struct file *file, const char *operation,
			  unsigned long prot, unsigned long flags)
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
	return aa_permission(operation, dentry->d_inode, dentry,
			     file->f_vfsmnt, mask, AA_CHECK_FD);
}

static int apparmor_file_mmap(struct file *file, unsigned long reqprot,
			      unsigned long prot, unsigned long flags)
{
	return aa_mmap(file, "file_mmap", prot, flags);
}

static int apparmor_file_mprotect(struct vm_area_struct *vma,
				  unsigned long reqprot, unsigned long prot)
{
	return aa_mmap(vma->vm_file, "file_mprotect", prot,
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

static int apparmor_socket_create(int family, int type, int protocol, int kern)
{
	struct aa_profile *profile;
	int error = 0;

	if (kern)
		return 0;

	profile = aa_get_profile(current);
	if (profile)
		error = aa_net_perm(profile, "socket_create", family,
							type, protocol);
	aa_put_profile(profile);

	return error;
}

static int apparmor_socket_post_create(struct socket * sock, int family,
					int type, int protocol, int kern)
{
	struct sock *sk = sock->sk;

	if (kern)
		return 0;

	return aa_revalidate_sk(sk, "socket_post_create");
}

static int apparmor_socket_bind(struct socket * sock,
				struct sockaddr * address, int addrlen)
{
	struct sock *sk = sock->sk;

	return aa_revalidate_sk(sk, "socket_bind");
}

static int apparmor_socket_connect(struct socket * sock,
					struct sockaddr * address, int addrlen)
{
	struct sock *sk = sock->sk;

	return aa_revalidate_sk(sk, "socket_connect");
}

static int apparmor_socket_listen(struct socket * sock, int backlog)
{
	struct sock *sk = sock->sk;

	return aa_revalidate_sk(sk, "socket_listen");
}

static int apparmor_socket_accept(struct socket * sock, struct socket * newsock)
{
	struct sock *sk = sock->sk;

	return aa_revalidate_sk(sk, "socket_accept");
}

static int apparmor_socket_sendmsg(struct socket * sock,
					struct msghdr * msg, int size)
{
	struct sock *sk = sock->sk;

	return aa_revalidate_sk(sk, "socket_sendmsg");
}

static int apparmor_socket_recvmsg(struct socket * sock,
				   struct msghdr * msg, int size, int flags)
{
	struct sock *sk = sock->sk;

	return aa_revalidate_sk(sk, "socket_recvmsg");
}

static int apparmor_socket_getsockname(struct socket * sock)
{
	struct sock *sk = sock->sk;

	return aa_revalidate_sk(sk, "socket_getsockname");
}

static int apparmor_socket_getpeername(struct socket * sock)
{
	struct sock *sk = sock->sk;

	return aa_revalidate_sk(sk, "socket_getpeername");
}

static int apparmor_socket_getsockopt(struct socket * sock, int level,
					int optname)
{
	struct sock *sk = sock->sk;

	return aa_revalidate_sk(sk, "socket_getsockopt");
}

static int apparmor_socket_setsockopt(struct socket * sock, int level,
					int optname)
{
	struct sock *sk = sock->sk;

	return aa_revalidate_sk(sk, "socket_setsockopt");
}

static int apparmor_socket_shutdown(struct socket * sock, int how)
{
	struct sock *sk = sock->sk;

	return aa_revalidate_sk(sk, "socket_shutdown");
}

static int apparmor_socket_getpeersec_stream(struct socket *sock,
			char __user *optval, int __user *optlen, unsigned len)
{
	struct sock *sk = sock->sk;

	return aa_revalidate_sk(sk, "socket_getpeersec_stream");
}

static int apparmor_socket_getpeersec_dgram(struct socket *sock,
					struct sk_buff *skb, u32 *secid)
{
	struct sock *sk = sock->sk;

	return aa_revalidate_sk(sk, "socket_getpeersec_dgram");
}

static int apparmor_getprocattr(struct task_struct *task, char *name,
				char **value)
{
	unsigned len;
	int error;
	struct aa_profile *profile;

	/* AppArmor only supports the "current" process attribute */
	if (strcmp(name, "current") != 0)
		return -EINVAL;

	/* must be task querying itself or admin */
	if (current != task && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	profile = aa_get_profile(task);
	error = aa_getprocattr(profile, value, &len);
	aa_put_profile(profile);
	if (!error)
		error = len;

	return error;
}

static int apparmor_setprocattr(struct task_struct *task, char *name,
				void *value, size_t size)
{
	char *command, *args;
	int error;

	if (strcmp(name, "current") != 0 || size == 0 || size >= PAGE_SIZE)
		return -EINVAL;
	args = value;
	args[size] = '\0';
	args = strstrip(args);
	command = strsep(&args, " ");
	if (!args)
		return -EINVAL;
	while (isspace(*args))
		args++;
	if (!*args)
		return -EINVAL;

	if (strcmp(command, "changehat") == 0) {
		if (current != task)
			return -EACCES;
		error = aa_setprocattr_changehat(args);
	} else if (strcmp(command, "changeprofile") == 0) {
		if (current != task)
			return -EACCES;
		error = aa_setprocattr_changeprofile(args);
	} else if (strcmp(command, "setprofile") == 0) {
		struct aa_profile *profile;

		/* Only an unconfined process with admin capabilities
		 * may change the profile of another task.
		 */

		if (!capable(CAP_SYS_ADMIN))
			return -EACCES;

		profile = aa_get_profile(current);
		if (profile) {
			struct aa_audit sa;
			memset(&sa, 0, sizeof(sa));
			sa.operation = "profile_set";
			sa.gfp_mask = GFP_KERNEL;
			sa.task = task->pid;
			sa.info = "from confined process";
			aa_audit_reject(profile, &sa);
			aa_put_profile(profile);
			return -EACCES;
		}
		error = aa_setprocattr_setprofile(task, args);
	} else {
		struct aa_audit sa;
		memset(&sa, 0, sizeof(sa));
		sa.operation = "setprocattr";
		sa.gfp_mask = GFP_KERNEL;
		sa.info = "invalid command";
		sa.name = command;
		sa.task = task->pid;
		aa_audit_reject(NULL, &sa);
		return -EINVAL;
	}

	if (!error)
		error = size;
	return error;
}

static void info_message(const char *str, const char *name)
{
       struct aa_audit sa;
       memset(&sa, 0, sizeof(sa));
       sa.gfp_mask = GFP_KERNEL;
       sa.name = name;
       sa.info = str;
       if (name)
	       printk(KERN_INFO "AppArmor: %s name=\"%s\"\n", str, name);
       else
	       printk(KERN_INFO "AppArmor: %s\n", str);
       aa_audit_message(NULL, &sa, AUDIT_APPARMOR_STATUS);
}

int apparmor_register_subsecurity(const char *name,
                                 struct security_operations *ops)
{
       int error = 0;

       if (mutex_lock_interruptible(&aa_secondary_lock))
               return -ERESTARTSYS;

       /* allow dazuko and capability to stack.  The stacking with
        * capability is not needed since apparmor already composes
        * capability using common cap.
        */
       if (!aa_secondary_ops && (strcmp(name, "dazuko") == 0 ||
                                 strcmp(name, "capability") == 0)){
               /* The apparmor module needs to be pinned while a secondary is
                * registered
                */
               if (try_module_get(aa_module)) {
                       aa_secondary_ops = ops;
                       info_message("Registered secondary security module",
                                    name);
               } else {
                       error = -EINVAL;
               }
       } else {
               info_message("Unable to register %s as a secondary security "
                            "module", name);
               error = -EPERM;
       }
       mutex_unlock(&aa_secondary_lock);
       return error;
}

int apparmor_unregister_subsecurity(const char *name,
                                   struct security_operations *ops)
{
       int error = 0;

       if (mutex_lock_interruptible(&aa_secondary_lock))
               return -ERESTARTSYS;

       if (aa_secondary_ops && aa_secondary_ops == ops) {
               rcu_assign_pointer(aa_secondary_ops, NULL);
               synchronize_rcu();
               module_put(aa_module);
               info_message("Unregistered secondary security module", name);
       } else {
               info_message("Unable to unregister secondary security module",
                            name);
               error = -EPERM;
       }
       mutex_unlock(&aa_secondary_lock);
       return error;
}

struct security_operations apparmor_ops = {
	.ptrace =			apparmor_ptrace,
	.capget =			cap_capget,
	.capset_check =			cap_capset_check,
	.capset_set =			cap_capset_set,
	.sysctl =			apparmor_sysctl,
	.capable =			apparmor_capable,
	.syslog =			cap_syslog,

	.bprm_apply_creds =		cap_bprm_apply_creds,
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
	.file_lock =			apparmor_file_lock,

	.task_alloc_security =		apparmor_task_alloc_security,
	.task_free_security =		apparmor_task_free_security,
	.task_post_setuid =		cap_task_post_setuid,
	.task_reparent_to_init =	cap_task_reparent_to_init,

	.getprocattr =			apparmor_getprocattr,
	.setprocattr =			apparmor_setprocattr,

	.register_security =            apparmor_register_subsecurity,
	.unregister_security =          apparmor_unregister_subsecurity,

	.socket_create =		apparmor_socket_create,
	.socket_post_create =		apparmor_socket_post_create,
	.socket_bind =			apparmor_socket_bind,
	.socket_connect =		apparmor_socket_connect,
	.socket_listen =		apparmor_socket_listen,
	.socket_accept =		apparmor_socket_accept,
	.socket_sendmsg =		apparmor_socket_sendmsg,
	.socket_recvmsg =		apparmor_socket_recvmsg,
	.socket_getsockname =		apparmor_socket_getsockname,
	.socket_getpeername =		apparmor_socket_getpeername,
	.socket_getsockopt =		apparmor_socket_getsockopt,
	.socket_setsockopt =		apparmor_socket_setsockopt,
	.socket_shutdown =		apparmor_socket_shutdown,
	.socket_getpeersec_stream =	apparmor_socket_getpeersec_stream,
	.socket_getpeersec_dgram =	apparmor_socket_getpeersec_dgram,
};

static int __init apparmor_init(void)
{
	int error;

	if ((error = create_apparmorfs())) {
		AA_ERROR("Unable to activate AppArmor filesystem\n");
		goto createfs_out;
	}

	if ((error = alloc_null_complain_profile())){
		AA_ERROR("Unable to allocate null complain profile\n");
		goto alloc_out;
	}

	if ((error = register_security(&apparmor_ops))) {
		AA_ERROR("Unable to register AppArmor\n");
		goto register_security_out;
	}

	if (apparmor_complain)
		info_message("AppArmor initialized: complainmode enabled",
			     NULL);
	else
		info_message("AppArmor initialized", NULL);

	return error;

register_security_out:
	free_null_complain_profile();

alloc_out:
	destroy_apparmorfs();

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

	/*
	 * Delay for an rcu cycle to make sure that all active task
	 * context readers have finished, and all profiles have been
	 * freed by their rcu callbacks.
	 */
	synchronize_rcu();

	destroy_apparmorfs();
	mutex_unlock(&aa_interface_lock);

	if (unregister_security(&apparmor_ops))
		info_message("Unable to properly unregister AppArmor", NULL);

	info_message("AppArmor protection removed", NULL);
}

module_init(apparmor_init);
module_exit(apparmor_exit);

MODULE_DESCRIPTION("AppArmor process confinement");
MODULE_AUTHOR("Novell/Immunix, http://bugs.opensuse.org");
MODULE_LICENSE("GPL");
