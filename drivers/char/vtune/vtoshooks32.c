/*
 *  vtoshooks32.c
 *
 *  Copyright (C) 2002-2004 Intel Corporation
 *  Maintainer - Juan Villacis <juan.villacis@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
/*
 * ===========================================================================
 *
 *	File: vtoshooks32.c
 *
 *	Description: OS hooks for sampling on IA32 platforms
 *
 *	Author(s): George Artz, Intel Corp.
 *                 Juan Villacis, Intel Corp. (sys_call_table scan support)
 *
 *	System: VTune(TM) Performance Analyzer Driver Kit for Linux*
 *
 * ===========================================================================
 */

#include <linux/vmalloc.h>
#include <linux/slab.h>		/*malloc */
#include <linux/interrupt.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/module.h>
#include <asm/mman.h>

#ifdef USE_MM_HEADER
#include <linux/mm.h>
#endif

#include "vtdef.h"
#include "vtuneshared.h"
#include "vtoshooks.h"

#ifdef EXPORTED_SYS_CALL_TABLE
extern unsigned long sys_call_table[];
#else
static void **sys_call_table;
#endif  /* EXPORTED_SYS_CALL_TABLE */

// static atomic_t hook_in_progress = ATOMIC_INIT(0);   // track number of hooks being executed

struct mmap_arg {
	unsigned long addr;
	unsigned long len;
	unsigned long prot;
	unsigned long flags;
	unsigned long fd;
	unsigned long pgoff;
};

typedef asmlinkage int (*sys_fork_t) (struct pt_regs regs);
typedef asmlinkage int (*sys_vfork_t) (struct pt_regs regs);
typedef asmlinkage int (*sys_clone_t) (struct pt_regs regs);
typedef asmlinkage int (*sys_execve_t) (struct pt_regs regs);
typedef asmlinkage long (*sys_mmap_t) (struct mmap_arg * arg);
typedef asmlinkage long (*sys_mmap2_t) (unsigned long addr, unsigned long len,
					unsigned long prot, unsigned long flags,
					unsigned long fd, unsigned long pgoff);
typedef asmlinkage long (*sys_create_module_t) (char *name, long size);

sys_fork_t original_sys_fork = NULL;	// address of original routine
sys_vfork_t original_sys_vfork = NULL;	// address of original routine
sys_clone_t original_sys_clone = NULL;	// address of original routine
sys_mmap_t original_sys_mmap = NULL;	// address of original routine
sys_mmap2_t original_sys_mmap2 = NULL;	// address of original routine
sys_execve_t original_sys_execve = NULL;	// address of original routine
sys_create_module_t original_sys_create_module = NULL;	// address of original routine

void
enum_modules_for_process(struct task_struct *p, PMGID_INFO pmgid_info)
{
	unsigned int i, options;
	struct vm_area_struct *mmap;
	char name[MAXNAMELEN];
	char *pname = NULL;

	//
	//  Call driver notification routine for each module 
	//  that is mapped into the process created by the fork
	//
	if (!p)
		return;
	if (!p->mm)
		return;

#ifdef SUPPORTS_MMAP_READ
	mmap_down_read(p->mm);
#else
	down_read((struct rw_semaphore *) &p->mm->mmap_sem);
#endif
	i = 0;
	for (mmap = p->mm->mmap; mmap; mmap = mmap->vm_next) {
	    if ((mmap->vm_flags & VM_EXEC) && (!(mmap->vm_flags & VM_WRITE))
		&& mmap->vm_file && mmap->vm_file->f_dentry) {
		memset(name, 0, MAXNAMELEN * sizeof (char));
		pname = d_path(mmap->vm_file->f_dentry,
			   mmap->vm_file->f_vfsmnt, name, MAXNAMELEN-1);
		if (pname) {
		   VDK_PRINT_DEBUG("enum: %s, %d, %lx, %lx \n",
				   pname, p->pid, mmap->vm_start,
				   (mmap->vm_end - mmap->vm_start));
		   options = 0;
		   if (i == 0) {
		       options |= LOPTS_1ST_MODREC;
		       i++;
		   }
		   samp_load_image_notify_routine(pname,
						mmap->vm_start,
						(mmap->vm_end -
						 mmap->vm_start),
						p->pid, options,
						pmgid_info, get_exec_mode(p));
		}
	    }
	}

#ifdef SUPPORTS_MMAP_READ
	mmap_up_read(p->mm);
#else
	up_read((struct rw_semaphore *) &p->mm->mmap_sem);
#endif
	return;
}

