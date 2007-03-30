/*
 *	Copyright (C) 2002-2005 Novell/SUSE
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

#include "apparmor.h"

#include "inline.h"

/*
 * A table of capability names: we generate it from capabilities.h.
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

/***************************
 * Private utility functions
 **************************/

/**
 * aa_taskattr_access
 * @name: name of file to check permission
 *
 * Check if name matches /proc/self/attr/current, with self resolved
 * to the current pid. This file is the usermode iterface for
 * changing one's hat.
 */
static inline int aa_taskattr_access(const char *name)
{
	unsigned long pid;
	char *end;

	if (strncmp(name, "/proc/", 6) != 0)
		return 0;
	pid = simple_strtoul(name + 6, &end, 10);
	if (pid != current->pid)
		return 0;
	return strcmp(end, "/attr/current") == 0;
}

static inline void aa_permerror2result(int perm_result, struct aa_audit *sa)
{
	if (perm_result == 0) {	/* success */
		sa->result = 1;
		sa->error_code = 0;
	} else { /* -ve internal error code or +ve mask of denied perms */
		sa->result = 0;
		sa->error_code = perm_result;
	}
}

/*************************
 * Main internal functions
 ************************/

/**
 * aa_file_denied - check for @mask access on a file
 * @profile: profile to check against
 * @name: name of file
 * @mask: permission mask requested for file
 *
 * Return %0 on success, or else the permissions in @mask that the
 * profile denies.
 */
static int aa_file_denied(struct aa_profile *profile, const char *name,
			  int mask)
{
	int perms;

	/* Always allow write access to /proc/self/attr/current. */
	if (mask == MAY_WRITE && aa_taskattr_access(name))
		return 0;

	perms = aa_match(profile->file_rules, name);

	return (mask & ~perms);
}

/**
 * aa_link_denied - check for permission to link a file
 * @profile: profile to check against
 * @link: name of link being created
 * @target: name of target to be linked to
 *
 * Return %0 on success, or else the permissions that the profile denies.
 */
static int aa_link_denied(struct aa_profile *profile, const char *link,
			  const char *target)
{
	int l_mode, t_mode;

	l_mode = aa_match(profile->file_rules, link);
	t_mode = aa_match(profile->file_rules, target);

	/**
	 * Link always requires 'l' on the link, a subset of the
	 * target's 'r', 'w', 'x', and 'm' permissions on the link, and
	 * if the link has 'x', an exact match of all flags except
	 * 'r', 'w', 'x', 'm'.
	 */
#define RWXM (MAY_READ | MAY_WRITE | MAY_EXEC | AA_EXEC_MMAP)
	if ((l_mode & AA_MAY_LINK) &&
	    !(l_mode & ~t_mode & RWXM) &&
	    (!(t_mode & MAY_EXEC) || (l_mode & ~RWXM) == (t_mode & ~RWXM)))
		return 0;
#undef RWXM

	/**
	 * FIXME: There currenly is no way to report which permissions
	 * we expect in t_mode, so linking could fail even after learning
	 * the required l_mode.
	 */
	return 0;
}

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

static int aa_perm_dentry(struct aa_profile *profile, struct dentry *dentry,
			  struct vfsmount *mnt, struct aa_audit *sa, int mask,
			  int check)
{
	char *buffer = NULL;
	int denied_mask, error;

	sa->name = aa_get_name(dentry, mnt, &buffer, check);

	if (IS_ERR(sa->name)) {
		/*
		 * deleted files are given a pass on permission checks when
		 * accessed through a file descriptor.
		 */
		if (PTR_ERR(sa->name) == -ENOENT && (check & AA_CHECK_FD))
			denied_mask = 0;
		else
			denied_mask = PTR_ERR(sa->name);
		sa->name = NULL;
	} else {
		denied_mask = aa_file_denied(profile, sa->name, mask);
	}

	aa_permerror2result(denied_mask, sa);

	error = aa_audit(profile, sa);

	aa_put_name_buffer(buffer);

	return error;
}

/**************************
 * Global utility functions
 *************************/

/**
 * attach_nullprofile - allocate and attach a null_profile hat to profile
 * @profile: profile to attach a null_profile hat to.
 *
 * Return %0 (success) or error (-%ENOMEM)
 */
