/*
 * arch/ppc/platforms/ev64260_setup.c
 *
 * Board setup routines for the Marvell/Galileo EV-64260-BP Evaluation Board.
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

/*
 * The EV-64260-BP port is the result of hard work from many people from
 * many companies.  In particular, employees of Marvell/Galileo, Mission
 * Critical Linux, Xyterra, and MontaVista Software were heavily involved.
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
#if	!defined(CONFIG_GT64260_CONSOLE)
#include <linux/serial.h>
#endif

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/time.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/todc.h>
#include <asm/bootinfo.h>
#include <asm/gt64260.h>
#include <platforms/ev64260.h>


extern char cmd_line[];
unsigned long ev64260_find_end_of_memory(void);

TODC_ALLOC();

/*
 * Marvell/Galileo EV-64260-BP Evaluation Board PCI interrupt routing.
 */
static int __init
ev64260_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	struct pci_controller	*hose = pci_bus_to_hose(dev->bus->number);

	if (hose->index == 0) {
		static char pci_irq_table[][4] =
		/*
		 *	PCI IDSEL/INTPIN->INTLINE
		 * 	   A   B   C   D
		 */
		{
			{ 91, 0, 0, 0 },	/* IDSEL 7 - PCI bus 0 */
			{ 91, 0, 0, 0 },	/* IDSEL 8 - PCI bus 0 */
		};

		const long min_idsel = 7, max_idsel = 8, irqs_per_slot = 4;
		return PCI_IRQ_TABLE_LOOKUP;
	}
	else {
		static char pci_irq_table[][4] =
		/*
		 *	PCI IDSEL/INTPIN->INTLINE
		 * 	   A   B   C   D
		 */
		{
			{ 93, 0, 0, 0 },	/* IDSEL 7 - PCI bus 1 */
			{ 93, 0, 0, 0 },	/* IDSEL 8 - PCI bus 1 */
		};

		const long min_idsel = 7, max_idsel = 8, irqs_per_slot = 4;
		return PCI_IRQ_TABLE_LOOKUP;
	}
}

static void __init
ev64260_setup_bridge(void)
{
	gt64260_bridge_info_t		info;
	int				window;

	GT64260_BRIDGE_INFO_DEFAULT(&info, ev64260_find_end_of_memory());

	/* Lookup PCI host bridges */
	if (gt64260_find_bridges(EV64260_BRIDGE_REG_BASE,
				 &info,
				 ev64260_map_irq)) {
		printk("Bridge initialization failed.\n");
	}

	/*
	 * Enabling of PCI internal-vs-external arbitration
	 * is a platform- and errata-dependent decision.
	 */
	if(gt64260_revision == GT64260)  {
		/* FEr#35 */
		gt_clr_bits(GT64260_PCI_0_ARBITER_CNTL, (1<<31));
		gt_clr_bits(GT64260_PCI_1_ARBITER_CNTL, (1<<31));
	} else if( gt64260_revision == GT64260A )  {
		gt_set_bits(GT64260_PCI_0_ARBITER_CNTL, (1<<31));
		gt_set_bits(GT64260_PCI_1_ARBITER_CNTL, (1<<31));
		/* Make external GPP interrupts level sensitive */
		gt_set_bits(GT64260_COMM_ARBITER_CNTL, (1<<10));
		/* Doc Change 9: > 100 MHz so must be set */
		gt_set_bits(GT64260_CPU_CONFIG, (1<<23));
	}

	gt_set_bits(GT64260_CPU_MASTER_CNTL, (1<<9)); /* Only 1 cpu */

	/* SCS windows not disabled above, disable all but SCS 0 */
	for (window=1; window<GT64260_CPU_SCS_DECODE_WINDOWS; window++) {
		gt64260_cpu_scs_set_window(window, 0, 0);
	}

	/* Set up windows to RTC/TODC and DUART on device module (CS 1 & 2) */
	gt64260_cpu_cs_set_window(1, EV64260_TODC_BASE, EV64260_TODC_LEN);
	gt64260_cpu_cs_set_window(2, EV64260_UART_BASE, EV64260_UART_LEN);

	/*
	 * The EV-64260-BP uses several Multi-Purpose Pins (MPP) on the 64260
	 * bridge as interrupt inputs (via the General Purpose Ports (GPP)
	 * register).  Need to route the MPP inputs to the GPP and set the
	 * polarity correctly.
	 *
	 * In MPP Control 2 Register
	 *   MPP 21 -> GPP 21 (DUART channel A intr)
	 *   MPP 22 -> GPP 22 (DUART channel B intr)
	 *
	 * In MPP Control 3 Register
	 *   MPP 27 -> GPP 27 (PCI 0 INTA)
	 *   MPP 29 -> GPP 29 (PCI 1 INTA)
	 */
	gt_clr_bits(GT64260_MPP_CNTL_2,
			       ((1<<20) | (1<<21) | (1<<22) | (1<<23) |
			        (1<<24) | (1<<25) | (1<<26) | (1<<27)));

	gt_clr_bits(GT64260_MPP_CNTL_3,
			       ((1<<12) | (1<<13) | (1<<14) | (1<<15) |
			        (1<<20) | (1<<21) | (1<<22) | (1<<23)));

	gt_write(GT64260_GPP_LEVEL_CNTL, 0x000002c6);

	/* DUART & PCI interrupts are active low */
	gt_set_bits(GT64260_GPP_LEVEL_CNTL,
			     ((1<<21) | (1<<22) | (1<<27) | (1<<29)));

	/* Clear any pending interrupts for these inputs and enable them. */
	gt_write(GT64260_GPP_INTR_CAUSE,
			  ~((1<<21) | (1<<22) | (1<<27) | (1<<29)));
	gt_set_bits(GT64260_GPP_INTR_MASK,
			     ((1<<21) | (1<<22)| (1<<27) | (1<<29)));
	gt_set_bits(GT64260_IC_CPU_INTR_MASK_HI, ((1<<26) | (1<<27)));

	/* Set MPSC Multiplex RMII */
	/* NOTE: ethernet driver modifies bit 0 and 1 */
	gt_write(GT64260_MPP_SERIAL_PORTS_MULTIPLEX, 0x00001102);

	return;
}


