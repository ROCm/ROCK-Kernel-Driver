/*
 *  include/asm-s390/processor.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "include/asm-i386/processor.h"
 *    Copyright (C) 1994, Linus Torvalds
 */

#ifndef __ASM_S390_PROCESSOR_H
#define __ASM_S390_PROCESSOR_H

#include <asm/page.h>
#include <asm/ptrace.h>

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ void *pc; __asm__("basr %0,0":"=a"(pc)); pc; })

/*
 *  CPU type and hardware bug flags. Kept separately for each CPU.
 *  Members of this structure are referenced in head.S, so think twice
 *  before touching them. [mj]
 */

typedef struct
{
        unsigned int version :  8;
        unsigned int ident   : 24;
        unsigned int machine : 16;
        unsigned int unused  : 16;
} __attribute__ ((packed)) cpuid_t;

struct cpuinfo_S390
{
        cpuid_t  cpu_id;
        __u16    cpu_addr;
        __u16    cpu_nr;
        unsigned long loops_per_sec;
        unsigned long *pgd_quick;
        unsigned long *pte_quick;
        unsigned long pgtable_cache_sz;
};

extern void print_cpu_info(struct cpuinfo_S390 *);

/* Lazy FPU handling on uni-processor */
extern struct task_struct *last_task_used_math;

/*
 * User space process size: 2GB (default).
 */
#define TASK_SIZE       (0x80000000)
/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE      (TASK_SIZE / 2)

#define THREAD_SIZE (2*PAGE_SIZE)

typedef struct {
        unsigned long seg;
        unsigned long acc4;
} mm_segment_t;

/* if you change the thread_struct structure, you must
 * update the _TSS_* defines in entry.S
 */

struct thread_struct
 {

        struct pt_regs *regs;         /* the user registers can be found on*/
	s390_fp_regs fp_regs;
        __u32   ar2;                   /* kernel access register 2         */
        __u32   ar4;                   /* kernel access register 4         */
        __u32   ksp;                   /* kernel stack pointer             */
        __u32   user_seg;              /* HSTD                             */
        __u32   error_code;            /* error-code of last prog-excep.   */
        __u32   prot_addr;             /* address of protection-excep.     */
        __u32   trap_no;
        /* perform syscall argument validation (get/set_fs) */
        mm_segment_t fs;
        per_struct per_info;/* Must be aligned on an 4 byte boundary*/
};

typedef struct thread_struct thread_struct;

#define INIT_MMAP \
{ &init_mm, 0, 0, NULL, PAGE_SHARED, \
VM_READ | VM_WRITE | VM_EXEC, 1, NULL, &init_mm.mmap }

#define INIT_THREAD { (struct pt_regs *) 0,                       \
                    { 0,{{0},{0},{0},{0},{0},{0},{0},{0},{0},{0}, \
			    {0},{0},{0},{0},{0},{0}}},            \
                     0, 0,                                        \
                    sizeof(init_stack) + (__u32) &init_stack,     \
              (__pa((__u32) &swapper_pg_dir[0]) + _SEGMENT_TABLE),\
                     0,0,0,                                       \
                     (mm_segment_t) { 0,1},                       \
                     (per_struct) {{{{0,}}},0,0,0,0,{{0,}}}       \
}

/* need to define ... */
#define start_thread(regs, new_psw, new_stackp) do {            \
        unsigned long *u_stack = new_stackp;                    \
        regs->psw.mask  = _USER_PSW_MASK;                       \
        regs->psw.addr  = new_psw | 0x80000000 ;                \
        get_user(regs->gprs[2],u_stack);                        \
        get_user(regs->gprs[3],u_stack+1);                      \
        get_user(regs->gprs[4],u_stack+2);                      \
        regs->gprs[15]  = new_stackp ;                          \
} while (0)

/* Forward declaration, a strange C thing */
struct mm_struct;

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);
extern int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags);

/* Copy and release all segment info associated with a VM */
#define copy_segments(nr, mm)           do { } while (0)
#define release_segments(mm)            do { } while (0)

/*
 * Return saved PC of a blocked thread. used in kernel/sched
 */
extern inline unsigned long thread_saved_pc(struct thread_struct *t)
{
        return (t->regs) ? ((unsigned long)t->regs->psw.addr) : 0;
}

unsigned long get_wchan(struct task_struct *p);
#define KSTK_EIP(tsk)   ((tsk)->thread.regs->psw.addr)
#define KSTK_ESP(tsk)   ((tsk)->thread.ksp)

/* Allocation and freeing of basic task resources. */
/*
 * NOTE! The task struct and the stack go together
 */
#define alloc_task_struct() \
        ((struct task_struct *) __get_free_pages(GFP_KERNEL,1))
#define free_task_struct(p)     free_pages((unsigned long)(p),1)
#define get_task_struct(tsk)      atomic_inc(&virt_to_page(tsk)->count)

#define init_task       (init_task_union.task)
#define init_stack      (init_task_union.stack)

/*
 * Set of msr bits that gdb can change on behalf of a process.
 */
/* Only let our hackers near the condition codes */
#define PSW_MASK_DEBUGCHANGE    0x00003000UL
/* Don't let em near the addressing mode either */    
#define PSW_ADDR_DEBUGCHANGE    0x7FFFFFFFUL
#define PSW_ADDR_MASK           0x7FFFFFFFUL
/* Program event recording mask */    
#define PSW_PER_MASK            0x40000000UL
#define USER_STD_MASK           0x00000080UL
#define PSW_PROBLEM_STATE       0x00010000UL

/*
 * Function to drop a processor into disabled wait state
 */

static inline void disabled_wait(unsigned long code)
{
        char psw_buffer[2*sizeof(psw_t)];
        psw_t *dw_psw = (psw_t *)(((unsigned long) &psw_buffer+sizeof(psw_t)-1)
                                  & -sizeof(psw_t));

        dw_psw->mask = 0x000a0000;
        dw_psw->addr = code;
        /* load disabled wait psw, the processor is dead afterwards */
        asm volatile ("lpsw 0(%0)" : : "a" (dw_psw));
}

#endif                                 /* __ASM_S390_PROCESSOR_H           */
