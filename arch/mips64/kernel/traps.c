/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 1999 by Ralf Baechle
 * Copyright (C) 1995, 1996 Paul M. Antoine
 * Copyright (C) 1998 Ulf Carlsson
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>

#include <asm/branch.h>
#include <asm/cachectl.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/bootinfo.h>
#include <asm/ptrace.h>
#include <asm/watch.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/mmu_context.h>

extern asmlinkage void __xtlb_mod(void);
extern asmlinkage void __xtlb_tlbl(void);
extern asmlinkage void __xtlb_tlbs(void);
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
extern asmlinkage void handle_reserved(void);

static char *cpu_names[] = CPU_NAMES;

char watch_available = 0;
char dedicated_iv_available = 0;
char vce_available = 0;
char mips4_available = 0;

int kstack_depth_to_print = 24;

/*
 * These constant is for searching for possible module text segments.
 * MODULE_RANGE is a guess of how much space is likely to be vmalloced.
 */
#define MODULE_RANGE (8*1024*1024)

/*
 * This routine abuses get_user()/put_user() to reference pointers
 * with at least a bit of error checking ...
 */
void show_stack(unsigned long *sp)
{
	int i;
	unsigned long *stack;

	stack = sp;
	i = 0;

	printk("Stack:");
	while ((unsigned long) stack & (PAGE_SIZE - 1)) {
		unsigned long stackdata;

		if (__get_user(stackdata, stack++)) {
			printk(" (Bad stack address)");
			break;
		}

		printk(" %016lx", stackdata);

		if (++i > 40) {
			printk(" ...");
			break;
		}

		if (i % 4 == 0)
			printk("\n      ");
	}
}

void show_trace(unsigned long *sp)
{
	int i;
	unsigned long *stack;
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

			/* Since our kernel is still at KSEG0,
			 * truncate the address so that ksymoops
			 * understands it.
			 */
			printk(" [<%08x>]", (unsigned int) addr);
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
		unsigned int insn;
		if (__get_user(insn, pc + i)) {
			printk(" (Bad address in epc)\n");
			break;
		}
		printk("%c%08x%c",(i?' ':'<'),insn,(i?' ':'>'));
	}
}

spinlock_t die_lock;

void die(const char * str, struct pt_regs * regs, unsigned long err)
{
	if (user_mode(regs))	/* Just return if in user mode.  */
		return;

	console_verbose();
	spin_lock_irq(&die_lock);
	printk("%s: %04lx\n", str, err & 0xffff);
	show_regs(regs);
	printk("Process %s (pid: %d, stackpage=%08lx)\n",
		current->comm, current->pid, (unsigned long) current);
	show_stack((unsigned long *) regs->regs[29]);
	show_trace((unsigned long *) regs->regs[29]);
	show_code((unsigned int *) regs->cp0_epc);
	printk("\n");
	spin_unlock_irq(&die_lock);
	do_exit(SIGSEGV);
}

void die_if_kernel(const char * str, struct pt_regs * regs, unsigned long err)
{
	if (!user_mode(regs))
		die(str, regs, err);
}

void do_ov(struct pt_regs *regs)
{
	if (compute_return_epc(regs))
		return;
	force_sig(SIGFPE, current);
}

#ifdef CONFIG_MIPS_FPE_MODULE
static void (*fpe_handler)(struct pt_regs *regs, unsigned int fcr31);

/*
 * Register_fpe/unregister_fpe are for debugging purposes only.  To make
 * this hack work a bit better there is no error checking.
 */
int register_fpe(void (*handler)(struct pt_regs *regs, unsigned int fcr31))
{
	fpe_handler = handler;
	return 0;
}

int unregister_fpe(void (*handler)(struct pt_regs *regs, unsigned int fcr31))
{
	fpe_handler = NULL;
	return 0;
}
#endif

/*
 * XXX Delayed fp exceptions when doing a lazy ctx switch XXX
 */
void do_fpe(struct pt_regs *regs, unsigned long fcr31)
{
	unsigned long pc;
	unsigned int insn;
	extern void simfp(unsigned int);

#ifdef CONFIG_MIPS_FPE_MODULE
	if (fpe_handler != NULL) {
		fpe_handler(regs, fcr31);
		return;
	}
#endif
	if (fcr31 & 0x20000) {
		/* Retry instruction with flush to zero ...  */
		if (!(fcr31 & (1<<24))) {
			printk("Setting flush to zero for %s.\n",
			       current->comm);
			fcr31 &= ~0x20000;
			fcr31 |= (1<<24);
			__asm__ __volatile__(
				"ctc1\t%0,$31"
				: /* No outputs */
				: "r" (fcr31));
			return;
		}
		pc = regs->cp0_epc + ((regs->cp0_cause & CAUSEF_BD) ? 4 : 0);
		if (get_user(insn, (unsigned int *)pc)) {
			/* XXX Can this happen?  */
			force_sig(SIGSEGV, current);
		}

		printk(KERN_DEBUG "Unimplemented exception for insn %08x at 0x%08lx in %s.\n",
		       insn, regs->cp0_epc, current->comm);
		simfp(insn);
	}

	if (compute_return_epc(regs))
		return;
	//force_sig(SIGFPE, current);
	printk(KERN_DEBUG "Should send SIGFPE to %s\n", current->comm);
}

