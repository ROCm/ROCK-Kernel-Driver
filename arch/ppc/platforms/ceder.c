/*
 *
 *    Copyright 2001 MontaVista Software Inc.
 *        <akuster@mvista.com>
 *	IBM NP405L ceder eval board
 *
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <asm/machdep.h>

#include <asm/io.h>

#ifdef CONFIG_PPC_RTC
#include <asm/todc.h>
#endif

void *ceder_rtc_base;

void __init
board_setup_arch(void)
{

	bd_t *bip = (bd_t *)__res;

#ifdef CONFIG_PPC_RTC
        /* RTC step for the walnut */
        ceder_rtc_base = (void *) CEDER_RTC_VADDR;
	TODC_INIT(TODC_TYPE_DS1743, ceder_rtc_base, ceder_rtc_base,ceder_rtc_base, 8);
#endif /* CONFIG_PPC_RTC */
#define CONFIG_DEBUG_BRINGUP
#ifdef CONFIG_DEBUG_BRINGUP
	printk("\n");
	printk("machine\t: %s\n", PPC4xx_MACHINE_NAME);
	printk("\n");
	printk("bi_s_version\t %s\n",      bip->bi_s_version);
	printk("bi_r_version\t %s\n",      bip->bi_r_version);
	printk("bi_memsize\t 0x%8.8x\t %dMBytes\n", bip->bi_memsize,bip->bi_memsize/(1024*1000));
	printk("bi_enetaddr %d\t %2.2x%2.2x%2.2x-%2.2x%2.2x%2.2x\n", 0,
	bip->bi_enetaddr[0][0], bip->bi_enetaddr[0][1],
	bip->bi_enetaddr[0][2], bip->bi_enetaddr[0][3],
	bip->bi_enetaddr[0][4], bip->bi_enetaddr[0][5]);

	printk("bi_enetaddr %d\t %2.2x%2.2x%2.2x-%2.2x%2.2x%2.2x\n", 1,
	bip->bi_enetaddr[1][0], bip->bi_enetaddr[1][1],
	bip->bi_enetaddr[1][2], bip->bi_enetaddr[1][3],
	bip->bi_enetaddr[1][4], bip->bi_enetaddr[1][5]);

	printk("bi_intfreq\t 0x%8.8x\t clock:\t %dMhz\n",
	       bip->bi_intfreq, bip->bi_intfreq/ 1000000);

	printk("bi_busfreq\t 0x%8.8x\t plb bus clock:\t %dMHz\n",
		bip->bi_busfreq, bip->bi_busfreq / 1000000 );
	printk("bi_pci_busfreq\t 0x%8.8x\t pci bus clock:\t %dMHz\n",
	       bip->bi_pci_busfreq, bip->bi_pci_busfreq/1000000);

	printk("\n");
#endif
}

void __init
board_io_mapping(void)
{
	io_block_mapping(CEDER_RTC_VADDR,
		CEDER_RTC_PADDR, CEDER_RTC_SIZE, _PAGE_IO);
}
void __init
board_setup_irq(void)
{
}

void __init
board_init(void)
{
#ifdef CONFIG_PPC_RTC
	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;
	ppc_md.nvram_read_val = todc_direct_read_val;
	ppc_md.nvram_write_val = todc_direct_write_val;
#endif
}
