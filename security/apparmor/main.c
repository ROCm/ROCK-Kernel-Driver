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

#include "apparmor.h"
#include "match/match.h"

#include "inline.h"

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
struct aaprofile *null_complain_profile;

/***************************
 * Private utility functions
 **************************/

/**
 * dentry_xlate_error
 * @dentry: pointer to dentry
 * @error: error number
 * @dtype: type of dentry
 *
 * Display error message when a dentry translation error occured
 */
static void dentry_xlate_error(struct dentry *dentry, int error, char *dtype)
{
	const unsigned int len = 16;
	char buf[len];

	if (dentry->d_inode) {
		snprintf(buf, len, "%lu", dentry->d_inode->i_ino);
	} else {
		strncpy(buf, "<negative>", len);
		buf[len-1]=0;
	}

	AA_ERROR("An error occured while translating %s %p "
		 "inode# %s to a pathname. Error %d\n",
		 dtype,
		 dentry,
		 buf,
		 error);
}

/**
 * aa_taskattr_access
 * @procrelname: name of file to check permission
 *
 * Determine if request is for write access to /proc/self/attr/current
 * This file is the usermode iterface for changing it's hat.
 */
static inline int aa_taskattr_access(const char *procrelname)
{
	char buf[sizeof("/attr/current") + 10];
	const int maxbuflen = sizeof(buf);
	/* assumption, 32bit pid (10 decimal digits incl \0) */

	snprintf(buf, maxbuflen, "%d/attr/current", current->pid);
	buf[maxbuflen - 1] = 0;

	return strcmp(buf, procrelname) == 0;
}

/**
 * aa_file_mode - get full mode for file entry from profile
 * @profile: profile
 * @name: filename
 */
static inline int aa_file_mode(struct aaprofile *profile, const char *name)
{
	struct aa_entry *entry;
	int mode = 0;

	AA_DEBUG("%s: %s\n", __FUNCTION__, name);
	if (!name) {
		AA_DEBUG("%s: no name\n", __FUNCTION__);
		goto out;
	}

	if (!profile) {
		AA_DEBUG("%s: no profile\n", __FUNCTION__);
		goto out;
	}
	list_for_each_entry(entry, &profile->file_entry, list) {
		if (aamatch_match(name, entry->filename,
				  entry->type, entry->extradata))
			mode |= entry->mode;
	}
out:
	return mode;
}

/**
 * aa_get_execmode - calculate what qualifier to apply to an exec
 * @active: profile to search
 * @name: name of file to exec
 * @xmod: pointer to a execution mode bit for the rule that was matched
 *         if the rule has no execuition qualifier {pui} then
 *         %AA_MAY_EXEC is returned indicating a naked x
 *         if the has an exec qualifier then only the qualifier bit {pui}
 *         is returned (%AA_MAY_EXEC) is not set.
 * @unsafe: true if secure_exec should be overriden
 * Returns %0 (false):
 *    if unable to find profile or there are conflicting pattern matches.
 *       *xmod - is not modified
 *       *unsafe - is not modified
 *
 * Returns %1 (true):
 *    if exec rule matched
 *       if the rule has an execution mode qualifier {pui} then
 *          *xmod = the execution qualifier of the rule {pui}
 *       else
 *          *xmod = %AA_MAY_EXEC
 *       unsafe = presence of unsage flag
 */
static inline int aa_get_execmode(struct aaprofile *active, const char *name,
				  int *xmod, int *unsafe)
{
	struct aa_entry *entry;
	struct aa_entry *match = NULL;

	int pattern_match_invalid = 0, rc = 0;

	/* search list of profiles with 'x' permission
	 * this will also include entries with 'p', 'u' and 'i'
	 * qualifiers.
	 *
	 * If we find a pattern match we will keep looking for an exact match
	 * If we find conflicting pattern matches we will flag (while still
	 * looking for an exact match).  If all we have is a conflict, FALSE
	 * is returned.
	 */

	list_for_each_entry(entry, &active->file_entryp[POS_AA_MAY_EXEC],
			    listp[POS_AA_MAY_EXEC]) {
		if (!pattern_match_invalid &&
		    entry->type == aa_entry_pattern &&
		    aamatch_match(name, entry->filename,
				  entry->type, entry->extradata)) {
			if (match &&
			    AA_EXEC_UNSAFE_MASK(entry->mode) !=
			    AA_EXEC_UNSAFE_MASK(match->mode))
				pattern_match_invalid = 1;
			else
				/* keep searching for an exact match */
				match = entry;
		} else if ((entry->type == aa_entry_literal ||
			    (!pattern_match_invalid &&
			     entry->type == aa_entry_tailglob)) &&
			    aamatch_match(name, entry->filename,
					  entry->type,
					  entry->extradata)) {
			if (entry->type == aa_entry_literal) {
				/* got an exact match -- there can be only
				 * one, asserted at profile load time
				 */
				match = entry;
				pattern_match_invalid = 0;
				break;
			} else {
				if (match &&
				    AA_EXEC_UNSAFE_MASK(entry->mode) !=
				    AA_EXEC_UNSAFE_MASK(match->mode))
					pattern_match_invalid = 1;
				else
					/* got a tailglob match, keep searching
					 * for an exact match
					 */
					match = entry;
			}
		}

	}

	rc = match && !pattern_match_invalid;

	if (rc) {
		int mode = AA_EXEC_MASK(match->mode);

		/* check for qualifiers, if present
		 * we just return the qualifier
		 */
		if (mode & ~AA_MAY_EXEC)
			mode = mode & ~AA_MAY_EXEC;

		*xmod = mode;
		*unsafe = (match->mode & AA_EXEC_UNSAFE);
	} else if (!match) {
		AA_DEBUG("%s: Unable to find execute entry in profile "
			 "for image '%s'\n",
			 __FUNCTION__,
			 name);
	} else if (pattern_match_invalid) {
		AA_WARN("%s: Inconsistency in profile %s. "
			"Two (or more) patterns specify conflicting exec "
			"qualifiers ('u', 'i' or 'p') for image %s\n",
			__FUNCTION__,
			active->name,
			name);
	}

	return rc;
}

