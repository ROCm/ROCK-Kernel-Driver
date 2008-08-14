/*
 *	Copyright (C) 2002-2007 Novell/SUSE
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 *
 *	AppArmor Core
 */

#include <linux/security.h>
#include <linux/namei.h>
#include <linux/audit.h>
#include <linux/mount.h>
#include <linux/ptrace.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <net/sock.h>

#include "apparmor.h"

#include "inline.h"

/*
 * Table of capability names: we generate it from capabilities.h.
 */
static const char *capability_names[] = {
#include "capability_names.h"
};

struct aa_namespace *default_namespace;

static int aa_inode_mode(struct inode *inode)
{
	/* if the inode doesn't exist the user is creating it */
	if (!inode || current->fsuid == inode->i_uid)
		return AA_USER_SHIFT;
	return AA_OTHER_SHIFT;
}

int alloc_default_namespace(void)
{
	struct aa_namespace *ns;
	char *name = kstrdup("default", GFP_KERNEL);
	if (!name)
		return -ENOMEM;
	ns = alloc_aa_namespace(name);
	if (!ns) {
		kfree(name);
		return -ENOMEM;
	}

	write_lock(&profile_ns_list_lock);
	default_namespace = ns;
	aa_get_namespace(ns);
	list_add(&ns->list, &profile_ns_list);
	write_unlock(&profile_ns_list_lock);

	return 0;
}

void free_default_namespace(void)
{
	write_lock(&profile_ns_list_lock);
	list_del_init(&default_namespace->list);
	write_unlock(&profile_ns_list_lock);
	aa_put_namespace(default_namespace);
	default_namespace = NULL;
}

static void aa_audit_file_sub_mask(struct audit_buffer *ab, char *buffer,
				   int mask)
{
	const char unsafex[] = "upcn";
	const char safex[] = "UPCN";
	char *m = buffer;

	if (mask & AA_EXEC_MMAP)
		*m++ = 'm';
	if (mask & MAY_READ)
		*m++ = 'r';
	if (mask & MAY_WRITE)
		*m++ = 'w';
	else if (mask & MAY_APPEND)
		*m++ = 'a';
	if (mask & MAY_EXEC) {
		int index = AA_EXEC_INDEX(mask);
		/* all indexes > 4 are also named transitions */
		if (index > 4)
			index = 4;
		if (index > 0) {
			if (mask & AA_EXEC_UNSAFE)
				*m++ = unsafex[index - 1];
			else
				*m++ = safex[index - 1];
		}
		if (mask & AA_EXEC_INHERIT)
			*m++ = 'i';
		*m++ = 'x';
	}
	if (mask & AA_MAY_LINK)
		*m++ = 'l';
	if (mask & AA_MAY_LOCK)
		*m++ = 'k';
	*m++ = '\0';
}

static void aa_audit_file_mask(struct audit_buffer *ab, const char *name,
			       int mask)
{
	char user[10], other[10];

	aa_audit_file_sub_mask(ab, user,
			       (mask & AA_USER_PERMS) >> AA_USER_SHIFT);
	aa_audit_file_sub_mask(ab, other,
			       (mask & AA_OTHER_PERMS) >> AA_OTHER_SHIFT);

	audit_log_format(ab, " %s=\"%s::%s\"", name, user, other);
}

static const char *address_families[] = {
#include "af_names.h"
};

static const char *sock_types[] = {
	"unknown(0)",
	"stream",
	"dgram",
	"raw",
	"rdm",
	"seqpacket",
	"dccp",
	"unknown(7)",
	"unknown(8)",
	"unknown(9)",
	"packet",
};

/**
 * aa_audit - Log an audit event to the audit subsystem
 * @profile: profile to check against
 * @sa: audit event
 * @audit_cxt: audit context to log message to
 * @type: audit event number
 */
static int aa_audit_base(struct aa_profile *profile, struct aa_audit *sa,
			 struct audit_context *audit_cxt, int type)
{
	struct audit_buffer *ab = NULL;

	ab = audit_log_start(audit_cxt, sa->gfp_mask, type);

	if (!ab) {
		AA_ERROR("Unable to log event (%d) to audit subsys\n",
			 type);
		 /* don't fail operations in complain mode even if logging
		  * fails */
		return type == AUDIT_APPARMOR_ALLOWED ? 0 : -ENOMEM;
	}

	if (sa->operation)
		audit_log_format(ab, "operation=\"%s\"", sa->operation);

	if (sa->info) {
		audit_log_format(ab, " info=\"%s\"", sa->info);
		if (sa->error_code)
			audit_log_format(ab, " error=%d", sa->error_code);
	}

	if (sa->request_mask)
		aa_audit_file_mask(ab, "requested_mask", sa->request_mask);

	if (sa->denied_mask)
		aa_audit_file_mask(ab, "denied_mask", sa->denied_mask);

	if (sa->request_mask)
		audit_log_format(ab, " fsuid=%d", current->fsuid);

	if (sa->rlimit)
		audit_log_format(ab, " rlimit=%d", sa->rlimit - 1);

	if (sa->iattr) {
		struct iattr *iattr = sa->iattr;

		audit_log_format(ab, " attribute=\"%s%s%s%s%s%s%s\"",
			iattr->ia_valid & ATTR_MODE ? "mode," : "",
			iattr->ia_valid & ATTR_UID ? "uid," : "",
			iattr->ia_valid & ATTR_GID ? "gid," : "",
			iattr->ia_valid & ATTR_SIZE ? "size," : "",
			iattr->ia_valid & (ATTR_ATIME | ATTR_ATIME_SET) ?
				"atime," : "",
			iattr->ia_valid & (ATTR_MTIME | ATTR_MTIME_SET) ?
				"mtime," : "",
			iattr->ia_valid & ATTR_CTIME ? "ctime," : "");
	}

	if (sa->task)
		audit_log_format(ab, " task=%d", sa->task);

	if (sa->parent)
		audit_log_format(ab, " parent=%d", sa->parent);

	if (sa->name) {
		audit_log_format(ab, " name=");
		audit_log_untrustedstring(ab, sa->name);
	}

	if (sa->name2) {
		audit_log_format(ab, " name2=");
		audit_log_untrustedstring(ab, sa->name2);
	}

	if (sa->family || sa->type) {
		if (address_families[sa->family])
			audit_log_format(ab, " family=\"%s\"",
					 address_families[sa->family]);
		else
			audit_log_format(ab, " family=\"unknown(%d)\"",
					 sa->family);

		if (sock_types[sa->type])
			audit_log_format(ab, " sock_type=\"%s\"",
					 sock_types[sa->type]);
		else
			audit_log_format(ab, " sock_type=\"unknown(%d)\"",
					 sa->type);

		audit_log_format(ab, " protocol=%d", sa->protocol);
	}

