/*
 *  vtoshooks64.c
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
 *	File: vtoshooks64.c
 *
 *	Description: Linux* OS hooks for sampling on Itanium(R) processor
 *                   family platforms
 *
 *	Author(s): George Artz, Intel Corp.
 *                 Juan Villacis, Intel Corp. (sys_call_table scan support)
 *
 *	System: VTune(TM) Performance Analyzer Driver Kit for Linux*
 *
 * ===========================================================================
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/init.h>

#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <linux/fs.h>
#include <linux/file.h>
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

static volatile int vtsync;
static spinlock_t clone2_table_lock = SPIN_LOCK_UNLOCKED;

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
    down_read(&p->mm->mmap_sem);
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
    up_read(&p->mm->mmap_sem);
#endif

    return;
}

static void
vt_set_pp_for_cpu(void *arg)
{
    __asm__ __volatile__(";; ssm psr.pp;; srlz.d;;":::"memory");

    itp_set_dcr(itp_get_dcr() | IA64_DCR_PP);

    while (vtsync) ;

    return;
}

//
// Set PSR.PP and DCR.PP to enable system wide perfomance counters
//
// PSR.PP is set for all tasks and set DCR.PP for each cpu.
//
void
set_PP_SW(void)
{
    struct task_struct *p;

    vtsync = 1;

    read_lock(&tasklist_lock);

    smp_call_function(vt_set_pp_for_cpu, NULL, 1, 0);
    __asm__ __volatile__(";; ssm psr.pp;; srlz.d;;":::"memory");

    itp_set_dcr(itp_get_dcr() | IA64_DCR_PP);

    for_each_task(p) {
        struct pt_regs *p_regs;

        // psr.pp should always be one
        p_regs = ia64_task_regs(p);
        ia64_psr(p_regs)->pp = 1;
    }

    vtsync = 0;

    read_unlock(&tasklist_lock);

    return;
}

#ifdef  __ia64
typedef struct {
    long fptr;
    long gp;
} plabel_t;
long kernel_gp;
#endif              /*  */

void *original_sys_mmap = NULL; // address of original routine
void *original_sys_mmap2 = NULL;    // address of original routine
void *original_sys_clone = NULL;    // address of original routine
void *original_sys_clone2 = NULL;   // address of original routine
void *original_sys_create_module = NULL;    // address of original routine
void *original_sys_execve = NULL;   // address of original routine

asmlinkage void
vt_sys_mmap(unsigned long ret, unsigned long len, int prot, int fd)
{
    struct file *file;

    //    atomic_inc(&hook_in_progress);

    if (track_module_loads && !IS_ERR((void *) ret) && (prot & PROT_EXEC)) {
        if ((file = fcheck(fd)) != NULL) {
            char *pname;
            char name[MAXNAMELEN];
            memset(name, 0, MAXNAMELEN * sizeof (char));
            pname = d_path(file->f_dentry, file->f_vfsmnt, name, MAXNAMELEN-1);
            if (pname) {
                VDK_PRINT_DEBUG("mmap: %s, %d, %lx, %lx \n",
				pname, current->pid, ret, len);
                samp_load_image_notify_routine(pname, ret, len,
                               current->pid, 0, 0, get_exec_mode(current));
            }
        }
    }

    //    atomic_dec(&hook_in_progress);

    return;
}

asmlinkage void
vt_sys_mmap2(unsigned long ret, unsigned long len, int prot, int fd)
{
    struct file *file;

    //    atomic_inc(&hook_in_progress);

    if (track_module_loads && !IS_ERR((void *) ret) && (prot & PROT_EXEC)) {
        if ((file = fcheck(fd)) != NULL) {
            char *pname;
            char name[MAXNAMELEN];
            memset(name, 0, MAXNAMELEN * sizeof (char));
            pname = d_path(file->f_dentry, file->f_vfsmnt, name, MAXNAMELEN-1);
            if (pname) {
                VDK_PRINT_DEBUG("mmap2: %s, %d, %lx, %lx \n",
				pname, current->pid, ret, len);
                samp_load_image_notify_routine(pname, ret, len,
                               current->pid, 0, 0, get_exec_mode(current));
            }
        }
    }

    //    atomic_dec(&hook_in_progress);

    return;
}