static void __init
ev64260_setup_arch(void)
{
#if	!defined(CONFIG_GT64260_CONSOLE)
	struct serial_struct	serial_req;
#endif

	if ( ppc_md.progress )
		ppc_md.progress("ev64260_setup_arch: enter", 0);

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

	if ( ppc_md.progress )
		ppc_md.progress("ev64260_setup_arch: find_bridges", 0);

	/*
	 * Set up the L2CR register.
	 * L2 cache was invalidated by bootloader.
	 */
	switch (PVR_VER(mfspr(PVR))) {
		case PVR_VER(PVR_750):
			_set_L2CR(0xfd100000);
			break;
		case PVR_VER(PVR_7400):
		case PVR_VER(PVR_7410):
			_set_L2CR(0xcd100000);
			break;
		/* case PVR_VER(PVR_7450): */
			/* XXXX WHAT VALUE?? FIXME */
			break;
	}

	ev64260_setup_bridge();

	TODC_INIT(TODC_TYPE_DS1501, 0, 0, ioremap(EV64260_TODC_BASE,0x20), 8);

#if	!defined(CONFIG_GT64260_CONSOLE)
	memset(&serial_req, 0, sizeof(serial_req));
	serial_req.line = 0;
	serial_req.baud_base = BASE_BAUD;
	serial_req.port = 0;
	serial_req.irq = 85;
	serial_req.flags = STD_COM_FLAGS;
	serial_req.io_type = SERIAL_IO_MEM;
	serial_req.iomem_base = ioremap(EV64260_SERIAL_0, 0x20);
	serial_req.iomem_reg_shift = 2;

	if (early_serial_setup(&serial_req) != 0) {
		printk("Early serial init of port 0 failed\n");
	}

	/* Assume early_serial_setup() doesn't modify serial_req */
	serial_req.line = 1;
	serial_req.port = 1;
	serial_req.irq = 86;
	serial_req.iomem_base = ioremap(EV64260_SERIAL_1, 0x20);

	if (early_serial_setup(&serial_req) != 0) {
		printk("Early serial init of port 1 failed\n");
	}
#endif

	printk("Marvell/Galileo EV-64260-BP Evaluation Board\n");
	printk("EV-64260-BP port (C) 2001 MontaVista Software, Inc. (source@mvista.com)\n");

	if ( ppc_md.progress )
		ppc_md.progress("ev64260_setup_arch: exit", 0);

	return;
}

static void __init
ev64260_init_irq(void)
{
	gt64260_init_irq();

	if(gt64260_revision != GT64260)  {
		/* XXXX Kludge--need to fix gt64260_init_irq() interface */
		/* Mark PCI intrs level sensitive */
		irq_desc[91].status |= IRQ_LEVEL;
		irq_desc[93].status |= IRQ_LEVEL;
	}
}

unsigned long __init
ev64260_find_end_of_memory(void)
{
	return 32*1024*1024;	/* XXXX FIXME */
}

static void
ev64260_reset_board(void)
{
	local_irq_disable();

	/* Set exception prefix high - to the firmware */
	_nmask_and_or_msr(0, MSR_IP);

	/* XXX FIXME */
	printk("XXXX **** trying to reset board ****\n");
	return;
}

