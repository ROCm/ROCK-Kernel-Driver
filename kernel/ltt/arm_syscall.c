/*
 * drivers/trace/xxx_syscall.c
 *
 * This code is distributed under the GPL license
 *
 * System call tracing.
 */
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <asm/uaccess.h>
#include <linux/ltt.h>

/* arm */
asmlinkage void ltt_pre_syscall(struct pt_regs * regs)
{
	int			scno = 0;
	int			depth = 0;
	unsigned long           end_code;
	unsigned long		*fp;			/* frame pointer */
	unsigned long		lower_bound;
	unsigned long		lr;			/* link register */
	unsigned long		*prev_fp;
	int			seek_depth;
	unsigned long           start_code;
	unsigned long           *start_stack;
	trace_syscall_entry	trace_syscall_event;
	unsigned long		upper_bound;
	int			use_bounds;
	int			use_depth;

	/* TODO: get_scno */
	trace_syscall_event.syscall_id = (uint8_t)scno;
	trace_syscall_event.address    = instruction_pointer(regs);
	
	if (! (user_mode(regs) ))
		goto trace_syscall_end;

	if (ltt_get_trace_config(&use_depth,
			     &use_bounds,
			     &seek_depth,
			     (void*)&lower_bound,
			     (void*)&upper_bound) < 0)
		goto trace_syscall_end;

	if ((use_depth == 1) || (use_bounds == 1)) {
		fp          = (unsigned long *)regs->ARM_fp;
		end_code    = current->mm->end_code;
		start_code  = current->mm->start_code;
		start_stack = (unsigned long *)current->mm->start_stack;

		while (!__get_user(lr, (unsigned long *)(fp - 1))) {
			if ((lr > start_code) && (lr < end_code)) {
				if (((use_depth == 1) && (depth >= seek_depth)) ||
				    ((use_bounds == 1) && (lr > lower_bound) && (lr < upper_bound))) {
					trace_syscall_event.address = lr;
					goto trace_syscall_end;
				} else {
					depth++;
				}
			}

			if ((__get_user((unsigned long)prev_fp, (fp - 3))) ||
			    (prev_fp > start_stack) ||
			    (prev_fp <= fp)) {
				goto trace_syscall_end;
			}
			fp = prev_fp;
		}
	}

trace_syscall_end:
	ltt_log_event(TRACE_EV_SYSCALL_ENTRY, &trace_syscall_event);
}
