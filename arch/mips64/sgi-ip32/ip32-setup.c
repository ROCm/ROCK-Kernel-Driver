/*
 * IP32 basic setup
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Harald Koerfgen
 */
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/mc146818rtc.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <asm/mipsregs.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/ip32/crime.h>
#include <asm/ip32/mace.h>
#include <asm/ip32/ip32_ints.h>
#include <asm/sgialib.h>

extern struct rtc_ops ip32_rtc_ops;
extern u32 cc_interval;

void __init ip32_init (int argc, char **argv, char **envp) {
	arc_meminit ();
}

void __init ip32_setup(void)
{
#ifdef CONFIG_SERIAL_CONSOLE
	char *ctype;
#endif
	TLBMISS_HANDLER_SETUP ();

#ifdef CONFIG_SERIAL_CONSOLE
	ctype = ArcGetEnvironmentVariable("console");
	if (*ctype == 'd') {
		if (ctype[1] == '2')
			console_setup ("ttyS1");
		else
			console_setup ("ttyS0");
	}
#endif

#ifdef CONFIG_VT
	conswitchp = &dummy_con;
#endif

	rtc_ops = &ip32_rtc_ops;

	crime_init ();
}

int __init page_is_ram (unsigned long pagenr)
{
	/* XXX: to do? */
	return 1;
}
