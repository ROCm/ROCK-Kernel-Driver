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

/* ppc */
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
	trace_syscall_event.syscall_id = (uint8_t) regs->gpr[0];

	/* Set the address in any case */
	trace_syscall_event.address  = instruction_pointer(regs);

	/* Are we in the kernel (This is a kernel thread)? */
	if(!user_mode(regs))
	  /* Don't go digining anywhere */
	  goto trace_syscall_end;

	/* Get the trace configuration */
	if(ltt_get_trace_config(&use_depth,
			    &use_bounds,
			    &seek_depth,
			    (void*)&lower_bound,
			    (void*)&upper_bound) < 0)
	  goto trace_syscall_end;

	/* Do we have to search for an eip address range */
	if((use_depth == 1) || (use_bounds == 1))
	  {
	  /* Start at the top of the stack (bottom address since stacks grow downward) */
	  stack = (unsigned long*) regs->gpr[1];

	  /* Skip over first stack frame as the return address isn't valid */
	  if(get_user(addr, stack))
	    goto trace_syscall_end;
	  stack = (unsigned long*) addr;

	  /* Keep on going until we reach the end of the process' stack limit (wherever it may be) */
	  while(!get_user(addr, stack + 1)) /* "stack + 1", since this is where the IP is */
	    {
	    /* Does this LOOK LIKE an address in the program */
	    if((addr > current->mm->start_code)
             &&(addr < current->mm->end_code))
	      {
	      /* Does this address fit the description */
	      if(((use_depth == 1) && (depth == seek_depth))
               ||((use_bounds == 1) && (addr > lower_bound) && (addr < upper_bound)))
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
	    if(get_user(addr, stack))
	      goto trace_syscall_end;
	    stack = (unsigned long*) addr;
	    }
	  }

trace_syscall_end:
	/* Trace the event */
	ltt_log_event(TRACE_EV_SYSCALL_ENTRY, &trace_syscall_event);
}
