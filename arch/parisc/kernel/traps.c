/*
 *  linux/arch/parisc/traps.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 1999, 2000  Philipp Rumpf <prumpf@tux.org>
 */

/*
 * 'Traps.c' handles hardware traps and faults after we have saved some
 * state in 'asm.s'.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/atomic.h>

#include <asm/smp.h>
#include <asm/pdc.h>

#ifdef CONFIG_KWDB
#include <kdb/break.h>		/* for BI2_KGDB_GDB */
#include <kdb/kgdb_types.h>	/* for __() */
#include <kdb/save_state.h>	/* for struct save_state */
#include <kdb/kgdb_machine.h>	/* for pt_regs_to_ssp and ssp_to_pt_regs */
#include <kdb/trap.h>		/* for I_BRK_INST */
#endif /* CONFIG_KWDB */


static inline void console_verbose(void)
{
	extern int console_loglevel;
	console_loglevel = 15;
}


void page_exception(void);

/*
 * These constants are for searching for possible module text
 * segments.  VMALLOC_OFFSET comes from mm/vmalloc.c; MODULE_RANGE is
 * a guess of how much space is likely to be vmalloced.
 */
#define VMALLOC_OFFSET (8*1024*1024)
#define MODULE_RANGE (8*1024*1024)

int kstack_depth_to_print = 24;

static void printbinary(unsigned long x, int nbits)
{
	unsigned long mask = 1UL << (nbits - 1);
	while (mask != 0) {
		printk(mask & x ? "1" : "0");
		mask >>= 1;
	}
}

void show_regs(struct pt_regs *regs)
{
	int i;
#ifdef __LP64__
#define RFMT " %016lx"
#else
#define RFMT " %08lx"
#endif

	printk("\n"); /* don't want to have that pretty register dump messed up */

	printk("     YZrvWESTHLNXBCVMcbcbcbcbOGFRQPDI\nPSW: ");
	printbinary(regs->gr[0], 32);
	printk("\n");

	for (i = 0; i < 32; i += 4) {
		int j;
		printk("r%d-%d\t", i, i + 3);
		for (j = 0; j < 4; j++) {
			printk(RFMT, i + j == 0 ? 0 : regs->gr[i + j]);
		}
		printk("\n");
	}

	for (i = 0; i < 8; i += 4) {
		int j;
		printk("sr%d-%d\t", i, i + 4);
		for (j = 0; j < 4; j++) {
			printk(RFMT, regs->sr[i + j]);
		}
		printk("\n");
	}

#if REDICULOUSLY_VERBOSE
	for (i = 0; i < 32; i++) {
		printk("FR%2d : %016lx  ", i, regs->fr[i]);
		if ((i & 1) == 1)
			printk("\n");
	}
#endif

	printk("\nIASQ:" RFMT RFMT " IAOQ:" RFMT RFMT "\n",
	       regs->iasq[0], regs->iasq[1], regs->iaoq[0], regs->iaoq[1]);
	printk(" IIR: %08lx    ISR:" RFMT "  IOR:" RFMT "\nORIG_R28:" RFMT
	       "\n", regs->iir, regs->isr, regs->ior, regs->orig_r28);
}

void
die_if_kernel (char *str, struct pt_regs *regs, long err)
{
	if (user_mode(regs)) {
#if 1
		if (err == 0)
			return; /* STFU */

		/* XXX for debugging only */
		printk ("!!die_if_kernel: %s(%d): %s %ld\n",
			current->comm, current->pid, str, err);
		show_regs(regs);
#endif
		return;
	}

	printk("%s[%d]: %s %ld\n", current->comm, current->pid, str, err);

	show_regs(regs);

	/* Wot's wrong wif bein' racy? */
	if (current->thread.flags & PARISC_KERNEL_DEATH) {
		printk("die_if_kernel recursion detected.\n");
		sti();
		while (1);
	}
	current->thread.flags |= PARISC_KERNEL_DEATH;
	do_exit(SIGSEGV);
}

