/*
 * Kernel Debugger Architecture Dependent Breakpoint Handling
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1999-2004 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/ptrace.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include <asm/pgalloc.h>


static char *kdba_rwtypes[] = { "Instruction(Register)", "Data Write",
			"I/O", "Data Access"};

/*
 * Table describing processor architecture hardware
 * breakpoint registers.
 */

static kdbhard_bp_t kdb_hardbreaks[KDB_MAXHARDBPT];

/*
 * kdba_db_trap
 *
 *	Perform breakpoint processing upon entry to the
 *	processor debugger fault.   Determine and print
 *	the active breakpoint.
 *
 * Parameters:
 *	regs	Exception frame containing machine register state
 *	error	Error number passed to kdb.
 * Outputs:
 *	None.
 * Returns:
 *	KDB_DB_BPT	Standard instruction or data breakpoint encountered
 *	KDB_DB_SS	Single Step fault ('ss' command or end of 'ssb' command)
 *	KDB_DB_SSB	Single Step fault, caller should continue ('ssb' command)
 *	KDB_DB_SSBPT	Single step over breakpoint
 *	KDB_DB_NOBPT	No existing kdb breakpoint matches this debug exception
 * Locking:
 *	None.
 * Remarks:
 *	Yup, there be goto's here.
 *
 *	If multiple processors receive debug exceptions simultaneously,
 *	one may be waiting at the kdb fence in kdb() while the user
 *	issues a 'bc' command to clear the breakpoint the processor
 *	which is waiting has already encountered.  If this is the case,
 *	the debug registers will no longer match any entry in the
 *	breakpoint table, and we'll return the value KDB_DB_NOBPT.
 *	This can cause a panic in die_if_kernel().  It is safer to
 *	disable the breakpoint (bd), go until all processors are past
 *	the breakpoint then clear the breakpoint (bc).  This code
 *	recognises a breakpoint even when disabled but not when it has
 *	been cleared.
 *
 *	WARNING: This routine clears the debug state.  It should be called
 *		 once per debug and the result cached.
 */

kdb_dbtrap_t
kdba_db_trap(struct pt_regs *regs, int error)
{
	int i;
	kdb_dbtrap_t rv = KDB_DB_BPT;
	kdb_bp_t *bp;

	if (KDB_NULL_REGS(regs))
		return KDB_DB_NOBPT;

	if (KDB_DEBUG(BP))
		kdb_printf("kdba_db_trap: error %d\n", error);

	if (error == 36) {
		/* Single step */
		if (KDB_STATE(SSBPT)) {
			if (KDB_DEBUG(BP))
				kdb_printf("ssbpt\n");
			KDB_STATE_CLEAR(SSBPT);
			for(i=0,bp=kdb_breakpoints;
			    i < KDB_MAXBPT;
			    i++, bp++) {
				if (KDB_DEBUG(BP))
					kdb_printf("bp 0x%p enabled %d delayed %d global %d cpu %d\n",
						   bp, bp->bp_enabled, bp->bp_delayed, bp->bp_global, bp->bp_cpu);
				if (!bp->bp_enabled)
					continue;
				if (!bp->bp_global && bp->bp_cpu != smp_processor_id())
					continue;
				if (KDB_DEBUG(BP))
					kdb_printf("bp for this cpu\n");
				if (bp->bp_delayed) {
					bp->bp_delayed = 0;
					if (KDB_DEBUG(BP))
						kdb_printf("kdba_installbp\n");
					kdba_installbp(regs, bp);
					if (!KDB_STATE(DOING_SS)) {
						kdba_clearsinglestep(regs);
						return(KDB_DB_SSBPT);
					}
					break;
				}
			}
			if (i == KDB_MAXBPT) {
				kdb_printf("kdb: Unable to find delayed breakpoint\n");
			}
			if (!KDB_STATE(DOING_SS)) {
				kdba_clearsinglestep(regs);
				return(KDB_DB_NOBPT);
			}
			/* FALLTHROUGH */
		}

		/*
		 * KDB_STATE_DOING_SS is set when the kernel debugger is using
		 * the processor trap flag to single-step a processor.  If a
		 * single step trap occurs and this flag is clear, the SS trap
		 * will be ignored by KDB and the kernel will be allowed to deal
		 * with it as necessary (e.g. for ptrace).
		 */
		if (!KDB_STATE(DOING_SS))
			return(KDB_DB_NOBPT);

		/* single step */
		rv = KDB_DB_SS;		/* Indicate single step */
		if (KDB_STATE(DOING_SSB))		/* No ia64 ssb support yet */
			KDB_STATE_CLEAR(DOING_SSB);	/* No ia64 ssb support yet */
		if (KDB_STATE(DOING_SSB)) {
			/* No IA64 ssb support yet */
		} else {
			/*
			 * Print current insn
			 */
			kdb_machreg_t pc = regs->cr_iip + ia64_psr(regs)->ri * 6;
			kdb_printf("SS trap at ");
			kdb_symbol_print(pc, NULL, KDB_SP_DEFAULT|KDB_SP_NEWLINE);
			kdb_id1(pc);
			KDB_STATE_CLEAR(DOING_SS);
		}

		if (rv != KDB_DB_SSB)
			kdba_clearsinglestep(regs);
	}

	return(rv);
}

