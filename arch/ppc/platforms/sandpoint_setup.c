/*
 * arch/ppc/platforms/sandpoint_setup.c
 * 
 * Board setup routines for the Motorola SPS Sandpoint Test Platform.
 *
 * Author: Mark A. Greer
 *         mgreer@mvista.com
 *
 * 2000-2002 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

/*
 * This file adds support for the Motorola SPS Sandpoint Test Platform.
 * These boards have a PPMC slot for the processor so any combination
 * of cpu and host bridge can be attached.  This port is for an 8240 PPMC
 * module from Motorola SPS and other closely related cpu/host bridge
 * combinations (e.g., 750/755/7400 with MPC107 host bridge).
 * The sandpoint itself has a Windbond 83c553 (PCI-ISA bridge, 2 DMA ctlrs, 2
 * cascaded 8259 interrupt ctlrs, 8254 Timer/Counter, and an IDE ctlr), a
 * National 87308 (RTC, 2 UARTs, Keyboard & mouse ctlrs, and a floppy ctlr),
 * and 4 PCI slots (only 2 of which are usable; the other 2 are keyed for 3.3V
 * but are really 5V).
 *
 * The firmware on the sandpoint is called DINK (not my acronym :).  This port
 * depends on DINK to do some basic initialization (e.g., initialize the memory
 * ctlr) and to ensure that the processor is using MAP B (CHRP map).
 *
 * The switch settings for the Sandpoint board MUST be as follows:
 * 	S3: down
 * 	S4: up
 * 	S5: up
 * 	S6: down
 *
 * 'down' is in the direction from the PCI slots towards the PPMC slot;
 * 'up' is in the direction from the PPMC slot towards the PCI slots.
 * Be careful, the way the sandpoint board is installed in XT chasses will
 * make the directions reversed.
 *
 * Since Motorola listened to our suggestions for improvement, we now have
 * the Sandpoint X3 board.  All of the PCI slots are available, it uses
 * the serial interrupt interface (just a hardware thing we need to
 * configure properly).
 *
 * Use the default X3 switch settings.  The interrupts are then:
 *		EPIC	Source
 *		  0	SIOINT 		(8259, active low)
 *		  1	PCI #1
 *		  2	PCI #2
 *		  3	PCI #3
 *		  4	PCI #4
 *		  7	Winbond INTC	(IDE interrupt)
 *		  8	Winbond INTD	(IDE interrupt)
 *
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
#include <asm/vga.h>
#include <asm/open_pic.h>
#include <asm/i8259.h>
#include <asm/todc.h>
#include <asm/bootinfo.h>
#include <asm/mpc10x.h>
#include <asm/pci-bridge.h>

#include "sandpoint.h"

extern u_int openpic_irq(void);
extern void openpic_eoi(void);

static void	sandpoint_halt(void);


/*
 * *** IMPORTANT ***
 *
 * The first 16 entries of 'sandpoint_openpic_initsenses[]' are there and
 * initialized to 0 on purpose.  DO NOT REMOVE THEM as the 'offset' parameter
 * of 'openpic_init()' does not work for the sandpoint because the 8259
 * interrupt is NOT routed to the EPIC's IRQ 0 AND the EPIC's IRQ 0's offset is
 * the same as a normal openpic's IRQ 16 offset.
 */
