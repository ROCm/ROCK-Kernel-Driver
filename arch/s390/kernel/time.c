/*
 *  arch/s390/kernel/time.c
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *
 *  Derived from "arch/i386/kernel/time.c"
 *    Copyright (C) 1991, 1992, 1995  Linus Torvalds
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/types.h>

#include <asm/uaccess.h>
#include <asm/delay.h>

#include <linux/mc146818rtc.h>
#include <linux/timex.h>

#include <asm/irq.h>


extern volatile unsigned long lost_ticks;

/* change this if you have some constant time drift */
#define USECS_PER_JIFFY ((signed long)1000000/HZ)
#define CLK_TICKS_PER_JIFFY ((signed long)USECS_PER_JIFFY<<12)

#define TICK_SIZE tick

static uint64_t init_timer_cc, last_timer_cc;

extern rwlock_t xtime_lock;

void tod_to_timeval(uint64_t todval, struct timeval *xtime)
{
        const int high_bit = 0x80000000L;
        const int c_f4240 = 0xf4240L;
        const int c_7a120 = 0x7a120;
	/* We have to divide the 64 bit value todval by 4096
	 * (because the 2^12 bit is the one that changes every 
         * microsecond) and then split it into seconds and
         * microseconds. A value of max (2^52-1) divided by
         * the value 0xF4240 can yield a max result of approx
         * (2^32.068). Thats to big to fit into a signed int
	 *   ... hacking time!
         */
	asm volatile ("L     2,%1\n\t"
		      "LR    3,2\n\t"
		      "SRL   2,12\n\t"
		      "SLL   3,20\n\t"
		      "L     4,%O1+4(%R1)\n\t"
		      "SRL   4,12\n\t"
		      "OR    3,4\n\t"  /* now R2/R3 contain (todval >> 12) */
		      "SR    4,4\n\t"
		      "CL    2,%2\n\t"
		      "JL    .+12\n\t"
		      "S     2,%2\n\t"
		      "L     4,%3\n\t"
                      "D     2,%4\n\t"
		      "OR    3,4\n\t"
		      "ST    2,%O0+4(%R0)\n\t"
		      "ST    3,%0"
		      : "=m" (*xtime) : "m" (todval),
		        "m" (c_7a120), "m" (high_bit), "m" (c_f4240)
		      : "cc", "memory", "2", "3", "4" );
}

unsigned long do_gettimeoffset(void) 
{
	__u64 timer_cc;

	asm volatile ("STCK %0" : "=m" (timer_cc));
        /* We require the offset from the previous interrupt */
        return ((unsigned long)((timer_cc - last_timer_cc)>>12));
}

/*
 * This version of gettimeofday has microsecond resolution.
 */
void do_gettimeofday(struct timeval *tv)
{
	extern volatile unsigned long lost_ticks;
	unsigned long flags;
	unsigned long usec, sec;

	read_lock_irqsave(&xtime_lock, flags);
	usec = do_gettimeoffset();
	if (lost_ticks)
		usec +=(USECS_PER_JIFFY*lost_ticks);
	sec = xtime.tv_sec;
	usec += xtime.tv_usec;
	read_unlock_irqrestore(&xtime_lock, flags);

	while (usec >= 1000000) {
		usec -= 1000000;
		sec++;
	}

	tv->tv_sec = sec;
	tv->tv_usec = usec;
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

	while (tv->tv_usec < 0) {
		tv->tv_usec += 1000000;
		tv->tv_sec--;
	}

	xtime = *tv;
	time_adjust = 0;		/* stop active adjtime() */
	time_status |= STA_UNSYNC;
	time_maxerror = NTP_PHASE_LIMIT;
	time_esterror = NTP_PHASE_LIMIT;
	write_unlock_irq(&xtime_lock);
}

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */

#ifdef CONFIG_SMP
extern __u16 boot_cpu_addr;
#endif

void do_timer_interrupt(struct pt_regs *regs,int error_code)
{
        unsigned long flags;

        /*
         * reset timer to 10ms minus time already elapsed
         * since timer-interrupt pending
         */
 
        save_flags(flags);
        cli();
#ifdef CONFIG_SMP
	if(S390_lowcore.cpu_data.cpu_addr==boot_cpu_addr) {
		write_lock(&xtime_lock);
		last_timer_cc = S390_lowcore.jiffy_timer_cc;
	}
#else
        last_timer_cc = S390_lowcore.jiffy_timer_cc;
#endif
        /* set clock comparator */
        S390_lowcore.jiffy_timer_cc += CLK_TICKS_PER_JIFFY;
        asm volatile ("SCKC %0" : : "m" (S390_lowcore.jiffy_timer_cc));

/*
 * In the SMP case we use the local timer interrupt to do the
 * profiling, except when we simulate SMP mode on a uniprocessor
 * system, in that case we have to call the local interrupt handler.
 */
#ifdef CONFIG_SMP
        /* when SMP, do smp_local_timer_interrupt for *all* CPUs,
           but only do the rest for the boot CPU */
        smp_local_timer_interrupt(regs);
#else
        if (!user_mode(regs))
                s390_do_profile(regs->psw.addr);
#endif

#ifdef CONFIG_SMP
	if(S390_lowcore.cpu_data.cpu_addr==boot_cpu_addr)
#endif
	{
		do_timer(regs);
#ifdef CONFIG_SMP
		write_unlock(&xtime_lock);
#endif
	}
        restore_flags(flags);

}

/*
 * Start the clock comparator on the current CPU
 */
static long cr0 __attribute__ ((aligned (8)));

void init_100hz_timer(void)
{
        /* allow clock comparator timer interrupt */
        asm volatile ("STCTL 0,0,%0" : "=m" (cr0) : : "memory");
        cr0 |= 0x800;
        asm volatile ("LCTL 0,0,%0" : : "m" (cr0) : "memory");
        /* set clock comparator */
        /* read the TOD clock */
        asm volatile ("STCK %0" : "=m" (S390_lowcore.jiffy_timer_cc));
        S390_lowcore.jiffy_timer_cc += CLK_TICKS_PER_JIFFY;
        asm volatile ("SCKC %0" : : "m" (S390_lowcore.jiffy_timer_cc));
}

/*
 * Initialize the TOD clock and the CPU timer of
 * the boot cpu.
 */
void __init time_init(void)
{
	int cc;

        /* kick the TOD clock */
        asm volatile ("STCK %1\n\t"
                      "IPM  %0\n\t"
                      "SRL  %0,28" : "=r" (cc), "=m" (init_timer_cc));
        switch (cc) {
        case 0: /* clock in set state: all is fine */
                break;
        case 1: /* clock in non-set state: FIXME */
                printk("time_init: TOD clock in non-set state\n");
                break;
        case 2: /* clock in error state: FIXME */
                printk("time_init: TOD clock in error state\n");
                break;
        case 3: /* clock in stopped or not-operational state: FIXME */
                printk("time_init: TOD clock stopped/non-operational\n");
                break;
        }
        init_100hz_timer();
        init_timer_cc = S390_lowcore.jiffy_timer_cc;
        init_timer_cc -= 0x8126d60e46000000LL -
                         (0x3c26700LL*1000000*4096);
        tod_to_timeval(init_timer_cc, &xtime);
}
