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

#include "apparmor.h"

#include "inline.h"

/*
 * Table of capability names: we generate it from capabilities.h.
 */
static const char *capability_names[] = {
#include "capability_names.h"
};

/* NULL complain profile
 *
 * Used when in complain mode, to emit Permitting messages for non-existant
 * profiles and hats.  This is necessary because of selective mode, in which
 * case we need a complain null_profile and enforce null_profile
 *
 * The null_complain_profile cannot be statically allocated, because it
 * can be associated to files which keep their reference even if apparmor is
 * unloaded
 */
struct aa_profile *null_complain_profile;

/**
 * aa_file_denied - check for @mask access on a file
 * @profile: profile to check against
 * @name: pathname of file
 * @mask: permission mask requested for file
 *
 * Return %0 on success, or else the permissions in @mask that the
 * profile denies.
 */
static int aa_file_denied(struct aa_profile *profile, const char *name,
			  int mask)
{
	return (mask & ~aa_match(profile->file_rules, name));
}

/**
 * aa_link_denied - check for permission to link a file
 * @profile: profile to check against
 * @link: pathname of link being created
 * @target: pathname of target to be linked to
 * @request_mask: the permissions subset valid only if link succeeds
 * Return %0 on success, or else the permissions that the profile denies.
 */
static int aa_link_denied(struct aa_profile *profile, const char *link,
			  const char *target, int *request_mask)
{
	int l_mode, t_mode, denied_mask;

	l_mode = aa_match(profile->file_rules, link);
	t_mode = aa_match(profile->file_rules, target);

	/* Ignore valid-profile-transition flags. */
	l_mode &= ~AA_CHANGE_PROFILE;
	t_mode &= ~AA_CHANGE_PROFILE;

	*request_mask = l_mode | AA_MAY_LINK;

	/* Link always requires 'l' on the link, a subset of the
	 * target's 'r', 'w', 'x', and 'm' permissions on the link, and
	 * if the link has 'x', an exact match of all the execute flags
	 * ('i', 'u', 'U', 'p', 'P').
	 */
#define RWXM (MAY_READ | MAY_WRITE | MAY_EXEC | AA_EXEC_MMAP)
	denied_mask = ~l_mode & AA_MAY_LINK;
	if (l_mode & RWXM)
		denied_mask |= (l_mode & ~ AA_MAY_LINK) & ~t_mode;
	else
		denied_mask |= t_mode | AA_MAY_LINK;
	if (denied_mask & AA_EXEC_MODIFIERS)
		denied_mask |= MAY_EXEC;

#undef RWXM

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

	if (IS_ERR(sa->name)) {
		/*
		 * deleted files are given a pass on permission checks when
		 * accessed through a file descriptor.
		 */
		if (PTR_ERR(sa->name) == -ENOENT && (check & AA_CHECK_FD))
			sa->denied_mask = 0;
		else
			sa->denied_mask = PTR_ERR(sa->name);
		sa->name = NULL;
	} else
		sa->denied_mask = aa_file_denied(profile, sa->name,
						 sa->requested_mask);

	if (!sa->denied_mask)
		sa->error_code = 0;

	error = aa_audit(profile, sa);
	aa_put_name_buffer(buffer);

	return error;
}

/**
 * alloc_null_complain_profile - Allocate the global null_complain_profile.
 *
 * Return %0 (success) or error (-%ENOMEM)
 */
int alloc_null_complain_profile(void)
{
	null_complain_profile = alloc_aa_profile();
	if (!null_complain_profile)
		goto fail;

	null_complain_profile->name =
		kstrdup("null-complain-profile", GFP_KERNEL);

	if (!null_complain_profile->name)
		goto fail;

	null_complain_profile->flags.complain = 1;

	return 0;

fail:
	/* free_aa_profile is safe for freeing partially constructed objects */
	free_aa_profile(null_complain_profile);
	null_complain_profile = NULL;

	return -ENOMEM;
}

/**
 * free_null_complain_profile - Free null profiles
 */
void free_null_complain_profile(void)
{
	aa_put_profile(null_complain_profile);
	null_complain_profile = NULL;
}

