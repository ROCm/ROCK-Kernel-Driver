/*
 * IP32 timer calibration
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 Keith M Wesolowski
 */
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/mipsregs.h>
#include <asm/param.h>
#include <asm/ip32/crime.h>
#include <asm/ip32/ip32_ints.h>

extern u32 cc_interval;

/* An arbitrary time; this can be decreased if reliability looks good */
#define WAIT_MS 10
#define PER_MHZ (1000000 / 2 / HZ)

void __init ip32_timer_setup (struct irqaction *irq) {
	u64 crime_time;
	u32 cc_tick;

	printk("Calibrating system timer... ");

	crime_time = crime_read_64 (CRIME_TIME) & CRIME_TIME_MASK;
	cc_tick = read_32bit_cp0_register (CP0_COUNT);

	while ((crime_read_64 (CRIME_TIME) & CRIME_TIME_MASK) - crime_time 
		< WAIT_MS * 1000000 / CRIME_NS_PER_TICK)
		;
	cc_tick = read_32bit_cp0_register (CP0_COUNT) - cc_tick;
	cc_interval = cc_tick / HZ * (1000 / WAIT_MS);
	/* The round-off seems unnecessary; in testing, the error of the
	 * above procedure is < 100 ticks, which means it gets filtered
	 * out by the HZ adjustment. 
	 */
	cc_interval = (cc_interval / PER_MHZ) * PER_MHZ;

	printk("%d MHz CPU detected\n", (int) (cc_interval / PER_MHZ));

	setup_irq (CLOCK_IRQ, irq);
}