int attach_nullprofile(struct aa_profile *profile)
{
	struct aa_profile *hat = NULL;
	char *hatname = NULL;

	hat = alloc_aa_profile();
	if (!hat)
		goto fail;
	if (profile->flags.complain)
		hatname = kstrdup("null-complain-profile", GFP_KERNEL);
	else
		hatname = kstrdup("null-profile", GFP_KERNEL);
	if (!hatname)
		goto fail;

	hat->flags.complain = profile->flags.complain;
	hat->name = hatname;
	hat->parent = profile;

	profile->null_profile = hat;

	return 0;

fail:
	kfree(hatname);
	free_aa_profile(hat);

	return -ENOMEM;
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
	if (attach_nullprofile(null_complain_profile))
		goto fail;

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

/**
 * aa_audit_message - Log a message to the audit subsystem
 * @profile: profile to check against
 * @gfp: allocation flags
 * @flags: audit flags
 * @fmt: varargs fmt
 */
int aa_audit_message(struct aa_profile *profile, gfp_t gfp, int flags,
		     const char *fmt, ...)
{
	int ret;
	struct aa_audit sa;

	sa.type = AA_AUDITTYPE_MSG;
	sa.name = fmt;
	va_start(sa.vaval, fmt);
	sa.flags = flags;
	sa.gfp_mask = gfp;
	sa.error_code = 0;
	sa.result = 0;	/* fake failure: force message to be logged */

	ret = aa_audit(profile, &sa);

	va_end(sa.vaval);

	return ret;
}

/**
 * aa_audit_syscallreject - Log a syscall rejection to the audit subsystem
 * @profile: profile to check against
 * @msg: string describing syscall being rejected
 * @gfp: memory allocation flags
 */
int aa_audit_syscallreject(struct aa_profile *profile, gfp_t gfp,
			   const char *msg)
{
	struct aa_audit sa;

	sa.type = AA_AUDITTYPE_SYSCALL;
	sa.name = msg;
	sa.flags = 0;
	sa.gfp_mask = gfp;
	sa.error_code = 0;
	sa.result = 0; /* failure */

	return aa_audit(profile, &sa);
}

/**
 * aa_audit - Log an audit event to the audit subsystem
 * @profile: profile to check against
 * @sa: audit event
 */
int aa_audit(struct aa_profile *profile, const struct aa_audit *sa)
{
	struct audit_buffer *ab = NULL;
	struct audit_context *audit_cxt;

	const char *logcls;
	unsigned int flags;
	int audit = 0,
	    complain = 0,
	    error = -EINVAL,
	    opspec_error = -EACCES;

	const gfp_t gfp_mask = sa->gfp_mask;

	WARN_ON(sa->type >= AA_AUDITTYPE__END);

	/*
	 * sa->result:	  1 success, 0 failure
	 * sa->error_code: success: 0
	 *		  failure: +ve mask of failed permissions or -ve
	 *		  system error
	 */

	if (likely(sa->result)) {
		if (likely(!PROFILE_AUDIT(profile))) {
			/* nothing to log */
			error = 0;
			goto out;
		} else {
			audit = 1;
			logcls = "AUDITING";
		}
	} else if (sa->error_code < 0) {
		audit_log(current->audit_context, gfp_mask, AUDIT_APPARMOR,
			"Internal error auditing event type %d (error %d)",
			sa->type, sa->error_code);
		AA_ERROR("Internal error auditing event type %d (error %d)\n",
			sa->type, sa->error_code);
		error = sa->error_code;
		goto out;
	} else if (sa->type == AA_AUDITTYPE_SYSCALL) {
		/* Currently AA_AUDITTYPE_SYSCALL is for rejects only.
		 * Values set by aa_audit_syscallreject will get us here.
		 */
		logcls = "REJECTING";
	} else {
		complain = PROFILE_COMPLAIN(profile);
		logcls = complain ? "PERMITTING" : "REJECTING";
	}

	/* In future extend w/ per-profile flags
	 * (flags |= sa->profile->flags)
	 */
	flags = sa->flags;
	if (apparmor_logsyscall)
		flags |= AA_AUDITFLAG_AUDITSS_SYSCALL;


	/* Force full audit syscall logging regardless of global setting if
	 * we are rejecting a syscall
	 */
	if (sa->type == AA_AUDITTYPE_SYSCALL) {
		audit_cxt = current->audit_context;
	} else {
		audit_cxt = (flags & AA_AUDITFLAG_AUDITSS_SYSCALL) ?
			current->audit_context : NULL;
	}

	ab = audit_log_start(audit_cxt, gfp_mask, AUDIT_APPARMOR);

	if (!ab) {
		AA_ERROR("Unable to log event (%d) to audit subsys\n",
			sa->type);
		if (complain)
			error = 0;
		goto out;
	}

	/* messages get special handling */
	if (sa->type == AA_AUDITTYPE_MSG) {
		audit_log_vformat(ab, sa->name, sa->vaval);
		audit_log_end(ab);
		error = 0;
		goto out;
	}

	/* log operation */

	audit_log_format(ab, "%s ", logcls);	/* REJECTING/ALLOWING/etc */

	if (sa->type == AA_AUDITTYPE_FILE) {
		int perm = audit ? sa->mask : sa->error_code;

		audit_log_format(ab, "%s%s%s%s%s access to %s ",
				 perm & AA_EXEC_MMAP ? "m" : "",
				 perm & MAY_READ  ? "r" : "",
				 perm & MAY_WRITE ? "w" : "",
				 perm & MAY_EXEC  ? "x" : "",
				 perm & AA_MAY_LINK  ? "l" : "",
				 sa->name);

		opspec_error = -EPERM;

	} else if (sa->type == AA_AUDITTYPE_DIR) {
		audit_log_format(ab, "%s on %s ", sa->operation, sa->name);

	} else if (sa->type == AA_AUDITTYPE_ATTR) {
		struct iattr *iattr = (struct iattr*)sa->pval;

		audit_log_format(ab,
			"attribute (%s%s%s%s%s%s%s) change to %s ",
			iattr->ia_valid & ATTR_MODE ? "mode," : "",
			iattr->ia_valid & ATTR_UID ? "uid," : "",
			iattr->ia_valid & ATTR_GID ? "gid," : "",
			iattr->ia_valid & ATTR_SIZE ? "size," : "",
			((iattr->ia_valid & ATTR_ATIME_SET) ||
			 (iattr->ia_valid & ATTR_ATIME)) ? "atime," : "",
			((iattr->ia_valid & ATTR_MTIME_SET) ||
			 (iattr->ia_valid & ATTR_MTIME)) ? "mtime," : "",
			iattr->ia_valid & ATTR_CTIME ? "ctime," : "",
			sa->name);

	} else if (sa->type == AA_AUDITTYPE_XATTR) {
		/* FIXME: how are special characters in sa->name escaped? */
		/* FIXME: check if this can be handled on the stack
			  with an inline varargs function. */
		audit_log_format(ab, "%s on %s ", sa->operation, sa->name);

	} else if (sa->type == AA_AUDITTYPE_LINK) {
		audit_log_format(ab,
			"link access from %s to %s ",
			sa->name,
			(char*)sa->pval);

	} else if (sa->type == AA_AUDITTYPE_CAP) {
		audit_log_format(ab,
			"access to capability '%s' ",
			capability_names[sa->capability]);

		opspec_error = -EPERM;
	} else if (sa->type == AA_AUDITTYPE_SYSCALL) {
		audit_log_format(ab, "access to syscall '%s' ", sa->name);

		opspec_error = -EPERM;
	} else {
		/* -EINVAL -- will WARN_ON above */
		goto out;
	}

	audit_log_format(ab, "(%s(%d) profile %s active %s)",
			 current->comm, current->pid,
			 profile->parent->name, profile->name);

	audit_log_end(ab);

	if (complain)
		error = 0;
	else
		error = sa->result ? 0 : opspec_error;

out:
	return error;
}

/***********************************
 * Global permission check functions
 ***********************************/

/**
 * aa_attr - check whether attribute change allowed
 * @profile: profile to check against
 * @dentry: file to check
 * @iattr: attribute changes requested
 */
int aa_attr(struct aa_profile *profile, struct dentry *dentry,
	    struct vfsmount *mnt, struct iattr *iattr)
{
	struct inode *inode = dentry->d_inode;
	int error, check;
	struct aa_audit sa;

	sa.type = AA_AUDITTYPE_ATTR;
	sa.pval = iattr;
	sa.flags = 0;
	sa.gfp_mask = GFP_KERNEL;

	check = 0;
	if (inode && S_ISDIR(inode->i_mode))
		check |= AA_CHECK_DIR;
	if (iattr->ia_valid & ATTR_FILE)
		check |= AA_CHECK_FD;

	error = aa_perm_dentry(profile, dentry, mnt, &sa, MAY_WRITE, check);

	return error;
}

/**
 * aa_perm_xattr - check whether xattr attribute change allowed
 * @profile: profile to check against
 * @dentry: file to check
 * @mnt: mount of file to check
 * @operation: xattr operation being done
 * @xattr_name: name of xattr to check
 * @mask: access mode requested
 */
int aa_perm_xattr(struct aa_profile *profile, struct dentry *dentry,
		  struct vfsmount *mnt, const char *operation,
		  const char *xattr_name, int mask, int check)
{
	struct inode *inode = dentry->d_inode;
	int error;
	struct aa_audit sa;

	sa.type = AA_AUDITTYPE_XATTR;
	sa.operation = operation;
	sa.pval = xattr_name;
	sa.flags = 0;
	sa.gfp_mask = GFP_KERNEL;

	if (inode && S_ISDIR(inode->i_mode))
		check |= AA_CHECK_DIR;

	error = aa_perm_dentry(profile, dentry, mnt, &sa, mask, check);

	return error;
}

/**
 * aa_perm - basic apparmor permissions check
 * @profile: profile to check against
 * @dentry: dentry
 * @mnt: mountpoint
 * @mask: access mode requested
 * @leaf: are we checking a leaf node?
 *
 * Determine if access (mask) for dentry is authorized by profile
 * profile.  Result, %0 (success), -ve (error)
 */
int aa_perm(struct aa_profile *profile, struct dentry *dentry,
	    struct vfsmount *mnt, int mask, int check)
{
	struct aa_audit sa;
	int error = 0;

	if ((check & (AA_CHECK_DIR | AA_CHECK_LEAF)) == AA_CHECK_DIR) {
		/*
		 * If checking a non-leaf directory, allow traverse and
		 * write access: we do not require profile access to
		 * non-leaf directories in order to traverse them,
		 * create or remove files in them. We do require
		 * MAY_WRITE profile access on the actual file or
		 * directory being created or removed, though.
		 */
		mask &= ~(MAY_EXEC | MAY_WRITE);
	}
	if (mask == 0)
		goto out;

	sa.type = AA_AUDITTYPE_FILE;
	sa.mask = mask;
	sa.flags = 0;
	sa.gfp_mask = GFP_KERNEL;
	error = aa_perm_dentry(profile, dentry, mnt, &sa, mask, check);

out:
	return error;
}

/**
 * aa_perm_dir
 * @profile: profile to check against
 * @dentry: requested dentry
 * @mnt: mount of file to check
 * @operation: directory operation being performed
 * @mask: access mode requested
 *
 * Determine if directory operation (make/remove) for dentry is authorized
 * by @profile.
 * Result, %0 (success), -ve (error)
 */
int aa_perm_dir(struct aa_profile *profile, struct dentry *dentry,
		struct vfsmount *mnt, const char *operation, int mask)
{
	struct aa_audit sa;

	sa.type = AA_AUDITTYPE_DIR;
	sa.operation = operation;
	sa.flags = 0;
	sa.gfp_mask = GFP_KERNEL;

	return aa_perm_dentry(profile, dentry, mnt, &sa, mask,
			      AA_CHECK_DIR | AA_CHECK_LEAF);
}

/**
 * aa_capability - test permission to use capability
 * @cxt: aa_task_context with profile to check against
 * @cap: capability to be tested
 *
 * Look up capability in profile capability set.
 * Return %0 (success), -%EPERM (error)
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
		 * caps_logged is a cache to reduce the occurance of
		 * duplicate messages in the log.  The worst that can
		 * happen is duplicate capability messages shows up in
		 * the audit log
		 */
		cap_raise(cxt->caps_logged, cap);

	sa.type = AA_AUDITTYPE_CAP;
	sa.name = NULL;
	sa.capability = cap;
	sa.flags = 0;
	sa.error_code = 0;
	sa.result = !error;
	sa.gfp_mask = GFP_ATOMIC;

	error = aa_audit(cxt->profile, &sa);

	return error;
}

