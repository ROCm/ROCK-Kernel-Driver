/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/bootmem.h>

#include <asm/addrspace.h>
#include <asm/bootinfo.h>

#define PLD_BASE	0xbc000000

#define REV             0x0     /* Board Assembly Revision */
#define PLD1ID          0x1     /* PLD 1 ID */
#define PLD2ID          0x2     /* PLD 2 ID */
#define RESET_STAT      0x3     /* Reset Status Register */
#define BOARD_STAT      0x4     /* Board Status Register */
#define CPCI_ID         0x5     /* Compact PCI ID Register */
#define CONTROL         0x8     /* Control Register */
#define CPU_EEPROM      0x9     /* CPU Configuration EEPROM Register */
#define INTMASK         0xA     /* Interrupt Mask Register */
#define INTSTAT         0xB     /* Interrupt Status Register */
#define INTSET          0xC     /* Interrupt Set Register */
#define INTCLR          0xD     /* Interrupt Clear Register */

#define PLD_REG(x)	((uint8_t*)(PLD_BASE+(x)))

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

	mips_machgroup = MACH_GROUP_MOMENCO;
	mips_machtype = MACH_MOMENCO_OCELOT;

	/* turn off the Bit Error LED, which comes on automatically
	 * at power-up reset */
	*PLD_REG(INTCLR) = 0x80;

	/* All the boards have at least 64MiB. If there's more, we
	   detect and register it later */
	add_memory_region(0, 64 << 20, BOOT_MEM_RAM);
}

void __init prom_free_prom_memory(void)
{
}

void __init prom_fixup_mem_map(unsigned long start, unsigned long end)
{
}
