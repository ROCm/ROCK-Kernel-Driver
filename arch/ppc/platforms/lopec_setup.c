/*
 * arch/ppc/platforms/lopec_setup.c
 *
 * Setup routines for the Motorola LoPEC.
 *
 * Author: Dan Cox
 *         danc@mvista.com
 *
 * 2001-2002 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/pci_ids.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/ide.h>
#include <linux/seq_file.h>
#include <linux/initrd.h>
#include <linux/console.h>
#include <linux/root_dev.h>

#include <asm/io.h>
#include <asm/open_pic.h>
#include <asm/i8259.h>
#include <asm/todc.h>
#include <asm/bootinfo.h>
#include <asm/mpc10x.h>
#include <asm/hw_irq.h>
#include <asm/prep_nvram.h>

extern char saved_command_line[];
extern void lopec_find_bridges(void);

/*
 * Define all of the IRQ senses and polarities.  Taken from the
 * LoPEC Programmer's Reference Guide.
 */
static u_char lopec_openpic_initsenses[16] __initdata = {
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* IRQ 0 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* IRQ 1 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* IRQ 2 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* IRQ 3 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* IRQ 4 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* IRQ 5 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* IRQ 6 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* IRQ 7 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* IRQ 8 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* IRQ 9 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* IRQ 10 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* IRQ 11 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* IRQ 12 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* IRQ 13 */
	(IRQ_SENSE_EDGE | IRQ_POLARITY_NEGATIVE),	/* IRQ 14 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE)	/* IRQ 15 */
};

static int
lopec_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "machine\t\t: Motorola LoPEC\n");
	return 0;
}

static u32
lopec_irq_canonicalize(u32 irq)
{
	if (irq == 2)
		return 9;
	else
		return irq;
}

static void
lopec_restart(char *cmd)
{
#define LOPEC_SYSSTAT1 0xffe00000
	/* force a hard reset, if possible */
	unsigned char reg = *((unsigned char *) LOPEC_SYSSTAT1);
	reg |= 0x80;
	*((unsigned char *) LOPEC_SYSSTAT1) = reg;

	local_irq_disable();
	while(1);
#undef LOPEC_SYSSTAT1
}

static void
lopec_halt(void)
{
	local_irq_disable();
	while(1);
}

static void
lopec_power_off(void)
{
	lopec_halt();
}

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
int lopec_ide_ports_known = 0;
static unsigned long lopec_ide_regbase[MAX_HWIFS];
static unsigned long lopec_ide_ctl_regbase[MAX_HWIFS];
static unsigned long lopec_idedma_regbase;

static void
lopec_ide_probe(void)
{
	struct pci_dev *dev = pci_find_device(PCI_VENDOR_ID_WINBOND,
					      PCI_DEVICE_ID_WINBOND_82C105,
					      NULL);
	lopec_ide_ports_known = 1;

	if (dev) {
		lopec_ide_regbase[0] = dev->resource[0].start;
		lopec_ide_regbase[1] = dev->resource[2].start;
		lopec_ide_ctl_regbase[0] = dev->resource[1].start;
		lopec_ide_ctl_regbase[1] = dev->resource[3].start;
		lopec_idedma_regbase = dev->resource[4].start;
	}
}

static int
lopec_ide_default_irq(unsigned long base)
{
	if (lopec_ide_ports_known == 0)
		lopec_ide_probe();

	if (base == lopec_ide_regbase[0])
		return 14;
	else if (base == lopec_ide_regbase[1])
		return 15;
	else
		return 0;
}

static unsigned long
lopec_ide_default_io_base(int index)
{
	if (lopec_ide_ports_known == 0)
		lopec_ide_probe();
	return lopec_ide_regbase[index];
}

static void __init
lopec_ide_init_hwif_ports(hw_regs_t *hw, unsigned long data,
			  unsigned long ctl, int *irq)
{
	unsigned long reg = data;
	uint alt_status_base;
	int i;

	for(i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++)
		hw->io_ports[i] = reg++;

	if (data == lopec_ide_regbase[0]) {
		alt_status_base = lopec_ide_ctl_regbase[0] + 2;
		hw->irq = 14;
	}
	else if (data == lopec_ide_regbase[1]) {
		alt_status_base = lopec_ide_ctl_regbase[1] + 2;
		hw->irq = 15;
	}
	else {
		alt_status_base = 0;
		hw->irq = 0;
	}

	if (ctl)
		hw->io_ports[IDE_CONTROL_OFFSET] = ctl;
	else
		hw->io_ports[IDE_CONTROL_OFFSET] = alt_status_base;

	if (irq != NULL)
		*irq = hw->irq;

}
#endif /* BLK_DEV_IDE */

static void __init
lopec_init_IRQ(void)
{
	int i;

	/*
	 * Provide the open_pic code with the correct table of interrupts.
	 */
	OpenPIC_InitSenses = lopec_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof(lopec_openpic_initsenses);

	/*
	 * We need to tell openpic_set_sources where things actually are.
	 * mpc10x_common will setup OpenPIC_Addr at ioremap(EUMB phys base +
	 * EPIC offset (0x40000));  The EPIC IRQ Register Address Map -
	 * Interrupt Source Configuration Registers gives these numbers
	 * as offsets starting at 0x50200, we need to adjust occordinly.
	 */
	/* Map serial interrupts 0-15 */
	openpic_set_sources(0, 16, OpenPIC_Addr + 0x10200);
	/* Skip reserved space and map i2c and DMA Ch[01] */
	openpic_set_sources(16, 3, OpenPIC_Addr + 0x11020);
	/* Skip reserved space and map Message Unit Interrupt (I2O) */
	openpic_set_sources(19, 1, OpenPIC_Addr + 0x110C0);

	openpic_init(NUM_8259_INTERRUPTS);

	/* Map i8259 interrupts */
	for(i = 0; i < NUM_8259_INTERRUPTS; i++)
		irq_desc[i].handler = &i8259_pic;

	/*
	 * The EPIC allows for a read in the range of 0xFEF00000 ->
	 * 0xFEFFFFFF to generate a PCI interrupt-acknowledge transaction.
	 */
	i8259_init(0xfef00000);
}

