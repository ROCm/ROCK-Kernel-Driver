/*
 * linux/arch/arm/mach-sa1100/lart.c
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>

#include <asm/hardware.h>
#include <asm/setup.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>

#include "generic.h"


static void __init
fixup_lart(struct machine_desc *desc, struct param_struct *params,
	   char **cmdline, struct meminfo *mi)
{
	/*
	 * Note that LART is a special case - it doesn't use physical
	 * address line A23 on the DRAM, so we effectively have 4 * 8MB
	 * in two SA1100 banks.
	 */
	SET_BANK( 0, 0xc0000000, 8*1024*1024 );
	SET_BANK( 1, 0xc1000000, 8*1024*1024 );
	SET_BANK( 2, 0xc8000000, 8*1024*1024 );
	SET_BANK( 3, 0xc9000000, 8*1024*1024 );
	SET_BANK( 4, 0xd0000000, 64*1024*1024 );
	SET_BANK( 5, 0xd8000000, 64*1024*1024 );

	/* make this 5 if you have the 64MB expansion card, or
	 * 6 if you have two 64MB expansion cards */
	mi->nr_banks = 4;

	ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
	setup_ramdisk(1, 0, 0, 8192);
	setup_initrd(0xc0400000, 4*1024*1024);
}

static struct map_desc lart_io_desc[] __initdata = {
 /* virtual     physical    length      domain     r  w  c  b */
  { 0xe8000000, 0x00000000, 0x00400000, DOMAIN_IO, 1, 1, 0, 0 }, /* main flash memory */
  { 0xec000000, 0x08000000, 0x00400000, DOMAIN_IO, 1, 1, 0, 0 }, /* main flash, alternative location */
  LAST_DESC
};

static void __init lart_map_io(void)
{
	sa1100_map_io();
	iotable_init(lart_io_desc);

	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);
	GAFR |= (GPIO_UART_TXD | GPIO_UART_RXD);
	GPDR |= GPIO_UART_TXD;
	GPDR &= ~GPIO_UART_RXD;
	PPAR |= PPAR_UPR;
}

MACHINE_START(LART, "LART")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_lart)
	MAPIO(lart_map_io)
	INITIRQ(sa1100_init_irq)
MACHINE_END
