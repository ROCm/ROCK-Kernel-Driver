/*
 * linux/arch/arm/mach-sa1100/pangolin.c
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>

#include <asm/hardware.h>
#include <asm/setup.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>

#include "generic.h"

static struct map_desc pangolin_io_desc[] __initdata = {
 /* virtual     physical    length      type */
  { 0xf2800000, 0x4b800000, 0x00800000, MT_DEVICE } /* MQ200 */
};

static void __init pangolin_map_io(void)
{
	sa1100_map_io();
	iotable_init(pangolin_io_desc, ARRAY_SIZE(pangolin_io_desc));

	sa1100_register_uart(0, 1);
	sa1100_register_uart(1, 3);
	Ser1SDCR0 |= SDCR0_UART;

	/* set some GPDR bits while it's safe */
	GPDR |= GPIO_PCMCIA_RESET;
#ifndef CONFIG_SA1100_PANGOLIN_PCMCIA_IDE
	GPDR |= GPIO_PCMCIA_BUS_ON;
#endif
}

MACHINE_START(PANGOLIN, "Dialogue-Pangolin")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	MAPIO(pangolin_map_io)
	INITIRQ(sa1100_init_irq)
MACHINE_END