static inline int get_insn_opcode(struct pt_regs *regs, unsigned int *opcode)
{
	unsigned int *epc;

	epc = (unsigned int *) (unsigned long) regs->cp0_epc;
	if (regs->cp0_cause & CAUSEF_BD)
		epc += 4;

	if (verify_area(VERIFY_READ, epc, 4)) {
		force_sig(SIGSEGV, current);
		return 1;
	}
	*opcode = *epc;

	return 0;
}

void do_bp(struct pt_regs *regs)
{
	unsigned int opcode, bcode;

	/*
	 * There is the ancient bug in the MIPS assemblers that the break
	 * code starts left to bit 16 instead to bit 6 in the opcode.
	 * Gas is bug-compatible ...
	 */
	if (get_insn_opcode(regs, &opcode))
		return;
	bcode = ((opcode >> 16) & ((1 << 20) - 1));

	/*
	 * (A short test says that IRIX 5.3 sends SIGTRAP for all break
	 * insns, even for break codes that indicate arithmetic failures.
	 * Weird ...)
	 */
	force_sig(SIGTRAP, current);
}

void do_tr(struct pt_regs *regs)
{
	unsigned int opcode, bcode;

	if (get_insn_opcode(regs, &opcode))
		return;
	bcode = ((opcode >> 6) & ((1 << 20) - 1));

	/*
	 * (A short test says that IRIX 5.3 sends SIGTRAP for all break
	 * insns, even for break codes that indicate arithmetic failures.
	 * Wiered ...)
	 */
	force_sig(SIGTRAP, current);
}

void do_ri(struct pt_regs *regs)
{
	printk("Cpu%d[%s:%d] Illegal instruction at %08lx ra=%08lx\n",
	        smp_processor_id(), current->comm, current->pid, regs->cp0_epc, 
		regs->regs[31]);
	if (compute_return_epc(regs))
		return;
	force_sig(SIGILL, current);
}

void do_cpu(struct pt_regs *regs)
{
	u32 cpid;

	cpid = (regs->cp0_cause >> CAUSEB_CE) & 3;
	if (cpid != 1)
		goto bad_cid;

	regs->cp0_status |= ST0_CU1;
#ifndef CONFIG_SMP
	if (last_task_used_math == current)
		return;

	if (current->used_math) {		/* Using the FPU again.  */
		lazy_fpu_switch(last_task_used_math, current);
	} else {				/* First time FPU user.  */
		lazy_fpu_switch(last_task_used_math, 0);
		init_fpu();
		current->used_math = 1;
	}
	last_task_used_math = current;
#else
	if (current->used_math) {
		lazy_fpu_switch(0, current);
	} else {
		init_fpu();
		current->used_math = 1;
	}
	current->flags |= PF_USEDFPU;
#endif
	return;

bad_cid:
	force_sig(SIGILL, current);
}

void do_watch(struct pt_regs *regs)
{
	/*
	 * We use the watch exception where available to detect stack
	 * overflows.
	 */
	show_regs(regs);
	panic("Caught WATCH exception - probably caused by stack overflow.");
}

void do_reserved(struct pt_regs *regs)
{
	/*
	 * Game over - no way to handle this if it ever occurs.  Most probably
	 * caused by a new unknown cpu type or after another deadly
	 * hard/software error.
	 */
	panic("Caught reserved exception %ld - should not happen.",
	      (regs->cp0_cause & 0x1f) >> 2);
}

static inline void watch_init(unsigned long cputype)
{
	switch(cputype) {
	case CPU_R10000:
	case CPU_R4000MC:
	case CPU_R4400MC:
	case CPU_R4000SC:
	case CPU_R4400SC:
	case CPU_R4000PC:
	case CPU_R4400PC:
	case CPU_R4200:
	case CPU_R4300:
		set_except_vector(23, handle_watch);
		watch_available = 1;
		break;
	}
}

/*
 * Some MIPS CPUs have a dedicated interrupt vector which reduces the
 * interrupt processing overhead.  Use it where available.
 * FIXME: more CPUs than just the Nevada have this feature.
 */
static inline void setup_dedicated_int(void)
{
	extern void except_vec4(void);

	switch(mips_cputype) {
	case CPU_NEVADA:
		memcpy((void *)(KSEG0 + 0x200), except_vec4, 8);
		set_cp0_cause(CAUSEF_IV, CAUSEF_IV);
		dedicated_iv_available = 1;
	}
}