static void aa_audit_file_mask(struct audit_buffer *ab, const char *name,
			       int mask)
{
	char mask_str[10], *m = mask_str;

	if (mask & AA_EXEC_MMAP)
		*m++ = 'm';
	if (mask & MAY_READ)
		*m++ = 'r';
	if (mask & MAY_WRITE)
		*m++ = 'w';
	if (mask & (MAY_EXEC | AA_EXEC_MODIFIERS)) {
		if (mask & AA_EXEC_UNSAFE) {
			if (mask & AA_EXEC_INHERIT)
				*m++ = 'i';
			if (mask & AA_EXEC_UNCONFINED)
				*m++ = 'u';
			if (mask & AA_EXEC_PROFILE)
				*m++ = 'p';
		} else {
			if (mask & AA_EXEC_INHERIT)
				*m++ = 'I';
			if (mask & AA_EXEC_UNCONFINED)
				*m++ = 'U';
			if (mask & AA_EXEC_PROFILE)
				*m++ = 'P';
		}
		if (mask & MAY_EXEC)
			*m++ = 'x';
	}
	if (mask & AA_MAY_LINK)
		*m++ = 'l';
	*m++ = '\0';

	audit_log_format(ab, " %s=\"%s\"", name, mask_str);
}

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

	if (sa->info)
		audit_log_format(ab, " info=\"%s\"", sa->info);

	if (sa->requested_mask)
		aa_audit_file_mask(ab, "requested_mask", sa->requested_mask);

	if (sa->denied_mask)
		aa_audit_file_mask(ab, "denied_mask", sa->denied_mask);

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

	audit_log_format(ab, " pid=%d", current->pid);

	if (profile) {
		audit_log_format(ab, " profile=");
		audit_log_untrustedstring(ab, profile->name);
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

	if (likely(!sa->error_code)) {
		if (likely(!PROFILE_AUDIT(profile)))
			/* nothing to log */
			return 0;
		else
			type = AUDIT_APPARMOR_AUDIT;
	} else if (PROFILE_COMPLAIN(profile)) {
		type = AUDIT_APPARMOR_ALLOWED;
	}

	audit_cxt = apparmor_logsyscall ? current->audit_context : NULL;
	return aa_audit_base(profile, sa, audit_cxt, type);
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
	sa.requested_mask = MAY_WRITE;
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
	sa.requested_mask = mask;
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
	sa.requested_mask = mask;
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
	sa.requested_mask = mask;
	sa.error_code = -EACCES;

	return aa_perm_dentry(profile, dentry, mnt, &sa, AA_CHECK_DIR);
}

int aa_perm_path(struct aa_profile *profile, const char *operation,
		 const char *name, int mask)
{
	struct aa_audit sa;

	memset(&sa, 0, sizeof(sa));
	sa.operation = operation;
	sa.gfp_mask = GFP_KERNEL;
	sa.requested_mask = mask;
	sa.name = name;

	sa.denied_mask = aa_file_denied(profile, name, mask);
	sa.error_code = sa.denied_mask ? -EACCES : 0;

	return aa_audit(profile, &sa);
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

	error = aa_audit(cxt->profile, &sa);

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
	int error, check = 0;
	struct aa_audit sa;
	char *buffer = NULL, *buffer2 = NULL;

	memset(&sa, 0, sizeof(sa));
	sa.operation = "inode_link";
	sa.gfp_mask = GFP_KERNEL;
	sa.name = aa_get_name(link, link_mnt, &buffer, check);
	sa.name2 = aa_get_name(target, target_mnt, &buffer2, check);

	if (IS_ERR(sa.name)) {
		sa.error_code = PTR_ERR(sa.name);
		sa.name = NULL;
	}
	if (IS_ERR(sa.name2)) {
		sa.error_code = PTR_ERR(sa.name2);
		sa.name2 = NULL;
	}

	if (sa.name && sa.name2) {
		sa.requested_mask = AA_MAY_LINK;
		sa.denied_mask = aa_link_denied(profile, sa.name, sa.name2,
						&sa.requested_mask);
		sa.error_code = sa.denied_mask ? -EACCES : 0;
	}

	error = aa_audit(profile, &sa);

	aa_put_name_buffer(buffer);
	aa_put_name_buffer(buffer2);

	return error;
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
	struct aa_task_context *cxt, *child_cxt;
	struct aa_profile *profile;

	if (!aa_task_context(current))
		return 0;
	child_cxt = aa_alloc_task_context(GFP_KERNEL);
	if (!child_cxt)
		return -ENOMEM;

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

		/* No need to grab the child's task lock here. */
		aa_change_task_context(child, child_cxt, profile,
				       cxt->cookie, cxt->previous_profile);
		unlock_profile(profile);

		if (APPARMOR_COMPLAIN(child_cxt) &&
		    profile == null_complain_profile) {
			struct aa_audit sa;
			memset(&sa, 0, sizeof(sa));
			sa.operation = "clone";
			sa.gfp_mask = GFP_KERNEL;
			sa.task = child->pid;
			aa_audit_hint(profile, &sa);
		}
		aa_put_profile(profile);
	} else
		aa_free_task_context(child_cxt);

	return 0;
}