/**
 * aa_link - hard link check
 * @profile: profile to check against
 * @link: dentry for link being created
 * @target: dentry for link target
 * @mnt: vfsmount (-EXDEV is link and target are not on same vfsmount)
 */
int aa_link(struct aa_profile *profile,
	    struct dentry *link, struct vfsmount *link_mnt,
	    struct dentry *target, struct vfsmount *target_mnt)
{
	char *name_buffer = NULL, *pval_buffer = NULL;
	int denied_mask = -EPERM, error;
	struct aa_audit sa;

	sa.name = aa_get_name(link, link_mnt, &name_buffer, 0);
	sa.pval = aa_get_name(target, target_mnt, &pval_buffer, 0);

	if (IS_ERR(sa.name)) {
		denied_mask = PTR_ERR(sa.name);
		sa.name = NULL;
	}
	if (IS_ERR(sa.pval)) {
		denied_mask = PTR_ERR(sa.pval);
		sa.pval = NULL;
	}

	if (sa.name && sa.pval)
		denied_mask = aa_link_denied(profile, sa.name, sa.pval);

	aa_permerror2result(denied_mask, &sa);

	sa.type = AA_AUDITTYPE_LINK;
	sa.flags = 0;
	sa.gfp_mask = GFP_KERNEL;

	error = aa_audit(profile, &sa);

