/*
 *	Copyright (C) 2002-2005 Novell/SUSE
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 *
 *	SubDomain Core
 */

#include <linux/security.h>
#include <linux/namei.h>

#include "immunix.h"
#include "subdomain.h"
#include "sdmatch/match.h"

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

/*
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
	struct list_head *lh;
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
	list_for_each(lh, &profile->file_entry) {
		struct sd_entry *entry = list_entry(lh, struct sd_entry, list);

		if (sdmatch_match(name, entry->filename,
				  entry->entry_type, entry->extradata))
			mode |= entry->mode;
	}
out:
	return mode;
}

/*
 * sd_get_execmode - calculate what qualifier to apply to an exec
 * @sd: subdomain to search
 * @name: name of file to exec
 * @xmod: pointer to a execution mode bit for the rule that was matched
 *         if the rule has no execuition qualifier {pui} then
 *         SD_MAY_EXEC is returned indicating a naked x
 *         if the has an exec qualifier then only the qualifier bit {pui}
 *         is returned (SD_MAY_EXEC) is not set.
 *
 * Returns 0 (false):
 *    if unable to find profile or there are conflicting pattern matches.
 *       *xmod - is not modified
 *
 * Returns 1 (true):
 *    if not confined
 *       *xmod = SD_MAY_EXEC
 *    if exec rule matched
 *       if the rule has an execution mode qualifier {pui} then
 *          *xmod = the execution qualifier of the rule {pui}
 *       else
 *          *xmod = SD_MAY_EXEC
 */
static inline int sd_get_execmode(struct subdomain *sd, const char *name,
				  int *xmod)
{
	struct sdprofile *profile;
	struct list_head *lh;
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

	list_for_each(lh, &profile->file_entryp[POS_SD_MAY_EXEC]) {
		struct sd_entry *entry;
		entry = list_entry(lh, struct sd_entry,
				   listp[POS_SD_MAY_EXEC]);
		if (!pattern_match_invalid &&
		    entry->entry_type == sd_entry_pattern &&
		    sdmatch_match(name, entry->filename,
				  entry->entry_type, entry->extradata)) {
			if (match &&
			    SD_EXEC_MASK(entry->mode) !=
			    SD_EXEC_MASK(match->mode))
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
				    SD_EXEC_MASK(entry->mode) !=
				    SD_EXEC_MASK(match->mode))
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
		int elim=MAY_APPEND;

		if (inode && S_ISDIR(inode->i_mode))
			elim |= (MAY_EXEC | MAY_WRITE);

		mask &= ~elim;
	}

	return mask;
}

/****************************
 * INTERNAL TRACING FUNCTIONS
 ***************************/

/**
 * sd_attr_trace - trace attempt to change file attributes
 * @sd: SubDomain to check against
 * @name: file requested
 * @iattr: requested new modes
 * @error: error flag
 *
 * Prints out the status of the attribute change request.  Only prints
 * accepted when in audit mode.
 */
static inline void sd_attr_trace(struct subdomain *sd, const char *name,
				 struct iattr *iattr, int error)
{
	const char *status = "AUDITING";

	if (error)
		status = SUBDOMAIN_COMPLAIN(sd) ? "PERMITTING" : "REJECTING";
	else if (!SUBDOMAIN_AUDIT(sd))
		return;

	SD_WARN("%s attribute (%s%s%s%s%s%s%s) change to %s (%s(%d) "
		"profile %s active %s)\n",
		status,
		iattr->ia_valid & ATTR_MODE ? "mode," : "",
		iattr->ia_valid & ATTR_UID ? "uid," : "",
		iattr->ia_valid & ATTR_GID ? "gid," : "",
		iattr->ia_valid & ATTR_SIZE ? "size," : "",
		((iattr->ia_valid & ATTR_ATIME_SET)
		 || (iattr->ia_valid & ATTR_ATIME)) ? "atime," : "",
		((iattr->ia_valid & ATTR_MTIME_SET)
		 || (iattr->ia_valid & ATTR_MTIME)) ? "mtime," : "",
		iattr->ia_valid & ATTR_CTIME ? "ctime," : "",
		name, current->comm, current->pid,
		sd->profile->name, sd->active->name);
}

