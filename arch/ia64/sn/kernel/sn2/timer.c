/*
 * linux/arch/ia64/sn/kernel/sn2/timer.c
 *
 * Copyright (C) 2003 Silicon Graphics, Inc.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/interrupt.h>

#include <asm/hw_irq.h>
#include <asm/system.h>

#include <asm/sn/leds.h>
#include <asm/sn/clksupport.h>


extern unsigned long sn_rtc_cycles_per_second;
static volatile unsigned long last_wall_rtc;

static long rtc_nsecs_per_cycle;
static long rtc_per_timer_tick;

static unsigned long
getoffset(void)
{
	return (long) (GET_RTC_COUNTER() - last_wall_rtc)*rtc_nsecs_per_cycle;
}


static void
update(long delta_nsec)
{
	last_wall_rtc = GET_RTC_COUNTER();
}


static void
reset(void)
{
	last_wall_rtc = GET_RTC_COUNTER();
}


static struct time_interpolator sn2_interpolator = {
	.get_offset =	getoffset,
	.update =	update,
	.reset =	reset
};

void __init
sn_timer_init(void)
{
	sn2_interpolator.frequency = sn_rtc_cycles_per_second;
	sn2_interpolator.drift = -1;	/* unknown */
	register_time_interpolator(&sn2_interpolator);

	rtc_per_timer_tick = sn_rtc_cycles_per_second / HZ;
	rtc_nsecs_per_cycle = 1000000000 / sn_rtc_cycles_per_second;

	last_wall_rtc = GET_RTC_COUNTER();
}
