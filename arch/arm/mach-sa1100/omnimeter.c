/*
 * linux/arch/arm/mach-sa1100/omnimeter.c
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/setup.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>

#include "generic.h"

static void omnimeter_backlight_power(int on)
{
	if (on)
		LEDBacklightOn();
	else
		LEDBacklightOff();
}

static void omnimeter_lcd_power(int on)
{
	if (on)
		LCDPowerOn();
}

static int __init omnimeter_init(void)
{
	if (machine_is_omnimeter()) {
		sa1100fb_backlight_power = omnimeter_backlight_power;
		sa1100fb_lcd_power = omnimeter_lcd_power;
	}
	return 0;
}

arch_initcall(omnimeter_init);

static struct map_desc omnimeter_io_desc[] __initdata = {
 /* virtual     physical    length      type */
  { 0xd2000000, 0x10000000, 0x02000000, MT_DEVICE } /* TS */
};

static void __init omnimeter_map_io(void)
{
	sa1100_map_io();
	iotable_init(omnimeter_io_desc, ARRAY_SIZE(omnimeter_io_desc));

	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);
}

MACHINE_START(OMNIMETER, "OmniMeter")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	MAPIO(omnimeter_map_io)
	INITIRQ(sa1100_init_irq)
	INITTIME(sa1100_init_time)
MACHINE_END
