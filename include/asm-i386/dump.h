/*
 * Kernel header file for Linux crash dumps.
 *
 * Created by: Matt Robinson (yakker@sgi.com)
 *
 * Copyright 1999 Silicon Graphics, Inc. All rights reserved.
 *
 * This code is released under version 2 of the GNU GPL.
 */

/* This header file holds the architecture specific crash dump header */
#ifndef _ASM_DUMP_H
#define _ASM_DUMP_H

/* necessary header files */
#include <asm/ptrace.h>
#include <asm/page.h>
#include <linux/threads.h>
#include <linux/mm.h>

/* definitions */
#define DUMP_ASM_MAGIC_NUMBER	0xdeaddeadULL	/* magic number            */
#define DUMP_ASM_VERSION_NUMBER	0x3	/* version number          */

#define platform_timestamp(x) rdtscll(x)

/*
 * Structure: __dump_header_asm
 *  Function: This is the header for architecture-specific stuff.  It
 *            follows right after the dump header.
 */
struct __dump_header_asm {
	/* the dump magic number -- unique to verify dump is valid */
	u64		dha_magic_number;

	/* the version number of this dump */
	u32		dha_version;

	/* the size of this header (in case we can't read it) */
	u32		dha_header_size;

	/* the esp for i386 systems */
	u32		dha_esp;

	/* the eip for i386 systems */
	u32		dha_eip;

	/* the dump registers */
	struct pt_regs	dha_regs;

	/* smp specific */
	u32		dha_smp_num_cpus;
	u32		dha_dumping_cpu;
	struct pt_regs	dha_smp_regs[NR_CPUS];
	u32		dha_smp_current_task[NR_CPUS];
	u32		dha_stack[NR_CPUS];
	u32		dha_stack_ptr[NR_CPUS];
} __attribute__((packed));

#ifdef __KERNEL__

extern struct __dump_header_asm dump_header_asm;

#ifdef CONFIG_SMP
extern cpumask_t irq_affinity[];
extern int (*dump_ipi_function_ptr)(struct pt_regs *);
extern void dump_send_ipi(void);
#else
#define dump_send_ipi() do { } while(0)
#endif

static inline void get_current_regs(struct pt_regs *regs)
{
	__asm__ __volatile__("movl %%ebx,%0" : "=m"(regs->ebx));
	__asm__ __volatile__("movl %%ecx,%0" : "=m"(regs->ecx));
	__asm__ __volatile__("movl %%edx,%0" : "=m"(regs->edx));
	__asm__ __volatile__("movl %%esi,%0" : "=m"(regs->esi));
	__asm__ __volatile__("movl %%edi,%0" : "=m"(regs->edi));
	__asm__ __volatile__("movl %%ebp,%0" : "=m"(regs->ebp));
	__asm__ __volatile__("movl %%eax,%0" : "=m"(regs->eax));
	__asm__ __volatile__("movl %%esp,%0" : "=m"(regs->esp));
	__asm__ __volatile__("movw %%ss, %%ax;" :"=a"(regs->xss));
	__asm__ __volatile__("movw %%cs, %%ax;" :"=a"(regs->xcs));
	__asm__ __volatile__("movw %%ds, %%ax;" :"=a"(regs->xds));
	__asm__ __volatile__("movw %%es, %%ax;" :"=a"(regs->xes));
	__asm__ __volatile__("pushfl; popl %0" :"=m"(regs->eflags));
	regs->eip = (unsigned long)current_text_addr();
}

#endif /* __KERNEL__ */

#endif /* _ASM_DUMP_H */
