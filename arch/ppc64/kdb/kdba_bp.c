/*
 * Kernel Debugger Architecture Dependent Breakpoint Handling
 *
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) Scott Lurndal (slurn@engr.sgi.com)
 * Copyright (C) Scott Foehner (sfoehner@engr.sgi.com)
 * Copyright (C) Srinivasa Thirumalachar (sprasad@engr.sgi.com)
 *
 * See the file LIA-COPYRIGHT for additional information.
 *
 * Written March 1999 by Scott Lurndal at Silicon Graphics, Inc.
 *
 * Modifications from:
 *      Richard Bass                    1999/07/20
 *              Many bug fixes and enhancements.
 *      Scott Foehner
 *              Port to ia64
 *	Scott Lurndal			1999/12/12
 *		v1.0 restructuring.
 *	Keith Owens			2000/05/23
 *		KDB v1.2
 */

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/ptrace.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include "privinst.h"

static char *kdba_rwtypes[] = { "Instruction(Register)", "Data Write",
			"I/O", "Data Access"};

extern void set_all_DABR(unsigned long val);

/*
 * Table describing processor architecture hardware
 * breakpoint registers.
 */

kdbhard_bp_t	kdb_hardbreaks[KDB_MAXHARDBPT];

/*
 * kdba_db_trap
 *
 * 	Perform breakpoint processing upon entry to the
 *	processor debugger fault.   Determine and print
 *	the active breakpoint.
 *
 * Parameters:
 *	ef	Exception frame containing machine register state
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
kdba_db_trap(kdb_eframe_t ef, int error_unused)
{
	kdb_machreg_t  msr,trap;
	int rw, reg;
	int i;
	kdb_dbtrap_t rv = KDB_DB_BPT;
	kdb_bp_t *bp;
	unsigned long primary;
	unsigned long extended;

	msr = get_msr();
	trap = ef->trap;
	if (KDB_DEBUG(BP))
		kdb_printf("kdb: msr 0x%lx trap 0x%lx\n", msr,trap);
	if (msr & MSR_SE || ((trap & 0x700) || (trap & 0xd00))) 
	{
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
					kdba_installbp(ef, bp);
					if (!KDB_STATE(DOING_SS)) {
						set_msr(get_msr() & ~MSR_SE);
						return(KDB_DB_SSBPT);
					}
					break;
				}
			}
			if (i == KDB_MAXBPT) {
				kdb_printf("kdb: Unable to find delayed breakpoint\n");
			}
			if (!KDB_STATE(DOING_SS)) {
				set_msr(get_msr() & ~MSR_SE);
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
			goto unknown;

		/* single step */
		rv = KDB_DB_SS;		/* Indicate single step */
		if (KDB_STATE(DOING_SSB)) {
		    unsigned long instruction;

			kdb_id1(ef->nip);
			kdb_getarea(instruction,ef->nip);
			primary=instruction & 0xfc000000;
			extended=instruction & 0x000007fe;
			if (kdb_getarea(instruction, ef->nip) ||   /* read failure */
/* branch conditional */
			    (primary==16 )||
/* branch */
			    (primary==18 )||    
/* branch conditional to LR, or branch conditional to CR */
			    (primary==19 && (extended==16 || extended == 528) 
			     )) {
				/*
				 * End the ssb command here.
				 */
			    KDB_STATE_CLEAR(DOING_SSB);
			    KDB_STATE_CLEAR(DOING_SS);
			    }
			rv = KDB_DB_SSB; /* Indicate ssb - dismiss immediately */
		} else {
			/*
			 * Print current insn
			 */
			kdb_printf("SS trap at ");
			kdb_symbol_print(ef->nip, NULL, KDB_SP_DEFAULT|KDB_SP_NEWLINE);
			kdb_printf(" "); /* wms */
			kdb_id1(ef->nip);
			KDB_STATE_CLEAR(DOING_SS);
		}

		if (rv != KDB_DB_SSB)
			set_msr(get_msr() & ~MSR_SE);
	}
	if (rv > 0)
		goto handled;
	
	goto handle;


