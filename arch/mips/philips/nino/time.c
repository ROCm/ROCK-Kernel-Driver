/*
 *  linux/arch/mips/philips/nino/time.c
 *
 *  Copyright (C) 1999 Harald Koerfgen
 *  Copyright (C) 2000 Pavel Machek (pavel@suse.cz)
 *  Copyright (C) 2001 Steven J. Hill (sjhill@realitydiluted.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Time handling functinos for Philips Nino.
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/delay.h>
#include <asm/tx3912.h>

extern volatile unsigned long wall_jiffies;
extern rwlock_t xtime_lock;

static struct timeval xbase;

#define USECS_PER_JIFFY (1000000/HZ)

/*
 * Poll the Interrupt Status Registers
 */
#undef POLL_STATUS

static unsigned long do_gettimeoffset(void)
{
    /*
     * This is a kludge
     */
    return 0;
}

static
void inline readRTC(unsigned long *high, unsigned long *low)
{
	/* read twice, and keep reading till we find two
	 * the same pairs. This is needed in case the RTC
	 * was updating its registers and we read a old
	 * High but a new Low. */
	do {
		*high = RTChigh & RTC_HIGHMASK;
		*low = RTClow;
	} while (*high != (RTChigh & RTC_HIGHMASK) || RTClow!=*low);
}

/*
 * This version of gettimeofday has near millisecond resolution.
 */
void do_gettimeofday(struct timeval *tv)
{
    unsigned long flags;
    unsigned long high, low;

    read_lock_irqsave(&xtime_lock, flags);
    // 40 bit RTC, driven by 32khz source:
    // +-----------+-----------------------------------------+
    // | HHHH.HHHH | LLLL.LLLL.LLLL.LLLL.LMMM.MMMM.MMMM.MMMM |
    // +-----------+-----------------------------------------+
    readRTC(&high,&low);
    tv->tv_sec  = (high << 17) | (low >> 15);
    tv->tv_usec = (low % 32768) * 1953 / 64;
    tv->tv_sec += xbase.tv_sec;
    tv->tv_usec += xbase.tv_usec;

    tv->tv_usec += do_gettimeoffset();

    /*
     * xtime is atomically updated in timer_bh. lost_ticks is
     * nonzero if the timer bottom half hasnt executed yet.
     */
    if (jiffies - wall_jiffies)
	tv->tv_usec += USECS_PER_JIFFY;

    read_unlock_irqrestore(&xtime_lock, flags);

    if (tv->tv_usec >= 1000000) {
	tv->tv_usec -= 1000000;
	tv->tv_sec++;
    }
}

void do_settimeofday(struct timeval *tv)
{
    write_lock_irq(&xtime_lock);
    /* This is revolting. We need to set the xtime.tv_usec
     * correctly. However, the value in this location is
     * is value at the last tick.
     * Discover what correction gettimeofday
     * would have done, and then undo it!
     */
    tv->tv_usec -= do_gettimeoffset();

    if (tv->tv_usec < 0) {
	tv->tv_usec += 1000000;
	tv->tv_sec--;
    }

    /* reset RTC to 0 (real time is xbase + RTC) */
    xbase = *tv;
    RTCtimerControl |=  TIM_RTCCLEAR;
    RTCtimerControl &= ~TIM_RTCCLEAR;
    RTCalarmHigh = RTCalarmLow = ~0UL;

    xtime = *tv;
    time_state = TIME_BAD;
    time_maxerror = MAXPHASE;
    time_esterror = MAXPHASE;
    write_unlock_irq(&xtime_lock);
}

static int set_rtc_mmss(unsigned long nowtime)
{
    int retval = 0;

    return retval;
}

/* last time the cmos clock got updated */
static long last_rtc_update = 0;

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */

int do_write = 1;

static void
timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
#ifdef POLL_STATUS
    static unsigned long old_IntStatus1 = 0;
    static unsigned long old_IntStatus3 = 0;
    static unsigned long old_IntStatus4 = 0;
    static unsigned long old_IntStatus5 = 0;
    static int counter = 0;
    int i;

    new_spircv = SPIData & 0xff;
    if ((old_spircv != new_spircv) && (new_spircv != 0xff)) {
	    printk( "SPIData changed: %x\n", new_spircv );
    }
    old_spircv = new_spircv;
    if (do_write)
	    SPIData = 0;
#endif

    if (!user_mode(regs)) {
	if (prof_buffer && current->pid) {
	    extern int _stext;
	    unsigned long pc = regs->cp0_epc;

	    pc -= (unsigned long) &_stext;
	    pc >>= prof_shift;
	    /*
	     * Dont ignore out-of-bounds pc values silently,
	     * put them into the last histogram slot, so if
	     * present, they will show up as a sharp peak.
	     */
	    if (pc > prof_len - 1)
		pc = prof_len - 1;
		atomic_inc((atomic_t *) & prof_buffer[pc]);
	    }
    }

    /*
     * aaaand... action!
     */
    do_timer(regs);

    /*
     * If we have an externally syncronized Linux clock, then update
     * CMOS clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
     * called as close as possible to 500 ms before the new second starts.
     */
    if (time_state != TIME_BAD && xtime.tv_sec > last_rtc_update + 660 &&
	xtime.tv_usec > 500000 - (tick >> 1) &&
	xtime.tv_usec < 500000 + (tick >> 1))
    {
	if (set_rtc_mmss(xtime.tv_sec) == 0)
	    last_rtc_update = xtime.tv_sec;
	else
	    last_rtc_update = xtime.tv_sec - 600;  /* do it again in 60 s */
    }
}

static struct irqaction irq0 = {timer_interrupt, SA_INTERRUPT, 0,
 			 "timer", NULL, NULL};

void (*board_time_init) (struct irqaction * irq);

int __init time_init(void)
{
    struct timeval starttime;

    starttime.tv_sec = mktime(2000, 1, 1, 0, 0, 0);
    starttime.tv_usec = 0;
    do_settimeofday(&starttime);

    board_time_init(&irq0);

    return 0;
}