        audit_log_format(ab, " pid=%d", current->pid);

	if (profile) {
		audit_log_format(ab, " profile=");
		audit_log_untrustedstring(ab, profile->name);

		if (profile->ns != default_namespace) {
			audit_log_format(ab, " namespace=");
			audit_log_untrustedstring(ab, profile->ns->name);
		}
	}

	audit_log_end(ab);

	return type == AUDIT_APPARMOR_ALLOWED ? 0 : sa->error_code;
}

/**
 * aa_audit_syscallreject - Log a syscall rejection to the audit subsystem
 * @profile: profile to check against
 * @gfp: memory allocation flags
 * @msg: string describing syscall being rejected
 */
int aa_audit_syscallreject(struct aa_profile *profile, gfp_t gfp,
			   const char *msg)
{
	struct aa_audit sa;
	memset(&sa, 0, sizeof(sa));
	sa.operation = "syscall";
	sa.name = msg;
	sa.gfp_mask = gfp;
	sa.error_code = -EPERM;

	return aa_audit_base(profile, &sa, current->audit_context,
			     AUDIT_APPARMOR_DENIED);
}

int aa_audit_message(struct aa_profile *profile, struct aa_audit *sa,
		      int type)
{
	struct audit_context *audit_cxt;

	audit_cxt = apparmor_logsyscall ? current->audit_context : NULL;
	return aa_audit_base(profile, sa, audit_cxt, type);
}

void aa_audit_hint(struct aa_profile *profile, struct aa_audit *sa)
{
	aa_audit_message(profile, sa, AUDIT_APPARMOR_HINT);
}

void aa_audit_status(struct aa_profile *profile, struct aa_audit *sa)
{
	aa_audit_message(profile, sa, AUDIT_APPARMOR_STATUS);
}

int aa_audit_reject(struct aa_profile *profile, struct aa_audit *sa)
{
	return aa_audit_message(profile, sa, AUDIT_APPARMOR_DENIED);
}

/**
 * aa_audit - Log an audit event to the audit subsystem
 * @profile: profile to check against
 * @sa: audit event
 */
int aa_audit(struct aa_profile *profile, struct aa_audit *sa)
{
	int type = AUDIT_APPARMOR_DENIED;
	struct audit_context *audit_cxt;

	if (likely(!sa->error_code))
		type = AUDIT_APPARMOR_AUDIT;
	else if (PROFILE_COMPLAIN(profile))
		type = AUDIT_APPARMOR_ALLOWED;

	audit_cxt = apparmor_logsyscall ? current->audit_context : NULL;
	return aa_audit_base(profile, sa, audit_cxt, type);
}

static int aa_audit_file(struct aa_profile *profile, struct aa_audit *sa)
{
	if (likely(!sa->error_code)) {
		int mask = sa->audit_mask & AUDIT_FILE_MASK;

		if (unlikely(PROFILE_AUDIT(profile)))
			mask |= AUDIT_FILE_MASK;

		if (likely(!(sa->request_mask & mask)))
			return 0;

		/* mask off perms that are not being force audited */
		sa->request_mask &= mask | ALL_AA_EXEC_TYPE;
	} else {
		int mask = AUDIT_QUIET_MASK(sa->audit_mask);

		if (!(sa->denied_mask & ~mask))
			return sa->error_code;

		/* mask off perms whose denial is being silenced */
		sa->denied_mask &= (~mask) | ALL_AA_EXEC_TYPE;
	}

	return aa_audit(profile, sa);
}

static int aa_audit_caps(struct aa_profile *profile, struct aa_audit *sa,
			 int cap)
{
	if (likely(!sa->error_code)) {
		if (likely(!PROFILE_AUDIT(profile) &&
			   !cap_raised(profile->audit_caps, cap)))
			return 0;
	}

	/* quieting of capabilities is handled the caps_logged cache */
	return aa_audit(profile, sa);
}

/**
 * aa_file_denied - check for @mask access on a file
 * @profile: profile to check against
 * @name: pathname of file
 * @mask: permission mask requested for file
 * @audit_mask: return audit mask for the match
 *
 * Return %0 on success, or else the permissions in @mask that the
 * profile denies.
 */
static int aa_file_denied(struct aa_profile *profile, const char *name,
			  int mask, int *audit_mask)
{
	return (mask & ~aa_match(profile->file_rules, name, audit_mask));
}

/**
 * aa_link_denied - check for permission to link a file
 * @profile: profile to check against
 * @link: pathname of link being created
 * @target: pathname of target to be linked to
 * @target_mode: UGO shift for target inode
 * @request_mask: the permissions subset valid only if link succeeds
 * @audit_mask: return the audit_mask for the link permission
 * Return %0 on success, or else the permissions that the profile denies.
 */
static int aa_link_denied(struct aa_profile *profile, const char *link,
			  const char *target, int target_mode,
			  int *request_mask, int *audit_mask)
{
	unsigned int state;
	int l_mode, t_mode, l_x, t_x, denied_mask = 0;
	int link_mask = AA_MAY_LINK << target_mode;

	*request_mask = link_mask;

	l_mode = aa_match_state(profile->file_rules, DFA_START, link, &state);

	if (l_mode & link_mask) {
		int mode;
		/* test to see if target can be paired with link */
		state = aa_dfa_null_transition(profile->file_rules, state);
		mode = aa_match_state(profile->file_rules, state, target,
				      &state);

		if (!(mode & link_mask))
			denied_mask |= link_mask;

		*audit_mask = dfa_audit_mask(profile->file_rules, state);

		/* return if link subset test is not required */
		if (!(mode & (AA_LINK_SUBSET_TEST << target_mode)))
			return denied_mask;
	}

	/* Do link perm subset test requiring permission on link are a
	 * subset of the permissions on target.
	 * If a subset test is required a permission subset test of the
	 * perms for the link are done against the user::other of the
	 * target's 'r', 'w', 'x', 'a', 'k', and 'm' permissions.
	 *
	 * If the link has 'x', an exact match of all the execute flags
	 * must match.
 	 */
	denied_mask |= ~l_mode & link_mask;

	t_mode = aa_match(profile->file_rules, target, NULL);

	l_x = l_mode & (ALL_AA_EXEC_TYPE | AA_EXEC_BITS);
	t_x = t_mode & (ALL_AA_EXEC_TYPE | AA_EXEC_BITS);

	/* For actual subset test ignore valid-profile-transition flags,
	 * and link bits
	 */
	l_mode &= AA_FILE_PERMS & ~AA_LINK_BITS;
	t_mode &= AA_FILE_PERMS & ~AA_LINK_BITS;

	*request_mask = l_mode | link_mask;

	if (l_mode) {
		int x = l_x | (t_x & ALL_AA_EXEC_UNSAFE);
		denied_mask |= l_mode & ~t_mode;
		/* mask off x modes not used by link */

		/* handle exec subset
		 * - link safe exec issubset of unsafe exec
		 * - no link x perm is subset of target having x perm
		 */
		if ((l_mode & AA_USER_EXEC) &&
		    (x & AA_USER_EXEC_TYPE) != (t_x & AA_USER_EXEC_TYPE))
			denied_mask = AA_USER_EXEC | (l_x & AA_USER_EXEC_TYPE);
		if ((l_mode & AA_OTHER_EXEC) &&
		    (x & AA_OTHER_EXEC_TYPE) != (t_x & AA_OTHER_EXEC_TYPE))
			denied_mask = AA_OTHER_EXEC | (l_x & AA_OTHER_EXEC_TYPE);
	}

	return denied_mask;
}

