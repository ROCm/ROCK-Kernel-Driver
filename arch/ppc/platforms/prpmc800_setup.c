/*
 *
 * Author: Dale Farnsworth <dale.farnsworth@mvista.com>
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
#include <platforms/prpmc800.h>
#include <asm/open_pic.h>
#include <asm/bootinfo.h>
#include <asm/harrier.h>

#define HARRIER_REVI_REG	(PRPMC800_HARRIER_XCSR_BASE+HARRIER_REVI_OFF)
#define HARRIER_UCTL_REG	(PRPMC800_HARRIER_XCSR_BASE+HARRIER_UCTL_OFF)
#define HARRIER_MISC_CSR_REG  (PRPMC800_HARRIER_XCSR_BASE+HARRIER_MISC_CSR_OFF)
#define HARRIER_IFEVP_REG   (PRPMC800_HARRIER_MPIC_BASE+HARRIER_MPIC_IFEVP_OFF)
#define HARRIER_IFEDE_REG   (PRPMC800_HARRIER_MPIC_BASE+HARRIER_MPIC_IFEDE_OFF)
#define HARRIER_FEEN_REG	(PRPMC800_HARRIER_XCSR_BASE+HARRIER_FEEN_OFF)
#define HARRIER_FEMA_REG	(PRPMC800_HARRIER_XCSR_BASE+HARRIER_FEMA_OFF)

extern void prpmc800_find_bridges(void);
extern int mpic_init(void);
extern unsigned long loops_per_jiffy;

static u_char prpmc800_openpic_initsenses[] __initdata =
{
    1,	/* PRPMC800_INT_HOSTINT0 */
    1,	/* PRPMC800_INT_UNUSED */
    1,	/* PRPMC800_INT_DEBUGINT */
    1,	/* PRPMC800_INT_HARRIER_WDT */
    1,	/* PRPMC800_INT_UNUSED */
    1,	/* PRPMC800_INT_UNUSED */
    1,	/* PRPMC800_INT_HOSTINT1 */
    1,	/* PRPMC800_INT_HOSTINT2 */
    1,	/* PRPMC800_INT_HOSTINT3 */
    1,	/* PRPMC800_INT_PMC_INTA */
    1,	/* PRPMC800_INT_PMC_INTB */
    1,	/* PRPMC800_INT_PMC_INTC */
    1,	/* PRPMC800_INT_PMC_INTD */
    1,	/* PRPMC800_INT_UNUSED */
    1,	/* PRPMC800_INT_UNUSED */
    1,	/* PRPMC800_INT_UNUSED */
    1,	/* PRPMC800_INT_HARRIER_INT (UARTS, ABORT, DMA) */
};

static int
prpmc800_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "machine\t\t: PrPMC800\n");

	return 0;
}

static void __init
prpmc800_setup_arch(void)
{

	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000/HZ;

	/* Lookup PCI host bridges */
	prpmc800_find_bridges();

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

	OpenPIC_InitSenses = prpmc800_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof(prpmc800_openpic_initsenses);

	printk("PrPMC800 port (C) 2001 MontaVista Software, Inc. (source@mvista.com)\n");
}

/*
 * Compute the PrPMC800's tbl frequency using the baud clock as a reference.
 */

static void __init
prpmc800_calibrate_decr(void)
{
	unsigned long tbl_start, tbl_end;
	unsigned long current_state, old_state, tb_ticks_per_second;
	unsigned int count;
	unsigned int harrier_revision;

	harrier_revision = readb(HARRIER_REVI_REG);
	if (harrier_revision < 2) {
		/* XTAL64 was broken in harrier revision 1 */
		printk("time_init: Harrier revision %d, assuming 100 Mhz bus\n",
			harrier_revision);
		tb_ticks_per_second = 100000000/4;
		tb_ticks_per_jiffy = tb_ticks_per_second / HZ;
		tb_to_us = mulhwu_scale_factor(tb_ticks_per_second, 1000000);
		return;
	}

	/*
	 * The XTAL64 bit oscillates at the 1/64 the base baud clock
	 * Set count to XTAL64 cycles per second.  Since we'll count
	 * half-cycles, we'll reach the count in half a second.
	 */
	count = PRPMC800_BASE_BAUD / 64;

	/* Find the first edge of the baud clock */
	old_state = readb(HARRIER_UCTL_REG) & HARRIER_XTAL64_MASK;
	do {
		current_state = readb(HARRIER_UCTL_REG) &
			HARRIER_XTAL64_MASK;
	} while(old_state == current_state);

	old_state = current_state;

	/* Get the starting time base value */
	tbl_start = get_tbl();

	/*
	 * Loop until we have found a number of edges (half-cycles)
	 * equal to the count (half a second)
	 */
	do {
		do {
			current_state = readb(HARRIER_UCTL_REG) &
				HARRIER_XTAL64_MASK;
		} while(old_state == current_state);
		old_state = current_state;
	} while (--count);

	/* Get the ending time base value */
	tbl_end = get_tbl();

	/* We only counted for half a second, so double to get ticks/second */
	tb_ticks_per_second = (tbl_end - tbl_start) * 2;
	tb_ticks_per_jiffy = tb_ticks_per_second / HZ;
	tb_to_us = mulhwu_scale_factor(tb_ticks_per_second, 1000000);
}

