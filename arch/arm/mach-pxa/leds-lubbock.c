/*
 * linux/arch/arm/mach-pxa/leds-lubbock.c
 *
 * Copyright (C) 2000 John Dorsey <john+@cs.cmu.edu>
 *
 * Copyright (c) 2001 Jeff Sutherland <jeffs@accelent.com>
 *
 * Original (leds-footbridge.c) by Russell King
 *
 * See leds.h for bit definitions.  The first version defines D28 on the
 * Lubbock dev board as the heartbeat, and D27 as the Sys_busy led.
 * There's plenty more if you're interested in adding them :)
 */


#include <linux/config.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/leds.h>
#include <asm/system.h>

#include "leds.h"


#define LED_STATE_ENABLED	1
#define LED_STATE_CLAIMED	2

static unsigned int led_state;
static unsigned int hw_led_state;

void lubbock_leds_event(led_event_t evt)
{
	unsigned long flags;

	local_irq_save(flags);

	switch (evt) {
	case led_start:
		hw_led_state = HEARTBEAT_LED | SYS_BUSY_LED;
		led_state = LED_STATE_ENABLED;
		break;

	case led_stop:
		led_state &= ~LED_STATE_ENABLED;
		break;

	case led_claim:
		led_state |= LED_STATE_CLAIMED;
		hw_led_state = HEARTBEAT_LED | SYS_BUSY_LED;
		break;

	case led_release:
		led_state &= ~LED_STATE_CLAIMED;
		hw_led_state = HEARTBEAT_LED | SYS_BUSY_LED;
		break;

#ifdef CONFIG_LEDS_TIMER
	case led_timer:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state ^= HEARTBEAT_LED;
		break;
#endif

#ifdef CONFIG_LEDS_CPU
	case led_idle_start:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state |= SYS_BUSY_LED;
		break;

	case led_idle_end:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state &= ~SYS_BUSY_LED;
		break;
#endif

	case led_halted:
		break;

	case led_green_on:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state &= ~HEARTBEAT_LED;
		break;

	case led_green_off:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state |= HEARTBEAT_LED;
		break;

	case led_amber_on:
		break;

	case led_amber_off:
		break;

	case led_red_on:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state &= ~SYS_BUSY_LED;
		break;

	case led_red_off:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state |= SYS_BUSY_LED;
		break;

	default:
		break;
	}

	if  (led_state & LED_STATE_ENABLED)
	{
		switch (hw_led_state) {
		case 0: // all on
			HEARTBEAT_LED_ON;
			SYS_BUSY_LED_ON;
			break;
		case 1: // turn off heartbeat, status on:
			HEARTBEAT_LED_OFF;
			SYS_BUSY_LED_ON;
			break;
		case 2: // status off, heartbeat on:
			HEARTBEAT_LED_ON;
			SYS_BUSY_LED_OFF;
			break;
		case 3: // turn them both off...
			HEARTBEAT_LED_OFF;
			SYS_BUSY_LED_OFF;
			break;
		default:
			break;
		}
	}
	local_irq_restore(flags);
}