static struct aa_profile *
aa_register_find(struct aa_profile *profile, const char *name, int mandatory,
		 int complain, struct aa_audit *sa)
{
	struct aa_profile *new_profile;

	/* Locate new profile */
	new_profile = aa_find_profile(name);
	if (new_profile) {
		AA_DEBUG("%s: setting profile %s\n",
			 __FUNCTION__, new_profile->name);
	} else if (mandatory && profile) {
		sa->info = "mandatory profile missing";
		sa->denied_mask = MAY_EXEC;
		if (complain) {
			aa_audit_hint(profile, sa);
			profile = aa_dup_profile(null_complain_profile);
		} else {
			aa_audit_reject(profile, sa);
			return ERR_PTR(-EACCES);  /* was -EPERM */
		}
	} else {
		/* Only way we can get into this code is if task
		 * is unconfined.
		 */
		AA_DEBUG("%s: No profile found for exec image '%s'\n",
			 __FUNCTION__,
			 name);
	}
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
	char  *buffer = NULL;
	struct file *filp = bprm->file;
	struct aa_profile *profile, *old_profile, *new_profile = NULL;
	int exec_mode = AA_EXEC_UNSAFE, complain = 0;
	struct aa_audit sa;

	AA_DEBUG("%s\n", __FUNCTION__);

	filename = aa_get_name(filp->f_dentry, filp->f_vfsmnt, &buffer, 0);
	if (IS_ERR(filename)) {
		AA_ERROR("%s: Failed to get filename", __FUNCTION__);
		return -ENOENT;
	}

	memset(&sa, 0, sizeof(sa));
	sa.operation = "exec";
	sa.gfp_mask = GFP_KERNEL;
	sa.name = filename;
	sa.requested_mask = MAY_EXEC;

repeat:
	profile = aa_get_profile(current);
	if (profile) {
		complain = PROFILE_COMPLAIN(profile);

		/* Confined task, determine what mode inherit, unconfined or
		 * mandatory to load new profile
		 */
		exec_mode = aa_match(profile->file_rules, filename);

		if (exec_mode & (MAY_EXEC | AA_EXEC_MODIFIERS)) {
			switch (exec_mode & (MAY_EXEC | AA_EXEC_MODIFIERS)) {
			case MAY_EXEC | AA_EXEC_INHERIT:
				AA_DEBUG("%s: INHERIT %s\n",
					 __FUNCTION__,
					 filename);
				/* nothing to be done here */
				goto cleanup;

			case MAY_EXEC | AA_EXEC_UNCONFINED:
				AA_DEBUG("%s: UNCONFINED %s\n",
					 __FUNCTION__,
					 filename);

				/* detach current profile */
				new_profile = NULL;
				break;

			case MAY_EXEC | AA_EXEC_PROFILE:
				AA_DEBUG("%s: PROFILE %s\n",
					 __FUNCTION__,
					 filename);
				new_profile = aa_register_find(profile,
							       filename,
							       1, complain,
							       &sa);
				break;
			}

		} else if (complain) {
			/* There was no entry in calling profile
			 * describing mode to execute image in.
			 * Drop into null-profile (disabling secure exec).
			 */
			new_profile = aa_dup_profile(null_complain_profile);
			exec_mode |= AA_EXEC_UNSAFE;
		} else {
			sa.denied_mask = MAY_EXEC;
			aa_audit_reject(profile, &sa);
			new_profile = ERR_PTR(-EPERM);
		}
	} else {
		/* Unconfined task, load profile if it exists */
		new_profile = aa_register_find(NULL, filename, 0, 0, &sa);
		if (new_profile == NULL)
			goto cleanup;
	}

	if (IS_ERR(new_profile))
		goto cleanup;

	old_profile = __aa_replace_profile(current, new_profile);
	if (IS_ERR(old_profile)) {
		aa_put_profile(new_profile);
		aa_put_profile(profile);
		if (PTR_ERR(old_profile) == -ESTALE)
			goto repeat;
		if (PTR_ERR(old_profile) == -EPERM) {
			sa.denied_mask = MAY_EXEC;
			sa.info = "unable to set profile due to ptrace";
			sa.task = current->parent->pid;
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
	if (!(exec_mode & AA_EXEC_UNSAFE)) {
		unsigned long bprm_flags;

		bprm_flags = AA_SECURE_EXEC_NEEDED;
		bprm->security = (void*)
			((unsigned long)bprm->security | bprm_flags);
	}

	if (complain && new_profile == null_complain_profile) {
		sa.requested_mask = 0;
		sa.name = NULL;
		sa.info = "set profile";
		aa_audit_hint(new_profile, &sa);
	}
cleanup:
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

static int do_change_profile(struct aa_profile *expected, const char *name,
			     u64 cookie, int restore, struct aa_audit *sa)
{
	struct aa_profile *new_profile = NULL, *old_profile = NULL,
		*previous_profile = NULL;
	struct aa_task_context *new_cxt, *cxt;
	int error = 0;

	sa->name = name;

	new_cxt = aa_alloc_task_context(GFP_KERNEL);
	if (!new_cxt)
		return -ENOMEM;

	new_profile = aa_find_profile(name);
	if (!new_profile && !restore) {
		if (!PROFILE_COMPLAIN(expected))
			return -ENOENT;
		new_profile = aa_dup_profile(null_complain_profile);
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

	if (new_profile == null_complain_profile)
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
 * aa_change_profile - change profile to/from previous stored profile
 * @name: name of profile to change to
 * @cookie: magic value to validate the profile change
 *
 * Change to new profile @name, and store the @cookie in the current task
 * context.  If the new @name is %NULL and the @cookie matches that
 * stored in the current task context, return to the previous profile.
 *
 * Returns %0 on success, error otherwise.
 */
int aa_change_profile(const char *name, u64 cookie)
{
	struct aa_task_context *cxt;
	struct aa_profile *profile, *previous_profile;
	struct aa_audit sa;
	int error = 0;

	memset(&sa, 0, sizeof(sa));
	sa.gfp_mask = GFP_ATOMIC;
	sa.operation = "change_profile";

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

	if (name) {
		if (profile != null_complain_profile &&
		    !(aa_match(profile->file_rules, name) &
		      AA_CHANGE_PROFILE)) {
			/* no permission to transition to profile @name */
			aa_put_profile(profile);
			return -EACCES;
		}
		error = do_change_profile(profile, name, cookie, 0, &sa);
	} else if (previous_profile)
		error = do_change_profile(profile, previous_profile->name,
					  cookie, 1, &sa);
	/* else ignore restores when there is no saved profile */

	aa_put_profile(previous_profile);
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

		name = kmalloc(strlen(hat_name) + 3 + strlen(profile_name),
			       GFP_KERNEL);
		if (!name) {
			error = -ENOMEM;
			goto out;
		}
		sprintf(name, "%s//%s", profile_name, hat_name);
		error = do_change_profile(profile, name, cookie, 0, &sa);
		kfree(name);
	} else if (previous_profile)
		error = do_change_profile(profile, previous_profile->name,
					  cookie, 1, &sa);
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
		task_unlock(task);
		unlock_both_profiles(profile, cxt ? cxt->profile : NULL);
		aa_free_task_context(new_cxt);
		return ERR_PTR(-ESTALE);
	}

	if ((current->ptrace & PT_PTRACED) && aa_may_ptrace(cxt, profile)) {
		task_unlock(task);
		unlock_both_profiles(profile, cxt ? cxt->profile : NULL);
		aa_free_task_context(new_cxt);
		return ERR_PTR(-EPERM);
	}

	if (cxt)
		old_profile = aa_dup_profile(cxt->profile);
	aa_change_task_context(task, new_cxt, profile, 0, NULL);

	task_unlock(task);
	unlock_both_profiles(profile, old_profile);
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
		call_rcu(&old_cxt->rcu, free_aa_task_context_rcu_callback);
	}
	if (new_cxt) {
		/* clear the caps_logged cache, so that new profile/hat has
		 * chance to emit its own set of cap messages */
		new_cxt->caps_logged = CAP_EMPTY_SET;
		new_cxt->cookie = cookie;
		new_cxt->task = task;
		new_cxt->profile = aa_dup_profile(profile);
		new_cxt->previous_profile = aa_dup_profile(previous_profile);
		list_move(&new_cxt->list, &profile->task_contexts);
	}
	rcu_assign_pointer(task->security, new_cxt);
}
