#ifndef _PPC64_PACA_H
#define _PPC64_PACA_H

/*
 * include/asm-ppc64/paca.h
 *
 * This control block defines the PACA which defines the processor 
 * specific data for each logical processor on the system.  
 * There are some pointers defined that are utilized by PLIC.
 *
 * C 2001 PPC 64 Team, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */    

#include	<asm/types.h>
#include	<asm/iSeries/ItLpPaca.h>
#include	<asm/iSeries/ItLpRegSave.h>
#include	<asm/mmu.h>

extern struct paca_struct paca[];
register struct paca_struct *local_paca asm("r13");
#define get_paca()	local_paca

struct task_struct;
struct ItLpQueue;

/*
 * Defines the layout of the paca.
 *
 * This structure is not directly accessed by firmware or the service
 * processor except for the first two pointers that point to the
 * ItLpPaca area and the ItLpRegSave area for this CPU.  Both the
 * ItLpPaca and ItLpRegSave objects are currently contained within the
 * PACA but they do not need to be.
 */
struct paca_struct {
	/*
	 * Because hw_cpu_id, unlike other paca fields, is accessed
	 * routinely from other CPUs (from the IRQ code), we stick to
	 * read-only (after boot) fields in the first cacheline to
	 * avoid cacheline bouncing.
	 */

	/*
	 * MAGIC: These first two pointers can't be moved - they're
	 * accessed by the firmware
	 */
	struct ItLpPaca *lppaca_ptr;	/* Pointer to LpPaca for PLIC */
	struct ItLpRegSave *reg_save_ptr; /* Pointer to LpRegSave for PLIC */

	/*
	 * MAGIC: the spinlock functions in arch/ppc64/lib/locks.c
	 * load lock_token and paca_index with a single lwz
	 * instruction.  They must travel together and be properly
	 * aligned.
	 */
	u16 lock_token;			/* Constant 0x8000, used in locks */
	u16 paca_index;			/* Logical processor number */

	u32 default_decr;		/* Default decrementer value */
	struct ItLpQueue *lpqueue_ptr;	/* LpQueue handled by this CPU */
	u64 kernel_toc;			/* Kernel TOC address */
	u64 stab_real;			/* Absolute address of segment table */
	u64 stab_addr;			/* Virtual address of segment table */
	void *emergency_sp;		/* pointer to emergency stack */
	u16 hw_cpu_id;			/* Physical processor number */
	u8 cpu_start;			/* At startup, processor spins until */
					/* this becomes non-zero. */

	/*
	 * Now, starting in cacheline 2, the exception save areas
	 */
	u64 exgen[8] __attribute__((aligned(0x80))); /* used for most interrupts/exceptions */
	u64 exmc[8];		/* used for machine checks */
	u64 exslb[8];		/* used for SLB/segment table misses
				 * on the linear mapping */
	u64 slb_r3;		/* spot to save R3 on SLB miss */
	mm_context_t context;
	u16 slb_cache[SLB_CACHE_ENTRIES];
	u16 slb_cache_ptr;

	/*
	 * then miscellaneous read-write fields
	 */
	struct task_struct *__current;	/* Pointer to current */
	u64 kstack;			/* Saved Kernel stack addr */
	u64 stab_rr;			/* stab/slb round-robin counter */
	u64 next_jiffy_update_tb;	/* TB value for next jiffy update */
	u64 saved_r1;			/* r1 save for RTAS calls */
	u64 saved_msr;			/* MSR saved here by enter_rtas */
	u32 lpevent_count;		/* lpevents processed  */
	u8 proc_enabled;		/* irq soft-enable flag */

	/* not yet used */
	u64 exdsi[8];		/* used for linear mapping hash table misses */

	/*
	 * iSeries structues which the hypervisor knows about - Not
	 * sure if these particularly need to be cacheline aligned.
	 * The lppaca is also used on POWER5 pSeries boxes.
	 */
	struct ItLpPaca lppaca __attribute__((aligned(0x80)));
	struct ItLpRegSave reg_save;

	/*
	 * iSeries profiling support
	 *
	 * FIXME: do we still want this, or can we ditch it in favour
	 * of oprofile?
	 */
	u32 *prof_buffer;		/* iSeries profiling buffer */
	u32 *prof_stext;		/* iSeries start of kernel text */
	u32 prof_multiplier;
	u32 prof_counter;
	u32 prof_shift;			/* iSeries shift for profile
					 * bucket size */
	u32 prof_len;			/* iSeries length of profile */
	u8 prof_enabled;		/* 1=iSeries profiling enabled */
};

#endif /* _PPC64_PACA_H */