asmlinkage void cache_flush_denied(struct pt_regs * regs, long error_code)
{
}

asmlinkage void do_general_protection(struct pt_regs * regs, long error_code)
{
}

#ifndef CONFIG_MATH_EMULATION

asmlinkage void math_emulate(long arg)
{
}

#endif /* CONFIG_MATH_EMULATION */

int syscall_ipi(int (*syscall) (struct pt_regs *), struct pt_regs *regs)
{
	return syscall(regs);
}

struct {
	int retval;

	int (*func) (void *, struct pt_regs *);
	void * data;
} ipi_action[NR_CPUS];

void ipi_interrupt(int irq, void *unused, struct pt_regs *regs)
{
	int cpu = smp_processor_id();

	if(!ipi_action[cpu].func)
		BUG();

	ipi_action[cpu].retval =
		ipi_action[cpu].func(ipi_action[cpu].data, regs);
}

/* gdb uses break 4,8 */
#define GDB_BREAK_INSN 0x10004
void handle_gdb_break(struct pt_regs *regs, int wot)
{
	struct siginfo si;

	si.si_code = wot;
	si.si_addr = (void *) (regs->iaoq[0] & ~3);
	si.si_signo = SIGTRAP;
	si.si_errno = 0;
	force_sig_info(SIGTRAP, &si, current);
}

void handle_break(unsigned iir, struct pt_regs *regs)
{
	struct siginfo si;
#ifdef CONFIG_KWDB
	struct save_state ssp;
#endif /* CONFIG_KWDB */   

	flush_all_caches();
	switch(iir) {
	case 0x00:
		/* show registers, halt */
		cli();
		printk("break 0,0: pid=%d command='%s'\n",
		       current->pid, current->comm);
		die_if_kernel("Breakpoint", regs, 0);
		show_regs(regs);
		si.si_code = TRAP_BRKPT;
		si.si_addr = (void *) (regs->iaoq[0] & ~3);
		si.si_signo = SIGTRAP;
		force_sig_info(SIGTRAP, &si, current);
		break;

	case GDB_BREAK_INSN:
		die_if_kernel("Breakpoint", regs, 0);
		handle_gdb_break(regs, TRAP_BRKPT);
		break;

#ifdef CONFIG_KWDB

	case KGDB_BREAK_INSN:
		mtctl(0, 15);
		pt_regs_to_ssp(regs, &ssp);
		kgdb_trap(I_BRK_INST, &ssp, 1);
		ssp_to_pt_regs(&ssp, regs);
		break;

	case KGDB_INIT_BREAK_INSN:
		mtctl(0, 15);
		pt_regs_to_ssp(regs, &ssp);
		kgdb_trap(I_BRK_INST, &ssp, 1);
		ssp_to_pt_regs(&ssp, regs);

		/* Advance pcoq to skip break */
		regs->iaoq[0] = regs->iaoq[1];
		regs->iaoq[1] += 4;
		break;

#endif /* CONFIG_KWDB */

	default:
		set_eiem(0);
		printk("break %#08x: pid=%d command='%s'\n",
		       iir, current->pid, current->comm);
		show_regs(regs);
		si.si_signo = SIGTRAP;
		si.si_code = TRAP_BRKPT;
		si.si_addr = (void *) (regs->iaoq[0] & ~3);
		force_sig_info(SIGTRAP, &si, current);
		return;
	}
}

/* Format of the floating-point exception registers. */
struct exc_reg {
	unsigned int exception : 6;
	unsigned int ei : 26;
};

/* Macros for grabbing bits of the instruction format from the 'ei'
   field above. */