static u_char sandpoint_openpic_initsenses[] __initdata = {
	0,	/* 0-15 not used by EPCI but by 8259 (std PC-type IRQs) */
	0,	/* 1 */
	0,	/* 2 */
	0,	/* 3 */
	0,	/* 4 */
	0,	/* 5 */
	0,	/* 6 */
	0,	/* 7 */
	0,	/* 8 */
	0,	/* 9 */
	0,	/* 10 */
	0,	/* 11 */
	0,	/* 12 */
	0,	/* 13 */
	0,	/* 14 */
	0,	/* 15 */
#ifdef CONFIG_SANDPOINT_X3
	1,	/* 16: EPIC IRQ 0: Active Low -- SIOINT (8259) */
 0, /* AACK!  Shouldn't need this.....see sandpoint_pci.c for more info */
	1,	/* 17: EPIC IRQ 1: Active Low -- PCI Slot 1 */
	1,	/* 18: EPIC IRQ 2: Active Low -- PCI Slot 2 */
	1,	/* 19: EPIC IRQ 3: Active Low -- PCI Slot 3 */
	1,	/* 20: EPIC IRQ 4: Active Low -- PCI Slot 4 */
	0,	/* 21 -- Unused */
	0,	/* 22 -- Unused */
	1,	/* 23 -- IDE (Winbond INT C)  */
	1,	/* 24 -- IDE (Winbond INT D)  */
		/* 35 - 31 (EPIC 9 - 15) Unused */
#else
	1,	/* 16: EPIC IRQ 0: Active Low -- PCI intrs */
	1,	/* 17: EPIC IRQ 1: Active Low -- PCI (possibly 8259) intrs */
	1,	/* 18: EPIC IRQ 2: Active Low -- PCI (possibly 8259) intrs  */
	1	/* 19: EPIC IRQ 3: Active Low -- PCI intrs */
		/* 20: EPIC IRQ 4: Not used */
#endif
};

static void __init
sandpoint_setup_arch(void)
{
	loops_per_jiffy = 100000000 / HZ;

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
#ifdef	CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
		ROOT_DEV = Root_HDA1;
#endif

	/* Lookup PCI host bridges */
	sandpoint_find_bridges();

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	printk("Motorola SPS Sandpoint Test Platform\n");
	printk("Sandpoint port (MontaVista Software, Inc. (source@mvista.com))\n");

	/* The Sandpoint rom doesn't enable any caches.  Do that now.
	 * The 7450 portion will also set up the L3s once I get enough
	 * information do do so.  If the processor running doesn't have
	 * and L2, the _set_L2CR is a no-op.
	 */
	if (cur_cpu_spec[0]->cpu_features & CPU_FTR_SPEC7450) {
		/* Just enable L2, the bits are different from others.
		*/
		_set_L2CR(L2CR_L2E);
	}
	else {
		/* The magic number for Sandpoint/74xx PrPMCs.
		*/
		_set_L2CR(0xbd014000);
	}
}

#define	SANDPOINT_87308_CFG_ADDR		0x15c
#define	SANDPOINT_87308_CFG_DATA		0x15d

#define	SANDPOINT_87308_CFG_INB(addr, byte) {				\
	outb((addr), SANDPOINT_87308_CFG_ADDR);				\
	(byte) = inb(SANDPOINT_87308_CFG_DATA);				\
}

#define	SANDPOINT_87308_CFG_OUTB(addr, byte) {				\
	outb((addr), SANDPOINT_87308_CFG_ADDR);				\
	outb((byte), SANDPOINT_87308_CFG_DATA);				\
}

#define SANDPOINT_87308_SELECT_DEV(dev_num) {				\
	SANDPOINT_87308_CFG_OUTB(0x07, (dev_num));			\
}

#define	SANDPOINT_87308_DEV_ENABLE(dev_num) {				\
	SANDPOINT_87308_SELECT_DEV(dev_num);				\
	SANDPOINT_87308_CFG_OUTB(0x30, 0x01);				\
}

/*
 * Initialize the ISA devices on the Nat'l PC87308VUL SuperIO chip.
 */
static void __init
sandpoint_setup_natl_87308(void)
{
	u_char	reg;

	/*
	 * Enable all the devices on the Super I/O chip.
	 */
	SANDPOINT_87308_SELECT_DEV(0x00); /* Select kbd logical device */
	SANDPOINT_87308_CFG_OUTB(0xf0, 0x00); /* Set KBC clock to 8 Mhz */
	SANDPOINT_87308_DEV_ENABLE(0x00); /* Enable keyboard */
	SANDPOINT_87308_DEV_ENABLE(0x01); /* Enable mouse */
	SANDPOINT_87308_DEV_ENABLE(0x02); /* Enable rtc */
	SANDPOINT_87308_DEV_ENABLE(0x03); /* Enable fdc (floppy) */
	SANDPOINT_87308_DEV_ENABLE(0x04); /* Enable parallel */
	SANDPOINT_87308_DEV_ENABLE(0x05); /* Enable UART 2 */
	SANDPOINT_87308_CFG_OUTB(0xf0, 0x82); /* Enable bank select regs */
	SANDPOINT_87308_DEV_ENABLE(0x06); /* Enable UART 1 */
	SANDPOINT_87308_CFG_OUTB(0xf0, 0x82); /* Enable bank select regs */

	/* Set up floppy in PS/2 mode */
	outb(0x09, SIO_CONFIG_RA);
	reg = inb(SIO_CONFIG_RD);
	reg = (reg & 0x3F) | 0x40;
	outb(reg, SIO_CONFIG_RD);
	outb(reg, SIO_CONFIG_RD);       /* Have to write twice to change! */

	return;
}