/**
 * aa_get_name - compute the pathname of a file
 * @dentry: dentry of the file
 * @mnt: vfsmount of the file
 * @buffer: buffer that aa_get_name() allocated
 * @check: AA_CHECK_DIR is set if the file is a directory
 *
 * Returns a pointer to the beginning of the pathname (which usually differs
 * from the beginning of the buffer), or an error code.
 *
 * We need @check to indicate whether the file is a directory or not because
 * the file may not yet exist, and so we cannot check the inode's file type.
 */
static char *aa_get_name(struct dentry *dentry, struct vfsmount *mnt,
			 char **buffer, int check)
{
	char *name;
	int is_dir, size = 256;

	is_dir = (check & AA_CHECK_DIR) ? 1 : 0;

	for (;;) {
		char *buf = kmalloc(size, GFP_KERNEL);
		if (!buf)
			return ERR_PTR(-ENOMEM);

		name = d_namespace_path(dentry, mnt, buf, size - is_dir);
		if (!IS_ERR(name)) {
			if (name[0] != '/') {
				/*
				 * This dentry is not connected to the
				 * namespace root -- reject access.
				 */
				kfree(buf);
				return ERR_PTR(-ENOENT);
			}
			if (is_dir && name[1] != '\0') {
				/*
				 * Append "/" to the pathname. The root
				 * directory is a special case; it already
				 * ends in slash.
				 */
				buf[size - 2] = '/';
				buf[size - 1] = '\0';
			}

			*buffer = buf;
			return name;
		}
		if (PTR_ERR(name) != -ENAMETOOLONG)
			return name;

		kfree(buf);
		size <<= 1;
		if (size > apparmor_path_max)
			return ERR_PTR(-ENAMETOOLONG);
	}
}

static char *new_compound_name(const char *n1, const char *n2)
{
	char *name = kmalloc(strlen(n1) + strlen(n2) + 3, GFP_KERNEL);
	if (name)
		sprintf(name, "%s//%s", n1, n2);
	return name;
}
static inline void aa_put_name_buffer(char *buffer)
{
	kfree(buffer);
}

/**
 * aa_perm_dentry - check if @profile allows @mask for a file
 * @profile: profile to check against
 * @dentry: dentry of the file
 * @mnt: vfsmount o the file
 * @sa: audit context
 * @mask: requested profile permissions
 * @check: kind of check to perform
 *
 * Returns 0 upon success, or else an error code.
 *
 * @check indicates the file type, and whether the file was accessed through
 * an open file descriptor (AA_CHECK_FD) or not.
 */
static int aa_perm_dentry(struct aa_profile *profile, struct dentry *dentry,
			  struct vfsmount *mnt, struct aa_audit *sa, int check)
{
	int error;
	char *buffer = NULL;

	sa->name = aa_get_name(dentry, mnt, &buffer, check);
	sa->request_mask <<= aa_inode_mode(dentry->d_inode);
	if (IS_ERR(sa->name)) {
		/*
		 * deleted files are given a pass on permission checks when
		 * accessed through a file descriptor.
		 */
		if (PTR_ERR(sa->name) == -ENOENT && (check & AA_CHECK_FD))
			sa->denied_mask = 0;
		else {
			sa->denied_mask = sa->request_mask;
			sa->error_code = PTR_ERR(sa->name);
			if (sa->error_code == -ENOENT)
				sa->info = "Failed name resolution - object not a valid entry";
			else if (sa->error_code == -ENAMETOOLONG)
				sa->info = "Failed name resolution - name too long";
			else
				sa->info = "Failed name resolution";
		}
		sa->name = NULL;
	} else
		sa->denied_mask = aa_file_denied(profile, sa->name,
						 sa->request_mask,
						 &sa->audit_mask);

	if (!sa->denied_mask)
		sa->error_code = 0;

	error = aa_audit_file(profile, sa);
	aa_put_name_buffer(buffer);

	return error;
}

/**
 * aa_attr - check if attribute change is allowed
 * @profile: profile to check against
 * @dentry: dentry of the file to check
 * @mnt: vfsmount of the file to check
 * @iattr: attribute changes requested
 */
int aa_attr(struct aa_profile *profile, struct dentry *dentry,
	    struct vfsmount *mnt, struct iattr *iattr)
{
	struct inode *inode = dentry->d_inode;
	int error, check;
	struct aa_audit sa;

	memset(&sa, 0, sizeof(sa));
	sa.operation = "setattr";
	sa.gfp_mask = GFP_KERNEL;
	sa.iattr = iattr;
	sa.request_mask = MAY_WRITE;
	sa.error_code = -EACCES;

	check = 0;
	if (inode && S_ISDIR(inode->i_mode))
		check |= AA_CHECK_DIR;
	if (iattr->ia_valid & ATTR_FILE)
		check |= AA_CHECK_FD;

	error = aa_perm_dentry(profile, dentry, mnt, &sa, check);

	return error;
}

/**
 * aa_perm_xattr - check if xattr attribute change is allowed
 * @profile: profile to check against
 * @dentry: dentry of the file to check
 * @mnt: vfsmount of the file to check
 * @operation: xattr operation being done
 * @mask: access mode requested
 * @check: kind of check to perform
 */
