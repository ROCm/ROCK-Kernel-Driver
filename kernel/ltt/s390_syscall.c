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

/* s390 */
asmlinkage void ltt_pre_syscall(struct pt_regs * regs)
{                                                              
	int		    use_depth;	                 
        int                 use_bounds;                        
        int                 depth = 0;                         
        int                 seek_depth;                        
        unsigned long       lower_bound;                       
        unsigned long       upper_bound;                       
        unsigned long       addr;                              
        unsigned long*      stack;
        unsigned long       temp_stack;
        trace_syscall_entry trace_syscall_event;               
        /* Set the syscall ID                               */ 
        /* Register 8 is setup just prior to the call       */ 
        /* This instruction is just following linkage       */ 
        /* so it's ok.  If moved and chance of R8 being     */ 
        /* clobbered, would need to dig it out of the stack */ 
        __asm__ volatile(                                      
        "  stc  8,%0\n\t"                                      
        : "=m" (trace_syscall_event.syscall_id));              
        /* get the psw address */                              
        trace_syscall_event.address  = regs->psw.addr;         
        /* and off the hi-order bit */                                          
        trace_syscall_event.address &= PSW_ADDR_MASK;                           
        if(!(user_mode(regs))) /* if kernel mode, return */                     
           goto trace_syscall_end;                                              
        /* Get the trace configuration - if none, return */                     
        if(ltt_get_trace_config(&use_depth,                                         
                            &use_bounds,                                        
                            &seek_depth,                                        
                            (void*)&lower_bound,                                
                            (void*)&upper_bound) < 0)                           
          goto trace_syscall_end;                                               
        /* Do we have to search for an instruction pointer address range */     
        if((use_depth == 1) || (use_bounds == 1))                               
        {                                                                       
          /* Start at the top of the stack */                                   
          /* stack pointer is register 15 */                                    
          stack = (unsigned long*) regs->gprs[15]; /* stack pointer */      
          /* Keep on going until we reach the end of the process' stack limit */
          do
          {
            get_user(addr,stack+14);  /* get the program address +0x38 */ 
            /* and off the hi-order bit */
            addr &= PSW_ADDR_MASK;                                
            /* Does this LOOK LIKE an address in the program */
            if ((addr > current->mm->start_code)               
               &&(addr < current->mm->end_code))               
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
            get_user(temp_stack,stack); /* get contents of stack */
            temp_stack &= PSW_ADDR_MASK; /* and off hi order bit */
            stack = (unsigned long *)temp_stack; /* move into stack */
            /* stack may or may not go to zero when end hit               */
            /* using 0x7fffffff-_STK_LIM to validate that the address is  */
            /* within the range of a valid stack address                  */
            /* If outside that range, exit the loop, stack end must have  */
            /* been hit.                                                  */
          } while (stack >= (unsigned long *)(0x7fffffff-_STK_LIM));
        }                                                         
trace_syscall_end:                                                
        /* Trace the event */                                     
        ltt_log_event(TRACE_EV_SYSCALL_ENTRY, &trace_syscall_event);
}                                                                 