asmlinkage void *
vt_sys_clone_before(void)
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
vt_sys_clone_after(unsigned long ret, void *ptr)
{
    VDK_PRINT_DEBUG("clone: after.... pid 0x%x pmgid_info 0x%p\n", ret, ptr);
    if (ptr) {
        if (ret > 0) {
            update_pid_for_module_group(ret, ptr);
        }
        kfree(ptr);
    }

    //    atomic_dec(&hook_in_progress);

    return;
}

typedef struct {
  //    u64 gp;
    u64 rp;
  //    u64 arpfs;
} cloneinfo_t;

typedef struct _CLONE_INFO {
    union {
        u32 flags;
        struct {
            u32 entry_in_use:1;
             u32:31;
        };
    };
    u32 pid;
    cloneinfo_t clone_info;
    MGID_INFO mgid_info;
} CLONE_INFO, *PCLONE_INFO;

#define CLONE_TBL_ENTRIES 100
CLONE_INFO clone_tbl[CLONE_TBL_ENTRIES];

//asmlinkage void
//vt_sys_clone2_before(u64 gp, u64 rp, u64 arpfs)
//
// Using a cmp4 in the assembly check so don't change the u32 return
// value without changing the assembly code.
//
// True means we successfully added the info to the clone_tbl, FALSE
// means we failed. The callee can then decide where the clone call should
// return to (ie, don't do the clone2_after() routine if the add
// to the table failed)
//
asmlinkage u32
vt_sys_clone2_before(u64 rp)
{
    u32 i;
    PCLONE_INFO p;

    //    atomic_inc(&hook_in_progress);

    VDK_PRINT_DEBUG("clone2: before... current pid 0x%x \n", current->pid);

    spin_lock(&clone2_table_lock);
    p = &clone_tbl[0];
    for (i = 0; i < CLONE_TBL_ENTRIES; i++, p++) {
        if (!p->entry_in_use) {
            p->entry_in_use = 1;
            break;
        }
    }
    spin_unlock(&clone2_table_lock);

    if (i >= CLONE_TBL_ENTRIES) {
        VDK_PRINT_ERROR("clone2: before... clone table size exceeded\n");
        return FALSE;
    }

    //    p->clone_info.gp = gp;
    p->clone_info.rp = rp;
    //    p->clone_info.arpfs = arpfs;
    p->pid = current->pid;
    p->mgid_info.mgid = 0;

    if (track_module_loads) {
        VDK_PRINT_DEBUG("clone: before... pmgid_info %p\n", p->mgid_info);
        alloc_module_group_ID(&p->mgid_info);
        enum_modules_for_process(current, &p->mgid_info);
    }

    return TRUE;
}

//asmlinkage cloneinfo_t
asmlinkage u64
vt_sys_clone2_after(unsigned long ret)
{
    cloneinfo_t r;
    PCLONE_INFO p;
    u32 i, pid;

    VDK_PRINT_DEBUG("clone2: after.... current pid 0x%x new pid 0x%x \n", current->pid, ret);

    pid = current->pid;
    p = &clone_tbl[0];
    for (i = 0; i < CLONE_TBL_ENTRIES; i++, p++) {
        if (p->entry_in_use && p->pid == pid) {
            if (p->mgid_info.mgid && (ret > 0)) {
                update_pid_for_module_group(ret, &p->mgid_info);
            }
            r = p->clone_info;
            p->entry_in_use = 0;
            break;
        }
    }

    if (i >= CLONE_TBL_ENTRIES) {
        VDK_PRINT_ERROR("clone2: after.... pid not found. Table full?\n");
        //
        // Caller will be branching to zero because of this
        // which will kill it... Not sure there is much else we an do...
        //
        return (0);
    }

    //    atomic_dec(&hook_in_progress);

    return (r.rp);
}

asmlinkage void
vt_sys_create_module(unsigned long ret)
{
    //    atomic_inc(&hook_in_progress);

    if (track_module_loads && !IS_ERR((void *) ret)) {
        struct module *mod = (struct module *) ret;
	int msize;
#ifdef KERNEL_26X
	msize = mod->init_size + mod->core_size;
#else
	msize = mod->size;
#endif
        VDK_PRINT_DEBUG("create_module: %s, %d, %lx, %lx \n",
			mod->name, current->pid, ret, msize);
        samp_load_image_notify_routine((char *) mod->name, ret, msize,
                       0, LOPTS_GLOBAL_MODULE,
                       (PMGID_INFO) 0, get_exec_mode(current));
    }

    //    atomic_dec(&hook_in_progress);

    return;
}

