/*
 *  arch/mips/ddb5074/prom.c -- NEC DDB Vrc-5074 PROM routines
 *
 *  Copyright (C) 2000 Geert Uytterhoeven <geert@sonycom.com>
 *                     Sony Software Development Center Europe (SDCE), Brussels
 */
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/bootmem.h>

#include <asm/addrspace.h>
#include <asm/bootinfo.h>


char arcs_cmdline[COMMAND_LINE_SIZE];

void __init prom_init(const char *s)
{
	int i = 0;

	if (s != (void *) -1)
		while (*s && i < sizeof(arcs_cmdline) - 1)
			arcs_cmdline[i++] = *s++;
	arcs_cmdline[i] = '\0';

	mips_machgroup = MACH_GROUP_NEC_DDB;
	mips_machtype = MACH_NEC_DDB5074;

	/* 64 MB non-upgradable */
	add_memory_region(0, 64 << 20, BOOT_MEM_RAM);
}

void __init prom_free_prom_memory(void)
{
}