static void
ev64260_restart(char *cmd)
{
	volatile ulong	i = 10000000;

	ev64260_reset_board();

	while (i-- > 0);
	panic("restart failed\n");
}

static void
ev64260_halt(void)
{
	local_irq_disable();
	while (1);
	/* NOTREACHED */
}

static void
ev64260_power_off(void)
{
	ev64260_halt();
	/* NOTREACHED */
}

static int
ev64260_show_cpuinfo(struct seq_file *m)
{
	uint pvid;

	pvid = mfspr(PVR);
	seq_printf(m, "vendor\t\t: Marvell/Galileo\n");
	seq_printf(m, "machine\t\t: EV-64260-BP\n");
	seq_printf(m, "PVID\t\t: 0x%x, vendor: %s\n",
			pvid, (pvid & (1<<15) ? "IBM" : "Motorola"));

	return 0;
}

/* DS1501 RTC has too much variation to use RTC for calibration */
static void __init
ev64260_calibrate_decr(void)
{
	ulong freq;

	freq = 100000000 / 4;

	printk("time_init: decrementer frequency = %lu.%.6lu MHz\n",
	       freq/1000000, freq%1000000);

	tb_ticks_per_jiffy = freq / HZ;
	tb_to_us = mulhwu_scale_factor(freq, 1000000);

	return;
}

#if defined(CONFIG_SERIAL_TEXT_DEBUG)
/*
 * Set BAT 3 to map 0xf0000000 to end of physical memory space.
 */
static __inline__ void
ev64260_set_bat(void)
{
	unsigned long   bat3u, bat3l;
	static int	mapping_set = 0;

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

#if !defined(CONFIG_GT64260_CONSOLE)
#include <linux/serialP.h>
#include <linux/serial_reg.h>
#include <asm/serial.h>

static struct serial_state rs_table[RS_TABLE_SIZE] = {
	SERIAL_PORT_DFNS	/* Defined in <asm/serial.h> */
};

static void
ev64260_16550_progress(char *s, unsigned short hex)
{
	volatile char c;
	volatile unsigned long com_port;
	u16 shift;

	com_port = rs_table[0].port;
	shift = rs_table[0].iomem_reg_shift;

	while ((c = *s++) != 0) {
		while ((*((volatile unsigned char *)com_port +
				(UART_LSR << shift)) & UART_LSR_THRE) == 0)
		                ;
	        *(volatile unsigned char *)com_port = c;

		if (c == '\n') {
			while ((*((volatile unsigned char *)com_port +
				(UART_LSR << shift)) & UART_LSR_THRE) == 0)
					;
	        	*(volatile unsigned char *)com_port = '\r';
		}
	}

	/* Move to next line on */
	while ((*((volatile unsigned char *)com_port +
		(UART_LSR << shift)) & UART_LSR_THRE) == 0)
			;
	*(volatile unsigned char *)com_port = '\n';
	while ((*((volatile unsigned char *)com_port +
		(UART_LSR << shift)) & UART_LSR_THRE) == 0)
			;
	*(volatile unsigned char *)com_port = '\r';

	return;
}
#endif	/* !CONFIG_GT64260_CONSOLE */
#endif	/* CONFIG_SERIAL_TEXT_DEBUG */

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());

	isa_mem_base = 0;

	ppc_md.setup_arch = ev64260_setup_arch;
	ppc_md.show_cpuinfo = ev64260_show_cpuinfo;
	ppc_md.irq_canonicalize = NULL;
	ppc_md.init_IRQ = ev64260_init_irq;
	ppc_md.get_irq = gt64260_get_irq;
	ppc_md.init = NULL;

	ppc_md.restart = ev64260_restart;
	ppc_md.power_off = ev64260_power_off;
	ppc_md.halt = ev64260_halt;

	ppc_md.find_end_of_memory = ev64260_find_end_of_memory;

	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;
	ppc_md.calibrate_decr = ev64260_calibrate_decr;

	ppc_md.nvram_read_val = todc_direct_read_val;
	ppc_md.nvram_write_val = todc_direct_write_val;

	ppc_md.heartbeat = NULL;
	ppc_md.heartbeat_reset = 0;
	ppc_md.heartbeat_count = 0;

#ifdef	CONFIG_SERIAL_TEXT_DEBUG
	ev64260_set_bat();
#ifdef	CONFIG_GT64260_CONSOLE
	gt64260_base = EV64260_BRIDGE_REG_BASE;
	ppc_md.progress = gt64260_mpsc_progress; /* embedded UART */
#else
	ppc_md.progress = ev64260_16550_progress; /* Dev module DUART */
#endif
#else	/* !CONFIG_SERIAL_TEXT_DEBUG */
	ppc_md.progress = NULL;
#endif	/* CONFIG_SERIAL_TEXT_DEBUG */

	return;
}