int aa_perm_xattr(struct aa_profile *profile, const char *operation,
		  struct dentry *dentry, struct vfsmount *mnt, int mask,
		  int check)
{
	struct inode *inode = dentry->d_inode;
	int error;
	struct aa_audit sa;

	memset(&sa, 0, sizeof(sa));
	sa.operation = operation;
	sa.gfp_mask = GFP_KERNEL;
	sa.request_mask = mask;
	sa.error_code = -EACCES;

	if (inode && S_ISDIR(inode->i_mode))
		check |= AA_CHECK_DIR;

	error = aa_perm_dentry(profile, dentry, mnt, &sa, check);

	return error;
}

/**
 * aa_perm - basic apparmor permissions check
 * @profile: profile to check against
 * @dentry: dentry of the file to check
 * @mnt: vfsmount of the file to check
 * @mask: access mode requested
 * @check: kind of check to perform
 *
 * Determine if access @mask for the file is authorized by @profile.
 * Returns 0 on success, or else an error code.
 */
int aa_perm(struct aa_profile *profile, const char *operation,
	    struct dentry *dentry, struct vfsmount *mnt, int mask, int check)
{
	struct aa_audit sa;
	int error = 0;

	if (mask == 0)
		goto out;

	memset(&sa, 0, sizeof(sa));
	sa.operation = operation;
	sa.gfp_mask = GFP_KERNEL;
	sa.request_mask = mask;
	sa.error_code = -EACCES;

	error = aa_perm_dentry(profile, dentry, mnt, &sa, check);

out:
	return error;
}

/**
 * aa_perm_dir
 * @profile: profile to check against
 * @dentry: dentry of directory to check
 * @mnt: vfsmount of directory to check
 * @operation: directory operation being performed
 * @mask: access mode requested
 *
 * Determine if directory operation (make/remove) for dentry is authorized
 * by @profile.
 * Returns 0 on success, or else an error code.
 */
int aa_perm_dir(struct aa_profile *profile, const char *operation,
		struct dentry *dentry, struct vfsmount *mnt, int mask)
{
	struct aa_audit sa;

	memset(&sa, 0, sizeof(sa));
	sa.operation = operation;
	sa.gfp_mask = GFP_KERNEL;
	sa.request_mask = mask;
	sa.error_code = -EACCES;

	return aa_perm_dentry(profile, dentry, mnt, &sa, AA_CHECK_DIR);
}

int aa_perm_path(struct aa_profile *profile, const char *operation,
		 const char *name, int mask, uid_t uid)
{
	struct aa_audit sa;

	memset(&sa, 0, sizeof(sa));
	sa.operation = operation;
	sa.gfp_mask = GFP_KERNEL;
	sa.request_mask = mask;
	sa.name = name;
	if (current->fsuid == uid)
		sa.request_mask = mask << AA_USER_SHIFT;
	else
		sa.request_mask = mask << AA_OTHER_SHIFT;

	sa.denied_mask = aa_file_denied(profile, name, sa.request_mask,
					&sa.audit_mask) ;
	sa.error_code = sa.denied_mask ? -EACCES : 0;

	return aa_audit_file(profile, &sa);
}

/**
 * aa_capability - test permission to use capability
 * @cxt: aa_task_context with profile to check against
 * @cap: capability to be tested
 *
 * Look up capability in profile capability set.
 * Returns 0 on success, or else an error code.
 */
int aa_capability(struct aa_task_context *cxt, int cap)
{
	int error = cap_raised(cxt->profile->capabilities, cap) ? 0 : -EPERM;
	struct aa_audit sa;

	/* test if cap has alread been logged */
	if (cap_raised(cxt->caps_logged, cap)) {
		if (PROFILE_COMPLAIN(cxt->profile))
			error = 0;
		return error;
	} else
		/* don't worry about rcu replacement of the cxt here.
		 * caps_logged is a cache to reduce the occurence of
		 * duplicate messages in the log.  The worst that can
		 * happen is duplicate capability messages shows up in
		 * the audit log
		 */
		cap_raise(cxt->caps_logged, cap);

	memset(&sa, 0, sizeof(sa));
	sa.operation = "capable";
	sa.gfp_mask = GFP_ATOMIC;
	sa.name = capability_names[cap];
	sa.error_code = error;

	error = aa_audit_caps(cxt->profile, &sa, cap);

	return error;
}

/* must be used inside rcu_read_lock or task_lock */
int aa_may_ptrace(struct aa_task_context *cxt, struct aa_profile *tracee)
{
	if (!cxt || cxt->profile == tracee)
		return 0;
	return aa_capability(cxt, CAP_SYS_PTRACE);
}

/**
 * aa_link - hard link check
 * @profile: profile to check against
 * @link: dentry of link being created
 * @link_mnt: vfsmount of link being created
 * @target: dentry of link target
 * @target_mnt: vfsmunt of link target
 *
 * Returns 0 on success, or else an error code.
 */
int aa_link(struct aa_profile *profile,
	    struct dentry *link, struct vfsmount *link_mnt,
	    struct dentry *target, struct vfsmount *target_mnt)
{
	int error;
	struct aa_audit sa;
	char *buffer = NULL, *buffer2 = NULL;

	memset(&sa, 0, sizeof(sa));
	sa.operation = "inode_link";
	sa.gfp_mask = GFP_KERNEL;
	sa.name = aa_get_name(link, link_mnt, &buffer, 0);
	sa.name2 = aa_get_name(target, target_mnt, &buffer2, 0);

	if (IS_ERR(sa.name)) {
		sa.error_code = PTR_ERR(sa.name);
		sa.name = NULL;
	}
	if (IS_ERR(sa.name2)) {
		sa.error_code = PTR_ERR(sa.name2);
		sa.name2 = NULL;
	}

	if (sa.name && sa.name2) {
		sa.denied_mask = aa_link_denied(profile, sa.name, sa.name2,
						aa_inode_mode(target->d_inode),
						&sa.request_mask,
						&sa.audit_mask);
		sa.error_code = sa.denied_mask ? -EACCES : 0;
	}

	error = aa_audit_file(profile, &sa);

	aa_put_name_buffer(buffer);
	aa_put_name_buffer(buffer2);

	return error;
}

