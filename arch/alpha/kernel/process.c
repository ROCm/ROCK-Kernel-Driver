/*
 *  linux/arch/alpha/kernel/process.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

/*
 * This file handles the architecture-dependent parts of process handling.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/utsname.h>
#include <linux/time.h>
#include <linux/major.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/elfcore.h>
#include <linux/reboot.h>
#include <linux/tty.h>
#include <linux/console.h>

#include <asm/reg.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/hwrpb.h>
#include <asm/fpu.h>

#include "proto.h"
#include "pci_impl.h"

/*
 * No need to acquire the kernel lock, we're entirely local..
 */
asmlinkage int
sys_sethae(unsigned long hae, unsigned long a1, unsigned long a2,
	   unsigned long a3, unsigned long a4, unsigned long a5,
	   struct pt_regs regs)
{
	(&regs)->hae = hae;
	return 0;
}

void default_idle(void)
{
	barrier();
}

void
cpu_idle(void)
{
	while (1) {
		void (*idle)(void) = default_idle;
		/* FIXME -- EV6 and LCA45 know how to power down
		   the CPU.  */

		while (!need_resched())
			idle();
		schedule();
	}
}


struct halt_info {
	int mode;
	char *restart_cmd;
};

static void
common_shutdown_1(void *generic_ptr)
{
	struct halt_info *how = (struct halt_info *)generic_ptr;
	struct percpu_struct *cpup;
	unsigned long *pflags, flags;
	int cpuid = smp_processor_id();

	/* No point in taking interrupts anymore. */
	local_irq_disable();

	cpup = (struct percpu_struct *)
			((unsigned long)hwrpb + hwrpb->processor_offset
			 + hwrpb->processor_size * cpuid);
	pflags = &cpup->flags;
	flags = *pflags;

	/* Clear reason to "default"; clear "bootstrap in progress". */
	flags &= ~0x00ff0001UL;

#ifdef CONFIG_SMP
	/* Secondaries halt here. */
	if (cpuid != boot_cpuid) {
		flags |= 0x00040000UL; /* "remain halted" */
		*pflags = flags;
		clear_bit(cpuid, &cpu_present_mask);
		halt();
	}
#endif

	if (how->mode == LINUX_REBOOT_CMD_RESTART) {
		if (!how->restart_cmd) {
			flags |= 0x00020000UL; /* "cold bootstrap" */
		} else {
			/* For SRM, we could probably set environment
			   variables to get this to work.  We'd have to
			   delay this until after srm_paging_stop unless
			   we ever got srm_fixup working.

			   At the moment, SRM will use the last boot device,
			   but the file and flags will be the defaults, when
			   doing a "warm" bootstrap.  */
			flags |= 0x00030000UL; /* "warm bootstrap" */
		}
	} else {
		flags |= 0x00040000UL; /* "remain halted" */
	}
	*pflags = flags;

#ifdef CONFIG_SMP
	/* Wait for the secondaries to halt. */
	clear_bit(boot_cpuid, &cpu_present_mask);
	while (cpu_present_mask)
		barrier();
#endif

	/* If booted from SRM, reset some of the original environment. */
	if (alpha_using_srm) {
#ifdef CONFIG_DUMMY_CONSOLE
		/* This has the effect of resetting the VGA video origin.  */
		take_over_console(&dummy_con, 0, MAX_NR_CONSOLES-1, 1);
#endif
		/* reset_for_srm(); */
		set_hae(srm_hae);
	}

	if (alpha_mv.kill_arch)
		alpha_mv.kill_arch(how->mode);

	if (! alpha_using_srm && how->mode != LINUX_REBOOT_CMD_RESTART) {
		/* Unfortunately, since MILO doesn't currently understand
		   the hwrpb bits above, we can't reliably halt the 
		   processor and keep it halted.  So just loop.  */
		return;
	}

	if (alpha_using_srm)
		srm_paging_stop();

	halt();
}

static void
common_shutdown(int mode, char *restart_cmd)
{
	struct halt_info args;
	args.mode = mode;
	args.restart_cmd = restart_cmd;
#ifdef CONFIG_SMP
	smp_call_function(common_shutdown_1, &args, 1, 0);
#endif
	common_shutdown_1(&args);
}

void
machine_restart(char *restart_cmd)
{
	common_shutdown(LINUX_REBOOT_CMD_RESTART, restart_cmd);
}

void
machine_halt(void)
{
	common_shutdown(LINUX_REBOOT_CMD_HALT, NULL);
}

void
machine_power_off(void)
{
	common_shutdown(LINUX_REBOOT_CMD_POWER_OFF, NULL);
}

