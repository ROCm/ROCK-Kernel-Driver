/*
 *  linux/arch/arm/kernel/leds-sa1100.c
 *
 *  Copyright (C) 2000 John Dorsey <john+@cs.cmu.edu>
 *
 *  Original (leds-footbridge.c) by Russell King
 *
 *  Added Brutus LEDs support
 *	Nicolas Pitre, Mar 19, 2000
 *
 *  Added LART LED support
 *      Erik Mouw (J.A.K.Mouw@its.tudelft.nl), April 21, 2000
 *
 *
 *  Assabet uses the LEDs as follows:
 *   - Green - toggles state every 50 timer interrupts
 *   - Red   - on if system is not idle
 *
 *  Brutus uses the LEDs as follows:
 *   - D3 (Green, GPIO9) - toggles state every 50 timer interrupts
 *   - D17 (Red, GPIO20) - on if system is not idle
 *   - D4 (Green, GPIO8) - misc function
 *
 *  LART uses the LED as follows:
 *   - GPIO23 is the LED, on if system is not idle
 *  You can use both CONFIG_LEDS_CPU and CONFIG_LEDS_TIMER at the same
 *  time, but in that case the timer events will still dictate the
 *  pace of the LED.
 * 
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>

#include <asm/hardware.h>
#include <asm/leds.h>
#include <asm/mach-types.h>
#include <asm/system.h>


#define LED_STATE_ENABLED	1
#define LED_STATE_CLAIMED	2

static unsigned int led_state;
static unsigned int hw_led_state;


#ifdef CONFIG_SA1100_ASSABET

#define BCR_LED_MASK	(BCR_LED_GREEN | BCR_LED_RED)

static void assabet_leds_event(led_event_t evt)
{
	unsigned long flags;

	local_irq_save(flags);

	switch (evt) {
	case led_start:
		hw_led_state = BCR_LED_RED | BCR_LED_GREEN;
		led_state = LED_STATE_ENABLED;
		break;

	case led_stop:
		led_state &= ~LED_STATE_ENABLED;
		break;

	case led_claim:
		led_state |= LED_STATE_CLAIMED;
		hw_led_state = BCR_LED_RED | BCR_LED_GREEN;
		break;

	case led_release:
		led_state &= ~LED_STATE_CLAIMED;
		hw_led_state = BCR_LED_RED | BCR_LED_GREEN;
		break;

#ifdef CONFIG_LEDS_TIMER
	case led_timer:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state ^= BCR_LED_GREEN;
		break;
#endif

#ifdef CONFIG_LEDS_CPU
	case led_idle_start:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state |= BCR_LED_RED;
		break;

	case led_idle_end:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state &= ~BCR_LED_RED;
		break;
#endif

	case led_halted:
		break;

	case led_green_on:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state &= ~BCR_LED_GREEN;
		break;

	case led_green_off:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state |= BCR_LED_GREEN;
		break;

	case led_amber_on:
		break;

	case led_amber_off:
		break;

	case led_red_on:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state &= ~BCR_LED_RED;
		break;

	case led_red_off:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state |= BCR_LED_RED;
		break;

	default:
		break;
	}

	if  (led_state & LED_STATE_ENABLED)
		BCR = BCR_value = (BCR_value & ~BCR_LED_MASK) | hw_led_state;

	local_irq_restore(flags);
}

#endif /* CONFIG_SA1100_ASSABET */

#ifdef CONFIG_SA1100_BRUTUS

#define LED_D3		GPIO_GPIO(9)
#define LED_D4		GPIO_GPIO(8)
#define LED_D17		GPIO_GPIO(20)
#define LED_MASK	(LED_D3|LED_D4|LED_D17)

static void brutus_leds_event(led_event_t evt)
{
	unsigned long flags;

	local_irq_save(flags);

	switch (evt) {
	case led_start:
		hw_led_state = LED_MASK;
		led_state = LED_STATE_ENABLED;
		break;

	case led_stop:
		led_state &= ~LED_STATE_ENABLED;
		break;

	case led_claim:
		led_state |= LED_STATE_CLAIMED;
		hw_led_state = LED_MASK;
		break;

	case led_release:
		led_state &= ~LED_STATE_CLAIMED;
		hw_led_state = LED_MASK;
		break;

#ifdef CONFIG_LEDS_TIMER
	case led_timer:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state ^= LED_D3;
		break;
#endif

#ifdef CONFIG_LEDS_CPU
	case led_idle_start:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state |= LED_D17;
		break;

	case led_idle_end:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state &= ~LED_D17;
		break;
#endif

	case led_green_on:
		hw_led_state &= ~LED_D4;
		break;

	case led_green_off:
		hw_led_state |= LED_D4;
		break;

	case led_amber_on:
		break;

	case led_amber_off:
		break;

	case led_red_on:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state &= ~LED_D17;
		break;

	case led_red_off:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state |= LED_D17;
		break;

	default:
		break;
	}

	if  (led_state & LED_STATE_ENABLED) {
		GPSR = hw_led_state;
		GPCR = hw_led_state ^ LED_MASK;
	}

	local_irq_restore(flags);
}