static int __init
lopec_request_io(void)
{
	outb(0x00, 0x4d0);
	outb(0xc0, 0x4d1);

	request_region(0x00, 0x20, "dma1");
	request_region(0x20, 0x20, "pic1");
	request_region(0x40, 0x20, "timer");
	request_region(0x80, 0x10, "dma page reg");
	request_region(0xa0, 0x20, "pic2");
	request_region(0xc0, 0x20, "dma2");

	return 0;
}

device_initcall(lopec_request_io);

static void __init
lopec_map_io(void)
{
	io_block_mapping(0xf0000000, 0xf0000000, 0x10000000, _PAGE_IO);
	io_block_mapping(0xb0000000, 0xb0000000, 0x10000000, _PAGE_IO);
}

static void __init
lopec_set_bat(void)
{
	unsigned long batu, batl;

	__asm__ __volatile__(
		"lis %0,0xf800\n \
                 ori %1,%0,0x002a\n \
                 ori %0,%0,0x0ffe\n \
                 mtspr 0x21e,%0\n \
                 mtspr 0x21f,%1\n \
                 isync\n \
                 sync "
		: "=r" (batu), "=r" (batl));
}

#ifdef  CONFIG_SERIAL_TEXT_DEBUG
#include <linux/serial.h>
#include <linux/serialP.h>
#include <linux/serial_reg.h>
#include <asm/serial.h>

static struct serial_state rs_table[RS_TABLE_SIZE] = {
	SERIAL_PORT_DFNS	/* Defined in <asm/serial.h> */
};

volatile unsigned char *com_port;
volatile unsigned char *com_port_lsr;

static void
serial_writechar(char c)
{
	while ((*com_port_lsr & UART_LSR_THRE) == 0)
		;
	*com_port = c;
}

void
lopec_progress(char *s, unsigned short hex)
{
	volatile char c;

	com_port = (volatile unsigned char *) rs_table[0].port;
	com_port_lsr = com_port + UART_LSR;

	while ((c = *s++) != 0)
		serial_writechar(c);

	/* Most messages don't have a newline in them */
	serial_writechar('\n');
	serial_writechar('\r');
}
#endif	/* CONFIG_SERIAL_TEXT_DEBUG */

TODC_ALLOC();

static void __init
lopec_setup_arch(void)
{

	TODC_INIT(TODC_TYPE_MK48T37, 0, 0,
		  ioremap(0xffe80000, 0x8000), 8);

	loops_per_jiffy = 100000000/HZ;

	lopec_find_bridges();

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#elif defined(CONFIG_ROOT_NFS)
        	ROOT_DEV = Root_NFS;
#elif defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
	        ROOT_DEV = Root_HDA1;
#else
        	ROOT_DEV = Root_SDA1;
#endif

#ifdef CONFIG_VT
	conswitchp = &dummy_con;
#endif
#ifdef CONFIG_PPCBUG_NVRAM
	/* Read in NVRAM data */
	init_prep_nvram();

	/* if no bootargs, look in NVRAM */
	if ( cmd_line[0] == '\0' ) {
		char *bootargs;
		 bootargs = prep_nvram_get_var("bootargs");
		 if (bootargs != NULL) {
			 strcpy(cmd_line, bootargs);
			 /* again.. */
			 strcpy(saved_command_line, cmd_line);
		}
	}
#endif
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());
	lopec_set_bat();

	isa_io_base = MPC10X_MAPB_ISA_IO_BASE;
	isa_mem_base = MPC10X_MAPB_ISA_MEM_BASE;
	pci_dram_offset = MPC10X_MAPB_DRAM_OFFSET;
	ISA_DMA_THRESHOLD = 0x00ffffff;
	DMA_MODE_READ = 0x44;
	DMA_MODE_WRITE = 0x48;

	ppc_md.setup_arch = lopec_setup_arch;
	ppc_md.show_cpuinfo = lopec_show_cpuinfo;
	ppc_md.irq_canonicalize = lopec_irq_canonicalize;
	ppc_md.init_IRQ = lopec_init_IRQ;
	ppc_md.get_irq = openpic_get_irq;

	ppc_md.restart = lopec_restart;
	ppc_md.power_off = lopec_power_off;
	ppc_md.halt = lopec_halt;

	ppc_md.setup_io_mappings = lopec_map_io;

	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;
	ppc_md.calibrate_decr = todc_calibrate_decr;

	ppc_md.nvram_read_val = todc_direct_read_val;
	ppc_md.nvram_write_val = todc_direct_write_val;

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
	ppc_ide_md.default_irq = lopec_ide_default_irq;
	ppc_ide_md.default_io_base = lopec_ide_default_io_base;
	ppc_ide_md.ide_init_hwif = lopec_ide_init_hwif_ports;
#endif
#ifdef CONFIG_SERIAL_TEXT_DEBUG
	ppc_md.progress = lopec_progress;
#endif
}
