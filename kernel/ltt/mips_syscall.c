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

/* mips */
asmlinkage void ltt_pre_syscall(struct pt_regs * regs)
{
	unsigned long       addr;
	int                 depth = 0;
	unsigned long       end_code;
	unsigned long       lower_bound;
	int                 seek_depth;
	unsigned long       *stack;
	unsigned long       start_code;
	unsigned long       *start_stack;
	trace_syscall_entry trace_syscall_event;
	unsigned long       upper_bound;
	int                 use_bounds;
	int                 use_depth;

	/* syscall_id will be negative for SVR4, IRIX5, BSD43, and POSIX
	 * syscalls -- these are not supported at this point by LTT
	 */
	trace_syscall_event.syscall_id = (uint8_t) (regs->regs[2] - __NR_Linux);

	trace_syscall_event.address  = regs->cp0_epc;

	if (!user_mode(regs))
		goto trace_syscall_end;

	if (ltt_get_trace_config(&use_depth,
			     &use_bounds,
			     &seek_depth,
			     (void*)&lower_bound,
			     (void*)&upper_bound) < 0)
		goto trace_syscall_end;

	/* Heuristic that might work:
	 * (BUT DOESN'T WORK for any of the cases I tested...) zzz
	 * Search through stack until a value is found that is within the
	 * range start_code .. end_code.  (This is looking for a return
	 * pointer to where a shared library was called from.)  If a stack
	 * variable contains a valid code address then an incorrect
	 * result will be generated.
	 */
	if ((use_depth == 1) || (use_bounds == 1)) {
		stack       = (unsigned long*) regs->regs[29];
		end_code    = current->mm->end_code;
		start_code  = current->mm->start_code;
		start_stack = (unsigned long *)current->mm->start_stack;

		while ((stack <= start_stack) && (!__get_user(addr, stack))) {
			if ((addr > start_code) && (addr < end_code)) {
				if (((use_depth  == 1) && (depth == seek_depth)) ||
				    ((use_bounds == 1) && (addr > lower_bound) && (addr < upper_bound))) {
					trace_syscall_event.address = addr;
					goto trace_syscall_end;
				} else {
					depth++;
				}
			}
		stack++;
		}
	}

trace_syscall_end:
	ltt_log_event(TRACE_EV_SYSCALL_ENTRY, &trace_syscall_event);
}
