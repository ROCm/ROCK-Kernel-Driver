/*
 *  linux/arch/x86-64/traps.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002 Andi Kleen, SuSE Labs
 *
 *  Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 *
 *  $Id: traps.c,v 1.36 2002/03/24 11:09:10 ak Exp $
 */

/*
 * 'Traps.c' handles hardware traps and faults after we have saved some
 * state in 'entry.S'.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/module.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include <asm/debugreg.h>
#include <asm/desc.h>
#include <asm/i387.h>
#include <asm/kdebug.h>

#include <asm/smp.h>
#include <asm/pgalloc.h>
#include <asm/pda.h>

#include <linux/irq.h>

asmlinkage int system_call(void);
asmlinkage int kernel_syscall(void);
extern void ia32_syscall(void);

char doublefault_stack[4*1024]; 
#ifndef CONFIG_SMP
char stackfault_stack[4*1024];
#endif

extern struct gate_struct idt_table[256]; 

asmlinkage void divide_error(void);
asmlinkage void debug(void);
asmlinkage void nmi(void);
asmlinkage void int3(void);
asmlinkage void overflow(void);
asmlinkage void bounds(void);
asmlinkage void invalid_op(void);
asmlinkage void device_not_available(void);
asmlinkage void double_fault(void);
asmlinkage void coprocessor_segment_overrun(void);
asmlinkage void invalid_TSS(void);
asmlinkage void segment_not_present(void);
asmlinkage void stack_segment(void);
asmlinkage void general_protection(void);
asmlinkage void page_fault(void);
asmlinkage void coprocessor_error(void);
asmlinkage void simd_coprocessor_error(void);
asmlinkage void reserved(void);
asmlinkage void alignment_check(void);
asmlinkage void machine_check(void);
asmlinkage void spurious_interrupt_bug(void);
asmlinkage void call_debug(void);

extern int exception_trace;

struct notifier_block *die_chain;

static int kstack_depth_to_print = 10;

#ifdef CONFIG_KALLSYMS
#include <linux/kallsyms.h> 
int printk_address(unsigned long address)
{ 
	unsigned long dummy; 
	const char *modname, *secname, *symname;
	unsigned long symstart; 
	char *delim = ":"; 

	/* What a function call! */
	if (!kallsyms_address_to_symbol(address, 
					&modname, &dummy, &dummy, 
					&secname, &dummy, &dummy,
					&symname, &symstart, &dummy)) {
		return printk("[<%016lx>]", address);
	} 
	if (!strcmp(modname, "kernel"))
		modname = delim = ""; 		
        return printk("[%016lx%s%s%s%s%+ld]",
		      address,delim,modname,delim,symname,address-symstart); 
} 
#else
int printk_address(unsigned long address)
{ 
	return printk("[<%016lx>]", address);
} 
#endif


#ifdef CONFIG_MODULES

extern struct module *module_list;
extern struct module kernel_module;

static inline int kernel_text_address(unsigned long addr)
{
   int retval = 0;
   struct module *mod;

   if (addr >= (unsigned long) &_stext &&
       addr <= (unsigned long) &_etext)
       return 1;

   for (mod = module_list; mod != &kernel_module; mod = mod->next) {
       /* mod_bound tests for addr being inside the vmalloc'ed
        * module area. Of course it'd be better to test only
        * for the .text subset... */
       if (mod_bound(addr, 0, mod)) {
           retval = 1;
           break;
       }
   }

   return retval;
}

#else

static inline int kernel_text_address(unsigned long addr)
{
   return (addr >= (unsigned long) &_stext &&
       addr <= (unsigned long) &_etext);
}

#endif

/*
 * These constants are for searching for possible module text
 * segments. MODULE_RANGE is a guess of how much space is likely
 * to be vmalloced.
 */
#define MODULE_RANGE (8*1024*1024)

