/*
 * Architecture-specific trap handling.
 *
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * 05/12/00 grao <goutham.rao@intel.com> : added isr in siginfo for SIGFPE
 */

#define FPSWA_DEBUG	1

/*
 * The fpu_fault() handler needs to be able to access and update all
 * floating point registers.  Those saved in pt_regs can be accessed
 * through that structure, but those not saved, will be accessed
 * directly.  To make this work, we need to ensure that the compiler
 * does not end up using a preserved floating point register on its
 * own.  The following achieves this by declaring preserved registers
 * that are not marked as "fixed" as global register variables.
 */
register double f2 asm ("f2"); register double f3 asm ("f3");
register double f4 asm ("f4"); register double f5 asm ("f5");

register long f16 asm ("f16"); register long f17 asm ("f17");
register long f18 asm ("f18"); register long f19 asm ("f19");
register long f20 asm ("f20"); register long f21 asm ("f21");
register long f22 asm ("f22"); register long f23 asm ("f23");

register double f24 asm ("f24"); register double f25 asm ("f25");
register double f26 asm ("f26"); register double f27 asm ("f27");
register double f28 asm ("f28"); register double f29 asm ("f29");
register double f30 asm ("f30"); register double f31 asm ("f31");

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>

#include <asm/ia32.h>
#include <asm/processor.h>
#include <asm/uaccess.h>

#include <asm/fpswa.h>

static fpswa_interface_t *fpswa_interface;

void __init
trap_init (void)
{
	printk("fpswa interface at %lx\n", ia64_boot_param.fpswa);
	if (ia64_boot_param.fpswa) {
#define OLD_FIRMWARE
#ifdef OLD_FIRMWARE
		/*
		 * HACK to work around broken firmware.  This code
		 * applies the label fixup to the FPSWA interface and
		 * works both with old and new (fixed) firmware.
		 */
		unsigned long addr = (unsigned long) __va(ia64_boot_param.fpswa);
		unsigned long gp_val = *(unsigned long *)(addr + 8);

		/* go indirect and indexed to get table address */
		addr = gp_val;
		gp_val = *(unsigned long *)(addr + 8);

		while (gp_val == *(unsigned long *)(addr + 8)) {
			*(unsigned long *)addr |= PAGE_OFFSET;
			*(unsigned long *)(addr + 8) |= PAGE_OFFSET;
			addr += 16;
		}
#endif
		/* FPSWA fixup: make the interface pointer a kernel virtual address: */
		fpswa_interface = __va(ia64_boot_param.fpswa);
	}
}

void
die_if_kernel (char *str, struct pt_regs *regs, long err)
{
	if (user_mode(regs)) {
#if 0
		/* XXX for debugging only */
		printk ("!!die_if_kernel: %s(%d): %s %ld\n",
			current->comm, current->pid, str, err);
		show_regs(regs);
#endif
		return;
	}

	printk("%s[%d]: %s %ld\n", current->comm, current->pid, str, err);

	show_regs(regs);

	if (current->thread.flags & IA64_KERNEL_DEATH) {
		printk("die_if_kernel recursion detected.\n");
		sti();
		while (1);
	}
	current->thread.flags |= IA64_KERNEL_DEATH;
	do_exit(SIGSEGV);
}

