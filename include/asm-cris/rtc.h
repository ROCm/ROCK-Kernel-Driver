/* $Id: rtc.h,v 1.1 2000/07/10 16:32:31 bjornw Exp $ */

#ifndef RTC_H
#define RTC_H

#include <linux/config.h>

/* Dallas DS1302 clock/calendar register numbers */

#define RTC_SECONDS 0
#define RTC_MINUTES 1
#define RTC_HOURS 2
#define RTC_DAY_OF_MONTH 3
#define RTC_MONTH 4
#define RTC_WEEKDAY 5
#define RTC_YEAR 6

#ifdef CONFIG_DS1302
#define CMOS_READ(x) ds1302_readreg(x)
#define CMOS_WRITE(val,reg) ds1302_writereg(reg,val)
#define RTC_INIT() ds1302_init()
#else
/* no RTC configured so we shouldn't try to access any */
#define CMOS_READ(x) 42
#define CMOS_WRITE(x,y)
#define RTC_INIT() (-1)
#endif

/* conversions to and from the stupid RTC internal format */

#define BCD_TO_BIN(x) x = (((x & 0xf0) >> 3) * 5 + (x & 0xf))
#define BIN_TO_BCD(x) x = (x % 10) | ((x / 10) << 4) 

/*
 * The struct used to pass data via the following ioctl. Similar to the
 * struct tm in <time.h>, but it needs to be here so that the kernel 
 * source is self contained, allowing cross-compiles, etc. etc.
 */

struct rtc_time {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_wday;
	int tm_yday;
	int tm_isdst;
};

/*
 * ioctl calls that are permitted to the /dev/rtc interface
 */

#define RTC_RD_TIME	_IOR('p', 0x09, struct rtc_time) /* Read RTC time   */
#define RTC_SET_TIME	_IOW('p', 0x0a, struct rtc_time) /* Set RTC time    */

#endif
