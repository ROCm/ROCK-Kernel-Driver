/*
 *  linux/arch/arm/mach-integrator/time.c
 *
 *  Copyright (C) 2000 Deep Blue Solutions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>

#include <asm/hardware.h>

#define RTC_DR		(*(unsigned long *)(IO_ADDRESS(INTEGRATOR_RTC_BASE) + 0))
#define RTC_MR		(*(unsigned long *)(IO_ADDRESS(INTEGRATOR_RTC_BASE) + 4))
#define RTC_STAT	(*(unsigned long *)(IO_ADDRESS(INTEGRATOR_RTC_BASE) + 8))
#define RTC_EOI		(*(unsigned long *)(IO_ADDRESS(INTEGRATOR_RTC_BASE) + 8))
#define RTC_LR		(*(unsigned long *)(IO_ADDRESS(INTEGRATOR_RTC_BASE) + 12))
#define RTC_CR		(*(unsigned long *)(IO_ADDRESS(INTEGRATOR_RTC_BASE) + 16))

#define RTC_CR_MIE	0x00000001

extern int (*set_rtc)(void);

static int integrator_set_rtc(void)
{
	RTC_LR = xtime.tv_sec;
	return 1;
}

static int integrator_rtc_init(void)
{
	RTC_CR = 0;
	RTC_EOI = 0;

	xtime.tv_sec = RTC_DR;

	set_rtc = integrator_set_rtc;

	return 0;
}

__initcall(integrator_rtc_init);
