/***********************************************************************
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * arch/mips/ddb5xxx/ddb5477/setup.c
 *     Setup file for DDB5477.
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
#include <linux/kernel.h>
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <linux/console.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/ide.h>
#include <linux/fs.h>		/* for ROOT_DEV */
#include <linux/ioport.h>
#include <linux/param.h>	/* for HZ */

#include <asm/addrspace.h>
#include <asm/time.h>
#include <asm/bcache.h>
#include <asm/irq.h>
#include <asm/reboot.h>
#include <asm/gdb-stub.h>

#include <asm/ddb5xxx/ddb5xxx.h>



#define	USE_CPU_COUNTER_TIMER	/* whether we use cpu counter */

#ifdef USE_CPU_COUNTER_TIMER
#define	CPU_COUNTER_FREQUENCY		83000000
#else
/* otherwise we use special timer 1 */
#define	SP_TIMER_FREQUENCY		83000000
#define	SP_TIMER_BASE			DDB_SPT1CTRL_L
#define	SP_TIMER_IRQ			(8 + 6)
#endif

static void ddb_machine_restart(char *command)
{
	static void (*back_to_prom) (void) = (void (*)(void)) 0xbfc00000;

	u32 t;

	/* PCI cold reset */
	ddb_pci_reset_bus();

	/* CPU cold reset */
	t = ddb_in32(DDB_CPUSTAT);
	MIPS_ASSERT((t&1));
	ddb_out32(DDB_CPUSTAT, t);

	/* Call the PROM */
	back_to_prom();
}

static void ddb_machine_halt(void)
{
	printk("DDB Vrc-5477 halted.\n");
	while (1);
}

static void ddb_machine_power_off(void)
{
	printk("DDB Vrc-5477 halted. Please turn off the power.\n");
	while (1);
}

extern void rtc_ds1386_init(unsigned long base);
static void __init ddb_time_init(void)
{
#if defined(USE_CPU_COUNTER_TIMER)
	mips_counter_frequency = CPU_COUNTER_FREQUENCY;
#endif

	/* we have ds1396 RTC chip */
	rtc_ds1386_init(KSEG1ADDR(DDB_LCS1_BASE));
}

#if defined(CONFIG_LL_DEBUG)
int  board_init_done_flag = 0;
#endif

extern int setup_irq(unsigned int irq, struct irqaction *irqaction);
static void __init ddb_timer_setup(struct irqaction *irq)
{
#if defined(USE_CPU_COUNTER_TIMER)
	unsigned int count;

        /* we are using the cpu counter for timer interrupts */
	setup_irq(7, irq);

        /* to generate the first timer interrupt */
        count = read_32bit_cp0_register(CP0_COUNT);
        write_32bit_cp0_register(CP0_COMPARE, count + 1000);

#else

	/* if we don't use Special purpose timer 1 */
	ddb_out32(SP_TIMER_BASE, SP_TIMER_FREQUENCY/HZ);
	ddb_out32(SP_TIMER_BASE+4, 0x1);
	setup_irq(SP_TIMER_IRQ, irq);

#endif
	
	/* this is the last board dependent code */
	MIPS_DEBUG(board_init_done_flag = 1);
}

static void ddb5477_board_init(void);
extern void ddb5477_irq_setup(void);

#if defined(CONFIG_BLK_DEV_INITRD)
extern unsigned long __rd_start, __rd_end, initrd_start, initrd_end;
#endif 

void __init ddb_setup(void)
{
	extern int panic_timeout;

	irq_setup = ddb5477_irq_setup;
	mips_io_port_base = KSEG1ADDR(DDB_PCI_IO_BASE);

	board_time_init = ddb_time_init;
	board_timer_setup = ddb_timer_setup;

	_machine_restart = ddb_machine_restart;
	_machine_halt = ddb_machine_halt;
	_machine_power_off = ddb_machine_power_off;

	/* setup resource limits */
	ioport_resource.end = DDB_PCI0_IO_SIZE + DDB_PCI1_IO_SIZE - 1;
	iomem_resource.end = 0xffffffff;
	
	/* Reboot on panic */
	panic_timeout = 180;

	/* initialize board - we don't trust the loader */
	ddb5477_board_init();

#if defined(CONFIG_BLK_DEV_INITRD)
	ROOT_DEV = MKDEV(RAMDISK_MAJOR, 0);
	initrd_start = (unsigned long)&__rd_start;
	initrd_end = (unsigned long)&__rd_end;
#endif

}

