/*
 * arch/ppc/platforms/k2_setup.c
 *
 * Board setup routines for SBS K2
 *
 * Author: Matt Porter <mporter@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <linux/major.h>
#include <linux/initrd.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/time.h>
#include <asm/i8259.h>
#include <asm/todc.h>
#include <asm/bootinfo.h>

#include "k2.h"

extern void k2_setup_hoses(void);
extern unsigned long loops_per_jiffy;

static unsigned int cpu_7xx[16] = {
	0, 15, 14, 0, 0, 13, 5, 9, 6, 11, 8, 10, 16, 12, 7, 0
};
static unsigned int cpu_6xx[16] = {
	0, 0, 14, 0, 0, 13, 5, 9, 6, 11, 8, 10, 0, 12, 7, 0
};

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
/* IDE functions */

static void __init
k2_ide_init_hwif_ports (hw_regs_t *hw, unsigned long data_port,
		unsigned long ctrl_port, int *irq)
{
	unsigned long reg = data_port;
	int i = 8;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg;
		reg += 1;
	}
	if (ctrl_port)
		hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;
	else
		hw->io_ports[IDE_CONTROL_OFFSET] =
			hw->io_ports[IDE_DATA_OFFSET] + 0x206;

	if (irq != NULL)
		*irq = 0;
}
#endif

static int
k2_get_bus_speed(void)
{
	int bus_speed;
	unsigned char board_id;

	board_id = *(unsigned char *)K2_BOARD_ID_REG;

	switch( K2_BUS_SPD(board_id) ) {

		case 0:
		default:
			bus_speed = 100000000;
			break;

		case 1:
			bus_speed = 83333333;
			break;

		case 2:
			bus_speed = 75000000;
			break;

		case 3:
			bus_speed = 66666666;
			break;
	}
	return bus_speed;
}

static int
k2_get_cpu_speed(void)
{
	unsigned long hid1;
	int cpu_speed;

	hid1 = mfspr(HID1) >> 28;

	if ((mfspr(PVR) >> 16) == 8)
		hid1 = cpu_7xx[hid1];
	else
		hid1 = cpu_6xx[hid1];

	cpu_speed = k2_get_bus_speed()*hid1/2;
	return cpu_speed;
}

static void __init
k2_calibrate_decr(void)
{
	int freq, divisor = 4;

	/* determine processor bus speed */
	freq = k2_get_bus_speed();
	tb_ticks_per_jiffy = freq / HZ / divisor;
	tb_to_us = mulhwu_scale_factor(freq/divisor, 1000000);
}

static int
k2_show_cpuinfo(struct seq_file *m)
{
	unsigned char k2_geo_bits, k2_system_slot;

	seq_printf(m, "vendor\t\t: SBS\n");
	seq_printf(m, "machine\t\t: K2\n");
	seq_printf(m, "cpu speed\t: %dMhz\n", k2_get_cpu_speed()/1000000);
	seq_printf(m, "bus speed\t: %dMhz\n", k2_get_bus_speed()/1000000);
	seq_printf(m, "memory type\t: SDRAM\n");

	k2_geo_bits = readb(K2_MSIZ_GEO_REG) & K2_GEO_ADR_MASK;
	k2_system_slot = !(readb(K2_MISC_REG) & K2_SYS_SLOT_MASK);
	seq_printf(m, "backplane\t: %s slot board",
		k2_system_slot ? "System" : "Non system");
	seq_printf(m, "with geographical address %x\n",	k2_geo_bits);

	return 0;
}

extern char cmd_line[];

TODC_ALLOC();

static void __init
k2_setup_arch(void)
{
	unsigned int cpu;

	/* Setup TODC access */
	TODC_INIT(TODC_TYPE_MK48T37, 0, 0,
		  ioremap(K2_RTC_BASE_ADDRESS, K2_RTC_SIZE),
		  8);

	/* init to some ~sane value until calibrate_delay() runs */
        loops_per_jiffy = 50000000/HZ;

	/* Setup PCI host bridges */
        k2_setup_hoses();

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
#ifdef CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
		ROOT_DEV = Root_HDC1;
#endif

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	/* Identify the system */
	printk("System Identification: SBS K2 - PowerPC 750 @ %d Mhz\n", k2_get_cpu_speed()/1000000);
	printk("SBS K2 port (C) 2001 MontaVista Software, Inc. (source@mvista.com)\n");

	/* Identify the CPU manufacturer */
	cpu = PVR_REV(mfspr(PVR));
	printk("CPU manufacturer: %s [rev=%04x]\n", (cpu & (1<<15)) ? "IBM" :
	       "Motorola", cpu);
}