/* Major opcode 0c and 0e */
#define FP0CE_UID(i) (((i) >> 6) & 3)
#define FP0CE_CLASS(i) (((i) >> 9) & 3)
#define FP0CE_SUBOP(i) (((i) >> 13) & 7)
#define FP0CE_SUBOP1(i) (((i) >> 15) & 7) /* Class 1 subopcode */
#define FP0C_FORMAT(i) (((i) >> 11) & 3)
#define FP0E_FORMAT(i) (((i) >> 11) & 1)

/* Major opcode 0c, uid 2 (performance monitoring) */
#define FPPM_SUBOP(i) (((i) >> 9) & 0x1f)

/* Major opcode 2e (fused operations).   */
#define FP2E_SUBOP(i)  (((i) >> 5) & 1)
#define FP2E_FORMAT(i) (((i) >> 11) & 1)

/* Major opcode 26 (FMPYSUB) */
/* Major opcode 06 (FMPYADD) */
#define FPx6_FORMAT(i) ((i) & 0x1f)

/* Flags and enable bits of the status word. */
#define FPSW_FLAGS(w) ((w) >> 27)
#define FPSW_ENABLE(w) ((w) & 0x1f)
#define FPSW_V (1<<4)
#define FPSW_Z (1<<3)
#define FPSW_O (1<<2)
#define FPSW_U (1<<1)
#define FPSW_I (1<<0)

/* Emulate a floating point instruction if necessary and possible
   (this will be moved elsewhere eventually).  Return zero if
   successful or if emulation was not required, -1 if the instruction
   is actually illegal or unimplemented.  The status word passed as
   the first parameter will be modified to signal exceptions, if
   any. */

/* FIXME!!!  This is really incomplete and, at the moment, most
   illegal FP instructions will simply act as no-ops.  Obviously that
   is *not* what we want.  Also we don't even try to handle exception
   types other than the 'unimplemented' ones. */
int
fp_emul_insn(u32 *sw, struct exc_reg exc, struct pt_regs *regs)
{
	switch (exc.exception) {
	case 0x3:  /* Unimplemented, opcode 06 */
		break;
	case 0x9:  /* Unimplemented, opcode 0c */
		/* We do not support quadword operations, end of
                   story.  There's no support for them in GCC. */
		if (FP0C_FORMAT(exc.ei) == 3)
			return -1; /* SIGILL */
		/* Fall through. */
	case 0xa:  /* Unimplemented, opcode 0e */
		if (FP0CE_CLASS(exc.ei) == 1) {
			/* FCNV instructions of various sorts. */
		} else {
			if (FP0CE_CLASS(exc.ei == 0)
			    && FP0CE_SUBOP(exc.ei == 5)) {
				/* FRND instructions should be
                                   emulated, at some point, I
                                   guess. */
				return -1; /* SIGILL */
			}
		}
		break;
	case 0x23: /* Unimplemented, opcode 26 */
		break;
	case 0x2b: /* Unimplemented, opcode 2e */
		break;
	case 0x1:  /* Unimplemented, opcode 0e/0c */
		/* FIXME: How the hell are we supposed to tell which
                   opcode it is? */
		break;
	default:
		return -1; /* Punt */
	}

	return 0;
}

/* Handle a floating point exception.  Return zero if the faulting
   instruction can be completed successfully. */
