/*
 * Copyright 2002 Momentum Computer Inc.
 * Author: Matthew Dharm <mdharm@momenco.com>
 *
 * Based on Ocelot Linux port, which is
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/bootmem.h>

#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/mv64340.h>

#include "ocelot_c_fpga.h"

struct callvectors {
	int	(*open) (char*, int, int);
	int	(*close) (int);
	int	(*read) (int, void*, int);
	int	(*write) (int, void*, int);
	off_t	(*lseek) (int, off_t, int);
	int	(*printf) (const char*, ...);
	void	(*cacheflush) (void);
	char*	(*gets) (char*);
};

struct callvectors* debug_vectors;
char arcs_cmdline[CL_SIZE];

extern unsigned long mv64340_base;
extern unsigned long cpu_clock;

#ifdef CONFIG_MV64340_ETH
extern unsigned char prom_mac_addr_base[6];
#endif

const char *get_system_type(void)
{
#ifdef CONFIG_CPU_SR71000
	return "Momentum Ocelot-CS";
#else
	return "Momentum Ocelot-C";
#endif
}

#ifdef CONFIG_MV64340_ETH
static void burn_clocks(void)
{
	int i;

	/* this loop should burn at least 1us -- this should be plenty */
	for (i = 0; i < 0x10000; i++)
		;
}

static u8 exchange_bit(u8 val, u8 cs)
{
	/* place the data */
	OCELOT_FPGA_WRITE((val << 2) | cs, EEPROM_MODE);
	burn_clocks();

	/* turn the clock on */
	OCELOT_FPGA_WRITE((val << 2) | cs | 0x2, EEPROM_MODE);
	burn_clocks();

	/* turn the clock off and read-strobe */
	OCELOT_FPGA_WRITE((val << 2) | cs | 0x10, EEPROM_MODE);
	
	/* return the data */
	return ((OCELOT_FPGA_READ(EEPROM_MODE) >> 3) & 0x1);
}

void get_mac(char dest[6])
{
	u8 read_opcode[12] = {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	int i,j;

	for (i = 0; i < 12; i++)
		exchange_bit(read_opcode[i], 1);

	for (j = 0; j < 6; j++) {
		dest[j] = 0;
		for (i = 0; i < 8; i++) {
			dest[j] <<= 1;
			dest[j] |= exchange_bit(0, 1);
		}
	}

	/* turn off CS */
	exchange_bit(0,0);
}
#endif

/* [jsun@junsun.net] PMON passes arguments in C main() style */
void __init prom_init(int argc, char **arg, char** env, struct callvectors *cv)
{
	int i;

	/* save the PROM vectors for debugging use */
	debug_vectors = cv;

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
	mips_machtype = MACH_MOMENCO_OCELOT_C;

	while (*env) {
		if (strncmp("gtbase", *env, strlen("gtbase")) == 0) {
			mv64340_base = simple_strtol(*env + strlen("gtbase="),
							NULL, 16);
		}
		if (strncmp("cpuclock", *env, strlen("cpuclock")) == 0) {
			cpu_clock = simple_strtol(*env + strlen("cpuclock="),
							NULL, 10);
		}
		env++;
	}

#ifdef CONFIG_MV64340_ETH
	/* get the base MAC address for on-board ethernet ports */
	get_mac(prom_mac_addr_base);
#endif

	debug_vectors->printf("Booting Linux kernel...\n");
}

void __init prom_free_prom_memory(void)
{
}

void __init prom_fixup_mem_map(unsigned long start, unsigned long end)
{
}