/*
 * kdba_bp_trap
 *
 *	Perform breakpoint processing upon entry to the
 *	processor breakpoint instruction fault.   Determine and print
 *	the active breakpoint.
 *
 * Parameters:
 *	regs	Exception frame containing machine register state
 *	error	Error number passed to kdb.
 * Outputs:
 *	None.
 * Returns:
 *	0	Standard instruction or data breakpoint encountered
 *	1	Single Step fault ('ss' command)
 *	2	Single Step fault, caller should continue ('ssb' command)
 *	3	No existing kdb breakpoint matches this debug exception
 * Locking:
 *	None.
 * Remarks:
 *
 *	If multiple processors receive debug exceptions simultaneously,
 *	one may be waiting at the kdb fence in kdb() while the user
 *	issues a 'bc' command to clear the breakpoint the processor which
 *	is waiting has already encountered.   If this is the case, the
 *	debug registers will no longer match any entry in the breakpoint
 *	table, and we'll return the value '3'.  This can cause a panic
 *	in die_if_kernel().  It is safer to disable the breakpoint (bd),
 *	'go' until all processors are past the breakpoint then clear the
 *	breakpoint (bc).  This code recognises a breakpoint even when
 *	disabled but not when it has been cleared.
 *
 *	WARNING: This routine resets the ip.  It should be called
 *		 once per breakpoint and the result cached.
 */

kdb_dbtrap_t
kdba_bp_trap(struct pt_regs *regs, int error)
{
	int i;
	kdb_dbtrap_t rv;
	kdb_bp_t *bp;

	if (KDB_NULL_REGS(regs))
		return KDB_DB_NOBPT;

	/*
	 * Determine which breakpoint was encountered.
	 */
	if (KDB_DEBUG(BP))
		kdb_printf("kdba_bp_trap: ip=0x%lx "
			   "regs=0x%p sp=0x%lx\n",
			   regs->cr_iip, regs, regs->r12);

	rv = KDB_DB_NOBPT;	/* Cause kdb() to return */

	for(i=0, bp=kdb_breakpoints; i<KDB_MAXBPT; i++, bp++) {
		if (bp->bp_free)
			continue;
		if (!bp->bp_global && bp->bp_cpu != smp_processor_id())
			continue;
		 if (bp->bp_addr == regs->cr_iip) {
			/* Hit this breakpoint.  */
			kdb_printf("Instruction(i) breakpoint #%d at 0x%lx\n",
				  i, regs->cr_iip);
			kdb_id1(regs->cr_iip);
			rv = KDB_DB_BPT;
			bp->bp_delay = 1;
			/* SSBPT is set when the kernel debugger must single
			 * step a task in order to re-establish an instruction
			 * breakpoint which uses the instruction replacement
			 * mechanism.  It is cleared by any action that removes
			 * the need to single-step the breakpoint.
			 */
			KDB_STATE_SET(SSBPT);
			break;
		}
	}

	return rv;
}

/*
 * kdba_handle_bp
 *
 *	Handle an instruction-breakpoint trap.  Called when re-installing
 *	an enabled breakpoint which has has the bp_delay bit set.
 *
 * Parameters:
 * Returns:
 * Locking:
 * Remarks:
 *
 * Ok, we really need to:
 *	1) Restore the original instruction byte(s)
 *	2) Single Step
 *	3) Restore breakpoint instruction
 *	4) Continue.
 *
 *
 */

