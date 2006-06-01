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
#include "aamatch/match.h"

#include "inline.h"

/* NULL profile
 *
 * Used when an attempt is made to changehat into a non-existant
 * subhat.   In the NULL profile,  no file access is allowed
 * (currently full network access is allowed).  Using a NULL
 * profile ensures that active is always non zero.
 *
 * Leaving the NULL profile is by either successfully changehatting
 * into a sibling hat, or changehatting back to the parent (NULL hat).
 */
struct sdprofile *null_profile;

/* NULL complain profile
 *
 * Used when in complain mode, to emit Permitting messages for non-existant
 * profiles and hats.  This is necessary because of selective mode, in which
 * case we need a complain null_profile and enforce null_profile
 *
 * The null_complain_profile cannot be statically allocated, because it
 * can be associated to files which keep their reference even if subdomain is
 * unloaded
 */
struct sdprofile *null_complain_profile;

/***************************
 * PRIVATE UTILITY FUNCTIONS
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

	SD_ERROR("An error occured while translating %s %p "
		 "inode# %s to a pathname. Error %d\n",
		 dtype,
		 dentry,
		 buf,
		 error);
}

/**
 * sd_taskattr_access:
 * @name: name of file to check permission
 * @mask: permission mask requested for file
 *
 * Determine if request is for write access to /proc/self/attr/current
 */
static inline int sd_taskattr_access(const char *procrelname)
{
/*
 * assumes a 32bit pid, which requires max 10 decimal digits to represent
 * sizeof includes trailing \0
 */
	char buf[sizeof("/attr/current") + 10];
	const int maxbuflen = sizeof(buf);

	snprintf(buf, maxbuflen, "%d/attr/current", current->pid);
	buf[maxbuflen - 1] = 0;

	return strcmp(buf, procrelname) == 0;
}

/**
 * sd_file_mode - get full mode for file entry from profile
 * @profile: profile
 * @name: filename
 */
static inline int sd_file_mode(struct sdprofile *profile, const char *name)
{
	struct sd_entry *entry;
	int mode = 0;

	SD_DEBUG("%s: %s\n", __FUNCTION__, name);
	if (!name) {
		SD_DEBUG("%s: no name\n", __FUNCTION__);
		goto out;
	}

	if (!profile) {
		SD_DEBUG("%s: no profile\n", __FUNCTION__);
		goto out;
	}
	list_for_each_entry(entry, &profile->file_entry, list) {
		if (sdmatch_match(name, entry->filename,
				  entry->entry_type, entry->extradata))
			mode |= entry->mode;
	}
out:
	return mode;
}

/**
 * sd_get_execmode - calculate what qualifier to apply to an exec
 * @sd: subdomain to search
 * @name: name of file to exec
 * @xmod: pointer to a execution mode bit for the rule that was matched
 *         if the rule has no execuition qualifier {pui} then
 *         SD_MAY_EXEC is returned indicating a naked x
 *         if the has an exec qualifier then only the qualifier bit {pui}
 *         is returned (SD_MAY_EXEC) is not set.
 * @unsafe: true if secure_exec should be overridden
 *
 * Returns 0 (false):
 *    if unable to find profile or there are conflicting pattern matches.
 *       *xmod - is not modified
 *       *unsafe - is not modified
 *
 * Returns 1 (true):
 *    if not confined
 *       *xmod = SD_MAY_EXEC
 *       *unsafe = 0
 *    if exec rule matched
 *       if the rule has an execution mode qualifier {pui} then
 *          *xmod = the execution qualifier of the rule {pui}
 *       else
 *          *xmod = SD_MAY_EXEC
 *       unsafe = presence of unsafe flag
 */
