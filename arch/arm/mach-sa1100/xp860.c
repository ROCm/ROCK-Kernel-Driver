/*
 * linux/arch/arm/mach-sa1100/xp860.c
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/tty.h>
#include <linux/ioport.h>

#include <asm/hardware.h>
#include <asm/setup.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>
#include <asm/hardware/sa1111.h>

#include "generic.h"


static void xp860_power_off(void)
{
	local_irq_disable();
	GPDR |= GPIO_GPIO20;
	GPSR = GPIO_GPIO20;
	mdelay(1000);
	GPCR = GPIO_GPIO20;
	while(1);
}

static struct resource sa1111_resources[] = {
	[0] = {
		.start		= 0x40000000,
		.end		= 0x40001fff,
		.flags		= IORESOURCE_MEM,
	},
};

static u64 sa1111_dmamask = 0xffffffffUL;

static struct platform_device sa1111_device = {
	.name		= "sa1111",
	.id		= 0,
	.dev		= {
		.dma_mask = &sa1111_dmamask,
	},
	.num_resources	= ARRAY_SIZE(sa1111_resources),
	.resource	= sa1111_resources,
};

static struct platform_device *devices[] __initdata = {
	&sa1111_device,
};

/*
 * Note: I replaced the sa1111_init() without the full SA1111 initialisation
 * because this machine doesn't appear to use the DMA features.  If this is
 * wrong, please look at neponset.c to fix it properly.
 */
static int __init xp860_init(void)
{
	pm_power_off = xp860_power_off;

	return platform_add_devices(devices, ARRAY_SIZE(devices));
}

arch_initcall(xp860_init);

static struct map_desc xp860_io_desc[] __initdata = {
 /* virtual     physical    length      type */
  { 0xf0000000, 0x10000000, 0x00100000, MT_DEVICE }, /* SCSI */
  { 0xf1000000, 0x18000000, 0x00100000, MT_DEVICE }, /* LAN */
  { 0xf4000000, 0x40000000, 0x00800000, MT_DEVICE }  /* SA-1111 */
};

static void __init xp860_map_io(void)
{
	sa1100_map_io();
	iotable_init(xp860_io_desc, ARRAY_SIZE(xp860_io_desc));

	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);
}

MACHINE_START(XP860, "XP860")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	MAPIO(xp860_map_io)
	INITIRQ(sa1100_init_irq)
MACHINE_END
