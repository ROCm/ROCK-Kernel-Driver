/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 1999, 2000, 01 Ralf Baechle
 * Modified for R3000 by Paul M. Antoine, 1995, 1996
 * Complete output from die() by Ulf Carlsson, 1998
 * Copyright (C) 1999 Silicon Graphics, Inc.
 *
 * Kevin D. Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000, 01 MIPS Technologies, Inc.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>

#include <asm/bootinfo.h>
#include <asm/branch.h>
#include <asm/cpu.h>
#include <asm/cachectl.h>
#include <asm/inst.h>
#include <asm/jazz.h>
#include <asm/module.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/siginfo.h>
#include <asm/watch.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/mmu_context.h>

/*
 * Machine specific interrupt handlers
 */
extern asmlinkage void acer_pica_61_handle_int(void);
extern asmlinkage void decstation_handle_int(void);
extern asmlinkage void deskstation_rpc44_handle_int(void);
extern asmlinkage void deskstation_tyne_handle_int(void);
extern asmlinkage void mips_magnum_4000_handle_int(void);

extern asmlinkage void handle_mod(void);
extern asmlinkage void handle_tlbl(void);
extern asmlinkage void handle_tlbs(void);
extern asmlinkage void handle_adel(void);
extern asmlinkage void handle_ades(void);
extern asmlinkage void handle_ibe(void);
extern asmlinkage void handle_dbe(void);
extern asmlinkage void handle_sys(void);
extern asmlinkage void handle_bp(void);
extern asmlinkage void handle_ri(void);
extern asmlinkage void handle_cpu(void);
extern asmlinkage void handle_ov(void);
extern asmlinkage void handle_tr(void);
extern asmlinkage void handle_fpe(void);
extern asmlinkage void handle_watch(void);
extern asmlinkage void handle_mcheck(void);
extern asmlinkage void handle_reserved(void);

extern int fpu_emulator_cop1Handler(struct pt_regs *);

char watch_available = 0;

void (*ibe_board_handler)(struct pt_regs *regs);
void (*dbe_board_handler)(struct pt_regs *regs);

int kstack_depth_to_print = 24;

/*
 * These constant is for searching for possible module text segments.
 * MODULE_RANGE is a guess of how much space is likely to be vmalloced.
 */
#define MODULE_RANGE (8*1024*1024)

#ifndef CONFIG_CPU_HAS_LLSC
/*
 * This stuff is needed for the userland ll-sc emulation for R2300
 */
void simulate_ll(struct pt_regs *regs, unsigned int opcode);
void simulate_sc(struct pt_regs *regs, unsigned int opcode);

#define OPCODE 0xfc000000
#define BASE   0x03e00000
#define RT     0x001f0000
#define OFFSET 0x0000ffff
#define LL     0xc0000000
#define SC     0xe0000000
#endif

/*
 * This routine abuses get_user()/put_user() to reference pointers
 * with at least a bit of error checking ...
 */
void show_stack(unsigned int *sp)
{
	int i;
	unsigned int *stack;

	stack = sp;
	i = 0;

	printk("Stack:");
	while ((unsigned long) stack & (PAGE_SIZE - 1)) {
		unsigned long stackdata;

		if (__get_user(stackdata, stack++)) {
			printk(" (Bad stack address)");
			break;
		}

		printk(" %08lx", stackdata);

		if (++i > 40) {
			printk(" ...");
			break;
		}

		if (i % 8 == 0)
			printk("\n      ");
	}
}

void show_trace(unsigned int *sp)
{
	int i;
	unsigned int *stack;
	unsigned long kernel_start, kernel_end;
	unsigned long module_start, module_end;
	extern char _stext, _etext;

	stack = sp;
	i = 0;

	kernel_start = (unsigned long) &_stext;
	kernel_end = (unsigned long) &_etext;
	module_start = VMALLOC_START;
	module_end = module_start + MODULE_RANGE;

	printk("\nCall Trace:");

	while ((unsigned long) stack & (PAGE_SIZE -1)) {
		unsigned long addr;

		if (__get_user(addr, stack++)) {
			printk(" (Bad stack address)\n");
			break;
		}

		/*
		 * If the address is either in the text segment of the
		 * kernel, or in the region which contains vmalloc'ed
		 * memory, it *may* be the address of a calling
		 * routine; if so, print it so that someone tracing
		 * down the cause of the crash will be able to figure
		 * out the call path that was taken.
		 */

		if ((addr >= kernel_start && addr < kernel_end) ||
		    (addr >= module_start && addr < module_end)) { 

			printk(" [<%08lx>]", addr);
			if (++i > 40) {
				printk(" ...");
				break;
			}
		}
	}
}