void show_trace(unsigned long *stack)
{
	unsigned long addr;
	unsigned long *irqstack, *irqstack_end;
	/* FIXME: should read the cpuid from the APIC; to still work with bogus %gs */
	const int cpu = smp_processor_id();
	int i;

	printk("\nCall Trace: ");

	irqstack_end = (unsigned long *) (cpu_pda[cpu].irqstackptr);
	irqstack = (unsigned long *) (cpu_pda[cpu].irqstackptr - IRQSTACKSIZE + 64);

	i = 1;
	if (stack >= irqstack && stack < irqstack_end) {
		unsigned long *tstack;

		printk("<IRQ> ");  
		while (stack < irqstack_end) {
			addr = *stack++;
			/*
			 * If the address is either in the text segment of the
			 * kernel, or in the region which contains vmalloc'ed
			 * memory, it *may* be the address of a calling
			 * routine; if so, print it so that someone tracing
			 * down the cause of the crash will be able to figure
			 * out the call path that was taken.
			 */
			 if (kernel_text_address(addr)) {  
				 i += printk_address(addr);
				 i += printk(" "); 
				 if (i > 50) { 
					printk("\n       ");
					 i = 0;
				 } 
			}
		} 
		stack = (unsigned long *) (irqstack_end[-1]);
		printk(" <EOI> ");
#if 1
		tstack = (unsigned long *)(current_thread_info()+1);
		if (stack < tstack || (char*)stack > (char*)tstack+THREAD_SIZE) 
			printk("\n" KERN_DEBUG 
		       "no stack at the end of irqstack; stack:%p, curstack %p\n",
			       stack, tstack); 
#endif			       
	} 

	while (((long) stack & (THREAD_SIZE-1)) != 0) {
		addr = *stack++;
		if (kernel_text_address(addr)) { 	 
			i += printk_address(addr);
			i += printk(" "); 
			if (i > 50) { 
				printk("\n       ");
					 i = 0;
			} 
		}
	}
	printk("\n");
}

void show_trace_task(struct task_struct *tsk)
{
	unsigned long rsp = tsk->thread.rsp;

	/* User space on another CPU? */
	if ((rsp ^ (unsigned long)tsk->thread_info) & (PAGE_MASK<<1))
		return;
	show_trace((unsigned long *)rsp);
}

void show_stack(unsigned long * rsp)
{
	unsigned long *stack;
	int i;
	const int cpu = smp_processor_id();
	unsigned long *irqstack_end = (unsigned long *) (cpu_pda[cpu].irqstackptr);
	unsigned long *irqstack = (unsigned long *) (cpu_pda[cpu].irqstackptr - IRQSTACKSIZE);    

	// debugging aid: "show_stack(NULL);" prints the
	// back trace for this cpu.

	if(rsp==NULL)
		rsp=(unsigned long*)&rsp;

	stack = rsp;
	for(i=0; i < kstack_depth_to_print; i++) {
		if (stack >= irqstack && stack <= irqstack_end) {
			if (stack == irqstack_end) {
				stack = (unsigned long *) (irqstack_end[-1]);
				printk(" <EOI> ");
			}
		} else {
		if (((long) stack & (THREAD_SIZE-1)) == 0)
			break;
		}
		if (i && ((i % 4) == 0))
			printk("\n       ");
		printk("%016lx ", *stack++);
	}
	show_trace((unsigned long *)rsp);
}

void show_registers(struct pt_regs *regs)
{
	int i;
	int in_kernel = (regs->cs & 3) == 0;
	unsigned long rsp;
#ifdef CONFIG_SMP
	/* For SMP should get the APIC id here, just to protect against corrupted GS */ 
	const int cpu = smp_processor_id(); 
#else
	const int cpu = 0;
#endif	
	struct task_struct *cur = cpu_pda[cpu].pcurrent; 

		rsp = regs->rsp;

	printk("CPU %d ", cpu);
	show_regs(regs);
	printk("Process %s (pid: %d, stackpage=%08lx)\n",
		cur->comm, cur->pid, 4096+(unsigned long)cur);

	/*
	 * When in-kernel, we also print out the stack and code at the
	 * time of the fault..
	 */
	if (in_kernel) {

		printk("Stack: ");
		show_stack((unsigned long*)rsp);

		printk("\nCode: ");
		if(regs->rip < PAGE_OFFSET)
			goto bad;

		for(i=0;i<20;i++)
		{
			unsigned char c;
			if(__get_user(c, &((unsigned char*)regs->rip)[i])) {
bad:
				printk(" Bad RIP value.");
				break;
			}
			printk("%02x ", c);
		}
	}
	printk("\n");
}	

