/*
 * linux/arch/arm/mach-sa1100/cerf.c
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


static void __init cerf_init_irq (void)
{
  sa1100_init_irq();

  /* Need to register these as rising edge interrupts
   * For standard 16550 serial driver support
   * Basically - I copied it from pfs168.c :)
   */
#ifdef CONFIG_SA1100_CERF_CPLD
  set_GPIO_IRQ_edge(GPIO_GPIO(3), GPIO_RISING_EDGE); /* PDA Full serial port */
  set_GPIO_IRQ_edge(GPIO_GPIO(2), GPIO_RISING_EDGE); /* PDA Bluetooth */
  GPDR &= ~(GPIO_GPIO(3)); /* Set the direction of serial port GPIO pin to in */
  GPDR &= ~(GPIO_GPIO(2)); /* Set the direction of bluetooth GPIO pin to in */
#endif /* CONFIG_SA1100_CERF_CPLD */
}

static void __init
fixup_cerf(struct machine_desc *desc, struct param_struct *params,
	   char **cmdline, struct meminfo *mi)
{
#if defined(CONFIG_SA1100_CERF_64MB)
        // 64MB RAM
        SET_BANK( 0, 0xc0000000, 64*1024*1024 );
        mi->nr_banks = 1;
#elif defined(CONFIG_SA1100_CERF_32MB)	// 32MB RAM
	SET_BANK( 0, 0xc0000000, 32*1024*1024 );
	mi->nr_banks = 1;
#elif defined(CONFIG_SA1100_CERF_16MB)	// 16Meg Ram.
	SET_BANK( 0, 0xc0000000, 8*1024*1024 );
	SET_BANK( 1, 0xc8000000, 8*1024*1024 );
	mi->nr_banks = 2;
#elif defined(CONFIG_SA1100_CERF_8MB)
        // 8Meg Ram.
        SET_BANK( 0, 0xc0000000, 8*1024*1024 );
        mi->nr_banks = 1;
#else
       #error "Undefined memory size for Cerfboard."
#endif

//	ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
//	setup_ramdisk(1,  0, 0, 8192);
//	// Save 2Meg for RAMDisk
//	setup_initrd(0xc0500000, 3*1024*1024);
}

static struct map_desc cerf_io_desc[] __initdata = {
  /* virtual     physical    length      domain     r  w  c  b */
  { 0xe8000000, 0x00000000, 0x02000000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 0 */
  { 0xf0000000, 0x08000000, 0x00100000, DOMAIN_IO, 1, 1, 0, 0 }, /* Crystal Ethernet Chip */
#ifdef CONFIG_SA1100_CERF_CPLD
  { 0xf1000000, 0x40000000, 0x00100000, DOMAIN_IO, 1, 1, 0, 0 }, /* CPLD Chip */
  { 0xf2000000, 0x10000000, 0x00100000, DOMAIN_IO, 1, 1, 0, 0 }, /* CerfPDA Bluetooth */
  { 0xf3000000, 0x18000000, 0x00100000, DOMAIN_IO, 1, 1, 0, 0 }, /* CerfPDA Serial */
#endif
  LAST_DESC
};

static void __init cerf_map_io(void)
{
	sa1100_map_io();
	iotable_init(cerf_io_desc);

        sa1100_register_uart(0, 3);
#ifdef CONFIG_SA1100_CERF_IRDA_ENABLED
	sa1100_register_uart(1, 1);
#else
	sa1100_register_uart(1, 2);
	sa1100_register_uart(2, 1);
#endif
}

MACHINE_START(CERF, "Intrinsyc's Cerf Family of Products")
	MAINTAINER("support@intrinsyc.com")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_cerf)
	MAPIO(cerf_map_io)
	INITIRQ(cerf_init_irq)
MACHINE_END
