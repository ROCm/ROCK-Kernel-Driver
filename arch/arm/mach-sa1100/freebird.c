/*
 * linux/arch/arm/mach-sa1100/freebird.c
 */

#include <linux/config.h>
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


unsigned long BCR_value = BCR_DB1110;
EXPORT_SYMBOL(BCR_value);

static void freebird_backlight_power(int on)
{
#error FIXME
	if (on) {
		BCR_set(BCR_FREEBIRD_LCD_PWR | BCR_FREEBIRD_LCD_DISP);
		/* Turn on backlight, Chester */
		BCR_set(BCR_FREEBIRD_LCD_BACKLIGHT);
	} else {
		BCR_clear(BCR_FREEBIRD_LCD_PWR | BCR_FREEBIRD_LCD_DISP
			  /* | BCR_FREEBIRD_LCD_BACKLIGHT */);
	}
}

static void freebird_lcd_power(int on)
{
}

static int __init freebird_init(void)
{
	if (machine_is_freebird()) {
		sa1100fb_backlight_power = freebird_backlight_power;
		sa1100fb_lcd_power = freebird_lcd_power;
	}
	return 0;
}

arch_initcall(freebird_init);

static struct map_desc freebird_io_desc[] __initdata = {
 /* virtual     physical    length      type */
  { 0xf0000000, 0x12000000, 0x00100000, MT_DEVICE }, /* Board Control Register */
  { 0xf2000000, 0x19000000, 0x00100000, MT_DEVICE }
};

static void __init freebird_map_io(void)
{
	sa1100_map_io();
	iotable_init(freebird_io_desc, ARRAY_SIZE(freebird_io_desc));

	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);

	/* Set up sleep mode registers */
	PWER = 0x1;
	PGSR = 0x0;
	PCFR = PCFR_OPDE | PCFR_FP | PCFR_FS;
}

MACHINE_START(FREEBIRD, "Freebird-HPC-1.1")
	BOOT_MEM(0xc0000000,0x80000000, 0xf8000000)
#ifdef CONFIG_SA1100_FREEBIRD_NEW
	BOOT_PARAMS(0xc0000100)
#endif
	MAPIO(freebird_map_io)
	INITIRQ(sa1100_init_irq)
	INITTIME(sa1100_init_time)
MACHINE_END