	aa_put_name_buffer(name_buffer);
	aa_put_name_buffer(pval_buffer);

	return error;
}

/*******************************
 * Global task related functions
 *******************************/

/**
 * aa_clone - initialize the task context for a new task
 * @task: task that is being created
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
				       cxt->hat_magic);
		unlock_profile(profile);

		if (APPARMOR_COMPLAIN(child_cxt) &&
		    profile == null_complain_profile) {
			LOG_HINT(profile, GFP_KERNEL, HINT_FORK,
				 "pid=%d child=%d\n",
				 current->pid, child->pid);
		}
		aa_put_profile(profile);
	} else
		aa_free_task_context(child_cxt);

	return 0;
}

static struct aa_profile *
aa_register_find(struct aa_profile *profile, const char *name, int mandatory,
		 int complain)
{
	struct aa_profile *new_profile;

	/* Locate new profile */
	new_profile = aa_find_profile(name);
	if (new_profile) {
		AA_DEBUG("%s: setting profile %s\n",
			 __FUNCTION__, new_profile->name);
	} else if (mandatory && profile) {
		if (complain) {
			LOG_HINT(profile, GFP_KERNEL, HINT_MANDPROF,
				"image=%s pid=%d profile=%s active=%s\n",
				name,
				current->pid,
				profile->parent->name, profile->name);

			profile = aa_dup_profile(null_complain_profile);
		} else {
			AA_WARN(GFP_KERNEL, "REJECTING exec(2) of image '%s'. "
				"Profile mandatory and not found "
				"(%s(%d) profile %s active %s)\n",
				name,
				current->comm, current->pid,
				profile->parent->name, profile->name);
			return ERR_PTR(-EPERM);
		}
	} else {
		/* Only way we can get into this code is if task
		 * is unconfined.
		 */
		AA_DEBUG("%s: No profile found for exec image %s\n",
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
 * new program a valid aa_task_context.
 */
int aa_register(struct linux_binprm *bprm)
{
	char *filename, *buffer = NULL;
	struct file *filp = bprm->file;
	struct aa_profile *profile, *old_profile, *new_profile = NULL;
	int exec_mode = AA_EXEC_UNSAFE, complain = 0;

	AA_DEBUG("%s\n", __FUNCTION__);

	filename = aa_get_name(filp->f_dentry, filp->f_vfsmnt, &buffer, 0);
	if (IS_ERR(filename)) {
		AA_WARN(GFP_KERNEL, "%s: Failed to get filename\n",
			__FUNCTION__);
		return -ENOENT;
	}

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
							       filename, 1,
							       complain);
				break;

			default:
				AA_ERROR("%s: Rejecting exec(2) of image '%s'. "
					 "Unknown exec qualifier %x "
					 "(%s (pid %d) profile %s active %s)\n",
					 __FUNCTION__,
					 filename,
					 exec_mode & AA_EXEC_MODIFIERS,
					 current->comm, current->pid,
					 profile->parent->name,
					 profile->name);
				new_profile = ERR_PTR(-EPERM);
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
			AA_WARN(GFP_KERNEL,
				"%s: Rejecting exec(2) of image '%s'. "
				"Unable to determine exec qualifier "
				"(%s (pid %d) profile %s active %s)\n",
				__FUNCTION__,
				filename,
				current->comm, current->pid,
				profile->parent->name, profile->name);
			new_profile = ERR_PTR(-EPERM);
		}
	} else {
		/* Unconfined task, load profile if it exists */
		new_profile = aa_register_find(NULL, filename, 0, 0);
		if (new_profile == NULL)
			goto cleanup;
	}

	if (IS_ERR(new_profile))
		goto cleanup;

	old_profile = aa_replace_profile(current, new_profile, 0);
	if (IS_ERR(old_profile)) {
		aa_put_profile(new_profile);
		aa_put_profile(profile);
		if (PTR_ERR(old_profile) == -ESTALE)
			goto repeat;
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
		LOG_HINT(new_profile, GFP_ATOMIC, HINT_CHGPROF,
			"pid=%d\n",
			current->pid);
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
	 * We also avoid taking the task_lock here because lock_dep
	 * would report a false {softirq-on-W} potential irq_lock
	 * inversion.
	 *
	 * If the task does not have a profile attached we are safe;
	 * nothing can race with us at this point.
	 */

repeat:
	profile = aa_get_profile(task);
	if (profile) {
		lock_profile(profile);
		cxt = aa_task_context(task);
		if (unlikely(!cxt || cxt->profile != profile)) {
			unlock_profile(profile);
			aa_put_profile(profile);
			goto repeat;
		}
		aa_change_task_context(task, NULL, NULL, 0);
		unlock_profile(profile);
		aa_put_profile(profile);
	}
}