int
handle_fpe(struct pt_regs *regs)
{
	struct siginfo si;
	union {
		struct fpsw {
			/* flag bits */
			unsigned int fv : 1;
			unsigned int fz : 1;
			unsigned int fo : 1;
			unsigned int fu : 1;
			unsigned int fi : 1;

			unsigned int c : 1;
			unsigned int pad1 : 4;
			unsigned int cq : 11;
			unsigned int rm : 2;
			unsigned int pad2 : 2;
			unsigned int t : 1;
			unsigned int d : 1;

			/* enable bits */
			unsigned int ev : 1;
			unsigned int ez : 1;
			unsigned int eo : 1;
			unsigned int eu : 1;
			unsigned int ei : 1;
		} status;
		u32 word;
	} sw;
	struct exc_reg excepts[7];
	unsigned int code = 0;
	unsigned int throw;

	/* Status word = FR0L. */
	memcpy(&sw, regs->fr, sizeof(sw));
	/* Exception words = FR0R-FR3R. */
	memcpy(excepts, ((char *) regs->fr) + 4, sizeof(excepts));

	/* This is all CPU dependent.  Since there is no public
           documentation on the PA2.0 processors we will just assume
           everything is like the 7100/7100LC/7300LC for now.

	   Specifically: All exceptions are marked as "unimplemented"
	   in the exception word, and the only exception word used is
	   excepts[1]. */

	/* Try to emulate the instruction.  Also determine if it is
           really an illegal instruction in the process.

	   FIXME: fp_emul_insn() only checks for the "unimplemented"
	   exceptions at the moment.  So this may break horribly on
	   PA2.0, where we may want to also check to see if we should
	   just send SIGFPE (or maybe not, let's see the documentation
	   first...) */
	if (fp_emul_insn(&sw.word, excepts[1], regs) == -1)
		goto send_sigill;

	/* Take the intersection of the flag bits in the FPSW and the
           enable bits in the FPSW. */
	throw = FPSW_FLAGS(sw.word) & FPSW_ENABLE(sw.word);

	/* Concoct an appropriate si_code.  Of course we don't know
           what to do if multiple exceptions were enabled and multiple
           flags were set.  Maybe that's why HP/UX doesn't implement
           feenableexcept(). */

	if (throw == 0)
		goto success; /* Duh. */
	else if (throw & FPSW_V)
		code = FPE_FLTINV;
	else if (throw & FPSW_Z)
		code = FPE_FLTDIV;
	else if (throw & FPSW_O)
		code = FPE_FLTOVF;
	else if (throw & FPSW_U)
		code = FPE_FLTUND;
	else if (throw & FPSW_I)
		code = FPE_FLTRES;

#if 1 /* Debugging... */
	printk("Unemulated floating point exception, pid=%d (%s)\n",
	       current->pid, current->comm);
	show_regs(regs);
	{
		int i;
		printk("FP Status: %08x\n", sw.word);
		printk("FP Exceptions:\n");
		for (i = 0; i < 7; i++) {
			printk("\tExcept%d: exception %03x insn %06x\n",
			       i, excepts[i].exception, excepts[i].ei);
		}
	}
#endif

	/* FIXME: Should we clear the flag bits, T bit, and exception
           registers here? */

	si.si_signo = SIGFPE;
	si.si_errno = 0;
	si.si_code = code;
	si.si_addr = (void *) regs->iaoq[0];
	force_sig_info(SIGFPE, &si, current);
	return -1;

 send_sigill:
	si.si_signo = SIGILL;
	si.si_errno = 0;
	si.si_code = ILL_COPROC;
	si.si_addr = (void *) regs->iaoq[0];
	force_sig_info(SIGILL, &si, current);
	return -1;

 success:
	/* We absolutely have to clear the T bit and exception
           registers to allow the process to recover.  Otherwise every
           subsequent floating point instruction will trap. */
	sw.status.t = 0;
	memset(excepts, 0, sizeof(excepts));

	memcpy(regs->fr, &sw, sizeof(sw));
	memcpy(((char *) regs->fr) + 4,excepts , sizeof(excepts));
	return 0;
}

int handle_toc(void)
{
	return 0;
}

void default_trap(int code, struct pt_regs *regs)
{
	printk("Trap %d on CPU %d\n", code, smp_processor_id());

	show_regs(regs);
}

void (*cpu_lpmc) (int code, struct pt_regs *regs) = default_trap;


#ifdef CONFIG_KWDB
int
debug_call (void) {
    printk ("Debug call.\n");
    return 0;
}

int
debug_call_leaf (void) {
    return 0;
}
#endif /* CONFIG_KWDB */

