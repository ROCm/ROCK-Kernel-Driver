/*
 *  include/asm-i386/mach-pc9800/mach_time.h
 *
 *  Machine specific set RTC function for PC-9800.
 *  Written by Osamu Tomita <tomita@cinet.co.jp>
 */
#ifndef _MACH_TIME_H
#define _MACH_TIME_H

#include <linux/bcd.h>
#include <linux/upd4990a.h>

/* for check timing call set_rtc_mmss() */
/* used in arch/i386/time.c::do_timer_interrupt() */
/*
 * Because PC-9800's RTC (NEC uPD4990A) does not allow setting
 * time partially, we always have to read-modify-write the
 * entire time (including year) so that set_rtc_mmss() will
 * take quite much time to execute.  You may want to relax
 * RTC resetting interval (currently ~11 minuts)...
 */
#define USEC_AFTER	1000000
#define USEC_BEFORE	0

static inline int mach_set_rtc_mmss(unsigned long nowtime)
{
	int retval = 0;
	int real_seconds, real_minutes, cmos_minutes;
	struct upd4990a_raw_data data;

	upd4990a_get_time(&data, 1);
	cmos_minutes = BCD2BIN(data.min);

	/*
	 * since we're only adjusting minutes and seconds,
	 * don't interfere with hour overflow. This avoids
	 * messing with unknown time zones but requires your
	 * RTC not to be off by more than 15 minutes
	 */
	real_seconds = nowtime % 60;
	real_minutes = nowtime / 60;
	if (((abs(real_minutes - cmos_minutes) + 15) / 30) & 1)
		real_minutes += 30;	/* correct for half hour time zone */
	real_minutes %= 60;

	if (abs(real_minutes - cmos_minutes) < 30) {
		u8 temp_seconds = (real_seconds / 10) * 16 + real_seconds % 10;
		u8 temp_minutes = (real_minutes / 10) * 16 + real_minutes % 10;

		if (data.sec != temp_seconds || data.min != temp_minutes) {
			data.sec = temp_seconds;
			data.min = temp_minutes;
			upd4990a_set_time(&data, 1);
		}
	} else {
		printk(KERN_WARNING
		       "set_rtc_mmss: can't update from %d to %d\n",
		       cmos_minutes, real_minutes);
		retval = -1;
	}

	/* uPD4990A users' manual says we should issue Register Hold
	 * command after reading time, or future Time Read command
	 * may not work.  When we have set the time, this also starts
	 * the clock.
	 */
	upd4990a_serial_command(UPD4990A_REGISTER_HOLD);

	return retval;
}

static inline unsigned long mach_get_cmos_time(void)
{
	int i;
	u8 prev, cur;
	unsigned int year;
	struct upd4990a_raw_data data;

	/* Connect uPD4990A's DATA OUT pin to its 1Hz reference clock. */
	upd4990a_serial_command(UPD4990A_REGISTER_HOLD);

	/* Catch rising edge of reference clock.  */
	prev = ~UPD4990A_READ_DATA();
	for (i = 0; i < 1800000; i++) { /* may take up to 1 second... */
		__asm__ ("outb %%al,%0" : : "N" (0x5f)); /* 0.6usec delay */
		cur = UPD4990A_READ_DATA();
		if (!(prev & cur & 1))
			break;
		prev = ~cur;
	}

	upd4990a_get_time(&data, 0);

	if ((year = BCD2BIN(data.year) + 1900) < 1995)
		year += 100;
	return mktime(year, data.mon, BCD2BIN(data.mday), BCD2BIN(data.hour),
			BCD2BIN(data.min), BCD2BIN(data.sec));
}

#endif /* !_MACH_TIME_H */
