/*
 * Architecture-specific setup.
 *
 * Copyright (C) 1998-2001 Hewlett-Packard Co
 * Copyright (C) 1998-2001 David Mosberger-Tang <davidm@hpl.hp.com>
 */
#define __KERNEL_SYSCALLS__	/* see <asm/unistd.h> */
#include <linux/config.h>

#include <linux/pm.h>
#include <linux/elf.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>

#include <asm/delay.h>
#include <asm/efi.h>
#include <asm/perfmon.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/sal.h>
#include <asm/uaccess.h>
#include <asm/unwind.h>
#include <asm/user.h>

static void
do_show_stack (struct unw_frame_info *info, void *arg)
{
	unsigned long ip, sp, bsp;

	printk("\nCall Trace: ");
	do {
		unw_get_ip(info, &ip);
		if (ip == 0)
			break;

		unw_get_sp(info, &sp);
		unw_get_bsp(info, &bsp);
		printk("[<%016lx>] sp=0x%016lx bsp=0x%016lx\n", ip, sp, bsp);
	} while (unw_unwind(info) >= 0);
}

void
show_stack (struct task_struct *task)
{
	if (!task)
		unw_init_running(do_show_stack, 0);
	else {
		struct unw_frame_info info;

		unw_init_from_blocked_task(&info, task);
		do_show_stack(&info, 0);
	}
}

void
show_regs (struct pt_regs *regs)
{
	unsigned long ip = regs->cr_iip + ia64_psr(regs)->ri;

	printk("\nPid: %d, comm: %20s\n", current->pid, current->comm);
	printk("psr : %016lx ifs : %016lx ip  : [<%016lx>]    %s\n",
	       regs->cr_ipsr, regs->cr_ifs, ip, print_tainted());
	printk("unat: %016lx pfs : %016lx rsc : %016lx\n",
	       regs->ar_unat, regs->ar_pfs, regs->ar_rsc);
	printk("rnat: %016lx bsps: %016lx pr  : %016lx\n",
	       regs->ar_rnat, regs->ar_bspstore, regs->pr);
	printk("ldrs: %016lx ccv : %016lx fpsr: %016lx\n",
	       regs->loadrs, regs->ar_ccv, regs->ar_fpsr);
	printk("b0  : %016lx b6  : %016lx b7  : %016lx\n", regs->b0, regs->b6, regs->b7);
	printk("f6  : %05lx%016lx f7  : %05lx%016lx\n",
	       regs->f6.u.bits[1], regs->f6.u.bits[0],
	       regs->f7.u.bits[1], regs->f7.u.bits[0]);
	printk("f8  : %05lx%016lx f9  : %05lx%016lx\n",
	       regs->f8.u.bits[1], regs->f8.u.bits[0],
	       regs->f9.u.bits[1], regs->f9.u.bits[0]);

	printk("r1  : %016lx r2  : %016lx r3  : %016lx\n", regs->r1, regs->r2, regs->r3);
	printk("r8  : %016lx r9  : %016lx r10 : %016lx\n", regs->r8, regs->r9, regs->r10);
	printk("r11 : %016lx r12 : %016lx r13 : %016lx\n", regs->r11, regs->r12, regs->r13);
	printk("r14 : %016lx r15 : %016lx r16 : %016lx\n", regs->r14, regs->r15, regs->r16);
	printk("r17 : %016lx r18 : %016lx r19 : %016lx\n", regs->r17, regs->r18, regs->r19);
	printk("r20 : %016lx r21 : %016lx r22 : %016lx\n", regs->r20, regs->r21, regs->r22);
	printk("r23 : %016lx r24 : %016lx r25 : %016lx\n", regs->r23, regs->r24, regs->r25);
	printk("r26 : %016lx r27 : %016lx r28 : %016lx\n", regs->r26, regs->r27, regs->r28);
	printk("r29 : %016lx r30 : %016lx r31 : %016lx\n", regs->r29, regs->r30, regs->r31);

	/* print the stacked registers if cr.ifs is valid: */
	if (regs->cr_ifs & 0x8000000000000000) {
		unsigned long val, sof, *bsp, ndirty;
		int i, is_nat = 0;

		sof = regs->cr_ifs & 0x7f;	/* size of frame */
		ndirty = (regs->loadrs >> 19);
		bsp = ia64_rse_skip_regs((unsigned long *) regs->ar_bspstore, ndirty);
		for (i = 0; i < sof; ++i) {
			get_user(val, ia64_rse_skip_regs(bsp, i));
			printk("r%-3u:%c%016lx%s", 32 + i, is_nat ? '*' : ' ', val,
			       ((i == sof - 1) || (i % 3) == 2) ? "\n" : " ");
		}
	}
	if (!user_mode(regs))
		show_stack(0);
}

