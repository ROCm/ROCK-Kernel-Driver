/*
 * linux/arch/arm/mach-sa1100/cerf.c
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>

#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/setup.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>

#include "generic.h"


static void __init cerf_init_irq(void)
{
	sa1100_init_irq();

	/* Need to register these as rising edge interrupts
	 * For standard 16550 serial driver support
	 * Basically - I copied it from pfs168.c :)
	 */
#ifdef CONFIG_SA1100_CERF_CPLD
	/* PDA Full serial port */
	set_irq_type(IRQ_GPIO3, IRQT_RISING);
	/* PDA Bluetooth */
	set_irq_type(IRQ_GPIO2, IRQT_RISING);
#endif /* CONFIG_SA1100_CERF_CPLD */

	set_irq_type(IRQ_GPIO_UCB1200_IRQ, IRQT_RISING);
}

static struct map_desc cerf_io_desc[] __initdata = {
  /* virtual	 physical    length	 type */
  { 0xf0000000, 0x08000000, 0x00100000, MT_DEVICE }  /* Crystal Ethernet Chip */
#ifdef CONFIG_SA1100_CERF_CPLD
 ,{ 0xf1000000, 0x40000000, 0x00100000, MT_DEVICE }, /* CPLD Chip */
  { 0xf2000000, 0x10000000, 0x00100000, MT_DEVICE }, /* CerfPDA Bluetooth */
  { 0xf3000000, 0x18000000, 0x00100000, MT_DEVICE }  /* CerfPDA Serial */
#endif
};

static void __init cerf_map_io(void)
{
	sa1100_map_io();
	iotable_init(cerf_io_desc, ARRAY_SIZE(cerf_io_desc));

	sa1100_register_uart(0, 3);
#ifdef CONFIG_SA1100_CERF_IRDA_ENABLED
	sa1100_register_uart(1, 1);
#else
	sa1100_register_uart(1, 2);
	sa1100_register_uart(2, 1);
#endif

	/* set some GPDR bits here while it's safe */
	GPDR |= GPIO_CF_RESET;
#ifdef CONFIG_SA1100_CERF_CPLD
	GPDR |= GPIO_PWR_SHUTDOWN;
#endif
}

MACHINE_START(CERF, "Intrinsyc's Cerf Family of Products")
	MAINTAINER("support@intrinsyc.com")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	MAPIO(cerf_map_io)
	INITIRQ(cerf_init_irq)
MACHINE_END