/**
 * sd_xattr_trace - trace attempt to change file attributes
 * @sd: SubDomain to check against
 * @name: file requested
 * @iattr: requested new modes
 * @error: error flag
 *
 * Prints out the status of the attribute change request.  Only prints
 * accepted when in audit mode.
 */
static inline void sd_xattr_trace(struct subdomain *sd, const char *name,
				  const char *xattr, int mask, int error)
{
	const char *status = "AUDITING";

	if (error) {
		status = SUBDOMAIN_COMPLAIN(sd) ? "PERMITTING" : "REJECTING";
	} else if (!SUBDOMAIN_AUDIT(sd)) {
		return;
	}

	SD_WARN("%s %s%s access to %s extended attribute %s (%s(%d) "
		"profile %s active %s)\n",
		status,
		mask & SD_MAY_READ  ? "r" : "",
		mask & SD_MAY_WRITE ? "w" : "",
		name, xattr,
		current->comm, current->pid,
		sd->profile->name, sd->active->name);
}

/**
 * sd_file_perm_trace - trace permission
 * @sd: SubDomain to check against
 * @name: file requested
 * @mask: requested permission
 * @error: error flag
 *
 * Prints out the status of the permission request.  Only prints
 * accepted when in audit mode.
 */
static inline void sd_file_perm_trace(struct subdomain *sd,
				      const char *name, int mask, int error)
{
	const char *status = "AUDITING";

	if (error)
		status = SUBDOMAIN_COMPLAIN(sd) ? "PERMITTING" : "REJECTING";
	else if (!SUBDOMAIN_AUDIT(sd))
		return;

	SD_WARN("%s %s%s%s%s access to %s (%s(%d) profile %s active %s)\n",
		status,
		mask & SD_MAY_READ  ? "r" : "",
		mask & SD_MAY_WRITE ? "w" : "",
		mask & SD_MAY_EXEC  ? "x" : "",
		mask & SD_MAY_LINK  ? "l" : "",
		name, current->comm, current->pid,
		sd->profile->name, sd->active->name);
}

/**
 * sd_link_perm_trace - trace link permission
 * @sd: current SubDomain
 * @lname: name requested as new link
 * @tname: name requested as new link's target
 * @error: error status
 *
 * Prints out the status of the permission request.  Only prints
 * accepted when in audit mode.
 */
static inline void sd_link_perm_trace(struct subdomain *sd,
				      const char *lname, const char *tname,
				      int error)
{
	const char *status = "AUDITING";

	if (error)
		status = SUBDOMAIN_COMPLAIN(sd) ? "PERMITTING" : "REJECTING";
	else if (!SUBDOMAIN_AUDIT(sd))
		return;

	SD_WARN("%s link access from %s to %s (%s(%d) profile %s active %s)\n",
		status, lname, tname, current->comm, current->pid,
		sd->profile->name, sd->active->name);
}

/*************************
 * MAIN INTERNAL FUNCTIONS
 ************************/

/**
 * sd_link_perm - test permission to link to a file
 * @sd: current SubDomain
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
	int l_mode, t_mode, error = -EPERM;
	struct sdprofile *profile = sd->active;

	error = -EPERM;
	l_mode = sd_file_mode(profile, link);
	if (!(l_mode & SD_MAY_LINK))
		goto out;
	/* ok, mask off link bit */
	l_mode &= ~SD_MAY_LINK;

	t_mode = sd_file_mode(profile, target);
	t_mode &= ~SD_MAY_LINK;

	if (l_mode == t_mode)
		error = 0;

out:
	sd_link_perm_trace(sd, link, target, error);
	if (SUBDOMAIN_COMPLAIN(sd))
		error = 0;
	return error;
}

/**************************
 * GLOBAL UTILITY FUNCTIONS
 *************************/

/**
 * __sd_get_name - retrieve fully qualified path name
 * @dentry: relative path element
 * @mnt: where in tree
 *
 * Returns fully qualified path name on sucess, NULL on failure.
 * sd_put_name must be used to free allocated buffer.
 */