void show_code(unsigned int *pc)
{
	long i;

	printk("\nCode:");

	for(i = -3 ; i < 6 ; i++) {
		unsigned long insn;
		if (__get_user(insn, pc + i)) {
			printk(" (Bad address in epc)\n");
			break;
		}
		printk("%c%08lx%c",(i?' ':'<'),insn,(i?' ':'>'));
	}
}

spinlock_t die_lock;

extern void __die(const char * str, struct pt_regs * regs, const char *where,
                  unsigned long line)
{
	console_verbose();
	spin_lock_irq(&die_lock);
	printk("%s", str);
	if (where)
		printk(" in %s, line %ld", where, line);
	printk(":\n");
	show_regs(regs);
	printk("Process %s (pid: %d, stackpage=%08lx)\n",
		current->comm, current->pid, (unsigned long) current);
	show_stack((unsigned int *) regs->regs[29]);
	show_trace((unsigned int *) regs->regs[29]);
	show_code((unsigned int *) regs->cp0_epc);
	printk("\n");
	spin_unlock_irq(&die_lock);
	do_exit(SIGSEGV);
}

void __die_if_kernel(const char * str, struct pt_regs * regs, const char *where,
	unsigned long line)
{
	if (!user_mode(regs))
		__die(str, regs, where, line);
}

extern const struct exception_table_entry __start___dbe_table[];
extern const struct exception_table_entry __stop___dbe_table[];

void __declare_dbe_table(void)
{
	__asm__ __volatile__(
	".section\t__dbe_table,\"a\"\n\t"
	".previous"
	);
}

static inline unsigned long
search_one_table(const struct exception_table_entry *first,
		 const struct exception_table_entry *last,
		 unsigned long value)
{
	const struct exception_table_entry *mid;
	long diff;

	while (first < last) {
		mid = (last - first) / 2 + first;
		diff = mid->insn - value;
		if (diff < 0)
			first = mid + 1;
		else
			last = mid;
	}
	return (first == last && first->insn == value) ? first->nextinsn : 0;
}

extern spinlock_t modlist_lock;

static inline unsigned long
search_dbe_table(unsigned long addr)
{
	unsigned long ret = 0;

#ifndef CONFIG_MODULES
	/* There is only the kernel to search.  */
	ret = search_one_table(__start___dbe_table, __stop___dbe_table-1, addr);
	return ret;
#else
	unsigned long flags;

	/* The kernel is the last "module" -- no need to treat it special.  */
	struct module *mp;
	struct archdata *ap;

	spin_lock_irqsave(&modlist_lock, flags);
	for (mp = module_list; mp != NULL; mp = mp->next) {
		if (!mod_member_present(mp, archdata_end) ||
        	    !mod_archdata_member_present(mp, struct archdata,
						 dbe_table_end))
			continue;
		ap = (struct archdata *)(mp->archdata_start);

		if (ap->dbe_table_start == NULL ||
		    !(mp->flags & (MOD_RUNNING | MOD_INITIALIZING)))
			continue;
		ret = search_one_table(ap->dbe_table_start,
				       ap->dbe_table_end - 1, addr);
		if (ret)
			break;
	}
	spin_unlock_irqrestore(&modlist_lock, flags);
	return ret;
#endif
}

