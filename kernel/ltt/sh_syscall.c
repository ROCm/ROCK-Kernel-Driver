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

/* sh */
asmlinkage void ltt_pre_syscall(struct pt_regs * regs)
{
	int                 use_depth;
	int                 use_bounds;
	int                 depth = 0;
	int                 seek_depth;
	unsigned long       lower_bound;
	unsigned long       upper_bound;
	unsigned long       addr;
	unsigned long*      stack;
	trace_syscall_entry trace_syscall_event;

	/* Set the syscall ID */
	trace_syscall_event.syscall_id = (uint8_t) regs->regs[REG_REG0+3];

	/* Set the address in any case */
	trace_syscall_event.address  = regs->pc;

	/* Are we in the kernel (This is a kernel thread)? */
	if(!user_mode(regs))
		/* Don't go digining anywhere */
		goto trace_syscall_end;

	/* Get the trace configuration */
	if(ltt_get_trace_config(&use_depth, &use_bounds, &seek_depth,
	   (void*)&lower_bound, (void*)&upper_bound) < 0)
		goto trace_syscall_end;

	/* Do we have to search for an eip address range */
	if((use_depth == 1) || (use_bounds == 1))
	{
		/* Start at the top of the stack (bottom address since stacks grow downward) */
		stack = (unsigned long*) regs->regs[REG_REG15];

		/* Keep on going until we reach the end of the process' stack limit (wherever it may be) */
		while(!get_user(addr, stack))
		{
			/* Does this LOOK LIKE an address in the program */
			/* TODO: does this work with shared libraries?? - Greg Banks */
			if((addr > current->mm->start_code) &&(addr < current->mm->end_code))
			{
				/* Does this address fit the description */
				if(((use_depth == 1) && (depth == seek_depth))
				   ||((use_bounds == 1) && (addr > lower_bound)
				   && (addr < upper_bound)))
				{
					/* Set the address */
					trace_syscall_event.address = addr;

					/* We're done */
					goto trace_syscall_end;
				}
				else
					/* We're one depth more */
					depth++;
			}
			/* Go on to the next address */
			stack++;
		}
	}

trace_syscall_end:
	/* Trace the event */
	ltt_log_event(TRACE_EV_SYSCALL_ENTRY, &trace_syscall_event);
}
