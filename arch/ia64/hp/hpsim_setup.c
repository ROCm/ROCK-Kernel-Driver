/*
 * Platform dependent support for HP simulator.
 *
 * Copyright (C) 1998, 1999 Hewlett-Packard Co
 * Copyright (C) 1998, 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 Vijay Chander <vijay@engr.sgi.com>
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/console.h>

#include <asm/delay.h>
#include <asm/irq.h>
#include <asm/pal.h>
#include <asm/machvec.h>
#include <asm/pgtable.h>
#include <asm/sal.h>

#include "hpsim_ssc.h"

extern struct console hpsim_cons;

/*
 * Simulator system call.
 */
inline long
ia64_ssc (long arg0, long arg1, long arg2, long arg3, int nr)
{
#ifdef __GCC_DOESNT_KNOW_IN_REGS__
	register long in0 asm ("r32") = arg0;
	register long in1 asm ("r33") = arg1;
	register long in2 asm ("r34") = arg2;
	register long in3 asm ("r35") = arg3;
#else
	register long in0 asm ("in0") = arg0;
	register long in1 asm ("in1") = arg1;
	register long in2 asm ("in2") = arg2;
	register long in3 asm ("in3") = arg3;
#endif
	register long r8 asm ("r8");
	register long r15 asm ("r15") = nr;

	asm volatile ("break 0x80001"
		      : "=r"(r8)
		      : "r"(r15), "r"(in0), "r"(in1), "r"(in2), "r"(in3));
	return r8;
}

void
ia64_ssc_connect_irq (long intr, long irq)
{
	ia64_ssc(intr, irq, 0, 0, SSC_CONNECT_INTERRUPT);
}

void
ia64_ctl_trace (long on)
{
	ia64_ssc(on, 0, 0, 0, SSC_CTL_TRACE);
}

void __init
hpsim_setup (char **cmdline_p)
{
	ROOT_DEV = to_kdev_t(0x0801);		/* default to first SCSI drive */

	register_console (&hpsim_cons);
}
