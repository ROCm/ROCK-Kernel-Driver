/* $Id: traps.c,v 1.8 2001/02/23 13:45:20 bjornw Exp $
 *
 *  linux/arch/cris/traps.c
 *
 *  Here we handle the break vectors not used by the system call 
 *  mechanism, as well as some general stack/register dumping 
 *  things.
 * 
 *  Copyright (C) 2000,2001 Axis Communications AB
 *
 *  Authors:   Bjorn Wesen
 *             Orjan Friberg
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/timer.h>
#include <linux/mm.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <asm/pgtable.h>

int kstack_depth_to_print = 24;

/*
 * These constants are for searching for possible module text
 * segments. MODULE_RANGE is a guess of how much space is likely
 * to be vmalloced.
 */

#define MODULE_RANGE (8*1024*1024)

void 
show_stack(unsigned long *sp)
{
        unsigned long *stack, addr, module_start, module_end;
        int i;
	extern char _stext, _etext;

        // debugging aid: "show_stack(NULL);" prints the
        // back trace for this cpu.

        if(sp == NULL)
                sp = (unsigned long*)rdsp();

        stack = sp;

        for(i = 0; i < kstack_depth_to_print; i++) {
                if (((long) stack & (THREAD_SIZE-1)) == 0)
                        break;
                if (i && ((i % 8) == 0))
                        printk("\n       ");
                printk("%08lx ", *stack++);
        }

        printk("\nCall Trace: ");
        stack = sp;
        i = 1;
        module_start = VMALLOC_START;
        module_end = VMALLOC_END;
        while (((long) stack & (THREAD_SIZE-1)) != 0) {
                addr = *stack++;
                /*
                 * If the address is either in the text segment of the
                 * kernel, or in the region which contains vmalloc'ed
                 * memory, it *may* be the address of a calling
                 * routine; if so, print it so that someone tracing
                 * down the cause of the crash will be able to figure
                 * out the call path that was taken.
                 */
                if (((addr >= (unsigned long) &_stext) &&
                     (addr <= (unsigned long) &_etext)) ||
                    ((addr >= module_start) && (addr <= module_end))) {
                        if (i && ((i % 8) == 0))
                                printk("\n       ");
                        printk("[<%08lx>] ", addr);
                        i++;
                }
        }
}

#if 0
/* displays a short stack trace */

int 
show_stack()
{
	unsigned long *sp = (unsigned long *)rdusp();
	int i;
	printk("Stack dump [0x%08lx]:\n", (unsigned long)sp);
	for(i = 0; i < 16; i++)
		printk("sp + %d: 0x%08lx\n", i*4, sp[i]);
	return 0;
}
#endif

void 
show_registers(struct pt_regs * regs)
{
	unsigned long usp = rdusp();

	printk("IRP: %08lx SRP: %08lx CCR: %08lx USP: %08lx\n",
	       regs->irp, regs->srp, regs->dccr, usp );
	printk(" r0: %08lx  r1: %08lx  r2: %08lx  r3: %08lx\n",
	       regs->r0, regs->r1, regs->r2, regs->r3);
	printk(" r4: %08lx  r5: %08lx  r6: %08lx  r7: %08lx\n",
	       regs->r4, regs->r5, regs->r6, regs->r7);
	printk(" r8: %08lx  r9: %08lx r10: %08lx r11: %08lx\n",
	       regs->r8, regs->r9, regs->r10, regs->r11);
	printk("r12: %08lx r13: %08lx oR10: %08lx\n",
	       regs->r12, regs->r13, regs->orig_r10);
	printk("Process %s (pid: %d, stackpage=%08lx)\n",
	       current->comm, current->pid, (unsigned long)current);

	// TODO, fix in_kernel detection

#if 0
	/*
         * When in-kernel, we also print out the stack and code at the
         * time of the fault..
         */
        if (1) {
		
                printk("\nStack: ");
                show_stack((unsigned long*)usp);

                printk("\nCode: ");
                if(regs->irp < PAGE_OFFSET)
                        goto bad;

                for(i = 0; i < 20; i++)
                {
                        unsigned char c;
                        if(__get_user(c, &((unsigned char*)regs->irp)[i])) {
bad:
                                printk(" Bad IP value.");
                                break;
                        }
                        printk("%02x ", c);
                }
        }
        printk("\n");
#endif
}

void 
die_if_kernel(const char * str, struct pt_regs * regs, long err)
{
	if(user_mode(regs))
		return;

	printk("%s: %04lx\n", str, err & 0xffff);

	show_registers(regs);
	show_stack(NULL);  /* print backtrace for kernel stack on this CPU */

	do_exit(SIGSEGV);
}

void __init 
trap_init(void)
{

}

/* Use static variables instead of the stack for temporary storage.  */
static int saved_r0 = 0;
static int saved_dccr = 0;

asm ("
  .global _gdb_handle_breakpoint
  .global _do_sigtrap
_gdb_handle_breakpoint:
;;
;; This handles a break instruction for entering a debug session.
;;
  move     dccr,[_saved_dccr]       ; Save dccr.
  move.d   r0,[_saved_r0]           ; Save r0. "
#ifdef CONFIG_KGDB
" 
  move     ccr,r0                   
  btstq    8,r0                     ; Test the U-flag.
  bmi      _ugdb_handle_breakpoint  ; Go to user mode debugging.
  nop                               ; Delay slot.
  move.d   [_saved_r0],r0           ; Restore r0.
  move     [_saved_dccr],dccr       ; Restore dccr.
  ba       _kgdb_handle_breakpoint  ; Go to kernel debugging.
  nop                               ; Delay slot. "
#endif
" 
_ugdb_handle_breakpoint:
;;
;; Yes, we could do a 'push brp' here and let gdb adjust the pc once it
;; starts talking to the target again, but this way we avoid a 'P' packet.
;;
  move     brp,r0               ; Use r0 temporarily for calculation.
  subq     2,r0                 ; Set to address of previous instruction.
  move     r0,brp               ; Restore new brp.
  move.d   [_saved_r0],r0       ; Restore r0.
  move     [_saved_dccr],dccr   ; Restore dccr.

_do_sigtrap:
;; 
;; SIGTRAP the process that executed the break instruction.
;; Make a frame that Rexit in entry.S expects.
;;
  push     brp                  ; Push breakpoint return pointer.
  push     srp                  ; Push subroutine return pointer.
  push     dccr                 ; Push condition codes.
  push     mof                  ; Push multiply overflow reg.
  di                            ; Need to disable irq's at this point.
  subq     14*4,sp              ; Make room for r0-r13.
  movem    r13,[sp]             ; Push the r0-r13 registers.
  push     r10                  ; Push orig_r10.
  clear.d  [sp=sp-4]            ; Frametype - this is a normal stackframe.

  movs.w   -8192,r9             ; THREAD_SIZE == 8192
  and.d    sp,r9
  move.d   [r9+LTASK_PID],r10   ; current->pid as arg1.
  moveq    5,r11                ; SIGTRAP as arg2.
  jsr      _sys_kill       

  jump     _ret_from_intr       ; Use the return routine for interrupts.
");
