/*
 * linux/arch/x86_64/kernel/ldt.c
 *
 * Copyright (C) 1992 Krishna Balasubramanian and Linus Torvalds
 * Copyright (C) 1999 Ingo Molnar <mingo@redhat.com>
 * Copyright (C) 2002 Andi Kleen
 * 
 * This handles calls from both 32bit and 64bit mode.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/ldt.h>
#include <asm/desc.h>

void load_gs_index(unsigned gs)
{
	int access;
	struct task_struct *me = current;
	if (me->mm) 
		read_lock(&me->mm->context.ldtlock); 
	asm volatile("pushf\n\t" 
		     "cli\n\t"
		     "swapgs\n\t"
		     "lar %1,%0\n\t"
		     "jnz 1f\n\t"
		     "movl %1,%%eax\n\t"
		     "movl %%eax,%%gs\n\t"
		     "jmp 2f\n\t"
		     "1: movl %2,%%gs\n\t"
		     "2: swapgs\n\t"
		     "popf" : "=g" (access) : "g" (gs), "r" (0) : "rax"); 
	if (me->mm) 
		read_unlock(&me->mm->context.ldtlock); 
}

#ifdef CONFIG_SMP /* avoids "defined but not used" warnig */
static void flush_ldt(void *mm)
{
	if (current->mm)
		load_LDT(&current->mm->context);
}
#endif

static int alloc_ldt(mm_context_t *pc, int mincount, int reload)
{
	void *oldldt;
	void *newldt;
	int oldsize;

	if (mincount <= pc->size)
		return 0;
	oldsize = pc->size;
	mincount = (mincount+511)&(~511);
	if (mincount*LDT_ENTRY_SIZE > PAGE_SIZE)
		newldt = vmalloc(mincount*LDT_ENTRY_SIZE);
	else
		newldt = kmalloc(mincount*LDT_ENTRY_SIZE, GFP_KERNEL);

	if (!newldt)
		return -ENOMEM;

	if (oldsize)
		memcpy(newldt, pc->ldt, oldsize*LDT_ENTRY_SIZE);
	oldldt = pc->ldt;
	memset(newldt+oldsize*LDT_ENTRY_SIZE, 0, (mincount-oldsize)*LDT_ENTRY_SIZE);
	wmb();
	pc->ldt = newldt;
	pc->size = mincount;
	if (reload) {
		load_LDT(pc);
#ifdef CONFIG_SMP
		if (current->mm->cpu_vm_mask != (1<<smp_processor_id()))
			smp_call_function(flush_ldt, 0, 1, 1);
#endif
	}
	wmb();
	if (oldsize) {
		if (oldsize*LDT_ENTRY_SIZE > PAGE_SIZE)
			vfree(oldldt);
		else
			kfree(oldldt);
	}
	return 0;
}

static inline int copy_ldt(mm_context_t *new, mm_context_t *old)
{
	int err = alloc_ldt(new, old->size, 0);
	if (err < 0) {
		printk(KERN_WARNING "ldt allocation failed\n");
		new->size = 0;
		return err;
	}
	memcpy(new->ldt, old->ldt, old->size*LDT_ENTRY_SIZE);
	return 0;
}

/*
 * we do not have to muck with descriptors here, that is
 * done in switch_mm() as needed.
 */
int init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	struct mm_struct * old_mm;
	int retval = 0;

	init_MUTEX(&mm->context.sem);
	mm->context.size = 0;
	old_mm = current->mm;
	if (old_mm && old_mm->context.size > 0) {
		down(&old_mm->context.sem);
		retval = copy_ldt(&mm->context, &old_mm->context);
		up(&old_mm->context.sem);
	}
	rwlock_init(&mm->context.ldtlock);
	return retval;
}

/*
 * No need to lock the MM as we are the last user
 */
void release_segments(struct mm_struct *mm)
{
	if (mm->context.size) {
		if (mm == current->active_mm)
			clear_LDT();
		if (mm->context.size*LDT_ENTRY_SIZE > PAGE_SIZE)
			vfree(mm->context.ldt);
		else
			kfree(mm->context.ldt);
		mm->context.size = 0;
	}
}

