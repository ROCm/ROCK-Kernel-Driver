/*
 * Copyright (C) 2001 MontaVista Software, ppopov@mvista.com
 * Copied and modified Carsten Langgaard's time.c
 *
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 * Setting up the clock on the MIPS boards.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <linux/spinlock.h>

#include <asm/mipsregs.h>
#include <asm/ptrace.h>
#include <asm/div64.h>
#include <asm/au1000.h>

#include <linux/mc146818rtc.h>
#include <linux/timex.h>

extern volatile unsigned long wall_jiffies;
unsigned long missed_heart_beats = 0;
unsigned long uart_baud_base;

static unsigned long r4k_offset; /* Amount to increment compare reg each time */
static unsigned long r4k_cur;    /* What counter should be at next timer irq */
extern rwlock_t xtime_lock;

#define ALLINTS (IE_IRQ0 | IE_IRQ1 | IE_IRQ2 | IE_IRQ3 | IE_IRQ4 | IE_IRQ5)

static inline void ack_r4ktimer(unsigned long newval)
{
	write_32bit_cp0_register(CP0_COMPARE, newval);
}


/*
 * There are a lot of conceptually broken versions of the MIPS timer interrupt
 * handler floating around.  This one is rather different, but the algorithm
 * is provably more robust.
 */
unsigned long wtimer;
void mips_timer_interrupt(struct pt_regs *regs)
{
	int irq = 7;

	if (r4k_offset == 0)
		goto null;

	do {
		kstat.irqs[0][irq]++;
		do_timer(regs);
		r4k_cur += r4k_offset;
		ack_r4ktimer(r4k_cur);

	} while (((unsigned long)read_32bit_cp0_register(CP0_COUNT)
	         - r4k_cur) < 0x7fffffff);

	return;

null:
	ack_r4ktimer(0);
}

/* 
 * Figure out the r4k offset, the amount to increment the compare
 * register for each time tick. 
 * Use the Programmable Counter 1 to do this.
 */
unsigned long cal_r4koff(void)
{
	unsigned long count;
	unsigned long cpu_pll;
	unsigned long cpu_speed;
	unsigned long start, end;
	unsigned long counter;
	int i;
	int trim_divide = 16;

	counter = inl(PC_COUNTER_CNTRL);
	outl(counter | PC_CNTRL_EN1, PC_COUNTER_CNTRL);

	while (inl(PC_COUNTER_CNTRL) & PC_CNTRL_T1S);
	outl(trim_divide-1, PC1_TRIM);    /* RTC now ticks at 32.768/16 kHz */
	while (inl(PC_COUNTER_CNTRL) & PC_CNTRL_T1S);

	while (inl(PC_COUNTER_CNTRL) & PC_CNTRL_C1S);
	outl (0, PC1_COUNTER_WRITE);
	while (inl(PC_COUNTER_CNTRL) & PC_CNTRL_C1S);

	start = inl(PC1_COUNTER_READ);
	start += 2;
	/* wait for the beginning of a new tick */
	while (inl(PC1_COUNTER_READ) < start);

	/* Start r4k counter. */
	write_32bit_cp0_register(CP0_COUNT, 0);
	end = start + (32768 / trim_divide)/2; /* wait 0.5 seconds */

	while (end > inl(PC1_COUNTER_READ));

	count = read_32bit_cp0_register(CP0_COUNT);
	cpu_speed = count * 2;
	uart_baud_base = (((cpu_speed) / 4) / 16);
	return (cpu_speed / HZ);
}

static unsigned long __init get_mips_time(void)
{
	return inl(PC0_COUNTER_READ);
}

void __init time_init(void)
{
        unsigned int est_freq, flags;

	printk("calculating r4koff... ");
	r4k_offset = cal_r4koff();
	printk("%08lx(%d)\n", r4k_offset, (int) r4k_offset);

	//est_freq = 2*r4k_offset*HZ;	
	est_freq = r4k_offset*HZ;	
	est_freq += 5000;    /* round */
	est_freq -= est_freq%10000;
	printk("CPU frequency %d.%02d MHz\n", est_freq/1000000, 
	       (est_freq%1000000)*100/1000000);
	r4k_cur = (read_32bit_cp0_register(CP0_COUNT) + r4k_offset);

	write_32bit_cp0_register(CP0_COMPARE, r4k_cur);
	set_cp0_status(ALLINTS);

	/* Read time from the RTC chipset. */
	write_lock_irqsave (&xtime_lock, flags);
	xtime.tv_sec = get_mips_time();
	xtime.tv_usec = 0;
	write_unlock_irqrestore(&xtime_lock, flags);
}

