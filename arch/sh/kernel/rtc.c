/*
 * linux/arch/sh/kernel/rtc.c -- SH3 / SH4 on-chip RTC support
 *
 *  Copyright (C) 2000  Philipp Rumpf <prumpf@tux.org>
 *  Copyright (C) 1999  Tetsuya Okada & Niibe Yutaka
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/time.h>

#include <asm/io.h>
#include <asm/rtc.h>

/* RCR1 Bits */
#define RCR1_CF		0x80	/* Carry Flag             */
#define RCR1_CIE	0x10	/* Carry Interrupt Enable */
#define RCR1_AIE	0x08	/* Alarm Interrupt Enable */
#define RCR1_AF		0x01	/* Alarm Flag             */

/* RCR2 Bits */
#define RCR2_PEF	0x80	/* PEriodic interrupt Flag */
#define RCR2_PESMASK	0x70	/* Periodic interrupt Set  */
#define RCR2_RTCEN	0x08	/* ENable RTC              */
#define RCR2_ADJ	0x04	/* ADJustment (30-second)  */
#define RCR2_RESET	0x02	/* Reset bit               */
#define RCR2_START	0x01	/* Start bit               */

#if defined(__sh3__)
/* SH-3 RTC */
#define R64CNT  	0xfffffec0
#define RSECCNT 	0xfffffec2
#define RMINCNT 	0xfffffec4
#define RHRCNT  	0xfffffec6
#define RWKCNT  	0xfffffec8
#define RDAYCNT 	0xfffffeca
#define RMONCNT 	0xfffffecc
#define RYRCNT  	0xfffffece
#define RSECAR  	0xfffffed0
#define RMINAR  	0xfffffed2
#define RHRAR   	0xfffffed4
#define RWKAR   	0xfffffed6
#define RDAYAR  	0xfffffed8
#define RMONAR  	0xfffffeda
#define RCR1    	0xfffffedc
#define RCR2    	0xfffffede
#elif defined(__SH4__)
/* SH-4 RTC */
#define R64CNT  	0xffc80000
#define RSECCNT 	0xffc80004
#define RMINCNT 	0xffc80008
#define RHRCNT  	0xffc8000c
#define RWKCNT  	0xffc80010
#define RDAYCNT 	0xffc80014
#define RMONCNT 	0xffc80018
#define RYRCNT  	0xffc8001c  /* 16bit */
#define RSECAR  	0xffc80020
#define RMINAR  	0xffc80024
#define RHRAR   	0xffc80028
#define RWKAR   	0xffc8002c
#define RDAYAR  	0xffc80030
#define RMONAR  	0xffc80034
#define RCR1    	0xffc80038
#define RCR2    	0xffc8003c
#endif

#ifndef BCD_TO_BIN
#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)
#endif

#ifndef BIN_TO_BCD
#define BIN_TO_BCD(val) ((val)=(((val)/10)<<4) + (val)%10)
#endif

void sh_rtc_gettimeofday(struct timeval *tv)
{
	unsigned int sec128, sec, min, hr, wk, day, mon, yr, yr100;

 again:
	do {
		ctrl_outb(0, RCR1);  /* Clear CF-bit */
		sec128 = ctrl_inb(RSECCNT);
		sec = ctrl_inb(RSECCNT);
		min = ctrl_inb(RMINCNT);
		hr  = ctrl_inb(RHRCNT);
		wk  = ctrl_inb(RWKCNT);
		day = ctrl_inb(RDAYCNT);
		mon = ctrl_inb(RMONCNT);
#if defined(__SH4__)
		yr  = ctrl_inw(RYRCNT);
		yr100 = (yr >> 8);
		yr &= 0xff;
#else
		yr  = ctrl_inb(RYRCNT);
		yr100 = (yr == 0x99) ? 0x19 : 0x20;
#endif
	} while ((ctrl_inb(RCR1) & RCR1_CF) != 0);

	BCD_TO_BIN(yr100);
	BCD_TO_BIN(yr);
	BCD_TO_BIN(mon);
	BCD_TO_BIN(day);
	BCD_TO_BIN(hr);
	BCD_TO_BIN(min);
	BCD_TO_BIN(sec);

	if (yr > 99 || mon < 1 || mon > 12 || day > 31 || day < 1 ||
	    hr > 23 || min > 59 || sec > 59) {
		printk(KERN_ERR
		       "SH RTC: invalid value, resetting to 1 Jan 2000\n");
		ctrl_outb(RCR2_RESET, RCR2);  /* Reset & Stop */
		ctrl_outb(0, RSECCNT);
		ctrl_outb(0, RMINCNT);
		ctrl_outb(0, RHRCNT);
		ctrl_outb(6, RWKCNT);
		ctrl_outb(1, RDAYCNT);
		ctrl_outb(1, RMONCNT);
#if defined(__SH4__)
		ctrl_outw(0x2000, RYRCNT);
#else
		ctrl_outb(0, RYRCNT);
#endif
		ctrl_outb(RCR2_RTCEN|RCR2_START, RCR2);  /* Start */
		goto again;
	}

	tv->tv_sec = mktime(yr100 * 100 + yr, mon, day, hr, min, sec);
	tv->tv_usec = (sec128 * 1000000) / 128;
}

static int set_rtc_time(unsigned long nowtime)
{
	extern int abs (int);
	int retval = 0;
	int real_seconds, real_minutes, cmos_minutes;

	ctrl_outb(RCR2_RESET, RCR2);  /* Reset pre-scaler & stop RTC */

	cmos_minutes = ctrl_inb(RMINCNT);
	BCD_TO_BIN(cmos_minutes);

	/*
	 * since we're only adjusting minutes and seconds,
	 * don't interfere with hour overflow. This avoids
	 * messing with unknown time zones but requires your
	 * RTC not to be off by more than 15 minutes
	 */
	real_seconds = nowtime % 60;
	real_minutes = nowtime / 60;
	if (((abs(real_minutes - cmos_minutes) + 15)/30) & 1)
		real_minutes += 30;	/* correct for half hour time zone */
	real_minutes %= 60;

	if (abs(real_minutes - cmos_minutes) < 30) {
		BIN_TO_BCD(real_seconds);
		BIN_TO_BCD(real_minutes);
		ctrl_outb(real_seconds, RSECCNT);
		ctrl_outb(real_minutes, RMINCNT);
	} else {
		printk(KERN_WARNING
		       "set_rtc_time: can't update from %d to %d\n",
		       cmos_minutes, real_minutes);
		retval = -1;
	}

	ctrl_outb(RCR2_RTCEN|RCR2_START, RCR2);  /* Start RTC */

	return retval;
}

int sh_rtc_settimeofday(const struct timeval *tv)
{
	return set_rtc_time(tv->tv_sec);
}
