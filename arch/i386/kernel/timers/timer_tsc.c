/*
 * This code largely moved from arch/i386/kernel/time.c.
 * See comments there for proper credits.
 */

#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/timex.h>
#include <linux/errno.h>
#include <linux/cpufreq.h>
#include <linux/string.h>

#include <asm/timer.h>
#include <asm/io.h>
/* processor.h for distable_tsc flag */
#include <asm/processor.h>

#include "io_ports.h"
#include "mach_timer.h"

int tsc_disable __initdata = 0;

extern spinlock_t i8253_lock;
extern unsigned long jiffies;

static int use_tsc;
/* Number of usecs that the last interrupt was delayed */
static int delay_at_last_interrupt;

static unsigned long last_tsc_low; /* lsb 32 bits of Time Stamp Counter */
static unsigned long last_tsc_high; /* msb 32 bits of Time Stamp Counter */
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


static int count2; /* counter for mark_offset_tsc() */

/* Cached *multiplier* to convert TSC counts to microseconds.
 * (see the equation below).
 * Equal to 2^32 * (1 / (clocks per usec) ).
 * Initialized in time_init.
 */
static unsigned long fast_gettimeoffset_quotient;

static unsigned long get_offset_tsc(void)
{
	register unsigned long eax, edx;

	/* Read the Time Stamp Counter */

	rdtsc(eax,edx);

	/* .. relative to previous jiffy (32 bits is enough) */
	eax -= last_tsc_low;	/* tsc_low delta */

	/*
         * Time offset = (tsc_low delta) * fast_gettimeoffset_quotient
         *             = (tsc_low delta) * (usecs_per_clock)
         *             = (tsc_low delta) * (usecs_per_jiffy / clocks_per_jiffy)
	 *
	 * Using a mull instead of a divl saves up to 31 clock cycles
	 * in the critical path.
         */

	__asm__("mull %2"
		:"=a" (eax), "=d" (edx)
		:"rm" (fast_gettimeoffset_quotient),
		 "0" (eax));

	/* our adjusted time offset in microseconds */
	return delay_at_last_interrupt + edx;
}

static unsigned long long monotonic_clock_tsc(void)
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

static void mark_offset_tsc(void)
{
	unsigned long lost,delay;
	unsigned long delta = last_tsc_low;
	int count;
	int countmp;
	static int count1 = 0;
	unsigned long long this_offset, last_offset;
	
	write_lock(&monotonic_lock);
	last_offset = ((unsigned long long)last_tsc_high<<32)|last_tsc_low;
	/*
	 * It is important that these two operations happen almost at
	 * the same time. We do the RDTSC stuff first, since it's
	 * faster. To avoid any inconsistencies, we need interrupts
	 * disabled locally.
	 */

	/*
	 * Interrupts are just disabled locally since the timer irq
	 * has the SA_INTERRUPT flag set. -arca
	 */
	
	/* read Pentium cycle counter */

	rdtsc(last_tsc_low, last_tsc_high);

	spin_lock(&i8253_lock);
	outb_p(0x00, PIT_MODE);     /* latch the count ASAP */

	count = inb_p(PIT_CH0);    /* read the latched count */
	count |= inb(PIT_CH0) << 8;
	spin_unlock(&i8253_lock);

	if (pit_latch_buggy) {
		/* get center value of last 3 time lutch */
		if ((count2 >= count && count >= count1)
		    || (count1 >= count && count >= count2)) {
			count2 = count1; count1 = count;
		} else if ((count1 >= count2 && count2 >= count)
			   || (count >= count2 && count2 >= count1)) {
			countmp = count;count = count2;
			count2 = count1;count1 = countmp;
		} else {
			count2 = count1; count1 = count; count = count1;
		}
	}

	/* lost tick compensation */
	delta = last_tsc_low - delta;
	{
		register unsigned long eax, edx;
		eax = delta;
		__asm__("mull %2"
		:"=a" (eax), "=d" (edx)
		:"rm" (fast_gettimeoffset_quotient),
		 "0" (eax));
		delta = edx;
	}
	delta += delay_at_last_interrupt;
	lost = delta/(1000000/HZ);
	delay = delta%(1000000/HZ);
	if (lost >= 2)
		jiffies += lost-1;

	/* update the monotonic base value */
	this_offset = ((unsigned long long)last_tsc_high<<32)|last_tsc_low;
	monotonic_base += cycles_2_ns(this_offset - last_offset);
	write_unlock(&monotonic_lock);

	/* calculate delay_at_last_interrupt */
	count = ((LATCH-1) - count) * TICK_SIZE;
	delay_at_last_interrupt = (count + LATCH/2) / LATCH;

	/* catch corner case where tick rollover occured 
	 * between tsc and pit reads (as noted when 
	 * usec delta is > 90% # of usecs/tick)
	 */
	if (abs(delay - delay_at_last_interrupt) > (900000/HZ))
		jiffies++;
}

