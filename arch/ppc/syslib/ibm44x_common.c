/*
 * arch/ppc/syslib/ibm44x_common.c
 *
 * PPC44x system library
 *
 * Matt Porter <mporter@mvista.com>
 * Copyright 2002-2003 MontaVista Software Inc.
 *
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 * Copyright (c) 2003, 2004 Zultys Technologies
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/serial.h>

#include <asm/param.h>
#include <asm/ibm44x.h>
#include <asm/mmu.h>
#include <asm/machdep.h>
#include <asm/time.h>
#include <asm/ppc4xx_pic.h>

phys_addr_t fixup_bigphys_addr(phys_addr_t addr, phys_addr_t size)
{
	phys_addr_t page_4gb = 0;

        /*
	 * Trap the least significant 32-bit portions of an
	 * address in the 440's 36-bit address space.  Fix
	 * them up with the appropriate ERPN
	 */
	if ((addr >= PPC44x_IO_LO) && (addr < PPC44x_IO_HI))
		page_4gb = PPC44x_IO_PAGE;
	else if ((addr >= PPC44x_PCICFG_LO) && (addr < PPC44x_PCICFG_HI))
		page_4gb = PPC44x_PCICFG_PAGE;
	else if ((addr >= PPC44x_PCIMEM_LO) && (addr < PPC44x_PCIMEM_HI))
		page_4gb = PPC44x_PCIMEM_PAGE;

	return (page_4gb | addr);
};

void __init ibm44x_calibrate_decr(unsigned int freq)
{
	tb_ticks_per_jiffy = freq / HZ;
	tb_to_us = mulhwu_scale_factor(freq, 1000000);

	/* Set the time base to zero */
	mtspr(SPRN_TBWL, 0);
	mtspr(SPRN_TBWU, 0);

	/* Clear any pending timer interrupts */
	mtspr(SPRN_TSR, TSR_ENW | TSR_WIS | TSR_DIS | TSR_FIS);

	/* Enable decrementer interrupt */
	mtspr(SPRN_TCR, TCR_DIE);
}

extern void abort(void);

static void ibm44x_restart(char *cmd)
{
	local_irq_disable();
	abort();
}

static void ibm44x_power_off(void)
{
	local_irq_disable();
	for(;;);
}

static void ibm44x_halt(void)
{
	local_irq_disable();
	for(;;);
}

/*
 * Read the 44x memory controller to get size of system memory.
 */
static unsigned long __init ibm44x_find_end_of_memory(void)
{
	u32 i, bank_config;
	u32 mem_size = 0;

	for (i=0; i<4; i++)
	{
		switch (i)
		{
			case 0:
				mtdcr(DCRN_SDRAM0_CFGADDR, SDRAM0_B0CR);
				break;
			case 1:
				mtdcr(DCRN_SDRAM0_CFGADDR, SDRAM0_B1CR);
				break;
			case 2:
				mtdcr(DCRN_SDRAM0_CFGADDR, SDRAM0_B2CR);
				break;
			case 3:
				mtdcr(DCRN_SDRAM0_CFGADDR, SDRAM0_B3CR);
				break;
		}

		bank_config = mfdcr(DCRN_SDRAM0_CFGDATA);

		if (!(bank_config & SDRAM_CONFIG_BANK_ENABLE))
			continue;
		switch (SDRAM_CONFIG_BANK_SIZE(bank_config))
		{
			case SDRAM_CONFIG_SIZE_8M:
				mem_size += PPC44x_MEM_SIZE_8M;
				break;
			case SDRAM_CONFIG_SIZE_16M:
				mem_size += PPC44x_MEM_SIZE_16M;
				break;
			case SDRAM_CONFIG_SIZE_32M:
				mem_size += PPC44x_MEM_SIZE_32M;
				break;
			case SDRAM_CONFIG_SIZE_64M:
				mem_size += PPC44x_MEM_SIZE_64M;
				break;
			case SDRAM_CONFIG_SIZE_128M:
				mem_size += PPC44x_MEM_SIZE_128M;
				break;
			case SDRAM_CONFIG_SIZE_256M:
				mem_size += PPC44x_MEM_SIZE_256M;
				break;
			case SDRAM_CONFIG_SIZE_512M:
				mem_size += PPC44x_MEM_SIZE_512M;
				break;
		}
	}
	return mem_size;
}

static void __init ibm44x_init_irq(void)
{
	int i;

	ppc4xx_pic_init();

	for (i = 0; i < NR_IRQS; i++)
		irq_desc[i].handler = ppc4xx_pic;
}

#ifdef CONFIG_SERIAL_TEXT_DEBUG
#include <linux/serialP.h>
#include <linux/serial_reg.h>
#include <asm/serial.h>

static struct serial_state rs_table[RS_TABLE_SIZE] = {
	SERIAL_PORT_DFNS	/* Defined in <asm/serial.h> */
};

static void ibm44x_progress(char *s, unsigned short hex)
{
	volatile char c;
	volatile unsigned long com_port;
	u16 shift;

	com_port = (unsigned long)rs_table[0].iomem_base;
	shift = rs_table[0].iomem_reg_shift;

	while ((c = *s++) != 0) {
		while ((*((volatile unsigned char *)com_port +
				(UART_LSR << shift)) & UART_LSR_THRE) == 0)
			;
		*(volatile unsigned char *)com_port = c;

	}

	/* Send LF/CR to pretty up output */
	while ((*((volatile unsigned char *)com_port +
		(UART_LSR << shift)) & UART_LSR_THRE) == 0)
		;
	*(volatile unsigned char *)com_port = '\r';
	while ((*((volatile unsigned char *)com_port +
		(UART_LSR << shift)) & UART_LSR_THRE) == 0)
		;
	*(volatile unsigned char *)com_port = '\n';
}
#endif /* CONFIG_SERIAL_TEXT_DEBUG */

void __init ibm44x_platform_init(void)
{
	ppc_md.init_IRQ = ibm44x_init_irq;
	ppc_md.find_end_of_memory = ibm44x_find_end_of_memory;
	ppc_md.restart = ibm44x_restart;
	ppc_md.power_off = ibm44x_power_off;
	ppc_md.halt = ibm44x_halt;

#ifdef CONFIG_SERIAL_TEXT_DEBUG
	ppc_md.progress = ibm44x_progress;
#endif /* CONFIG_SERIAL_TEXT_DEBUG */
}

