/*
 * Kernel header file for Linux crash dumps.
 *
 * Created by: Matt Robinson (yakker@sgi.com)
 *
 * Copyright 1999 - 2002 Silicon Graphics, Inc. All rights reserved.
 *
 * This code is released under version 2 of the GNU GPL.
 */

/* This header file holds the architecture specific crash dump header */
#ifndef _ASM_DUMP_H
#define _ASM_DUMP_H

/* definitions */
#define DUMP_ASM_MAGIC_NUMBER     0xdeaddeadULL  /* magic number            */
#define DUMP_ASM_VERSION_NUMBER   0x4            /* version number          */

/* max number of cpus */
#define DUMP_MAX_NUM_CPUS 32

#ifdef __KERNEL__
#include <linux/efi.h>
#include <asm/pal.h>
#include <asm/ptrace.h>

#ifdef CONFIG_SMP
extern unsigned long irq_affinity[];
extern int (*dump_ipi_function_ptr)(struct pt_regs *);
extern void dump_send_ipi(void);
#else /* !CONFIG_SMP */
#define dump_send_ipi() do { } while(0)
#endif

#else  /* !__KERNEL__ */
/* necessary header files */
#include <asm/ptrace.h>                          /* for pt_regs             */
#include <linux/threads.h>
#endif /* __KERNEL__ */

/* 
 * mkswap.c calls getpagesize() to get the system page size,
 * which is not  necessarily the same as the hardware page size.
 *
 * For ia64 the kernel PAGE_SIZE can be configured from 4KB ... 16KB.
 *
 * The physical memory is layed out out in the hardware/minimal pages.
 * This is the size we need to use for dumping physical pages.
 *
 * Note ths hardware/minimal page size being use in;
 *      arch/ia64/kernel/efi.c`efi_memmap_walk():
 *	    curr.end   = curr.start + (md->num_pages << 12);
 *
 * Since the system page size could change between the kernel we boot
 * on the the kernel that cause the core dume we may want to have something
 * more constant like the maximum system page size (See include/asm-ia64/page.h).
 */
#define STACK_START_POSITION(tsk)		(tsk)
#define DUMP_MIN_PAGE_SHIFT                 	12
#define DUMP_MIN_PAGE_SIZE                  	(1UL << DUMP_MIN_PAGE_SHIFT)
#define DUMP_MIN_PAGE_MASK                  	(~(DUMP_MIN_PAGE_SIZE - 1))
#define DUMP_MIN_PAGE_ALIGN(addr)           	(((addr) + DUMP_MIN_PAGE_SIZE - 1) & DUMP_MIN_PAGE_MASK)

#define DUMP_MAX_PAGE_SHIFT                 	16
#define DUMP_MAX_PAGE_SIZE                  	(1UL << DUMP_MAX_PAGE_SHIFT)
#define DUMP_MAX_PAGE_MASK                  	(~(DUMP_MAX_PAGE_SIZE - 1))
#define DUMP_MAX_PAGE_ALIGN(addr)           	(((addr) + DUMP_MAX_PAGE_SIZE - 1) & DUMP_MAX_PAGE_MASK)


#undef	DUMP_PAGE_SHIFT				/* Redefining Default for ia64  */
#undef	DUMP_PAGE_SIZE				/* "	"	"	"	*/
#undef	DUMP_PAGE_MASK				/* "    "       "       "       */
#undef	DUMP_PAGE_ALIGN				/* "    "       "       "       */
#undef	DUMP_HEADER_OFFSET			/* "    "       "       "       */

#define DUMP_HEADER_OFFSET                    PAGE_SIZE

#define DUMP_EF_PAGE_SHIFT			DUMP_MIN_PAGE_SHIFT

/* changed here coz its already defined in linux.h  confirm this */
#define DUMP_PAGE_SHIFT				DUMP_MIN_PAGE_SHIFT
#define DUMP_PAGE_SIZE				DUMP_MIN_PAGE_SIZE
#define DUMP_PAGE_MASK				DUMP_MIN_PAGE_MASK
#define DUMP_PAGE_ALIGN(addr)			DUMP_MIN_PAGE_ALIGN(addr)

extern int _end,_start;
#define START			((unsigned long) 0xa000000100000000)
#define END			((unsigned long) &_end)
#define PHYS_START		(64*1024*1024)
#define IS_PINNED_ADDRESS(loc)			(loc > PHYS_START && \
		        			loc< \
						((PHYS_START \
						 + (END - \
						   START))))

/*
 * Structure: dump_header_asm_t
 *  Function: This is the header for architecture-specific stuff.  It
 *            follows right after the dump header.
 */
/*typedef struct _dump_header_asm {*/

typedef struct __dump_header_asm {

        /* the dump magic number -- unique to verify dump is valid */
        uint64_t             dha_magic_number;

        /* the version number of this dump */
        uint32_t             dha_version;

        /* the size of this header (in case we can't read it) */
        uint32_t             dha_header_size;

        /* pointer to pt_regs, (OLD: (struct pt_regs *, NEW: (uint64_t)) */
	uint64_t             dha_pt_regs;

	/* the dump registers */
	struct pt_regs       dha_regs;

        /* the rnat register saved after flushrs */
        uint64_t             dha_rnat;

	/* the pfs register saved after flushrs */
	uint64_t             dha_pfs;

	/* the bspstore register saved after flushrs */
	uint64_t             dha_bspstore;

	/* smp specific */
	uint32_t	     dha_smp_num_cpus;
	uint32_t	     dha_dumping_cpu;	
	struct pt_regs	     dha_smp_regs[DUMP_MAX_NUM_CPUS];
	uint64_t	     dha_smp_current_task[DUMP_MAX_NUM_CPUS];
	uint64_t	     dha_stack[DUMP_MAX_NUM_CPUS];
	uint64_t	     dha_stack_ptr[DUMP_MAX_NUM_CPUS];

} __attribute__((packed)) dump_header_asm_t;