static void default_be_board_handler(struct pt_regs *regs)
{
	unsigned long new_epc;
	unsigned long fixup;
	int data = regs->cp0_cause & 4;

	if (data && !user_mode(regs)) {
		fixup = search_dbe_table(regs->cp0_epc);
		if (fixup) {
			new_epc = fixup_exception(dpf_reg, fixup,
						  regs->cp0_epc);
			regs->cp0_epc = new_epc;
			return;
		}
	}

	/*
	 * Assume it would be too dangerous to continue ...
	 */
	printk(KERN_ALERT "%s bus error, epc == %08lx, ra == %08lx\n",
	       data ? "Data" : "Instruction",
	       regs->cp0_epc, regs->regs[31]);
	die_if_kernel("Oops", regs);
	force_sig(SIGBUS, current);
}

asmlinkage void do_ibe(struct pt_regs *regs)
{
	ibe_board_handler(regs);
}

asmlinkage void do_dbe(struct pt_regs *regs)
{
	dbe_board_handler(regs);
}

asmlinkage void do_ov(struct pt_regs *regs)
{
	if (compute_return_epc(regs))
		return;

	force_sig(SIGFPE, current);
}

/*
 * XXX Delayed fp exceptions when doing a lazy ctx switch XXX
 */
asmlinkage void do_fpe(struct pt_regs *regs, unsigned long fcr31)
{
	if (fcr31 & FPU_CSR_UNI_X) {
		extern void save_fp(struct task_struct *);
		extern void restore_fp(struct task_struct *);
		int sig;
		/*
	 	 * Unimplemented operation exception.  If we've got the
	 	 * full software emulator on-board, let's use it...
		 *
		 * Force FPU to dump state into task/thread context.
		 * We're moving a lot of data here for what is probably
		 * a single instruction, but the alternative is to 
		 * pre-decode the FP register operands before invoking
		 * the emulator, which seems a bit extreme for what
		 * should be an infrequent event.
		 */
		save_fp(current);
	
		/* Run the emulator */
		sig = fpu_emulator_cop1Handler(regs);

		/* 
		 * We can't allow the emulated instruction to leave the
		 * Unimplemented Operation bit set in $fcr31.
		 */
		current->thread.fpu.soft.sr &= ~FPU_CSR_UNI_X;

		/* Restore the hardware register state */
		restore_fp(current);

		/* If something went wrong, signal */
		if (sig)
			force_sig(sig, current);

		return;
	}

	if (compute_return_epc(regs))
		return;

	force_sig(SIGFPE, current);
	printk(KERN_DEBUG "Sent send SIGFPE to %s\n", current->comm);
}

static inline int get_insn_opcode(struct pt_regs *regs, unsigned int *opcode)
{
	unsigned int *epc;

	epc = (unsigned int *) regs->cp0_epc +
	      ((regs->cp0_cause & CAUSEF_BD) != 0);
	if (!get_user(*opcode, epc))
		return 0;

	force_sig(SIGSEGV, current);
	return 1;
}

asmlinkage void do_bp(struct pt_regs *regs)
{
	siginfo_t info;
	unsigned int opcode, bcode;
	unsigned int *epc;

	epc = (unsigned int *) regs->cp0_epc +
	      ((regs->cp0_cause & CAUSEF_BD) != 0);
	if (get_user(opcode, epc))
		goto sigsegv;

	/*
	 * There is the ancient bug in the MIPS assemblers that the break
	 * code starts left to bit 16 instead to bit 6 in the opcode.
	 * Gas is bug-compatible ...
	 */
	bcode = ((opcode >> 16) & ((1 << 20) - 1));

	/*
	 * (A short test says that IRIX 5.3 sends SIGTRAP for all break
	 * insns, even for break codes that indicate arithmetic failures.
	 * Weird ...)
	 * But should we continue the brokenness???  --macro
	 */
	switch (bcode) {
	case 6:
	case 7:
		if (bcode == 7)
			info.si_code = FPE_INTDIV;
		else
			info.si_code = FPE_INTOVF;
		info.si_signo = SIGFPE;
		info.si_errno = 0;
		info.si_addr = (void *)compute_return_epc(regs);
		force_sig_info(SIGFPE, &info, current);
		break;
	default:
		force_sig(SIGTRAP, current);
	}
	return;

sigsegv:
	force_sig(SIGSEGV, current);
}

