/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 1999 by Ralf Baechle
 * Modified for R3000 by Paul M. Antoine, 1995, 1996
 * Complete output from die() by Ulf Carlsson, 1998
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
#include <asm/jazz.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/bootinfo.h>
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
extern asmlinkage void handle_reserved(void);

static char *cpu_names[] = CPU_NAMES;

char watch_available = 0;
char dedicated_iv_available = 0;
char vce_available = 0;

void (*ibe_board_handler)(struct pt_regs *regs);
void (*dbe_board_handler)(struct pt_regs *regs);

int kstack_depth_to_print = 24;

/*
 * These constant is for searching for possible module text segments.
 * MODULE_RANGE is a guess of how much space is likely to be vmalloced.
 */
#define MODULE_RANGE (8*1024*1024)

#if !defined(CONFIG_CPU_HAS_LLSC)
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
#define SC     0xd0000000

#define DEBUG_LLSC
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
while(1);
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

#define search_dbe_table(addr)	\
	search_one_table(__start___dbe_table, __stop___dbe_table - 1, (addr))

static void default_be_board_handler(struct pt_regs *regs)
{
	unsigned long new_epc;
	unsigned long fixup = search_dbe_table(regs->cp0_epc);

	if (fixup) {
		new_epc = fixup_exception(dpf_reg, fixup, regs->cp0_epc);
		regs->cp0_epc = new_epc;
		return;
	}

	/*
	 * Assume it would be too dangerous to continue ...
	 */
/* XXX */
printk("Got Bus Error at %08x\n", (unsigned int)regs->cp0_epc);
show_regs(regs); while(1);
	force_sig(SIGBUS, current);
}

void do_ibe(struct pt_regs *regs)
{
	ibe_board_handler(regs);
}

void do_dbe(struct pt_regs *regs)
{
	dbe_board_handler(regs);
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
	 * Weird ...)
	 */
	force_sig(SIGTRAP, current);
}

#if !defined(CONFIG_CPU_HAS_LLSC)

/*
 * userland emulation for R2300 CPUs
 * needed for the multithreading part of glibc
 */
void do_ri(struct pt_regs *regs)
{
	unsigned int opcode;

	if (!get_insn_opcode(regs, &opcode)) {
		if ((opcode & OPCODE) == LL)
			simulate_ll(regs, opcode);
		if ((opcode & OPCODE) == SC)
			simulate_sc(regs, opcode);
	} else {
	printk("[%s:%d] Illegal instruction at %08lx ra=%08lx\n",
	       current->comm, current->pid, regs->cp0_epc, regs->regs[31]);
	}
	if (compute_return_epc(regs))
		return;
	force_sig(SIGILL, current);
}

/*
 * the ll_bit will be cleared by r2300_switch.S
 */
unsigned long ll_bit, *lladdr;
 
void simulate_ll(struct pt_regs *regp, unsigned int opcode)
{
	unsigned long *addr, *vaddr;
	long offset;
 
	/*
	 * analyse the ll instruction that just caused a ri exception
	 * and put the referenced address to addr.
	 */
	/* sign extend offset */
	offset = opcode & OFFSET;
	if (offset & 0x00008000)
		offset = -(offset & 0x00007fff);
	else
		offset = (offset & 0x00007fff);

	vaddr = (unsigned long *)((long)(regp->regs[(opcode & BASE) >> 21]) + offset);

#ifdef DEBUG_LLSC
	printk("ll: vaddr = 0x%08x, reg = %d\n", (unsigned int)vaddr, (opcode & RT) >> 16);
#endif

	/*
	 * TODO: compute physical address from vaddr
	 */
	panic("ll: emulation not yet finished!");

	lladdr = addr;
	ll_bit = 1;
	regp->regs[(opcode & RT) >> 16] = *addr;
}
 
void simulate_sc(struct pt_regs *regp, unsigned int opcode)
{
	unsigned long *addr, *vaddr, reg;
	long offset;

	/*
	 * analyse the sc instruction that just caused a ri exception
	 * and put the referenced address to addr.
	 */
	/* sign extend offset */
	offset = opcode & OFFSET;
	if (offset & 0x00008000)
		offset = -(offset & 0x00007fff);
	else
		offset = (offset & 0x00007fff);

	vaddr = (unsigned long *)((long)(regp->regs[(opcode & BASE) >> 21]) + offset);
	reg = (opcode & RT) >> 16;

#ifdef DEBUG_LLSC
	printk("sc: vaddr = 0x%08x, reg = %d\n", (unsigned int)vaddr, (unsigned int)reg);
#endif

	/*
	 * TODO: compute physical address from vaddr
	 */
	panic("sc: emulation not yet finished!");

	lladdr = addr;

	if (ll_bit == 0) {
		regp->regs[reg] = 0;
		return;
	}

	*addr = regp->regs[reg];
	regp->regs[reg] = 1;
}