void __attribute__((noreturn))
cpu_idle (void *unused)
{
	/* endless idle loop with no priority at all */
	init_idle();
	current->nice = 20;
	current->counter = -100;


	while (1) {
#ifdef CONFIG_SMP
		if (!current->need_resched)
			min_xtp();
#endif
		while (!current->need_resched)
			continue;
#ifdef CONFIG_SMP
		normal_xtp();
#endif
		schedule();
		check_pgt_cache();
		if (pm_idle)
			(*pm_idle)();
	}
}

void
ia64_save_extra (struct task_struct *task)
{
	if ((task->thread.flags & IA64_THREAD_DBG_VALID) != 0)
		ia64_save_debug_regs(&task->thread.dbr[0]);
#ifdef CONFIG_PERFMON
	if ((task->thread.flags & IA64_THREAD_PM_VALID) != 0)
		pfm_save_regs(task);
#endif
	if (IS_IA32_PROCESS(ia64_task_regs(task)))
		ia32_save_state(task);
}

void
ia64_load_extra (struct task_struct *task)
{
	if ((task->thread.flags & IA64_THREAD_DBG_VALID) != 0)
		ia64_load_debug_regs(&task->thread.dbr[0]);
#ifdef CONFIG_PERFMON
	if ((task->thread.flags & IA64_THREAD_PM_VALID) != 0)
		pfm_load_regs(task);
#endif
	if (IS_IA32_PROCESS(ia64_task_regs(task)))
		ia32_load_state(task);
}

/*
 * Copy the state of an ia-64 thread.
 *
 * We get here through the following  call chain:
 *
 *	<clone syscall>
 *	sys_clone
 *	do_fork
 *	copy_thread
 *
 * This means that the stack layout is as follows:
 *
 *	+---------------------+ (highest addr)
 *	|   struct pt_regs    |
 *	+---------------------+
 *	| struct switch_stack |
 *	+---------------------+
 *	|                     |
 *	|    memory stack     |
 *	|                     | <-- sp (lowest addr)
 *	+---------------------+
 *
 * Note: if we get called through kernel_thread() then the memory
 * above "(highest addr)" is valid kernel stack memory that needs to
 * be copied as well.
 *
 * Observe that we copy the unat values that are in pt_regs and
 * switch_stack.  Spilling an integer to address X causes bit N in
 * ar.unat to be set to the NaT bit of the register, with N=(X &
 * 0x1ff)/8.  Thus, copying the unat value preserves the NaT bits ONLY
 * if the pt_regs structure in the parent is congruent to that of the
 * child, modulo 512.  Since the stack is page aligned and the page
 * size is at least 4KB, this is always the case, so there is nothing
 * to worry about.
 */
int
copy_thread (int nr, unsigned long clone_flags,
	     unsigned long user_stack_base, unsigned long user_stack_size,
	     struct task_struct *p, struct pt_regs *regs)
{
	unsigned long rbs, child_rbs, rbs_size, stack_offset, stack_top, stack_used;
	struct switch_stack *child_stack, *stack;
	extern char ia64_ret_from_clone, ia32_ret_from_clone;
	struct pt_regs *child_ptregs;
	int retval = 0;

#ifdef CONFIG_SMP
	/*
	 * For SMP idle threads, fork_by_hand() calls do_fork with
	 * NULL regs.
	 */
	if (!regs)
		return 0;
#endif