asmlinkage void do_tr(struct pt_regs *regs)
{
	siginfo_t info;
	unsigned int opcode, bcode;
	unsigned *epc;

	epc = (unsigned int *) regs->cp0_epc +
	      ((regs->cp0_cause & CAUSEF_BD) != 0);
	if (get_user(opcode, epc))
		goto sigsegv;

	bcode = ((opcode >> 6) & ((1 << 20) - 1));

	/*
	 * (A short test says that IRIX 5.3 sends SIGTRAP for all break
	 * insns, even for break codes that indicate arithmetic failures.
	 * Weird ...)
	 * But should we continue the brokenness???  --macro
	 */
	switch (bcode) {
	case 6:
	case 7:
		if (bcode == 7)
			info.si_code = FPE_INTDIV;
		else
			info.si_code = FPE_INTOVF;
		info.si_signo = SIGFPE;
		info.si_errno = 0;
		info.si_addr = (void *)compute_return_epc(regs);
		force_sig_info(SIGFPE, &info, current);
		break;
	default:
		force_sig(SIGTRAP, current);
	}
	return;

sigsegv:
	force_sig(SIGSEGV, current);
}

#ifndef CONFIG_CPU_HAS_LLSC

#ifdef CONFIG_SMP
#error "ll/sc emulation is not SMP safe"
#endif

/*
 * userland emulation for R2300 CPUs
 * needed for the multithreading part of glibc
 *
 * this implementation can handle only sychronization between 2 or more
 * user contexts and is not SMP safe.
 */
asmlinkage void do_ri(struct pt_regs *regs)
{
	unsigned int opcode;

	if (!user_mode(regs))
		BUG();

	if (!get_insn_opcode(regs, &opcode)) {
		if ((opcode & OPCODE) == LL) {
			simulate_ll(regs, opcode);
			return;
		}
		if ((opcode & OPCODE) == SC) {
			simulate_sc(regs, opcode);
			return;
		}
	}

	if (compute_return_epc(regs))
		return;
	force_sig(SIGILL, current);
}

/*
 * The ll_bit is cleared by r*_switch.S
 */

unsigned long ll_bit;
#ifdef CONFIG_PROC_FS
extern unsigned long ll_ops;
extern unsigned long sc_ops;
#endif

static struct task_struct *ll_task = NULL;

void simulate_ll(struct pt_regs *regp, unsigned int opcode)
{
	unsigned long value, *vaddr;
	long offset;
	int signal = 0;

	/*
	 * analyse the ll instruction that just caused a ri exception
	 * and put the referenced address to addr.
	 */

	/* sign extend offset */
	offset = opcode & OFFSET;
	offset <<= 16;
	offset >>= 16;

	vaddr = (unsigned long *)((long)(regp->regs[(opcode & BASE) >> 21]) + offset);

#ifdef CONFIG_PROC_FS
	ll_ops++;
#endif

	if ((unsigned long)vaddr & 3)
		signal = SIGBUS;
	else if (get_user(value, vaddr))
		signal = SIGSEGV;
	else {
		if (ll_task == NULL || ll_task == current) {
			ll_bit = 1;
		} else {
			ll_bit = 0;
		}
		ll_task = current;
		regp->regs[(opcode & RT) >> 16] = value;
	}
	if (compute_return_epc(regp))
		return;
	if (signal)
		send_sig(signal, current, 1);
}

void simulate_sc(struct pt_regs *regp, unsigned int opcode)
{
	unsigned long *vaddr, reg;
	long offset;
	int signal = 0;

	/*
	 * analyse the sc instruction that just caused a ri exception
	 * and put the referenced address to addr.
	 */

	/* sign extend offset */
	offset = opcode & OFFSET;
	offset <<= 16;
	offset >>= 16;

	vaddr = (unsigned long *)((long)(regp->regs[(opcode & BASE) >> 21]) + offset);
	reg = (opcode & RT) >> 16;

#ifdef CONFIG_PROC_FS
	sc_ops++;
#endif

	if ((unsigned long)vaddr & 3)
		signal = SIGBUS;
	else if (ll_bit == 0 || ll_task != current)
		regp->regs[reg] = 0;
	else if (put_user(regp->regs[reg], vaddr))
		signal = SIGSEGV;
	else
		regp->regs[reg] = 1;
	if (compute_return_epc(regp))
		return;
	if (signal)
		send_sig(signal, current, 1);
}

