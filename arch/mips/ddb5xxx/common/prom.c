/***********************************************************************
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * arch/mips/ddb5xxx/common/prom.c
 *     prom.c file.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 ***********************************************************************
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/bootmem.h>

#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/ddb5xxx/ddb5xxx.h>

char arcs_cmdline[COMMAND_LINE_SIZE];

/* [jsun@junsun.net] PMON passes arguments in C main() style */
void __init prom_init(int argc, const char **arg)
{
	int i;

	/* arg[0] is "g", the rest is boot parameters */
	arcs_cmdline[0] = '\0';
	for (i = 1; i < argc; i++) {
		if (strlen(arcs_cmdline) + strlen(arg[i] + 1)
		    >= sizeof(arcs_cmdline))
			break;
		strcat(arcs_cmdline, arg[i]);
		strcat(arcs_cmdline, " ");
	}

	mips_machgroup = MACH_GROUP_NEC_DDB;

#if defined(CONFIG_DDB5074)
	mips_machtype = MACH_NEC_DDB5074;
#elif defined(CONFIG_DDB5476)
	mips_machtype = MACH_NEC_DDB5476;
#elif defined(CONFIG_DDB5477)
	mips_machtype = MACH_NEC_DDB5477;
#endif

	add_memory_region(0, DDB_SDRAM_SIZE, BOOT_MEM_RAM);
}

void __init prom_free_prom_memory(void)
{
}
