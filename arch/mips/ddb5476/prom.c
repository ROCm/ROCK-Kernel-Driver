/*
 *  arch/mips/ddb5476/prom.c -- NEC DDB Vrc-5476 PROM routines
 *
 *  Copyright (C) 2000 Geert Uytterhoeven <geert@sonycom.com>
 *                     Sony Software Development Center Europe (SDCE), Brussels
 *
 *   	Jun Sun - modified for DDB5476.
 */
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/bootmem.h>

#include <asm/addrspace.h>
#include <asm/bootinfo.h>


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
	mips_machtype = MACH_NEC_DDB5476;
	/* 64 MB non-upgradable */
	add_memory_region(0, 64 << 20, BOOT_MEM_RAM);
}

void __init prom_free_prom_memory(void)
{
}
