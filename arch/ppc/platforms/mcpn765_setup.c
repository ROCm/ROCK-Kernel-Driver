/*
 * arch/ppc/platforms/mcpn765_setup.c
 *
 * Board setup routines for the Motorola MCG MCPN765 cPCI Board.
 *
 * Author: Mark A. Greer
 *         mgreer@mvista.com
 *
 * 2001-2002 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

/*
 * This file adds support for the Motorola MCG MCPN765.
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
#include <linux/blk.h>
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
#include <asm/todc.h>
#include <asm/pci-bridge.h>
#include <asm/bootinfo.h>
#include <asm/pplus.h>

#include "mcpn765.h"

static u_char mcpn765_openpic_initsenses[] __initdata = {
	0,	/* 16: i8259 cascade (active high) */
	1,	/* 17: COM1,2,3,4 */
	1,	/* 18: Enet 1 (front panel) */
	1,	/* 19: HAWK WDT XXXX */
	1,	/* 20: 21554 PCI-PCI bridge */
	1,	/* 21: cPCI INTA# */
	1,	/* 22: cPCI INTB# */
	1,	/* 23: cPCI INTC# */
	1,	/* 24: cPCI INTD# */
	1,	/* 25: PMC1 INTA#, PMC2 INTB# */
	1,	/* 26: PMC1 INTB#, PMC2 INTC# */
	1,	/* 27: PMC1 INTC#, PMC2 INTD# */
	1,	/* 28: PMC1 INTD#, PMC2 INTA# */
	1,	/* 29: Enet 2 (connected to J3) */
	1,	/* 30: Abort Switch */
	1,	/* 31: RTC Alarm */
};


extern u_int openpic_irq(void);
extern char cmd_line[];

int use_of_interrupt_tree = 0;

static void mcpn765_halt(void);

TODC_ALLOC();

static void __init
mcpn765_setup_arch(void)
{
	struct pci_controller *hose;

	if ( ppc_md.progress )
		ppc_md.progress("mcpn765_setup_arch: enter", 0);

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
		ppc_md.progress("mcpn765_setup_arch: find_bridges", 0);

	/* Lookup PCI host bridges */
	mcpn765_find_bridges();

	hose = pci_bus_to_hose(0);
	isa_io_base = (ulong)hose->io_base_virt;

	TODC_INIT(TODC_TYPE_MK48T37,
		  (MCPN765_PHYS_NVRAM_AS0 - isa_io_base),
		  (MCPN765_PHYS_NVRAM_AS1 - isa_io_base),
		  (MCPN765_PHYS_NVRAM_DATA - isa_io_base),
		  8);

	OpenPIC_InitSenses = mcpn765_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof(mcpn765_openpic_initsenses);

	printk("Motorola MCG MCPN765 cPCI Non-System Board\n");
	printk("MCPN765 port (MontaVista Software, Inc. (source@mvista.com))\n");

	if ( ppc_md.progress )
		ppc_md.progress("mcpn765_setup_arch: exit", 0);

	return;
}

/*
 * Initialize the VIA 82c586b.
 */
static void __init
mcpn765_setup_via_82c586b(void)
{
	struct pci_dev	*dev;
	u_char		c;

	if ((dev = pci_find_device(PCI_VENDOR_ID_VIA,
				   PCI_DEVICE_ID_VIA_82C586_1,
				   NULL)) == NULL) {
		printk("No VIA ISA bridge found\n");
		mcpn765_halt();
		/* NOTREACHED */
	}

	/*
	 * PPCBug doesn't set the enable bits for the IDE device.
	 * Turn them on now.
	 */
	pci_read_config_byte(dev, 0x40, &c);
	c |= 0x03;
	pci_write_config_byte(dev, 0x40, c);

	return;
}

static void __init
mcpn765_init2(void)
{
	/* Do MCPN765 board specific initialization.  */
	mcpn765_setup_via_82c586b();

	request_region(0x00,0x20,"dma1");
	request_region(0x20,0x20,"pic1");
	request_region(0x40,0x20,"timer");
	request_region(0x80,0x10,"dma page reg");
	request_region(0xa0,0x20,"pic2");
	request_region(0xc0,0x20,"dma2");

	return;
}

/*
 * Interrupt setup and service.
 * Have MPIC on HAWK and cascaded 8259s on VIA 82586 cascaded to MPIC.
 */
static void __init
mcpn765_init_IRQ(void)
{
	int i;

	if ( ppc_md.progress )
		ppc_md.progress("init_irq: enter", 0);

	openpic_init(1, NUM_8259_INTERRUPTS, NULL, -1);

	for(i=0; i < NUM_8259_INTERRUPTS; i++)
		irq_desc[i].handler = &i8259_pic;

	i8259_init(NULL);

	if ( ppc_md.progress )
		ppc_md.progress("init_irq: exit", 0);

	return;
}

static u32
mcpn765_irq_cannonicalize(u32 irq)
{
	if (irq == 2)
		return 9;
	else
		return irq;
}

static unsigned long __init
mcpn765_find_end_of_memory(void)
{
	return pplus_get_mem_size(MCPN765_HAWK_SMC_BASE);
}

static void __init
mcpn765_map_io(void)
{
	io_block_mapping(0xfe800000, 0xfe800000, 0x00800000, _PAGE_IO);
}

static void
mcpn765_reset_board(void)
{
	local_irq_disable();

	/* Set exception prefix high - to the firmware */
	_nmask_and_or_msr(0, MSR_IP);

	out_8((u_char *)MCPN765_BOARD_MODRST_REG, 0x01);

	return;
}