	stack_top = (unsigned long) current + IA64_STK_OFFSET;
	stack = ((struct switch_stack *) regs) - 1;
	stack_used = stack_top - (unsigned long) stack;
	stack_offset = IA64_STK_OFFSET - stack_used;

	child_stack = (struct switch_stack *) ((unsigned long) p + stack_offset);
	child_ptregs = (struct pt_regs *) (child_stack + 1);

	/* copy parent's switch_stack & pt_regs to child: */
	memcpy(child_stack, stack, stack_used);

	rbs = (unsigned long) current + IA64_RBS_OFFSET;
	child_rbs = (unsigned long) p + IA64_RBS_OFFSET;
	rbs_size = stack->ar_bspstore - rbs;

	/* copy the parent's register backing store to the child: */
	memcpy((void *) child_rbs, (void *) rbs, rbs_size);

	if (user_mode(child_ptregs)) {
		if (user_stack_base) {
			child_ptregs->r12 = user_stack_base + user_stack_size;
			child_ptregs->ar_bspstore = user_stack_base;
			child_ptregs->ar_rnat = 0;
			child_ptregs->loadrs = 0;
		}
	} else {
		/*
		 * Note: we simply preserve the relative position of
		 * the stack pointer here.  There is no need to
		 * allocate a scratch area here, since that will have
		 * been taken care of by the caller of sys_clone()
		 * already.
		 */
		child_ptregs->r12 = (unsigned long) (child_ptregs + 1); /* kernel sp */
		child_ptregs->r13 = (unsigned long) p;		/* set `current' pointer */
	}
	if (IS_IA32_PROCESS(regs))
		child_stack->b0 = (unsigned long) &ia32_ret_from_clone;
	else
		child_stack->b0 = (unsigned long) &ia64_ret_from_clone;
	child_stack->ar_bspstore = child_rbs + rbs_size;

	/* copy parts of thread_struct: */
	p->thread.ksp = (unsigned long) child_stack - 16;
	/*
	 * NOTE: The calling convention considers all floating point
	 * registers in the high partition (fph) to be scratch.  Since
	 * the only way to get to this point is through a system call,
	 * we know that the values in fph are all dead.  Hence, there
	 * is no need to inherit the fph state from the parent to the
	 * child and all we have to do is to make sure that
	 * IA64_THREAD_FPH_VALID is cleared in the child.
	 *
	 * XXX We could push this optimization a bit further by
	 * clearing IA64_THREAD_FPH_VALID on ANY system call.
	 * However, it's not clear this is worth doing.  Also, it
	 * would be a slight deviation from the normal Linux system
	 * call behavior where scratch registers are preserved across
	 * system calls (unless used by the system call itself).
	 */
#	define THREAD_FLAGS_TO_CLEAR	(IA64_THREAD_FPH_VALID | IA64_THREAD_DBG_VALID \
					 | IA64_THREAD_PM_VALID)
#	define THREAD_FLAGS_TO_SET	0
	p->thread.flags = ((current->thread.flags & ~THREAD_FLAGS_TO_CLEAR)
			   | THREAD_FLAGS_TO_SET);
#ifdef CONFIG_IA32_SUPPORT
	/*
	 * If we're cloning an IA32 task then save the IA32 extra
	 * state from the current task to the new task
	 */
	if (IS_IA32_PROCESS(ia64_task_regs(current)))
		ia32_save_state(p);
#endif
#ifdef CONFIG_PERFMON
	if (p->thread.pfm_context)
		retval = pfm_inherit(p, child_ptregs);
#endif
	return retval;
}

