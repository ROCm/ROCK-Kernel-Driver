/*
 * arch/ppc/platforms/mvme5100_setup.c
 *
 * Board setup routines for the Motorola MVME5100.
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
#include <linux/major.h>
#include <linux/initrd.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/ide.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/time.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/open_pic.h>
#include <asm/i8259.h>
#include <platforms/mvme5100.h>
#include <asm/todc.h>
#include <asm/pci-bridge.h>
#include <asm/bootinfo.h>
#include <asm/pplus.h>

extern char cmd_line[];

static u_char mvme5100_openpic_initsenses[] __initdata = {
	0,	/* 16: i8259 cascade (active high) */
	1,	/* 17: TL16C550 UART 1,2 */
	1,	/* 18: Enet 1 (front panel or P2) */
	1,	/* 19: Hawk Watchdog 1,2 */
	1,	/* 20: DS1621 thermal alarm */
	1,	/* 21: Universe II LINT0# */
	1,	/* 22: Universe II LINT1# */
	1,	/* 23: Universe II LINT2# */
	1,	/* 24: Universe II LINT3# */
	1,	/* 25: PMC1 INTA#, PMC2 INTB# */
	1,	/* 26: PMC1 INTB#, PMC2 INTC# */
	1,	/* 27: PMC1 INTC#, PMC2 INTD# */
	1,	/* 28: PMC1 INTD#, PMC2 INTA# */
	1,	/* 29: Enet 2 (front panel) */
	1,	/* 30: Abort Switch */
	1,	/* 31: RTC Alarm */
};

static void __init
mvme5100_setup_arch(void)
{
	if ( ppc_md.progress )
		ppc_md.progress("mvme5100_setup_arch: enter", 0);

	loops_per_jiffy = 50000000 / HZ;

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
#ifdef	CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
		ROOT_DEV = Root_SDA2;
#endif

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	if ( ppc_md.progress )
		ppc_md.progress("mvme5100_setup_arch: find_bridges", 0);

	/* Setup PCI host bridge */
	mvme5100_setup_bridge();

	/* Find and map our OpenPIC */
	pplus_mpic_init(MVME5100_PCI_MEM_OFFSET);
	OpenPIC_InitSenses = mvme5100_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof(mvme5100_openpic_initsenses);

	printk("MVME5100 port (C) 2001 MontaVista Software, Inc. (source@mvista.com)\n");

	if ( ppc_md.progress )
		ppc_md.progress("mvme5100_setup_arch: exit", 0);

	return;
}

static void __init
mvme5100_init2(void)
{
#ifdef CONFIG_MVME5100_IPMC761_PRESENT
		request_region(0x00,0x20,"dma1");
		request_region(0x20,0x20,"pic1");
		request_region(0x40,0x20,"timer");
		request_region(0x80,0x10,"dma page reg");
		request_region(0xa0,0x20,"pic2");
		request_region(0xc0,0x20,"dma2");
#endif
	return;
}

/*
 * Interrupt setup and service.
 * Have MPIC on HAWK and cascaded 8259s on Winbond cascaded to MPIC.
 */
static void __init
mvme5100_init_IRQ(void)
{
#ifdef CONFIG_MVME5100_IPMC761_PRESENT
	int i;
#endif

	if ( ppc_md.progress )
		ppc_md.progress("init_irq: enter", 0);

#ifdef CONFIG_MVME5100_IPMC761_PRESENT
	openpic_init(1, NUM_8259_INTERRUPTS, NULL, -1);
	openpic_hookup_cascade(NUM_8259_INTERRUPTS,"82c59 cascade",&i8259_irq);

	for(i=0; i < NUM_8259_INTERRUPTS; i++)
		irq_desc[i].handler = &i8259_pic;

	i8259_init(NULL);
#else
	openpic_init(1, 0, NULL, -1);
#endif

	if ( ppc_md.progress )
		ppc_md.progress("init_irq: exit", 0);

	return;
}

/*
 * Set BAT 3 to map 0xf0000000 to end of physical memory space.
 */
static __inline__ void
mvme5100_set_bat(void)
{
	unsigned long bat3u, bat3l;
	static int mapping_set = 0;

	if (!mapping_set) {

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

static unsigned long __init
mvme5100_find_end_of_memory(void)
{
	mvme5100_set_bat();
	return pplus_get_mem_size(MVME5100_HAWK_SMC_BASE);
}

static void __init
mvme5100_map_io(void)
{
	io_block_mapping(0xfe000000, 0xfe000000, 0x02000000, _PAGE_IO);
	ioremap_base = 0xfe000000;
}

static void
mvme5100_reset_board(void)
{
	local_irq_disable();

	/* Set exception prefix high - to the firmware */
	_nmask_and_or_msr(0, MSR_IP);

	out_8((u_char *)MVME5100_BOARD_MODRST_REG, 0x01);

	return;
}

static void
mvme5100_restart(char *cmd)
{
	volatile ulong i = 10000000;

	mvme5100_reset_board();

	while (i-- > 0);
	panic("restart failed\n");
}

static void
mvme5100_halt(void)
{
	local_irq_disable();
	while (1);
}

static void
mvme5100_power_off(void)
{
	mvme5100_halt();
}

static int
mvme5100_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "vendor\t\t: Motorola\n");
	seq_printf(m, "machine\t\t: MVME5100\n");

	return 0;
}

TODC_ALLOC();

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());

	isa_io_base = MVME5100_ISA_IO_BASE;
	isa_mem_base = MVME5100_ISA_MEM_BASE;
	pci_dram_offset = MVME5100_PCI_DRAM_OFFSET;

	ppc_md.setup_arch = mvme5100_setup_arch;
	ppc_md.show_cpuinfo = mvme5100_show_cpuinfo;
	ppc_md.init_IRQ = mvme5100_init_IRQ;
	ppc_md.get_irq = openpic_get_irq;
	ppc_md.init = mvme5100_init2;

	ppc_md.restart = mvme5100_restart;
	ppc_md.power_off = mvme5100_power_off;
	ppc_md.halt = mvme5100_halt;

	ppc_md.find_end_of_memory = mvme5100_find_end_of_memory;
	ppc_md.setup_io_mappings = mvme5100_map_io;

	TODC_INIT(TODC_TYPE_MK48T37,
		  MVME5100_NVRAM_AS0,
		  MVME5100_NVRAM_AS1,
		  MVME5100_NVRAM_DATA,
		  8);

	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;
	ppc_md.calibrate_decr = todc_calibrate_decr;

	ppc_md.nvram_read_val = todc_m48txx_read_val;
	ppc_md.nvram_write_val = todc_m48txx_write_val;

	ppc_md.progress = NULL;
}
