#ifndef _ASM_IA64_SN_IDLE_H
#define _ASM_IA64_SN_IDLE_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2001-2002 Silicon Graphics, Inc.  All rights reserved.
 */

#include <linux/config.h>
#include <asm/sn/leds.h>
#include <asm/sn/simulator.h>

static __inline__ void
snidle(void) {

#ifdef CONFIG_IA64_SGI_AUTOTEST
	{
		extern int	autotest_enabled;
		if (autotest_enabled) {
			extern void llsc_main(int, long, long);
			llsc_main(smp_processor_id(), 0xe000000000000000LL, 0xe000000001000000LL);
		}
	}
#endif
	
	if (pda.idle_flag == 0) {
		/* 
		 * Turn the activity LED off.
		 */
		set_led_bits(0, LED_CPU_ACTIVITY);
	}

#ifdef CONFIG_IA64_SGI_SN_SIM
	if (IS_RUNNING_ON_SIMULATOR())
		SIMULATOR_SLEEP();
#endif

	pda.idle_flag = 1;
}

static __inline__ void
snidleoff(void) {
	/* 
	 * Turn the activity LED on.
	 */
	set_led_bits(LED_CPU_ACTIVITY, LED_CPU_ACTIVITY);

	pda.idle_flag = 0;
}

#endif /* _ASM_IA64_SN_IDLE_H */
