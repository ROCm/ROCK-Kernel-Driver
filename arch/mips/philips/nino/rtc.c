/*
 *  linux/arch/mips/philips/nino/rtc.c
 *
 *  Copyright (C) 2001 Steven J. Hill (sjhill@realitydiluted.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Functions to access RTC on the Philips Nino.
 */
#include <linux/spinlock.h>
#include <linux/mc146818rtc.h>

static unsigned char nino_rtc_read_data(unsigned long addr)
{
	return 0;
}

static void nino_rtc_write_data(unsigned char data, unsigned long addr)
{
}

static int nino_rtc_bcd_mode(void)
{
	return 0;
}

struct rtc_ops nino_rtc_ops =
{
	&nino_rtc_read_data,
	&nino_rtc_write_data,
	&nino_rtc_bcd_mode
};
