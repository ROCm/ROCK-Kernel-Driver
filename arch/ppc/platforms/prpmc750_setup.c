/*
 * arch/ppc/platforms/prpmc750_setup.c
 *
 * Board setup routines for Motorola PrPMC750
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
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/ide.h>
#include <linux/root_dev.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/time.h>
#include <platforms/prpmc750.h>
#include <asm/open_pic.h>
#include <asm/bootinfo.h>
#include <asm/pplus.h>

extern void prpmc750_find_bridges(void);
extern int mpic_init(void);
extern unsigned long loops_per_jiffy;

static u_char prpmc750_openpic_initsenses[] __initdata =
{
    1,	/* PRPMC750_INT_HOSTINT0 */
    1,	/* PRPMC750_INT_UART */
    1,	/* PRPMC750_INT_DEBUGINT */
    1,	/* PRPMC750_INT_HAWK_WDT */
    1,	/* PRPMC750_INT_UNUSED */
    1,	/* PRPMC750_INT_ABORT */
    1,	/* PRPMC750_INT_HOSTINT1 */
    1,	/* PRPMC750_INT_HOSTINT2 */
    1,	/* PRPMC750_INT_HOSTINT3 */
    1,	/* PRPMC750_INT_PMC_INTA */
    1,	/* PRPMC750_INT_PMC_INTB */
    1,	/* PRPMC750_INT_PMC_INTC */
    1,	/* PRPMC750_INT_PMC_INTD */
    1,	/* PRPMC750_INT_UNUSED */
    1,	/* PRPMC750_INT_UNUSED */
    1,	/* PRPMC750_INT_UNUSED */
};

static int
prpmc750_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "machine\t\t: PrPMC750\n");

	return 0;
}

static void __init
prpmc750_setup_arch(void)
{
	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000/HZ;

	/* Lookup PCI host bridges */
	prpmc750_find_bridges();

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
#ifdef CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
		ROOT_DEV = Root_SDA2;
#endif

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	/* Find and map our OpenPIC */
	pplus_mpic_init(PRPMC750_PCI_MEM_OFFSET);
	OpenPIC_InitSenses = prpmc750_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof(prpmc750_openpic_initsenses);

	printk("PrPMC750 port (C) 2001 MontaVista Software, Inc. (source@mvista.com)\n");
}

/*
 * Compute the PrPMC750's bus speed using the baud clock as a
 * reference.
 */
static unsigned long __init
prpmc750_get_bus_speed(void)
{
	unsigned long tbl_start, tbl_end;
	unsigned long current_state, old_state, bus_speed;
	unsigned char lcr, dll, dlm;
	int baud_divisor, count;

	/* Read the UART's baud clock divisor */
	lcr = readb(PRPMC750_SERIAL_0_LCR);
	writeb(lcr | UART_LCR_DLAB, PRPMC750_SERIAL_0_LCR);
	dll = readb(PRPMC750_SERIAL_0_DLL);
	dlm = readb(PRPMC750_SERIAL_0_DLM);
	writeb(lcr & ~UART_LCR_DLAB, PRPMC750_SERIAL_0_LCR);
	baud_divisor = (dlm << 8) | dll;

	/*
	 * Use the baud clock divisor and base baud clock
	 * to determine the baud rate and use that as
	 * the number of baud clock edges we use for
	 * the time base sample.  Make it half the baud
	 * rate.
	 */
	count = PRPMC750_BASE_BAUD / (baud_divisor * 16);

	/* Find the first edge of the baud clock */
	old_state = readb(PRPMC750_STATUS_REG) & PRPMC750_BAUDOUT_MASK;
	do {
		current_state = readb(PRPMC750_STATUS_REG) &
			PRPMC750_BAUDOUT_MASK;
	} while(old_state == current_state);

	old_state = current_state;

	/* Get the starting time base value */
	tbl_start = get_tbl();

	/*
	 * Loop until we have found a number of edges equal
	 * to half the count (half the baud rate)
	 */
	do {
		do {
			current_state = readb(PRPMC750_STATUS_REG) &
				PRPMC750_BAUDOUT_MASK;
		} while(old_state == current_state);
		old_state = current_state;
	} while (--count);

	/* Get the ending time base value */
	tbl_end = get_tbl();

	/* Compute bus speed */
	bus_speed = (tbl_end-tbl_start)*128;

	return bus_speed;
}

static void __init
prpmc750_calibrate_decr(void)
{
	unsigned long freq;
	int divisor = 4;

	freq = prpmc750_get_bus_speed();

	tb_ticks_per_jiffy = freq / (HZ * divisor);
	tb_to_us = mulhwu_scale_factor(freq/divisor, 1000000);
}

static void
prpmc750_restart(char *cmd)
{
	local_irq_disable();
	writeb(PRPMC750_MODRST_MASK, PRPMC750_MODRST_REG);
	while(1);
}

static void
prpmc750_halt(void)
{
	local_irq_disable();
	while (1);
}

static void
prpmc750_power_off(void)
{
	prpmc750_halt();
}

static void __init
prpmc750_init_IRQ(void)
{
	openpic_init(1, 0, 0, -1);
}

/*
 * Set BAT 3 to map 0xf0000000 to end of physical memory space.
 */
static __inline__ void
prpmc750_set_bat(void)
{
	unsigned long   bat3u, bat3l;
	static int	mapping_set = 0;

	if (!mapping_set)
	{
		__asm__ __volatile__(
				" lis %0,0xf000\n \
				ori %1,%0,0x002a\n \
				ori %0,%0,0x1ffe\n \
				mtspr 0x21e,%0\n \
				mtspr 0x21f,%1\n \
				isync\n \
				sync "
				: "=r" (bat3u), "=r" (bat3l));

		mapping_set = 1;
	}
	return;
}

/*
 * We need to read the Falcon/Hawk memory controller
 * to properly determine this value
 */
static unsigned long __init
prpmc750_find_end_of_memory(void)
{
	/* Cover the Hawk registers with a BAT */
	prpmc750_set_bat();

	/* Read the memory size from the Hawk SMC */
	return pplus_get_mem_size(PRPMC750_HAWK_SMC_BASE);
}

static void __init
prpmc750_map_io(void)
{
	io_block_mapping(0x80000000, 0x80000000, 0x10000000, _PAGE_IO);
	io_block_mapping(0xf0000000, 0xc0000000, 0x08000000, _PAGE_IO);
	io_block_mapping(0xf8000000, 0xf8000000, 0x08000000, _PAGE_IO);
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());

	isa_io_base = PRPMC750_ISA_IO_BASE;
	isa_mem_base = PRPMC750_ISA_MEM_BASE;
	pci_dram_offset = PRPMC750_SYS_MEM_BASE;

	ppc_md.setup_arch	= prpmc750_setup_arch;
	ppc_md.show_cpuinfo	= prpmc750_show_cpuinfo;
	ppc_md.init_IRQ		= prpmc750_init_IRQ;
	ppc_md.get_irq		= openpic_get_irq;

	ppc_md.find_end_of_memory = prpmc750_find_end_of_memory;
	ppc_md.setup_io_mappings = prpmc750_map_io;

	ppc_md.restart		= prpmc750_restart;
	ppc_md.power_off	= prpmc750_power_off;
	ppc_md.halt		= prpmc750_halt;

	/* PrPMC750 has no timekeeper part */
	ppc_md.time_init	= NULL;
	ppc_md.get_rtc_time	= NULL;
	ppc_md.set_rtc_time	= NULL;
	ppc_md.calibrate_decr	= prpmc750_calibrate_decr;
}