/*
 * Fix IDE interrupts.
 */
static void __init
sandpoint_fix_winbond_83553(void)
{
	/* Make all 8259 interrupt level sensitive */
	outb(0xf8, 0x4d0);
	outb(0xde, 0x4d1);

	return;
}

static void __init
sandpoint_init2(void)
{
	/* Do Sandpoint board specific initialization.  */
	sandpoint_fix_winbond_83553();
	sandpoint_setup_natl_87308();

	request_region(0x00,0x20,"dma1");
	request_region(0x20,0x20,"pic1");
	request_region(0x40,0x20,"timer");
	request_region(0x80,0x10,"dma page reg");
	request_region(0xa0,0x20,"pic2");
	request_region(0xc0,0x20,"dma2");

	return;
}

/*
 * Interrupt setup and service.  Interrrupts on the Sandpoint come
 * from the four PCI slots plus the 8259 in the Winbond Super I/O (SIO).
 * These interrupts are sent to one of four IRQs on the EPIC.
 * The SIO shares its interrupt with either slot 2 or slot 3 (INTA#).
 * Slot numbering is confusing.  Sometimes in the documentation they
 * use 0,1,2,3 and others 1,2,3,4.  We will use slots 1,2,3,4 and
 * map this to IRQ 16, 17, 18, 19.
 * For Sandpoint X3, this has been better designed.  The 8259 is
 * cascaded from EPIC IRQ0, IRQ1-4 map to PCI slots 1-4, IDE is on
 * EPIC 7 and 8.
 */
static void __init
sandpoint_init_IRQ(void)
{
	int i;

	/*
	 * 3 things cause us to jump through some hoops:
	 *   1) the EPIC on the 8240 & 107 are not full-blown openpic pic's
	 *   2) the 8259 is NOT cascaded on the openpic IRQ 0
	 *   3) the 8259 shares its interrupt line with some PCI interrupts.
	 *
	 * What we'll do is set up the 8259 to be level sensitive, active low
	 * just like a PCI device.  Then, when an interrupt on the IRQ that is
	 * shared with the 8259 comes in, we'll take a peek at the 8259 to see
	 * it its generating an interrupt.  If it is, we'll handle the 8259
	 * interrupt.  Otherwise, we'll handle it just like a normal PCI
	 * interrupt.  This does give the 8259 interrupts a higher priority
	 * than the EPIC ones--hopefully, not a problem.
	 */
	OpenPIC_InitSenses = sandpoint_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof(sandpoint_openpic_initsenses);

	openpic_init(1, 0, NULL, -1);

	/*
	 * openpic_init() has set up irq_desc[0-23] to be openpic
	 * interrupts.  We need to set irq_desc[0-15] to be 8259 interrupts.
	 * We then need to request and enable the 8259 irq.
	 */
	for(i=0; i < NUM_8259_INTERRUPTS; i++) 
		irq_desc[i].handler = &i8259_pic;

	if (request_irq(SANDPOINT_SIO_IRQ, no_action, SA_INTERRUPT,
			"8259 cascade to EPIC", NULL)) {
	
		printk("Unable to get OpenPIC IRQ %d for cascade\n",
			SANDPOINT_SIO_IRQ);
	}

	i8259_init(NULL);
}