handle:

	/*
	 * Determine which breakpoint was encountered.
	 */
	for(i=0, bp=kdb_breakpoints; i<KDB_MAXBPT; i++, bp++) {
		if (!(bp->bp_free)
		 && (bp->bp_global || bp->bp_cpu == smp_processor_id())
		 && (bp->bp_hard)
		 && (bp->bp_hard->bph_reg == reg)) {
			/*
			 * Hit this breakpoint.
			 */
			kdb_printf("%s breakpoint #%d at " kdb_bfd_vma_fmt "\n", 
				  kdba_rwtypes[rw],
				  i, (long )bp->bp_addr);

			/*
			 * For an instruction breakpoint, disassemble
			 * the current instruction.
			 */
			if (rw == 0) {
				kdb_id1(ef->nip);
			}

			goto handled;
		}
	}

unknown:
	rv = KDB_DB_NOBPT;	/* Cause kdb() to return */

handled:


	return rv;
}

/*
 * kdba_bp_trap
 *
 * 	Perform breakpoint processing upon entry to the
 *	processor breakpoint instruction fault.   Determine and print
 *	the active breakpoint.
 *
 * Parameters:
 *	ef	Exception frame containing machine register state
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
 * 	If multiple processors receive debug exceptions simultaneously,
 *	one may be waiting at the kdb fence in kdb() while the user
 *	issues a 'bc' command to clear the breakpoint the processor which
 * 	is waiting has already encountered.   If this is the case, the
 *	debug registers will no longer match any entry in the breakpoint
 *	table, and we'll return the value '3'.  This can cause a panic
 *	in die_if_kernel().  It is safer to disable the breakpoint (bd),
 *	'go' until all processors are past the breakpoint then clear the
 *	breakpoint (bc).  This code recognises a breakpoint even when
 *	disabled but not when it has been cleared.
 *
 *	WARNING: This routine resets the eip.  It should be called
 *		 once per breakpoint and the result cached.
 */

kdb_dbtrap_t
kdba_bp_trap(kdb_eframe_t ef, int error_unused)
{
	int i;
	kdb_dbtrap_t rv;
	kdb_bp_t *bp;

	/*
	 * Determine which breakpoint was encountered.
	 */
	if (KDB_DEBUG(BP))
		kdb_printf("kdba_bp_trap: eip=0x%lx (not adjusted) "
			   "msr=0x%lx trap=0x%lx ef=0x%p esp=0x%lx\n",
			   ef->nip, ef->msr, ef->trap, ef, ef->gpr[1]);

	rv = KDB_DB_NOBPT;	/* Cause kdb() to return */

	for(i=0, bp=kdb_breakpoints; i<KDB_MAXBPT; i++, bp++) {
		if (bp->bp_free)
			continue;
		if (!bp->bp_global && bp->bp_cpu != smp_processor_id())
			continue;
		 if (bp->bp_addr == (ef->nip - bp->bp_adjust)) {
			/* Hit this breakpoint.  */
			ef->nip -= bp->bp_adjust;
			kdb_printf("Instruction(i) breakpoint #%d at 0x%lx (adjusted)\n",
				  i, ef->nip);
			kdb_id1(ef->nip);
			rv = KDB_DB_BPT;
			bp->bp_delay = 1;
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
 *	1) Restore the original instruction byte
 *	2) Single Step
 *	3) Restore breakpoint instruction
 *	4) Continue.
 *
 *
 */

static void
kdba_handle_bp(kdb_eframe_t ef, kdb_bp_t *bp)
{
	if (!ef) {
		kdb_printf("kdba_handle_bp: ef == NULL\n");
		return;
	}

	if (KDB_DEBUG(BP))
		kdb_printf("ef->eip = 0x%lx\n", ef->nip);

	/*
	 * Setup single step
	 */
	kdba_setsinglestep(ef);

	/* KDB_STATE_SSBPT is set when the kernel debugger must single step
	 * a task in order to re-establish an instruction breakpoint which
	 * uses the instruction replacement mechanism. 
	 */
	KDB_STATE_SET(SSBPT);

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

void
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
 *	for Ia32 architure, data access, data write and
 *	I/O breakpoints are supported in addition to instruction
 * 	breakpoints.
 *
 *	{datar|dataw|io|inst} [length]
 */

int
kdba_parsebp(int argc, const char **argv, int *nextargp, kdb_bp_t *bp)
{
	int		nextarg = *nextargp;
	int		diag;
	kdbhard_bp_t 	*bph = &bp->bp_template;

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
			bp->bp_adjust = PPC64_ADJUST_OFFSET;
		}
	}

	*nextargp = nextarg;
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
 *	There is one entry per register.  On the ia32 architecture
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
 *	ef	Exception frame
 *	bp	Breakpoint structure for the breakpoint to be installed
 * Outputs:
 *	None.
 * Returns:
 *	None.
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
 *	the breakpoint instruction after the single-step.
 */