char *__sd_get_name(struct dentry *dentry, struct vfsmount *mnt)
{
	char *page, *name = NULL;

	page = (char *)__get_free_page(GFP_KERNEL);
	if (!page)
		goto out;

	name = d_path(dentry, mnt, page, PAGE_SIZE);

	/* check for (deleted) that d_path appends to pathnames if the dentry
	 * has been removed from the cache.
	 * The size > deleted_size and strcmp checks are redundant safe guards.
	 */
	if (name) {
		const char deleted_str[] = " (deleted)";
		const size_t deleted_size = sizeof(deleted_str) - 1;
		size_t size;
		size = strlen(name);
		if (!IS_ROOT(dentry) && d_unhashed(dentry) &&
		    size > deleted_size &&
		    strcmp(name + size - deleted_size, deleted_str) == 0)
			name[size - deleted_size] = '\0';
	}
	SD_DEBUG("%s: full_path=%s\n", __FUNCTION__, name);
out:
	return name;
}

/***********************************
 * GLOBAL PERMISSION CHECK FUNCTIONS
 ***********************************/

/*
 * sd_file_perm - calculate access mode for file
 * @subdomain: current subdomain
 * @name: name of file to calculate mode for
 * @mask: permission mask requested for file
 * @log:  log errors
 *
 * Search the sd_entry list in @profile.
 * Search looking to verify all permissions passed in mask.
 * Perform the search by looking at the partitioned list of entries, one
 * partition per permission bit.
 * Return 0 on access allowed, < 0 on error.
 */
int sd_file_perm(struct subdomain *sd, const char *name, int mask, int log)
{
	struct sdprofile *profile;
	int i, error, mode;

#define PROCPFX "/proc/"
#define PROCLEN sizeof(PROCPFX) - 1

	SD_DEBUG("%s: %s 0x%x\n", __FUNCTION__, name, mask);

	error = 0;

	/* should not enter with other than R/W/X/L */
	BUG_ON(mask &
	       ~(SD_MAY_READ | SD_MAY_WRITE | SD_MAY_EXEC | SD_MAY_LINK));

	/* not confined */
	if (!__sd_is_confined(sd)) {
		/* exit with access allowed */
		SD_DEBUG("%s: not confined\n", __FUNCTION__);
		goto done_notrace;
	}

	/* Special case access to /proc/self/attr/current
	 * Currently we only allow access if opened O_WRONLY
	 */
	if (mask == MAY_WRITE && strncmp(PROCPFX, name, PROCLEN) == 0 &&
	    (!list_empty(&sd->profile->sub) || SUBDOMAIN_COMPLAIN(sd)) &&
	    sd_taskattr_access(name + PROCLEN))
		goto done_notrace;

	error = -EACCES;

	profile = sd->active;

	mode = 0;

	/* iterate over partition, one permission bit at a time */
	for (i = 0; i <= POS_SD_FILE_MAX; i++) {
		struct list_head *lh;

		/* do we have to accumulate this bit?
		 * or have we already accumulated it (shortcut below)? */
		if (!(mask & (1 << i)) || mode & (1 << i))
			continue;

		list_for_each(lh, &profile->file_entryp[i]) {
			struct sd_entry *entry;
			entry = list_entry(lh, struct sd_entry, listp[i]);

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

					error = 0;
					goto done;
				}
			}
		}
	}
	/* error: only log permissions that weren't granted */
	mask &= ~mode;

done:
	if (log) {
		sd_file_perm_trace(sd, name, mask, error);

		if (SUBDOMAIN_COMPLAIN(sd))
			error = 0;
	}

done_notrace:
	return error;
}

/**
 * sd_attr - check whether attribute change allowed
 * @sd: SubDomain to check against to check against
 * @dentry: file to check
 * @iattr: attribute changes requested
 *
 * This function is a replica of sd_perm. In fact, calling sd_perm(MAY_WRITE)
 * will achieve the same access control, but logging would appear to indicate
 * success/failure of a "w" rather than an attribute change.  Also, this way we
 * can log a single message indicating success/failure and also what attribite
 * changes were attempted.
 */
int sd_attr(struct subdomain *sd, struct dentry *dentry, struct iattr *iattr)
{
	int error = 0, sdpath_error;
	struct sd_path_data data;
	char *name;

	/* if not confined or empty mask permission granted */
	if (!__sd_is_confined(sd) || !iattr)
		goto out;

	/* search all paths to dentry */

	sd_path_begin(dentry, &data);
	do {
		name = sd_path_getname(&data);
		if (name) {
			error = sd_file_perm(sd, name, MAY_WRITE, 0);

			/* access via any path is enough */
			if (error)
				sd_attr_trace(sd, name, iattr, error);

			sd_put_name(name);

			if (!error)
				break;
		}

	} while (name);

	if ((sdpath_error = sd_path_end(&data)) != 0) {
		SD_ERROR("%s: An error occured while translating dentry %p "
			 "inode# %lu to a pathname. Error %d\n",
			 __FUNCTION__,
			 dentry,
			 dentry->d_inode->i_ino,
			 sdpath_error);

		error = sdpath_error;
	}

	if (SUBDOMAIN_COMPLAIN(sd))
		error = 0;

out:
	return error;
}

