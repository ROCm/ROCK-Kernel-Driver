/* $Id: rtc-no.c,v 1.2 1998/06/25 20:19:15 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Stub RTC routines to keep Linux from crashing on machine which don't
 * have a RTC chip.
 *
 * Copyright (C) 1998 by Ralf Baechle
 */
#include <linux/kernel.h>
#include <linux/mc146818rtc.h>

static unsigned char no_rtc_read_data(unsigned long addr)
{
	panic("no_rtc_read_data called - shouldn't happen.");
}

static void no_rtc_write_data(unsigned char data, unsigned long addr)
{
	panic("no_rtc_write_data called - shouldn't happen.");
}

static int no_rtc_bcd_mode(void)
{
	panic("no_rtc_bcd_mode called - shouldn't happen.");
}

struct rtc_ops no_rtc_ops = {
	&no_rtc_read_data,
	&no_rtc_write_data,
	&no_rtc_bcd_mode
};