void
ia64_bad_break (unsigned long break_num, struct pt_regs *regs)
{
	siginfo_t siginfo;
	int sig, code;

	/* SIGILL, SIGFPE, SIGSEGV, and SIGBUS want these field initialized: */
	siginfo.si_addr = (void *) (regs->cr_iip + ia64_psr(regs)->ri);
	siginfo.si_imm = break_num;

	switch (break_num) {
	      case 0: /* unknown error */
		sig = SIGILL; code = ILL_ILLOPC;
		break;

	      case 1: /* integer divide by zero */
		sig = SIGFPE; code = FPE_INTDIV;
		break;

	      case 2: /* integer overflow */
		sig = SIGFPE; code = FPE_INTOVF;
		break;

	      case 3: /* range check/bounds check */
		sig = SIGFPE; code = FPE_FLTSUB;
		break;

	      case 4: /* null pointer dereference */
		sig = SIGSEGV; code = SEGV_MAPERR;
		break;

	      case 5: /* misaligned data */
		sig = SIGSEGV; code = BUS_ADRALN;
		break;

	      case 6: /* decimal overflow */
		sig = SIGFPE; code = __FPE_DECOVF;
		break;

	      case 7: /* decimal divide by zero */
		sig = SIGFPE; code = __FPE_DECDIV;
		break;

	      case 8: /* packed decimal error */
		sig = SIGFPE; code = __FPE_DECERR;
		break;

	      case 9: /* invalid ASCII digit */
		sig = SIGFPE; code = __FPE_INVASC;
		break;

	      case 10: /* invalid decimal digit */
		sig = SIGFPE; code = __FPE_INVDEC;
		break;

	      case 11: /* paragraph stack overflow */
		sig = SIGSEGV; code = __SEGV_PSTKOVF;
		break;

	      default:
		if (break_num < 0x40000 || break_num > 0x100000)
			die_if_kernel("Bad break", regs, break_num);

		if (break_num < 0x80000) {
			sig = SIGILL; code = __ILL_BREAK;
		} else {
			sig = SIGTRAP; code = TRAP_BRKPT;
		}
	}
	siginfo.si_signo = sig;
	siginfo.si_errno = 0;
	siginfo.si_code = code;
	force_sig_info(sig, &siginfo, current);
}

/*
 * Unimplemented system calls.  This is called only for stuff that
 * we're supposed to implement but haven't done so yet.  Everything
 * else goes to sys_ni_syscall.
 */
asmlinkage long
ia64_ni_syscall (unsigned long arg0, unsigned long arg1, unsigned long arg2, unsigned long arg3,
		 unsigned long arg4, unsigned long arg5, unsigned long arg6, unsigned long arg7,
		 unsigned long stack)
{
	struct pt_regs *regs = (struct pt_regs *) &stack;

	printk("<sc%ld(%lx,%lx,%lx,%lx)>\n", regs->r15, arg0, arg1, arg2, arg3);
	return -ENOSYS;
}

/*
 * disabled_fph_fault() is called when a user-level process attempts
 * to access one of the registers f32..f127 when it doesn't own the
 * fp-high register partition.  When this happens, we save the current
 * fph partition in the task_struct of the fpu-owner (if necessary)
 * and then load the fp-high partition of the current task (if
 * necessary).  Note that the kernel has access to fph by the time we
 * get here, as the IVT's "Diabled FP-Register" handler takes care of
 * clearing psr.dfh.
 */
static inline void
disabled_fph_fault (struct pt_regs *regs)
{
	struct ia64_psr *psr = ia64_psr(regs);

	/* first, grant user-level access to fph partition: */
	psr->dfh = 0;
#ifndef CONFIG_SMP
	{
		struct task_struct *fpu_owner = ia64_get_fpu_owner();

		if (fpu_owner == current)
			return;

		if (fpu_owner)
			ia64_flush_fph(fpu_owner);

		ia64_set_fpu_owner(current);
	}
#endif /* !CONFIG_SMP */
	if ((current->thread.flags & IA64_THREAD_FPH_VALID) != 0) {
		__ia64_load_fpu(current->thread.fph);
		psr->mfh = 0;
	} else {
		__ia64_init_fpu();
		/*
		 * Set mfh because the state in thread.fph does not match the state in
		 * the fph partition.
		 */
		psr->mfh = 1;
	}
}

static inline int
fp_emulate (int fp_fault, void *bundle, long *ipsr, long *fpsr, long *isr, long *pr, long *ifs,
	    struct pt_regs *regs)
{
	fp_state_t fp_state;
	fpswa_ret_t ret;
#ifdef FPSWA_BUG
	struct ia64_fpreg f6_15[10];
#endif

	if (!fpswa_interface)
		return -1;

	memset(&fp_state, 0, sizeof(fp_state_t));

