/*
 * Copyright (C) 2003  Maciej W. Rozycki
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/stddef.h>

#include <asm/bugs.h>
#include <asm/cpu.h>
#include <asm/fpu.h>
#include <asm/mipsregs.h>
#include <asm/system.h>

static inline void check_mult_sh(void)
{
	unsigned long flags;
	int m1, m2;
	long p, s, v;

	printk("Checking for the multiply/shift bug... ");

	local_irq_save(flags);
	/*
	 * The following code leads to a wrong result of dsll32 when
	 * executed on R4000 rev. 2.2 or 3.0.
	 *
	 * See "MIPS R4000PC/SC Errata, Processor Revision 2.2 and
	 * 3.0" by MIPS Technologies, Inc., errata #16 and #28 for
	 * details.  I got no permission to duplicate them here,
	 * sigh... --macro
	 */
	asm volatile(
		".set	push\n\t"
		".set	noat\n\t"
		".set	noreorder\n\t"
		".set	nomacro\n\t"
		"mult	%1, %2\n\t"
		"dsll32	%0, %3, %4\n\t"
		"mflo	$0\n\t"
		".set	pop"
		: "=r" (v)
		: "r" (5), "r" (8), "r" (5), "I" (0)
		: "hi", "lo", "accum");
	local_irq_restore(flags);

	if (v == 5L << 32) {
		printk("no.\n");
		return;
	}

	printk("yes, workaround... ");
	local_irq_save(flags);
	/*
	 * We want the multiply and the shift to be isolated from the
	 * rest of the code to disable gcc optimizations.  Hence the
	 * asm statements that execute nothing, but make gcc not know
	 * what the values of m1, m2 and s are and what v and p are
	 * used for.
	 *
	 * We have to use single integers for m1 and m2 and a double
	 * one for p to be sure the mulsidi3 gcc's RTL multiplication
	 * instruction has the workaround applied.  Older versions of
	 * gcc have correct mulsi3, but other multiplication variants
	 * lack the workaround.
	 */
	asm volatile(
		""
		: "=r" (m1), "=r" (m2), "=r" (s)
		: "0" (5), "1" (8), "2" (5));
	p = m1 * m2;
	v = s << 32;
	asm volatile(
		""
		: "=r" (v)
		: "0" (v), "r" (p));
	local_irq_restore(flags);

	if (v == 5L << 32) {
		printk("yes.\n");
		return;
	}

	printk("no.\n");
	panic("Reliable operation impossible!\n"
#ifndef CONFIG_CPU_R4000
	      "Configure for R4000 to enable the workaround."
#else
	      "Please report to <linux-mips@linux-mips.org>."
#endif
	      );
}

static volatile int daddi_ov __initdata = 0;

asmlinkage void __init do_daddi_ov(struct pt_regs *regs)
{
	daddi_ov = 1;
	regs->cp0_epc += 4;
}

static inline void check_daddi(void)
{
	extern asmlinkage void handle_daddi_ov(void);
	unsigned long flags;
	void *handler;
	long v;

	printk("Checking for the daddi bug... ");

	local_irq_save(flags);
	handler = set_except_vector(12, handle_daddi_ov);
	/*
	 * The following code fails to trigger an overflow exception
	 * when executed on R4000 rev. 2.2 or 3.0.
	 *
	 * See "MIPS R4000PC/SC Errata, Processor Revision 2.2 and
	 * 3.0" by MIPS Technologies, Inc., erratum #23 for details.
	 * I got no permission to duplicate it here, sigh... --macro
	 */
	asm volatile(
		".set	push\n\t"
		".set	noat\n\t"
		".set	noreorder\n\t"
		".set	nomacro\n\t"
#ifdef HAVE_AS_SET_DADDI
		".set	daddi\n\t"
#endif
		"daddi	%0, %1, %2\n\t"
		".set	pop"
		: "=r" (v)
		: "r" (0x7fffffffffffedcd), "I" (0x1234));
	set_except_vector(12, handler);
	local_irq_restore(flags);

	if (daddi_ov) {
		printk("no.\n");
		return;
	}

	printk("yes, workaround... ");

	local_irq_save(flags);
	handler = set_except_vector(12, handle_daddi_ov);
	asm volatile(
		"daddi	%0, %1, %2"
		: "=r" (v)
		: "r" (0x7fffffffffffedcd), "I" (0x1234));
	set_except_vector(12, handler);
	local_irq_restore(flags);

	if (daddi_ov) {
		printk("yes.\n");
		return;
	}

	printk("no.\n");
	panic("Reliable operation impossible!\n"
#if !defined(CONFIG_CPU_R4000) && !defined(CONFIG_CPU_R4400)
	      "Configure for R4000 or R4400 to enable the workaround."
#else
	      "Please report to <linux-mips@linux-mips.org>."
#endif
	      );
}

static inline void check_daddiu(void)
{
	long v, w;

	printk("Checking for the daddiu bug... ");

	/*
	 * The following code leads to a wrong result of daddiu when
	 * executed on R4400 rev. 1.0.
	 *
	 * See "MIPS R4400PC/SC Errata, Processor Revision 1.0" by
	 * MIPS Technologies, Inc., erratum #7 for details.
	 *
	 * According to "MIPS R4000PC/SC Errata, Processor Revision
	 * 2.2 and 3.0" by MIPS Technologies, Inc., erratum #41 this
	 * problem affects R4000 rev. 2.2 and 3.0, too.  Testing
	 * failed to trigger it so far.
	 *
	 * I got no permission to duplicate the errata here, sigh...
	 * --macro
	 */
	asm volatile(
		".set	push\n\t"
		".set	noat\n\t"
		".set	noreorder\n\t"
		".set	nomacro\n\t"
#ifdef HAVE_AS_SET_DADDI
		".set	daddi\n\t"
#endif
		"daddiu	%0, %2, %3\n\t"
		"addiu	%1, $0, %3\n\t"
		"daddu	%1, %2\n\t"
		".set	pop"
		: "=&r" (v), "=&r" (w)
		: "r" (0x7fffffffffffedcd), "I" (0x1234));

	if (v == w) {
		printk("no.\n");
		return;
	}

	printk("yes, workaround... ");

	asm volatile(
		"daddiu	%0, %2, %3\n\t"
		"addiu	%1, $0, %3\n\t"
		"daddu	%1, %2"
		: "=&r" (v), "=&r" (w)
		: "r" (0x7fffffffffffedcd), "I" (0x1234));

	if (v == w) {
		printk("yes.\n");
		return;
	}

	printk("no.\n");
	panic("Reliable operation impossible!\n"
#if !defined(CONFIG_CPU_R4000) && !defined(CONFIG_CPU_R4400)
	      "Configure for R4000 or R4400 to enable the workaround."
#else
	      "Please report to <linux-mips@linux-mips.org>."
#endif
	      );
}

void __init check_bugs64(void)
{
	check_mult_sh();
	check_daddi();
	check_daddiu();
}
