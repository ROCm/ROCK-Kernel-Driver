/*
 * linux/arch/arm/mach-sa1100/pfs168.c
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/device.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/setup.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>

#include "generic.h"

static struct resource sa1111_resources[] = {
	[0] = {
		.start		= 0x40000000,
		.end		= 0x40001fff,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= IRQ_GPIO25,
		.end		= IRQ_GPIO25,
		.flags		= IORESOURCE_IRQ,
	},
};

static u64 sa1111_dmamask = 0xffffffffUL;

static struct platform_device sa1111_device = {
	.name		= "sa1111",
	.id		= 0,
	.dev		= {
		.dma_mask = &sa1111_dmamask,
		.coherent_dma_mask = 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(sa1111_resources),
	.resource	= sa1111_resources,
};

static struct platform_device *devices[] __initdata = {
	&sa1111_device,
};

static int __init pfs168_init(void)
{
	int ret;

	if (!machine_is_pfs168())
		return -ENODEV;

	/*
	 * Ensure that the memory bus request/grant signals are setup,
	 * and the grant is held in its inactive state
	 */
	sa1110_mb_disable();

	return platform_add_devices(devices, ARRAY_SIZE(devices));
}

arch_initcall(pfs168_init);


static void __init pfs168_init_irq(void)
{
	sa1100_init_irq();

	/*
	 * Need to register these as rising edge interrupts
	 * for standard 16550 serial driver support.
	 */
	set_GPIO_IRQ_edge(GPIO_GPIO(19), GPIO_RISING_EDGE);
	set_GPIO_IRQ_edge(GPIO_GPIO(20), GPIO_RISING_EDGE);
	set_GPIO_IRQ_edge(GPIO_GPIO(25), GPIO_RISING_EDGE);
	set_GPIO_IRQ_edge(GPIO_UCB1300_IRQ, GPIO_RISING_EDGE);
}

static struct map_desc pfs168_io_desc[] __initdata = {
 /* virtual     physical    length      type */
  { 0xf0000000, 0x10000000, 0x00001000, MT_DEVICE }, /* 16C752 DUART port A (COM5) */
  { 0xf0001000, 0x10800000, 0x00001000, MT_DEVICE }, /* 16C752 DUART port B (COM6) */
  { 0xf0002000, 0x11000000, 0x00001000, MT_DEVICE }, /* COM1 RTS control (SYSC1RTS) */
  { 0xf0003000, 0x11400000, 0x00001000, MT_DEVICE }, /* Status LED control (SYSLED) */
  { 0xf0004000, 0x11800000, 0x00001000, MT_DEVICE }, /* DTMF code read (SYSDTMF) */
  { 0xf0005000, 0x11c00000, 0x00001000, MT_DEVICE }, /* LCD configure, enable (SYSLCDDE) */
  { 0xf0006000, 0x12000000, 0x00001000, MT_DEVICE }, /* COM1 DSR and motion sense (SYSC1DSR) */
  { 0xf0007000, 0x12800000, 0x00001000, MT_DEVICE }, /* COM3 xmit enable (SYSC3TEN) */
  { 0xf0008000, 0x13000000, 0x00001000, MT_DEVICE }, /* Control register A (SYSCTLA) */
  { 0xf0009000, 0x13800000, 0x00001000, MT_DEVICE }, /* Control register B (SYSCTLB) */
  { 0xf000a000, 0x18000000, 0x00001000, MT_DEVICE }, /* SMC91C96 */
  { 0xf2800000, 0x4b800000, 0x00800000, MT_DEVICE }, /* MQ200 */
  { 0xf4000000, 0x40000000, 0x00100000, MT_DEVICE }  /* SA-1111 */
};

static void __init pfs168_map_io(void)
{
	sa1100_map_io();
	iotable_init(pfs168_io_desc, ARRAY_SIZE(pfs168_io_desc));

	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);
}

MACHINE_START(PFS168, "Tulsa")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	BOOT_PARAMS(0xc0000100)
	MAPIO(pfs168_map_io)
	INITIRQ(pfs168_init_irq)
	INITTIME(sa1100_init_time)
MACHINE_END