/*****************************
 * global subprofile functions
 ****************************/

/**
 * do_change_hat - actually switch hats
 * @hat_name: name of hat to switch to
 * @new_cxt: new aa_task_context to use on profile change
 *
 * Switch to a new hat.  Return %0 on success, error otherwise.
 */
static inline int do_change_hat(const char *hat_name,
				struct aa_task_context *new_cxt,
				u64 hat_magic)
{
	struct aa_task_context *cxt = aa_task_context(current);
	struct aa_profile *sub;
	int error = 0;

	/*
	 * Note: the profile and sub-profiles cannot go away under us here;
	 * no need to grab an additional reference count.
	 */
	sub = __aa_find_profile(hat_name, &cxt->profile->parent->sub);
	if (sub) {
		/* change hat */
		aa_change_task_context(current, new_cxt, sub, hat_magic);
	} else {
		struct aa_profile *profile = cxt->profile;

		if (APPARMOR_COMPLAIN(cxt)) {
			LOG_HINT(profile, GFP_ATOMIC, HINT_UNKNOWN_HAT,
 				"%s pid=%d "
				"profile=%s active=%s\n",
				hat_name,
				current->pid,
				profile->parent->name,
				profile->name);
		} else {
			AA_DEBUG("%s: Unknown hatname '%s'. "
				"Changing to NULL profile "
				"(%s(%d) profile %s active %s)\n",
				 __FUNCTION__,
				 hat_name,
				 current->comm, current->pid,
				 profile->parent->name,
				 profile->name);
			error = -EACCES;
		}
		/*
		 * Switch to the NULL profile: it grants no accesses, so in
		 * learning mode all accesses will get logged, and in enforce
		 * mode all accesses will be denied.
		 *
		 * In learning mode, this allows us to learn about new hats.
		 */
		aa_change_task_context(current, new_cxt,
				       cxt->profile->null_profile, hat_magic);
	}

	return error;
}