int sd_xattr(struct subdomain *sd, struct dentry *dentry, const char *attr,
	     int flags)
{
	int error = 0, sdpath_error;
	struct sd_path_data data;
	char *name;

	/* if not confined or empty mask permission granted */
	if (!__sd_is_confined(sd))
		goto out;

	/* search all paths to dentry */

	sd_path_begin(dentry, &data);
	do {
		name = sd_path_getname(&data);
		if (name) {
			error = sd_file_perm(sd, name, flags, 0);

			/* access via any path is enough */
			if (error)
				sd_xattr_trace(sd, name, attr, flags, error);

			sd_put_name(name);

			if (!error)
				break;
		}

	} while (name);

	if ((sdpath_error = sd_path_end(&data)) != 0) {
		SD_ERROR("%s: An error occured while translating dentry %p "
			 "inode# %lu to a pathname. Error %d\n",
			 __FUNCTION__,
			 dentry,
			 dentry->d_inode->i_ino,
			 sdpath_error);

		error = sdpath_error;
	}

	if (SUBDOMAIN_COMPLAIN(sd))
		error = 0;

out:
	return error;
}

/**
 * sd_perm - basic SubDomain permissions check
 * @sd: SubDomain to check against
 * @dentry: dentry
 * @mnt: mountpoint
 * @mask: access mode requested
 *
 * This checks that the @inode is in the current subdomain, @sd, and
 * that it can be accessed in the mode requested by @mask.  Returns 0 on
 * success.
 */
int sd_perm(struct subdomain *sd, struct dentry *dentry, struct vfsmount *mnt,
	    int mask)
{
	char *name = NULL;
	int error = 0;

	if (!__sd_is_confined(sd))
		goto out;

	if ((mask = sd_filter_mask(mask, dentry->d_inode)) == 0)
		goto out;

	error = -ENOMEM;

	name = __sd_get_name(dentry, mnt);
	if (name) {
		error = sd_file_perm(sd, name, mask, 1);
		sd_put_name(name);
	}

out:
	return error;
}

/**
 * sd_perm_nameidata: interface to sd_perm accepting nameidata
 * @sd: SubDomain to check against
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

int sd_perm_dentry(struct subdomain *sd, struct dentry *dentry, int mask)
{
	char *name;
	struct sd_path_data data;
	int error = 0, sdpath_error;

	if (!__sd_is_confined(sd))
		goto out;

	if ((mask = sd_filter_mask(mask, dentry->d_inode)) == 0)
		goto out;

	/* search all paths to dentry */

	sd_path_begin(dentry, &data);
	do {
		name = sd_path_getname(&data);
		if (name) {
			error = sd_file_perm(sd, name, mask, 1);
			sd_put_name(name);

			/* access via any path is enough */
			if (!error)
				break;
		}
	} while (name);

	if ((sdpath_error = sd_path_end(&data)) != 0) {
		SD_ERROR("%s: An error occured while translating dentry %p "
			 "inode# %lu to a pathname. Error %d\n",
			 __FUNCTION__,
			 dentry,
			 dentry->d_inode->i_ino,
			 sdpath_error);

		error = sdpath_error;
	}

out:
	return error;
}

/**
 * sd_capability - test permission to use capability
 * @sd: SubDomain to check against
 * @cap: capability to be tested
 *
 * Look up capability in active profile capability set.
 * Return 0 if valid, -EPERM if invalid
 */

