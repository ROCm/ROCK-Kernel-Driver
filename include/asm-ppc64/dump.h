/*
 * Kernel header file for Linux crash dumps.
 *
 * Created by: Todd Inglett <tinglett@vnet.ibm.com>
 *
 * Copyright 2002 - 2004 International Business Machines
 *
 * This code is released under version 2 of the GNU GPL.
 */

/* This header file holds the architecture specific crash dump header */
#ifndef _ASM_DUMP_H
#define _ASM_DUMP_H

/* necessary header files */
#include <asm/ptrace.h>                          /* for pt_regs             */
#include <asm/kmap_types.h>
#include <linux/threads.h>

/* definitions */
#define DUMP_ASM_MAGIC_NUMBER     0xdeaddeadULL  /* magic number            */
#define DUMP_ASM_VERSION_NUMBER   0x5            /* version number          */

/* max number of cpus */
#define DUMP_MAX_NUM_CPUS 32

/*
 * Structure: __dump_header_asm
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
	struct pt_regs	     dha_smp_regs[DUMP_MAX_NUM_CPUS];
	uint64_t	     dha_smp_current_task[DUMP_MAX_NUM_CPUS];
	uint64_t	     dha_stack[DUMP_MAX_NUM_CPUS];
	uint64_t     	     dha_stack_ptr[DUMP_MAX_NUM_CPUS];
} __attribute__((packed));

#ifdef __KERNEL__
static inline void get_current_regs(struct pt_regs *regs)
{
	unsigned long tmp1, tmp2;

	__asm__ __volatile__ (
		"std	0,0(%2)\n"
		"std	1,8(%2)\n"
		"std	2,16(%2)\n"
		"std	3,24(%2)\n"
		"std	4,32(%2)\n"
		"std	5,40(%2)\n"
		"std	6,48(%2)\n"
		"std	7,56(%2)\n"
		"std	8,64(%2)\n"
		"std	9,72(%2)\n"
		"std	10,80(%2)\n"
		"std	11,88(%2)\n"
		"std	12,96(%2)\n"
		"std	13,104(%2)\n"
		"std	14,112(%2)\n"
		"std	15,120(%2)\n"
		"std	16,128(%2)\n"
		"std	17,136(%2)\n"
		"std	18,144(%2)\n"
		"std	19,152(%2)\n"
		"std	20,160(%2)\n"
		"std	21,168(%2)\n"
		"std	22,176(%2)\n"
		"std	23,184(%2)\n"
		"std	24,192(%2)\n"
		"std	25,200(%2)\n"
		"std	26,208(%2)\n"
		"std	27,216(%2)\n"
		"std	28,224(%2)\n"
		"std	29,232(%2)\n"
		"std	30,240(%2)\n"
		"std	31,248(%2)\n"
		"mfmsr	%0\n"
		"std	%0, 264(%2)\n"
		"mfctr	%0\n"
		"std	%0, 280(%2)\n"
		"mflr	%0\n"
		"std	%0, 288(%2)\n"
		"bl	1f\n"
	"1:	 mflr	%1\n"
		"std	%1, 256(%2)\n"
		"mtlr	%0\n"
		"mfxer	%0\n"
		"std	%0, 296(%2)\n"
		: "=&r" (tmp1), "=&r" (tmp2)
		: "b" (regs));
}

extern struct __dump_header_asm dump_header_asm;

#ifdef CONFIG_SMP
extern void dump_send_ipi(int (*dump_ipi_callback)(struct pt_regs *));
#else
#define dump_send_ipi() do { } while(0)
#endif
#endif /* __KERNEL__ */

#endif /* _ASM_DUMP_H */
