/*
 * This code largely moved from arch/i386/kernel/time.c.
 * See comments there for proper credits.
 */

#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/timex.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/jiffies.h>

#include <asm/timer.h>
#include <asm/io.h>
#include <asm/processor.h>

#include "io_ports.h"
#include "mach_timer.h"
#include <asm/hpet.h>

static unsigned long hpet_usec_quotient;	/* convert hpet clks to usec */
static unsigned long tsc_hpet_quotient;		/* convert tsc to hpet clks */
static unsigned long hpet_last; 	/* hpet counter value at last tick*/
static unsigned long last_tsc_low;	/* lsb 32 bits of Time Stamp Counter */
static unsigned long last_tsc_high; 	/* msb 32 bits of Time Stamp Counter */
static unsigned long long monotonic_base;
static rwlock_t monotonic_lock = RW_LOCK_UNLOCKED;

/* convert from cycles(64bits) => nanoseconds (64bits)
 *  basic equation:
 *		ns = cycles / (freq / ns_per_sec)
 *		ns = cycles * (ns_per_sec / freq)
 *		ns = cycles * (10^9 / (cpu_mhz * 10^6))
 *		ns = cycles * (10^3 / cpu_mhz)
 *
 *	Then we use scaling math (suggested by george@mvista.com) to get:
 *		ns = cycles * (10^3 * SC / cpu_mhz) / SC
 *		ns = cycles * cyc2ns_scale / SC
 *
 *	And since SC is a constant power of two, we can convert the div
 *  into a shift.
 *			-johnstul@us.ibm.com "math is hard, lets go shopping!"
 */
static unsigned long cyc2ns_scale;
#define CYC2NS_SCALE_FACTOR 10 /* 2^10, carefully chosen */

static inline void set_cyc2ns_scale(unsigned long cpu_mhz)
{
	cyc2ns_scale = (1000 << CYC2NS_SCALE_FACTOR)/cpu_mhz;
}

static inline unsigned long long cycles_2_ns(unsigned long long cyc)
{
	return (cyc * cyc2ns_scale) >> CYC2NS_SCALE_FACTOR;
}

static unsigned long long monotonic_clock_hpet(void)
{
	unsigned long long last_offset, this_offset, base;

	/* atomically read monotonic base & last_offset */
	read_lock_irq(&monotonic_lock);
	last_offset = ((unsigned long long)last_tsc_high<<32)|last_tsc_low;
	base = monotonic_base;
	read_unlock_irq(&monotonic_lock);

	/* Read the Time Stamp Counter */
	rdtscll(this_offset);

	/* return the value in ns */
	return base + cycles_2_ns(this_offset - last_offset);
}

static unsigned long get_offset_hpet(void)
{
	register unsigned long eax, edx;

	eax = hpet_readl(HPET_COUNTER);
	eax -= hpet_last;	/* hpet delta */

	/*
         * Time offset = (hpet delta) * ( usecs per HPET clock )
	 *             = (hpet delta) * ( usecs per tick / HPET clocks per tick)
	 *             = (hpet delta) * ( hpet_usec_quotient ) / (2^32)
	 *
	 * Where,
	 * hpet_usec_quotient = (2^32 * usecs per tick)/HPET clocks per tick
	 *
	 * Using a mull instead of a divl saves some cycles in critical path.
         */
	ASM_MUL64_REG(eax, edx, hpet_usec_quotient, eax);

	/* our adjusted time offset in microseconds */
	return edx;
}

static void mark_offset_hpet(void)
{
	unsigned long long this_offset, last_offset;
	unsigned long offset;

	write_lock(&monotonic_lock);
	last_offset = ((unsigned long long)last_tsc_high<<32)|last_tsc_low;
	rdtsc(last_tsc_low, last_tsc_high);

	offset = hpet_readl(HPET_T0_CMP) - hpet_tick;
	if (unlikely(((offset - hpet_last) > hpet_tick) && (hpet_last != 0))) {
		int lost_ticks = (offset - hpet_last) / hpet_tick;
		jiffies += lost_ticks;
	}
	hpet_last = offset;

	/* update the monotonic base value */
	this_offset = ((unsigned long long)last_tsc_high<<32)|last_tsc_low;
	monotonic_base += cycles_2_ns(this_offset - last_offset);
	write_unlock(&monotonic_lock);
}