static void
kdba_handle_bp(struct pt_regs *regs, kdb_bp_t *bp)
{
	if (KDB_NULL_REGS(regs))
		return;

	if (KDB_DEBUG(BP))
		kdb_printf("regs->cr_iip = 0x%lx\n", regs->cr_iip);

	/*
	 * Setup single step
	 */
	kdba_setsinglestep(regs);

	/*
	 * Reset delay attribute
	 */
	bp->bp_delay = 0;
	bp->bp_delayed = 1;
}


/*
 * kdba_bptype
 *
 *	Return a string describing type of breakpoint.
 *
 * Parameters:
 *	bph	Pointer to hardware breakpoint description
 * Outputs:
 *	None.
 * Returns:
 *	Character string.
 * Locking:
 *	None.
 * Remarks:
 */

char *
kdba_bptype(kdbhard_bp_t *bph)
{
	char *mode;

	mode = kdba_rwtypes[bph->bph_mode];

	return mode;
}

/*
 * kdba_printbpreg
 *
 *	Print register name assigned to breakpoint
 *
 * Parameters:
 *	bph	Pointer hardware breakpoint structure
 * Outputs:
 *	None.
 * Returns:
 *	None.
 * Locking:
 *	None.
 * Remarks:
 */

static void
kdba_printbpreg(kdbhard_bp_t *bph)
{
	kdb_printf(" in dr%ld", bph->bph_reg);
}

/*
 * kdba_printbp
 *
 *	Print string describing hardware breakpoint.
 *
 * Parameters:
 *	bph	Pointer to hardware breakpoint description
 * Outputs:
 *	None.
 * Returns:
 *	None.
 * Locking:
 *	None.
 * Remarks:
 */

void
kdba_printbp(kdb_bp_t *bp)
{
	kdb_printf("\n    is enabled");
	if (bp->bp_hardtype) {
		kdba_printbpreg(bp->bp_hard);
		if (bp->bp_hard->bph_mode != 0) {
			kdb_printf(" for %d bytes",
				   bp->bp_hard->bph_length+1);
		}
	}
}

/*
 * kdba_parsebp
 *
 *	Parse architecture dependent portion of the
 *	breakpoint command.
 *
 * Parameters:
 *	None.
 * Outputs:
 *	None.
 * Returns:
 *	Zero for success, a kdb diagnostic for failure
 * Locking:
 *	None.
 * Remarks:
 *	for IA64 architure, data access, data write and
 *	I/O breakpoints are supported in addition to instruction
 *	breakpoints.
 *
 *	{datar|dataw|io|inst} [length]
 */

int
kdba_parsebp(int argc, const char **argv, int *nextargp, kdb_bp_t *bp)
{
	int nextarg = *nextargp;
	int diag;
	kdbhard_bp_t *bph = &bp->bp_template;

	bph->bph_mode = 0;		/* Default to instruction breakpoint */
	bph->bph_length = 0;		/* Length must be zero for insn bp */
	if ((argc + 1) != nextarg) {
		if (strnicmp(argv[nextarg], "datar", sizeof("datar")) == 0) {
			bph->bph_mode = 3;
		} else if (strnicmp(argv[nextarg], "dataw", sizeof("dataw")) == 0) {
			bph->bph_mode = 1;
		} else if (strnicmp(argv[nextarg], "io", sizeof("io")) == 0) {
			bph->bph_mode = 2;
		} else if (strnicmp(argv[nextarg], "inst", sizeof("inst")) == 0) {
			bph->bph_mode = 0;
		} else {
			return KDB_ARGCOUNT;
		}

		bph->bph_length = 3;	/* Default to 4 byte */

		nextarg++;

		if ((argc + 1) != nextarg) {
			unsigned long len;

			diag = kdbgetularg((char *)argv[nextarg],
					   &len);
			if (diag)
				return diag;


			if ((len > 4) || (len == 3))
				return KDB_BADLENGTH;

			bph->bph_length = len;
			bph->bph_length--; /* Normalize for debug register */
			nextarg++;
		}

		if ((argc + 1) != nextarg)
			return KDB_ARGCOUNT;

		/*
		 * Indicate to architecture independent level that
		 * a hardware register assignment is required to enable
		 * this breakpoint.
		 */

		bph->bph_free = 0;
	} else {
		if (KDB_DEBUG(BP))
			kdb_printf("kdba_bp: no args, forcehw is %d\n", bp->bp_forcehw);
		if (bp->bp_forcehw) {
			/*
			 * We are forced to use a hardware register for this
			 * breakpoint because either the bph or bpha
			 * commands were used to establish this breakpoint.
			 */
			bph->bph_free = 0;
		} else {
			/*
			 * Indicate to architecture dependent level that
			 * the instruction replacement breakpoint technique
			 * should be used for this breakpoint.
			 */
			bph->bph_free = 1;
			bp->bp_adjust = 0;	/* software, break is fault, not trap */
		}
	}

	if (bph->bph_mode == 0 && kdba_verify_rw(bp->bp_addr, bph->bph_length+1)) {
		kdb_printf("Invalid address for breakpoint, ignoring bp command\n");
		return KDB_BADADDR;
	}

	*nextargp = nextarg;
	if (!bph->bph_free) {
		kdb_printf("kdba_parsebp hardware breakpoints are not supported yet\n");
		return KDB_NOTIMP;
	}
	return 0;
}