static inline int sd_get_execmode(struct subdomain *sd, const char *name,
				  int *xmod, int *unsafe)
{
	struct sdprofile *profile;
	struct sd_entry *entry;
	struct sd_entry *match = NULL;

	int pattern_match_invalid = 0, rc = 0;

	/* not confined */
	if (!__sd_is_confined(sd)) {
		SD_DEBUG("%s: not confined\n", __FUNCTION__);
		goto not_confined;
	}

	profile = sd->active;

	/* search list of profiles with 'x' permission
	 * this will also include entries with 'p', 'u' and 'i'
	 * qualifiers.
	 *
	 * If we find a pattern match we will keep looking for an exact match
	 * If we find conflicting pattern matches we will flag (while still
	 * looking for an exact match).  If all we have is a conflict, FALSE
	 * is returned.
	 */

	list_for_each_entry(entry, &profile->file_entryp[POS_SD_MAY_EXEC],
			    listp[POS_SD_MAY_EXEC]) {
		if (!pattern_match_invalid &&
		    entry->entry_type == sd_entry_pattern &&
		    sdmatch_match(name, entry->filename,
				  entry->entry_type, entry->extradata)) {
			if (match &&
			    SD_EXEC_UNSAFE_MASK(entry->mode) !=
			    SD_EXEC_UNSAFE_MASK(match->mode))
				pattern_match_invalid = 1;
			else
				/* keep searching for an exact match */
				match = entry;
		} else if ((entry->entry_type == sd_entry_literal ||
			    (!pattern_match_invalid &&
			     entry->entry_type == sd_entry_tailglob)) &&
			    sdmatch_match(name, entry->filename,
					  entry->entry_type,
					  entry->extradata)) {
			if (entry->entry_type == sd_entry_literal) {
				/* got an exact match -- there can be only
				 * one, asserted at profile load time
				 */
				match = entry;
				pattern_match_invalid = 0;
				break;
			} else {
				if (match &&
				    SD_EXEC_UNSAFE_MASK(entry->mode) !=
				    SD_EXEC_UNSAFE_MASK(match->mode))
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
		int mode = SD_EXEC_MASK(match->mode);

		/* check for qualifiers, if present
		 * we just return the qualifier
		 */
		if (mode & ~SD_MAY_EXEC)
			mode = mode & ~SD_MAY_EXEC;

		*xmod = mode;
		*unsafe = (match->mode & SD_EXEC_UNSAFE);
	} else if (!match) {
		SD_DEBUG("%s: Unable to find execute entry in profile "
			 "for image '%s'\n",
			 __FUNCTION__,
			 name);
	} else if (pattern_match_invalid) {
		SD_WARN("%s: Inconsistency in profile %s. "
			"Two (or more) patterns specify conflicting exec "
			"qualifiers ('u', 'i' or 'p') for image %s\n",
			__FUNCTION__,
			sd->active->name,
			name);
	}

	return rc;

not_confined:
	*xmod = SD_MAY_EXEC;
	*unsafe = 0;
	return 1;
}

/**
 * sd_filter_mask
 * @mask: requested mask
 * @inode: potential directory inode
 *
 * This fn performs pre-verification of the requested mask
 * We ignore append. Previously we required 'w' on a dir to add a file.
 * No longer. Now we require 'w' on just the file itself. Traversal 'x' is
 * also ignored for directories.
 *
 * Returned value of 0 indicates no need to perform a perm check.
 */
static inline int sd_filter_mask(int mask, struct inode *inode)
{
	if (mask) {
		int elim = MAY_APPEND;

		if (inode && S_ISDIR(inode->i_mode))
			elim |= (MAY_EXEC | MAY_WRITE);

		mask &= ~elim;
	}

	return mask;
}

static inline void sd_permerror2result(int perm_result, struct sd_audit *sa)
{
	if (perm_result == 0) {	/* success */
		sa->result = 1;
		sa->errorcode = 0;
	} else { /* -ve internal error code or +ve mask of denied perms */
		sa->result = 0;
		sa->errorcode = perm_result;
	}
}

/*************************
 * MAIN INTERNAL FUNCTIONS
 ************************/

/**
 * sd_file_perm - calculate access mode for file
 * @subdomain: current subdomain
 * @name: name of file to calculate mode for
 * @mask: permission mask requested for file
 *
 * Search the sd_entry list in @profile.
 * Search looking to verify all permissions passed in mask.
 * Perform the search by looking at the partitioned list of entries, one
 * partition per permission bit.
 *
 * Return 0 on success, else mask of non-allowed permissions
 */
static unsigned int sd_file_perm(struct subdomain *sd, const char *name,
				 int mask)
{
	struct sdprofile *profile;
	int i, error = 0, mode;

#define PROCPFX "/proc/"
#define PROCLEN sizeof(PROCPFX) - 1

	SD_DEBUG("%s: %s 0x%x\n", __FUNCTION__, name, mask);

	/* should not enter with other than R/W/M/X/L */
	BUG_ON(mask &
	       ~(SD_MAY_READ | SD_MAY_WRITE | SD_MAY_EXEC |
		 SD_EXEC_MMAP | SD_MAY_LINK));

	/* not confined */
	if (!__sd_is_confined(sd)) {
		/* exit with access allowed */
		SD_DEBUG("%s: not confined\n", __FUNCTION__);
		goto done;
	}

	/* Special case access to /proc/self/attr/current
	 * Currently we only allow access if opened O_WRONLY
	 */
	if (mask == MAY_WRITE && strncmp(PROCPFX, name, PROCLEN) == 0 &&
	    (!list_empty(&sd->profile->sub) || SUBDOMAIN_COMPLAIN(sd)) &&
	    sd_taskattr_access(name + PROCLEN))
		goto done;

	profile = sd->active;

	mode = 0;

	/* iterate over partition, one permission bit at a time */
	for (i = 0; i <= POS_SD_FILE_MAX; i++) {
		struct sd_entry *entry;

		/* do we have to accumulate this bit?
		 * or have we already accumulated it (shortcut below)? */
		if (!(mask & (1 << i)) || mode & (1 << i))
			continue;

		list_for_each_entry(entry, &profile->file_entryp[i],
				    listp[i]) {
			if (sdmatch_match(name, entry->filename,
				entry->entry_type, entry->extradata)) {
				/* Shortcut, accumulate all bits present */
				mode |= entry->mode;

				/*
				 * Mask bits are overloaded
				 * MAY_{EXEC,WRITE,READ,APPEND} are used by
				 * kernel, other values are used locally only.
				 */
				if ((mode & mask) == mask) {
					SD_DEBUG("MATCH! %s=0x%x [total mode=0x%x]\n",
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
 * sd_link_perm - test permission to link to a file
 * @sd: current subdomain
 * @link: name of link being created
 * @target: name of target to be linked to
 *
 * Look up permission mode on both @link and @target.  @link must have same
 * permission mode as @target.  At least @link must have the link bit enabled.
 * Return 0 on success, error otherwise.
 */
static int sd_link_perm(struct subdomain *sd,
			const char *link, const char *target)
{
	int l_mode, t_mode, ret;
	struct sdprofile *profile = sd->active;

	l_mode = sd_file_mode(profile, link);
	if (l_mode & SD_MAY_LINK) {
		/* mask off link bit */
		l_mode &= ~SD_MAY_LINK;

		t_mode = sd_file_mode(profile, target);
		t_mode &= ~SD_MAY_LINK;

		ret = (l_mode == t_mode);
	} else {
		ret = 0;
	}

	return ret;
}

/**
 * _sd_perm_dentry
 * @sd: current subdomain
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
 * Return 0 (success), +ve (mask of permissions not satisfied) or -ve (system
 * error, most likely -ENOMEM).
 */
static int _sd_perm_dentry(struct subdomain *sd, struct dentry *dentry,
			   int mask, const char **pname)
{
	char *name = NULL, *failed_name = NULL;
	struct sd_path_data data;
	int error = 0, failed_error = 0, sdpath_error,
	    sdcomplain = SUBDOMAIN_COMPLAIN(sd);

	/* search all paths to dentry */

	sd_path_begin(dentry, &data);
	do {
		name = sd_path_getname(&data);
		if (name) {
			/* error here is 0 (success) or +ve (mask of perms) */
			error = sd_file_perm(sd, name, mask);

			/* access via any path is enough */
			if (sdcomplain || error == 0)
				break; /* Caller must free name */

			/* Already have an path that failed? */
			if (failed_name) {
				sd_put_name(name);
			} else {
				failed_name = name;
				failed_error = error;
			}
		}
	} while (name);

	if ((sdpath_error = sd_path_end(&data)) != 0) {
		dentry_xlate_error(dentry, sdpath_error, "dentry");

		WARN_ON(name);	/* name should not be set if error */
		error = sdpath_error;
		name = NULL;
	} else if (name) {
		if (failed_name)
			sd_put_name(failed_name);
	} else {
		name = failed_name;
		error = failed_error;
	}

	*pname = name;

	return error;
}

/**************************
 * GLOBAL UTILITY FUNCTIONS
 *************************/

/**
 * alloc_nullprofiles - Allocate null profiles
 */
int alloc_nullprofiles(void)
{
	null_profile = alloc_sdprofile();
	null_complain_profile = alloc_sdprofile();

	if (!null_profile || !null_complain_profile)
		goto fail;

	null_profile->name = kstrdup("null-profile", GFP_KERNEL);
	null_complain_profile->name =
		kstrdup("null-complain-profile", GFP_KERNEL);

	if (!null_profile->name ||
	    !null_complain_profile->name)
		goto fail;

	get_sdprofile(null_profile);
	get_sdprofile(null_complain_profile);
	null_complain_profile->flags.complain = 1;

	return 1;

fail:
	/* free_sdprofile is safe for freeing partially constructed objects */
	free_sdprofile(null_profile);
	free_sdprofile(null_complain_profile);
	null_profile = null_complain_profile = NULL;
	return 0;
}

/**
 * free_nullprofiles - Free null profiles
 */
void free_nullprofiles(void)
{
	put_sdprofile(null_complain_profile);
	put_sdprofile(null_profile);
	null_profile = null_complain_profile = NULL;
}

/**
 * sd_audit_message - Log a message to the audit subsystem
 * @sd: current subdomain
 * @gfp: allocation flags
 * @flags: audit flags
 * @fmt: varargs fmt
 */
int sd_audit_message(struct subdomain *sd, unsigned int gfp, int flags,
		     const char *fmt, ...)
{
	int ret;
	struct sd_audit sa;

	sa.type = SD_AUDITTYPE_MSG;
	sa.name = fmt;
	va_start(sa.vaval, fmt);
	sa.flags = flags;
	sa.gfp_mask = gfp;
	sa.errorcode = 0;
	sa.result = 0;	/* fake failure: force message to be logged */

	ret = sd_audit(sd, &sa);

	va_end(sa.vaval);

	return ret;
}

/**
 * sd_audit_syscallreject - Log a syscall rejection to the audit subsystem
 * @sd: current subdomain
 * @msg: string describing syscall being rejected
 * @gfp: memory allocation flags
 */
int sd_audit_syscallreject(struct subdomain *sd, unsigned int gfp,
			   const char *msg)
{
	struct sd_audit sa;

	sa.type = SD_AUDITTYPE_SYSCALL;
	sa.name = msg;
	sa.flags = 0;
	sa.gfp_mask = gfp;
	sa.errorcode = 0;
	sa.result = 0; /* failure */

	return sd_audit(sd, &sa);
}

/**
 * sd_audit - Log an audit event to the audit subsystem
 * @sd: current subdomain
 * @sa: audit event
 */
int sd_audit(struct subdomain *sd, const struct sd_audit *sa)
{
	struct audit_buffer *ab = NULL;
	struct audit_context *ctx;

	const char *logcls;
	unsigned int flags;
	int sdaudit = 0,
	    sdcomplain = 0,
	    error = -EINVAL,
	    opspec_error = -EACCES;

	const unsigned int gfp_mask = sa->gfp_mask;

	WARN_ON(sa->type >= SD_AUDITTYPE__END);

	/*
	 * sa->result:	  1 success, 0 failure
	 * sa->errorcode: success: 0
	 *		  failure: +ve mask of failed permissions or -ve
	 *		  system error
	 */

	if (likely(sa->result)) {
		if (likely(!SUBDOMAIN_AUDIT(sd))) {
			/* nothing to log */
			error = 0;
			goto out;
		} else {
			sdaudit = 1;
			logcls = "AUDITING";
		}
	} else if (sa->errorcode < 0) {
		audit_log(current->audit_context, gfp_mask, AUDIT_SD,
			"Internal error auditing event type %d (error %d)",
			sa->type, sa->errorcode);
		SD_ERROR("Internal error auditing event type %d (error %d)\n",
			sa->type, sa->errorcode);
		error = sa->errorcode;
		goto out;
	} else if (sa->type == SD_AUDITTYPE_SYSCALL) {
		/* Currently SD_AUDITTYPE_SYSCALL is for rejects only.
		 * Values set by sd_audit_syscallreject will get us here.
		 */
		logcls = "REJECTING";
	} else {
		sdcomplain = SUBDOMAIN_COMPLAIN(sd);
		logcls = sdcomplain ? "PERMITTING" : "REJECTING";
	}

	/* In future extend w/ per-profile flags
	 * (flags |= sa->active->flags)
	 */
	flags = sa->flags;
	if (subdomain_logsyscall)
		flags |= SD_AUDITFLAG_AUDITSS_SYSCALL;


	/* Force full audit syscall logging regardless of global setting if
	 * we are rejecting a syscall
	 */
	if (sa->type == SD_AUDITTYPE_SYSCALL) {
		ctx = current->audit_context;
	} else {
		ctx = (flags & SD_AUDITFLAG_AUDITSS_SYSCALL) ?
			current->audit_context : NULL;
	}

	ab = audit_log_start(ctx, gfp_mask, AUDIT_SD);

	if (!ab) {
		SD_ERROR("Unable to log event (%d) to audit subsys\n",
			sa->type);
		if (sdcomplain)
			error = 0;
		goto out;
	}

	/* messages get special handling */
	if (sa->type == SD_AUDITTYPE_MSG) {
		audit_log_vformat(ab, sa->name, sa->vaval);
		audit_log_end(ab);
		error = 0;
		goto out;
	}

	/* log operation */

	audit_log_format(ab, "%s ", logcls);	/* REJECTING/ALLOWING/etc */

	if (sa->type == SD_AUDITTYPE_FILE) {
		int perm = sdaudit ? sa->ival : sa->errorcode;

		audit_log_format(ab, "%s%s%s%s%s access to %s ",
			perm & SD_EXEC_MMAP ? "m" : "",
			perm & SD_MAY_READ  ? "r" : "",
			perm & SD_MAY_WRITE ? "w" : "",
			perm & SD_MAY_EXEC  ? "x" : "",
			perm & SD_MAY_LINK  ? "l" : "",
			sa->name);

		opspec_error = -EPERM;

	} else if (sa->type == SD_AUDITTYPE_DIR) {
		audit_log_format(ab, "%s on %s ",
			sa->ival == SD_DIR_MKDIR ? "mkdir" : "rmdir",
			sa->name);

	} else if (sa->type == SD_AUDITTYPE_ATTR) {
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

	} else if (sa->type == SD_AUDITTYPE_XATTR) {
		const char *fmt;
		switch (sa->ival) {
			case SD_XATTR_GET:
				fmt = "xattr get";
				break;
			case SD_XATTR_SET:
				fmt = "xattr set";
				break;
			case SD_XATTR_LIST:
				fmt = "xattr list";
				break;
			case SD_XATTR_REMOVE:
				fmt = "xattr remove";
				break;
			default:
				fmt = "xattr <unknown>";
				break;
		}

		audit_log_format(ab, "%s on %s ", fmt, sa->name);

	} else if (sa->type == SD_AUDITTYPE_LINK) {
		audit_log_format(ab,
			"link access from %s to %s ",
			sa->name,
			(char*)sa->pval);

	} else if (sa->type == SD_AUDITTYPE_CAP) {
		audit_log_format(ab,
			"access to capability '%s' ",
			capability_to_name(sa->ival));

		opspec_error = -EPERM;
	} else if (sa->type == SD_AUDITTYPE_SYSCALL) {
		audit_log_format(ab, "access to syscall '%s' ", sa->name);

		opspec_error = -EPERM;
	} else {
		/* -EINVAL -- will WARN_ON above */
		goto out;
	}

	audit_log_format(ab, "(%s(%d) profile %s active %s)",
		current->comm, current->pid,
		sd->profile->name, sd->active->name);

	audit_log_end(ab);

	if (sdcomplain)
		error = 0;
	else
		error = sa->result ? 0 : opspec_error;

out:
	return error;
}

/**
 * sd_get_name - retrieve fully qualified path name
 * @dentry: relative path element
 * @mnt: where in tree
 *
 * Returns fully qualified path name on sucess, NULL on failure.
 * sd_put_name must be used to free allocated buffer.
 */
char *sd_get_name(struct dentry *dentry, struct vfsmount *mnt)
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

		SD_DEBUG("%s: full_path=%s\n", __FUNCTION__, name);
	}

out:
	return name;
}

/***********************************
 * GLOBAL PERMISSION CHECK FUNCTIONS
 ***********************************/

/**
 * sd_attr - check whether attribute change allowed
 * @sd: subdomain to check against to check against
 * @dentry: file to check
 * @iattr: attribute changes requested
 */
int sd_attr(struct subdomain *sd, struct dentry *dentry, struct iattr *iattr)
{
	int error = 0, permerror;
	struct sd_audit sa;

	if (!__sd_is_confined(sd))
		goto out;

	sa.type = SD_AUDITTYPE_ATTR;
	sa.pval = iattr;
	sa.flags = 0;
	sa.gfp_mask = GFP_KERNEL;

	permerror = _sd_perm_dentry(sd, dentry, MAY_WRITE, &sa.name);
	sd_permerror2result(permerror, &sa);

	error = sd_audit(sd, &sa);

	sd_put_name(sa.name);

out:
	return error;
}

int sd_xattr(struct subdomain *sd, struct dentry *dentry, const char *xattr,
	     int xattroptype)
{
	int error = 0, permerror, mask = 0;
	struct sd_audit sa;

	/* if not confined or empty mask permission granted */
	if (!__sd_is_confined(sd))
		goto out;

	if (xattroptype == SD_XATTR_GET || xattroptype == SD_XATTR_LIST)
		mask = MAY_READ;
	else if (xattroptype == SD_XATTR_SET || xattroptype == SD_XATTR_REMOVE)
		mask = MAY_WRITE;

	sa.type = SD_AUDITTYPE_XATTR;
	sa.ival = xattroptype;
	sa.pval = xattr;
	sa.flags = 0;
	sa.gfp_mask = GFP_KERNEL;

	permerror = _sd_perm_dentry(sd, dentry, mask, &sa.name);
	sd_permerror2result(permerror, &sa);

	error = sd_audit(sd, &sa);

	sd_put_name(sa.name);

out:
	return error;
}

/**
 * sd_perm - basic subdomain permissions check
 * @sd: subdomain to check against
 * @dentry: dentry
 * @mnt: mountpoint
 * @mask: access mode requested
 *
 * Determine if access (mask) for dentry is authorized by subdomain sd.
 * Result, 0 (success), -ve (error)
 */
int sd_perm(struct subdomain *sd, struct dentry *dentry, struct vfsmount *mnt,
	    int mask)
{
	int error = 0, permerror;
	struct sd_audit sa;

	if (!__sd_is_confined(sd))
		goto out;

	if ((mask = sd_filter_mask(mask, dentry->d_inode)) == 0)
		goto out;

	sa.type = SD_AUDITTYPE_FILE;
	sa.name = sd_get_name(dentry, mnt);
	sa.ival = mask;
	sa.flags = 0;
	sa.gfp_mask = GFP_KERNEL;

	if (IS_ERR(sa.name)) {
		permerror = PTR_ERR(sa.name);
		sa.name = NULL;
	} else {
		permerror = sd_file_perm(sd, sa.name, mask);
	}

	sd_permerror2result(permerror, &sa);

	error = sd_audit(sd, &sa);

	sd_put_name(sa.name);

out:
	return error;
}

/**
 * sd_perm_nameidata: interface to sd_perm accepting nameidata
 * @sd: subdomain to check against
 * @nd: namespace data (for vfsmnt and dentry)
 * @mask: access mode requested
 */
int sd_perm_nameidata(struct subdomain *sd, struct nameidata *nd, int mask)
{
	int error = 0;

	if (nd)
		error = sd_perm(sd, nd->dentry, nd->mnt, mask);

	return error;
}

/**
 * sd_perm_dentry - file permissions interface when no vfsmnt available
 * @sd: current subdomain
 * @dentry: requested dentry
 * @mask: access mode requested
 *
 * Determine if access (mask) for dentry is authorized by subdomain sd.
 * Result, 0 (success), -ve (error)
 */
int sd_perm_dentry(struct subdomain *sd, struct dentry *dentry, int mask)
{
	int error = 0, permerror;
	struct sd_audit sa;

	if (!__sd_is_confined(sd))
		goto out;

	if ((mask = sd_filter_mask(mask, dentry->d_inode)) == 0)
		goto out;

	sa.type = SD_AUDITTYPE_FILE;
	sa.ival = mask;
	sa.flags = 0;
	sa.gfp_mask = GFP_KERNEL;

	permerror = _sd_perm_dentry(sd, dentry, mask, &sa.name);
	sd_permerror2result(permerror, &sa);

	error = sd_audit(sd, &sa);

	sd_put_name(sa.name);

out:
	return error;
}

/**
 * sd_perm_dir
 * @sd: current subdomain
 * @dentry: requested dentry
 * @mode: SD_DIR_MKDIR or SD_DIR_RMDIR
 *
 * Determine if directory operation (make/remove) for dentry is authorized
 * by subdomain sd.
 * Result, 0 (success), -ve (error)
 */
int sd_perm_dir(struct subdomain *sd, struct dentry *dentry, int diroptype)
{
	int error = 0, permerror, mask;
	struct sd_audit sa;

	BUG_ON(diroptype != SD_DIR_MKDIR && diroptype != SD_DIR_RMDIR);

	if (!__sd_is_confined(sd))
		goto out;

	mask = MAY_WRITE;

	sa.type = SD_AUDITTYPE_DIR;
	sa.ival = diroptype;
	sa.flags = 0;
	sa.gfp_mask = GFP_KERNEL;

	permerror = _sd_perm_dentry(sd, dentry, mask, &sa.name);
	sd_permerror2result(permerror, &sa);

	error = sd_audit(sd, &sa);

	sd_put_name(sa.name);

out:
	return error;
}

/**
 * sd_capability - test permission to use capability
 * @sd: subdomain to check against
 * @cap: capability to be tested
 *
 * Look up capability in active profile capability set.
 * Return 0 (success), -EPERM (error)
 */
int sd_capability(struct subdomain *sd, int cap)
{
	int error = 0;

	if (__sd_is_confined(sd)) {
		struct sd_audit sa;

		sa.type = SD_AUDITTYPE_CAP;
		sa.name = NULL;
		sa.ival = cap;
		sa.flags = 0;
		sa.errorcode = 0;
		sa.result = cap_raised(sd->active->capabilities, cap);
		sa.gfp_mask = GFP_ATOMIC;

		error = sd_audit(sd, &sa);
	}

	return error;
}

/**
 * sd_link - hard link check
 * @link: dentry for link being created
 * @target: dentry for link target
 * @sd: subdomain to check against
 *
 * Checks link permissions for all possible name combinations.  This is
 * particularly ugly.  Returns 0 on sucess, error otherwise.
 */
int sd_link(struct subdomain *sd, struct dentry *link, struct dentry *target)
{
	char *iname = NULL, *oname = NULL,
	     *failed_iname = NULL, *failed_oname = NULL;
	unsigned int result = 0;
	int error, sdpath_error, errorcode = 0, match = 0,
	    sdcomplain = SUBDOMAIN_COMPLAIN(sd);
	struct sd_path_data idata, odata;
	struct sd_audit sa;

	if (!__sd_is_confined(sd))
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

	sd_path_begin2(target, link, &odata);
	do {
		oname = sd_path_getname(&odata);
		if (oname) {
			sd_path_begin(target, &idata);
			do {
				iname = sd_path_getname(&idata);
				if (iname) {
					result = sd_link_perm(sd, oname, iname);

					/* access via any path is enough */
					if (result || sdcomplain) {
						match = 1;
						break;
					}

					/* Already have an path that failed? */
					if (failed_iname) {
						sd_put_name(iname);
					} else {
						failed_iname = iname;
						failed_oname = oname;
					}
				}
			} while (iname && !match);

			/* should not be possible if we matched */
			if ((sdpath_error = sd_path_end(&idata)) != 0) {
				dentry_xlate_error(target, sdpath_error,
						   "inner dentry [link]");

				/* name should not be set if error */
				WARN_ON(iname);

				errorcode = sdpath_error;
			}

			/* don't release if we're saving it */
			if (!match && failed_oname != oname)
				sd_put_name(oname);
		}
	} while (oname && !match);

	if (errorcode != 0) {
		/* inner error */
		(void)sd_path_end(&odata);
	} else if ((sdpath_error = sd_path_end(&odata)) != 0) {
		dentry_xlate_error(link, sdpath_error, "outer dentry [link]");

		errorcode = sdpath_error;
	}

	if (errorcode != 0) {
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

	sa.type = SD_AUDITTYPE_LINK;
	sa.name = oname;	/* link */
	sa.pval = iname;	/* target */
	sa.flags = 0;
	sa.errorcode = errorcode;
	sa.result = result;
	sa.gfp_mask = GFP_KERNEL;

	error = sd_audit(sd, &sa);

	if (failed_oname != oname)
		sd_put_name(failed_oname);
	if (failed_iname != iname)
		sd_put_name(failed_iname);

	sd_put_name(oname);
	sd_put_name(iname);

	return error;
}

/**********************************
 * GLOBAL PROCESS RELATED FUNCTIONS
 *********************************/

/**
 * sd_fork - create a new subdomain
 * @p: new process
 *
 * Create a new subdomain for newly created process @p if it's parent
 * is already confined.  Otherwise a subdomain will be lazily allocated
 * for the child if it subsequently execs (in sd_register).
 * Return 0 on sucess.
 */

int sd_fork(struct task_struct *p)
{
	struct subdomain *sd = SD_SUBDOMAIN(current->security);
	struct subdomain *newsd = NULL;

	SD_DEBUG("%s\n", __FUNCTION__);

	if (__sd_is_confined(sd)) {
		unsigned long flags;

		newsd = alloc_subdomain(p);

		if (!newsd)
			return -ENOMEM;

		/* Can get away with a read rather than write lock here
		 * as we just allocated newsd above, so we can guarantee
		 * that it's active/profile are null and therefore a replace
		 * cannot happen.
		 */
		read_lock_irqsave(&sd_lock, flags);
		sd_switch(newsd, sd->profile, sd->active);
		newsd->sd_hat_magic = sd->sd_hat_magic;
		read_unlock_irqrestore(&sd_lock, flags);

		if (SUBDOMAIN_COMPLAIN(sd) &&
		    sd->active == null_complain_profile)
			LOG_HINT(sd, GFP_KERNEL, HINT_FORK,
				"pid=%d child=%d\n",
				current->pid, p->pid);
	}
	p->security = newsd;
	return 0;
}

/**
 * sd_register - register a new program
 * @filp: file of program being registered
 *
 * Try to register a new program during execve().  This should give the
 * new program a valid subdomain.
 *
 * This _used_ to be a really simple piece of code :-(
 *
 */
int sd_register(struct linux_binprm *bprm)
{
	char *filename;
	struct file *filp = bprm->file;
	struct subdomain *sd, sdcopy;
	struct sdprofile *newprofile = NULL, unconstrained_flag;
	int 	error = -ENOMEM,
		exec_mode = 0,
		findprofile = 0,
		findprofile_mandatory = 0,
		unsafe_exec = 0,
		complain = 0;

	SD_DEBUG("%s\n", __FUNCTION__);

	sd = get_sdcopy(&sdcopy);

	filename = sd_get_name(filp->f_dentry, filp->f_vfsmnt);
	if (IS_ERR(filename)) {
		SD_WARN("%s: Failed to get filename\n", __FUNCTION__);
		goto out;
	}

	error = 0;

	if (!__sd_is_confined(sd)) {
		/* Unconfined task, load profile if it exists */
		findprofile = 1;
		goto find_profile;
	}

	complain = SUBDOMAIN_COMPLAIN(sd);

	/* Confined task, determine what mode inherit, unconstrained or
	 * mandatory to load new profile
	 */
	if (sd_get_execmode(sd, filename, &exec_mode, &unsafe_exec)) {
		switch (exec_mode) {
		case SD_EXEC_INHERIT:
			/* do nothing - setting of profile
			 * already handed in sd_fork
			 */
			SD_DEBUG("%s: INHERIT %s\n",
				 __FUNCTION__,
				 filename);
			break;

		case SD_EXEC_UNCONSTRAINED:
			SD_DEBUG("%s: UNCONSTRAINED %s\n",
				 __FUNCTION__,
				 filename);

			/* unload profile */
			newprofile = &unconstrained_flag;
			break;

		case SD_EXEC_PROFILE:
			SD_DEBUG("%s: PROFILE %s\n",
				 __FUNCTION__,
				 filename);

			findprofile = 1;
			findprofile_mandatory = 1;
			break;

		case SD_MAY_EXEC:
			/* this should not happen, entries
			 * with just EXEC only should be
			 * rejected at profile load time
			 */
			SD_ERROR("%s: Rejecting exec(2) of image '%s'. "
				"SD_MAY_EXEC without exec qualifier invalid "
				"(%s(%d) profile %s active %s\n",
				 __FUNCTION__,
				 filename,
				 current->comm, current->pid,
				 sd->profile->name, sd->active->name);
			error = -EPERM;
			break;

		default:
			SD_ERROR("%s: Rejecting exec(2) of image '%s'. "
				 "Unknown exec qualifier %x "
				 "(%s (pid %d) profile %s active %s)\n",
				 __FUNCTION__,
				 filename,
				 exec_mode,
				 current->comm, current->pid,
				 sd->profile->name, sd->active->name);
			error = -EPERM;
			break;
		}

	} else if (complain) {
		/* There was no entry in calling profile
		 * describing mode to execute image in.
		 * Drop into null-profile (disabling secure exec).
		 */
		newprofile = get_sdprofile(null_complain_profile);
		unsafe_exec = 1;
	} else {
		SD_WARN("%s: Rejecting exec(2) of image '%s'. "
			"Unable to determine exec qualifier "
			"(%s (pid %d) profile %s active %s)\n",
			__FUNCTION__,
			filename,
			current->comm, current->pid,
			sd->profile->name, sd->active->name);
		error = -EPERM;
	}


find_profile:
	if (!findprofile)
		goto apply_profile;

	/* Locate new profile */
	newprofile = sd_profilelist_find(filename);
	if (newprofile) {
		SD_DEBUG("%s: setting profile %s\n",
			 __FUNCTION__, newprofile->name);
	} else if (findprofile_mandatory) {
		/* Profile (mandatory) could not be found */

		if (complain) {
			LOG_HINT(sd, GFP_KERNEL, HINT_MANDPROF,
				"image=%s pid=%d profile=%s active=%s\n",
				filename,
				current->pid,
				sd->profile->name,
				sd->active->name);

			newprofile = get_sdprofile(null_complain_profile);
		} else {
			SD_WARN("REJECTING exec(2) of image '%s'. "
				"Profile mandatory and not found "
				"(%s(%d) profile %s active %s)\n",
				filename,
				current->comm, current->pid,
				sd->profile->name, sd->active->name);
			error = -EPERM;
		}
	} else {
		/* Profile (non-mandatory) could not be found */

		/* Only way we can get into this code is if task
		 * is unconstrained.
		 */

		BUG_ON(__sd_is_confined(sd));

		SD_DEBUG("%s: No profile found for exec image %s\n",
			 __FUNCTION__,
			 filename);
	} /* newprofile */


apply_profile:
	/* Apply profile if necessary */
	if (newprofile) {
		struct subdomain *latest_sd, *lazy_sd = NULL;
		unsigned long flags;

		if (newprofile == &unconstrained_flag)
			newprofile = NULL;

		/* grab a write lock
		 *
		 * - Task may be presently unconfined (have no sd). In which
		 *   case we have to lazily allocate one.  Note we may be raced
		 *   to this allocation by a setprofile.
		 *
		 * - sd is a refcounted copy of the subdomain (get_sdcopy) and
		 *   not the actual subdomain. This allows us to not have to
		 *   hold a read lock around all this code. However, we need to
		 *   change the actual subdomain, not the copy.
		 *
		 * - If newprofile points to an actual profile (result of
		 *   sd_profilelist_find above), this profile may have been
		 *   replaced.  We need to fix it up.  Doing this to avoid
		 *   having to hold a write lock around all this code.
		 */

		if (!sd) {
			lazy_sd = alloc_subdomain(current);
		}

		write_lock_irqsave(&sd_lock, flags);

		latest_sd = SD_SUBDOMAIN(current->security);

		if (latest_sd) {
			if (lazy_sd) {
				/* raced by setprofile (created latest_sd) */
				free_subdomain(lazy_sd);
				lazy_sd = NULL;
			}
		} else {
			if (lazy_sd) {
				latest_sd = lazy_sd;
				current->security = lazy_sd;
			} else {
				SD_ERROR("%s: Failed to allocate subdomain\n",
					__FUNCTION__);

				error = -ENOMEM;
				write_unlock_irqrestore(&sd_lock, flags);
				goto done;
			}
		}

		/* Determine if profile we found earlier is stale.
		 * If so, reobtain it.  N.B stale flag should never be
		 * set on null_complain profile.
		 */
		if (newprofile && unlikely(newprofile->isstale)) {
			BUG_ON(newprofile == null_complain_profile);

			/* drop refcnt obtained from earlier get_sdprofile */
			put_sdprofile(newprofile);

			newprofile = sd_profilelist_find(filename);

			if (!newprofile) {
				/* Race, profile was removed, not replaced.
				 * Redo with error checking
				 */
				write_unlock_irqrestore(&sd_lock, flags);
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
		if (__sd_is_confined(latest_sd) && !unsafe_exec) {
			unsigned long bprm_flags;

			bprm_flags = SD_SECURE_EXEC_NEEDED;
			bprm->security = (void*)
				((unsigned long)bprm->security | bprm_flags);
		}

		sd_switch(latest_sd, newprofile, newprofile);
		put_sdprofile(newprofile);

		if (complain && newprofile == null_complain_profile)
			LOG_HINT(latest_sd, GFP_ATOMIC, HINT_CHGPROF,
				"pid=%d\n",
				current->pid);

		write_unlock_irqrestore(&sd_lock, flags);
	}

done:
	sd_put_name(filename);

	if (sd)
		put_sdcopy(sd);

out:
	return error;
}

/**
 * sd_release - release the task's subdomain
 * @p: task being released
 *
 * This is called after a task has exited and the parent has reaped it.
 * p->security must be !NULL. The @p->security blob is freed.
 */
void sd_release(struct task_struct *p)
{
	struct subdomain *sd = SD_SUBDOMAIN(p->security);
	p->security = NULL;

	sd_subdomainlist_remove(sd);

	/* release profiles */
	put_sdprofile(sd->profile);
	put_sdprofile(sd->active);

	kfree(sd);
}

/*****************************
 * GLOBAL SUBPROFILE FUNCTIONS
 ****************************/

/**
 * do_change_hat - actually switch hats
 * @name: name of hat to swtich to
 * @sd: current subdomain
 *
 * Switch to a new hat.  Return 0 on success, error otherwise.
 */
static inline int do_change_hat(const char *hat_name, struct subdomain *sd)
{
	struct sdprofile *sub;
	struct sdprofile *p = sd->active;
	int error = 0;

	sub = __sd_find_profile(hat_name, &sd->profile->sub);

	if (sub) {
		/* change hat */
		sd->active = sub;
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
			LOG_HINT(sd, GFP_ATOMIC, HINT_UNKNOWN_HAT,
 				"%s pid=%d "
				"profile=%s active=%s\n",
				hat_name,
				current->pid,
				sd->profile->name,
				sd->active->name);
			sd->active = get_sdprofile(null_complain_profile);
		} else {
			SD_DEBUG("%s: Unknown hatname '%s'. "
				"Changing to NULL profile "
				"(%s(%d) profile %s active %s)\n",
				 __FUNCTION__,
				 hat_name,
				 current->comm, current->pid,
				 sd->profile->name, sd->active->name);

			sd->active = get_sdprofile(null_profile);
			error = -EACCES;
		}
	}
	put_sdprofile(p);

	return error;
}

/**
 * sd_change_hat - change hat to/from subprofile
 * @hat_name: specifies hat to change to
 * @hat_magic: token to validate hat change
 *
 * Change to new @hat_name when current hat is top level profile, and store
 * the @hat_magic in the current subdomain.  If the new @hat_name is
 * NULL, and the @hat_magic matches that stored in the current subdomain
 * return to original top level profile.  Returns 0 on success, error
 * otherwise.
 */
#define IN_SUBPROFILE(sd)	((sd)->profile != (sd)->active)
int sd_change_hat(const char *hat_name, __u32 hat_magic)
{
	struct subdomain *sd = SD_SUBDOMAIN(current->security);
	int error = 0;

	SD_DEBUG("%s: %p, 0x%x (pid %d)\n",
		 __FUNCTION__,
		 hat_name, hat_magic,
		 current->pid);

	/* Dump out above debugging in WARN mode if we are in AUDIT mode */
	if (SUBDOMAIN_AUDIT(sd)) {
		SD_WARN("%s: %s, 0x%x (pid %d)\n",
			__FUNCTION__, hat_name ? hat_name : "NULL",
			hat_magic, current->pid);
	}

	/* no subdomain: changehat into the null_profile, since the process
	   has no subdomain do_change_hat won't find a match which will cause
	   a changehat to null_profile.  We could short circuit this but since
	   the subdprofile (hat) list is empty we would save very little. */

	/* check to see if an unconfined process is doing a changehat. */
	if (!__sd_is_confined(sd)) {
		error = -EACCES;
		goto out;
	}

	/* Check whether current domain is parent
	 * or one of the sibling children
	 */
	if (sd->profile == sd->active) {
		/*
		 * parent
		 */
		if (hat_name) {
			SD_DEBUG("%s: switching to %s, 0x%x\n",
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
			sd->sd_hat_magic = hat_magic;
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
		if (hat_magic == sd->sd_hat_magic && sd->sd_hat_magic) {
			if (!hat_name) {
				/*
				 * Got here via changehat(NULL, magic)
				 * Return from subprofile, back to parent
				 */
				put_sdprofile(sd->active);
				sd->active = get_sdprofile(sd->profile);

				/* Reset hat_magic to zero.
				 * New value will be passed on next changehat
				 */
				sd->sd_hat_magic = 0;
			} else {
				/* change to another (sibling) profile */
				error = do_change_hat(hat_name, sd);
			}
		} else if (sd->sd_hat_magic) {
			SD_ERROR("KILLING process %s(%d) "
				 "Invalid change_hat() magic# 0x%x "
				 "(hatname %s profile %s active %s)\n",
				 current->comm, current->pid,
				 hat_magic,
				 hat_name ? hat_name : "NULL",
				 sd->profile->name, sd->active->name);

			/* terminate current process */
			(void)send_sig_info(SIGKILL, NULL, current);
		} else {	/* sd->sd_hat_magic == NULL */
			SD_ERROR("KILLING process %s(%d) "
				 "Task was confined to current subprofile "
				 "(profile %s active %s)\n",
				 current->comm, current->pid,
				 sd->profile->name, sd->active->name);

			/* terminate current process */
			(void)send_sig_info(SIGKILL, NULL, current);
		}

	}

out:
	return error;
}
