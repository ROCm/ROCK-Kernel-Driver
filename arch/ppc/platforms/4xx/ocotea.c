/*
 * arch/ppc/platforms/ocotea.c
 *
 * Ocotea board specific routines
 *
 * Matt Porter <mporter@mvista.com>
 *
 * Copyright 2003 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
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
#include <linux/blkdev.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/initrd.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serial_core.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/time.h>
#include <asm/todc.h>
#include <asm/bootinfo.h>
#include <asm/ppc4xx_pic.h>

extern void abort(void);

/* Global Variables */
bd_t __res;

static void __init
ocotea_calibrate_decr(void)
{
	unsigned int freq;

	freq = OCOTEA_SYSCLK;

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

static int
ocotea_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "vendor\t\t: IBM\n");
	seq_printf(m, "machine\t\t: PPC440GX EVB (Ocotea)\n");

	return 0;
}
static inline int
ocotea_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	/*
	 *	PCI IDSEL/INTPIN->INTLINE
	 * 	   A   B   C   D
	 */
	{
		{ 23, 23, 23, 23 },	/* IDSEL 1 - PCI Slot 0 */
		{ 24, 24, 24, 24 },	/* IDSEL 2 - PCI Slot 1 */
		{ 25, 25, 25, 25 },	/* IDSEL 3 - PCI Slot 2 */
		{ 26, 26, 26, 26 },	/* IDSEL 4 - PCI Slot 3 */
	};

	const long min_idsel = 1, max_idsel = 4, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
}

#define PCIX_READW(offset) \
	(readw((u32)pcix_reg_base+offset))

#define PCIX_WRITEW(value, offset) \
	(writew(value, (u32)pcix_reg_base+offset))

#define PCIX_WRITEL(value, offset) \
	(writel(value, (u32)pcix_reg_base+offset))

/*
 * FIXME: This is only here to "make it work".  This will move
 * to a ibm_pcix.c which will contain a generic IBM PCIX bridge
 * configuration library. -Matt
 */
static void __init
ocotea_setup_pcix(void)
{
	void *pcix_reg_base;

	pcix_reg_base = ioremap64(PCIX0_REG_BASE, PCIX0_REG_SIZE);

	/* Enable PCIX0 I/O, Mem, and Busmaster cycles */
	PCIX_WRITEW(PCIX_READW(PCIX0_COMMAND) | PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER, PCIX0_COMMAND);

	/* Disable all windows */
	PCIX_WRITEL(0, PCIX0_POM0SA);
	PCIX_WRITEL(0, PCIX0_POM1SA);
	PCIX_WRITEL(0, PCIX0_POM2SA);
	PCIX_WRITEL(0, PCIX0_PIM0SA);
	PCIX_WRITEL(0, PCIX0_PIM0SAH);
	PCIX_WRITEL(0, PCIX0_PIM1SA);
	PCIX_WRITEL(0, PCIX0_PIM2SA);
	PCIX_WRITEL(0, PCIX0_PIM2SAH);

	/* Setup 2GB PLB->PCI outbound mem window (3_8000_0000->0_8000_0000) */
	PCIX_WRITEL(0x00000003, PCIX0_POM0LAH);
	PCIX_WRITEL(0x80000000, PCIX0_POM0LAL);
	PCIX_WRITEL(0x00000000, PCIX0_POM0PCIAH);
	PCIX_WRITEL(0x80000000, PCIX0_POM0PCIAL);
	PCIX_WRITEL(0x80000001, PCIX0_POM0SA);

	/* Setup 2GB PCI->PLB inbound memory window at 0, enable MSIs */
	PCIX_WRITEL(0x00000000, PCIX0_PIM0LAH);
	PCIX_WRITEL(0x00000000, PCIX0_PIM0LAL);
	PCIX_WRITEL(0xe0000007, PCIX0_PIM0SA);

	eieio();
}

static void __init
ocotea_setup_hose(void)
{
	struct pci_controller *hose;

	/* Configure windows on the PCI-X host bridge */
	ocotea_setup_pcix();

	hose = pcibios_alloc_controller();

	if (!hose)
		return;

	hose->first_busno = 0;
	hose->last_busno = 0xff;

	hose->pci_mem_offset = OCOTEA_PCI_MEM_OFFSET;

	pci_init_resource(&hose->io_resource,
			OCOTEA_PCI_LOWER_IO,
			OCOTEA_PCI_UPPER_IO,
			IORESOURCE_IO,
			"PCI host bridge");

	pci_init_resource(&hose->mem_resources[0],
			OCOTEA_PCI_LOWER_MEM,
			OCOTEA_PCI_UPPER_MEM,
			IORESOURCE_MEM,
			"PCI host bridge");

	hose->io_space.start = OCOTEA_PCI_LOWER_IO;
	hose->io_space.end = OCOTEA_PCI_UPPER_IO;
	hose->mem_space.start = OCOTEA_PCI_LOWER_MEM;
	hose->mem_space.end = OCOTEA_PCI_UPPER_MEM;
	isa_io_base =
		(unsigned long)ioremap64(OCOTEA_PCI_IO_BASE, OCOTEA_PCI_IO_SIZE);
	hose->io_base_virt = (void *)isa_io_base;

	setup_indirect_pci(hose,
			OCOTEA_PCI_CFGA_PLB32,
			OCOTEA_PCI_CFGD_PLB32);
	hose->set_cfg_type = 1;

	hose->last_busno = pciauto_bus_scan(hose, hose->first_busno);

	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = ocotea_map_irq;
}