/* This is for machines which generate the exact clock. */
#define USECS_PER_JIFFY (1000000/HZ)
#define USECS_PER_JIFFY_FRAC ((1000000ULL << 32) / HZ & 0xffffffff)

/* Cycle counter value at the previous timer interrupt.. */

static unsigned int timerhi = 0, timerlo = 0;

static unsigned long
div64_32(unsigned long v1, unsigned long v2, unsigned long v3)
{
	unsigned long r0;
	do_div64_32(r0, v1, v2, v3);
	return r0;
}


/*
 * FIXME: Does playing with the RP bit in c0_status interfere with this code?
 */
static unsigned long do_fast_gettimeoffset(void)
{
	u32 count;
	unsigned long res, tmp;
	unsigned long r0;

	/* Last jiffy when do_fast_gettimeoffset() was called. */
	static unsigned long last_jiffies=0;
	unsigned long quotient;

	/*
	 * Cached "1/(clocks per usec)*2^32" value.
	 * It has to be recalculated once each jiffy.
	 */
	static unsigned long cached_quotient=0;

	tmp = jiffies;

	quotient = cached_quotient;

	if (tmp && last_jiffies != tmp) {
		last_jiffies = tmp;
		if (last_jiffies != 0) {
			r0 = div64_32(timerhi, timerlo, tmp);
			quotient = div64_32(USECS_PER_JIFFY, USECS_PER_JIFFY_FRAC, r0);
			cached_quotient = quotient;
		}
	}

	/* Get last timer tick in absolute kernel time */
	count = read_32bit_cp0_register(CP0_COUNT);

	/* .. relative to previous jiffy (32 bits is enough) */
	count -= timerlo;

	__asm__("multu\t%1,%2\n\t"
		"mfhi\t%0"
		:"=r" (res)
		:"r" (count),
		 "r" (quotient));

	/*
 	 * Due to possible jiffies inconsistencies, we need to check 
	 * the result so that we'll get a timer that is monotonic.
	 */
	if (res >= USECS_PER_JIFFY)
		res = USECS_PER_JIFFY-1;

	return res;
}

void do_gettimeofday(struct timeval *tv)
{
	unsigned int flags;

	read_lock_irqsave (&xtime_lock, flags);
	*tv = xtime;
	tv->tv_usec += do_fast_gettimeoffset();

	/*
	 * xtime is atomically updated in timer_bh. jiffies - wall_jiffies
	 * is nonzero if the timer bottom half hasnt executed yet.
	 */
	if (jiffies - wall_jiffies)
		tv->tv_usec += USECS_PER_JIFFY;

	read_unlock_irqrestore (&xtime_lock, flags);

	if (tv->tv_usec >= 1000000) {
		tv->tv_usec -= 1000000;
		tv->tv_sec++;
	}
}

void do_settimeofday(struct timeval *tv)
{
	write_lock_irq (&xtime_lock);

	/* This is revolting. We need to set the xtime.tv_usec correctly.
	 * However, the value in this location is is value at the last tick.
	 * Discover what correction gettimeofday would have done, and then
	 * undo it!
	 */
	tv->tv_usec -= do_fast_gettimeoffset();

	if (tv->tv_usec < 0) {
		tv->tv_usec += 1000000;
		tv->tv_sec--;
	}

	xtime = *tv;
	time_adjust = 0;		/* stop active adjtime() */
	time_status |= STA_UNSYNC;
	time_maxerror = NTP_PHASE_LIMIT;
	time_esterror = NTP_PHASE_LIMIT;

	write_unlock_irq (&xtime_lock);
}

/*
 * The UART baud base is not known at compile time ... if
 * we want to be able to use the same code on different
 * speed CPUs.
 */
unsigned long get_au1000_uart_baud()
{
	return uart_baud_base;
}