static void
prpmc800_restart(char *cmd)
{
	local_irq_disable();
	writeb(HARRIER_RSTOUT_MASK, HARRIER_MISC_CSR_REG);
	while(1);
}

static void
prpmc800_halt(void)
{
	local_irq_disable();
	while (1);
}

static void
prpmc800_power_off(void)
{
	prpmc800_halt();
}

static void __init
prpmc800_init_IRQ(void)
{
	openpic_init(1, 0, 0, -1);

#define PRIORITY	15
#define VECTOR	 	16
#define PROCESSOR	0
	/* initialize the harrier's internal interrupt priority 15, irq 1 */
	out_be32((u32 *)HARRIER_IFEVP_REG, (PRIORITY<<16) | VECTOR);
	out_be32((u32 *)HARRIER_IFEDE_REG, (1<<PROCESSOR));

	/* enable functional exceptions for uarts and abort */
	out_8((u8 *)HARRIER_FEEN_REG, (HARRIER_FE_UA0|HARRIER_FE_UA1));
	out_8((u8 *)HARRIER_FEMA_REG, ~(HARRIER_FE_UA0|HARRIER_FE_UA1));
}

/*
 * Set BAT 3 to map 0xf0000000 to end of physical memory space.
 */
static __inline__ void
prpmc800_set_bat(void)
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

#ifdef  CONFIG_SERIAL_TEXT_DEBUG
#include <linux/serial.h>
#include <linux/serialP.h>
#include <linux/serial_reg.h>
#include <asm/serial.h>

static struct serial_state rs_table[RS_TABLE_SIZE] = {
	SERIAL_PORT_DFNS	/* Defined in <asm/serial.h> */
};

void
prpmc800_progress(char *s, unsigned short hex)
{
	volatile char c;
	volatile unsigned char *com_port;
	volatile unsigned char *com_port_lsr;

	com_port = (volatile unsigned char *) rs_table[0].port;
	com_port_lsr = com_port + UART_LSR;

	while ((c = *s++) != 0) {
		while ((*com_port_lsr & UART_LSR_THRE) == 0)
		                ;
	        *com_port = c;

		if (c == '\n') {
			while ((*com_port_lsr & UART_LSR_THRE) == 0)
					;
	        	*com_port = '\r';
		}
	}
}
#endif	/* CONFIG_SERIAL_TEXT_DEBUG */

/*
 * We need to read the Harrier memory controller
 * to properly determine this value
 */
static unsigned long __init
prpmc800_find_end_of_memory(void)
{
	/* Cover the harrier registers with a BAT */
	prpmc800_set_bat();

	/* Read the memory size from the Harrier XCSR */
	return harrier_get_mem_size(PRPMC800_HARRIER_XCSR_BASE);
}

static void __init
prpmc800_map_io(void)
{
        io_block_mapping(0x80000000, 0x80000000, 0x10000000, _PAGE_IO);
	io_block_mapping(0xf0000000, 0xf0000000, 0x10000000, _PAGE_IO);
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	  unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());

	prpmc800_set_bat();

	isa_io_base = PRPMC800_ISA_IO_BASE;
	isa_mem_base = PRPMC800_ISA_MEM_BASE;
	pci_dram_offset = PRPMC800_PCI_DRAM_OFFSET;

	ppc_md.setup_arch	= prpmc800_setup_arch;
	ppc_md.show_cpuinfo	= prpmc800_show_cpuinfo;
	ppc_md.init_IRQ		= prpmc800_init_IRQ;
	ppc_md.get_irq		= openpic_get_irq;

	ppc_md.find_end_of_memory = prpmc800_find_end_of_memory;
	ppc_md.setup_io_mappings = prpmc800_map_io;

	ppc_md.restart		= prpmc800_restart;
	ppc_md.power_off	= prpmc800_power_off;
	ppc_md.halt		= prpmc800_halt;

	/* PrPMC800 has no timekeeper part */
	ppc_md.time_init	= NULL;
	ppc_md.get_rtc_time	= NULL;
	ppc_md.set_rtc_time	= NULL;
	ppc_md.calibrate_decr	= prpmc800_calibrate_decr;
#ifdef  CONFIG_SERIAL_TEXT_DEBUG
        ppc_md.progress = prpmc800_progress;
#else   /* !CONFIG_SERIAL_TEXT_DEBUG */
	ppc_md.progress = NULL;
#endif  /* CONFIG_SERIAL_TEXT_DEBUG */
}