extern void do_page_fault(struct pt_regs *, int, unsigned long);
extern void parisc_terminate(char *, struct pt_regs *, int, unsigned long);
extern void transfer_pim_to_trap_frame(struct pt_regs *);
extern void pdc_console_restart(void);

void handle_interruption(int code, struct pt_regs *regs)
{
	unsigned long fault_address = 0;
	unsigned long fault_space = 0;
	struct siginfo si;
#ifdef CONFIG_KWDB
	struct save_state ssp;
#endif /* CONFIG_KWDB */   

	if (code == 1)
	    pdc_console_restart();  /* switch back to pdc if HPMC */
	else
	    sti();

#ifdef __LP64__

	/*
	 * FIXME:
	 * For 32 bit processes we don't want the b bits (bits 0 & 1)
	 * in the ior. This is more appropriately handled in the tlb
	 * miss handlers. Changes need to be made to support addresses
	 * >32 bits for 64 bit processes.
	 */

	regs->ior &= 0x3FFFFFFFFFFFFFFFUL;
#endif

#if 0
	printk("interrupted with code %d, regs %p\n", code, regs);
	show_regs(regs);
#endif

	switch(code) {
	case  1:
		parisc_terminate("High Priority Machine Check (HPMC)",regs,code,0);
		/* NOT REACHED */
	case  3: /* Recovery counter trap */
		regs->gr[0] &= ~PSW_R;
		if (regs->iasq[0])
			handle_gdb_break(regs, TRAP_TRACE);
		/* else this must be the start of a syscall - just let it
		 * run.
		 */
		return;

	case  5:
		flush_all_caches();
		cpu_lpmc(5, regs);
		return;

	case  6:
		fault_address = regs->iaoq[0];
		fault_space   = regs->iasq[0];
		break;

	case  9: /* Break Instruction */
		handle_break(regs->iir,regs);
		return;

	case 14:
		/* Assist Exception Trap, i.e. floating point exception. */
		die_if_kernel("Floating point exception", regs, 0); /* quiet */
		handle_fpe(regs);
		return;
	case 15:
	case 16:  /* Non-Access TLB miss faulting address is in IOR */
	case 17:
	case 26:
		fault_address = regs->ior;
		fault_space   = regs->isr;

		if (code == 26 && fault_space == 0)
		    parisc_terminate("Data access rights fault in kernel",regs,code,fault_address);
		break;

	case 19:
		regs->gr[0] |= PSW_X; /* So we can single-step over the trap */
		/* fall thru */
	case 21:
		handle_gdb_break(regs, TRAP_HWBKPT);
		return;

	case 25: /* Taken branch trap */
		regs->gr[0] &= ~PSW_T;
		if (regs->iasq[0])
			handle_gdb_break(regs, TRAP_BRANCH);
		/* else this must be the start of a syscall - just let it
		 * run.
		 */
		return;

#if 0 /* def CONFIG_KWDB */
	case I_TAKEN_BR:	/* 25 */
		mtctl(0, 15);
		pt_regs_to_ssp(regs, &ssp);
		kgdb_trap(I_TAKEN_BR, &ssp, 1);
		ssp_to_pt_regs(&ssp, regs);
		break;
#endif /* CONFIG_KWDB */

	case  8:
		die_if_kernel("Illegal instruction", regs, code);
		si.si_code = ILL_ILLOPC;
		goto give_sigill;

	case 10:
		die_if_kernel("Priviledged operation - shouldn't happen!", regs, code);
		si.si_code = ILL_PRVOPC;
		goto give_sigill;
	case 11:
		die_if_kernel("Priviledged register - shouldn't happen!", regs, code);
		si.si_code = ILL_PRVREG;
	give_sigill:
		si.si_signo = SIGILL;
		si.si_errno = 0;
		si.si_addr = (void *) regs->iaoq[0];
		force_sig_info(SIGILL, &si, current);
		return;

	case 28:  /* Unaligned just causes SIGBUS for now */
		die_if_kernel("Unaligned data reference", regs, code);
		si.si_code = BUS_ADRALN;
		si.si_signo = SIGBUS;
		si.si_errno = 0;
		si.si_addr = (void *) regs->ior;
		force_sig_info(SIGBUS, &si, current);
		return;

	default:
		if (user_mode(regs)) {
			printk("\nhandle_interruption() pid=%d command='%s'\n",
			    current->pid, current->comm);
			show_regs(regs);
			/* SIGBUS, for lack of a better one. */
			si.si_signo = SIGBUS;
			si.si_code = BUS_OBJERR;
			si.si_errno = 0;
			si.si_addr = (void *) regs->ior;
			force_sig_info(SIGBUS, &si, current);
			return;
		}
		parisc_terminate("Unexpected Interruption!",regs,code,0);
		/* NOT REACHED */
	}

	if (user_mode(regs)) {
	    if (fault_space != regs->sr[7]) {
		if (fault_space == 0)
			printk("User Fault on Kernel Space ");
		else /* this case should never happen, but whatever... */
			printk("User Fault (long pointer) ");
		printk("pid=%d command='%s'\n", current->pid, current->comm);
		show_regs(regs);
		si.si_signo = SIGSEGV;
		si.si_errno = 0;
		si.si_code = SEGV_MAPERR;
		si.si_addr = (void *) regs->ior;
		force_sig_info(SIGSEGV, &si, current);
		return;
	    }
	}
	else {

	    /*
	     * The kernel should never fault on its own address space.
	     */

	    if (fault_space == 0)
		    parisc_terminate("Kernel Fault",regs,code,fault_address);
	}

#ifdef CONFIG_KWDB
	debug_call_leaf ();
#endif /* CONFIG_KWDB */

	do_page_fault(regs, code, fault_address);

	/*
	 * This should not be necessary.
	 * However, we do not currently
	 * implement flush_page_to_ram.
	 *
	 * The problem is that if we just
	 * brought in some code through the
	 * D-cache, the I-cache may not see
	 * it since it hasn't been flushed
	 * to ram.
	 */

/* 	flush_all_caches(); */

#if 0
	printk("returning %p\n", regs);
/*	show_regs(regs); */
#endif

	return;

}