void *vt_save_execve_ret;

asmlinkage void
vt_sys_execve(void)
{
    //    atomic_inc(&hook_in_progress);

    VDK_PRINT_DEBUG("exec: pid=%d\n", current->pid);
    // samp_create_process_notify_routine(0, current->pid, 1);
    if (track_module_loads) {
        enum_modules_for_process(current, 0);
    }

    //    atomic_dec(&hook_in_progress);

    return;
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
    plabel_t *p;
    extern int vt_sys_mmap_stub(void);
    extern int vt_sys_mmap2_stub(void);
    extern int vt_sys_clone_stub(void);
    extern int vt_sys_clone2_stub(void);
    extern int vt_sys_create_module_stub(void);
    extern int vt_sys_execve_stub(void);

#ifdef KERNEL_26X
// Note:  currently unable to track processes started during collection
//        (see note below)
    VDK_PRINT_WARNING("process creates and image loads will not be tracked during sampling session\n");
    return;
#endif

    memset(clone_tbl, sizeof(clone_tbl), 0);

#if !defined(EXPORTED_SYS_CALL_TABLE)
    sys_call_table = find_sys_call_table_symbol(1);
#endif

    if (sys_call_table) {

    p = (plabel_t *) vt_sys_mmap_stub;
    original_sys_mmap =
        (void *) xchg(&sys_call_table[__NR_mmap - 1024], p->fptr);

    p = (plabel_t *) vt_sys_mmap2_stub;
    original_sys_mmap2 =
        (void *) xchg(&sys_call_table[__NR_mmap2 - 1024], p->fptr);
#ifdef OS_RHEL3
    VDK_PRINT_WARNING("disabling sys_clone hooking\n");
#else
    p = (plabel_t *) vt_sys_clone2_stub; // use sys_clone2 stub for sys_clone
    original_sys_clone =
        (void *) xchg(&sys_call_table[__NR_clone - 1024], p->fptr);
#endif
    p = (plabel_t *) vt_sys_clone2_stub;
    original_sys_clone2 =
        (void *) xchg(&sys_call_table[__NR_clone2 - 1024], p->fptr);
#ifdef KERNEL_26X
// Note: __NR_create_module is not implemented (see include/asm-ia64/unistd.h)
    VDK_PRINT_WARNING("process creates will not be tracked during sampling session\n");
#else
    p = (plabel_t *) vt_sys_create_module_stub;
    original_sys_create_module =
        (void *) xchg(&sys_call_table[__NR_create_module - 1024], p->fptr);
#endif
    p = (plabel_t *) vt_sys_execve_stub;
    original_sys_execve =
        (void *) xchg(&sys_call_table[__NR_execve - 1024], p->fptr);

    } else
      VDK_PRINT_WARNING("process creates and image loads will not be tracked during sampling session\n");

    return;
}

void
un_install_OS_hooks(void)
{
    void *org_fn;

#ifdef KERNEL_26X
    return;
#endif

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

#if !defined(EXPORTED_SYS_CALL_TABLE)
    sys_call_table = find_sys_call_table_symbol(0);
#endif

    if (sys_call_table) {

    if ((org_fn = xchg(&original_sys_mmap, 0))) {
        xchg(&sys_call_table[__NR_mmap - 1024], org_fn);
    }
    if ((org_fn = xchg(&original_sys_mmap2, 0))) {
        xchg(&sys_call_table[__NR_mmap2 - 1024], org_fn);
    }
#ifndef OS_RHEL3
    if ((org_fn = xchg(&original_sys_clone, 0))) {
        xchg(&sys_call_table[__NR_clone - 1024], org_fn);
    }
#endif
    if ((org_fn = xchg(&original_sys_clone2, 0))) {
        xchg(&sys_call_table[__NR_clone2 - 1024], org_fn);
    }
#ifndef KERNEL_26X
    if ((org_fn = xchg(&original_sys_create_module, 0))) {
        xchg(&sys_call_table[__NR_create_module - 1024], org_fn);
    }
#endif
    if ((org_fn = xchg(&original_sys_execve, 0))) {
        xchg(&sys_call_table[__NR_execve - 1024], org_fn);
    }

    }

    return;
}
