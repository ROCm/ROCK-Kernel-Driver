/* ckrmutils.c - Utility functions for CKRM
 *
 * Copyright (C) Chandra Seetharaman,  IBM Corp. 2003
 *           (C) Hubertus Franke    ,  IBM Corp. 2004
 * 
 * Provides simple utility functions for the core module, CE and resource
 * controllers.
 *
 * Latest version, more details at http://ckrm.sf.net
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

/* Changes
 * 
 * 13 Nov 2003
 *        Created
 */

#include <linux/mm.h>
#include <linux/err.h>
#include <linux/mount.h>
#include <linux/module.h>
#include <linux/ckrm_rc.h>

int
get_exe_path_name(struct task_struct *tsk, char *buf, int buflen)
{
	struct vm_area_struct * vma;
	struct vfsmount *mnt;
	struct mm_struct * mm = get_task_mm(tsk);
	struct dentry *dentry;
	char *lname;
	int rc = 0;

	*buf = '\0';
	if (!mm) {
		return -EINVAL;
	}

	down_read(&mm->mmap_sem);
	vma = mm->mmap;
	while (vma) {
		if ((vma->vm_flags & VM_EXECUTABLE) &&
				vma->vm_file) {
			dentry = dget(vma->vm_file->f_dentry);
			mnt = mntget(vma->vm_file->f_vfsmnt);
			lname = d_path(dentry, mnt, buf, buflen);
			if (! IS_ERR(lname)) {
				strncpy(buf, lname, strlen(lname) + 1);
			} else {
				rc = (int) PTR_ERR(lname);
			}
			mntput(mnt);
			dput(dentry);
			break;
		}
		vma = vma->vm_next;
	}
	up_read(&mm->mmap_sem);
	mmput(mm);
	return rc;
}


/*
 * must be called with cnt_lock of parres held
 * Caller is responsible for making sure that the new guarantee doesn't
 * overflow parent's total guarantee.
 */
void
child_guarantee_changed(struct ckrm_shares *parent, int cur, int new)
{
	if (new == cur || !parent) {
		return;
	}
	if (new != CKRM_SHARE_DONTCARE) {
		parent->unused_guarantee -= new;
	}
	if (cur != CKRM_SHARE_DONTCARE) {
		parent->unused_guarantee += cur;
	}
	return;
}

/*
 * must be called with cnt_lock of parres held
 * Caller is responsible for making sure that the new limit is not more 
 * than parent's max_limit
 */
void
child_maxlimit_changed(struct ckrm_shares *parent, int new_limit)
{
	if (parent && parent->cur_max_limit < new_limit) {
		parent->cur_max_limit = new_limit;
	}
	return;
}

/*
 * Caller is responsible for holding any lock to protect the data
 * structures passed to this function
 */
int
set_shares(struct ckrm_shares *new, struct ckrm_shares *cur,
		struct ckrm_shares *par)
{
	int rc = -EINVAL;
	int cur_usage_guar = cur->total_guarantee - cur->unused_guarantee;
	int increase_by = new->my_guarantee - cur->my_guarantee;

	// Check total_guarantee for correctness
	if (new->total_guarantee <= CKRM_SHARE_DONTCARE) {
		goto set_share_err;
	} else if (new->total_guarantee == CKRM_SHARE_UNCHANGED) {
		;// do nothing
	} else if (cur_usage_guar > new->total_guarantee) {
		goto set_share_err;
	}

	// Check max_limit for correctness
	if (new->max_limit <= CKRM_SHARE_DONTCARE) {
		goto set_share_err;
	} else if (new->max_limit == CKRM_SHARE_UNCHANGED) {
		; // do nothing
	} else if (cur->cur_max_limit > new->max_limit) {
		goto set_share_err;
	}

	// Check my_guarantee for correctness
	if (new->my_guarantee == CKRM_SHARE_UNCHANGED) {
		; // do nothing
	} else if (new->my_guarantee == CKRM_SHARE_DONTCARE) {
		; // do nothing
	} else if (par && increase_by > par->unused_guarantee) {
		goto set_share_err;
	}

	// Check my_limit for correctness
	if (new->my_limit == CKRM_SHARE_UNCHANGED) {
		; // do nothing
	} else if (new->my_limit == CKRM_SHARE_DONTCARE) {
		; // do nothing
	} else if (par && new->my_limit > par->max_limit) {
		// I can't get more limit than my parent's limit
		goto set_share_err;
		
	}

	// make sure guarantee is lesser than limit
	if (new->my_limit == CKRM_SHARE_DONTCARE) {
		; // do nothing
	} else if (new->my_limit == CKRM_SHARE_UNCHANGED) {
		if (new->my_guarantee == CKRM_SHARE_DONTCARE) {
			; // do nothing
		} else if (new->my_guarantee == CKRM_SHARE_UNCHANGED) {
			; // do nothing earlier setting would 've taken care of it
		} else if (new->my_guarantee > cur->my_limit) {
			goto set_share_err;
		}
	} else { // new->my_limit has a valid value
		if (new->my_guarantee == CKRM_SHARE_DONTCARE) {
			; // do nothing
		} else if (new->my_guarantee == CKRM_SHARE_UNCHANGED) {
			if (cur->my_guarantee > new->my_limit) {
				goto set_share_err;
			}
		} else if (new->my_guarantee > new->my_limit) {
			goto set_share_err;
		}
	}

	if (new->my_guarantee != CKRM_SHARE_UNCHANGED) {
		child_guarantee_changed(par, cur->my_guarantee,
				new->my_guarantee);
		cur->my_guarantee = new->my_guarantee;
	}

	if (new->my_limit != CKRM_SHARE_UNCHANGED) {
		child_maxlimit_changed(par, new->my_limit);
		cur->my_limit = new->my_limit;
	}

	if (new->total_guarantee != CKRM_SHARE_UNCHANGED) {
		cur->unused_guarantee = new->total_guarantee - cur_usage_guar;
		cur->total_guarantee = new->total_guarantee;
	}

	if (new->max_limit != CKRM_SHARE_UNCHANGED) {
		cur->max_limit = new->max_limit;
	}

	rc = 0;
set_share_err:
	return rc;
}

EXPORT_SYMBOL(get_exe_path_name);
EXPORT_SYMBOL(child_guarantee_changed);
EXPORT_SYMBOL(child_maxlimit_changed);
EXPORT_SYMBOL(set_shares);