/*
 * kdba_allocbp
 *
 *	Associate a hardware register with a breakpoint.
 *
 * Parameters:
 *	None.
 * Outputs:
 *	None.
 * Returns:
 *	A pointer to the allocated register kdbhard_bp_t structure for
 *	success, Null and a non-zero diagnostic for failure.
 * Locking:
 *	None.
 * Remarks:
 */

kdbhard_bp_t *
kdba_allocbp(kdbhard_bp_t *bph, int *diagp)
{
	int i;
	kdbhard_bp_t *newbph;

	for(i=0,newbph=kdb_hardbreaks; i < KDB_MAXHARDBPT; i++, newbph++) {
		if (newbph->bph_free) {
			break;
		}
	}

	if (i == KDB_MAXHARDBPT) {
		*diagp = KDB_TOOMANYDBREGS;
		return NULL;
	}

	*diagp = 0;

	/*
	 * Copy data from template.  Can't just copy the entire template
	 * here because the register number in kdb_hardbreaks must be
	 * preserved.
	 */
	newbph->bph_data = bph->bph_data;
	newbph->bph_write = bph->bph_write;
	newbph->bph_mode = bph->bph_mode;
	newbph->bph_length = bph->bph_length;

	/*
	 * Mark entry allocated.
	 */
	newbph->bph_free = 0;

	return newbph;
}

/*
 * kdba_freebp
 *
 *	Deallocate a hardware breakpoint
 *
 * Parameters:
 *	None.
 * Outputs:
 *	None.
 * Returns:
 *	Zero for success, a kdb diagnostic for failure
 * Locking:
 *	None.
 * Remarks:
 */

void
kdba_freebp(kdbhard_bp_t *bph)
{
	bph->bph_free = 1;
}

/*
 * kdba_initbp
 *
 *	Initialize the breakpoint table for the hardware breakpoint
 *	register.
 *
 * Parameters:
 *	None.
 * Outputs:
 *	None.
 * Returns:
 *	Zero for success, a kdb diagnostic for failure
 * Locking:
 *	None.
 * Remarks:
 *
 *	There is one entry per register.  On the ia64 architecture
 *	all the registers are interchangeable, so no special allocation
 *	criteria are required.
 */

void
kdba_initbp(void)
{
	int i;
	kdbhard_bp_t *bph;

	/*
	 * Clear the hardware breakpoint table
	 */

	memset(kdb_hardbreaks, '\0', sizeof(kdb_hardbreaks));

	for(i=0,bph=kdb_hardbreaks; i<KDB_MAXHARDBPT; i++, bph++) {
		bph->bph_reg = i;
		bph->bph_free = 1;
	}
}