#else /* MIPS 2 or higher */

asmlinkage void do_ri(struct pt_regs *regs)
{
	unsigned int opcode;

	get_insn_opcode(regs, &opcode);
	if (compute_return_epc(regs))
		return;

	force_sig(SIGILL, current);
}

#endif

asmlinkage void do_cpu(struct pt_regs *regs)
{
	unsigned int cpid;
	extern void lazy_fpu_switch(void *);
	extern void init_fpu(void);
	void fpu_emulator_init_fpu(void);
	int sig;

	cpid = (regs->cp0_cause >> CAUSEB_CE) & 3;
	if (cpid != 1)
		goto bad_cid;

	if (!(mips_cpu.options & MIPS_CPU_FPU))
		goto fp_emul;

	regs->cp0_status |= ST0_CU1;
	if (last_task_used_math == current)
		return;

	if (current->used_math) {		/* Using the FPU again.  */
		lazy_fpu_switch(last_task_used_math);
	} else {				/* First time FPU user.  */
		init_fpu();
		current->used_math = 1;
	}
	last_task_used_math = current;
	return;

fp_emul:
	if (last_task_used_math != current) {
		if (!current->used_math) {
			fpu_emulator_init_fpu();
			current->used_math = 1;
		}
	}
	sig = fpu_emulator_cop1Handler(regs);
	last_task_used_math = current;
	if (sig)
		force_sig(sig, current);
	return;

bad_cid:
	force_sig(SIGILL, current);
}

asmlinkage void do_watch(struct pt_regs *regs)
{
	/*
	 * We use the watch exception where available to detect stack
	 * overflows.
	 */
	show_regs(regs);
	panic("Caught WATCH exception - probably caused by stack overflow.");
}

asmlinkage void do_mcheck(struct pt_regs *regs)
{
	show_regs(regs);
	panic("Caught Machine Check exception - probably caused by multiple "
	      "matching entries in the TLB.");
}

asmlinkage void do_reserved(struct pt_regs *regs)
{
	/*
	 * Game over - no way to handle this if it ever occurs.  Most probably
	 * caused by a new unknown cpu type or after another deadly
	 * hard/software error.
	 */
	show_regs(regs);
	panic("Caught reserved exception - should not happen.");
}

static inline void watch_init(void)
{
	if (mips_cpu.options & MIPS_CPU_WATCH ) {
		set_except_vector(23, handle_watch);
 		watch_available = 1;
 	}
}

/*
 * Some MIPS CPUs can enable/disable for cache parity detection, but do
 * it different ways.
 */
static inline void parity_protection_init(void)
{
	switch (mips_cpu.cputype) {
	case CPU_5KC:
		/* Set the PE bit (bit 31) in the CP0_ECC register. */
		printk(KERN_INFO "Enable the cache parity protection for "
		       "MIPS 5KC CPUs.\n");
		write_32bit_cp0_register(CP0_ECC,
		                         read_32bit_cp0_register(CP0_ECC)
		                         | 0x80000000); 
		break;
	default:
		break;
	}
}

asmlinkage void cache_parity_error(void)
{
	unsigned int reg_val;

	/* For the moment, report the problem and hang. */
	reg_val = read_32bit_cp0_register(CP0_ERROREPC);
	printk("Cache error exception:\n");
	printk("cp0_errorepc == %08x\n", reg_val);
	reg_val = read_32bit_cp0_register(CP0_CACHEERR);
	printk("cp0_cacheerr == %08x\n", reg_val);

	printk("Decoded CP0_CACHEERR: %s cache fault in %s reference.\n",
	       reg_val & (1<<30) ? "secondary" : "primary",
	       reg_val & (1<<31) ? "data" : "insn");
	printk("Error bits: %s%s%s%s%s%s%s\n",
	       reg_val & (1<<29) ? "ED " : "",
	       reg_val & (1<<28) ? "ET " : "",
	       reg_val & (1<<26) ? "EE " : "",
	       reg_val & (1<<25) ? "EB " : "",
	       reg_val & (1<<24) ? "EI " : "",
	       reg_val & (1<<23) ? "E1 " : "",
	       reg_val & (1<<22) ? "E0 " : "");
	printk("IDX: 0x%08x\n", reg_val & ((1<<22)-1));

	if (reg_val&(1<<22))
		printk("DErrAddr0: 0x%08x\n",
		       read_32bit_cp0_set1_register(CP0_S1_DERRADDR0));

	if (reg_val&(1<<23))
		printk("DErrAddr1: 0x%08x\n",
		       read_32bit_cp0_set1_register(CP0_S1_DERRADDR1));

	panic("Can't handle the cache error!");
}