static void
k2_restart(char *cmd)
{
	local_irq_disable();
	/* SRR0 has system reset vector, SRR1 has default MSR value */
	/* rfi restores MSR from SRR1 and sets the PC to the SRR0 value */
	__asm__ __volatile__
	("lis	3,0xfff0\n\t"
	 "ori	3,3,0x0100\n\t"
	 "mtspr	26,3\n\t"
	 "li	3,0\n\t"
	 "mtspr	27,3\n\t"
	 "rfi\n\t");
	for(;;);
}

static void
k2_power_off(void)
{
	for(;;);
}

static void
k2_halt(void)
{
	k2_restart(NULL);
}

/*
 * Set BAT 3 to map PCI32 I/O space.
 */
static __inline__ void
k2_set_bat(void)
{
	unsigned long   bat3u, bat3l;
	static int	mapping_set = 0;

	if (!mapping_set)
	{
		__asm__ __volatile__
		("lis %0,0x8000\n\t"
		 "ori %1,%0,0x002a\n\t"
		 "ori %0,%0,0x1ffe\n\t"
		 "mtspr 0x21e,%0\n\t"
		 "mtspr 0x21f,%1\n\t"
		 "isync\n\t"
		 "sync\n\t"
		 : "=r" (bat3u), "=r" (bat3l));

		mapping_set = 1;
	}
	return;
}

static unsigned long __init
k2_find_end_of_memory(void)
{
	unsigned long total;
	unsigned char msize = 7;        /* Default to 128MB */

	k2_set_bat();

	msize = K2_MEM_SIZE(readb(K2_MSIZ_GEO_REG));

	switch (msize)
	{
		case 2:
			/*
			 * This will break without a lowered
			 * KERNELBASE or CONFIG_HIGHMEM on.
			 * It seems non 1GB builds exist yet,
			 * though.
			 */
			total = K2_MEM_SIZE_1GB;
			break;
		case 3:
		case 4:
			total = K2_MEM_SIZE_512MB;
			break;
		case 5:
		case 6:
			total = K2_MEM_SIZE_256MB;
			break;
		case 7:
			total = K2_MEM_SIZE_128MB;
			break;
		default:
			printk("K2: Invalid memory size detected, defaulting to 128MB\n");
				total = K2_MEM_SIZE_128MB;
			break;
	}
	return total;
}

static void __init
k2_map_io(void)
{
	io_block_mapping(K2_PCI32_IO_BASE,
			K2_PCI32_IO_BASE,
			0x00200000,
			_PAGE_IO);
	io_block_mapping(0xff000000,
			0xff000000,
			0x01000000,
			_PAGE_IO);
}

static void __init
k2_init_irq(void)
{
	int i;

	for ( i = 0 ; i < 16 ; i++ )
		irq_desc[i].handler = &i8259_pic;

	i8259_init(NULL);
}

void __init platform_init(unsigned long r3, unsigned long r4,
		unsigned long r5, unsigned long r6, unsigned long r7)
{
	parse_bootinfo((struct bi_record *) (r3 + KERNELBASE));

	isa_io_base = K2_ISA_IO_BASE;
	isa_mem_base = K2_ISA_MEM_BASE;
	pci_dram_offset = K2_PCI32_SYS_MEM_BASE;

	ppc_md.setup_arch = k2_setup_arch;
	ppc_md.show_cpuinfo = k2_show_cpuinfo;
	ppc_md.init_IRQ = k2_init_irq;
	ppc_md.get_irq = i8259_irq;

	ppc_md.find_end_of_memory = k2_find_end_of_memory;
	ppc_md.setup_io_mappings = k2_map_io;

	ppc_md.restart = k2_restart;
	ppc_md.power_off = k2_power_off;
	ppc_md.halt = k2_halt;

	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;
	ppc_md.calibrate_decr = k2_calibrate_decr;

	ppc_md.nvram_read_val = todc_direct_read_val;
	ppc_md.nvram_write_val = todc_direct_write_val;

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
	ppc_ide_md.ide_init_hwif = k2_ide_init_hwif_ports;
#endif
}