static void delay_tsc(unsigned long loops)
{
	unsigned long bclock, now;
	
	rdtscl(bclock);
	do
	{
		rep_nop();
		rdtscl(now);
	} while ((now-bclock) < loops);
}

/* ------ Calibrate the TSC ------- 
 * Return 2^32 * (1 / (TSC clocks per usec)) for do_fast_gettimeoffset().
 * Too much 64-bit arithmetic here to do this cleanly in C, and for
 * accuracy's sake we want to keep the overhead on the CTC speaker (channel 2)
 * output busy loop as low as possible. We avoid reading the CTC registers
 * directly because of the awkward 8-bit access mechanism of the 82C54
 * device.
 */

#define CALIBRATE_TIME	(5 * 1000020/HZ)

unsigned long __init calibrate_tsc(void)
{
	mach_prepare_counter();

	{
		unsigned long startlow, starthigh;
		unsigned long endlow, endhigh;
		unsigned long count;

		rdtsc(startlow,starthigh);
		mach_countup(&count);
		rdtsc(endlow,endhigh);

		last_tsc_low = endlow;

		/* Error: ECTCNEVERSET */
		if (count <= 1)
			goto bad_ctc;

		/* 64-bit subtract - gcc just messes up with long longs */
		__asm__("subl %2,%0\n\t"
			"sbbl %3,%1"
			:"=a" (endlow), "=d" (endhigh)
			:"g" (startlow), "g" (starthigh),
			 "0" (endlow), "1" (endhigh));

		/* Error: ECPUTOOFAST */
		if (endhigh)
			goto bad_ctc;

		/* Error: ECPUTOOSLOW */
		if (endlow <= CALIBRATE_TIME)
			goto bad_ctc;

		__asm__("divl %2"
			:"=a" (endlow), "=d" (endhigh)
			:"r" (endlow), "0" (0), "1" (CALIBRATE_TIME));

		return endlow;
	}

	/*
	 * The CTC wasn't reliable: we got a hit on the very first read,
	 * or the CPU was so fast/slow that the quotient wouldn't fit in
	 * 32 bits..
	 */
bad_ctc:
	return 0;
}


#ifdef CONFIG_CPU_FREQ
static unsigned int  ref_freq = 0;
static unsigned long loops_per_jiffy_ref = 0;

#ifndef CONFIG_SMP
static unsigned long fast_gettimeoffset_ref = 0;
static unsigned long cpu_khz_ref = 0;
#endif

static int
time_cpufreq_notifier(struct notifier_block *nb, unsigned long val,
		       void *data)
{
	struct cpufreq_freqs *freq = data;

	write_seqlock(&xtime_lock);
	if (!ref_freq) {
		ref_freq = freq->old;
		loops_per_jiffy_ref = cpu_data[freq->cpu].loops_per_jiffy;
#ifndef CONFIG_SMP
		fast_gettimeoffset_ref = fast_gettimeoffset_quotient;
		cpu_khz_ref = cpu_khz;
#endif
	}

	if ((val == CPUFREQ_PRECHANGE  && freq->old < freq->new) ||
	    (val == CPUFREQ_POSTCHANGE && freq->old > freq->new)) {
		cpu_data[freq->cpu].loops_per_jiffy = cpufreq_scale(loops_per_jiffy_ref, ref_freq, freq->new);
#ifndef CONFIG_SMP
		if (use_tsc) {
			fast_gettimeoffset_quotient = cpufreq_scale(fast_gettimeoffset_ref, freq->new, ref_freq);
			cpu_khz = cpufreq_scale(cpu_khz_ref, ref_freq, freq->new);
		}
#endif
	}
	write_sequnlock(&xtime_lock);

	return 0;
}

