/*
 * arch/ppc/platforms/4xx/cedar.c
 *
 * Support for the IBM NP405L ceder eval board
 *
 * Author: Armin Kuster <akuster@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <asm/machdep.h>

#include <asm/io.h>
#include <asm/todc.h>

void *cedar_rtc_base;

void __init
cedar_setup_arch(void)
{
	bd_t *bip = &__res;

	/* RTC step for the walnut */
	cedar_rtc_base = (void *) CEDAR_RTC_VADDR;
	TODC_INIT(TODC_TYPE_DS1743, cedar_rtc_base, cedar_rtc_base,
		  cedar_rtc_base, 8);

#ifdef CONFIG_DEBUG_BRINGUP
	printk("\n");
	printk("machine\t: %s\n", PPC4xx_MACHINE_NAME);
	printk("\n");
	printk("bi_s_version\t %s\n", bip->bi_s_version);
	printk("bi_r_version\t %s\n", bip->bi_r_version);
	printk("bi_memsize\t 0x%8.8x\t %dMBytes\n", bip->bi_memsize,
	       bip->bi_memsize / (1024 * 1000));
	printk("bi_enetaddr %d\t %2.2x%2.2x%2.2x-%2.2x%2.2x%2.2x\n", 0,
	       bip->bi_enetaddr[0][0], bip->bi_enetaddr[0][1],
	       bip->bi_enetaddr[0][2], bip->bi_enetaddr[0][3],
	       bip->bi_enetaddr[0][4], bip->bi_enetaddr[0][5]);

	printk("bi_enetaddr %d\t %2.2x%2.2x%2.2x-%2.2x%2.2x%2.2x\n", 1,
	       bip->bi_enetaddr[1][0], bip->bi_enetaddr[1][1],
	       bip->bi_enetaddr[1][2], bip->bi_enetaddr[1][3],
	       bip->bi_enetaddr[1][4], bip->bi_enetaddr[1][5]);

	printk("bi_intfreq\t 0x%8.8x\t clock:\t %dMhz\n",
	       bip->bi_intfreq, bip->bi_intfreq / 1000000);

	printk("bi_busfreq\t 0x%8.8x\t plb bus clock:\t %dMHz\n",
	       bip->bi_busfreq, bip->bi_busfreq / 1000000);
	printk("bi_pci_busfreq\t 0x%8.8x\t pci bus clock:\t %dMHz\n",
	       bip->bi_pci_busfreq, bip->bi_pci_busfreq / 1000000);

	printk("\n");
#endif

	/* Identify the system */
	printk
	    ("IBM Cedar port (C) 2002 MontaVista Software, Inc. (source@mvista.com)\n");

}

void __init
cedar_map_io(void)
{
	ppc4xx_map_io();
	io_block_mapping(CEDAR_RTC_VADDR,
			 CEDAR_RTC_PADDR, CEDAR_RTC_SIZE, _PAGE_IO);
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	ppc4xx_init(r3, r4, r5, r6, r7);

	ppc_md.setup_arch = cedar_setup_arch;
	ppc_md.setup_io_mappings = cedar_map_io;

	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;
	ppc_md.nvram_read_val = todc_direct_read_val;
	ppc_md.nvram_write_val = todc_direct_write_val;
}
