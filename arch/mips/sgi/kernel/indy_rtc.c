/* $Id: indy_rtc.c,v 1.1 1998/06/30 00:21:58 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * RTC routines for Indy style attached Dallas chip.
 *
 * Copyright (C) 1998 by Ralf Baechle
 */
#include <linux/mc146818rtc.h>
#include <asm/sgi/sgihpc.h>

static unsigned char indy_rtc_read_data(unsigned long addr)
{
	volatile unsigned int *rtcregs = (void *)INDY_CLOCK_REGS;

	return rtcregs[addr];
}

static void indy_rtc_write_data(unsigned char data, unsigned long addr)
{
	volatile unsigned int *rtcregs = (void *)INDY_CLOCK_REGS;

	rtcregs[addr] = data;
}

static int indy_rtc_bcd_mode(void)
{
	return 0;
}

struct rtc_ops indy_rtc_ops = {
	&indy_rtc_read_data,
	&indy_rtc_write_data,
	&indy_rtc_bcd_mode
};