#else /* MIPS 2 or higher */

void do_ri(struct pt_regs *regs)
{
	printk("[%s:%d] Illegal instruction at %08lx ra=%08lx\n",
	       current->comm, current->pid, regs->cp0_epc, regs->regs[31]);
	if (compute_return_epc(regs))
		return;
	force_sig(SIGILL, current);
}

#endif

void do_cpu(struct pt_regs *regs)
{
	unsigned int cpid;
	extern void lazy_fpu_switch(void*);
	extern void init_fpu(void);

	cpid = (regs->cp0_cause >> CAUSEB_CE) & 3;
	if (cpid != 1)
		goto bad_cid;

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
	 * Game over - no way to handle this if it ever occurs.
	 * Most probably caused by a new unknown cpu type or
	 * after another deadly hard/software error.
	 */
	panic("Caught reserved exception - should not happen.");
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
	unsigned handler = (unsigned long) addr;
	exception_handlers[n] = handler;
	if (n == 0 && dedicated_iv_available) {
		*(volatile u32 *)(KSEG0+0x200) = 0x08000000 |
		                                 (0x03ffffff & (handler >> 2));
		flush_icache_range(KSEG0+0x200, KSEG0 + 0x204);
	}
}

void __init trap_init(void)
{
	extern char except_vec0_nevada, except_vec0_r4000;
	extern char except_vec0_r4600, except_vec0_r2300;
	extern char except_vec1_generic, except_vec2_generic;
	extern char except_vec3_generic, except_vec3_r4000;
	unsigned long i;

	if(mips_machtype == MACH_MIPS_MAGNUM_4000 ||
	   mips_machtype == MACH_SNI_RM200_PCI)
		EISA_bus = 1;

	/* Some firmware leaves the BEV flag set, clear it.  */
	set_cp0_status(ST0_BEV, 0);

	/* Copy the generic exception handler code to it's final destination. */
	memcpy((void *)(KSEG0 + 0x80), &except_vec1_generic, 0x80);
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
	set_except_vector(15, handle_fpe);

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
		/*
		 * The R10k might even work for Linux/MIPS - but we're paranoid
		 * and refuse to run until this is tested on real silicon
		 */
		panic("CPU too expensive - making holiday in the ANDES!");
		break;
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
     /* case CPU_R4640: */
	case CPU_R4600:
        case CPU_R5000:
        case CPU_NEVADA:
		if(mips_cputype == CPU_NEVADA) {
			memcpy((void *)KSEG0, &except_vec0_nevada, 0x80);
		} else if (mips_cputype == CPU_R4600)
			memcpy((void *)KSEG0, &except_vec0_r4600, 0x80);
		else
			memcpy((void *)KSEG0, &except_vec0_r4000, 0x80);

		/* Cache error vector already set above.  */

		if (vce_available) {
			memcpy((void *)(KSEG0 + 0x180), &except_vec3_r4000,
			       0x80);
		} else {
			memcpy((void *)(KSEG0 + 0x180), &except_vec3_generic,
			       0x80);
		}
		break;

	case CPU_R6000:
	case CPU_R6000A:
#if 0
		/*
		 * The R6000 is the only R-series CPU that features a machine
		 * check exception (similar to the R4000 cache error) and
		 * unaligned ldc1/sdc1 exception.  The handlers have not been
		 * written yet.  Well, anyway there is no R6000 machine on the
		 * current list of targets for Linux/MIPS.
		 * (Duh, crap, there is someone with a tripple R6k machine)
		 */
		set_except_vector(14, handle_mc);
		set_except_vector(15, handle_ndc);
#endif
	case CPU_R2000:
	case CPU_R3000:
	case CPU_R3000A:
	case CPU_R3041:
	case CPU_R3051:
	case CPU_R3052:
	case CPU_R3081:
	case CPU_R3081E:
		memcpy((void *)KSEG0, &except_vec0_r2300, 0x80);
		memcpy((void *)(KSEG0 + 0x80), &except_vec3_generic, 0x80);
		break;
	case CPU_R8000:
		printk("Detected unsupported CPU type %s.\n",
			cpu_names[mips_cputype]);
		panic("Can't handle CPU");
		break;

	case CPU_UNKNOWN:
	default:
		panic("Unknown CPU type");
	}
	flush_icache_range(KSEG0, KSEG0 + 0x200);

	atomic_inc(&init_mm.mm_count);	/* XXX  UP?  */
	current->active_mm = &init_mm;
	current_pgd = init_mm.pgd;
}
