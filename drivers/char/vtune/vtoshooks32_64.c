/*
 *  vtoshooks32_64.c
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
 *	File: vtoshooks32_64.c
 *
 *	Description: OS hooks for sampling on Pentium(R) 4 processors
 *	             with Intel(R) Extended Memory 64 Technology
 *
 *	Author(s): George Artz, Intel Corp.
 *                 Juan Villacis, Intel Corp. (sys_call_table scan support)
 *                 Thomas M Johnson, Intel Corp. (sys_call_table support on
 *                                                Intel(R) EM64T platforms)
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
#include <asm/ia32_unistd.h>
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

#ifdef EXPORTED_IA32_SYS_CALL_TABLE
extern unsigned long ia32_sys_call_table[];
#else
// TODO:  need to handle special cases for searching for ia32_sys_call_table
// currently only support symbol being directly exported
static void **ia32_sys_call_table = NULL;
#endif

void vt_clone_prologue(void);
void vt_clone32_prologue(void);
void vt_fork32_prologue(void);
void vt_vfork32_prologue(void);
void vt_execve32_prologue(void);

struct mmap_arg_struct {
        unsigned int addr;
        unsigned int len;
        unsigned int prot;
        unsigned int flags;
        unsigned int fd;
        unsigned int offset;
};

struct short_sys32_pt_regs {
	ulong r11;
	ulong r10;
	ulong r9;
	ulong r8;
	ulong rax;
	ulong rcx;
	ulong rdx;
	ulong rsi;
	ulong rdi;
	ulong orig_rax;
};

struct short_pt_regs {
	struct short_sys32_pt_regs sys32_regs;
	ulong rip;
	ulong cs;
	ulong eflags;
	ulong rsp;
	ulong ss;
};

/*
 * typedefs for 64-bit entry points
 */
typedef asmlinkage long (*sys_fork_t) (struct short_pt_regs regs);
typedef asmlinkage long (*sys_vfork_t) (struct short_pt_regs regs);
typedef asmlinkage long (*sys_clone_t) (struct short_pt_regs regs);
typedef asmlinkage long (*sys_execve_t) (char* name, char** argv,
					 char** envp, struct short_pt_regs regs);
typedef asmlinkage long (*sys_mmap_t) (unsigned long addr, unsigned long len,
					unsigned long prot, unsigned long flags,
					unsigned long fd, unsigned long pgoff);
typedef asmlinkage long (*sys_create_module_t) (char *name, long size);

/*
 * typedefs for 32-bit entry points
 */
typedef asmlinkage long (*sys32_mmap2_t) (unsigned long addr, unsigned long len,
					unsigned long prot, unsigned long flags,
					unsigned long fd, unsigned long pgoff);
typedef asmlinkage long (*sys32_fork_t) (struct short_pt_regs regs);
typedef asmlinkage long (*sys32_vfork_t) (struct short_pt_regs regs);
typedef asmlinkage long (*sys32_clone_t) (unsigned int flags, unsigned int newsp, struct short_pt_regs regs);
typedef asmlinkage long (*sys32_mmap_t) (struct mmap_arg_struct* arg);
typedef asmlinkage long (*sys32_execve_t) (char* name, u32 argv, u32 envp, struct short_pt_regs regs);

/*
 * function pointers for original 64-bit entry points
 */
sys_fork_t original_sys_fork = NULL;
sys_vfork_t original_sys_vfork = NULL;
sys_clone_t original_sys_clone = NULL;
sys_mmap_t original_sys_mmap = NULL;
sys_execve_t original_sys_execve = NULL;
sys_create_module_t original_sys_create_module = NULL;

/*
 * function pointers for original 32-bit entry points
 */
sys32_mmap2_t original_ia32_sys_mmap2 = NULL;
sys32_fork_t original_ia32_sys_fork = NULL;
sys32_vfork_t original_ia32_sys_vfork = NULL;
sys32_clone_t original_ia32_sys_clone = NULL;
sys32_mmap_t original_ia32_sys_mmap = NULL;
sys32_execve_t original_ia32_sys_execve = NULL;