unsigned long exception_handlers[32];

/*
 * As a side effect of the way this is implemented we're limited
 * to interrupt handlers in the address range from
 * KSEG0 <= x < KSEG0 + 256mb on the Nevada.  Oh well ...
 */
void *set_except_vector(int n, void *addr)
{
	unsigned handler = (unsigned long) addr;
	unsigned old_handler = exception_handlers[n];
	exception_handlers[n] = handler;

	if (n == 0 && mips_cpu.options & MIPS_CPU_DIVEC) {
		*(volatile u32 *)(KSEG0+0x200) = 0x08000000 |
		                                 (0x03ffffff & (handler >> 2));
		flush_icache_range(KSEG0+0x200, KSEG0 + 0x204);
	}
	return (void *)old_handler;
}

asmlinkage int (*save_fp_context)(struct sigcontext *sc);
asmlinkage int (*restore_fp_context)(struct sigcontext *sc);
extern asmlinkage int _save_fp_context(struct sigcontext *sc);
extern asmlinkage int _restore_fp_context(struct sigcontext *sc);

extern asmlinkage int fpu_emulator_save_context(struct sigcontext *sc);
extern asmlinkage int fpu_emulator_restore_context(struct sigcontext *sc);

void __init trap_init(void)
{
	extern char except_vec0_nevada, except_vec0_r4000;
	extern char except_vec0_r4600, except_vec0_r2300;
	extern char except_vec1_generic, except_vec2_generic;
	extern char except_vec3_generic, except_vec3_r4000;
	extern char except_vec4;
	extern char except_vec_ejtag_debug;
	unsigned long i;

	/* Some firmware leaves the BEV flag set, clear it.  */
	clear_cp0_status(ST0_BEV);

	/* Copy the generic exception handler code to it's final destination. */
	memcpy((void *)(KSEG0 + 0x80), &except_vec1_generic, 0x80);
	memcpy((void *)(KSEG0 + 0x100), &except_vec2_generic, 0x80);
	memcpy((void *)(KSEG0 + 0x180), &except_vec3_generic, 0x80);
	flush_icache_range(KSEG0 + 0x80, KSEG0 + 0x200);
	/*
	 * Setup default vectors
	 */
	for (i = 0; i <= 31; i++)
		set_except_vector(i, handle_reserved);

	/* 
	 * Copy the EJTAG debug exception vector handler code to it's final 
	 * destination.
	 */
	memcpy((void *)(KSEG0 + 0x300), &except_vec_ejtag_debug, 0x80);

	/*
	 * Only some CPUs have the watch exceptions or a dedicated
	 * interrupt vector.
	 */
	watch_init();

	/*
	 * Some MIPS CPUs have a dedicated interrupt vector which reduces the
	 * interrupt processing overhead.  Use it where available.
	 */
	if (mips_cpu.options & MIPS_CPU_DIVEC) {
		memcpy((void *)(KSEG0 + 0x200), &except_vec4, 8);
		set_cp0_cause(CAUSEF_IV);
	}

	/*
	 * Some CPUs can enable/disable for cache parity detection, but does
	 * it different ways.
	 */
	parity_protection_init();

	set_except_vector(1, handle_mod);
	set_except_vector(2, handle_tlbl);
	set_except_vector(3, handle_tlbs);
	set_except_vector(4, handle_adel);
	set_except_vector(5, handle_ades);

	/*
	 * The Data Bus Error/ Instruction Bus Errors are signaled
	 * by external hardware.  Therefore these two expection have
	 * board specific handlers.
	 */
	set_except_vector(6, handle_ibe);
	set_except_vector(7, handle_dbe);
	ibe_board_handler = default_be_board_handler;
	dbe_board_handler = default_be_board_handler;

	set_except_vector(8, handle_sys);
	set_except_vector(9, handle_bp);
	set_except_vector(10, handle_ri);
	set_except_vector(11, handle_cpu);
	set_except_vector(12, handle_ov);
	set_except_vector(13, handle_tr);

	if (mips_cpu.options & MIPS_CPU_FPU)
		set_except_vector(15, handle_fpe);

	/*
	 * Handling the following exceptions depends mostly of the cpu type
	 */
	if ((mips_cpu.options & MIPS_CPU_4KEX)
	    && (mips_cpu.options & MIPS_CPU_4KTLB)) {
		if (mips_cpu.cputype == CPU_NEVADA) {
			memcpy((void *)KSEG0, &except_vec0_nevada, 0x80);
		} else if (mips_cpu.cputype == CPU_R4600)
			memcpy((void *)KSEG0, &except_vec0_r4600, 0x80);
		else
			memcpy((void *)KSEG0, &except_vec0_r4000, 0x80);

		/* Cache error vector already set above.  */

		if (mips_cpu.options & MIPS_CPU_VCE) {
			memcpy((void *)(KSEG0 + 0x180), &except_vec3_r4000,
			       0x80);
		} else {
			memcpy((void *)(KSEG0 + 0x180), &except_vec3_generic,
			       0x80);
		}

		if (mips_cpu.options & MIPS_CPU_FPU) {
		        save_fp_context = _save_fp_context;
			restore_fp_context = _restore_fp_context;
		} else {
		        save_fp_context = fpu_emulator_save_context;
			restore_fp_context = fpu_emulator_restore_context;
		}
	} else switch (mips_cpu.cputype) {
	case CPU_SB1:
		/*
		 * XXX - This should be folded in to the "cleaner" handling,
		 * above
		 */
		memcpy((void *)KSEG0, &except_vec0_r4000, 0x80);
		memcpy((void *)(KSEG0 + 0x180), &except_vec3_r4000, 0x80);
		save_fp_context = _save_fp_context;
		restore_fp_context = _restore_fp_context;

		/* Enable timer interrupt and scd mapped interrupt */
		clear_cp0_status(0xf000);
		set_cp0_status(0xc00);
		break;
	case CPU_R6000:
	case CPU_R6000A:
	        save_fp_context = _save_fp_context;
		restore_fp_context = _restore_fp_context;
		
		/*
		 * The R6000 is the only R-series CPU that features a machine
		 * check exception (similar to the R4000 cache error) and
		 * unaligned ldc1/sdc1 exception.  The handlers have not been
		 * written yet.  Well, anyway there is no R6000 machine on the
		 * current list of targets for Linux/MIPS.
		 * (Duh, crap, there is someone with a tripple R6k machine)
		 */
		//set_except_vector(14, handle_mc);
		//set_except_vector(15, handle_ndc);
	case CPU_R2000:
	case CPU_R3000:
	case CPU_R3000A:
	case CPU_R3041:
	case CPU_R3051:
	case CPU_R3052:
	case CPU_R3081:
	case CPU_R3081E:
	case CPU_TX3912:
	case CPU_TX3922:
	case CPU_TX3927:
	        save_fp_context = _save_fp_context;
		restore_fp_context = _restore_fp_context;
		memcpy((void *)KSEG0, &except_vec0_r2300, 0x80);
		memcpy((void *)(KSEG0 + 0x80), &except_vec3_generic, 0x80);
		break;

	case CPU_UNKNOWN:
	default:
		panic("Unknown CPU type");
	}
	flush_icache_range(KSEG0, KSEG0 + 0x200);

	if (mips_cpu.isa_level == MIPS_CPU_ISA_IV)
		set_cp0_status(ST0_XX);

	atomic_inc(&init_mm.mm_count);	/* XXX  UP?  */
	current->active_mm = &init_mm;
	write_32bit_cp0_register(CP0_CONTEXT, smp_processor_id()<<23);
	current_pgd[0] = init_mm.pgd;
}