extern struct __dump_header_asm dump_header_asm;

#ifdef __KERNEL__
static inline void get_current_regs(struct pt_regs *regs)
{
	/* 
	 * REMIND: Looking at functions/Macros like:
	 *		 DO_SAVE_SWITCH_STACK
	 *		 ia64_switch_to()
	 *		 ia64_save_extra()
	 *		 switch_to()
	 *	   to implement this new feature that Matt seem to have added
	 *	   to panic.c; seems all platforms are now expected to provide
	 *	   this function to dump the current registers into the pt_regs
	 *	   structure.
	 */
	volatile unsigned long rsc_value;/*for storing the rsc value*/
	volatile unsigned long ic_value;

	__asm__ __volatile__("mov %0=b6;;":"=r"(regs->b6));
	__asm__ __volatile__("mov %0=b7;;":"=r"(regs->b7));
	
        __asm__ __volatile__("mov %0=ar.csd;;":"=r"(regs->ar_csd));
	__asm__ __volatile__("mov %0=ar.ssd;;":"=r"(regs->ar_ssd));
	__asm__ __volatile__("mov %0=psr;;":"=r"(ic_value));
	if(ic_value & 0x1000)/*Within an interrupt*/
	{
		__asm__ __volatile__("mov %0=cr.ipsr;;":"=r"(regs->cr_ipsr));
		__asm__ __volatile__("mov %0=cr.iip;;":"=r"(regs->cr_iip));
		__asm__ __volatile__("mov %0=cr.ifs;;":"=r"(regs->cr_ifs));
	}
	else
	{
		regs->cr_ipsr=regs->cr_iip=regs->cr_ifs=(unsigned long)-1;
	}
	__asm__ __volatile__("mov %0=ar.unat;;":"=r"(regs->ar_unat));
	__asm__ __volatile__("mov %0=ar.pfs;;":"=r"(regs->ar_pfs));
	__asm__ __volatile__("mov %0=ar.rsc;;":"=r"(rsc_value));
	regs->ar_rsc = rsc_value;
	/*loadrs is from 16th bit to 29th bit of rsc*/
	regs->loadrs =  rsc_value >> 16 & (unsigned long)0x3fff;
	/*setting the rsc.mode value to 0 (rsc.mode is the last two bits of rsc)*/
	__asm__ __volatile__("mov ar.rsc=%0;;"::"r"(rsc_value & (unsigned long)(~3)));
	__asm__ __volatile__("mov %0=ar.rnat;;":"=r"(regs->ar_rnat));
	__asm__ __volatile__("mov %0=ar.bspstore;;":"=r"(regs->ar_bspstore));
	/*copying the original value back*/
	__asm__ __volatile__("mov ar.rsc=%0;;"::"r"(rsc_value));
	__asm__ __volatile__("mov %0=pr;;":"=r"(regs->pr));
	__asm__ __volatile__("mov %0=ar.fpsr;;":"=r"(regs->ar_fpsr));
	__asm__ __volatile__("mov %0=ar.ccv;;":"=r"(regs->ar_ccv));

	__asm__ __volatile__("mov %0=r2;;":"=r"(regs->r2));
        __asm__ __volatile__("mov %0=r3;;":"=r"(regs->r3));
        __asm__ __volatile__("mov %0=r8;;":"=r"(regs->r8));
        __asm__ __volatile__("mov %0=r9;;":"=r"(regs->r9));
        __asm__ __volatile__("mov %0=r10;;":"=r"(regs->r10));
	__asm__ __volatile__("mov %0=r11;;":"=r"(regs->r11));
        __asm__ __volatile__("mov %0=r12;;":"=r"(regs->r12));
	__asm__ __volatile__("mov %0=r13;;":"=r"(regs->r13));
	__asm__ __volatile__("mov %0=r14;;":"=r"(regs->r14));
	__asm__ __volatile__("mov %0=r15;;":"=r"(regs->r15));
	__asm__ __volatile__("mov %0=r16;;":"=r"(regs->r16));
	__asm__ __volatile__("mov %0=r17;;":"=r"(regs->r17));
	__asm__ __volatile__("mov %0=r18;;":"=r"(regs->r18));
	__asm__ __volatile__("mov %0=r19;;":"=r"(regs->r19));
	__asm__ __volatile__("mov %0=r20;;":"=r"(regs->r20));
	__asm__ __volatile__("mov %0=r21;;":"=r"(regs->r21));
	__asm__ __volatile__("mov %0=r22;;":"=r"(regs->r22));
	__asm__ __volatile__("mov %0=r23;;":"=r"(regs->r23));
	__asm__ __volatile__("mov %0=r24;;":"=r"(regs->r24));
	__asm__ __volatile__("mov %0=r25;;":"=r"(regs->r25));
	__asm__ __volatile__("mov %0=r26;;":"=r"(regs->r26));
	__asm__ __volatile__("mov %0=r27;;":"=r"(regs->r27));
	__asm__ __volatile__("mov %0=r28;;":"=r"(regs->r28));
	__asm__ __volatile__("mov %0=r29;;":"=r"(regs->r29));
	__asm__ __volatile__("mov %0=r30;;":"=r"(regs->r30));
	__asm__ __volatile__("mov %0=r31;;":"=r"(regs->r31));
}

/* Perhaps added to Common Arch Specific Functions and moved to dump.h some day */
extern void * __dump_memcpy(void *, const void *, size_t);
#endif /* __KERNEL__ */

#endif /* _ASM_DUMP_H */