static int
sandpoint_get_irq(struct pt_regs *regs)
{
        int	irq, cascade_irq;

	irq = openpic_irq();

	if (irq == SANDPOINT_SIO_IRQ) {
		cascade_irq = i8259_irq(regs);

		if (cascade_irq != -1) {
			irq = cascade_irq;
			openpic_eoi();
		}
	}
	else if (irq == OPENPIC_VEC_SPURIOUS) {
		irq = -1;
	}

	return irq;
}

static u32
sandpoint_irq_cannonicalize(u32 irq)
{
	if (irq == 2)
	{
		return 9;
	}
	else
	{
		return irq;
	}
}

static ulong __init
sandpoint_find_end_of_memory(void)
{
	ulong	size = 0;

#if 0	/* Leave out until DINK sets mem ctlr correctly */
	size = mpc10x_get_mem_size(MPC10X_MEM_MAP_B);
#else
	size = 32*1024*1024;
#endif

	return size;
}

static void __init
sandpoint_map_io(void)
{
	io_block_mapping(0xfe000000, 0xfe000000, 0x02000000, _PAGE_IO);
}

/*
 * Due to Sandpoint X2 errata, the Port 92 will not work.
 */
static void
sandpoint_restart(char *cmd)
{
	local_irq_disable();

	/* Set exception prefix high - to the firmware */
	_nmask_and_or_msr(0, MSR_IP);

	/* Reset system via Port 92 */
	outb(0x00, 0x92);
	outb(0x01, 0x92);
	for(;;);	/* Spin until reset happens */
}

static void
sandpoint_power_off(void)
{
	local_irq_disable();
	for(;;);  /* No way to shut power off with software */
	/* NOTREACHED */
}

static void
sandpoint_halt(void)
{
	sandpoint_power_off();
	/* NOTREACHED */
}

static int
sandpoint_show_cpuinfo(struct seq_file *m)
{
	uint pvid;

	pvid = mfspr(PVR);

	seq_printf(m, "vendor\t\t: Motorola SPS\n");
	seq_printf(m, "machine\t\t: Sandpoint\n");
	seq_printf(m, "processor\t: PVID: 0x%x, vendor: %s\n",
			pvid, (pvid & (1<<15) ? "IBM" : "Motorola"));

	return 0;
}

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
/*
 * IDE support.
 */
static int		sandpoint_ide_ports_known = 0;
static unsigned long	sandpoint_ide_regbase[MAX_HWIFS];
static unsigned long	sandpoint_ide_ctl_regbase[MAX_HWIFS];
static unsigned long	sandpoint_idedma_regbase;

static void
sandpoint_ide_probe(void)
{
        struct pci_dev *pdev = pci_find_device(PCI_VENDOR_ID_WINBOND,
					       PCI_DEVICE_ID_WINBOND_82C105,
					       NULL);

        if(pdev) {
                sandpoint_ide_regbase[0]=pdev->resource[0].start;
                sandpoint_ide_regbase[1]=pdev->resource[2].start;
                sandpoint_ide_ctl_regbase[0]=pdev->resource[1].start;
                sandpoint_ide_ctl_regbase[1]=pdev->resource[3].start;
                sandpoint_idedma_regbase=pdev->resource[4].start;
        }

        sandpoint_ide_ports_known = 1;
	return;
}

static int
sandpoint_ide_default_irq(unsigned long base)
{
        if (sandpoint_ide_ports_known == 0)
	        sandpoint_ide_probe();

	if (base == sandpoint_ide_regbase[0])
		return SANDPOINT_IDE_INT0;
	else if (base == sandpoint_ide_regbase[1])
		return SANDPOINT_IDE_INT1;
	else
		return 0;
}

static unsigned long
sandpoint_ide_default_io_base(int index)
{
        if (sandpoint_ide_ports_known == 0)
	        sandpoint_ide_probe();

	return sandpoint_ide_regbase[index];
}