int aa_net_perm(struct aa_profile *profile, char *operation,
		int family, int type, int protocol)
{
	struct aa_audit sa;
	int error = 0;
	u16 family_mask, audit_mask, quiet_mask;

	if ((family < 0) || (family >= AF_MAX))
		return -EINVAL;

	if ((type < 0) || (type >= SOCK_MAX))
		return -EINVAL;

	/* unix domain and netlink sockets are handled by ipc */
	if (family == AF_UNIX || family == AF_NETLINK)
		return 0;

	family_mask = profile->network_families[family];
	audit_mask = profile->audit_network[family];
	quiet_mask = profile->quiet_network[family];

	error = (family_mask & (1 << type)) ? 0 : -EACCES;

	memset(&sa, 0, sizeof(sa));
	sa.operation = operation;
	sa.gfp_mask = GFP_KERNEL;
	sa.family = family;
	sa.type = type;
	sa.protocol = protocol;
	sa.error_code = error;

	if (likely(!error)) {
		if (!PROFILE_AUDIT(profile) && !(family_mask & audit_mask))
			return 0;
	} else if (!((1 << type) & ~quiet_mask)) {
		return error;
	}

	error = aa_audit(profile, &sa);

	return error;
}

int aa_revalidate_sk(struct sock *sk, char *operation)
{
	struct aa_profile *profile;
	int error = 0;

	/* this is some debugging code to flush out the network hooks that
	   that are called in interrupt context */
	if (in_interrupt()) {
		printk("AppArmor Debug: Hook being called from interrupt context\n");
		dump_stack();
		return 0;
	}

	profile = aa_get_profile(current);
	if (profile)
		error = aa_net_perm(profile, operation,
				    sk->sk_family, sk->sk_type,
				    sk->sk_protocol);
	aa_put_profile(profile);

	return error;
}
/**
 * aa_task_setrlimit - test permission to set an rlimit
 * @profile - profile confining the task
 * @resource - the resource being set
 * @new_rlim - the new resource limit
 *
 * Control raising the processes hard limit.
 */
int aa_task_setrlimit(struct aa_profile *profile, unsigned int resource,
		      struct rlimit *new_rlim)
{
	struct aa_audit sa;
	int error = 0;

	memset(&sa, 0, sizeof(sa));
	sa.operation = "setrlimit";
	sa.gfp_mask = GFP_KERNEL;
	sa.rlimit = resource + 1;

	if (profile->rlimits.mask & (1 << resource) &&
	    new_rlim->rlim_max > profile->rlimits.limits[resource].rlim_max) {
		sa.error_code = -EACCES;

		error = aa_audit(profile, &sa);
	}

	return error;
}

static int aa_rlimit_nproc(struct aa_profile *profile) {
	if (profile && (profile->rlimits.mask & (1 << RLIMIT_NPROC)) &&
	    profile->task_count >= profile->rlimits.limits[RLIMIT_NPROC].rlim_max)
		return -EAGAIN;
	return 0;
}

void aa_set_rlimits(struct task_struct *task, struct aa_profile *profile)
{
	int i, mask;

	if (!profile)
		return;

	if (!profile->rlimits.mask)
		return;

	task_lock(task->group_leader);
	mask = 1;
	for (i = 0; i < RLIM_NLIMITS; i++, mask <<= 1) {
		struct rlimit new_rlim, *old_rlim;

		/* check to see if NPROC which is per profile and handled
		 * in clone/exec or whether this is a limit to be set
		 * can't set cpu limit either right now
		 */
		if (i == RLIMIT_NPROC || i == RLIMIT_CPU)
			continue;

		old_rlim = task->signal->rlim + i;
		new_rlim = *old_rlim;

		if (mask & profile->rlimits.mask &&
		    profile->rlimits.limits[i].rlim_max < new_rlim.rlim_max) {
			new_rlim.rlim_max = profile->rlimits.limits[i].rlim_max;
			/* soft limit should not exceed hard limit */
			if (new_rlim.rlim_cur > new_rlim.rlim_max)
				new_rlim.rlim_cur = new_rlim.rlim_max;
		}

		*old_rlim = new_rlim;
	}
	task_unlock(task->group_leader);
}

/*******************************
 * Global task related functions
 *******************************/

/**
 * aa_clone - initialize the task context for a new task
 * @child: task that is being created
 *
 * Returns 0 on success, or else an error code.
 */
int aa_clone(struct task_struct *child)
{
	struct aa_audit sa;
	struct aa_task_context *cxt, *child_cxt;
	struct aa_profile *profile;

	if (!aa_task_context(current))
		return 0;
	child_cxt = aa_alloc_task_context(GFP_KERNEL);
	if (!child_cxt)
		return -ENOMEM;

	memset(&sa, 0, sizeof(sa));
	sa.operation = "clone";
	sa.task = child->pid;
	sa.gfp_mask = GFP_KERNEL;

repeat:
	profile = aa_get_profile(current);
	if (profile) {
		lock_profile(profile);
		cxt = aa_task_context(current);
		if (unlikely(profile->isstale || !cxt ||
			     cxt->profile != profile)) {
			/**
			 * Race with profile replacement or removal, or with
			 * task context removal.
			 */
			unlock_profile(profile);
			aa_put_profile(profile);
			goto repeat;
		}

		if (aa_rlimit_nproc(profile)) {
			sa.info = "rlimit nproc limit exceeded";
			unlock_profile(profile);
			aa_audit_reject(profile, &sa);
			aa_put_profile(profile);
			return -EAGAIN;
		}

		/* No need to grab the child's task lock here. */
		aa_change_task_context(child, child_cxt, profile,
				       cxt->cookie, cxt->previous_profile);

		unlock_profile(profile);

		if (APPARMOR_COMPLAIN(child_cxt) &&
		    profile == profile->ns->null_complain_profile) {
			aa_audit_hint(profile, &sa);
		}
		aa_put_profile(profile);
	} else
		aa_free_task_context(child_cxt);

	return 0;
}

static struct aa_profile *
aa_register_find(struct aa_profile *profile, const char* ns_name,
		 const char *name, int mandatory, int complain,
		 struct aa_audit *sa)
{
	struct aa_namespace *ns;
	struct aa_profile *new_profile;
	int ns_ref = 0;

	if (profile)
		ns = profile->ns;
	else
		ns = default_namespace;

	if (ns_name) {
		/* locate the profile namespace */
		ns = aa_find_namespace(ns_name);
		if (!ns) {
			if (mandatory) {
				sa->info = "profile namespace not found";
				sa->denied_mask = sa->request_mask;
				sa->error_code = -ENOENT;
				return ERR_PTR(-ENOENT);
			} else {
				return NULL;
			}
		}
		ns_ref++;
	}

	/* Locate new profile */
	new_profile = aa_find_profile(ns, name);

