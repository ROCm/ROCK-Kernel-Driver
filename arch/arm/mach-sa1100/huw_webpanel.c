/*
 * linux/arch/arm/mach-sa1100/huw_webpanel.c
 *
 */
#include <linux/module.h>
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


unsigned long BCR_value;
EXPORT_SYMBOL(BCR_value);

static void huw_lcd_power(int on)
{
	if (on)
		BCR_clear(BCR_TFT_NPWR);
	else
		BCR_set(BCR_TFT_NPWR);
}

static void huw_backlight_power(int on)
{
#error FIXME
	if (on) {
		BCR_set(BCR_CCFL_POW | BCR_PWM_BACKLIGHT);
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_task(200 * HZ / 1000);
		BCR_set(BCR_TFT_ENA);
	}
}

static int __init init_huw_cs3(void)
{
	// here we can place some initcode
	// BCR_value = 0x1045bf70; //*((volatile unsigned long*)0xf1fffff0);
	if (machine_is_huw_webpanel()) {
		sa1100fb_lcd_power = huw_lcd_power;
		sa1100fb_backlight_power = huw_backlight_power;
	}

	return 0;
}

arch_initcall(init_huw_cs3);


/**
   memory information (JOR):
   32 MByte - 256KByte bootloader (init at boot time) - 32 kByte save area
   area size = 288 kByte (0x48000 Bytes)
**/
static struct map_desc huw_webpanel_io_desc[] __initdata = {
 /* virtual     physical    length      type */
  { 0xf0000000, 0xc1fb8000, 0x00048000, MT_DEVICE }, /* Parameter */
  { 0xf1000000, 0x18000000, 0x00100000, MT_DEVICE }  /* Paules CS3, write only */
};

static void __init huw_webpanel_map_io(void)
{
	sa1100_map_io();
	iotable_init(huw_webpanel_io_desc, ARRAY_SIZE(huw_webpanel_io_desc));

	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);
}


MACHINE_START(HUW_WEBPANEL, "HuW-Webpanel")
	MAINTAINER("Roman Jordan")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	MAPIO(huw_webpanel_map_io)
	INITIRQ(sa1100_init_irq)
	INITTIME(sa1100_init_time)
MACHINE_END