void show_stack(unsigned long sp)
{
#if 1
	if ((sp & 0xc0000000UL) == 0xc0000000UL) {

	    __u32 *stackptr;
	    __u32 *dumpptr;

	    /* Stack Dump! */

	    stackptr = (__u32 *)sp;
	    dumpptr  = (__u32 *)(sp & ~(INIT_TASK_SIZE - 1));
	    printk("\nDumping Stack from %p to %p:\n",dumpptr,stackptr);
	    while (dumpptr < stackptr) {
		printk("%04x %08x %08x %08x %08x %08x %08x %08x %08x\n",
		    ((__u32)dumpptr) & 0xffff,
		    dumpptr[0], dumpptr[1], dumpptr[2], dumpptr[3],
		    dumpptr[4], dumpptr[5], dumpptr[6], dumpptr[7]);
		dumpptr += 8;
	    }
	}
#endif
}


void parisc_terminate(char *msg, struct pt_regs *regs, int code, unsigned long offset)
{
	set_eiem(0);
	cli();

	if (code == 1)
	    transfer_pim_to_trap_frame(regs);

#if 1
	show_stack(regs->gr[30]);
#endif

	printk("\n%s: Code=%d regs=%p (Addr=%08lx)\n",msg,code,regs,offset);
	show_regs(regs);

	for(;;)
	    ;
}

