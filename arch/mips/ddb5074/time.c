/*
 *  arch/mips/ddb5074/time.c -- Timer routines
 *
 *  Copyright (C) 2000 Geert Uytterhoeven <geert@sonycom.com>
 *                     Sony Software Development Center Europe (SDCE), Brussels
 */
#include <linux/init.h>
#include <asm/mc146818rtc.h>

static unsigned char ddb_rtc_read_data(unsigned long addr)
{
	outb_p(addr, RTC_PORT(0));

	return inb_p(RTC_PORT(1));
}

static void ddb_rtc_write_data(unsigned char data, unsigned long addr)
{
	outb_p(addr, RTC_PORT(0));
	outb_p(data, RTC_PORT(1));
}

static int ddb_rtc_bcd_mode(void)
{
	return 1;
}

struct rtc_ops ddb_rtc_ops = {
	ddb_rtc_read_data,
	ddb_rtc_write_data,
	ddb_rtc_bcd_mode
};
