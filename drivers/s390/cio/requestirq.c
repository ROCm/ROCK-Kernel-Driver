/*
 *  drivers/s390/cio/requestirq.c
 *   S/390 common I/O routines -- enabling and disabling of devices
 *   $Revision: 1.46 $
 *
 *    Copyright (C) 1999-2002 IBM Deutschland Entwicklung GmbH,
 *			      IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 *		 Cornelia Huck (cohuck@de.ibm.com)
 *		 Arnd Bergmann (arndb@de.ibm.com)
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/device.h>
#include <linux/init.h>
#include <asm/lowcore.h>

#include "css.h"

struct pgid global_pgid;
EXPORT_SYMBOL_GPL(global_pgid);

/*
 * init_IRQ is now only used to set the pgid as early as possible
 */
void __init
init_IRQ(void)
{
	/*
	 * Let's build our path group ID here.
	 */
	if (MACHINE_NEW_STIDP)
		global_pgid.cpu_addr = 0x8000;
	else {
#ifdef CONFIG_SMP
		global_pgid.cpu_addr = hard_smp_processor_id();
#else
		global_pgid.cpu_addr = 0;
#endif
	}
	global_pgid.cpu_id = ((cpuid_t *) __LC_CPUID)->ident;
	global_pgid.cpu_model = ((cpuid_t *) __LC_CPUID)->machine;
	global_pgid.tod_high = (__u32) (get_clock() >> 32);
}