static void __init ddb5477_board_init()
{
	/* ----------- setup PDARs ------------ */

	/* SDRAM should have been set */
	MIPS_ASSERT(ddb_in32(DDB_SDRAM0) == 
		    ddb_calc_pdar(DDB_SDRAM_BASE, DDB_SDRAM_SIZE, 32, 0, 1));

	/* SDRAM1 should be turned off.  What is this for anyway ? */
	MIPS_ASSERT( (ddb_in32(DDB_SDRAM1) & 0xf) == 0);

	/* Set LDCSs */
	/* flash */
	ddb_set_pdar(DDB_LCS0, DDB_LCS0_BASE, DDB_LCS0_SIZE, 16, 0, 0);
	/* misc */
	ddb_set_pdar(DDB_LCS1, DDB_LCS1_BASE, DDB_LCS1_SIZE, 8, 0, 0);
	/* mezzanie (?) */
	ddb_set_pdar(DDB_LCS2, DDB_LCS2_BASE, DDB_LCS2_SIZE, 16, 0, 0);

	/* verify VRC5477 base addr */
	MIPS_ASSERT(ddb_in32(DDB_VRC5477) == 
		    ddb_calc_pdar(DDB_VRC5477_BASE, DDB_VRC5477_SIZE, 32, 0, 1));
	
	/* verify BOOT ROM addr */
	MIPS_ASSERT(ddb_in32(DDB_BOOTCS) == 
		    ddb_calc_pdar(DDB_BOOTCS_BASE, DDB_BOOTCS_SIZE, 8, 0, 0));

	/* setup PCI windows - window0 for MEM/config, window1 for IO */
	ddb_set_pdar(DDB_PCIW0, DDB_PCI0_MEM_BASE, DDB_PCI0_MEM_SIZE, 32, 0, 1);
	ddb_set_pdar(DDB_PCIW1, DDB_PCI0_IO_BASE, DDB_PCI0_IO_SIZE, 32, 0, 1);
	ddb_set_pdar(DDB_IOPCIW0, DDB_PCI1_MEM_BASE, DDB_PCI1_MEM_SIZE, 32, 0, 1);
	ddb_set_pdar(DDB_IOPCIW1, DDB_PCI1_IO_BASE, DDB_PCI1_IO_SIZE, 32, 0, 1);

	/* ------------ reset PCI bus and BARs ----------------- */
	ddb_pci_reset_bus();

	ddb_out32(DDB_BARM010, 0x00000008);
	ddb_out32(DDB_BARM011, 0x00000008);

	ddb_out32(DDB_BARC0, 0xffffffff);
	ddb_out32(DDB_BARM230, 0xffffffff);
	ddb_out32(DDB_BAR00, 0xffffffff);
	ddb_out32(DDB_BAR10, 0xffffffff);
	ddb_out32(DDB_BAR20, 0xffffffff);
	ddb_out32(DDB_BAR30, 0xffffffff);
	ddb_out32(DDB_BAR40, 0xffffffff);
	ddb_out32(DDB_BAR50, 0xffffffff);
	ddb_out32(DDB_BARB0, 0xffffffff);

	ddb_out32(DDB_BARC1, 0xffffffff);
	ddb_out32(DDB_BARM231, 0xffffffff);
	ddb_out32(DDB_BAR01, 0xffffffff);
	ddb_out32(DDB_BAR11, 0xffffffff);
	ddb_out32(DDB_BAR21, 0xffffffff);
	ddb_out32(DDB_BAR31, 0xffffffff);
	ddb_out32(DDB_BAR41, 0xffffffff);
	ddb_out32(DDB_BAR51, 0xffffffff);
	ddb_out32(DDB_BARB1, 0xffffffff);

	/* 
	 * We use pci master register 0  for memory space / config space
	 * And we use register 1 for IO space.
	 * Note that for memory space, we bump up the pci base address
	 * so that we have 1:1 mapping between PCI memory and cpu physical.
	 * For PCI IO space, it starts from 0 in PCI IO space but with
	 * DDB_xx_IO_BASE in CPU physical address space.
	 */
	ddb_set_pmr(DDB_PCIINIT00, DDB_PCICMD_MEM, DDB_PCI0_MEM_BASE, 
		    DDB_PCI_ACCESS_32);
	ddb_set_pmr(DDB_PCIINIT10, DDB_PCICMD_IO, 0, DDB_PCI_ACCESS_32);

	ddb_set_pmr(DDB_PCIINIT01, DDB_PCICMD_MEM, DDB_PCI1_MEM_BASE, 
		    DDB_PCI_ACCESS_32);
	ddb_set_pmr(DDB_PCIINIT11, DDB_PCICMD_IO, DDB_PCI0_IO_SIZE, 
                    DDB_PCI_ACCESS_32);

	
	/* PCI cross window should be set properly */
	ddb_set_pdar(DDB_BARP00, DDB_PCI1_MEM_BASE, DDB_PCI1_MEM_SIZE, 32, 0, 1);
	ddb_set_pdar(DDB_BARP10, DDB_PCI1_IO_BASE, DDB_PCI1_IO_SIZE, 32, 0, 1);
	ddb_set_pdar(DDB_BARP01, DDB_PCI0_MEM_BASE, DDB_PCI0_MEM_SIZE, 32, 0, 1);
	ddb_set_pdar(DDB_BARP11, DDB_PCI0_IO_BASE, DDB_PCI0_IO_SIZE, 32, 0, 1);

	/* enable USB input buffers */
	ddb_out32(DDB_PIBMISC, 0x00000007);

	/* For dual-function pins, make them all non-GPIO */
	ddb_out32(DDB_GIUFUNSEL, 0x0);
	// ddb_out32(DDB_GIUFUNSEL, 0xfe0fcfff);  /* NEC recommanded value */
}
