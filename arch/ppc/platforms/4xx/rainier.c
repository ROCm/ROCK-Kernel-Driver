/*
 *
 *
 *    Copyright 2000-2002 MontaVista Software Inc.
 *      Author: MontaVista Software, Inc.
 *      	akuster@mvista.com <source@mvista.com>
 *
 *    Module name: rainier.c
 *
 *    Description:
 *      Architecture- / platform-specific boot-time initialization code for
 *      IBM PowerPC 4xx based boards. Adapted from original
 *      code from walnut.c
 *
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/threads.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/blk.h>
#include <linux/pci.h>
#include <linux/rtc.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serial_core.h>

#include <asm/system.h>
#include <asm/pci-bridge.h>
#include <asm/processor.h>
#include <asm/machdep.h>
#include <asm/page.h>
#include <asm/time.h>
#include <asm/io.h>
#include <platforms/ibm_ocp.h>
#include <asm/todc.h>

#undef DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

void *rainier_rtc_base;
unsigned int rainier_io_page;

void
*locate_rainier_io(void)
{
	unsigned int temp;

	temp = in_le32((void*)PPC405_PCI_CONFIG_ADDR) & PCI_CONFIG_ADDR_MASK;
	out_le32((void*)PPC405_PCI_CONFIG_ADDR,
			temp | PCI_CONFIG_CYCLE_ENABLE |PCI_BASE_ADDRESS_2);
	temp = in_le32((void*)PPC405_PCI_CONFIG_DATA);

	if (temp == (PCI_BASE_ADDRESS_MEM_CARD2 | PCI_BASE_ADDRESS_MEM_PREFETCH))
		return PPC_405RAINIER2_IO_PAGE;
	else
		return PPC_405RAINIER1_IO_PAGE;
}

int __init
ppc405_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	    /*
	     *      PCI IDSEL/INTPIN->INTLINE
	     *      A       B       C       D
	     */
	{
		{28, 28, 28, 28},	/* IDSEL 1 - PCI slot 1 */
		{29, 29, 29, 29},	/* IDSEL 2 - PCI slot 2 */
		{30, 30, 30, 30},	/* IDSEL 3 - PCI slot 3 */
		{31, 31, 31, 31},	/* IDSEL 4 - PCI slot 4 */
	};

	const long min_idsel = 1, max_idsel = 4, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
};

void __init
raininer_setup_arch(void)
{
	struct uart_port port;

	ppc4xx_setup_arch();

	port.membase = (void*)PPC405_UART0_IO_BASE;
	port.membase += rainier_io_page;
	port.irq = PPC405_UART0_INT;
	port.uartclk = BASE_BAUD * 16;
	port.iotype = SERIAL_IO_MEM;
	port.flags = STD_COM_FLAGS;
	port.line = 0;

        early_serial_setup(&port);

	/* RTC step for the rainier */
	rainier_rtc_base = (void *) WALNUT_RTC_VADDR;
	TODC_INIT(TODC_TYPE_DS1743, rainier_rtc_base, rainier_rtc_base,
		  rainier_rtc_base, 8);
}