int sd_capability(struct subdomain *sd, int cap)
{
	int error = 0;

	if (__sd_is_confined(sd)) {
		const char *status;

		status = SUBDOMAIN_COMPLAIN(sd) ? "PERMITTING" : "REJECTING";

		error = cap_raised(sd->active->capabilities, cap) ? 0 : -EPERM;

		if (error || SUBDOMAIN_AUDIT(sd)) {
			if (error == 0)
				status = "AUDITING";

			SD_WARN("%s access to capability '%s' (%s(%d) "
				"profile %s active %s)\n",
				status,
				capability_to_name(cap),
				current->comm, current->pid,
				sd->profile->name, sd->active->name);

			if (SUBDOMAIN_COMPLAIN(sd))
				error = 0;
		}
	}

	return error;
}

/**
 * sd_link - hard link check
 * @link: dentry for link being created
 * @target: dentry for link target
 * @sd: SubDomain to check against
 *
 * Checks link permissions for all possible name combinations.  This is
 * particularly ugly.  Returns 0 on sucess, error otherwise.
 */
int sd_link(struct subdomain *sd, struct dentry *link, struct dentry *target)
{
	char *iname, *oname;
	struct sd_path_data idata, odata;
	int error = 0, sdpath_error, done;

	if (!__sd_is_confined(sd))
		goto out;

	/* Perform nested lookup for names.
	 * This is necessary in the case where /dev/block is mounted
	 * multiple times,  i.e /dev/block->/a and /dev/block->/b
	 * This allows us to detect links where src/dest are on different
	 * mounts.   N.B no support yet for links across bind mounts of
	 * the form mount -bind /mnt/subpath /mnt2
	 */

	done = 0;
	sd_path_begin2(target, link, &odata);
	do {
		oname = sd_path_getname(&odata);

		if (oname) {
			sd_path_begin(target, &idata);
			do {
				iname = sd_path_getname(&idata);
				if (iname) {
					error = sd_link_perm(sd, oname, iname);
					sd_put_name(iname);

					/* access via any path is enough */
					if (!error)
						done = 1;
				}
			} while (!done && iname);

			if ((sdpath_error = sd_path_end(&idata)) != 0) {
				SD_ERROR("%s: An error occured while "
					 "translating inner dentry %p "
					 "inode %lu to a pathname. Error %d\n",
					 __FUNCTION__,
					 target,
					 target->d_inode->i_ino,
					 sdpath_error);

				(void)sd_path_end(&odata);
				error = sdpath_error;
				goto out;
			}
			sd_put_name(oname);

		}
	} while (!done && oname);

	if ((sdpath_error = sd_path_end(&odata)) != 0) {
		SD_ERROR("%s: An error occured while translating outer "
			 "dentry %p inode %lu to a pathname. Error %d\n",
			 __FUNCTION__,
			 link,
			 link->d_inode->i_ino,
			 sdpath_error);

		error = sdpath_error;
	}

out:
	return error;
}

/**********************************
 * GLOBAL PROCESS RELATED FUNCTIONS
 *********************************/

/**
 * sd_fork - create a new subdomain
 * @p: new process
 *
 * Create a new subdomain struct for the newly created process @p.
 * Copy parent info to child.  If parent has no subdomain, child
 * will get one with NULL values.  Return 0 on sucess.
 */

