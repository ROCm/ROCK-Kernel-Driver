/*
 * include/asm-cris/processor.h
 *
 * Copyright (C) 2000 Axis Communications AB
 *
 * Authors:         Bjorn Wesen        Initial version
 *
 */

#ifndef __ASM_CRIS_PROCESSOR_H
#define __ASM_CRIS_PROCESSOR_H

#include <linux/config.h>
#include <asm/system.h>
#include <asm/ptrace.h>

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ __label__ _l; _l: &&_l;})

/* CRIS has no problems with write protection */

#define wp_works_ok 1

/*
 * User space process size. This is hardcoded into a few places,
 * so don't change it unless you know what you are doing.
 */

#ifdef CONFIG_CRIS_LOW_MAP
#define TASK_SIZE       (0x50000000UL)   /* 1.25 GB */
#else
#define TASK_SIZE       (0xB0000000UL)   /* 2.75 GB */
#endif

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE      (TASK_SIZE / 3)

/* CRIS thread_struct. this really has nothing to do with the processor itself, since
 * CRIS does not do any hardware task-switching, but it's here for legacy reasons.
 * The thread_struct here is used when task-switching using _resume defined in entry.S.
 * The offsets here are hardcoded into _resume - if you change this struct, you need to
 * change them as well!!!
*/

struct thread_struct {
	unsigned long ksp;     /* kernel stack pointer */
	unsigned long usp;     /* user stack pointer */
	unsigned long esp0;    /* points to start of saved stack frame, set in entry.S */  
	unsigned long dccr;    /* saved flag register */
};

/* saved stack-frame upon syscall entry, points to registers */

#define current_regs() (current->thread.esp0)

/* this lives in process.c */
asmlinkage void set_esp0(unsigned long ssp);

/* INIT_MMAP is the kernels map of memory, between KSEG_C and KSEG_D */

#ifdef CONFIG_CRIS_LOW_MAP
#define INIT_MMAP { &init_mm, KSEG_6, KSEG_7, NULL, PAGE_SHARED, \
			     VM_READ | VM_WRITE | VM_EXEC, 1, NULL, NULL }
#else
#define INIT_MMAP { &init_mm, KSEG_C, KSEG_D, NULL, PAGE_SHARED, \
			     VM_READ | VM_WRITE | VM_EXEC, 1, NULL, NULL }
#endif

#define INIT_THREAD  { \
   0, 0, 0, 0x20 }  /* ccr = int enable, nothing else */

/* TODO: REMOVE */
#define alloc_kernel_stack()    __get_free_page(GFP_KERNEL)
#define free_kernel_stack(page) free_page((page))

extern int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags);

/* give the thread a program location
 * set user-mode (The 'U' flag (User mode flag) is CCR/DCCR bit 8) 
 * switch user-stackpointer
 */

#define start_thread(regs, ip, usp) do { \
	set_fs(USER_DS);      \
	regs->irp = ip;       \
	regs->dccr |= 1 << 8; \
	wrusp(usp);           \
} while(0)

unsigned long get_wchan(struct task_struct *p);

#define KSTK_EIP(tsk)   \
    ({                  \
        unsigned long eip = 0;   \
        if ((tsk)->thread.esp0 > PAGE_SIZE && \
            VALID_PAGE(virt_to_page((tsk)->thread.esp0))) \
              eip = ((struct pt_regs *) (tsk)->thread.esp0)->irp; \
        eip; })

#define KSTK_ESP(tsk)   ((tsk) == current ? rdusp() : (tsk)->thread.usp)

#define copy_segments(tsk, mm)          do { } while (0)
#define release_segments(mm)            do { } while (0)
#define forget_segments()               do { } while (0)
 
/*
 * Free current thread data structures etc..
 */

static inline void exit_thread(void)
{
}

/* Free all resources held by a thread. */
static inline void release_thread(struct task_struct *dead_task)
{
}

/*
 * Return saved PC of a blocked thread.
 */
extern inline unsigned long thread_saved_pc(struct thread_struct *t)
{
	return (unsigned long)((struct pt_regs *)t->esp0)->irp;
}

/* THREAD_SIZE is the size of the task_struct/kernel_stack combo.
 * normally, the stack is found by doing something like p + THREAD_SIZE
 * in CRIS, a page is 8192 bytes, which seems like a sane size
 */

#define THREAD_SIZE     PAGE_SIZE
#define KERNEL_STACK_SIZE PAGE_SIZE

#define alloc_task_struct()  ((struct task_struct *) __get_free_pages(GFP_KERNEL,1))
#define free_task_struct(p)  free_pages((unsigned long) (p), 1)
#define get_task_struct(tsk) atomic_inc(&virt_to_page(tsk)->count)

#define init_task       (init_task_union.task)
#define init_stack      (init_task_union.stack)

#endif /* __ASM_CRIS_PROCESSOR_H */
