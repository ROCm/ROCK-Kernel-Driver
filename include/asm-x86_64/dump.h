/*
 * Kernel header file for Linux crash dumps.
 *
 * Created by: Matt Robinson (yakker@sgi.com)
 *
 * Copyright 1999 Silicon Graphics, Inc. All rights reserved.
 * x86_64 lkcd port Sachin Sant ( sachinp@in.ibm.com)
 * This code is released under version 2 of the GNU GPL.
 */

/* This header file holds the architecture specific crash dump header */
#ifndef _ASM_DUMP_H
#define _ASM_DUMP_H

/* necessary header files */
#include <asm/ptrace.h>                          /* for pt_regs             */
#include <linux/threads.h>

/* definitions */
#define DUMP_ASM_MAGIC_NUMBER     0xdeaddeadULL  /* magic number            */
#define DUMP_ASM_VERSION_NUMBER   0x2            /* version number          */

#define platform_timestamp(x) rdtscll(x)

/*
 * Structure: dump_header_asm_t
 *  Function: This is the header for architecture-specific stuff.  It
 *            follows right after the dump header.
 */
struct __dump_header_asm {

        /* the dump magic number -- unique to verify dump is valid */
        uint64_t             dha_magic_number;

        /* the version number of this dump */
        uint32_t             dha_version;

        /* the size of this header (in case we can't read it) */
        uint32_t             dha_header_size;

	/* the dump registers */
	struct pt_regs       dha_regs;

	/* smp specific */
	uint32_t	     dha_smp_num_cpus;
	int		     dha_dumping_cpu;
	struct pt_regs	     dha_smp_regs[NR_CPUS];
	uint64_t	     dha_smp_current_task[NR_CPUS];
	uint64_t	     dha_stack[NR_CPUS];
	uint64_t	     dha_stack_ptr[NR_CPUS];
} __attribute__((packed));

#ifdef __KERNEL__
static inline void get_current_regs(struct pt_regs *regs)
{
	unsigned seg;
	__asm__ __volatile__("movq %%r15,%0" : "=m"(regs->r15));
	__asm__ __volatile__("movq %%r14,%0" : "=m"(regs->r14));
	__asm__ __volatile__("movq %%r13,%0" : "=m"(regs->r13));
	__asm__ __volatile__("movq %%r12,%0" : "=m"(regs->r12));
	__asm__ __volatile__("movq %%r11,%0" : "=m"(regs->r11));
	__asm__ __volatile__("movq %%r10,%0" : "=m"(regs->r10));
	__asm__ __volatile__("movq %%r9,%0" : "=m"(regs->r9));
	__asm__ __volatile__("movq %%r8,%0" : "=m"(regs->r8));
	__asm__ __volatile__("movq %%rbx,%0" : "=m"(regs->rbx));
	__asm__ __volatile__("movq %%rcx,%0" : "=m"(regs->rcx));
	__asm__ __volatile__("movq %%rdx,%0" : "=m"(regs->rdx));
	__asm__ __volatile__("movq %%rsi,%0" : "=m"(regs->rsi));
	__asm__ __volatile__("movq %%rdi,%0" : "=m"(regs->rdi));
	__asm__ __volatile__("movq %%rbp,%0" : "=m"(regs->rbp));
	__asm__ __volatile__("movq %%rax,%0" : "=m"(regs->rax));
	__asm__ __volatile__("movq %%rsp,%0" : "=m"(regs->rsp));
	__asm__ __volatile__("movl %%ss, %0" :"=r"(seg));
	regs->ss = (unsigned long)seg;
	__asm__ __volatile__("movl %%cs, %0" :"=r"(seg));
	regs->cs = (unsigned long)seg;
	__asm__ __volatile__("pushfq; popq %0" :"=m"(regs->eflags));
	regs->rip = (unsigned long)current_text_addr();

}

extern volatile int dump_in_progress;
extern struct __dump_header_asm dump_header_asm;

#ifdef CONFIG_SMP


extern void dump_send_ipi(void);
#else
#define dump_send_ipi() do { } while(0)
#endif
#endif /* __KERNEL__ */

#endif /* _ASM_DUMP_H */