void handle_BUG(struct pt_regs *regs)
{ 
	struct bug_frame f;
	char tmp;

	if (regs->cs & 3)
		return; 
	if (__copy_from_user(&f, (struct bug_frame *) regs->rip, 
			     sizeof(struct bug_frame)))
		return; 
	if ((unsigned long)f.filename < __PAGE_OFFSET || 
	    f.ud2[0] != 0x0f || f.ud2[1] != 0x0b) 
		return;
	if (__get_user(tmp, f.filename))
		f.filename = "unmapped filename"; 
	printk("Kernel BUG at %.50s:%d\n", f.filename, f.line); 	
} 

void out_of_line_bug(void)
{ 
	BUG(); 
} 

spinlock_t die_lock = SPIN_LOCK_UNLOCKED;
int die_owner = -1;

void die(const char * str, struct pt_regs * regs, long err)
{
	int cpu;
	struct die_args args = { regs, str, err };
	console_verbose();
	notifier_call_chain(&die_chain,  DIE_DIE, &args); 
	bust_spinlocks(1);
	handle_BUG(regs); 
	printk("%s: %04lx\n", str, err & 0xffff);
	cpu = smp_processor_id(); 
	/* racy, but better than risking deadlock. */ 
	local_irq_disable();
	if (!spin_trylock(&die_lock)) { 
		if (cpu == die_owner) 
			/* nested oops. should stop eventually */;
		else
			spin_lock(&die_lock); 
	}
	die_owner = cpu; 
	show_registers(regs);
	bust_spinlocks(0);
	spin_unlock_irq(&die_lock);
	notify_die(DIE_OOPS, (char *)str, regs, err);
	do_exit(SIGSEGV);
}

static inline void die_if_kernel(const char * str, struct pt_regs * regs, long err)
{
	if (!(regs->eflags & VM_MASK) && (regs->cs == __KERNEL_CS))
		die(str, regs, err);
}

static inline unsigned long get_cr2(void)
{
	unsigned long address;

	/* get the address */
	__asm__("movq %%cr2,%0":"=r" (address));
	return address;
}

static void do_trap(int trapnr, int signr, char *str, 
			   struct pt_regs * regs, long error_code, siginfo_t *info)
{
	if ((regs->cs & 3)  != 0) { 
		struct task_struct *tsk = current;

		if (exception_trace && trapnr != 3)
			printk("%s[%d] trap %s at rip:%lx rsp:%lx err:%lx\n",
		       tsk->comm, tsk->pid, str, regs->rip, regs->rsp, error_code);

		tsk->thread.error_code = error_code;
		tsk->thread.trap_no = trapnr;
		if (info)
			force_sig_info(signr, info, tsk);
		else
			force_sig(signr, tsk);
		return;
	}


	/* kernel trap */ 
	{	     
		unsigned long fixup = search_exception_table(regs->rip);
		if (fixup) {
			extern int exception_trace; 
			if (exception_trace)
	       printk(KERN_ERR
	             "%s: fixed kernel exception at %lx err:%ld\n",
	             current->comm, regs->rip, error_code);
		
			regs->rip = fixup;
		} else	
			die(str, regs, error_code);
		return;
	}
}

#define DO_ERROR(trapnr, signr, str, name) \
asmlinkage void do_##name(struct pt_regs * regs, long error_code) \
{ \
	do_trap(trapnr, signr, str, regs, error_code, NULL); \
}

#define DO_ERROR_INFO(trapnr, signr, str, name, sicode, siaddr) \
asmlinkage void do_##name(struct pt_regs * regs, long error_code) \
{ \
	siginfo_t info; \
	info.si_signo = signr; \
	info.si_errno = 0; \
	info.si_code = sicode; \
	info.si_addr = (void *)siaddr; \
	do_trap(trapnr, signr, str, regs, error_code, &info); \
}