	if (new_profile) {
		AA_DEBUG("%s: setting profile %s\n",
			 __FUNCTION__, new_profile->name);
	} else if (mandatory && profile) {
		sa->info = "mandatory profile missing";
		sa->denied_mask = sa->request_mask;	/* shifted MAY_EXEC */
		if (complain) {
			aa_audit_hint(profile, sa);
			new_profile =
			    aa_dup_profile(profile->ns->null_complain_profile);
		} else {
			sa->error_code = -EACCES;
			if (ns_ref)
				aa_put_namespace(ns);
			return ERR_PTR(-EACCES);
		}
	} else {
		/* Only way we can get into this code is if task
		 * is unconfined, pix, nix.
		 */
		AA_DEBUG("%s: No profile found for exec image '%s'\n",
			 __FUNCTION__,
			 name);
	}
	if (ns_ref)
		aa_put_namespace(ns);
	return new_profile;
}

static struct aa_profile *
aa_x_to_profile(struct aa_profile *profile, const char *filename, int xmode,
		struct aa_audit *sa, char **child)
{
	struct aa_profile *new_profile = NULL;
	int ix = xmode & AA_EXEC_INHERIT;
	int complain = PROFILE_COMPLAIN(profile);
	int index;

	*child = NULL;
	switch (xmode & AA_EXEC_MODIFIERS) {
	case 0:
		/* only valid with ix flag */
		ix = 1;
		break;
	case AA_EXEC_UNCONFINED:
		/* only valid without ix flag */
		ix = 0;
		break;
	case AA_EXEC_PROFILE:
		new_profile = aa_register_find(profile, NULL, filename, !ix,
					       complain, sa);
		break;
	case AA_EXEC_CHILD:
		*child = new_compound_name(profile->name, filename);
		sa->name2 = *child;
		if (!*child) {
			sa->info = "Failed name resolution - exec failed";
			sa->error_code = -ENOMEM;
			new_profile = ERR_PTR(-ENOMEM);
		} else {
			new_profile = aa_register_find(profile, NULL, *child,
						       !ix, complain, sa);
		}
		break;
	default:
		/* all other indexes are named transitions */
		index = AA_EXEC_INDEX(xmode);
		if (index - 4 > profile->exec_table_size) {
			sa->info = "invalid named transition - exec failed";
			sa->error_code = -EACCES;
			new_profile = ERR_PTR(-EACCES);
		} else {
			char *ns_name = NULL;
			char *name = profile->exec_table[index - 4];
			if (*name == ':') {
				ns_name = name + 1;
				name = ns_name + strlen(ns_name) + 1;
			}
			sa->name2 = name;
			sa->name3 = ns_name;
			new_profile =
				aa_register_find(profile, ns_name, name,
						 !ix, complain, sa);
		}
	}
	if (IS_ERR(new_profile))
		/* all these failures must be audited - no quieting */
		return ERR_PTR(aa_audit_reject(profile, sa));
	return new_profile;
}

/**
 * aa_register - register a new program
 * @bprm: binprm of program being registered
 *
 * Try to register a new program during execve().  This should give the
 * new program a valid aa_task_context if confined.
 */
int aa_register(struct linux_binprm *bprm)
{
	const char *filename;
	char  *buffer = NULL, *child = NULL;
	struct file *filp = bprm->file;
	struct aa_profile *profile, *old_profile, *new_profile = NULL;
	int exec_mode, complain = 0, shift;
	struct aa_audit sa;

	AA_DEBUG("%s\n", __FUNCTION__);

	profile = aa_get_profile(current);

	shift = aa_inode_mode(filp->f_dentry->d_inode);
	memset(&sa, 0, sizeof(sa));
	sa.operation = "exec";
	sa.gfp_mask = GFP_KERNEL;
	sa.request_mask = MAY_EXEC << shift;

	filename = aa_get_name(filp->f_dentry, filp->f_vfsmnt, &buffer, 0);
	if (IS_ERR(filename)) {
		if (profile) {
			sa.info = "Failed name resolution - exec failed";
			sa.error_code = PTR_ERR(filename);
			aa_audit_file(profile, &sa);
			return sa.error_code;
		} else
			return 0;
	}
	sa.name = filename;

	exec_mode = AA_EXEC_UNSAFE << shift;

repeat:
	if (profile) {
		complain = PROFILE_COMPLAIN(profile);

		/* Confined task, determine what mode inherit, unconfined or
		 * mandatory to load new profile
		 */
		exec_mode = aa_match(profile->file_rules, filename,
				     &sa.audit_mask);


		if (exec_mode & sa.request_mask) {
			int xm = exec_mode >> shift;
			new_profile = aa_x_to_profile(profile, filename,
						      xm, &sa, &child);

			if (!new_profile && (xm & AA_EXEC_INHERIT))
				/* (p|c|n|)ix - don't change profile */
				goto cleanup;
			/* error case caught below */

		} else if (sa.request_mask & AUDIT_QUIET_MASK(sa.audit_mask)) {
			/* quiet failed exit */
			new_profile = ERR_PTR(-EACCES);
		} else if (complain) {
			/* There was no entry in calling profile
			 * describing mode to execute image in.
			 * Drop into null-profile (disabling secure exec).
			 */
			new_profile =
			    aa_dup_profile(profile->ns->null_complain_profile);
			exec_mode |= AA_EXEC_UNSAFE << shift;
		} else {
			sa.denied_mask = sa.request_mask;
			sa.error_code = -EACCES;
			new_profile = ERR_PTR(aa_audit_file(profile, &sa));
		}
	} else {
		/* Unconfined task, load profile if it exists */
		new_profile = aa_register_find(NULL, NULL, filename, 0, 0, &sa);
		if (new_profile == NULL)
			goto cleanup;
	}

	if (IS_ERR(new_profile))
		goto cleanup;

	old_profile = __aa_replace_profile(current, new_profile);
	if (IS_ERR(old_profile)) {
		aa_put_profile(new_profile);
		aa_put_profile(profile);
		if (PTR_ERR(old_profile) == -ESTALE) {
			profile = aa_get_profile(current);
			goto repeat;
		}
		if (PTR_ERR(old_profile) == -EPERM) {
			sa.denied_mask = sa.request_mask;
			sa.info = "unable to set profile due to ptrace";
			sa.task = current->parent->pid;
			aa_audit_reject(profile, &sa);
		}
		if (PTR_ERR(old_profile) == -EAGAIN) {
			sa.info = "rlimit nproc limit exceeded";
			aa_audit_reject(profile, &sa);
		}
		new_profile = old_profile;
		goto cleanup;
	}
	aa_put_profile(old_profile);
	aa_put_profile(profile);

	/* Handle confined exec.
	 * Can be at this point for the following reasons:
	 * 1. unconfined switching to confined
	 * 2. confined switching to different confinement
	 * 3. confined switching to unconfined
	 *
	 * Cases 2 and 3 are marked as requiring secure exec
	 * (unless policy specified "unsafe exec")
	 */
	if (!(exec_mode & (AA_EXEC_UNSAFE << shift))) {
		unsigned long bprm_flags;

		bprm_flags = AA_SECURE_EXEC_NEEDED;
		bprm->security = (void*)
			((unsigned long)bprm->security | bprm_flags);
	}

	if (complain && new_profile &&
	    new_profile == new_profile->ns->null_complain_profile) {
		sa.request_mask = 0;
		sa.name = NULL;
		sa.info = "set profile";
		aa_audit_hint(new_profile, &sa);
	}

cleanup:
	aa_put_name_buffer(child);
	aa_put_name_buffer(buffer);
	if (IS_ERR(new_profile))
		return PTR_ERR(new_profile);
	aa_put_profile(new_profile);
	return 0;
}