void transfer_pim_to_trap_frame(struct pt_regs *regs)
{
    register int i;
    extern unsigned int hpmc_pim_data[];
    struct pdc_hpmc_pim_11 *pim_narrow;
    struct pdc_hpmc_pim_20 *pim_wide;

    if (boot_cpu_data.cpu_type >= pcxu) {

	pim_wide = (struct pdc_hpmc_pim_20 *)hpmc_pim_data;

	/*
	 * Note: The following code will probably generate a
	 * bunch of truncation error warnings from the compiler.
	 * Could be handled with an ifdef, but perhaps there
	 * is a better way.
	 */

	regs->gr[0] = pim_wide->cr[22];

	for (i = 1; i < 32; i++)
	    regs->gr[i] = pim_wide->gr[i];

	for (i = 0; i < 32; i++)
	    regs->fr[i] = pim_wide->fr[i];

	for (i = 0; i < 8; i++)
	    regs->sr[i] = pim_wide->sr[i];

	regs->iasq[0] = pim_wide->cr[17];
	regs->iasq[1] = pim_wide->iasq_back;
	regs->iaoq[0] = pim_wide->cr[18];
	regs->iaoq[1] = pim_wide->iaoq_back;

	regs->cr30 = pim_wide->cr[30];
	regs->sar  = pim_wide->cr[11];
	regs->iir  = pim_wide->cr[19];
	regs->isr  = pim_wide->cr[20];
	regs->ior  = pim_wide->cr[21];
    }
    else {
	pim_narrow = (struct pdc_hpmc_pim_11 *)hpmc_pim_data;

	regs->gr[0] = pim_narrow->cr[22];

	for (i = 1; i < 32; i++)
	    regs->gr[i] = pim_narrow->gr[i];

	for (i = 0; i < 32; i++)
	    regs->fr[i] = pim_narrow->fr[i];

	for (i = 0; i < 8; i++)
	    regs->sr[i] = pim_narrow->sr[i];

	regs->iasq[0] = pim_narrow->cr[17];
	regs->iasq[1] = pim_narrow->iasq_back;
	regs->iaoq[0] = pim_narrow->cr[18];
	regs->iaoq[1] = pim_narrow->iaoq_back;

	regs->cr30 = pim_narrow->cr[30];
	regs->sar  = pim_narrow->cr[11];
	regs->iir  = pim_narrow->cr[19];
	regs->isr  = pim_narrow->cr[20];
	regs->ior  = pim_narrow->cr[21];
    }

    /*
     * The following fields only have meaning if we came through
     * another path. So just zero them here.
     */

    regs->ksp = 0;
    regs->kpc = 0;
    regs->orig_r28 = 0;
}

int __init check_ivt(void *iva)
{
	int i;
	u32 check = 0;
	u32 *ivap;
	u32 *hpmcp;
	u32 length;
	extern void os_hpmc(void);
	extern void os_hpmc_end(void);

	if(strcmp((char *)iva, "cows can fly"))
		return -1;

	ivap = (u32 *)iva;

	for (i = 0; i < 8; i++)
	    *ivap++ = 0;

	/* Compute Checksum for HPMC handler */

	length = (u32)((unsigned long)os_hpmc_end - (unsigned long)os_hpmc);
	ivap[7] = length;

	hpmcp = (u32 *)os_hpmc;

	for(i=0; i<length/4; i++)
	    check += *hpmcp++;

	for(i=0; i<8; i++)
	    check += ivap[i];

	ivap[5] = -check;

	return 0;
}
	
#ifndef __LP64__
extern const void fault_vector_11;
#endif
extern const void fault_vector_20;

void __init trap_init(void)
{
	volatile long eiem;
	void *iva;

	printk("trap_init\n");
	
	if (boot_cpu_data.cpu_type >= pcxu)
		iva = (void *) &fault_vector_20;
	else
#ifdef __LP64__
		panic("Can't boot 64-bit OS on PA1.1 processor!");
#else
		iva = (void *) &fault_vector_11;
#endif

	if(check_ivt(iva))
		panic("IVT invalid");

	mtctl(0, 30);
	mtctl(90000000, 16);
	set_eiem(-1L);
	mtctl(-1L, 23);
	asm volatile ("rsm 0,%0" : "=r" (eiem));
}