TODC_ALLOC();

static void __init
ocotea_early_serial_map(void)
{
	struct uart_port port;

	/* Setup ioremapped serial port access */
	memset(&port, 0, sizeof(port));
	port.membase = ioremap64(PPC440GX_UART0_ADDR, 8);
	port.irq = 0;
	port.uartclk = BASE_BAUD * 16;
	port.regshift = 0;
	port.iotype = SERIAL_IO_MEM;
	port.flags = ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST;
	port.line = 0;

	if (early_serial_setup(&port) != 0) {
		printk("Early serial init of port 0 failed\n");
	}

	port.membase = ioremap64(PPC440GX_UART1_ADDR, 8);
	port.irq = 1;
	port.line = 1;

	if (early_serial_setup(&port) != 0) {
		printk("Early serial init of port 1 failed\n");
	}
}

static void __init
ocotea_setup_arch(void)
{
	unsigned char *addr;
	unsigned long long mac64;

	/* Retrieve MAC addresses from flash */
	addr = ioremap64(OCOTEA_MAC_BASE, OCOTEA_MAC_SIZE);
	mac64 = simple_strtoull(addr, 0, 16);
	memcpy(__res.bi_enetaddr[0], (char *)&mac64+2, 6);
	mac64 = simple_strtoull(addr+OCOTEA_MAC1_OFFSET, 0, 16);
	memcpy(__res.bi_enetaddr[1], (char *)&mac64+2, 6);
	iounmap(addr);

#if !defined(CONFIG_BDI_SWITCH)
	/*
	 * The Abatron BDI JTAG debugger does not tolerate others
	 * mucking with the debug registers.
	 */
        mtspr(SPRN_DBCR0, (DBCR0_TDE | DBCR0_IDM));
#endif

	/* Setup TODC access */
	TODC_INIT(TODC_TYPE_DS1743,
			0,
			0,
			ioremap64(OCOTEA_RTC_ADDR, OCOTEA_RTC_SIZE),
			8);

	/* init to some ~sane value until calibrate_delay() runs */
        loops_per_jiffy = 50000000/HZ;

	/* Setup PCI host bridge */
	ocotea_setup_hose();

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
#ifdef CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
		ROOT_DEV = Root_HDA1;
#endif

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	ocotea_early_serial_map();

	/* Identify the system */
	printk("IBM Ocotea port (MontaVista Software, Inc. <source@mvista.com>)\n");
}

static void
ocotea_restart(char *cmd)
{
	local_irq_disable();
	abort();
}

static void
ocotea_power_off(void)
{
	local_irq_disable();
	for(;;);
}

static void
ocotea_halt(void)
{
	local_irq_disable();
	for(;;);
}

/*
 * Read the 440GX memory controller to get size of system memory.
 */
static unsigned long __init
ocotea_find_end_of_memory(void)
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

static void __init
ocotea_init_irq(void)
{
	int i;

	/* Enable PPC440GP interrupt compatibility mode */
	SDR_WRITE(DCRN_SDR_MFR,SDR_READ(DCRN_SDR_MFR) | DCRN_SDR_MFR_PCM);

	ppc4xx_pic_init();

	for (i = 0; i < NR_IRQS; i++)
		irq_desc[i].handler = ppc4xx_pic;
}

#ifdef CONFIG_SERIAL_TEXT_DEBUG
#include <linux/serialP.h>
#include <linux/serial_reg.h>
#include <asm/serial.h>
struct serial_state rs_table[RS_TABLE_SIZE] = {
	SERIAL_PORT_DFNS	/* Defined in <asm/serial.h> */
};

static void
ocotea_progress(char *s, unsigned short hex)
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

#if 0
static void __init
ocotea_map_io(void)
{
	io_block_mapping(0xe0000000, 0x0000000140000000,
			 0x00001000, _PAGE_IO);
}
#endif

void __init platform_init(unsigned long r3, unsigned long r4,
		unsigned long r5, unsigned long r6, unsigned long r7)
{
	parse_bootinfo((struct bi_record *) (r3 + KERNELBASE));

	ppc_md.setup_arch = ocotea_setup_arch;
	ppc_md.show_cpuinfo = ocotea_show_cpuinfo;
	ppc_md.init_IRQ = ocotea_init_irq;
	ppc_md.get_irq = NULL;		/* Set in ppc4xx_pic_init() */

	ppc_md.find_end_of_memory = ocotea_find_end_of_memory;

	ppc_md.restart = ocotea_restart;
	ppc_md.power_off = ocotea_power_off;
	ppc_md.halt = ocotea_halt;

	ppc_md.calibrate_decr = ocotea_calibrate_decr;
	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;

	ppc_md.nvram_read_val = todc_direct_read_val;
	ppc_md.nvram_write_val = todc_direct_write_val;

#ifdef CONFIG_SERIAL_TEXT_DEBUG
	ppc_md.progress = ocotea_progress;
#endif /* CONFIG_SERIAL_TEXT_DEBUG */
#ifdef CONFIG_KGDB
	ppc_md.early_serial_map = ocotea_early_serial_map;
#endif
}
