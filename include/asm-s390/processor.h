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

#ifdef __KERNEL__
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
        unsigned long loops_per_jiffy;
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

typedef struct {
        __u32 ar4;
} mm_segment_t;

/* if you change the thread_struct structure, you must
 * update the _TSS_* defines in entry.S
 */

struct thread_struct
 {
	s390_fp_regs fp_regs;
        __u32   ar2;                   /* kernel access register 2         */
        __u32   ar4;                   /* kernel access register 4         */
        __u32   ksp;                   /* kernel stack pointer             */
        __u32   user_seg;              /* HSTD                             */
        __u32   error_code;            /* error-code of last prog-excep.   */
        __u32   prot_addr;             /* address of protection-excep.     */
        __u32   trap_no;
        per_struct per_info;/* Must be aligned on an 4 byte boundary*/
	/* Used to give failing instruction back to user for ieee exceptions */
	addr_t  ieee_instruction_pointer; 
        /* pfault_wait is used to block the process on a pfault event */
	addr_t  pfault_wait;
};

typedef struct thread_struct thread_struct;

#define INIT_THREAD {{0,{{0},{0},{0},{0},{0},{0},{0},{0},{0},{0}, \
			    {0},{0},{0},{0},{0},{0}}},            \
                     0, 0,                                        \
                    sizeof(init_stack) + (__u32) &init_stack,     \
              (__pa((__u32) &swapper_pg_dir[0]) + _SEGMENT_TABLE),\
                     0,0,0,                                       \
                     (per_struct) {{{{0,}}},0,0,0,0,{{0,}}},      \
                     0, 0                                         \
}

/* need to define ... */
#define start_thread(regs, new_psw, new_stackp) do {            \
        regs->psw.mask  = PSW_USER_BITS;                        \
        regs->psw.addr  = new_psw | PSW_ADDR_AMODE31;           \
        regs->gprs[15]  = new_stackp ;                          \
} while (0)

/* Forward declaration, a strange C thing */
struct task_struct;
struct mm_struct;

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);
extern int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags);

/* Prepare to copy thread state - unlazy all lazy status */
#define prepare_to_copy(tsk)	do { } while (0)

/*
 * Return saved PC of a blocked thread.
 */
extern unsigned long thread_saved_pc(struct task_struct *t);

/*
 * Print register of task into buffer. Used in fs/proc/array.c.
 */
extern char *task_show_regs(struct task_struct *task, char *buffer);

unsigned long get_wchan(struct task_struct *p);
#define __KSTK_PTREGS(tsk) ((struct pt_regs *) \
        (((addr_t) tsk->thread_info + THREAD_SIZE - sizeof(struct pt_regs)) & -8L))
#define KSTK_EIP(tsk)	(__KSTK_PTREGS(tsk)->psw.addr)
#define KSTK_ESP(tsk)	(__KSTK_PTREGS(tsk)->gprs[15])

/*
 * Give up the time slice of the virtual PU.
 */
#define cpu_relax()	asm volatile ("diag 0,0,68" : : : "memory")

/*
 * Set PSW mask to specified value, while leaving the
 * PSW addr pointing to the next instruction.
 */

static inline void __load_psw_mask (unsigned long mask)
{
	unsigned long addr;

	psw_t psw;
	psw.mask = mask;

	asm volatile (
		"    basr %0,0\n"
		"0:  ahi  %0,1f-0b\n"
		"    st   %0,4(%1)\n"
		"    lpsw 0(%1)\n"
		"1:"
		: "=&d" (addr) : "a" (&psw) : "memory", "cc" );
}
 
/*
 * Function to stop a processor until an interruption occurred
 */
static inline void enabled_wait(void)
{
	unsigned long reg;
	psw_t wait_psw;

	wait_psw.mask = PSW_BASE_BITS | PSW_MASK_IO | PSW_MASK_EXT |
		PSW_MASK_MCHECK | PSW_MASK_WAIT;
	asm volatile (
		"    basr %0,0\n"
		"0:  la   %0,1f-0b(%0)\n"
		"    st   %0,4(%1)\n"
		"    oi   4(%1),0x80\n"
		"    lpsw 0(%1)\n"
		"1:"
		: "=&a" (reg) : "a" (&wait_psw) : "memory", "cc" );
}

/*
 * Function to drop a processor into disabled wait state
 */

static inline void disabled_wait(unsigned long code)
{
        char psw_buffer[2*sizeof(psw_t)];
        char ctl_buf[4];
        psw_t *dw_psw = (psw_t *)(((unsigned long) &psw_buffer+sizeof(psw_t)-1)
                                  & -sizeof(psw_t));

        dw_psw->mask = PSW_BASE_BITS | PSW_MASK_WAIT;
        dw_psw->addr = code;
        /* 
         * Store status and then load disabled wait psw,
         * the processor is dead afterwards
         */

        asm volatile ("    stctl 0,0,0(%1)\n"
                      "    ni    0(%1),0xef\n" /* switch off protection */
                      "    lctl  0,0,0(%1)\n"
                      "    stpt  0xd8\n"       /* store timer */
                      "    stckc 0xe0\n"       /* store clock comparator */
                      "    stpx  0x108\n"      /* store prefix register */
                      "    stam  0,15,0x120\n" /* store access registers */
                      "    std   0,0x160\n"    /* store f0 */
                      "    std   2,0x168\n"    /* store f2 */
                      "    std   4,0x170\n"    /* store f4 */
                      "    std   6,0x178\n"    /* store f6 */
                      "    stm   0,15,0x180\n" /* store general registers */
                      "    stctl 0,15,0x1c0\n" /* store control registers */
                      "    oi    0(%1),0x10\n" /* fake protection bit */
                      "    lpsw 0(%0)"
                      : : "a" (dw_psw), "a" (&ctl_buf) : "cc" );
}

#endif

#endif                                 /* __ASM_S390_PROCESSOR_H           */
