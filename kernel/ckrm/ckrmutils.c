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



/*
 *  functions that come in handy for debugging 
 */

#if 0

#include <linux/ckrm_rc.h>

void
check_tasklist_sanity(struct ckrm_core_class *core)
{
	struct list_head *lh1, *lh2;
	int count = 0;

	if (core) {
		spin_lock(&core->ckrm_lock);
		if (list_empty(&core->tasklist)) {
			spin_unlock(&core->ckrm_lock);
			printk("check_tasklist_sanity: class %s empty list\n",
					core->name);
			return;
		}
		list_for_each_safe(lh1, lh2, &core->tasklist) {
			struct task_struct *tsk = container_of(lh1, struct task_struct, ckrm_link);
			if (count++ > 20000) {
				printk("list is CORRUPTED\n");
				break;
			}
			if (tsk->ckrm_core != core) {
				ckrm_core_class_t *tcore = tsk->ckrm_core;
				printk("sanity: task %s:%d has ckrm_core |%s| but in list |%s|\n",
						tsk->comm,tsk->pid,
						tcore ? tcore->name: "NULL",
						core->name);
			}
		}
		spin_unlock(&core->ckrm_lock);
	}
}

void 
ckrm_debug_free_core_class(struct ckrm_core_class *core)
{
	struct task_struct *proc, *thread;
	int count = 0;

	printk("Analyze Error <%s> %d\n",core->name,atomic_read(&core->refcnt));
	read_lock(&tasklist_lock);
	spin_lock(&core->ckrm_lock);
	do_each_thread(proc, thread) {
		struct ckrm_core_class *tcore = (struct ckrm_core_class*) (thread->ckrm_core);
		count += (core == thread->ckrm_core);
		printk("%d thread=<%s:%d>  -> <%s> <%lx>\n",
			count,thread->comm,thread->pid,tcore ? tcore->name : "NULL", thread->flags & PF_EXITING);
	} while_each_thread(proc, thread);
	spin_unlock(&core->ckrm_lock);
	read_unlock(&tasklist_lock);
	printk("End Analyze Error <%s> %d\n",core->name,atomic_read(&core->refcnt));
} 

#endif