DO_ERROR_INFO( 0, SIGFPE,  "divide error", divide_error, FPE_INTDIV, regs->rip)
DO_ERROR( 4, SIGSEGV, "overflow", overflow)
DO_ERROR( 5, SIGSEGV, "bounds", bounds)
DO_ERROR_INFO( 6, SIGILL,  "invalid operand", invalid_op, ILL_ILLOPN, regs->rip)
DO_ERROR( 7, SIGSEGV, "device not available", device_not_available)
DO_ERROR( 8, SIGSEGV, "double fault", double_fault)
DO_ERROR( 9, SIGFPE,  "coprocessor segment overrun", coprocessor_segment_overrun)
DO_ERROR(10, SIGSEGV, "invalid TSS", invalid_TSS)
DO_ERROR(11, SIGBUS,  "segment not present", segment_not_present)
DO_ERROR(12, SIGBUS,  "stack segment", stack_segment)
DO_ERROR_INFO(17, SIGBUS, "alignment check", alignment_check, BUS_ADRALN, get_cr2())
DO_ERROR(18, SIGSEGV, "reserved", reserved)

asmlinkage void do_int3(struct pt_regs * regs, long error_code)
{
	if (notify_die(DIE_INT3, "int3", regs, error_code) == NOTIFY_BAD)
		return;
	do_trap(3, SIGTRAP, "int3", regs, error_code, NULL);
}

extern void dump_pagetable(unsigned long);

asmlinkage void do_general_protection(struct pt_regs * regs, long error_code)
{
	if ((regs->cs & 3)!=0) { 
		struct task_struct *tsk = current;
		if (exception_trace)
			printk("%s[%d] #gp at rip:%lx rsp:%lx err:%lx\n",
			       tsk->comm, tsk->pid, regs->rip, regs->rsp, error_code);

		tsk->thread.error_code = error_code;
		tsk->thread.trap_no = 13;
		force_sig(SIGSEGV, tsk);
	return;
	} 

	/* kernel gp */
	{
		unsigned long fixup;
		fixup = search_exception_table(regs->rip);
		if (fixup) {
			regs->rip = fixup;
			return;
		}
//		dump_pagetable(regs->rip); 
		die("general protection fault", regs, error_code);
	}
}

static void mem_parity_error(unsigned char reason, struct pt_regs * regs)
{
	printk("Uhhuh. NMI received. Dazed and confused, but trying to continue\n");
	printk("You probably have a hardware problem with your RAM chips\n");

	/* Clear and disable the memory parity error line. */
	reason = (reason & 0xf) | 4;
	outb(reason, 0x61);
}

static void io_check_error(unsigned char reason, struct pt_regs * regs)
{
	printk("NMI: IOCK error (debug interrupt?)\n");
	show_registers(regs);

	/* Re-enable the IOCK line, wait for a few seconds */
	reason = (reason & 0xf) | 8;
	outb(reason, 0x61);
	mdelay(2000);
	reason &= ~8;
	outb(reason, 0x61);
}

static void unknown_nmi_error(unsigned char reason, struct pt_regs * regs)
{
	printk("Uhhuh. NMI received for unknown reason %02x.\n", reason);
	printk("Dazed and confused, but trying to continue\n");
	printk("Do you have a strange power saving mode enabled?\n");
}

asmlinkage void do_nmi(struct pt_regs * regs)
{
	unsigned char reason = inb(0x61);

	add_pda(__nmi_count,1);
	if (!(reason & 0xc0)) {
#if CONFIG_X86_LOCAL_APIC
		/*
		 * Ok, so this is none of the documented NMI sources,
		 * so it must be the NMI watchdog.
		 */
		if (nmi_watchdog) {
			nmi_watchdog_tick(regs);
			return;
		}
#endif
		unknown_nmi_error(reason, regs);
		return;
	}
	if (notify_die(DIE_NMI, "nmi", regs, reason) == NOTIFY_BAD)
		return; 
	if (reason & 0x80)
		mem_parity_error(reason, regs);
	if (reason & 0x40)
		io_check_error(reason, regs);
	/*
	 * Reassert NMI in case it became active meanwhile
	 * as it's edge-triggered.
	 */
	outb(0x8f, 0x70);
	inb(0x71);		/* dummy */
	outb(0x0f, 0x70);
	inb(0x71);		/* dummy */
}

