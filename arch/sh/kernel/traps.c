/* $Id: traps.c,v 1.5 2000/02/27 08:27:55 gniibe Exp $
 *
 *  linux/arch/sh/traps.c
 *
 *  SuperH version: Copyright (C) 1999  Niibe Yutaka
 */

/*
 * 'Traps.c' handles hardware traps and faults after we have saved some
 * state in 'entry.S'.
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/spinlock.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include <asm/processor.h>

#define DO_ERROR(trapnr, signr, str, name, tsk) \
asmlinkage void do_##name(unsigned long r4, unsigned long r5, \
			  unsigned long r6, unsigned long r7, \
			  struct pt_regs regs) \
{ \
	unsigned long error_code; \
 \
	asm volatile("stc	$r2_bank, %0": "=r" (error_code)); \
	sti(); \
	tsk->thread.error_code = error_code; \
	tsk->thread.trap_no = trapnr; \
	force_sig(signr, tsk); \
	die_if_no_fixup(str,&regs,error_code); \
}

/*
 * These constants are for searching for possible module text
 * segments.  VMALLOC_OFFSET comes from mm/vmalloc.c; MODULE_RANGE is
 * a guess of how much space is likely to be vmalloced.
 */
#define VMALLOC_OFFSET (8*1024*1024)
#define MODULE_RANGE (8*1024*1024)

spinlock_t die_lock;

void die(const char * str, struct pt_regs * regs, long err)
{
	console_verbose();
	spin_lock_irq(&die_lock);
	printk("%s: %04lx\n", str, err & 0xffff);
	show_regs(regs);
	spin_unlock_irq(&die_lock);
	do_exit(SIGSEGV);
}

static inline void die_if_kernel(const char * str, struct pt_regs * regs, long err)
{
	if (!user_mode(regs))
		die(str, regs, err);
}

static void die_if_no_fixup(const char * str, struct pt_regs * regs, long err)
{
	if (!user_mode(regs))
	{
		unsigned long fixup;
		fixup = search_exception_table(regs->pc);
		if (fixup) {
			regs->pc = fixup;
			return;
		}
		die(str, regs, err);
	}
}

DO_ERROR( 7, SIGSEGV, "address error (load)", address_error_load, current)
DO_ERROR( 8, SIGSEGV, "address error (store)", address_error_store, current)
DO_ERROR(12, SIGILL,  "reserved instruction", reserved_inst, current)
DO_ERROR(13, SIGILL,  "illegal slot instruction", illegal_slot_inst, current)

asmlinkage void do_exception_error(unsigned long r4, unsigned long r5,
				   unsigned long r6, unsigned long r7,
				   struct pt_regs regs)
{
	long ex;
	asm volatile("stc	$r2_bank, %0" : "=r" (ex));
	die_if_kernel("exception", &regs, ex);
}

void __init trap_init(void)
{
	extern void *vbr_base;
	extern void *exception_handling_table[14];

	exception_handling_table[7] = (void *)do_address_error_load;
	exception_handling_table[8] = (void *)do_address_error_store;
	exception_handling_table[12] = (void *)do_reserved_inst;
	exception_handling_table[13] = (void *)do_illegal_slot_inst;

	/* NOTE: The VBR value should be at P1
	   (or P2, virtural "fixed" address space).
	   It's definitely should not in physical address.  */

	asm volatile("ldc	%0, $vbr"
		     : /* no output */
		     : "r" (&vbr_base)
		     : "memory");
}

void dump_stack(void)
{
	unsigned long *start;
	unsigned long *end;
	unsigned long *p;

	asm("mov	$r15, %0" : "=r" (start));
	asm("stc	$r7_bank, %0" : "=r" (end));
	end += 8192/4;

	printk("%08lx:%08lx\n", (unsigned long)start, (unsigned long)end);
	for (p=start; p < end; p++) {
		extern long _text, _etext;
		unsigned long v=*p;

		if ((v >= (unsigned long )&_text)
		    && (v <= (unsigned long )&_etext)) {
			printk("%08lx\n", v);
		}
	}
}
