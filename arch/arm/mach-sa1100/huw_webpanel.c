/*
 * linux/arch/arm/mach-sa1100/huw_webpanel.c
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>

#include <asm/hardware.h>
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

__initcall(init_huw_cs3);


static void __init
fixup_huw_webpanel(struct machine_desc *desc, struct tag *tags,
		   char **cmdline, struct meminfo *mi)
{
	/**
	  memory information (JOR):
	  32 MByte - 256KByte bootloader (init at boot time) - 32 kByte save area
	 **/
	SET_BANK( 0, 0xc0000000, ((32*1024 - (256 + 32)) * 1024));
	mi->nr_banks = 1;
	ROOT_DEV = mk_kdev(RAMDISK_MAJOR,0);
	setup_ramdisk( 1, 0, 0, 8192 );
	setup_initrd( __phys_to_virt(0xc0800000), 8*1024*1024 );
}


/**
   memory information (JOR):
   32 MByte - 256KByte bootloader (init at boot time) - 32 kByte save area
   area size = 288 kByte (0x48000 Bytes)
**/
static struct map_desc huw_webpanel_io_desc[] __initdata = {
 /* virtual     physical    length      domain     r  w  c  b */
  { 0xf0000000, 0xc1fb8000, 0x00048000, DOMAIN_IO, 0, 1, 0, 0 }, /* Parameter */
  { 0xf1000000, 0x18000000, 0x00100000, DOMAIN_IO, 0, 1, 0, 0 }, /* Paules CS3, write only */
  LAST_DESC
};

static void __init huw_webpanel_map_io(void)
{
	sa1100_map_io();
	iotable_init(huw_webpanel_io_desc);

	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);
}


MACHINE_START(HUW_WEBPANEL, "HuW-Webpanel")
	MAINTAINER("Roman Jordan")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_huw_webpanel)
	MAPIO(huw_webpanel_map_io)
	INITIRQ(sa1100_init_irq)
MACHINE_END