	/*
	 * compute fp_state.  only FP registers f6 - f11 are used by the 
	 * kernel, so set those bits in the mask and set the low volatile
	 * pointer to point to these registers.
	 */
#ifndef FPSWA_BUG
	fp_state.bitmask_low64 = 0x3c0;  /* bit 6..9 */
	fp_state.fp_state_low_volatile = (fp_state_low_volatile_t *) &regs->f6;
#else
	fp_state.bitmask_low64 = 0xffc0;  /* bit6..bit15 */
	f6_15[0] = regs->f6;
	f6_15[1] = regs->f7;
	f6_15[2] = regs->f8;
	f6_15[3] = regs->f9;
 	__asm__ ("stf.spill %0=f10%P0" : "=m"(f6_15[4]));
 	__asm__ ("stf.spill %0=f11%P0" : "=m"(f6_15[5]));
 	__asm__ ("stf.spill %0=f12%P0" : "=m"(f6_15[6]));
 	__asm__ ("stf.spill %0=f13%P0" : "=m"(f6_15[7]));
 	__asm__ ("stf.spill %0=f14%P0" : "=m"(f6_15[8]));
 	__asm__ ("stf.spill %0=f15%P0" : "=m"(f6_15[9]));
	fp_state.fp_state_low_volatile = (fp_state_low_volatile_t *) f6_15;
#endif
        /*
	 * unsigned long (*EFI_FPSWA) (
	 *      unsigned long    trap_type,
	 *	void             *Bundle,
	 *	unsigned long    *pipsr,
	 *	unsigned long    *pfsr,
	 *	unsigned long    *pisr,
	 *	unsigned long    *ppreds,
	 *	unsigned long    *pifs,
	 *	void             *fp_state);
	 */
	ret = (*fpswa_interface->fpswa)((unsigned long) fp_fault, bundle,
					(unsigned long *) ipsr, (unsigned long *) fpsr,
					(unsigned long *) isr, (unsigned long *) pr,
					(unsigned long *) ifs, &fp_state);
#ifdef FPSWA_BUG
 	__asm__ ("ldf.fill f10=%0%P0" :: "m"(f6_15[4]));
 	__asm__ ("ldf.fill f11=%0%P0" :: "m"(f6_15[5]));
 	__asm__ ("ldf.fill f12=%0%P0" :: "m"(f6_15[6]));
 	__asm__ ("ldf.fill f13=%0%P0" :: "m"(f6_15[7]));
 	__asm__ ("ldf.fill f14=%0%P0" :: "m"(f6_15[8]));
 	__asm__ ("ldf.fill f15=%0%P0" :: "m"(f6_15[9]));
	regs->f6 = f6_15[0];
	regs->f7 = f6_15[1];
	regs->f8 = f6_15[2];
	regs->f9 = f6_15[3];
#endif
	return ret.status;
}

/*
 * Handle floating-point assist faults and traps.
 */
static int
handle_fpu_swa (int fp_fault, struct pt_regs *regs, unsigned long isr)
{
	long exception, bundle[2];
	unsigned long fault_ip;
	struct siginfo siginfo;
	static int fpu_swa_count = 0;
	static unsigned long last_time;

	fault_ip = regs->cr_iip;
	if (!fp_fault && (ia64_psr(regs)->ri == 0))
		fault_ip -= 16;
	if (copy_from_user(bundle, (void *) fault_ip, sizeof(bundle)))
		return -1;

#ifdef FPSWA_DEBUG
	if (fpu_swa_count > 5 && jiffies - last_time > 5*HZ)
		fpu_swa_count = 0;
	if (++fpu_swa_count < 5) {
		last_time = jiffies;
		printk("%s(%d): floating-point assist fault at ip %016lx\n",
		       current->comm, current->pid, regs->cr_iip + ia64_psr(regs)->ri);
	}
#endif
	exception = fp_emulate(fp_fault, bundle, &regs->cr_ipsr, &regs->ar_fpsr, &isr, &regs->pr,
 			       &regs->cr_ifs, regs);
	if (fp_fault) {
		if (exception == 0) {
			/* emulation was successful */
 			ia64_increment_ip(regs);
		} else if (exception == -1) {
			printk("handle_fpu_swa: fp_emulate() returned -1\n");
			return -1;
		} else {
			/* is next instruction a trap? */
			if (exception & 2) {
				ia64_increment_ip(regs);
			}
			siginfo.si_signo = SIGFPE;
			siginfo.si_errno = 0;
			siginfo.si_code = 0;
			siginfo.si_addr = (void *) (regs->cr_iip + ia64_psr(regs)->ri);
			if (isr & 0x11) {
				siginfo.si_code = FPE_FLTINV;
			} else if (isr & 0x44) {
				siginfo.si_code = FPE_FLTDIV;
			}
			siginfo.si_isr = isr;
			force_sig_info(SIGFPE, &siginfo, current);
		}
	} else {
		if (exception == -1) {
			printk("handle_fpu_swa: fp_emulate() returned -1\n");
			return -1;
		} else if (exception != 0) {
			/* raise exception */
			siginfo.si_signo = SIGFPE;
			siginfo.si_errno = 0;
			siginfo.si_code = 0;
			siginfo.si_addr = (void *) (regs->cr_iip + ia64_psr(regs)->ri);
			if (isr & 0x880) {
				siginfo.si_code = FPE_FLTOVF;
			} else if (isr & 0x1100) {
				siginfo.si_code = FPE_FLTUND;
			} else if (isr & 0x2200) {
				siginfo.si_code = FPE_FLTRES;
			}
			siginfo.si_isr = isr;
			force_sig_info(SIGFPE, &siginfo, current);
		}
	}
	return 0;
}