void delay_hpet(unsigned long loops)
{
	unsigned long hpet_start, hpet_end;
	unsigned long eax;

	/* loops is the number of cpu cycles. Convert it to hpet clocks */
	ASM_MUL64_REG(eax, loops, tsc_hpet_quotient, loops);

	hpet_start = hpet_readl(HPET_COUNTER);
	do {
		rep_nop();
		hpet_end = hpet_readl(HPET_COUNTER);
	} while ((hpet_end - hpet_start) < (loops));
}

/* ------ Calibrate the TSC -------
 * Return 2^32 * (1 / (TSC clocks per usec)) for getting the CPU freq.
 * Set 2^32 * (1 / (tsc per HPET clk)) for delay_hpet().
 * calibrate_tsc() calibrates the processor TSC by comparing
 * it to the HPET timer of known frequency.
 * Too much 64-bit arithmetic here to do this cleanly in C
 */
#define CALIBRATE_CNT_HPET 	(5 * hpet_tick)
#define CALIBRATE_TIME_HPET 	(5 * KERNEL_TICK_USEC)

static unsigned long __init calibrate_tsc(void)
{
	unsigned long tsc_startlow, tsc_starthigh;
	unsigned long tsc_endlow, tsc_endhigh;
	unsigned long hpet_start, hpet_end;
	unsigned long result, remain;

	hpet_start = hpet_readl(HPET_COUNTER);
	rdtsc(tsc_startlow, tsc_starthigh);
	do {
		hpet_end = hpet_readl(HPET_COUNTER);
	} while ((hpet_end - hpet_start) < CALIBRATE_CNT_HPET);
	rdtsc(tsc_endlow, tsc_endhigh);

	/* 64-bit subtract - gcc just messes up with long longs */
	__asm__("subl %2,%0\n\t"
		"sbbl %3,%1"
		:"=a" (tsc_endlow), "=d" (tsc_endhigh)
		:"g" (tsc_startlow), "g" (tsc_starthigh),
		 "0" (tsc_endlow), "1" (tsc_endhigh));

	/* Error: ECPUTOOFAST */
	if (tsc_endhigh)
		goto bad_calibration;

	/* Error: ECPUTOOSLOW */
	if (tsc_endlow <= CALIBRATE_TIME_HPET)
		goto bad_calibration;

	ASM_DIV64_REG(result, remain, tsc_endlow, 0, CALIBRATE_TIME_HPET);
	if (remain > (tsc_endlow >> 1))
		result++; /* rounding the result */

	ASM_DIV64_REG(tsc_hpet_quotient, remain, tsc_endlow, 0,
			CALIBRATE_CNT_HPET);
	if (remain > (tsc_endlow >> 1))
		tsc_hpet_quotient++; /* rounding the result */

	return result;
bad_calibration:
	/*
	 * the CPU was so fast/slow that the quotient wouldn't fit in
	 * 32 bits..
	 */
	return 0;
}

static int __init init_hpet(char* override)
{
	unsigned long result, remain;

	/* check clock override */
	if (override[0] && strncmp(override,"hpet",4))
		return -ENODEV;

	if (!is_hpet_enabled())
		return -ENODEV;

	printk("Using HPET for gettimeofday\n");
	if (cpu_has_tsc) {
		unsigned long tsc_quotient = calibrate_tsc();
		if (tsc_quotient) {
			/* report CPU clock rate in Hz.
			 * The formula is (10^6 * 2^32) / (2^32 * 1 / (clocks/us)) =
			 * clock/second. Our precision is about 100 ppm.
			 */
			{	unsigned long eax=0, edx=1000;
				ASM_DIV64_REG(cpu_khz, edx, tsc_quotient,
						eax, edx);
				printk("Detected %lu.%03lu MHz processor.\n",
					cpu_khz / 1000, cpu_khz % 1000);
			}
			set_cyc2ns_scale(cpu_khz/1000);
		}
	}

	/*
	 * Math to calculate hpet to usec multiplier
	 * Look for the comments at get_offset_hpet()
	 */
	ASM_DIV64_REG(result, remain, hpet_tick, 0, KERNEL_TICK_USEC);
	if (remain > (hpet_tick >> 1))
		result++; /* rounding the result */
	hpet_usec_quotient = result;

	return 0;
}

/************************************************************/

/* tsc timer_opts struct */
struct timer_opts timer_hpet = {
	.init =			init_hpet,
	.mark_offset =		mark_offset_hpet,
	.get_offset =		get_offset_hpet,
	.monotonic_clock =	monotonic_clock_hpet,
	.delay = 		delay_hpet,
};
