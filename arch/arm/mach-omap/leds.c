/*
 * linux/arch/arm/mach-omap/leds.c
 *
 * OMAP LEDs dispatcher
 */
#include <linux/init.h>

#include <asm/leds.h>
#include <asm/mach-types.h>

#include "leds.h"

static int __init
omap1510_leds_init(void)
{
	if (machine_is_omap_innovator())
		leds_event = innovator_leds_event;

	else if (machine_is_omap_perseus2()) {
		leds_event = perseus2_leds_event;
	}

	leds_event(led_start);
	return 0;
}

__initcall(omap1510_leds_init);