void
show_regs(struct pt_regs * regs)
{
	printk("\n");
	printk("Pid: %d, comm: %20s\n", current->pid, current->comm);
	printk("ps: %04lx pc: [<%016lx>] CPU %d    %s\n",
	       regs->ps, regs->pc, smp_processor_id(), print_tainted());
	printk("rp: [<%016lx>] sp: %p\n", regs->r26, regs+1);
	printk(" r0: %016lx  r1: %016lx  r2: %016lx  r3: %016lx\n",
	       regs->r0, regs->r1, regs->r2, regs->r3);
	printk(" r4: %016lx  r5: %016lx  r6: %016lx  r7: %016lx\n",
	       regs->r4, regs->r5, regs->r6, regs->r7);
	printk(" r8: %016lx r16: %016lx r17: %016lx r18: %016lx\n",
	       regs->r8, regs->r16, regs->r17, regs->r18);
	printk("r19: %016lx r20: %016lx r21: %016lx r22: %016lx\n",
	       regs->r19, regs->r20, regs->r21, regs->r22);
	printk("r23: %016lx r24: %016lx r25: %016lx r26: %016lx\n",
	       regs->r23, regs->r24, regs->r25, regs->r26);
	printk("r27: %016lx r28: %016lx r29: %016lx hae: %016lx\n",
	       regs->r27, regs->r28, regs->gp, regs->hae);
}

/*
 * Re-start a thread when doing execve()
 */
void
start_thread(struct pt_regs * regs, unsigned long pc, unsigned long sp)
{
	set_fs(USER_DS);
	regs->pc = pc;
	regs->ps = 8;
	wrusp(sp);
}

/*
 * Free current thread data structures etc..
 */
void
exit_thread(void)
{
}

void
flush_thread(void)
{
	/* Arrange for each exec'ed process to start off with a clean slate
	   with respect to the FPU.  This is all exceptions disabled.  */
	current_thread_info()->ieee_state = 0;
	wrfpcr(FPCR_DYN_NORMAL | ieee_swcr_to_fpcr(0));
}

void
release_thread(struct task_struct *dead_task)
{
}

/*
 * "alpha_clone()".. By the time we get here, the
 * non-volatile registers have also been saved on the
 * stack. We do some ugly pointer stuff here.. (see
 * also copy_thread)
 *
 * Notice that "fork()" is implemented in terms of clone,
 * with parameters (SIGCHLD, 0).
 */
int
alpha_clone(unsigned long clone_flags, unsigned long usp,
	    struct switch_stack * swstack)
{
	struct task_struct *p;
	if (!usp)
		usp = rdusp();

	p = do_fork(clone_flags & ~CLONE_IDLETASK,
		    usp, (struct pt_regs *) (swstack+1), 0);
	return IS_ERR(p) ? PTR_ERR(p) : p->pid;
}

int
alpha_vfork(struct switch_stack * swstack)
{
	struct task_struct *p;
	p = do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, rdusp(),
		    (struct pt_regs *) (swstack+1), 0);
	return IS_ERR(p) ? PTR_ERR(p) : p->pid;
}

/*
 * Copy an alpha thread..
 *
 * Note the "stack_offset" stuff: when returning to kernel mode, we need
 * to have some extra stack-space for the kernel stack that still exists
 * after the "ret_from_fork".  When returning to user mode, we only want
 * the space needed by the syscall stack frame (ie "struct pt_regs").
 * Use the passed "regs" pointer to determine how much space we need
 * for a kernel fork().
 */

int
copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
	    unsigned long unused,
	    struct task_struct * p, struct pt_regs * regs)
{
	extern void ret_from_sys_call(void);
	extern void ret_from_fork(void);

	struct thread_info *childti = p->thread_info;
	struct pt_regs * childregs;
	struct switch_stack * childstack, *stack;
	unsigned long stack_offset;

	stack_offset = PAGE_SIZE - sizeof(struct pt_regs);
	if (!(regs->ps & 8))
		stack_offset = (PAGE_SIZE-1) & (unsigned long) regs;
	childregs = (struct pt_regs *)
	  (stack_offset + PAGE_SIZE + (long) childti);
		
	*childregs = *regs;
	childregs->r0 = 0;
	childregs->r19 = 0;
	childregs->r20 = 1;	/* OSF/1 has some strange fork() semantics.  */
	regs->r20 = 0;
	stack = ((struct switch_stack *) regs) - 1;
	childstack = ((struct switch_stack *) childregs) - 1;
	*childstack = *stack;
#ifdef CONFIG_SMP
	childstack->r26 = (unsigned long) ret_from_fork;
#else
	childstack->r26 = (unsigned long) ret_from_sys_call;
#endif
	childti->pcb.usp = usp;
	childti->pcb.ksp = (unsigned long) childstack;
	childti->pcb.flags = 1;	/* set FEN, clear everything else */

	return 0;
}

/*
 * fill in the user structure for a core dump..
 */
