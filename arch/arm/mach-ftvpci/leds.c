/*
 *  linux/arch/arm/kernel/leds-ftvpci.c
 *
 *  Copyright (C) 1999 FutureTV Labs Ltd
 */

#include <linux/module.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/leds.h>
#include <asm/system.h>
#include <asm/io.h>

static void ftvpci_leds_event(led_event_t ledevt)
{
	static int led_state = 0;

	switch(ledevt) {
	case led_timer:
		led_state ^= 1;
		raw_writeb(0x1a | led_state, INTCONT_BASE);
		break;

	default:
		break;
	}
}

static int __init ftvpci_leds_init(void)
{
	leds_event = ftvpci_leds_event;
	return 0;
}

arch_initcall(ftvpci_leds_init);
