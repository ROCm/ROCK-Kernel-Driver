/* ckrmutils.c - Utility functions for CKRM
 *
 * Copyright (C) Chandra Seetharaman,  IBM Corp. 2003
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

#include <linux/ckrm.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/mount.h>
#include <linux/module.h>

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

EXPORT_SYMBOL(get_exe_path_name);