void
do_copy_regs (struct unw_frame_info *info, void *arg)
{
	unsigned long mask, sp, nat_bits = 0, ip, ar_rnat, urbs_end, cfm;
	elf_greg_t *dst = arg;
	struct pt_regs *pt;
	char nat;
	int i;

	memset(dst, 0, sizeof(elf_gregset_t));	/* don't leak any kernel bits to user-level */

	if (unw_unwind_to_user(info) < 0)
		return;

	unw_get_sp(info, &sp);
	pt = (struct pt_regs *) (sp + 16);

	urbs_end = ia64_get_user_rbs_end(current, pt, &cfm);

	if (ia64_sync_user_rbs(current, info->sw, pt->ar_bspstore, urbs_end) < 0)
		return;

	ia64_peek(current, info->sw, urbs_end, (long) ia64_rse_rnat_addr((long *) urbs_end),
		  &ar_rnat);

	/*
	 * coredump format:
	 *	r0-r31
	 *	NaT bits (for r0-r31; bit N == 1 iff rN is a NaT)
	 *	predicate registers (p0-p63)
	 *	b0-b7
	 *	ip cfm user-mask
	 *	ar.rsc ar.bsp ar.bspstore ar.rnat
	 *	ar.ccv ar.unat ar.fpsr ar.pfs ar.lc ar.ec
	 */

	/* r0 is zero */
	for (i = 1, mask = (1UL << i); i < 32; ++i) {
		unw_get_gr(info, i, &dst[i], &nat);
		if (nat)
			nat_bits |= mask;
		mask <<= 1;
	}
	dst[32] = nat_bits;
	unw_get_pr(info, &dst[33]);

	for (i = 0; i < 8; ++i)
		unw_get_br(info, i, &dst[34 + i]);

	unw_get_rp(info, &ip);
	dst[42] = ip + ia64_psr(pt)->ri;
	dst[43] = cfm;
	dst[44] = pt->cr_ipsr & IA64_PSR_UM;

	unw_get_ar(info, UNW_AR_RSC, &dst[45]);
	/*
	 * For bsp and bspstore, unw_get_ar() would return the kernel
	 * addresses, but we need the user-level addresses instead:
	 */
	dst[46] = urbs_end;	/* note: by convention PT_AR_BSP points to the end of the urbs! */
	dst[47] = pt->ar_bspstore;
	dst[48] = ar_rnat;
	unw_get_ar(info, UNW_AR_CCV, &dst[49]);
	unw_get_ar(info, UNW_AR_UNAT, &dst[50]);
	unw_get_ar(info, UNW_AR_FPSR, &dst[51]);
	dst[52] = pt->ar_pfs;	/* UNW_AR_PFS is == to pt->cr_ifs for interrupt frames */
	unw_get_ar(info, UNW_AR_LC, &dst[53]);
	unw_get_ar(info, UNW_AR_EC, &dst[54]);
}

void
do_dump_fpu (struct unw_frame_info *info, void *arg)
{
	elf_fpreg_t *dst = arg;
	int i;

	memset(dst, 0, sizeof(elf_fpregset_t));	/* don't leak any "random" bits */

	if (unw_unwind_to_user(info) < 0)
		return;

	/* f0 is 0.0, f1 is 1.0 */

	for (i = 2; i < 32; ++i)
		unw_get_fr(info, i, dst + i);

	ia64_flush_fph(current);
	if ((current->thread.flags & IA64_THREAD_FPH_VALID) != 0)
		memcpy(dst + 32, current->thread.fph, 96*16);
}

void
ia64_elf_core_copy_regs (struct pt_regs *pt, elf_gregset_t dst)
{
	unw_init_running(do_copy_regs, dst);
}

int
dump_fpu (struct pt_regs *pt, elf_fpregset_t dst)
{
	unw_init_running(do_dump_fpu, dst);
	return 1;	/* f0-f31 are always valid so we always return 1 */
}

asmlinkage long
sys_execve (char *filename, char **argv, char **envp, struct pt_regs *regs)
{
	int error;

	filename = getname(filename);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename, argv, envp, regs);
	putname(filename);
out:
	return error;
}

pid_t
kernel_thread (int (*fn)(void *), void *arg, unsigned long flags)
{
	struct task_struct *parent = current;
	int result, tid;

	tid = clone(flags | CLONE_VM, 0);
	if (parent != current) {
		result = (*fn)(arg);
		_exit(result);
	}
	return tid;
}

/*
 * Flush thread state.  This is called when a thread does an execve().
 */