asmlinkage void do_debug(struct pt_regs * regs, long error_code)
{
	unsigned long condition;
	struct task_struct *tsk = current;
	siginfo_t info;

	asm("movq %%db6,%0" : "=r" (condition));

	if (notify_die(DIE_DEBUG, "debug", regs, error_code) == NOTIFY_BAD)
		return; 

	/* Mask out spurious debug traps due to lazy DR7 setting */
	if (condition & (DR_TRAP0|DR_TRAP1|DR_TRAP2|DR_TRAP3)) {
		if (!tsk->thread.debugreg[7]) { 
			goto clear_dr7;
		}
	}

	tsk->thread.debugreg[6] = condition;

	/* Mask out spurious TF errors due to lazy TF clearing */
	if (condition & DR_STEP) {
		/*
		 * The TF error should be masked out only if the current
		 * process is not traced and if the TRAP flag has been set
		 * previously by a tracing process (condition detected by
		 * the PT_DTRACE flag); remember that the i386 TRAP flag
		 * can be modified by the process itself in user mode,
		 * allowing programs to debug themselves without the ptrace()
		 * interface.
		 */
                if ((regs->cs & 3) == 0)
                       goto clear_TF;
		if ((tsk->ptrace & (PT_DTRACE|PT_PTRACED)) == PT_DTRACE)
			goto clear_TF;
	}

	/* Ok, finally something we can handle */
	tsk->thread.trap_no = 1;
	tsk->thread.error_code = error_code;
	info.si_signo = SIGTRAP;
	info.si_errno = 0;
	info.si_code = TRAP_BRKPT;
	info.si_addr = ((regs->cs & 3) == 0) ? (void *)tsk->thread.rip : 
	                                        (void *)regs->rip;
	force_sig_info(SIGTRAP, &info, tsk);	
clear_dr7:
	asm("movq %0,%%db7"::"r"(0UL));
	return;

clear_TF:
	regs->eflags &= ~TF_MASK;
	return;
}

/*
 * Note that we play around with the 'TS' bit in an attempt to get
 * the correct behaviour even in the presence of the asynchronous
 * IRQ13 behaviour
 */
void math_error(void *rip)
{
	struct task_struct * task;
	siginfo_t info;
	unsigned short cwd, swd;

	/*
	 * Save the info for the exception handler and clear the error.
	 */
	task = current;
	save_init_fpu(task);
	task->thread.trap_no = 16;
	task->thread.error_code = 0;
	info.si_signo = SIGFPE;
	info.si_errno = 0;
	info.si_code = __SI_FAULT;
	info.si_addr = rip;
	/*
	 * (~cwd & swd) will mask out exceptions that are not set to unmasked
	 * status.  0x3f is the exception bits in these regs, 0x200 is the
	 * C1 reg you need in case of a stack fault, 0x040 is the stack
	 * fault bit.  We should only be taking one exception at a time,
	 * so if this combination doesn't produce any single exception,
	 * then we have a bad program that isn't syncronizing its FPU usage
	 * and it will suffer the consequences since we won't be able to
	 * fully reproduce the context of the exception
	 */
	cwd = get_fpu_cwd(task);
	swd = get_fpu_swd(task);
	switch (((~cwd) & swd & 0x3f) | (swd & 0x240)) {
		case 0x000:
		default:
			break;
		case 0x001: /* Invalid Op */
		case 0x040: /* Stack Fault */
		case 0x240: /* Stack Fault | Direction */
			info.si_code = FPE_FLTINV;
			break;
		case 0x002: /* Denormalize */
		case 0x010: /* Underflow */
			info.si_code = FPE_FLTUND;
			break;
		case 0x004: /* Zero Divide */
			info.si_code = FPE_FLTDIV;
			break;
		case 0x008: /* Overflow */
			info.si_code = FPE_FLTOVF;
			break;
		case 0x020: /* Precision */
			info.si_code = FPE_FLTRES;
			break;
	}
	force_sig_info(SIGFPE, &info, task);
}

asmlinkage void do_coprocessor_error(struct pt_regs * regs, long error_code)
{
	math_error((void *)regs->rip);
}

