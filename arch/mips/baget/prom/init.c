/*
 * init.c: PROM library initialisation code.
 *
 * Copyright (C) 1998 Gleb Raiko & Vladimir Roganov
 */
#include <linux/init.h>
#include <asm/addrspace.h>
#include <asm/bootinfo.h>

const char *get_system_type(void)
{
	/* Should probably return one of "BT23-201", "BT23-202" */
	return "Baget";
}

void __init prom_init(void)
{
	mem_upper = PHYSADDR(fw_arg0);

	mips_machgroup  = MACH_GROUP_UNKNOWN;
	mips_machtype   = MACH_UNKNOWN;
	arcs_cmdline[0] = 0;

	vac_memory_upper = mem_upper;

	add_memory_region(0, mem_upper, BOOT_MEM_RAM);
}

unsigned long __init prom_free_prom_memory(void)
{
	return 0;
}