static void
mcpn765_restart(char *cmd)
{
	volatile ulong	i = 10000000;

	mcpn765_reset_board();

	while (i-- > 0);
	panic("restart failed\n");
}

static void
mcpn765_power_off(void)
{
	mcpn765_halt();
	/* NOTREACHED */
}

static void
mcpn765_halt(void)
{
	local_irq_disable();
	while (1);
	/* NOTREACHED */
}

static int
mcpn765_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "vendor\t\t: Motorola MCG\n");
	seq_printf(m, "machine\t\t: MCPN765\n");

	return 0;
}

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
/*
 * IDE support.
 */
static int		mcpn765_ide_ports_known = 0;
static unsigned long	mcpn765_ide_regbase[MAX_HWIFS];
static unsigned long	mcpn765_ide_ctl_regbase[MAX_HWIFS];
static unsigned long	mcpn765_idedma_regbase;

static void
mcpn765_ide_probe(void)
{
	struct pci_dev *pdev = pci_find_device(PCI_VENDOR_ID_VIA,
					       PCI_DEVICE_ID_VIA_82C586_1,
					       NULL);

        if(pdev) {
                mcpn765_ide_regbase[0]=pdev->resource[0].start;
                mcpn765_ide_regbase[1]=pdev->resource[2].start;
                mcpn765_ide_ctl_regbase[0]=pdev->resource[1].start;
                mcpn765_ide_ctl_regbase[1]=pdev->resource[3].start;
                mcpn765_idedma_regbase=pdev->resource[4].start;
        }

        mcpn765_ide_ports_known = 1;
	return;
}

static int
mcpn765_ide_default_irq(unsigned long base)
{
        if (mcpn765_ide_ports_known == 0)
	        mcpn765_ide_probe();

	if (base == mcpn765_ide_regbase[0])
		return 14;
	else if (base == mcpn765_ide_regbase[1])
		return 14;
	else
		return 0;
}

static unsigned long
mcpn765_ide_default_io_base(int index)
{
        if (mcpn765_ide_ports_known == 0)
	        mcpn765_ide_probe();

	return mcpn765_ide_regbase[index];
}

static void __init
mcpn765_ide_init_hwif_ports(hw_regs_t *hw, unsigned long data_port,
			      unsigned long ctrl_port, int *irq)
{
	unsigned long reg = data_port;
	uint	alt_status_base;
	int	i;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg++;
	}

	if (data_port == mcpn765_ide_regbase[0]) {
		alt_status_base = mcpn765_ide_ctl_regbase[0] + 2;
		hw->irq = 14;
	} else if (data_port == mcpn765_ide_regbase[1]) {
		alt_status_base = mcpn765_ide_ctl_regbase[1] + 2;
		hw->irq = 14;
	} else {
		alt_status_base = 0;
		hw->irq = 0;
	}

	if (ctrl_port)
		hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;
	else
		hw->io_ports[IDE_CONTROL_OFFSET] = alt_status_base;

	if (irq != NULL)
		*irq = hw->irq;

	return;
}
#endif

/*
 * Set BAT 3 to map 0xf0000000 to end of physical memory space.
 */
static __inline__ void
mcpn765_set_bat(void)
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

#ifdef CONFIG_SERIAL_TEXT_DEBUG
#include <linux/serialP.h>
#include <linux/serial_reg.h>
#include <asm/serial.h>

static struct serial_state rs_table[RS_TABLE_SIZE] = {
	SERIAL_PORT_DFNS	/* Defined in <asm/serial.h> */
};

static void
mcpn765_progress(char *s, unsigned short hex)
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
}
#endif	/* CONFIG_SERIAL_TEXT_DEBUG */

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());

	/* Map in board regs, etc. */
	mcpn765_set_bat();

	isa_mem_base = MCPN765_ISA_MEM_BASE;
	pci_dram_offset = MCPN765_PCI_DRAM_OFFSET;
	ISA_DMA_THRESHOLD = 0x00ffffff;
	DMA_MODE_READ = 0x44;
	DMA_MODE_WRITE = 0x48;

	ppc_md.setup_arch = mcpn765_setup_arch;
	ppc_md.show_cpuinfo = mcpn765_show_cpuinfo;
	ppc_md.irq_cannonicalize = mcpn765_irq_cannonicalize;
	ppc_md.init_IRQ = mcpn765_init_IRQ;
	ppc_md.get_irq = openpic_get_irq;
	ppc_md.init = mcpn765_init2;

	ppc_md.restart = mcpn765_restart;
	ppc_md.power_off = mcpn765_power_off;
	ppc_md.halt = mcpn765_halt;

	ppc_md.find_end_of_memory = mcpn765_find_end_of_memory;
	ppc_md.setup_io_mappings = mcpn765_map_io;

	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;
	ppc_md.calibrate_decr = todc_calibrate_decr;

	ppc_md.nvram_read_val = todc_m48txx_read_val;
	ppc_md.nvram_write_val = todc_m48txx_write_val;

	ppc_md.heartbeat = NULL;
	ppc_md.heartbeat_reset = 0;
	ppc_md.heartbeat_count = 0;

#ifdef	CONFIG_SERIAL_TEXT_DEBUG
	ppc_md.progress = mcpn765_progress;
#else	/* !CONFIG_SERIAL_TEXT_DEBUG */
	ppc_md.progress = NULL;
#endif	/* CONFIG_SERIAL_TEXT_DEBUG */

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
        ppc_ide_md.default_irq = mcpn765_ide_default_irq;
        ppc_ide_md.default_io_base = mcpn765_ide_default_io_base;
        ppc_ide_md.ide_init_hwif = mcpn765_ide_init_hwif_ports;
#endif

	return;
}