/*
 * kdba_installbp
 *
 *	Install a breakpoint
 *
 * Parameters:
 *	regs	Exception frame
 *	bp	Breakpoint structure for the breakpoint to be installed
 * Outputs:
 *	None.
 * Returns:
 *	0 if breakpoint set, otherwise error.
 * Locking:
 *	None.
 * Remarks:
 *	For hardware breakpoints, a debug register is allocated
 *	and assigned to the breakpoint.  If no debug register is
 *	available, a warning message is printed and the breakpoint
 *	is disabled.
 *
 *	For instruction replacement breakpoints, we must single-step
 *	over the replaced instruction at this point so we can re-install
 *	the breakpoint instruction after the single-step.  SSBPT is set
 *	when the breakpoint is initially hit and is cleared by any action
 *	that removes the need for single-step over the breakpoint.
 */

int
kdba_installbp(struct pt_regs *regs, kdb_bp_t *bp)
{
	/*
	 * Install the breakpoint, if it is not already installed.
	 */

	if (KDB_DEBUG(BP)) {
		kdb_printf("kdba_installbp bp_installed %d\n", bp->bp_installed);
	}
	if (!KDB_STATE(SSBPT))
		bp->bp_delay = 0;
	if (!bp->bp_installed) {
		if (bp->bp_hardtype) {
			kdba_installdbreg(bp);
			bp->bp_installed = 1;
			if (KDB_DEBUG(BP)) {
				kdb_printf("kdba_installbp hardware reg %ld at " kdb_bfd_vma_fmt0 "\n",
					   bp->bp_hard->bph_reg, bp->bp_addr);
			}
		} else if (bp->bp_delay) {
			if (KDB_DEBUG(BP))
				kdb_printf("kdba_installbp delayed bp\n");
			kdba_handle_bp(regs, bp);
		} else {
			/* Software breakpoints always use slot 0 in the 128 bit
			 * bundle.  The template type does not matter, slot 0
			 * can only be M or B and the encodings for break.m and
			 * break.b are the same.
			 */
			unsigned long break_inst;
			if (kdb_getarea_size(bp->bp_inst.inst, bp->bp_addr, sizeof(bp->bp_inst.inst))) {
				kdb_printf("kdba_installbp failed to read software breakpoint at 0x%lx\n", bp->bp_addr);
				return(1);
			}
			break_inst = (bp->bp_inst.inst[0] & ~INST_SLOT0_MASK) | BREAK_INSTR;
			if (kdb_putarea_size(bp->bp_addr, &break_inst, sizeof(break_inst))) {
				kdb_printf("kdba_installbp failed to set software breakpoint at 0x%lx\n", bp->bp_addr);
				return(1);
			}
			if (KDB_DEBUG(BP))
				kdb_printf("kdba_installbp instruction 0x%lx at " kdb_bfd_vma_fmt0 "\n",
					   BREAK_INSTR, bp->bp_addr);
			bp->bp_installed = 1;
			flush_icache_range(bp->bp_addr, bp->bp_addr+16);
		}
	}
	return(0);
}

/*
 * kdba_removebp
 *
 *	Make a breakpoint ineffective.
 *
 * Parameters:
 *	None.
 * Outputs:
 *	None.
 * Returns:
 *	0 if breakpoint removed, otherwise error.
 * Locking:
 *	None.
 * Remarks:
 */

int
kdba_removebp(kdb_bp_t *bp)
{
	/*
	 * For hardware breakpoints, remove it from the active register,
	 * for software breakpoints, restore the instruction stream.
	 */
	if (KDB_DEBUG(BP)) {
		kdb_printf("kdba_removebp bp_installed %d\n", bp->bp_installed);
	}
	if (bp->bp_installed) {
		if (bp->bp_hardtype) {
			if (KDB_DEBUG(BP)) {
				kdb_printf("kdb: removing hardware reg %ld at " kdb_bfd_vma_fmt0 "\n",
					   bp->bp_hard->bph_reg, bp->bp_addr);
			}
			kdba_removedbreg(bp);
		} else {
			if (KDB_DEBUG(BP))
				kdb_printf("kdb: restoring instruction 0x%016lx%016lx at " kdb_bfd_vma_fmt0 "\n",
					   bp->bp_inst.inst[0], bp->bp_inst.inst[1], bp->bp_addr);
			if (kdba_putarea_size(bp->bp_addr, bp->bp_inst.inst, sizeof(bp->bp_inst.inst)))
				return(1);
		}
		bp->bp_installed = 0;
		flush_icache_range(bp->bp_addr, bp->bp_addr+16);
	}
	return(0);
}