#endif /* CONFIG_SA1100_BRUTUS */

#ifdef CONFIG_SA1100_LART

#define LED_23    GPIO_GPIO23
#define LED_MASK  (LED_23)


static void lart_leds_event(led_event_t evt)
{
	unsigned long flags;

	local_irq_save(flags);

	switch(evt) {
	case led_start:
		hw_led_state = LED_MASK;
		led_state = LED_STATE_ENABLED;
		break;
		
	case led_stop:
		led_state &= ~LED_STATE_ENABLED;
		break;

	case led_claim:
		led_state |= LED_STATE_CLAIMED;
		hw_led_state = LED_MASK;
		break;

	case led_release:
		led_state &= ~LED_STATE_CLAIMED;
		hw_led_state = LED_MASK;
		break;

#ifdef CONFIG_LEDS_TIMER
	case led_timer:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state ^= LED_23;
		break;
#endif

#ifdef CONFIG_LEDS_CPU
	case led_idle_start:
		/* The LART people like the LED to be off when the
                   system is idle... */
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state &= ~LED_23;
		break;

	case led_idle_end:
		/* ... and on if the system is not idle */
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state |= LED_23;
		break;
#endif

	case led_red_on:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state &= ~LED_23;
		break;

	case led_red_off:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state |= LED_23;
		break;

	default:
		break;
	}

	/* Now set the GPIO state, or nothing will happen at all */
	if (led_state & LED_STATE_ENABLED) {
		GPSR = hw_led_state;
		GPCR = hw_led_state ^ LED_MASK;
	}

	local_irq_restore(flags);
}

#endif /* CONFIG_SA1100_LART */

#ifdef CONFIG_SA1100_CERF
#define LED_D0          GPIO_GPIO(0)
#define LED_D1          GPIO_GPIO(1)
#define LED_D2          GPIO_GPIO(2)
#define LED_D3          GPIO_GPIO(3)
#define LED_MASK        (LED_D0|LED_D1|LED_D2|LED_D3)

static void cerf_leds_event(led_event_t evt)
{
        unsigned long flags;

	local_irq_save(flags);

        switch (evt) {
        case led_start:
                hw_led_state = LED_MASK;
                led_state = LED_STATE_ENABLED;
                break;

        case led_stop:
                led_state &= ~LED_STATE_ENABLED;
                break;

        case led_claim:
                led_state |= LED_STATE_CLAIMED;
                hw_led_state = LED_MASK;
                break;
        case led_release:
                led_state &= ~LED_STATE_CLAIMED;
                hw_led_state = LED_MASK;
                break;

#ifdef CONFIG_LEDS_TIMER
        case led_timer:
                if (!(led_state & LED_STATE_CLAIMED))
                        hw_led_state ^= LED_D0;
                break;
#endif

#ifdef CONFIG_LEDS_CPU
        case led_idle_start:
                if (!(led_state & LED_STATE_CLAIMED))
                        hw_led_state &= ~LED_D1;
                break;

        case led_idle_end:
                if (!(led_state & LED_STATE_CLAIMED))
                        hw_led_state |= LED_D1;
                break;
#endif
        case led_green_on:
                if (!(led_state & LED_STATE_CLAIMED))
                        hw_led_state &= ~LED_D2;
                break;

        case led_green_off:
                if (!(led_state & LED_STATE_CLAIMED))
                        hw_led_state |= LED_D2;
                break;

        case led_amber_on:
                if (!(led_state & LED_STATE_CLAIMED))
                        hw_led_state &= ~LED_D3;
                break;

        case led_amber_off:
                if (!(led_state & LED_STATE_CLAIMED))
                        hw_led_state |= LED_D3;
                break;

        case led_red_on:
                if (!(led_state & LED_STATE_CLAIMED))
                        hw_led_state &= ~LED_D1;
                break;

        case led_red_off:
                if (!(led_state & LED_STATE_CLAIMED))
                        hw_led_state |= LED_D1;
                break;

        default:
                break;
        }

        if  (led_state & LED_STATE_ENABLED) {
                GPSR = hw_led_state;
                GPCR = hw_led_state ^ LED_MASK;
        }

	local_irq_restore(flags);
}

#endif /* CONFIG_SA1100_CERF */

static int __init
sa1100_leds_init(void)
{
#ifdef CONFIG_SA1100_ASSABET
	if (machine_is_assabet())
		leds_event = assabet_leds_event;
#endif
#ifdef CONFIG_SA1100_BRUTUS
	if (machine_is_brutus())
		leds_event = brutus_leds_event;
#endif
#ifdef CONFIG_SA1100_LART
	if (machine_is_lart())
		leds_event = lart_leds_event;
#endif
#ifdef CONFIG_SA1100_CERF
       if (machine_is_cerf())
       {
                //GPDR |= 0x0000000F;
               leds_event = cerf_leds_event;
       }
#endif
	leds_event(led_start);
	return 0;
}

__initcall(sa1100_leds_init);