void __init
bios_fixup(struct pci_controller *hose, struct pcil0_regs *pcip)
{
	/*
	 * Expected PCI mapping:
	 *
	 *  PLB addr             PCI memory addr
	 *  ---------------------       ---------------------
	 *  0000'0000 - 7fff'ffff <---  0000'0000 - 7fff'ffff
	 *  8000'0000 - Bfff'ffff --->  8000'0000 - Bfff'ffff
	 *
	 *  PLB addr             PCI io addr
	 *  ---------------------       ---------------------
	 *  e800'0000 - e800'ffff --->  0000'0000 - 0001'0000
	 *
	 * The following code is simplified by assuming that the bootrom
	 * has been well behaved in following this mapping.
	 */

#ifdef DEBUG
	int i;

	printk("ioremap PCLIO_BASE = 0x%x\n", pcip);
	printk("PCI bridge regs before fixup \n");
	for (i = 0; i <= 3; i++) {
		printk(" pmm%dma\t0x%x\n", i, in_le32(&(pcip->pmm[i].ma)));
		printk(" pmm%dma\t0x%x\n", i, in_le32(&(pcip->pmm[i].la)));
		printk(" pmm%dma\t0x%x\n", i, in_le32(&(pcip->pmm[i].pcila)));
		printk(" pmm%dma\t0x%x\n", i, in_le32(&(pcip->pmm[i].pciha)));
	}
	printk(" ptm1ms\t0x%x\n", in_le32(&(pcip->ptm1ms)));
	printk(" ptm1la\t0x%x\n", in_le32(&(pcip->ptm1la)));
	printk(" ptm2ms\t0x%x\n", in_le32(&(pcip->ptm2ms)));
	printk(" ptm2la\t0x%x\n", in_le32(&(pcip->ptm2la)));

#endif

	/* Disable region first */
	out_le32((void *) &(pcip->pmm[0].ma), 0x00000000);
	/* PLB starting addr, PCI: 0x80000000 */
	out_le32((void *) &(pcip->pmm[0].la), 0x80000000);
	/* PCI start addr, 0x80000000 */
	out_le32((void *) &(pcip->pmm[0].pcila), PPC405_PCI_MEM_BASE);
	/* 512MB range of PLB to PCI */
	out_le32((void *) &(pcip->pmm[0].pciha), 0x00000000);
	/* Enable no pre-fetch, enable region */
	out_le32((void *) &(pcip->pmm[0].ma), ((0xffffffff -
						(PPC405_PCI_UPPER_MEM -
						 PPC405_PCI_MEM_BASE)) | 0x01));

	/*region one used bu rainier*/
	out_le32((void *) &(pcip->pmm[1].ma), 0x00000000);
	out_le32((void *) &(pcip->pmm[1].la), 0x80000000);
	out_le32((void *) &(pcip->pmm[1].pcila), 0x80000000);
	out_le32((void *) &(pcip->pmm[1].pciha), 0x00000000);
	out_le32((void *) &(pcip->pmm[1].ma), 0xFFFF8001);
	out_le32((void *) &(pcip->ptm1ms), 0x00000000);

	/* Disable region two */
	out_le32((void *) &(pcip->pmm[2].ma), 0x00000000);
	out_le32((void *) &(pcip->pmm[2].la), 0x00000000);
	out_le32((void *) &(pcip->pmm[2].pcila), 0x00000000);
	out_le32((void *) &(pcip->pmm[2].pciha), 0x00000000);
	out_le32((void *) &(pcip->pmm[2].ma), 0x00000000);
	out_le32((void *) &(pcip->ptm2ms), 0x00000000);

	/* end work arround */

#ifdef DEBUG
	printk("PCI bridge regs after fixup \n");
	for (i = 0; i <= 3; i++) {
		printk(" pmm%dma\t0x%x\n", i, in_le32(&(pcip->pmm[i].ma)));
		printk(" pmm%dma\t0x%x\n", i, in_le32(&(pcip->pmm[i].la)));
		printk(" pmm%dma\t0x%x\n", i, in_le32(&(pcip->pmm[i].pcila)));
		printk(" pmm%dma\t0x%x\n", i, in_le32(&(pcip->pmm[i].pciha)));
	}
	printk(" ptm1ms\t0x%x\n", in_le32(&(pcip->ptm1ms)));
	printk(" ptm1la\t0x%x\n", in_le32(&(pcip->ptm1la)));
	printk(" ptm2ms\t0x%x\n", in_le32(&(pcip->ptm2ms)));
	printk(" ptm2la\t0x%x\n", in_le32(&(pcip->ptm2la)));

#endif
}

void __init
rainier_map_io(void)
{
	ppc4xx_map_io();

	io_block_mapping(RAINIER_IO_PAGE_INTERPOSER_PADDR,
			 RAINIER_IO_PAGE_INTERPOSER_VADDR,PAGE_SIZE , _PAGE_IO);

	io_block_mapping(RAINIER_IO_PAGE_PCI_PADDR,
			 RAINIER_IO_PAGE_PCI_VADDR,PAGE_SIZE , _PAGE_IO);
	
	io_block_mapping(RAINIER_RTC_VADDR,
			 RAINIER_RTC_PADDR, RAINIER_RTC_SIZE, _PAGE_IO);

	rainier_io_page = locate_rainier_io();
       
	io_block_mapping(rainier_io_page ,
			 rainier_io_page , PAGE_SIZE, _PAGE_IO);

}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	ppc4xx_init(r3, r4, r5, r6, r7);

	ppc_md.setup_arch = rainier_setup_arch;
	ppc_md.setup_io_mappings = rainier_map_io;

	ppc_md.time_init	 	= m48t3x_time_init;
	ppc_md.set_rtc_time	 	= m48t3x_set_rtc_time;
	ppc_md.get_rtc_time	 	= m48t3x_get_rtc_time;
	ppc_md.time_init = todc_time_init;
	ppc_md.nvram_read_val = todc_direct_read_val;
	ppc_md.nvram_write_val = todc_direct_write_val;
}