/**
 * aa_filter_mask
 * @mask: requested mask
 * @inode: potential directory inode
 *
 * This fn performs pre-verification of the requested mask
 * We ignore append. Previously we required 'w' on a dir to add a file.
 * No longer. Now we require 'w' on just the file itself. Traversal 'x' is
 * also ignored for directories.
 *
 * Returned value of %0 indicates no need to perform a perm check.
 */
static inline int aa_filter_mask(int mask, struct inode *inode)
{
	if (mask) {
		int elim = MAY_APPEND;

		if (inode && S_ISDIR(inode->i_mode))
			elim |= (MAY_EXEC | MAY_WRITE);

		mask &= ~elim;
	}

	return mask;
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
 * aa_file_perm - calculate access mode for file
 * @active: profile to check against
 * @name: name of file to calculate mode for
 * @mask: permission mask requested for file
 *
 * Search the aa_entry list in @active.
 * Search looking to verify all permissions passed in mask.
 * Perform the search by looking at the partitioned list of entries, one
 * partition per permission bit.
 *
 * Return %0 on success, else mask of non-allowed permissions
 */
static unsigned int aa_file_perm(struct aaprofile *active, const char *name,
				 int mask)
{
	int i, error = 0, mode;

#define PROCPFX "/proc/"
#define PROCLEN sizeof(PROCPFX) - 1

	AA_DEBUG("%s: %s 0x%x\n", __FUNCTION__, name, mask);

	/* should not enter with other than R/W/M/X/L */
	WARN_ON(mask &
	       ~(AA_MAY_READ | AA_MAY_WRITE | AA_MAY_EXEC | AA_EXEC_MMAP |
		 AA_MAY_LINK));

	/* Special case access to /proc/self/attr/current
	 * Currently we only allow access if opened O_WRONLY
	 */
	if (mask == MAY_WRITE && strncmp(PROCPFX, name, PROCLEN) == 0 &&
	    (!list_empty(&BASE_PROFILE(active)->sub) ||
	     PROFILE_COMPLAIN(active)) && aa_taskattr_access(name + PROCLEN))
		goto done;

	mode = 0;

	/* iterate over partition, one permission bit at a time */
	for (i = 0; i <= POS_AA_FILE_MAX; i++) {
		struct aa_entry *entry;

		/* do we have to accumulate this bit?
		 * or have we already accumulated it (shortcut below)? */
		if (!(mask & (1 << i)) || mode & (1 << i))
			continue;

		list_for_each_entry(entry, &active->file_entryp[i],
				    listp[i]) {
			if (aamatch_match(name, entry->filename,
				entry->type, entry->extradata)) {
				/* Shortcut, accumulate all bits present */
				mode |= entry->mode;

				/* Mask bits are overloaded
				 * MAY_{EXEC,WRITE,READ,APPEND} are used by
				 * kernel, other values are used locally only.
				 */
				if ((mode & mask) == mask) {
					AA_DEBUG("MATCH! %s=0x%x [total mode=0x%x]\n",
						 name, mask, mode);

					goto done;
				}
			}
		}
	}

	/* return permissions not satisfied */
	error = mask & ~mode;

done:
	return error;
}

/**
 * aa_link_perm - test permission to link to a file
 * @active: profile to check against
 * @link: name of link being created
 * @target: name of target to be linked to
 *
 * Look up permission mode on both @link and @target.  @link must have same
 * permission mode as @target.  At least @link must have the link bit enabled.
 * Return %0 on success, error otherwise.
 */
static int aa_link_perm(struct aaprofile *active,
			const char *link, const char *target)
{
	int l_mode, t_mode, ret;

	l_mode = aa_file_mode(active, link);
	if (l_mode & AA_MAY_LINK) {
		/* mask off link bit */
		l_mode &= ~AA_MAY_LINK;

		t_mode = aa_file_mode(active, target);
		t_mode &= ~AA_MAY_LINK;

		ret = (l_mode == t_mode);
	} else {
		ret = 0;
	}

	return ret;
}

/**
 * _aa_perm_dentry
 * @active: profile to check against
 * @dentry: requested dentry
 * @mask: mask of requested operations
 * @pname: pointer to hold matched pathname (if any)
 *
 * Helper function.  Obtain pathname for specified dentry. Verify if profile
 * authorizes mask operations on pathname (due to lack of vfsmnt it is sadly
 * necessary to search mountpoints in namespace -- when nameidata is passed
 * more fully, this code can go away).  If more than one mountpoint matches
 * but none satisfy the profile, only the first pathname (mountpoint) is
 * returned for subsequent logging.
 *
 * Return %0 (success), +ve (mask of permissions not satisfied) or -ve (system
 * error, most likely -%ENOMEM).
 */
static int _aa_perm_dentry(struct aaprofile *active, struct dentry *dentry,
			   int mask, const char **pname)
{
	char *name = NULL, *failed_name = NULL;
	struct aa_path_data data;
	int error = 0, failed_error = 0, path_error,
	    complain = PROFILE_COMPLAIN(active);

	/* search all paths to dentry */

	aa_path_begin(dentry, &data);
	do {
		name = aa_path_getname(&data);
		if (name) {
			/* error here is 0 (success) or +ve (mask of perms) */
			error = aa_file_perm(active, name, mask);

			/* access via any path is enough */
			if (complain || error == 0)
				break; /* Caller must free name */

			/* Already have an path that failed? */
			if (failed_name) {
				aa_put_name(name);
			} else {
				failed_name = name;
				failed_error = error;
			}
		}
	} while (name);

	if ((path_error = aa_path_end(&data)) != 0) {
		dentry_xlate_error(dentry, path_error, "dentry");
		WARN_ON(name);	/* name should not be set if error */
		error = path_error;
		name = NULL;
	} else if (name) {
		if (failed_name)
			aa_put_name(failed_name);
	} else {
		name = failed_name;
		error = failed_error;
	}

	*pname = name;

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
int attach_nullprofile(struct aaprofile *profile)
{
	struct aaprofile *hat = NULL;
	char *hatname = NULL;

	hat = alloc_aaprofile();
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
	free_aaprofile(hat);

	return -ENOMEM;
}


/**
 * alloc_null_complain_profile - Allocate the global null_complain_profile.
 *
 * Return %0 (success) or error (-%ENOMEM)
 */
int alloc_null_complain_profile(void)
{
	null_complain_profile = alloc_aaprofile();
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
	/* free_aaprofile is safe for freeing partially constructed objects */
	free_aaprofile(null_complain_profile);
	null_complain_profile = NULL;

	return -ENOMEM;
}

/**
 * free_null_complain_profile - Free null profiles
 */
void free_null_complain_profile(void)
{
	put_aaprofile(null_complain_profile);
	null_complain_profile = NULL;
}

/**
 * aa_audit_message - Log a message to the audit subsystem
 * @active: profile to check against
 * @gfp: allocation flags
 * @flags: audit flags
 * @fmt: varargs fmt
 */
int aa_audit_message(struct aaprofile *active, gfp_t gfp, int flags,
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

	ret = aa_audit(active, &sa);

	va_end(sa.vaval);

	return ret;
}

/**
 * aa_audit_syscallreject - Log a syscall rejection to the audit subsystem
 * @active: profile to check against
 * @msg: string describing syscall being rejected
 * @gfp: memory allocation flags
 */
int aa_audit_syscallreject(struct aaprofile *active, gfp_t gfp,
			   const char *msg)
{
	struct aa_audit sa;

	sa.type = AA_AUDITTYPE_SYSCALL;
	sa.name = msg;
	sa.flags = 0;
	sa.gfp_mask = gfp;
	sa.error_code = 0;
	sa.result = 0; /* failure */

	return aa_audit(active, &sa);
}

/**
 * aa_audit - Log an audit event to the audit subsystem
 * @active: profile to check against
 * @sa: audit event
 */
int aa_audit(struct aaprofile *active, const struct aa_audit *sa)
{
	struct audit_buffer *ab = NULL;
	struct audit_context *ctx;

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
		if (likely(!PROFILE_AUDIT(active))) {
			/* nothing to log */
			error = 0;
			goto out;
		} else {
			audit = 1;
			logcls = "AUDITING";
		}
	} else if (sa->error_code < 0) {
		audit_log(current->audit_context, gfp_mask, AUDIT_SD,
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
		complain = PROFILE_COMPLAIN(active);
		logcls = complain ? "PERMITTING" : "REJECTING";
	}

	/* In future extend w/ per-profile flags
	 * (flags |= sa->active->flags)
	 */
	flags = sa->flags;
	if (apparmor_logsyscall)
		flags |= AA_AUDITFLAG_AUDITSS_SYSCALL;


	/* Force full audit syscall logging regardless of global setting if
	 * we are rejecting a syscall
	 */
	if (sa->type == AA_AUDITTYPE_SYSCALL) {
		ctx = current->audit_context;
	} else {
		ctx = (flags & AA_AUDITFLAG_AUDITSS_SYSCALL) ?
			current->audit_context : NULL;
	}

	ab = audit_log_start(ctx, gfp_mask, AUDIT_SD);

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
		int perm = audit ? sa->ival : sa->error_code;

		audit_log_format(ab, "%s%s%s%s%s access to %s ",
				 perm & AA_EXEC_MMAP ? "m" : "",
				 perm & AA_MAY_READ  ? "r" : "",
				 perm & AA_MAY_WRITE ? "w" : "",
				 perm & AA_MAY_EXEC  ? "x" : "",
				 perm & AA_MAY_LINK  ? "l" : "",
				 sa->name);

		opspec_error = -EPERM;

	} else if (sa->type == AA_AUDITTYPE_DIR) {
		audit_log_format(ab, "%s on %s ",
			sa->ival == aa_dir_mkdir ? "mkdir" : "rmdir",
			sa->name);

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
		const char *fmt;
		switch (sa->ival) {
			case aa_xattr_get:
				fmt = "xattr get";
				break;
			case aa_xattr_set:
				fmt = "xattr set";
				break;
			case aa_xattr_list:
				fmt = "xattr list";
				break;
			case aa_xattr_remove:
				fmt = "xattr remove";
				break;
			default:
				fmt = "xattr <unknown>";
				break;
		}

		audit_log_format(ab, "%s on %s ", fmt, sa->name);

	} else if (sa->type == AA_AUDITTYPE_LINK) {
		audit_log_format(ab,
			"link access from %s to %s ",
			sa->name,
			(char*)sa->pval);

	} else if (sa->type == AA_AUDITTYPE_CAP) {
		audit_log_format(ab,
			"access to capability '%s' ",
			capability_to_name(sa->ival));

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
			 BASE_PROFILE(active)->name, active->name);

	audit_log_end(ab);

	if (complain)
		error = 0;
	else
		error = sa->result ? 0 : opspec_error;

out:
	return error;
}

/**
 * aa_get_name - retrieve fully qualified path name
 * @dentry: relative path element
 * @mnt: where in tree
 *
 * Returns fully qualified path name on sucess, NULL on failure.
 * aa_put_name must be used to free allocated buffer.
 */
char *aa_get_name(struct dentry *dentry, struct vfsmount *mnt)
{
	char *page, *name;

	page = (char *)__get_free_page(GFP_KERNEL);
	if (!page) {
		name = ERR_PTR(-ENOMEM);
		goto out;
	}

	name = d_path(dentry, mnt, page, PAGE_SIZE);
	/* check for (deleted) that d_path appends to pathnames if the dentry
	 * has been removed from the cache.
	 * The size > deleted_size and strcmp checks are redundant safe guards.
	 */
	if (IS_ERR(name)) {
		free_page((unsigned long)page);
	} else {
		const char deleted_str[] = " (deleted)";
		const size_t deleted_size = sizeof(deleted_str) - 1;
		size_t size;
		size = strlen(name);
		if (!IS_ROOT(dentry) && d_unhashed(dentry) &&
		    size > deleted_size &&
		    strcmp(name + size - deleted_size, deleted_str) == 0)
			name[size - deleted_size] = '\0';
		AA_DEBUG("%s: full_path=%s\n", __FUNCTION__, name);
	}

out:
	return name;
}

/***********************************
 * Global permission check functions
 ***********************************/

/**
 * aa_attr - check whether attribute change allowed
 * @active: profile to check against
 * @dentry: file to check
 * @iattr: attribute changes requested
 */
int aa_attr(struct aaprofile *active, struct dentry *dentry,
	    struct iattr *iattr)
{
	int error = 0, permerror;
	struct aa_audit sa;

	sa.type = AA_AUDITTYPE_ATTR;
	sa.pval = iattr;
	sa.flags = 0;
	sa.gfp_mask = GFP_KERNEL;

	permerror = _aa_perm_dentry(active, dentry, MAY_WRITE, &sa.name);
	aa_permerror2result(permerror, &sa);

	error = aa_audit(active, &sa);

	aa_put_name(sa.name);

	return error;
}

/**
 * aa_xattr - check whether xattr attribute change allowed
 * @active: profile to check against
 * @dentry: file to check
 * @xattr: xattr to check
 * @xattroptype: type of xattr operation
 */
int aa_xattr(struct aaprofile *active, struct dentry *dentry,
	     const char *xattr, enum aa_xattroptype xattroptype)
{
	int error = 0, permerror, mask = 0;
	struct aa_audit sa;

	/* if not confined or empty mask permission granted */
	if (!active)
		goto out;

	if (xattroptype == aa_xattr_get || xattroptype == aa_xattr_list)
		mask = MAY_READ;
	else if (xattroptype == aa_xattr_set || xattroptype == aa_xattr_remove)
		mask = MAY_WRITE;

	sa.type = AA_AUDITTYPE_XATTR;
	sa.ival = xattroptype;
	sa.pval = xattr;
	sa.flags = 0;
	sa.gfp_mask = GFP_KERNEL;

	permerror = _aa_perm_dentry(active, dentry, mask, &sa.name);
	aa_permerror2result(permerror, &sa);

	error = aa_audit(active, &sa);

	aa_put_name(sa.name);

out:
	return error;
}

/**
 * aa_perm - basic apparmor permissions check
 * @active: profile to check against
 * @dentry: dentry
 * @mnt: mountpoint
 * @mask: access mode requested
 *
 * Determine if access (mask) for dentry is authorized by active
 * profile.  Result, %0 (success), -ve (error)
 */
int aa_perm(struct aaprofile *active, struct dentry *dentry,
	    struct vfsmount *mnt, int mask)
{
	int error = 0, permerror;
	struct aa_audit sa;

	if (!active)
		goto out;

	if ((mask = aa_filter_mask(mask, dentry->d_inode)) == 0)
		goto out;

	sa.type = AA_AUDITTYPE_FILE;
	sa.name = aa_get_name(dentry, mnt);
	sa.ival = mask;
	sa.flags = 0;
	sa.gfp_mask = GFP_KERNEL;

	if (IS_ERR(sa.name)) {
		permerror = PTR_ERR(sa.name);
		sa.name = NULL;
	} else {
		permerror = aa_file_perm(active, sa.name, mask);
	}

	aa_permerror2result(permerror, &sa);

	error = aa_audit(active, &sa);

	aa_put_name(sa.name);

out:
	return error;
}

/**
 * aa_perm_nameidata: interface to sd_perm accepting nameidata
 * @active: profile to check against
 * @nd: namespace data (for vfsmnt and dentry)
 * @mask: access mode requested
 */
int aa_perm_nameidata(struct aaprofile *active, struct nameidata *nd, int mask)
{
	int error = 0;

	if (nd)
		error = aa_perm(active, nd->dentry, nd->mnt, mask);

	return error;
}

/**
 * aa_perm_dentry - file permissions interface when no vfsmnt available
 * @active: profile to check against
 * @dentry: requested dentry
 * @mask: access mode requested
 *
 * Determine if access (mask) for dentry is authorized by active profile.
 * Result, %0 (success), -ve (error)
 */
int aa_perm_dentry(struct aaprofile *active, struct dentry *dentry, int mask)
{
	int error = 0, permerror;
	struct aa_audit sa;

	if (!active)
		goto out;

	if ((mask = aa_filter_mask(mask, dentry->d_inode)) == 0)
		goto out;

	sa.type = AA_AUDITTYPE_FILE;
	sa.ival = mask;
	sa.flags = 0;
	sa.gfp_mask = GFP_KERNEL;

	permerror = _aa_perm_dentry(active, dentry, mask, &sa.name);
	aa_permerror2result(permerror, &sa);

	error = aa_audit(active, &sa);

	aa_put_name(sa.name);

out:
	return error;
}

/**
 * aa_perm_dir
 * @active: profile to check against
 * @dentry: requested dentry
 * @diroptype: aa_dir_mkdir or aa_dir_rmdir
 *
 * Determine if directory operation (make/remove) for dentry is authorized
 * by @active profile.
 * Result, %0 (success), -ve (error)
 */
int aa_perm_dir(struct aaprofile *active, struct dentry *dentry,
		enum aa_diroptype diroptype)
{
	int error = 0, permerror, mask;
	struct aa_audit sa;

	WARN_ON(diroptype != aa_dir_mkdir && diroptype != aa_dir_rmdir);

	if (!active)
		goto out;

	mask = MAY_WRITE;

	sa.type = AA_AUDITTYPE_DIR;
	sa.ival = diroptype;
	sa.flags = 0;
	sa.gfp_mask = GFP_KERNEL;

	permerror = _aa_perm_dentry(active, dentry, mask, &sa.name);
	aa_permerror2result(permerror, &sa);

	error = aa_audit(active, &sa);

	aa_put_name(sa.name);

out:
	return error;
}

/**
 * aa_capability - test permission to use capability
 * @active: profile to check against
 * @cap: capability to be tested
 *
 * Look up capability in active profile capability set.
 * Return %0 (success), -%EPERM (error)
 */
int aa_capability(struct aaprofile *active, int cap)
{
	int error = 0;

	struct aa_audit sa;

	sa.type = AA_AUDITTYPE_CAP;
	sa.name = NULL;
	sa.ival = cap;
	sa.flags = 0;
	sa.error_code = 0;
	sa.result = cap_raised(active->capabilities, cap);
	sa.gfp_mask = GFP_ATOMIC;

	error = aa_audit(active, &sa);

	return error;
}

/**
 * aa_link - hard link check
 * @active: profile to check against
 * @link: dentry for link being created
 * @target: dentry for link target
 *
 * Checks link permissions for all possible name combinations.  This is
 * particularly ugly.  Returns %0 on sucess, error otherwise.
 */
int aa_link(struct aaprofile *active, struct dentry *link,
	    struct dentry *target)
{
	char *iname = NULL, *oname = NULL,
	     *failed_iname = NULL, *failed_oname = NULL;
	unsigned int result = 0;
	int error, path_error, error_code = 0, match = 0,
	    complain = PROFILE_COMPLAIN(active);
	struct aa_path_data idata, odata;
	struct aa_audit sa;

	if (!active)
		return 0;

	/* Perform nested lookup for names.
	 * This is necessary in the case where /dev/block is mounted
	 * multiple times,  i.e /dev/block->/a and /dev/block->/b
	 * This allows us to detect links where src/dest are on different
	 * mounts.   N.B no support yet for links across bind mounts of
	 * the form mount -bind /mnt/subpath /mnt2
	 *
	 * Getting direct access to vfsmounts (via nameidata) for link and
	 * target would allow all this uglyness to go away.
	 *
 	 * If more than one mountpoint matches but none satisfy the profile,
	 * only the first pathname (mountpoint) is logged.
	 */

	__aa_path_begin(target, link, &odata);
	do {
		oname = aa_path_getname(&odata);
		if (oname) {
			aa_path_begin(target, &idata);
			do {
				iname = aa_path_getname(&idata);
				if (iname) {
					result = aa_link_perm(active, oname,
							      iname);

					/* access via any path is enough */
					if (result || complain) {
						match = 1;
						break;
					}

					/* Already have an path that failed? */
					if (failed_iname) {
						aa_put_name(iname);
					} else {
						failed_iname = iname;
						failed_oname = oname;
					}
				}
			} while (iname && !match);

			/* should not be possible if we matched */
			if ((path_error = aa_path_end(&idata)) != 0) {
				dentry_xlate_error(target, path_error,
						   "inner dentry [link]");

				/* name should not be set if error */
				WARN_ON(iname);

				error_code = path_error;
			}

			/* don't release if we're saving it */
			if (!match && failed_oname != oname)
				aa_put_name(oname);
		}
	} while (oname && !match);

	if (error_code != 0) {
		/* inner error */
		(void)aa_path_end(&odata);
	} else if ((path_error = aa_path_end(&odata)) != 0) {
		dentry_xlate_error(link, path_error, "outer dentry [link]");
		error_code = path_error;
	}

	if (error_code != 0) {
		/* inner or outer error */
		result = 0;
	} else if (match) {
		result = 1;
	} else {
		/* failed to match */
		WARN_ON(iname);
		WARN_ON(oname);

		result = 0;
		iname = failed_iname;
		oname = failed_oname;
	}

	sa.type = AA_AUDITTYPE_LINK;
	sa.name = oname;	/* link */
	sa.pval = iname;	/* target */
	sa.flags = 0;
	sa.error_code = error_code;
	sa.result = result;
	sa.gfp_mask = GFP_KERNEL;

	error = aa_audit(active, &sa);

	if (failed_oname != oname)
		aa_put_name(failed_oname);
	if (failed_iname != iname)
		aa_put_name(failed_iname);

	aa_put_name(oname);
	aa_put_name(iname);

	return error;
}

/*******************************
 * Global task related functions
 *******************************/

/**
 * aa_fork - create a new subdomain
 * @p: new process
 *
 * Create a new subdomain for newly created process @p if it's parent
 * is already confined.  Otherwise a subdomain will be lazily allocated
 * will get one with NULL values.  Return 0 on sucess.
 * for the child if it subsequently execs (in aa_register).
 * Return 0 on sucess.
 *
 * The sd_lock is used to maintain consistency against profile
 * replacement/removal.
 */

int aa_fork(struct task_struct *p)
{
	struct subdomain *sd = AA_SUBDOMAIN(current->security);
	struct subdomain *newsd = NULL;

	AA_DEBUG("%s\n", __FUNCTION__);

	if (__aa_is_confined(sd)) {
		unsigned long flags;

		newsd = alloc_subdomain(p);

		if (!newsd)
			return -ENOMEM;

		/* Use locking here instead of getting the reference
		 * because we need both the old reference and the
		 * new reference to be consistent.
		 */
		spin_lock_irqsave(&sd_lock, flags);
		aa_switch(newsd, sd->active);
		newsd->hat_magic = sd->hat_magic;
		spin_unlock_irqrestore(&sd_lock, flags);

		if (SUBDOMAIN_COMPLAIN(sd) &&
		    sd->active == null_complain_profile)
			LOG_HINT(sd->active, GFP_KERNEL, HINT_FORK,
				"pid=%d child=%d\n",
				current->pid, p->pid);
	}
	p->security = newsd;
	return 0;
}

/**
 * aa_register - register a new program
 * @bprm: binprm of program being registered
 *
 * Try to register a new program during execve().  This should give the
 * new program a valid subdomain.
 */
int aa_register(struct linux_binprm *bprm)
{
	char *filename;
	struct file *filp = bprm->file;
	struct aaprofile *active;
	struct aaprofile *newprofile = NULL, unconstrained_flag;
	int 	error = -ENOMEM,
		exec_mode = 0,
		find_profile = 0,
		find_profile_mandatory = 0,
	        unsafe_exec = 0,
		complain = 0;

	AA_DEBUG("%s\n", __FUNCTION__);

	filename = aa_get_name(filp->f_dentry, filp->f_vfsmnt);
	if (IS_ERR(filename)) {
		AA_WARN("%s: Failed to get filename\n", __FUNCTION__);
		goto out;
	}

	error = 0;

	active = get_active_aaprofile();

	if (!active) {
		/* Unconfined task, load profile if it exists */
		find_profile = 1;
		goto find_profile;
	}

	complain = PROFILE_COMPLAIN(active);

	/* Confined task, determine what mode inherit, unconstrained or
	 * mandatory to load new profile
	 */
	if (aa_get_execmode(active, filename, &exec_mode, &unsafe_exec)) {
		switch (exec_mode) {
		case AA_EXEC_INHERIT:
			/* do nothing - setting of profile
			 * already handed in aa_fork
			 */
			AA_DEBUG("%s: INHERIT %s\n",
				 __FUNCTION__,
				 filename);
			break;

		case AA_EXEC_UNCONSTRAINED:
			AA_DEBUG("%s: UNCONSTRAINED %s\n",
				 __FUNCTION__,
				 filename);

			/* unload profile */
			newprofile = &unconstrained_flag;
			break;

		case AA_EXEC_PROFILE:
			AA_DEBUG("%s: PROFILE %s\n",
				 __FUNCTION__,
				 filename);

			find_profile = 1;
			find_profile_mandatory = 1;
			break;

		case AA_MAY_EXEC:
			/* this should not happen, entries
			 * with just EXEC only should be
			 * rejected at profile load time
			 */
			AA_ERROR("%s: Rejecting exec(2) of image '%s'. "
				"AA_MAY_EXEC without exec qualifier invalid "
				"(%s(%d) profile %s active %s\n",
				 __FUNCTION__,
				 filename,
				 current->comm, current->pid,
				 BASE_PROFILE(active)->name, active->name);
			error = -EPERM;
			break;

		default:
			AA_ERROR("%s: Rejecting exec(2) of image '%s'. "
				 "Unknown exec qualifier %x "
				 "(%s (pid %d) profile %s active %s)\n",
				 __FUNCTION__,
				 filename,
				 exec_mode,
				 current->comm, current->pid,
				 BASE_PROFILE(active)->name, active->name);
			error = -EPERM;
			break;
		}

	} else if (complain) {
		/* There was no entry in calling profile
		 * describing mode to execute image in.
		 * Drop into null-profile (disabling secure exec).
		 */
		newprofile = get_aaprofile(null_complain_profile);
		unsafe_exec = 1;
	} else {
		AA_WARN("%s: Rejecting exec(2) of image '%s'. "
			"Unable to determine exec qualifier "
			"(%s (pid %d) profile %s active %s)\n",
			__FUNCTION__,
			filename,
			current->comm, current->pid,
			BASE_PROFILE(active)->name, active->name);
		error = -EPERM;
	}


find_profile:
	if (!find_profile)
		goto apply_profile;

	/* Locate new profile */
	newprofile = aa_profilelist_find(filename);
	if (newprofile) {
		AA_DEBUG("%s: setting profile %s\n",
			 __FUNCTION__, newprofile->name);
	} else if (find_profile_mandatory) {
		/* Profile (mandatory) could not be found */

		if (complain) {
			LOG_HINT(active, GFP_KERNEL, HINT_MANDPROF,
				"image=%s pid=%d profile=%s active=%s\n",
				filename,
				current->pid,
				BASE_PROFILE(active)->name, active->name);

			newprofile = get_aaprofile(null_complain_profile);
		} else {
			AA_WARN("REJECTING exec(2) of image '%s'. "
				"Profile mandatory and not found "
				"(%s(%d) profile %s active %s)\n",
				filename,
				current->comm, current->pid,
				BASE_PROFILE(active)->name, active->name);
			error = -EPERM;
		}
	} else {
		/* Profile (non-mandatory) could not be found */

		/* Only way we can get into this code is if task
		 * is unconstrained.
		 */

		WARN_ON(active);

		AA_DEBUG("%s: No profile found for exec image %s\n",
			 __FUNCTION__,
			 filename);
	} /* newprofile */


apply_profile:
	/* Apply profile if necessary */
	if (newprofile) {
		struct subdomain *sd, *lazy_sd = NULL;
		unsigned long flags;

		if (newprofile == &unconstrained_flag)
			newprofile = NULL;

		/* grab a lock - this is to guarentee consistency against
		 * other writers of subdomain (replacement/removal)
		 *
		 * Several things may have changed since the code above
		 *
		 * - Task may be presently unconfined (have no sd). In which
		 *   case we have to lazily allocate one.  Note we may be raced
		 *   to this allocation by a setprofile.
		 *
		 * - If we are a confined process, active is a refcounted copy
		 *   of the profile that was on the subdomain at entry.
		 *   This allows us to not have to hold a lock around
		 *   all this code.   If profile replacement has taken place
		 *   our active may not equal sd->active any more.
		 *   This is okay since the operation is treated as if
		 *   the transition occured before replacement.
		 *
		 * - If newprofile points to an actual profile (result of
		 *   aa_profilelist_find above), this profile may have been
		 *   replaced.  We need to fix it up.  Doing this to avoid
		 *   having to hold a lock around all this code.
		 */

		if (!active && !(sd = AA_SUBDOMAIN(current->security))) {
			lazy_sd = alloc_subdomain(current);
			if (!lazy_sd) {
				AA_ERROR("%s: Failed to allocate subdomain\n",
					 __FUNCTION__);
				error = -ENOMEM;
				goto cleanup;
			}
		}

		spin_lock_irqsave(&sd_lock, flags);

		sd = AA_SUBDOMAIN(current->security);
		if (lazy_sd) {
			if (sd) {
				/* raced by setprofile - created sd */
				free_subdomain(lazy_sd);
				lazy_sd = NULL;
			} else {
				/* Not rcu used to get the write barrier
				 * correct */
				rcu_assign_pointer(current->security, lazy_sd);
				sd = lazy_sd;
			}
		}

		/* Determine if profile we found earlier is stale.
		 * If so, reobtain it.  N.B stale flag should never be
		 * set on null_complain profile.
		 */
		if (newprofile && unlikely(newprofile->isstale)) {
			WARN_ON(newprofile == null_complain_profile);

			/* drop refcnt obtained from earlier get_aaprofile */
			put_aaprofile(newprofile);

			newprofile = aa_profilelist_find(filename);

			if (!newprofile) {
				/* Race, profile was removed, not replaced.
				 * Redo with error checking
				 */
				spin_unlock_irqrestore(&sd_lock, flags);
				goto find_profile;
			}
		}

		/* Handle confined exec.
		 * Can be at this point for the following reasons:
		 * 1. unconfined switching to confined
		 * 2. confined switching to different confinement
		 * 3. confined switching to unconfined
		 *
		 * Cases 2 and 3 are marked as requiring secure exec
		 * (unless policy specified "unsafe exec")
		 */
		if (__aa_is_confined(sd) && !unsafe_exec) {
			unsigned long bprm_flags;

			bprm_flags = AA_SECURE_EXEC_NEEDED;
			bprm->security = (void*)
				((unsigned long)bprm->security | bprm_flags);
		}

		aa_switch(sd, newprofile);
		put_aaprofile(newprofile);

		if (complain && newprofile == null_complain_profile)
			LOG_HINT(newprofile, GFP_ATOMIC, HINT_CHGPROF,
				"pid=%d\n",
				current->pid);

		spin_unlock_irqrestore(&sd_lock, flags);
	}

cleanup:
	aa_put_name(filename);

	put_aaprofile(active);

out:
	return error;
}

/**
 * aa_release - release the task's subdomain
 * @p: task being released
 *
 * This is called after a task has exited and the parent has reaped it.
 * @p->security blob is freed.
 *
 * This is the one case where we don't need to hold the sd_lock before
 * removing a profile from a subdomain.  Once the subdomain has been
 * removed from the subdomain_list, we are no longer racing other writers.
 * There may still be other readers so we must still use aa_switch
 * to put the subdomain's reference safely.
 */
void aa_release(struct task_struct *p)
{
	struct subdomain *sd = AA_SUBDOMAIN(p->security);
	if (sd) {
		p->security = NULL;

		aa_subdomainlist_remove(sd);
		aa_switch_unconfined(sd);

		kfree(sd);
	}
}

/*****************************
 * global subprofile functions
 ****************************/

/**
 * do_change_hat - actually switch hats
 * @hat_name: name of hat to swtich to
 * @sd: current subdomain
 *
 * Switch to a new hat.  Return %0 on success, error otherwise.
 */
static inline int do_change_hat(const char *hat_name, struct subdomain *sd)
{
	struct aaprofile *sub;
	int error = 0;

	sub = __aa_find_profile(hat_name, &BASE_PROFILE(sd->active)->sub);

	if (sub) {
		/* change hat */
		aa_switch(sd, sub);
		put_aaprofile(sub);
	} else {
		/* There is no such subprofile change to a NULL profile.
		 * The NULL profile grants no file access.
		 *
		 * This feature is used by changehat_apache.
		 *
		 * N.B from the null-profile the task can still changehat back
		 * out to the parent profile (assuming magic != NULL)
		 */
		if (SUBDOMAIN_COMPLAIN(sd)) {
			LOG_HINT(sd->active, GFP_ATOMIC, HINT_UNKNOWN_HAT,
 				"%s pid=%d "
				"profile=%s active=%s\n",
				hat_name,
				current->pid,
				BASE_PROFILE(sd->active)->name,
				sd->active->name);
		} else {
			AA_DEBUG("%s: Unknown hatname '%s'. "
				"Changing to NULL profile "
				"(%s(%d) profile %s active %s)\n",
				 __FUNCTION__,
				 hat_name,
				 current->comm, current->pid,
				 BASE_PROFILE(sd->active)->name,
				 sd->active->name);
			error = -EACCES;
		}
		aa_switch(sd, sd->active->null_profile);
	}

	return error;
}

/**
 * aa_change_hat - change hat to/from subprofile
 * @hat_name: specifies hat to change to
 * @hat_magic: token to validate hat change
 *
 * Change to new @hat_name when current hat is top level profile, and store
 * the @hat_magic in the current subdomain.  If the new @hat_name is
 * %NULL, and the @hat_magic matches that stored in the current subdomain
 * return to original top level profile.  Returns %0 on success, error
 * otherwise.
 */
int aa_change_hat(const char *hat_name, u32 hat_magic)
{
	struct subdomain *sd = AA_SUBDOMAIN(current->security);
	int error = 0;

	AA_DEBUG("%s: %p, 0x%x (pid %d)\n",
		 __FUNCTION__,
		 hat_name, hat_magic,
		 current->pid);

	/* Dump out above debugging in WARN mode if we are in AUDIT mode */
	if (SUBDOMAIN_AUDIT(sd)) {
		AA_WARN("%s: %s, 0x%x (pid %d)\n",
			__FUNCTION__, hat_name ? hat_name : "NULL",
			hat_magic, current->pid);
	}

	/* check to see if an unconfined process is doing a changehat. */
	if (!__aa_is_confined(sd)) {
		error = -EPERM;
		goto out;
	}

	/* Check whether current domain is parent
	 * or one of the sibling children
	 */
	if (!IN_SUBPROFILE(sd->active)) {
		/*
		 * parent
		 */
		if (hat_name) {
			AA_DEBUG("%s: switching to %s, 0x%x\n",
				 __FUNCTION__,
				 hat_name,
				 hat_magic);

			/*
			 * N.B hat_magic == 0 has a special meaning
			 * this indicates that the task may never changehat
			 * back to it's parent, it will stay in this subhat
			 * (or null-profile, if the hat doesn't exist) until
			 * the task terminates
			 */
			sd->hat_magic = hat_magic;
			error = do_change_hat(hat_name, sd);
		} else {
			/* Got here via changehat(NULL, magic)
			 *
			 * We used to simply update the magic cookie.
			 * That's an odd behaviour, so just do nothing.
			 */
		}
	} else {
		/*
		 * child -- check to make sure magic is same as what was
		 * passed when we switched into this profile,
		 * Handle special casing of NULL magic which confines task
		 * to subprofile and prohibits further changehats
		 */
		if (hat_magic == sd->hat_magic && sd->hat_magic) {
			if (!hat_name) {
				/*
				 * Got here via changehat(NULL, magic)
				 * Return from subprofile, back to parent
				 */
				aa_switch(sd, sd->active->parent);

				/* Reset hat_magic to zero.
				 * New value will be passed on next changehat
				 */
				sd->hat_magic = 0;
			} else {
				/* change to another (sibling) profile */
				error = do_change_hat(hat_name, sd);
			}
		} else if (sd->hat_magic) {
			AA_ERROR("KILLING process %s(%d) "
				 "Invalid change_hat() magic# 0x%x "
				 "(hatname %s profile %s active %s)\n",
				 current->comm, current->pid,
				 hat_magic,
				 hat_name ? hat_name : "NULL",
				 BASE_PROFILE(sd->active)->name,
				 sd->active->name);

			/* terminate current process */
			(void)send_sig_info(SIGKILL, NULL, current);
		} else {	/* sd->hat_magic == NULL */
			AA_ERROR("KILLING process %s(%d) "
				 "Task was confined to current subprofile "
				 "(profile %s active %s)\n",
				 current->comm, current->pid,
				 BASE_PROFILE(sd->active)->name,
				 sd->active->name);

			/* terminate current process */
			(void)send_sig_info(SIGKILL, NULL, current);
		}

	}

out:
	return error;
}