void
enum_modules_for_process(struct task_struct *p, PMGID_INFO pmgid_info)
{
	unsigned int i, options;
	unsigned short mode;
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
		   mode = get_exec_mode(p);
		   VDK_PRINT_DEBUG("enum_modules_for_process: name=%s, mode=%d\n",pname,mode);
		   samp_load_image_notify_routine(pname,
						mmap->vm_start,
						(mmap->vm_end -
						 mmap->vm_start),
						p->pid, options,
						pmgid_info, mode);
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

asmlinkage long
vt_sys_fork(struct short_pt_regs regs)
{
	struct short_pt_regs* pregs;
	long ret = -EINVAL;
	MGID_INFO mgid_info;

	//	atomic_inc(&hook_in_progress);

	if (track_module_loads) {
		alloc_module_group_ID(&mgid_info);
		enum_modules_for_process(current, &mgid_info);
	}

	ret = original_sys_fork(regs);
	__asm__("movq %%rsp, %0" : "=m" (pregs));
	regs = *pregs;

	if (track_module_loads && (ret >= 0 )) {
		update_pid_for_module_group(ret, &mgid_info);
	}

	//	atomic_dec(&hook_in_progress);

	return (ret);
}

asmlinkage long
vt_sys_vfork(struct short_pt_regs regs)
{
	struct short_pt_regs *pregs;
	long ret = -EINVAL;
	MGID_INFO mgid_info;

	//	atomic_inc(&hook_in_progress);

	if (track_module_loads) {
		alloc_module_group_ID(&mgid_info);
		enum_modules_for_process(current, &mgid_info);
	}

	ret = original_sys_vfork(regs);
	__asm__("movq %%rsp, %0" : "=m" (pregs));
	regs = *pregs;

	if (track_module_loads && (ret >= 0)) {
		update_pid_for_module_group(ret, &mgid_info);
	}

	//	atomic_dec(&hook_in_progress);

	return (ret);
}

asmlinkage void* vt_sys_clone_enter(void)
{
    PMGID_INFO pmgid_info = NULL;
    
    //    atomic_inc(&hook_in_progress);
    
    if (track_module_loads) {
        pmgid_info = kmalloc(sizeof (MGID_INFO), GFP_ATOMIC);
        VDK_PRINT_DEBUG("clone: before... pmgid_info %p\n", pmgid_info);
        if (pmgid_info) {
            alloc_module_group_ID(pmgid_info);
            enum_modules_for_process(current, pmgid_info);
        } else {
            VDK_PRINT("clone: unable to allocate mgid_info \n");
        }
    }

    return (pmgid_info);
}

asmlinkage void vt_sys_clone_exit(long ret, void* ptr)
{
    VDK_PRINT_DEBUG("clone: after.... pid 0x%x pmgid_info 0x%p\n", ret, ptr);
    if (ptr) {
        if (track_module_loads && (ret > 0)) {
            update_pid_for_module_group(ret, ptr);
        }
        kfree(ptr);
    }

    //    atomic_dec(&hook_in_progress);

    return;
}

asmlinkage long
vt_sys_execve(char* name, char** argv, char** envp, struct short_pt_regs regs)
{
	struct short_pt_regs *pregs;
	long ret = -EINVAL;

	//	atomic_inc(&hook_in_progress);

	ret = original_sys_execve(name, argv, envp, regs);
	__asm__("movq %%rsp, %0" : "=m" (pregs));
	regs = *pregs;

	if (track_module_loads && !ret) {
		VDK_PRINT_DEBUG("exec: %d\n", current->pid);
		//samp_create_process_notify_routine(0, current->pid, 1);
		enum_modules_for_process(current, 0);
	}

	//	atomic_dec(&hook_in_progress);

	return (ret);
}

asmlinkage long
vt_sys_mmap(unsigned long addr, unsigned long len,
	    unsigned long prot, unsigned long flags,
	    unsigned long fd, unsigned long pgoff)
{
	long ret = -EINVAL;
	struct file *file;

	VDK_PRINT_DEBUG("vt_sys_mmap() entered pid %d \n", current->pid);

	//	atomic_inc(&hook_in_progress);

	ret = original_sys_mmap(addr, len, prot, flags, fd, pgoff);

	if (track_module_loads && !IS_ERR((void *) ret) && (prot & PROT_EXEC)
	    && (fd)) {

		if ((file = fcheck(fd)) != NULL) {
			char *pname;
			char name[MAXNAMELEN];
			memset(name, 0, MAXNAMELEN * sizeof (char));
			pname = d_path(file->f_dentry,
				       file->f_vfsmnt, name, MAXNAMELEN-1);
			if (pname) {
                                VDK_PRINT_DEBUG("vt_sys_mmap:  %s, %d, %x \n",
					     pname, current->pid, ret);
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

	VDK_PRINT_DEBUG("vt_sys_module() entered pid %d\n", current->pid);

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
                VDK_PRINT_DEBUG("create_module: %s, %d, %x, %x \n",
			     mod->name, current->pid, ret, msize);
		samp_load_image_notify_routine((char *) mod->name, ret, msize,
					   0, LOPTS_GLOBAL_MODULE,
					   (PMGID_INFO) 0, get_exec_mode(current));
	}

	//	atomic_dec(&hook_in_progress);

	return (ret);
}

asmlinkage void*
vt_sys32_fork_enter(void)
{
	PMGID_INFO pmgid_info = NULL;

	//	atomic_inc(&hook_in_progress);

	if (track_module_loads) {
	  pmgid_info = kmalloc(sizeof (MGID_INFO), GFP_ATOMIC);
	  VDK_PRINT_DEBUG("fork32: before... pmgid_info %p\n", pmgid_info);
	  if (pmgid_info) {
	      alloc_module_group_ID(pmgid_info);
	      enum_modules_for_process(current, pmgid_info);
	  } else {
            VDK_PRINT("fork32: unable to allocate mgid_info \n");
	  }
	}

	return (pmgid_info);
}

asmlinkage void
vt_sys32_fork_exit(long ret, void* ptr)
{
    VDK_PRINT_DEBUG("fork32: after.... pid 0x%x pmgid_info 0x%p\n", ret, ptr);
    if (ptr) {
        if (track_module_loads && (ret > 0)) {
	    VDK_PRINT_DEBUG("fork32: updating pids for module group\n");
            update_pid_for_module_group(ret, ptr);
        }
        kfree(ptr);
    }

    //    atomic_dec(&hook_in_progress);

    return;
}

asmlinkage void*
vt_sys32_vfork_enter(void)
{
	PMGID_INFO pmgid_info = NULL;

	//	atomic_inc(&hook_in_progress);

	if (track_module_loads) {
	  pmgid_info = kmalloc(sizeof (MGID_INFO), GFP_ATOMIC);
	  VDK_PRINT_DEBUG("vfork32: before... pmgid_info %p\n", pmgid_info);
	  if (pmgid_info) {
	      alloc_module_group_ID(pmgid_info);
	      enum_modules_for_process(current, pmgid_info);
	  } else {
            VDK_PRINT("vfork32: unable to allocate mgid_info \n");
	  }
	}

	return (pmgid_info);
}

asmlinkage void
vt_sys32_vfork_exit(long ret, void* ptr)
{
    VDK_PRINT_DEBUG("vfork32: after.... pid 0x%x pmgid_info 0x%p\n", ret, ptr);
    if (ptr) {
        if (track_module_loads && (ret > 0)) {
	    VDK_PRINT_DEBUG("vfork32: updating pids for module group\n");
            update_pid_for_module_group(ret, ptr);
        }
        kfree(ptr);
    }

    //    atomic_dec(&hook_in_progress);

    return;
}

asmlinkage void*
vt_sys32_clone_enter(void)
{
    PMGID_INFO pmgid_info = NULL;

    //    atomic_inc(&hook_in_progress);
    
    if (track_module_loads) {
        pmgid_info = kmalloc(sizeof (MGID_INFO), GFP_ATOMIC);
        VDK_PRINT_DEBUG("clone: before... pmgid_info %p\n", pmgid_info);
        if (pmgid_info) {
            alloc_module_group_ID(pmgid_info);
            enum_modules_for_process(current, pmgid_info);
        } else {
            VDK_PRINT("clone: unable to allocate mgid_info \n");
        }
    }

    return (pmgid_info);
}

asmlinkage void
vt_sys32_clone_exit(long ret, void* ptr)
{
    VDK_PRINT_DEBUG("clone: after.... pid 0x%x pmgid_info 0x%p\n", ret, ptr);
    if (ptr) {
        if (track_module_loads && (ret > 0)) {
	    VDK_PRINT_DEBUG("vt_sys32_clone_exit: updating pids for module group\n");
            update_pid_for_module_group(ret, ptr);
        }
        kfree(ptr);
    }

    //    atomic_dec(&hook_in_progress);

    return;
}

asmlinkage void*
vt_sys32_execve_enter(void)
{
    //	atomic_inc(&hook_in_progress);
    
    return 0;
}

asmlinkage void
vt_sys32_execve_exit(long ret, void* ptr)
{
    if (track_module_loads && !ret) {
	enum_modules_for_process(current, 0);
    }
    
    //	atomic_dec(&hook_in_progress);

    return;
}

asmlinkage long
vt_sys32_mmap(struct mmap_arg_struct* map_arg)
{
	long ret = -EINVAL;
	struct mmap_arg_struct tmp;
	struct file *file;

	//	atomic_inc(&hook_in_progress);

	if (track_module_loads) {
	  if (copy_from_user(&tmp, map_arg, sizeof (tmp)))
	    //	    atomic_dec(&hook_in_progress);
	    return (ret);
	}

	ret = original_ia32_sys_mmap(map_arg);

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
vt_sys32_mmap2(unsigned long addr,
	     unsigned long len,
	     unsigned long prot,
	     unsigned long flags, unsigned long fd, unsigned long pgoff)
{
	long ret = -EINVAL;
	struct file *file;

	//	atomic_inc(&hook_in_progress);

	VDK_PRINT_DEBUG("vt_sys32_mmap2() entered pid %d \n", current->pid);

	ret = original_ia32_sys_mmap2(addr, len, prot, flags, fd, pgoff);

	if (track_module_loads && !IS_ERR((void *) ret) && (prot & PROT_EXEC)) {
		if ((file = fcheck(fd)) != NULL) {
			char *pname;
			char name[MAXNAMELEN];
			memset(name, 0, MAXNAMELEN * sizeof (char));
			pname = d_path(file->f_dentry,
				       file->f_vfsmnt, name, MAXNAMELEN-1);
			if (pname) {
                                VDK_PRINT_DEBUG("vt_sys32_mmap2: %s, %d, %x, %x \n",
					     pname, current->pid, ret, len);
			samp_load_image_notify_routine(pname, ret, len,
						   current->pid, 0, 0, get_exec_mode(current));
			}
		}
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

#ifndef EXPORTED_SYS_CALL_TABLE
  sys_call_table = find_sys_call_table_symbol(1);
#endif

  if (sys_call_table)
  {
        // handle 64-bit entry points
	original_sys_fork =
	    (sys_fork_t) xchg(&sys_call_table[__NR_fork], vt_sys_fork);
	original_sys_vfork =
	    (sys_vfork_t) xchg(&sys_call_table[__NR_vfork], vt_sys_vfork);
	original_sys_clone =
	    (sys_clone_t) xchg(&sys_call_table[__NR_clone], vt_clone_prologue);
	original_sys_mmap =
	    (sys_mmap_t) xchg(&sys_call_table[__NR_mmap], vt_sys_mmap);
	original_sys_execve =
	    (sys_execve_t) xchg(&sys_call_table[__NR_execve], vt_sys_execve);
	original_sys_create_module =
	    (sys_create_module_t) xchg(&sys_call_table[__NR_create_module],
				       vt_sys_create_module);
  }
  else
    VDK_PRINT_WARNING("64-bit process creates and image loads will not be tracked during sampling session\n");

  if (FALSE && ia32_sys_call_table)
  {
	// handle 32-bit entry points
	original_ia32_sys_mmap2 =
	    (sys32_mmap2_t) xchg(&ia32_sys_call_table[__NR_ia32_mmap2], vt_sys32_mmap2);
	original_ia32_sys_fork =
	    (sys32_fork_t) xchg(&ia32_sys_call_table[__NR_ia32_fork], vt_fork32_prologue);
	original_ia32_sys_vfork =
	    (sys32_vfork_t) xchg(&ia32_sys_call_table[__NR_ia32_vfork], vt_vfork32_prologue);
	original_ia32_sys_clone =
	    (sys32_clone_t) xchg(&ia32_sys_call_table[__NR_ia32_clone], vt_clone32_prologue);
	original_ia32_sys_mmap =
	    (sys32_mmap_t) xchg(&ia32_sys_call_table[__NR_ia32_mmap], vt_sys32_mmap);
	original_ia32_sys_execve =
	    (sys32_execve_t) xchg(&ia32_sys_call_table[__NR_ia32_execve], vt_execve32_prologue);
  }
  else
    VDK_PRINT_WARNING("32-bit process creates and image loads will not be tracked during sampling session\n");

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

#ifndef EXPORTED_SYS_CALL_TABLE
  sys_call_table = find_sys_call_table_symbol(0);
#endif

  if (sys_call_table)
  {
        // restore 64-bit entry points
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
	if ((org_fn = xchg(&original_sys_execve, 0))) {
		xchg(&sys_call_table[__NR_execve], org_fn);
	}
	if ((org_fn = xchg(&original_sys_create_module, 0))) {
		xchg(&sys_call_table[__NR_create_module], org_fn);
	}
  }

  if (FALSE && ia32_sys_call_table)
  {
	// restore 32-bit entry points
	if ((org_fn = xchg(&original_ia32_sys_mmap2, 0))) {
		xchg(&ia32_sys_call_table[__NR_ia32_mmap2], org_fn);
	}
	if ((org_fn = xchg(&original_ia32_sys_fork, 0))) {
		xchg(&ia32_sys_call_table[__NR_ia32_fork], org_fn);
	}
	if ((org_fn = xchg(&original_ia32_sys_vfork, 0))) {
		xchg(&ia32_sys_call_table[__NR_ia32_vfork], org_fn);
	}
	if ((org_fn = xchg(&original_ia32_sys_clone, 0))) {
		xchg(&ia32_sys_call_table[__NR_ia32_clone], org_fn);
	}
	if ((org_fn = xchg(&original_ia32_sys_mmap, 0))) {
		xchg(&ia32_sys_call_table[__NR_ia32_mmap], org_fn);
	}
	if ((org_fn = xchg(&original_ia32_sys_execve, 0))) {
		xchg(&ia32_sys_call_table[__NR_ia32_execve], org_fn);
	}
  }

  return;
}