/**
 * aa_change_hat - change hat to/from subprofile
 * @hat_name: specifies hat to change to
 * @hat_magic: token to validate hat change
 *
 * Change to new @hat_name when current hat is top level profile, and store
 * the @hat_magic in the current aa_task_context.  If the new @hat_name is
 * %NULL, and the @hat_magic matches that stored in the current aa_task_context
 * return to original top level profile.  Returns %0 on success, error
 * otherwise.
 */
int aa_change_hat(const char *hat_name, u64 hat_magic)
{
	struct aa_task_context *cxt, *new_cxt;
	struct aa_profile *profile = NULL;
	int error = 0;

	/* Dump out above debugging in WARN mode if we are in AUDIT mode */
	if (APPARMOR_AUDIT(aa_task_context(current))) {
		AA_WARN(GFP_KERNEL, "%s: %s, 0x%llx (pid %d)\n",
			__FUNCTION__, hat_name ? hat_name : "NULL",
			hat_magic, current->pid);
	}

	new_cxt = aa_alloc_task_context(GFP_KERNEL);
	if (!new_cxt)
		return -ENOMEM;

	cxt = lock_task_and_profiles(current, NULL);
	if (!cxt) {
		/* An unconfined process cannot change_hat(). */
		error = -EPERM;
		goto out;
	}

	/* No need to get reference count: we do not sleep. */
	profile = cxt->profile;

	/* check to see if the confined process has any hats. */
	if (list_empty(&profile->parent->sub) && !PROFILE_COMPLAIN(profile)) {
		error = -ECHILD;
		goto out;
	}

	if (profile == profile->parent) {
		/* We are in the parent profile. */
		if (hat_name) {
			AA_DEBUG("%s: switching to %s, 0x%llx\n",
				 __FUNCTION__,
				 hat_name,
				 hat_magic);
			error = do_change_hat(hat_name, new_cxt, hat_magic);
		}
	} else {
		/*
		 * We are in a child profile.
		 *
		 * Check to make sure magic is same as what was passed when
		 * we switched into this profile.  Handle special casing of
		 * NULL magic which confines task to subprofile and prohibits
		 * further change_hats.
		 */
		if (hat_magic && hat_magic == cxt->hat_magic) {
			if (!hat_name) {
				/* Return from subprofile back to parent. */
				aa_change_task_context(current, new_cxt,
						       profile->parent, 0);
			} else {
				/*
				 * Change to another (sibling) profile, and
				 * stick with the same hat_magic.
				 */
				error = do_change_hat(hat_name, new_cxt,
						      cxt->hat_magic);
			}
		} else if (cxt->hat_magic) {
			AA_ERROR("KILLING process %s(%d) "
				 "Invalid change_hat() magic# 0x%llx "
				 "(hatname %s profile %s active %s)\n",
				 current->comm, current->pid,
				 hat_magic,
				 hat_name ? hat_name : "NULL",
				 profile->parent->name,
				 profile->name);

			/* terminate current process */
			(void)send_sig_info(SIGKILL, NULL, current);
		} else {	/* cxt->hat_magic == 0 */
			AA_ERROR("KILLING process %s(%d) "
				 "Task was confined to current subprofile "
				 "(profile %s active %s)\n",
				 current->comm, current->pid,
				 profile->parent->name,
				 profile->name);

			/* terminate current process */
			(void)send_sig_info(SIGKILL, NULL, current);
		}

	}

out:
	if (aa_task_context(current) != new_cxt)
		aa_free_task_context(new_cxt);
	task_unlock(current);
	unlock_profile(profile);
	return error;
}