asmlinkage int
vt_sys_fork(struct pt_regs regs)
{
	struct pt_regs *tmp;
	long ret = -EINVAL;
	MGID_INFO mgid_info;

	//	atomic_inc(&hook_in_progress);

	if (track_module_loads) {
		alloc_module_group_ID(&mgid_info);
		enum_modules_for_process(current, &mgid_info);
	}

	ret = original_sys_fork(regs);
	__asm__("movl %%esp, %0": "=m"(tmp):);
	regs = *tmp;

	if (track_module_loads && (ret >= 0 )) {
		update_pid_for_module_group(ret, &mgid_info);
	}

	//	atomic_dec(&hook_in_progress);

	return (ret);
}

asmlinkage int
vt_sys_vfork(struct pt_regs regs)
{
	struct pt_regs *tmp;
	long ret = -EINVAL;
	MGID_INFO mgid_info;

	//	atomic_inc(&hook_in_progress);

	if (track_module_loads) {
		alloc_module_group_ID(&mgid_info);
		enum_modules_for_process(current, &mgid_info);
	}

	ret = original_sys_vfork(regs);
	__asm__("movl %%esp, %0": "=m"(tmp):);
	regs = *tmp;

	if (track_module_loads && (ret >= 0)) {
		update_pid_for_module_group(ret, &mgid_info);
	}

	//	atomic_dec(&hook_in_progress);

	return (ret);
}

asmlinkage int
vt_sys_clone(struct pt_regs regs)
{
	struct pt_regs *tmp;
	long ret = -EINVAL;
	MGID_INFO mgid_info;

	//	atomic_inc(&hook_in_progress);

	if (track_module_loads) {
		alloc_module_group_ID(&mgid_info);
		enum_modules_for_process(current, &mgid_info);
	}

	ret = original_sys_clone(regs);
	__asm__("movl %%esp, %0": "=m"(tmp):);
	regs = *tmp;

	if (track_module_loads && (ret >= 0)) {
		update_pid_for_module_group(ret, &mgid_info);
	}

	//	atomic_dec(&hook_in_progress);

	return (ret);
}

asmlinkage long
vt_sys_execve(struct pt_regs regs)
{
	struct pt_regs *tmp;
	long ret = -EINVAL;

	//	atomic_inc(&hook_in_progress);

	ret = original_sys_execve(regs);
	__asm__("movl %%esp, %0": "=m"(tmp):);
	regs = *tmp;

	if (track_module_loads && !ret) {
		enum_modules_for_process(current, 0);
	}

	//	atomic_dec(&hook_in_progress);

	return (ret);
}

asmlinkage long
vt_sys_mmap(struct mmap_arg *map_arg)
{
	long ret = -EINVAL;
	struct mmap_arg tmp;
	struct file *file;

	//	atomic_inc(&hook_in_progress);

	if (track_module_loads) {
	  if (copy_from_user(&tmp, map_arg, sizeof (tmp)))
	    //	    atomic_dec(&hook_in_progress);
	    return (ret);
	}

	ret = original_sys_mmap(map_arg);

	if (track_module_loads && !IS_ERR((void *) ret) && (tmp.prot & PROT_EXEC)
	    && (tmp.fd)) {

		if ((file = fcheck(tmp.fd)) != NULL) {
			char *pname;
			char name[MAXNAMELEN];
			memset(name, 0, MAXNAMELEN * sizeof (char));
			pname = d_path(file->f_dentry,
				       file->f_vfsmnt, name, MAXNAMELEN-1);
			if (pname) {
				samp_load_image_notify_routine(pname, ret, tmp.len,
							   current->pid, 0, 0, get_exec_mode(current));
			}
		}
	}

	//	atomic_dec(&hook_in_progress);

	return (ret);
}

asmlinkage long
vt_sys_mmap2(unsigned long addr,
	     unsigned long len,
	     unsigned long prot,
	     unsigned long flags, unsigned long fd, unsigned long pgoff)
{
	long ret = -EINVAL;
	struct file *file;

	//	atomic_inc(&hook_in_progress);

	ret = original_sys_mmap2(addr, len, prot, flags, fd, pgoff);

	if (track_module_loads && !IS_ERR((void *) ret) && (prot & PROT_EXEC)) {
		if ((file = fcheck(fd)) != NULL) {
			char *pname;
			char name[MAXNAMELEN];
			memset(name, 0, MAXNAMELEN * sizeof (char));
			pname = d_path(file->f_dentry,
				       file->f_vfsmnt, name, MAXNAMELEN-1);
			if (pname) {
			  samp_load_image_notify_routine(pname, ret, len,
						   current->pid, 0, 0, get_exec_mode(current));
			}
		}
	}

	//	atomic_dec(&hook_in_progress);

	return (ret);
}