/**
 * aa_release - release a task context
 * @task: task being released
 *
 * This is called after a task has exited and the parent has reaped it.
 */
void aa_release(struct task_struct *task)
{
	struct aa_task_context *cxt;
	struct aa_profile *profile;
	/*
	 * While the task context is still on a profile's task context
	 * list, another process could replace the profile under us,
	 * leaving us with a locked profile that is no longer attached
	 * to this task. So after locking the profile, we check that
	 * the profile is still attached.  The profile lock is
	 * sufficient to prevent the replacement race so we do not lock
	 * the task.
	 *
	 * Use lock subtyping to avoid lockdep reporting a false irq
	 * possible inversion between the task_lock and profile_lock
	 *
	 * We also avoid taking the task_lock here because lock_dep
	 * would report another false {softirq-on-W} potential irq_lock
	 * inversion.
	 *
	 * If the task does not have a profile attached we are safe;
	 * nothing can race with us at this point.
	 */

repeat:
	profile = aa_get_profile(task);
	if (profile) {
		lock_profile_nested(profile, aa_lock_task_release);
		cxt = aa_task_context(task);
		if (unlikely(!cxt || cxt->profile != profile)) {
			unlock_profile(profile);
			aa_put_profile(profile);
			goto repeat;
		}
		aa_change_task_context(task, NULL, NULL, 0, NULL);
		unlock_profile(profile);
		aa_put_profile(profile);
	}
}

static int do_change_profile(struct aa_profile *expected,
			     struct aa_namespace *ns, const char *name,
			     u64 cookie, int restore, int hat,
			     struct aa_audit *sa)
{
	struct aa_profile *new_profile = NULL, *old_profile = NULL,
		*previous_profile = NULL;
	struct aa_task_context *new_cxt, *cxt;
	int error = 0;

	sa->name = name;

	new_cxt = aa_alloc_task_context(GFP_KERNEL);
	if (!new_cxt)
		return -ENOMEM;

	new_profile = aa_find_profile(ns, name);
	if (!new_profile && !restore) {
		if (!PROFILE_COMPLAIN(expected)) {
			aa_free_task_context(new_cxt);
			return -ENOENT;
		}
		new_profile = aa_dup_profile(ns->null_complain_profile);
	} else if (new_profile && hat && !PROFILE_IS_HAT(new_profile)) {
		aa_free_task_context(new_cxt);
		aa_put_profile(new_profile);
		return error;
	}

	cxt = lock_task_and_profiles(current, new_profile);
	if (!cxt) {
		error = -EPERM;
		goto out;
	}
	old_profile = cxt->profile;

	if (cxt->profile != expected || (new_profile && new_profile->isstale)) {
		error = -ESTALE;
		goto out;
	}

	if (cxt->previous_profile) {
		if (cxt->cookie != cookie) {
			error = -EACCES;
			sa->info = "killing process";
			aa_audit_reject(cxt->profile, sa);
			/* terminate process */
			(void)send_sig_info(SIGKILL, NULL, current);
			goto out;
		}

		if (!restore)
			previous_profile = cxt->previous_profile;
	} else
		previous_profile = cxt->profile;

	if ((current->ptrace & PT_PTRACED) && aa_may_ptrace(cxt, new_profile)) {
		error = -EACCES;
		goto out;
	}

	if ((error = aa_rlimit_nproc(new_profile))) {
		sa->info = "rlimit nproc limit exceeded";
		aa_audit_reject(cxt->profile, sa);
		goto out;
	}

	if (new_profile == ns->null_complain_profile)
		aa_audit_hint(cxt->profile, sa);

	if (APPARMOR_AUDIT(cxt))
		aa_audit_message(cxt->profile, sa, AUDIT_APPARMOR_AUDIT);

	if (!restore && cookie)
		aa_change_task_context(current, new_cxt, new_profile, cookie,
				       previous_profile);
	else
		/* either return to previous_profile, or a permanent change */
		aa_change_task_context(current, new_cxt, new_profile, 0, NULL);

out:
	if (aa_task_context(current) != new_cxt)
		aa_free_task_context(new_cxt);
	task_unlock(current);
	unlock_both_profiles(old_profile, new_profile);
	aa_put_profile(new_profile);
	return error;
}

/**
 * aa_change_profile - perform a one-way profile transition
 * @ns_name: name of the profile namespace to change to
 * @name: name of profile to change to
 * Change to new profile @name.  Unlike with hats, there is no way
 * to change back.
 *
 * Returns %0 on success, error otherwise.
 */
int aa_change_profile(const char *ns_name, const char *name)
{
	struct aa_task_context *cxt;
	struct aa_profile *profile = NULL;
	struct aa_namespace *ns = NULL;
	struct aa_audit sa;
	unsigned int state;
	int error = -EINVAL;

	if (!name)
		return -EINVAL;

	memset(&sa, 0, sizeof(sa));
	sa.gfp_mask = GFP_ATOMIC;
	sa.operation = "change_profile";

repeat:
	task_lock(current);
	cxt = aa_task_context(current);
	if (cxt)
		profile = aa_dup_profile(cxt->profile);
	task_unlock(current);

	if (ns_name)
		ns = aa_find_namespace(ns_name);
	else if (profile)
		ns = aa_get_namespace(profile->ns);
	else
		ns = aa_get_namespace(default_namespace);

	if (!ns) {
		aa_put_profile(profile);
		return -ENOENT;
	}

	if (!profile || PROFILE_COMPLAIN(profile) ||
	    (ns == profile->ns &&
	     (aa_match(profile->file_rules, name, NULL) & AA_CHANGE_PROFILE)))
		error = do_change_profile(profile, ns, name, 0, 0, 0, &sa);
	else {
		/* check for a rule with a namespace prepended */
		aa_match_state(profile->file_rules, DFA_START, ns->name,
			       &state);
		state = aa_dfa_null_transition(profile->file_rules, state);
		if ((aa_match_state(profile->file_rules, state, name, NULL) &
		      AA_CHANGE_PROFILE))
			error = do_change_profile(profile, ns, name, 0, 0, 0,
						  &sa);
		else
			/* no permission to transition to profile @name */
			error = -EACCES;
	}

	aa_put_namespace(ns);
	aa_put_profile(profile);
	if (error == -ESTALE)
		goto repeat;

	return error;
}