int sd_fork(struct task_struct *p)
{
	struct subdomain *sd = SD_SUBDOMAIN(current->security);
	struct subdomain *newsd = alloc_subdomain(p);

	SD_DEBUG("%s\n", __FUNCTION__);

	if (!newsd)
		return -ENOMEM;

	if (sd) {
		/* Can get away with a read rather than write lock here
		 * as we just allocated newsd above, so we can guarantee
		 * that it's active/profile are null and therefore a replace
		 * cannot happen.
		 */
		read_lock(&sd_lock);
		sd_switch(newsd, sd->profile, sd->active);
		newsd->sd_hat_magic = sd->sd_hat_magic;
		read_unlock(&sd_lock);

		if (SUBDOMAIN_COMPLAIN(sd) &&
		    sd->active == null_complain_profile)
			SD_WARN("LOGPROF-HINT fork pid=%d child=%d\n",
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
 * new program a valid SubDomain.
 *
 * This _used_ to be a really simple piece of code :-(
 *
 */
int sd_register(struct file *filp)
{
	char *filename;
	struct subdomain *sd, sdcopy;
	struct sdprofile *newprofile = NULL, unconstrained_flag;
	int 	error = -ENOMEM,
		exec_mode = 0,
		findprofile = 0,
		findprofile_mandatory = 0,
		issdcopy = 1,
		complain = 0;

	SD_DEBUG("%s\n", __FUNCTION__);

	sd = get_sdcopy(&sdcopy);

	if (sd) {
		complain = SUBDOMAIN_COMPLAIN(sd);
	} else {
		/* task has no subdomain.  This can happen when a task is
		 * created when subdomain is not loaded.  Allocate and
		 * attach a subdomain to the task
		 */
		issdcopy = 0;

		sd = alloc_subdomain(current);
		if (!sd) {
			SD_WARN("%s: Failed to allocate SubDomain\n",
				__FUNCTION__);
			goto out;
		}

		current->security = sd;
	}

	filename = __sd_get_name(filp->f_dentry, filp->f_vfsmnt);
	if (!filename) {
		SD_WARN("%s: Failed to get filename\n", __FUNCTION__);
		goto out;
	}

	error = 0;

	if (!__sd_is_confined(sd)) {
		/* Unconfined task, load profile if it exists */
		findprofile = 1;
		goto find_profile;
	}

	/* Confined task, determine what mode inherit, unconstrained or
	 * mandatory to load new profile
	 */
	if (sd_get_execmode(sd, filename, &exec_mode)) {
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
		 * Drop into null-profile
		 */
		newprofile = get_sdprofile(null_complain_profile);
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
			SD_WARN("LOGPROF-HINT missing_mandatory_profile "
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
		struct subdomain *latest_sd;

		if (newprofile == &unconstrained_flag)
			newprofile = NULL;

		/* grab a write lock
		 *
		 * Several things may have changed since the code above
		 *
		 * - If we are a confined process, sd is a refcounted copy of
		 *   the SubDomain (get_sdcopy) and not the actual SubDomain.
		 *   This allows us to not have to hold a read lock around
		 *   all this code.  However, we need to change the actual
		 *   SubDomain, not the copy.  Also, if profile replacement
		 *   has taken place, our sd->profile may be inaccurate
		 *   so we need to undo the copy and reverse the refcounting.
		 *
		 * - If newprofile points to an actual profile (result of
		 *   sd_profilelist_find above), this profile may have been
		 *   replaced.  We need to fix it up.  Doing this to avoid
		 *   having to hold a write lock around all this code.
		 */

		write_lock(&sd_lock);

		/* task is guaranteed to have a SubDomain (->security)
		 * by this point
		 */
		latest_sd = SD_SUBDOMAIN(current->security);

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
				write_unlock(&sd_lock);
				goto find_profile;
			}
		}

		/* need to drop reference counts we obtained in get_sdcopy
		 * above.  Need to do it before overwriting latest_sd, in
		 * case latest_sd == sd (no async replacement has taken place).
		 */
		if (issdcopy) {
			put_sdcopy(sd);
			issdcopy = 0;
		}

		sd_switch(latest_sd, newprofile, newprofile);
		put_sdprofile(newprofile);

		if (complain && newprofile == null_complain_profile)
			SD_WARN("LOGPROF-HINT changing_profile pid=%d\n",
				current->pid);

		write_unlock(&sd_lock);
	}

	sd_put_name(filename);

	if (issdcopy)
		put_sdcopy(sd);

out:
	return error;
}

/**
 * sd_release - release the task's SubDomain
 * @p: task being released
 *
 * This is called after a task has exited and the parent has reaped it.
 * @p->security blob is freed.
 */
void sd_release(struct task_struct *p)
{
	struct subdomain *sd = SD_SUBDOMAIN(p->security);
	if (sd) {
		p->security = NULL;

		sd_subdomainlist_remove(sd);

		/* release profiles */
		put_sdprofile(sd->profile);
		put_sdprofile(sd->active);

		kfree(sd);
	}
}

/*****************************
 * GLOBAL SUBPROFILE FUNCTIONS
 ****************************/

/**
 * do_change_hat - actually switch hats
 * @name: name of hat to swtich to
 * @sd: current SubDomain
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
			SD_WARN("LOGPROF-HINT unknown_hat %s "
				"pid=%d profile=%s active=%s\n",
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
 * the @hat_magic in the current SubDomain.  If the new @hat_name is
 * NULL, and the @hat_magic matches that stored in the current SubDomain
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

	/* no SubDomains: changehat into the null_profile, since the process
	   has no SubDomains do_change_hat won't find a match which will cause
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