void
flush_thread (void)
{
	/* drop floating-point and debug-register state if it exists: */
	current->thread.flags &= ~(IA64_THREAD_FPH_VALID | IA64_THREAD_DBG_VALID);

#ifndef CONFIG_SMP
	if (ia64_get_fpu_owner() == current)
		ia64_set_fpu_owner(0);
#endif
}

#ifdef CONFIG_PERFMON
/*
 * By the time we get here, the task is detached from the tasklist. This is important
 * because it means that no other tasks can ever find it as a notifiied task, therfore
 * there is no race condition between this code and let's say a pfm_context_create().
 * Conversely, the pfm_cleanup_notifiers() cannot try to access a task's pfm context if
 * this other task is in the middle of its own pfm_context_exit() because it would alreayd
 * be out of the task list. Note that this case is very unlikely between a direct child
 * and its parents (if it is the notified process) because of the way the exit is notified
 * via SIGCHLD.
 */
void
release_thread (struct task_struct *task)
{
	if (task->thread.pfm_context)
		pfm_context_exit(task);

	if (atomic_read(&task->thread.pfm_notifiers_check) > 0)
		pfm_cleanup_notifiers(task);
}
#endif

/*
 * Clean up state associated with current thread.  This is called when
 * the thread calls exit().
 */
void
exit_thread (void)
{
#ifndef CONFIG_SMP
	if (ia64_get_fpu_owner() == current)
		ia64_set_fpu_owner(0);
#endif
#ifdef CONFIG_PERFMON
       /* stop monitoring */
	if ((current->thread.flags & IA64_THREAD_PM_VALID) != 0) {
		/*
		 * we cannot rely on switch_to() to save the PMU
		 * context for the last time. There is a possible race
		 * condition in SMP mode between the child and the
		 * parent.  by explicitly saving the PMU context here
		 * we garantee no race.  this call we also stop
		 * monitoring
		 */
		pfm_flush_regs(current);
		/*
		 * make sure that switch_to() will not save context again
		 */
		current->thread.flags &= ~IA64_THREAD_PM_VALID;
	}
#endif
}

unsigned long
get_wchan (struct task_struct *p)
{
	struct unw_frame_info info;
	unsigned long ip;
	int count = 0;
	/*
	 * These bracket the sleeping functions..
	 */
	extern void scheduling_functions_start_here(void);
	extern void scheduling_functions_end_here(void);
#	define first_sched	((unsigned long) scheduling_functions_start_here)
#	define last_sched	((unsigned long) scheduling_functions_end_here)

	/*
	 * Note: p may not be a blocked task (it could be current or
	 * another process running on some other CPU.  Rather than
	 * trying to determine if p is really blocked, we just assume
	 * it's blocked and rely on the unwind routines to fail
	 * gracefully if the process wasn't really blocked after all.
	 * --davidm 99/12/15
	 */
	unw_init_from_blocked_task(&info, p);
	do {
		if (unw_unwind(&info) < 0)
			return 0;
		unw_get_ip(&info, &ip);
		if (ip < first_sched || ip >= last_sched)
			return ip;
	} while (count++ < 16);
	return 0;
#	undef first_sched
#	undef last_sched
}

void
cpu_halt (void)
{
	pal_power_mgmt_info_u_t power_info[8];
	unsigned long min_power;
	int i, min_power_state;

	if (ia64_pal_halt_info(power_info) != 0)
		return;

	min_power_state = 0;
	min_power = power_info[0].pal_power_mgmt_info_s.power_consumption;
	for (i = 1; i < 8; ++i)
		if (power_info[i].pal_power_mgmt_info_s.im
		    && power_info[i].pal_power_mgmt_info_s.power_consumption < min_power) {
			min_power = power_info[i].pal_power_mgmt_info_s.power_consumption;
			min_power_state = i;
		}

	while (1)
		ia64_pal_halt(min_power_state);
}

void
machine_restart (char *restart_cmd)
{
	(*efi.reset_system)(EFI_RESET_WARM, 0, 0, 0);
}

void
machine_halt (void)
{
	cpu_halt();
}

void
machine_power_off (void)
{
	if (pm_power_off)
		pm_power_off();
	machine_halt();
}