asmlinkage long
vt_sys_create_module(char *name, long size)
{
	long ret = -EINVAL;

	//	atomic_inc(&hook_in_progress);

	ret = original_sys_create_module(name, size);

	if (track_module_loads && !IS_ERR((void *) ret)) {
		struct module *mod = (struct module *) ret;
		int msize;
#ifdef KERNEL_26X
		msize = mod->init_size + mod->core_size;
#else
		msize = mod->size;
#endif
		samp_load_image_notify_routine((char *) mod->name, ret, msize,
					   0, LOPTS_GLOBAL_MODULE,
					   (PMGID_INFO) 0, get_exec_mode(current));
	}

	//	atomic_dec(&hook_in_progress);

	return (ret);
}

long
enum_user_mode_modules(void)
{
	struct task_struct *p;

	read_lock(&tasklist_lock);
	for_each_task(p) {
		enum_modules_for_process(p, 0);
	}
	read_unlock(&tasklist_lock);

	return (0);
}

void
install_OS_hooks(void)
{

#if !defined(EXPORTED_SYS_CALL_TABLE) && !defined(KERNEL_26X)
  sys_call_table = find_sys_call_table_symbol(1);
#endif

  if (sys_call_table)
  {
	original_sys_fork =
	    (sys_fork_t) xchg(&sys_call_table[__NR_fork], vt_sys_fork);
	original_sys_vfork =
	    (sys_vfork_t) xchg(&sys_call_table[__NR_vfork], vt_sys_vfork);
	original_sys_clone =
	    (sys_clone_t) xchg(&sys_call_table[__NR_clone], vt_sys_clone);
	original_sys_mmap =
	    (sys_mmap_t) xchg(&sys_call_table[__NR_mmap], vt_sys_mmap);
	original_sys_mmap2 =
	    (sys_mmap2_t) xchg(&sys_call_table[__NR_mmap2], vt_sys_mmap2);
	original_sys_execve =
	    (sys_execve_t) xchg(&sys_call_table[__NR_execve], vt_sys_execve);
	original_sys_create_module =
	    (sys_create_module_t) xchg(&sys_call_table[__NR_create_module],
				       vt_sys_create_module);
  }
  else
    VDK_PRINT_WARNING("process creates and image loads will not be tracked during sampling session\n");

  return;
}

void
un_install_OS_hooks(void)
{
  void *org_fn;

  /*
   * NOTE: should check that there are no more "in flight" hooks occuring
   *       before uninstalling the hooks and restoring the original hooks;
   *       this can be done using atomic_inc/dec(&hook_in_progress) within
   *       each of the vt_sys_* functions and then block here until the
   *       hook_in_progress is 0, e.g.,
   *
   *       while (atomic_read(&hook_in_progress)) ; // may want to sleep
   *
   */

#if !defined(EXPORTED_SYS_CALL_TABLE) && !defined(KERNEL_26X)
  sys_call_table = find_sys_call_table_symbol(0);
#endif

  if (sys_call_table)
  {
	if ((org_fn = xchg(&original_sys_fork, 0))) {
		xchg(&sys_call_table[__NR_fork], org_fn);
	}

	if ((org_fn = xchg(&original_sys_vfork, 0))) {
		xchg(&sys_call_table[__NR_vfork], org_fn);
	}

	if ((org_fn = xchg(&original_sys_clone, 0))) {
		xchg(&sys_call_table[__NR_clone], org_fn);
	}

	if ((org_fn = xchg(&original_sys_mmap, 0))) {
		xchg(&sys_call_table[__NR_mmap], org_fn);
	}
	if ((org_fn = xchg(&original_sys_mmap2, 0))) {
		xchg(&sys_call_table[__NR_mmap2], org_fn);
	}
	if ((org_fn = xchg(&original_sys_execve, 0))) {
		xchg(&sys_call_table[__NR_execve], org_fn);
	}

	if ((org_fn = xchg(&original_sys_create_module, 0))) {
		xchg(&sys_call_table[__NR_create_module], org_fn);
	}
  }

  return;
}
