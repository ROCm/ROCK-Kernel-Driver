/*
 * linux/arch/arm/mach-sa1100/pfs168.c
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/ioport.h>

#include <asm/hardware.h>
#include <asm/setup.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>

#include "generic.h"
#include "sa1111.h"


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

	/*
	 * Probe for SA1111.
	 */
	return sa1111_init(NULL, 0x40000000, IRQ_GPIO25);
}

__initcall(pfs168_init);


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
 /* virtual     physical    length      domain     r  w  c  b */
  { 0xe8000000, 0x00000000, 0x02000000, DOMAIN_IO, 0, 1, 0, 0 }, /* Flash bank 0 */
  { 0xf0000000, 0x10000000, 0x00001000, DOMAIN_IO, 0, 1, 0, 0 }, /* 16C752 DUART port A (COM5) */
  { 0xf0001000, 0x10800000, 0x00001000, DOMAIN_IO, 0, 1, 0, 0 }, /* 16C752 DUART port B (COM6) */
  { 0xf0002000, 0x11000000, 0x00001000, DOMAIN_IO, 0, 1, 0, 0 }, /* COM1 RTS control (SYSC1RTS) */
  { 0xf0003000, 0x11400000, 0x00001000, DOMAIN_IO, 0, 1, 0, 0 }, /* Status LED control (SYSLED) */
  { 0xf0004000, 0x11800000, 0x00001000, DOMAIN_IO, 0, 1, 0, 0 }, /* DTMF code read (SYSDTMF) */
  { 0xf0005000, 0x11c00000, 0x00001000, DOMAIN_IO, 0, 1, 0, 0 }, /* LCD configure, enable (SYSLCDDE) */
  { 0xf0006000, 0x12000000, 0x00001000, DOMAIN_IO, 0, 1, 0, 0 }, /* COM1 DSR and motion sense (SYSC1DSR) */
  { 0xf0007000, 0x12800000, 0x00001000, DOMAIN_IO, 0, 1, 0, 0 }, /* COM3 xmit enable (SYSC3TEN) */
  { 0xf0008000, 0x13000000, 0x00001000, DOMAIN_IO, 0, 1, 0, 0 }, /* Control register A (SYSCTLA) */
  { 0xf0009000, 0x13800000, 0x00001000, DOMAIN_IO, 0, 1, 0, 0 }, /* Control register B (SYSCTLB) */
  { 0xf000a000, 0x18000000, 0x00001000, DOMAIN_IO, 0, 1, 0, 0 }, /* SMC91C96 */
  { 0xf2800000, 0x4b800000, 0x00800000, DOMAIN_IO, 0, 1, 0, 0 }, /* MQ200 */
  { 0xf4000000, 0x40000000, 0x00100000, DOMAIN_IO, 0, 1, 0, 0 }, /* SA-1111 */
  LAST_DESC
};

static void __init pfs168_map_io(void)
{
	sa1100_map_io();
	iotable_init(pfs168_io_desc);

	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);
}

MACHINE_START(PFS168, "Tulsa")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	BOOT_PARAMS(0xc0000100)
	MAPIO(pfs168_map_io)
	INITIRQ(pfs168_init_irq)
MACHINE_END
