/*
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __PROC_MM_H
#define __PROC_MM_H

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/compiler.h>

/* The differences between this one and do_mmap are that:
 * - we must perform controls for userspace-supplied params (which are
 *   arch-specific currently). And also fget(fd) if needed and so on...
 * - we must accept the struct mm_struct on which to act as first param, and the
 *   offset in byte rather than page units as last param.
 */
static inline long __do_mmap(struct mm_struct *mm, unsigned long addr,
		     unsigned long len, unsigned long prot,
		     unsigned long flags, unsigned long fd,
		     unsigned long off);

/* This header can be used only on archs defining CONFIG_PROC_MM in their
 * configs, so asm/proc_mm.h can still exist only for the needed archs.
 * Including it only in the x86-64 case does not make sense.*/
#include <asm/proc_mm.h>

/*XXX: this is defined on x86_64, but not on every 64-bit arch (not on sh64).*/
#ifdef CONFIG_64BIT

#define write_proc_mm write_proc_mm_emul
#define write_proc_mm64 write_proc_mm_native

/* It would make more sense to do this mapping the reverse direction, to map the
 * called name to the defined one and not the reverse. Like the 2nd example
 */
/*#define proc_mm_get_mm proc_mm_get_mm_emul
#define proc_mm_get_mm64 proc_mm_get_mm_native*/

#define proc_mm_get_mm_emul proc_mm_get_mm
#define proc_mm_get_mm_native proc_mm_get_mm64

#else

#define write_proc_mm write_proc_mm_native
#undef write_proc_mm64

/*#define proc_mm_get_mm proc_mm_get_mm_native
#undef proc_mm_get_mm64*/

#define proc_mm_get_mm_native proc_mm_get_mm
#undef proc_mm_get_mm_emul

#endif

#define MM_MMAP 54
#define MM_MUNMAP 55
#define MM_MPROTECT 56
#define MM_COPY_SEGMENTS 57

struct mm_mmap {
	unsigned long addr;
	unsigned long len;
	unsigned long prot;
	unsigned long flags;
	unsigned long fd;
	unsigned long offset;
};

struct mm_munmap {
	unsigned long addr;
	unsigned long len;
};

struct mm_mprotect {
	unsigned long addr;
	unsigned long len;
        unsigned int prot;
};

struct proc_mm_op {
	int op;
	union {
		struct mm_mmap mmap;
		struct mm_munmap munmap;
	        struct mm_mprotect mprotect;
		int copy_segments;
	} u;
};

extern struct mm_struct *proc_mm_get_mm(int fd);
extern struct mm_struct *proc_mm_get_mm64(int fd);

/* Cope with older kernels */
#ifndef __acquires
#define __acquires(x)
#endif

#ifdef CONFIG_PROC_MM_DUMPABLE
/*
 * Since we take task_lock of child and it's needed also by the caller, we
 * return with it locked.
 */
extern void lock_fix_dumpable_setting(struct task_struct * child,
		struct mm_struct* new) __acquires(child->alloc_lock);
#else
static inline void lock_fix_dumpable_setting(struct task_struct * child,
		struct mm_struct* new) __acquires(child->alloc_lock)
{
	task_lock(child);
}
#endif

#endif
