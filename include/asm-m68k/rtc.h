/* asm-m68k/rtc.h
 *
 * Copyright Richard Zidlicky
 * implementation details for genrtc/q40rtc driver
 */
/* permission is hereby granted to copy, modify and redistribute this code
 * in terms of the GNU Library General Public License, Version 2 or later,
 * at your option.
 */

#ifndef _ASM_RTC_H
#define _ASM_RTC_H

#ifdef __KERNEL__

#include <asm/machdep.h>

/* a few implementation details for the emulation : */

extern unsigned gen_rtc_irq_flags; /* which sort(s) of interrupts caused int */
extern unsigned gen_rtc_irq_ctrl;  /*                             are enabled */
extern short q40rtc_oldsecs;

#define RTC_PIE 0x40		/* periodic interrupt enable */
#define RTC_AIE 0x20		/* alarm interrupt enable */
#define RTC_UIE 0x10		/* update-finished interrupt enable */

extern void gen_rtc_interrupt(unsigned long);

/* some dummy definitions */
#define RTC_SQWE 0x08		/* enable square-wave output */
#define RTC_DM_BINARY 0x04	/* all time/date values are BCD if clear */
#define RTC_24H 0x02		/* 24 hour mode - else hours bit 7 means pm */
#define RTC_DST_EN 0x01	        /* auto switch DST - works f. USA only */

static inline void get_rtc_time(struct rtc_time *time)
{
	/*
	 * Only the values that we read from the RTC are set. We leave
	 * tm_wday, tm_yday and tm_isdst untouched. Even though the
	 * RTC has RTC_DAY_OF_WEEK, we ignore it, as it is only updated
	 * by the RTC when initially set to a non-zero value.
	 */
	mach_hwclk(0, time);
}

static inline int set_rtc_time(struct rtc_time *time)
{
	return mach_hwclk(1, time);
}

static inline int get_rtc_pll(struct rtc_pll_info *pll)
{
	if (mach_get_rtc_pll)
		return mach_get_rtc_pll(pll);
	else
		return -EINVAL;
}
static inline int set_rtc_pll(struct rtc_pll_info *pll)
{
	if (mach_set_rtc_pll)
		return mach_set_rtc_pll(pll);
	else
		return -EINVAL;
}

#endif /* __KERNEL__ */

#endif /* _ASM__RTC_H */