/**
 * aa_change_hat - change hat to/from subprofile
 * @hat_name: hat to change to
 * @cookie: magic value to validate the hat change
 *
 * Change to new @hat_name, and store the @hat_magic in the current task
 * context.  If the new @hat_name is %NULL and the @cookie matches that
 * stored in the current task context and is not 0, return to the top level
 * profile.
 * Returns %0 on success, error otherwise.
 */
int aa_change_hat(const char *hat_name, u64 cookie)
{
	struct aa_task_context *cxt;
	struct aa_profile *profile, *previous_profile;
	struct aa_audit sa;
	int error = 0;

	memset(&sa, 0, sizeof(sa));
	sa.gfp_mask = GFP_ATOMIC;
	sa.operation = "change_hat";

repeat:
	task_lock(current);
	cxt = aa_task_context(current);
	if (!cxt) {
		task_unlock(current);
		return -EPERM;
	}
	profile = aa_dup_profile(cxt->profile);
	previous_profile = aa_dup_profile(cxt->previous_profile);
	task_unlock(current);

	if (hat_name) {
		char *name, *profile_name;

		if (previous_profile)
			profile_name = previous_profile->name;
		else
			profile_name = profile->name;

		name = new_compound_name(profile_name, hat_name);
		if (!name) {
			error = -ENOMEM;
			goto out;
		}
		error = do_change_profile(profile, profile->ns, name, cookie,
					  0, 1, &sa);
		aa_put_name_buffer(name);
	} else if (previous_profile)
		error = do_change_profile(profile, profile->ns,
					  previous_profile->name, cookie, 1, 0,
					  &sa);
	/* else ignore restores when there is no saved profile */

out:
	aa_put_profile(previous_profile);
	aa_put_profile(profile);
	if (error == -ESTALE)
		goto repeat;

	return error;
}

/**
 * __aa_replace_profile - replace a task's profile
 * @task: task to switch the profile of
 * @profile: profile to switch to
 *
 * Returns a handle to the previous profile upon success, or else an
 * error code.
 */
struct aa_profile *__aa_replace_profile(struct task_struct *task,
					struct aa_profile *profile)
{
	struct aa_task_context *cxt, *new_cxt = NULL;
	struct aa_profile *old_profile = NULL;

	if (profile) {
		new_cxt = aa_alloc_task_context(GFP_KERNEL);
		if (!new_cxt)
			return ERR_PTR(-ENOMEM);
	}

	cxt = lock_task_and_profiles(task, profile);
	if (unlikely(profile && profile->isstale)) {
		old_profile = ERR_PTR(-ESTALE);
		goto error;
	}

	if ((current->ptrace & PT_PTRACED) && aa_may_ptrace(cxt, profile)) {
		old_profile = ERR_PTR(-EPERM);
		goto error;
	}

	if (aa_rlimit_nproc(profile)) {
		old_profile = ERR_PTR(-EAGAIN);
		goto error;
	}

	if (cxt)
		old_profile = aa_dup_profile(cxt->profile);
	aa_change_task_context(task, new_cxt, profile, 0, NULL);

	task_unlock(task);
	aa_set_rlimits(task, profile);
	unlock_both_profiles(profile, old_profile);
	return old_profile;

error:
	task_unlock(task);
	unlock_both_profiles(profile, cxt ? cxt->profile : NULL);
	aa_free_task_context(new_cxt);
	return old_profile;
}

/**
 * lock_task_and_profiles - lock the task and confining profiles and @profile
 * @task: task to lock
 * @profile: extra profile to lock in addition to the current profile
 *
 * Handle the spinning on locking to make sure the task context and
 * profile are consistent once all locks are aquired.
 *
 * return the aa_task_context currently confining the task.  The task lock
 * will be held whether or not the task is confined.
 */
struct aa_task_context *
lock_task_and_profiles(struct task_struct *task, struct aa_profile *profile)
{
	struct aa_task_context *cxt;
	struct aa_profile *old_profile = NULL;

	rcu_read_lock();
repeat:
	cxt = aa_task_context(task);
	if (cxt)
		old_profile = cxt->profile;

	lock_both_profiles(profile, old_profile);
	task_lock(task);

	/* check for race with profile transition, replacement or removal */
	if (unlikely(cxt != aa_task_context(task))) {
		task_unlock(task);
		unlock_both_profiles(profile, old_profile);
		old_profile = NULL;
		goto repeat;
	}
	rcu_read_unlock();
	return cxt;
}

static void free_aa_task_context_rcu_callback(struct rcu_head *head)
{
	struct aa_task_context *cxt;

	cxt = container_of(head, struct aa_task_context, rcu);
	aa_free_task_context(cxt);
}

/**
 * aa_change_task_context - switch a task to use a new context and profile
 * @task: task that is having its task context changed
 * @new_cxt: new task context to use after the switch
 * @profile: new profile to use after the switch
 * @cookie: magic value to switch to
 * @previous_profile: profile the task can return to
 */
void aa_change_task_context(struct task_struct *task,
			    struct aa_task_context *new_cxt,
			    struct aa_profile *profile, u64 cookie,
			    struct aa_profile *previous_profile)
{
	struct aa_task_context *old_cxt = aa_task_context(task);

	if (old_cxt) {
		list_del_init(&old_cxt->list);
		old_cxt->profile->task_count--;
		call_rcu(&old_cxt->rcu, free_aa_task_context_rcu_callback);
	}
	if (new_cxt) {
		/* set the caps_logged cache to the quiet_caps mask
		 * this has the effect of quieting caps that are not
		 * supposed to be logged
		 */
		new_cxt->caps_logged = profile->quiet_caps;
		new_cxt->cookie = cookie;
		new_cxt->task = task;
		new_cxt->profile = aa_dup_profile(profile);
		profile->task_count++;
		new_cxt->previous_profile = aa_dup_profile(previous_profile);
		list_move(&new_cxt->list, &profile->task_contexts);
	}
	rcu_assign_pointer(task->security, new_cxt);
}
