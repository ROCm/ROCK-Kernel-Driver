/*
 * RTC routines for IP32 style attached Dallas chip.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Harald Koerfgen
 */
#include <linux/mc146818rtc.h>
#include <asm/ip32/mace.h>

static unsigned char ip32_rtc_read_data(unsigned long addr)
{
	return (unsigned char) mace_read_8 (MACEISA_RTC_BASE + (addr << 8));
}

static void ip32_rtc_write_data(unsigned char data, unsigned long addr)
{
	mace_write_8 (MACEISA_RTC_BASE + (addr << 8), data);
}

static int ip32_rtc_bcd_mode(void)
{
	return 0;
}

struct rtc_ops ip32_rtc_ops = {
	&ip32_rtc_read_data,
	&ip32_rtc_write_data,
	&ip32_rtc_bcd_mode
};
