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

#include <linux/config.h>

struct hwclk_time {
	unsigned	sec;	/* 0..59 */
	unsigned	min;	/* 0..59 */
	unsigned	hour;	/* 0..23 */
	unsigned	day;	/* 1..31 */
	unsigned	mon;	/* 0..11 */
	unsigned	year;	/* 70... */
	int		wday;	/* 0..6, 0 is Sunday, -1 means unknown/don't set */
};

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


#endif /* __KERNEL__ */

#endif /* _ASM__RTC_H */