void
dump_thread(struct pt_regs * pt, struct user * dump)
{
	/* switch stack follows right below pt_regs: */
	struct switch_stack * sw = ((struct switch_stack *) pt) - 1;

	dump->magic = CMAGIC;
	dump->start_code  = current->mm->start_code;
	dump->start_data  = current->mm->start_data;
	dump->start_stack = rdusp() & ~(PAGE_SIZE - 1);
	dump->u_tsize = ((current->mm->end_code - dump->start_code)
			 >> PAGE_SHIFT);
	dump->u_dsize = ((current->mm->brk + PAGE_SIZE-1 - dump->start_data)
			 >> PAGE_SHIFT);
	dump->u_ssize = (current->mm->start_stack - dump->start_stack
			 + PAGE_SIZE-1) >> PAGE_SHIFT;

	/*
	 * We store the registers in an order/format that is
	 * compatible with DEC Unix/OSF/1 as this makes life easier
	 * for gdb.
	 */
	dump->regs[EF_V0]  = pt->r0;
	dump->regs[EF_T0]  = pt->r1;
	dump->regs[EF_T1]  = pt->r2;
	dump->regs[EF_T2]  = pt->r3;
	dump->regs[EF_T3]  = pt->r4;
	dump->regs[EF_T4]  = pt->r5;
	dump->regs[EF_T5]  = pt->r6;
	dump->regs[EF_T6]  = pt->r7;
	dump->regs[EF_T7]  = pt->r8;
	dump->regs[EF_S0]  = sw->r9;
	dump->regs[EF_S1]  = sw->r10;
	dump->regs[EF_S2]  = sw->r11;
	dump->regs[EF_S3]  = sw->r12;
	dump->regs[EF_S4]  = sw->r13;
	dump->regs[EF_S5]  = sw->r14;
	dump->regs[EF_S6]  = sw->r15;
	dump->regs[EF_A3]  = pt->r19;
	dump->regs[EF_A4]  = pt->r20;
	dump->regs[EF_A5]  = pt->r21;
	dump->regs[EF_T8]  = pt->r22;
	dump->regs[EF_T9]  = pt->r23;
	dump->regs[EF_T10] = pt->r24;
	dump->regs[EF_T11] = pt->r25;
	dump->regs[EF_RA]  = pt->r26;
	dump->regs[EF_T12] = pt->r27;
	dump->regs[EF_AT]  = pt->r28;
	dump->regs[EF_SP]  = rdusp();
	dump->regs[EF_PS]  = pt->ps;
	dump->regs[EF_PC]  = pt->pc;
	dump->regs[EF_GP]  = pt->gp;
	dump->regs[EF_A0]  = pt->r16;
	dump->regs[EF_A1]  = pt->r17;
	dump->regs[EF_A2]  = pt->r18;
	memcpy((char *)dump->regs + EF_SIZE, sw->fp, 32 * 8);
}

int
dump_fpu(struct pt_regs * regs, elf_fpregset_t *r)
{
	/* switch stack follows right below pt_regs: */
	struct switch_stack * sw = ((struct switch_stack *) regs) - 1;
	memcpy(r, sw->fp, 32 * 8);
	return 1;
}

/*
 * sys_execve() executes a new program.
 *
 * This works due to the alpha calling sequence: the first 6 args
 * are gotten from registers, while the rest is on the stack, so
 * we get a0-a5 for free, and then magically find "struct pt_regs"
 * on the stack for us..
 *
 * Don't do this at home.
 */
asmlinkage int
sys_execve(char *ufilename, char **argv, char **envp,
	   unsigned long a3, unsigned long a4, unsigned long a5,
	   struct pt_regs regs)
{
	int error;
	char *filename;

	filename = getname(ufilename);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename, argv, envp, &regs);
	putname(filename);
out:
	return error;
}

/*
 * Return saved PC of a blocked thread.  This assumes the frame
 * pointer is the 6th saved long on the kernel stack and that the
 * saved return address is the first long in the frame.  This all
 * holds provided the thread blocked through a call to schedule() ($15
 * is the frame pointer in schedule() and $15 is saved at offset 48 by
 * entry.S:do_switch_stack).
 *
 * Under heavy swap load I've seen this lose in an ugly way.  So do
 * some extra sanity checking on the ranges we expect these pointers
 * to be in so that we can fail gracefully.  This is just for ps after
 * all.  -- r~
 */

unsigned long
thread_saved_pc(task_t *t)
{
	unsigned long base = (unsigned long)t->thread_info;
	unsigned long fp, sp = t->thread_info->pcb.ksp;

	if (sp > base && sp+6*8 < base + 16*1024) {
		fp = ((unsigned long*)sp)[6];
		if (fp > sp && fp < base + 16*1024)
			return *(unsigned long *)fp;
	}

	return 0;
}

/*
 * These bracket the sleeping functions..
 */
extern void scheduling_functions_start_here(void);
extern void scheduling_functions_end_here(void);
#define first_sched	((unsigned long) scheduling_functions_start_here)
#define last_sched	((unsigned long) scheduling_functions_end_here)

unsigned long
get_wchan(struct task_struct *p)
{
	unsigned long schedule_frame;
	unsigned long pc;
	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;
	/*
	 * This one depends on the frame size of schedule().  Do a
	 * "disass schedule" in gdb to find the frame size.  Also, the
	 * code assumes that sleep_on() follows immediately after
	 * interruptible_sleep_on() and that add_timer() follows
	 * immediately after interruptible_sleep().  Ugly, isn't it?
	 * Maybe adding a wchan field to task_struct would be better,
	 * after all...
	 */

	pc = thread_saved_pc(p);
	if (pc >= first_sched && pc < last_sched) {
		schedule_frame = ((unsigned long *)p->thread_info->pcb.ksp)[6];
		return ((unsigned long *)schedule_frame)[12];
	}
	return pc;
}