static void __init
sandpoint_ide_init_hwif_ports(hw_regs_t *hw, unsigned long data_port,
			      unsigned long ctrl_port, int *irq)
{
	unsigned long reg = data_port;
	uint	alt_status_base;
	int	i;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg++;
	}

	if (data_port == sandpoint_ide_regbase[0]) {
		alt_status_base = sandpoint_ide_ctl_regbase[0] + 2;
		hw->irq = 14;
	}
	else if (data_port == sandpoint_ide_regbase[1]) {
		alt_status_base = sandpoint_ide_ctl_regbase[1] + 2;
		hw->irq = 15;
	}
	else {
		alt_status_base = 0;
		hw->irq = 0;
	}

	if (ctrl_port) {
		hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;
	} else {
		hw->io_ports[IDE_CONTROL_OFFSET] = alt_status_base;
	}

	if (irq != NULL) {
		*irq = hw->irq;
	}

	return;
}
#endif

/*
 * Set BAT 3 to map 0xf8000000 to end of physical memory space 1-to-1.
 */
static __inline__ void
sandpoint_set_bat(void)
{
	unsigned long   bat3u, bat3l;
	static int	mapping_set = 0;

	if (!mapping_set) {

		__asm__ __volatile__(
		" lis %0,0xf800\n \
		  ori %1,%0,0x002a\n \
		  ori %0,%0,0x0ffe\n \
		  mtspr 0x21e,%0\n \
		  mtspr 0x21f,%1\n \
		  isync\n \
		  sync "
		  : "=r" (bat3u), "=r" (bat3l));

		mapping_set = 1;
	}

	return;
}

#ifdef	CONFIG_SERIAL_TEXT_DEBUG
#include <linux/serialP.h>
#include <linux/serial_reg.h>
#include <asm/serial.h>

static struct serial_state rs_table[RS_TABLE_SIZE] = {
	SERIAL_PORT_DFNS	/* Defined in <asm/serial.h> */
};

static void
sandpoint_progress(char *s, unsigned short hex)
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

__init void sandpoint_setup_pci_ptrs(void);

TODC_ALLOC();

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());

	/* Map in board regs, etc. */
	sandpoint_set_bat();

	isa_io_base = MPC10X_MAPB_ISA_IO_BASE;
	isa_mem_base = MPC10X_MAPB_ISA_MEM_BASE;
	pci_dram_offset = MPC10X_MAPB_DRAM_OFFSET;
	ISA_DMA_THRESHOLD = 0x00ffffff;
	DMA_MODE_READ = 0x44;
	DMA_MODE_WRITE = 0x48;

	ppc_md.setup_arch = sandpoint_setup_arch;
	ppc_md.show_cpuinfo = sandpoint_show_cpuinfo;
	ppc_md.irq_cannonicalize = sandpoint_irq_cannonicalize;
	ppc_md.init_IRQ = sandpoint_init_IRQ;
	ppc_md.get_irq = sandpoint_get_irq;
	ppc_md.init = sandpoint_init2;

	ppc_md.restart = sandpoint_restart;
	ppc_md.power_off = sandpoint_power_off;
	ppc_md.halt = sandpoint_halt;

	ppc_md.find_end_of_memory = sandpoint_find_end_of_memory;
	ppc_md.setup_io_mappings = sandpoint_map_io;

	TODC_INIT(TODC_TYPE_PC97307, 0x70, 0x00, 0x71, 8);
	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;
	ppc_md.calibrate_decr = todc_calibrate_decr;

	ppc_md.nvram_read_val = todc_mc146818_read_val;
	ppc_md.nvram_write_val = todc_mc146818_write_val;

	ppc_md.heartbeat = NULL;
	ppc_md.heartbeat_reset = 0;
	ppc_md.heartbeat_count = 0;

#ifdef	CONFIG_SERIAL_TEXT_DEBUG
	ppc_md.progress = sandpoint_progress;
#else	/* !CONFIG_SERIAL_TEXT_DEBUG */
	ppc_md.progress = NULL;
#endif	/* CONFIG_SERIAL_TEXT_DEBUG */

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
        ppc_ide_md.default_irq = sandpoint_ide_default_irq;
        ppc_ide_md.default_io_base = sandpoint_ide_default_io_base;
        ppc_ide_md.ide_init_hwif = sandpoint_ide_init_hwif_ports;
#endif

	return;
}