int
kdba_installbp(kdb_eframe_t ef, kdb_bp_t *bp)
{
    int rc;
	/*
	 * Install the breakpoint, if it is not already installed.
	 */

	if (KDB_DEBUG(BP)) {
		kdb_printf("kdba_installbp bp_installed %d\n", bp->bp_installed);
	}
	if (!bp->bp_installed) {
		if (bp->bp_hardtype) {
			kdba_installdbreg(bp); 
			bp->bp_installed = 1;
			if (KDB_DEBUG(BP)) {
				kdb_printf("kdba_installbp hardware reg %ld at " kdb_bfd_vma_fmt "\n",
					   (long unsigned int) bp->bp_hard->bph_reg, (long unsigned int) bp->bp_addr);
			}
		} else if (bp->bp_delay) {
			if (KDB_DEBUG(BP))
				kdb_printf("kdba_installbp delayed bp\n");
			kdba_handle_bp(ef, bp);
		} else {
		    if (KDB_DEBUG(BP))
			kdb_printf("0x%lx 0x%lx 0x%lx\n",bp->bp_inst,bp->bp_addr,sizeof(bp->bp_addr));
		    rc = kdb_getword(&bp->bp_inst, bp->bp_addr,sizeof(bp->bp_addr));
		    kdb_putword(bp->bp_addr, PPC64_BREAKPOINT_INSTRUCTION,sizeof(PPC64_BREAKPOINT_INSTRUCTION));
		    if (KDB_DEBUG(BP))
			kdb_printf("kdba_installbp instruction 0x%x at " kdb_bfd_vma_fmt "\n",
				   PPC64_BREAKPOINT_INSTRUCTION, bp->bp_addr);
		    bp->bp_installed = 1;
		}
	}
return 0;
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
 *	None.
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
				kdb_printf("kdb: removing hardware reg %ld at " kdb_bfd_vma_fmt "\n",
					   bp->bp_hard->bph_reg, bp->bp_addr);
			}
			kdba_removedbreg(bp);
		} else
		{
			if (KDB_DEBUG(BP))
				kdb_printf("kdb: restoring instruction 0x%lx at " kdb_bfd_vma_fmt "\n",
					   bp->bp_inst, bp->bp_addr);
			kdb_putword(bp->bp_addr, bp->bp_inst,sizeof(bp->bp_inst));
		}
		bp->bp_installed = 0;
	}
return 0;
}

#if 0
#define systemcfg naca
#endif

/* install data breakpoint */
void
kdba_installdbreg(kdb_bp_t *bp) {
    if (systemcfg->platform==PLATFORM_PSERIES)
    {
    /* set_dabr is the kdb form, using mtspr instructions */
	set_dabr(bp->bp_addr); 
    } else if (systemcfg->platform==PLATFORM_PSERIES_LPAR ) {
	/*set_all_DABR(bp->bp_addr); missing from 2.5? */
#if 0
	HvCall_setDABR(bp->bp_addr);
#endif
    } else if (systemcfg->platform==PLATFORM_ISERIES_LPAR ) {
	/* different hcall interface needed here. */
    }
}

/* remove data breakpoint-- set it to zero. */
void
kdba_removedbreg(kdb_bp_t *bp) {
    if (systemcfg->platform==PLATFORM_PSERIES)
    {
    /* set_dabr is the kdb form, using mtspr instructions */
	set_dabr(0x0); 
    } else if (systemcfg->platform==PLATFORM_PSERIES_LPAR ) {
	/*set_all_DABR(bp->bp_addr); missing from 2.5? */
#if 0
	HvCall_setDABR(0x0);
#endif
    } else if (systemcfg->platform==PLATFORM_ISERIES_LPAR ) {
	/* different hcall interface needed here. */
    }
}