static struct notifier_block time_cpufreq_notifier_block = {
	.notifier_call	= time_cpufreq_notifier
};
#endif


static int __init init_tsc(char* override)
{

	/* check clock override */
	if (override[0] && strncmp(override,"tsc",3))
			return -ENODEV;

	/*
	 * If we have APM enabled or the CPU clock speed is variable
	 * (CPU stops clock on HLT or slows clock to save power)
	 * then the TSC timestamps may diverge by up to 1 jiffy from
	 * 'real time' but nothing will break.
	 * The most frequent case is that the CPU is "woken" from a halt
	 * state by the timer interrupt itself, so we get 0 error. In the
	 * rare cases where a driver would "wake" the CPU and request a
	 * timestamp, the maximum error is < 1 jiffy. But timestamps are
	 * still perfectly ordered.
	 * Note that the TSC counter will be reset if APM suspends
	 * to disk; this won't break the kernel, though, 'cuz we're
	 * smart.  See arch/i386/kernel/apm.c.
	 */
 	/*
 	 *	Firstly we have to do a CPU check for chips with
 	 * 	a potentially buggy TSC. At this point we haven't run
 	 *	the ident/bugs checks so we must run this hook as it
 	 *	may turn off the TSC flag.
 	 *
 	 *	NOTE: this doesn't yet handle SMP 486 machines where only
 	 *	some CPU's have a TSC. Thats never worked and nobody has
 	 *	moaned if you have the only one in the world - you fix it!
 	 */
 
#ifdef CONFIG_CPU_FREQ
	cpufreq_register_notifier(&time_cpufreq_notifier_block, CPUFREQ_TRANSITION_NOTIFIER);
#endif

	count2 = LATCH; /* initialize counter for mark_offset_tsc() */

	if (cpu_has_tsc) {
		unsigned long tsc_quotient = calibrate_tsc();
		if (tsc_quotient) {
			fast_gettimeoffset_quotient = tsc_quotient;
			use_tsc = 1;
			/*
			 *	We could be more selective here I suspect
			 *	and just enable this for the next intel chips ?
			 */
			/* report CPU clock rate in Hz.
			 * The formula is (10^6 * 2^32) / (2^32 * 1 / (clocks/us)) =
			 * clock/second. Our precision is about 100 ppm.
			 */
			{	unsigned long eax=0, edx=1000;
				__asm__("divl %2"
		       		:"=a" (cpu_khz), "=d" (edx)
        	       		:"r" (tsc_quotient),
	                	"0" (eax), "1" (edx));
				printk("Detected %lu.%03lu MHz processor.\n", cpu_khz / 1000, cpu_khz % 1000);
			}
			set_cyc2ns_scale(cpu_khz/1000);
			return 0;
		}
	}
	return -ENODEV;
}

#ifndef CONFIG_X86_TSC
/* disable flag for tsc.  Takes effect by clearing the TSC cpu flag
 * in cpu/common.c */
static int __init tsc_setup(char *str)
{
	tsc_disable = 1;
	return 1;
}
#else
static int __init tsc_setup(char *str)
{
	printk(KERN_WARNING "notsc: Kernel compiled with CONFIG_X86_TSC, "
				"cannot disable TSC.\n");
	return 1;
}
#endif
__setup("notsc", tsc_setup);



/************************************************************/

/* tsc timer_opts struct */
struct timer_opts timer_tsc = {
	.init =		init_tsc,
	.mark_offset =	mark_offset_tsc, 
	.get_offset =	get_offset_tsc,
	.monotonic_clock =	monotonic_clock_tsc,
	.delay = delay_tsc,
};