unsigned long exception_handlers[32];

/*
 * As a side effect of the way this is implemented we're limited
 * to interrupt handlers in the address range from
 * KSEG0 <= x < KSEG0 + 256mb on the Nevada.  Oh well ...
 */
void set_except_vector(int n, void *addr)
{
	unsigned long handler = (unsigned long) addr;
	exception_handlers[n] = handler;
	if (n == 0 && dedicated_iv_available) {
		*(volatile u32 *)(KSEG0+0x200) = 0x08000000 |
		                                 (0x03ffffff & (handler >> 2));
		flush_icache_range(KSEG0+0x200, KSEG0 + 0x204);
	}
}

static inline void mips4_setup(void)
{
	switch (mips_cputype) {
	case CPU_R5000:
	case CPU_R5000A:
	case CPU_NEVADA:
	case CPU_R8000:
	case CPU_R10000:
		mips4_available = 1;
		set_cp0_status(ST0_XX, ST0_XX);
	}
}

static inline void go_64(void)
{
	unsigned int bits;

	bits = ST0_KX|ST0_SX|ST0_UX;
	set_cp0_status(bits, bits);
	printk("Entering 64-bit mode.\n");
}

void __init trap_init(void)
{
	extern char except_vec0;
	extern char except_vec1_r10k;
	extern char except_vec2_generic;
	extern char except_vec3_generic, except_vec3_r4000;
	extern void bus_error_init(void);
	unsigned long i;

	/* Some firmware leaves the BEV flag set, clear it.  */
	set_cp0_status(ST0_BEV, 0);

	/* Copy the generic exception handler code to it's final destination. */
	memcpy((void *)(KSEG0 + 0x100), &except_vec2_generic, 0x80);
	memcpy((void *)(KSEG0 + 0x180), &except_vec3_generic, 0x80);

	/*
	 * Setup default vectors
	 */
	for(i = 0; i <= 31; i++)
		set_except_vector(i, handle_reserved);

	/*
	 * Only some CPUs have the watch exceptions or a dedicated
	 * interrupt vector.
	 */
	watch_init(mips_cputype);
	setup_dedicated_int();
	mips4_setup();
	go_64();		/* In memoriam C128 ;-)  */

	/*
	 * Handling the following exceptions depends mostly of the cpu type
	 */
	switch(mips_cputype) {
	case CPU_R10000:
		/*
		 * The R10000 is in most aspects similar to the R4400.  It
		 * should get some special optimizations.
		 */
		write_32bit_cp0_register(CP0_FRAMEMASK, 0);
		set_cp0_status(ST0_XX, ST0_XX);
		goto r4k;

	case CPU_R4000MC:
	case CPU_R4400MC:
	case CPU_R4000SC:
	case CPU_R4400SC:
		vce_available = 1;
		/* Fall through ...  */
	case CPU_R4000PC:
	case CPU_R4400PC:
	case CPU_R4200:
	case CPU_R4300:
	case CPU_R4600:
	case CPU_R5000:
	case CPU_NEVADA:
r4k:
		/* Debug TLB refill handler.  */
		memcpy((void *)KSEG0, &except_vec0, 0x80);
		memcpy((void *)KSEG0 + 0x080, &except_vec1_r10k, 0x80);

		/* Cache error vector  */
		memcpy((void *)(KSEG0 + 0x100), (void *) KSEG0, 0x80);

		if (vce_available) {
			memcpy((void *)(KSEG0 + 0x180), &except_vec3_r4000,
			       0x180);
		} else {
			memcpy((void *)(KSEG0 + 0x180), &except_vec3_generic,
			       0x100);
		}

		set_except_vector(1, __xtlb_mod);
		set_except_vector(2, __xtlb_tlbl);
		set_except_vector(3, __xtlb_tlbs);
		set_except_vector(4, handle_adel);
		set_except_vector(5, handle_ades);

		/* DBE / IBE exception handler are system specific.  */
		bus_error_init();

		set_except_vector(8, handle_sys);
		set_except_vector(9, handle_bp);
		set_except_vector(10, handle_ri);
		set_except_vector(11, handle_cpu);
		set_except_vector(12, handle_ov);
		set_except_vector(13, handle_tr);
		set_except_vector(15, handle_fpe);
		break;

	case CPU_R8000:
		panic("unsupported CPU type %s.\n", cpu_names[mips_cputype]);
		break;

	case CPU_UNKNOWN:
	default:
		panic("Unknown CPU type");
	}
	flush_icache_range(KSEG0, KSEG0 + 0x200);

	atomic_inc(&init_mm.mm_count);	/* XXX UP?  */
	current->active_mm = &init_mm;
}