struct illegal_op_return {
	unsigned long fkt, arg1, arg2, arg3;
};

struct illegal_op_return
ia64_illegal_op_fault (unsigned long ec, unsigned long arg1, unsigned long arg2,
		       unsigned long arg3, unsigned long arg4, unsigned long arg5,
		       unsigned long arg6, unsigned long arg7, unsigned long stack)
{
	struct pt_regs *regs = (struct pt_regs *) &stack;
	struct illegal_op_return rv;
	struct siginfo si;
	char buf[128];

#ifdef CONFIG_IA64_BRL_EMU	
	{
		extern struct illegal_op_return ia64_emulate_brl (struct pt_regs *, unsigned long);

		rv = ia64_emulate_brl(regs, ec);
		if (rv.fkt != (unsigned long) -1)
			return rv;
	}
#endif

	sprintf(buf, "IA-64 Illegal operation fault");
	die_if_kernel(buf, regs, 0);

	memset(&si, 0, sizeof(si));
	si.si_signo = SIGILL;
	si.si_code = ILL_ILLOPC;
	si.si_addr = (void *) (regs->cr_iip + ia64_psr(regs)->ri);
	force_sig_info(SIGILL, &si, current);
	rv.fkt = 0;
	return rv;
}

void
ia64_fault (unsigned long vector, unsigned long isr, unsigned long ifa,
	    unsigned long iim, unsigned long itir, unsigned long arg5,
	    unsigned long arg6, unsigned long arg7, unsigned long stack)
{
	struct pt_regs *regs = (struct pt_regs *) &stack;
	unsigned long code, error = isr;
	struct siginfo siginfo;
	char buf[128];
	int result;
	static const char *reason[] = {
		"IA-64 Illegal Operation fault",
		"IA-64 Privileged Operation fault",
		"IA-64 Privileged Register fault",
		"IA-64 Reserved Register/Field fault",
		"Disabled Instruction Set Transition fault",
		"Unknown fault 5", "Unknown fault 6", "Unknown fault 7", "Illegal Hazard fault",
		"Unknown fault 9", "Unknown fault 10", "Unknown fault 11", "Unknown fault 12", 
		"Unknown fault 13", "Unknown fault 14", "Unknown fault 15"
	};

#if 0
	/* this is for minimal trust debugging; yeah this kind of stuff is useful at times... */

	if (vector != 25) {
		static unsigned long last_time;
		static char count;
		unsigned long n = vector;
		char buf[32], *cp;

		if (count > 5 && jiffies - last_time > 5*HZ)
			count = 0;

		if (count++ < 5) {
			last_time = jiffies;
			cp = buf + sizeof(buf);
			*--cp = '\0';
			while (n) {
				*--cp = "0123456789abcdef"[n & 0xf];
				n >>= 4;
			}
			printk("<0x%s>", cp);
		}
	}
#endif

	switch (vector) {
	      case 24: /* General Exception */
		code = (isr >> 4) & 0xf;
		sprintf(buf, "General Exception: %s%s", reason[code],
			(code == 3) ? ((isr & (1UL << 37))
				       ? " (RSE access)" : " (data access)") : "");
#ifndef CONFIG_ITANIUM_ASTEP_SPECIFIC
		if (code == 8) {
# ifdef CONFIG_IA64_PRINT_HAZARDS
			printk("%016lx:possible hazard, pr = %016lx\n", regs->cr_iip, regs->pr);
# endif
			return;
		}
#endif
		break;

	      case 25: /* Disabled FP-Register */
		if (isr & 2) {
			disabled_fph_fault(regs);
			return;
		}
		sprintf(buf, "Disabled FPL fault---not supposed to happen!");
		break;

	      case 26: /* NaT Consumption */
	      case 31: /* Unsupported Data Reference */
		if (user_mode(regs)) {
			siginfo.si_signo = SIGILL;
			siginfo.si_code = ILL_ILLOPN;
			siginfo.si_errno = 0;
			siginfo.si_addr = (void *) (regs->cr_iip + ia64_psr(regs)->ri);
			siginfo.si_imm = vector;
			force_sig_info(SIGILL, &siginfo, current);
			return;
		}
		sprintf(buf, (vector == 26) ? "NaT consumption" : "Unsupported data reference");
		break;

	      case 29: /* Debug */
	      case 35: /* Taken Branch Trap */
	      case 36: /* Single Step Trap */
		switch (vector) {
		      case 29: 
			siginfo.si_code = TRAP_HWBKPT;
#ifdef CONFIG_ITANIUM
			/*
			 * Erratum 10 (IFA may contain incorrect address) now has
			 * "NoFix" status.  There are no plans for fixing this.
			 */
			if (ia64_psr(regs)->is == 0)
			  ifa = regs->cr_iip;
#endif
			siginfo.si_addr = (void *) ifa;
		        break;
		      case 35: siginfo.si_code = TRAP_BRANCH; break;
		      case 36: siginfo.si_code = TRAP_TRACE; break;
		}
		siginfo.si_signo = SIGTRAP;
		siginfo.si_errno = 0;
		force_sig_info(SIGTRAP, &siginfo, current);
		return;

	      case 32: /* fp fault */
	      case 33: /* fp trap */
		result = handle_fpu_swa((vector == 32) ? 1 : 0, regs, isr);
		if (result < 0) {
			siginfo.si_signo = SIGFPE;
			siginfo.si_errno = 0;
			siginfo.si_code = FPE_FLTINV;
			siginfo.si_addr = (void *) (regs->cr_iip + ia64_psr(regs)->ri);
			force_sig(SIGFPE, current);
		}
		return;

	      case 34:		/* Unimplemented Instruction Address Trap */
		if (user_mode(regs)) {
			siginfo.si_signo = SIGILL;
			siginfo.si_code = ILL_BADIADDR;
			siginfo.si_errno = 0;
			siginfo.si_addr = (void *) (regs->cr_iip + ia64_psr(regs)->ri);
			force_sig_info(SIGILL, &siginfo, current);
			return;
		}
		sprintf(buf, "Unimplemented Instruction Address fault");
		break;

	      case 45:
#ifdef CONFIG_IA32_SUPPORT
		if (ia32_exception(regs, isr) == 0)
			return;
#endif
		printk("Unexpected IA-32 exception (Trap 45)\n");
		printk("  iip - 0x%lx, ifa - 0x%lx, isr - 0x%lx\n", regs->cr_iip, ifa, isr);
		force_sig(SIGSEGV, current);
		break;

	      case 46:
		printk("Unexpected IA-32 intercept trap (Trap 46)\n");
		printk("  iip - 0x%lx, ifa - 0x%lx, isr - 0x%lx, iim - 0x%lx\n",
		       regs->cr_iip, ifa, isr, iim);
		force_sig(SIGSEGV, current);
		return;

	      case 47:
		sprintf(buf, "IA-32 Interruption Fault (int 0x%lx)", isr >> 16);
		break;

	      default:
		sprintf(buf, "Fault %lu", vector);
		break;
	}
	die_if_kernel(buf, regs, error);
	force_sig(SIGILL, current);
}