static int read_ldt(void * ptr, unsigned long bytecount)
{
	int err;
	unsigned long size;
	struct mm_struct * mm = current->mm;

	if (!mm->context.size)
		return 0;
	if (bytecount > LDT_ENTRY_SIZE*LDT_ENTRIES)
		bytecount = LDT_ENTRY_SIZE*LDT_ENTRIES;
	down(&mm->context.sem);
	size = mm->context.size*LDT_ENTRY_SIZE;
	if (size > bytecount)
		size = bytecount;

	err = 0;
	if (copy_to_user(ptr, mm->context.ldt, size))
		err = -EFAULT;
	up(&mm->context.sem);
	if (err < 0)
	return err;
	if (size != bytecount) {
		/* zero-fill the rest */
		clear_user(ptr+size, bytecount-size);
	}
	return bytecount;
}

static int read_default_ldt(void * ptr, unsigned long bytecount)
{
	/* Arbitary number */ 
	/* x86-64 default LDT is all zeros */
	if (bytecount > 128) 
		bytecount = 128; 	
	if (clear_user(ptr, bytecount))
		return -EFAULT;
	return bytecount; 
}

static int write_ldt(void * ptr, unsigned long bytecount, int oldmode)
{
	struct task_struct *me = current;
	struct mm_struct * mm = me->mm;
	__u32 entry_1, entry_2, *lp;
	int error;
	struct modify_ldt_ldt_s ldt_info;

	error = -EINVAL;

	if (bytecount != sizeof(ldt_info))
		goto out;
	error = -EFAULT; 	
	if (copy_from_user(&ldt_info, ptr, bytecount))
		goto out;

	error = -EINVAL;
	if (ldt_info.entry_number >= LDT_ENTRIES)
		goto out;
	if (ldt_info.contents == 3) {
		if (oldmode)
			goto out;
		if (ldt_info.seg_not_present == 0)
			goto out;
	}

	me->thread.fsindex = 0; 
	me->thread.gsindex = 0; 
	me->thread.gs = 0; 
	me->thread.fs = 0; 

	down(&mm->context.sem);
	if (ldt_info.entry_number >= mm->context.size) {
		error = alloc_ldt(&current->mm->context, ldt_info.entry_number+1, 1);
		if (error < 0)
			goto out_unlock;
	}

	lp = (__u32 *) ((ldt_info.entry_number << 3) + (char *) mm->context.ldt);

   	/* Allow LDTs to be cleared by the user. */
   	if (ldt_info.base_addr == 0 && ldt_info.limit == 0) {
		if (oldmode ||
		    (ldt_info.contents == 0		&&
		     ldt_info.read_exec_only == 1	&&
		     ldt_info.seg_32bit == 0		&&
		     ldt_info.limit_in_pages == 0	&&
		     ldt_info.seg_not_present == 1	&&
		     ldt_info.useable == 0 && 
		     ldt_info.lm == 0)) {
			entry_1 = 0;
			entry_2 = 0;
			goto install;
		}
	}

	entry_1 = ((ldt_info.base_addr & 0x0000ffff) << 16) |
		  (ldt_info.limit & 0x0ffff);
	entry_2 = (ldt_info.base_addr & 0xff000000) |
		  ((ldt_info.base_addr & 0x00ff0000) >> 16) |
		  (ldt_info.limit & 0xf0000) |
		  ((ldt_info.read_exec_only ^ 1) << 9) |
		  (ldt_info.contents << 10) |
		  ((ldt_info.seg_not_present ^ 1) << 15) |
		  (ldt_info.seg_32bit << 22) |
		  (ldt_info.limit_in_pages << 23) |
		  (ldt_info.lm << 21) |
		  0x7000;
	if (!oldmode)
		entry_2 |= (ldt_info.useable << 20);

	/* Install the new entry ...  */
install:
	write_lock(&mm->context.ldtlock); 
	*lp	= entry_1;
	*(lp+1)	= entry_2;
	write_unlock(&mm->context.ldtlock);
	error = 0;

out_unlock:
	up(&mm->context.sem);
out:
	return error;
}

asmlinkage int sys_modify_ldt(int func, void *ptr, unsigned long bytecount)
{
	int ret = -ENOSYS;

	switch (func) {
	case 0:
		ret = read_ldt(ptr, bytecount);
		break;
	case 1:
		ret = write_ldt(ptr, bytecount, 1);
		break;
	case 2:
		ret = read_default_ldt(ptr, bytecount);
		break;
	case 0x11:
		ret = write_ldt(ptr, bytecount, 0);
		break;
	}
	return ret;
}