/**
 * aa_replace_profile - replace a task's profile
 */
struct aa_profile *aa_replace_profile(struct task_struct *task,
				      struct aa_profile *profile,
				      u32 hat_magic)
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

	if (cxt) {
		old_profile = aa_dup_profile(cxt->profile);
		aa_change_task_context(task, new_cxt, profile, cxt->hat_magic);
	} else
		aa_change_task_context(task, new_cxt, profile, 0);

	task_unlock(task);
	unlock_both_profiles(profile, old_profile);

	return old_profile;
}

void free_aa_task_context_rcu_callback(struct rcu_head *head)
{
	struct aa_task_context *cxt;

	cxt = container_of(head, struct aa_task_context, rcu);
	aa_free_task_context(cxt);
}

/**
 * lock_task_and_profile - lock the task and confining profiles and @profile
 * @task - task to lock
 * @profile - extra profile to lock in addition to the current profile
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
		goto repeat;
	}
	rcu_read_unlock();
	return cxt;
}

/**
 * aa_change_task_context - switch a tasks to use a new context and profile
 * @task: task that is having its aa_task_context changed
 * @new_cxt: new aa_task_context to use after the switch
 * @profile: new profile to use after the switch
 * @hat_magic: hat value to switch to (0 for no hat)
 */
void aa_change_task_context(struct task_struct *task,
			    struct aa_task_context *new_cxt,
			    struct aa_profile *profile, u64 hat_magic)
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
		new_cxt->hat_magic = hat_magic;
		new_cxt->task = task;
		new_cxt->profile = aa_dup_profile(profile);
		list_move(&new_cxt->list, &profile->parent->task_contexts);
	}
	rcu_assign_pointer(task->security, new_cxt);
}

