/* $Id$
 *
 *  Copyright (C) 1995 Waldorf Electronics
 *  Copyright (C) 1997, 1999 Ralf Baechle
 */
#include <asm/bootinfo.h>

/*
 * This is included by init/main.c to check for architecture-dependent bugs.
 *
 * Needs:
 *	void check_bugs(void);
 */


static inline void check_wait(void)
{
	printk("Checking for 'wait' instruction... ");
	switch(mips_cputype) {
	case CPU_R4200: 
	case CPU_R4300: 
	case CPU_R4600: 
	case CPU_R4700: 
	case CPU_R5000: 
	case CPU_NEVADA:
		wait_available = 1;
		printk(" available.\n");
		break;
	default:
		printk(" unavailable.\n");
		break;
	}
}

static void __init
check_bugs(void)
{
	check_wait();
}
