/*
 *  linux/arch/mips/philips/nino/prom.c
 *
 *  Copyright (C) 2001 Steven J. Hill (sjhill@realitydiluted.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *  
 *  Early initialization code for the Philips Nino.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/bootinfo.h>
#include <asm/addrspace.h>
#include <asm/page.h>

char arcs_cmdline[COMMAND_LINE_SIZE];

#ifdef CONFIG_FB_TX3912
extern u_long tx3912fb_paddr;
extern u_long tx3912fb_vaddr;
extern u_long tx3912fb_size;
#endif

/* Do basic initialization */
void __init prom_init(int argc, char **argv,
		unsigned long magic, int *prom_vec)
{
	u_long free_end, mem_size;
	u_int i;

	/*
	 * collect args and prepare cmd_line
	 */
	for (i = 1; i < argc; i++) {
		strcat(arcs_cmdline, argv[i]);
		if (i < (argc - 1))
			strcat(arcs_cmdline, " ");
	}

	mips_machgroup = MACH_GROUP_PHILIPS;
	mips_machtype = MACH_PHILIPS_NINO;

#ifdef CONFIG_NINO_4MB
	mem_size = 4 << 20;
#elif CONFIG_NINO_8MB
	mem_size = 8 << 20;
#elif CONFIG_NINO_16MB
	mem_size = 16 << 20;
#endif

#ifdef CONFIG_FB_TX3912
	/*
	 * The LCD controller requires that the framebuffer
	 * start address fall within a 1MB segment and is
	 * aligned on a 16 byte boundary. The way to assure
	 * this is to place the framebuffer at the end of
	 * memory and mark it as reserved.
	 */
	free_end = (mem_size - tx3912fb_size) & PAGE_MASK;
	add_memory_region(0, free_end, BOOT_MEM_RAM); 
	add_memory_region(free_end, (mem_size - free_end), BOOT_MEM_RESERVED); 

	/*
	 * Calculate physical and virtual addresses for
	 * the beginning of the framebuffer.
	 */
	tx3912fb_paddr = PHYSADDR(free_end);
	tx3912fb_vaddr = KSEG1ADDR(free_end);
#else
	add_memory_region(0, mem_size, BOOT_MEM_RAM); 
#endif
}

void __init prom_free_prom_memory (void)
{
}