asmlinkage void bad_intr(void)
{
	printk("bad interrupt"); 
}

static inline void simd_math_error(void *rip)
{
	struct task_struct * task;
	siginfo_t info;
	unsigned short mxcsr;

	/*
	 * Save the info for the exception handler and clear the error.
	 */
	task = current;
	save_init_fpu(task);
	task->thread.trap_no = 19;
	task->thread.error_code = 0;
	info.si_signo = SIGFPE;
	info.si_errno = 0;
	info.si_code = __SI_FAULT;
	info.si_addr = rip;
	/*
	 * The SIMD FPU exceptions are handled a little differently, as there
	 * is only a single status/control register.  Thus, to determine which
	 * unmasked exception was caught we must mask the exception mask bits
	 * at 0x1f80, and then use these to mask the exception bits at 0x3f.
	 */
	mxcsr = get_fpu_mxcsr(task);
	switch (~((mxcsr & 0x1f80) >> 7) & (mxcsr & 0x3f)) {
		case 0x000:
		default:
			break;
		case 0x001: /* Invalid Op */
			info.si_code = FPE_FLTINV;
			break;
		case 0x002: /* Denormalize */
		case 0x010: /* Underflow */
			info.si_code = FPE_FLTUND;
			break;
		case 0x004: /* Zero Divide */
			info.si_code = FPE_FLTDIV;
			break;
		case 0x008: /* Overflow */
			info.si_code = FPE_FLTOVF;
			break;
		case 0x020: /* Precision */
			info.si_code = FPE_FLTRES;
			break;
	}
	force_sig_info(SIGFPE, &info, task);
}

asmlinkage void do_simd_coprocessor_error(struct pt_regs * regs,
					  long error_code)
{
		simd_math_error((void *)regs->rip);
}

asmlinkage void do_spurious_interrupt_bug(struct pt_regs * regs)
{
}

/*
 *  'math_state_restore()' saves the current math information in the
 * old math state array, and gets the new ones from the current task
 *
 * Careful.. There are problems with IBM-designed IRQ13 behaviour.
 * Don't touch unless you *really* know how it works.
 */
asmlinkage void math_state_restore(void)
{
	struct task_struct *me = current;
	clts();			/* Allow maths ops (or we recurse) */

	if (!me->used_math)
		init_fpu();
	restore_fpu_checking(&me->thread.i387.fxsave);
	set_thread_flag(TIF_USEDFPU); 
}

asmlinkage void math_emulate(void)
{
	BUG();
}

void do_call_debug(struct pt_regs *regs) 
{ 
	notify_die(DIE_CALL, "debug call", regs, 0); 
}

void __init trap_init(void)
{
	set_intr_gate(0,&divide_error);
	set_intr_gate(1,&debug);
	set_intr_gate_ist(2,&nmi,NMI_STACK);
	set_system_gate(3,&int3);	/* int3-5 can be called from all */
	set_system_gate(4,&overflow);
	set_system_gate(5,&bounds);
	set_intr_gate(6,&invalid_op);
	set_intr_gate(7,&device_not_available);
	set_intr_gate_ist(8,&double_fault, DOUBLEFAULT_STACK);
	set_intr_gate(9,&coprocessor_segment_overrun);
	set_intr_gate(10,&invalid_TSS);
	set_intr_gate(11,&segment_not_present);
	set_intr_gate_ist(12,&stack_segment,STACKFAULT_STACK);
	set_intr_gate(13,&general_protection);
	set_intr_gate(14,&page_fault);
	set_intr_gate(15,&spurious_interrupt_bug);
	set_intr_gate(16,&coprocessor_error);
	set_intr_gate(17,&alignment_check);
	set_intr_gate(18,&machine_check); 
	set_intr_gate(19,&simd_coprocessor_error);

#ifdef CONFIG_IA32_EMULATION
	set_intr_gate(IA32_SYSCALL_VECTOR, ia32_syscall);
#endif
       
	set_intr_gate(KDB_VECTOR, call_debug);
       
	notify_die(DIE_TRAPINIT, "traps initialized", 0, 0); 
	
	/*
	 * Should be a barrier for any external CPU state.
	 */
	cpu_init();
}
