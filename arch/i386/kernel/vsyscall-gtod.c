/*
 *  linux/arch/i386/kernel/vsyscall-gtod.c
 *
 *  Copyright (C) 2001 Andrea Arcangeli <andrea@suse.de> SuSE
 *  Copyright (C) 2003,2004 John Stultz <johnstul@us.ibm.com> IBM
 *
 *  Thanks to hpa@transmeta.com for some useful hint.
 *  Special thanks to Ingo Molnar for his early experience with
 *  a different vsyscall implementation for Linux/IA32 and for the name.
 *
 *  vsyscall 0 is located at VSYSCALL_START, vsyscall 1 is located
 *  at virtual address VSYSCALL_START+1024bytes etc...
 *
 *  Originally written for x86-64 by Andrea Arcangeli <andrea@suse.de>
 *  Ported to i386 by John Stultz <johnstul@us.ibm.com>
 */


#include <linux/time.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/sched.h>

#include <asm/vsyscall-gtod.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/fixmap.h>
#include <asm/msr.h>
#include <asm/timer.h>
#include <asm/system.h>
#include <asm/unistd.h>
#include <asm/errno.h>

int errno;
static inline _syscall2(int,gettimeofday,struct timeval *,tv,struct timezone *,tz);
static int vsyscall_mapped = 0; /* flag variable for remap_vsyscall() */

enum vsyscall_timesource_e vsyscall_timesource;
enum vsyscall_timesource_e __vsyscall_timesource __section_vsyscall_timesource;

/* readonly clones of generic time values */
seqlock_t  __xtime_lock __section_xtime_lock  = SEQLOCK_UNLOCKED;
struct timespec __xtime __section_xtime;
volatile unsigned long __jiffies __section_jiffies;
unsigned long __wall_jiffies __section_wall_jiffies;
struct timezone __sys_tz __section_sys_tz;
/* readonly clones of ntp time variables */
int __tickadj __section_tickadj;
long __time_adjust __section_time_adjust;

/* readonly clones of TSC timesource values*/
unsigned long __last_tsc_low __section_last_tsc_low;
int __tsc_delay_at_last_interrupt __section_tsc_delay_at_last_interrupt;
unsigned long __fast_gettimeoffset_quotient __section_fast_gettimeoffset_quotient;

/* readonly clones of cyclone timesource values*/
u32* __cyclone_timer __section_cyclone_timer;	/* Cyclone MPMC0 register */
u32 __last_cyclone_low __section_last_cyclone_low;
int __cyclone_delay_at_last_interrupt __section_cyclone_delay_at_last_interrupt;


static inline unsigned long vgettimeoffset_tsc(void)
{
	unsigned long eax, edx;

	/* Read the Time Stamp Counter */
	rdtsc(eax,edx);

	/* .. relative to previous jiffy (32 bits is enough) */
	eax -= __last_tsc_low;	/* tsc_low delta */

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
		:"rm" (__fast_gettimeoffset_quotient),
		 "0" (eax));

	/* our adjusted time offset in microseconds */
	return __tsc_delay_at_last_interrupt + edx;

}

static inline unsigned long vgettimeoffset_cyclone(void)
{
	u32 offset;

	if (!__cyclone_timer)
		return 0;

	/* Read the cyclone timer */
	offset = __cyclone_timer[0];

	/* .. relative to previous jiffy */
	offset = offset - __last_cyclone_low;

	/* convert cyclone ticks to microseconds */
	offset = offset/(CYCLONE_TIMER_FREQ/1000000);

	/* our adjusted time offset in microseconds */
	return __cyclone_delay_at_last_interrupt + offset;
}

static inline void do_vgettimeofday(struct timeval * tv)
{
	long sequence;
	unsigned long usec, sec;
	unsigned long lost;
	unsigned long max_ntp_tick;

	/* If we don't have a valid vsyscall time source,
	 * just call gettimeofday()
	 */
	if (__vsyscall_timesource == VSYSCALL_GTOD_NONE) {
		gettimeofday(tv, NULL);
		return;
	}


	do {
		sequence = read_seqbegin(&__xtime_lock);

		/* Get the high-res offset */
		if (__vsyscall_timesource == VSYSCALL_GTOD_CYCLONE)
			usec = vgettimeoffset_cyclone();
		else
			usec = vgettimeoffset_tsc();

		lost = __jiffies - __wall_jiffies;

		/*
		 * If time_adjust is negative then NTP is slowing the clock
		 * so make sure not to go into next possible interval.
		 * Better to lose some accuracy than have time go backwards..
		 */
		if (unlikely(__time_adjust < 0)) {
			max_ntp_tick = (USEC_PER_SEC / HZ) - __tickadj;
			usec = min(usec, max_ntp_tick);

			if (lost)
				usec += lost * max_ntp_tick;
		}
		else if (unlikely(lost))
			usec += lost * (USEC_PER_SEC / HZ);

		sec = __xtime.tv_sec;
		usec += (__xtime.tv_nsec / 1000);

	} while (read_seqretry(&__xtime_lock, sequence));

	tv->tv_sec = sec + usec / 1000000;
	tv->tv_usec = usec % 1000000;
}

static inline void do_get_tz(struct timezone * tz)
{
	long sequence;

	do {
		sequence = read_seqbegin(&__xtime_lock);

		*tz = __sys_tz;

	} while (read_seqretry(&__xtime_lock, sequence));
}

static int __vsyscall(0) asmlinkage vgettimeofday(struct timeval * tv, struct timezone * tz)
{
	if (tv)
		do_vgettimeofday(tv);
	if (tz)
		do_get_tz(tz);
	return 0;
}

static time_t __vsyscall(1) asmlinkage vtime(time_t * t)
{
	struct timeval tv;
	vgettimeofday(&tv,NULL);
	if (t)
		*t = tv.tv_sec;
	return tv.tv_sec;
}

static long __vsyscall(2) asmlinkage venosys_0(void)
{
	return -ENOSYS;
}

static long __vsyscall(3) asmlinkage venosys_1(void)
{
	return -ENOSYS;
}


void vsyscall_set_timesource(char* name)
{
	if (!strncmp(name, "tsc", 3))
		vsyscall_timesource = VSYSCALL_GTOD_TSC;
	else if (!strncmp(name, "cyclone", 7))
		vsyscall_timesource = VSYSCALL_GTOD_CYCLONE;
	else
		vsyscall_timesource = VSYSCALL_GTOD_NONE;
}


static void __init map_vsyscall(void)
{
	unsigned long physaddr_page0 = (unsigned long) &__vsyscall_0 - PAGE_OFFSET;

	/* Initially we map the VSYSCALL page w/ PAGE_KERNEL permissions to
	 * keep the alternate_instruction code from bombing out when it
	 * changes the seq_lock memory barriers in vgettimeofday()
	 */
	__set_fixmap(FIX_VSYSCALL_GTOD_FIRST_PAGE, physaddr_page0, PAGE_KERNEL);
}

static int __init remap_vsyscall(void)
{
	unsigned long physaddr_page0 = (unsigned long) &__vsyscall_0 - PAGE_OFFSET;

	if (!vsyscall_mapped)
		return 0;

	/* Remap the VSYSCALL page w/ PAGE_KERNEL_VSYSCALL permissions
	 * after the alternate_instruction code has run
	 */
	clear_fixmap(FIX_VSYSCALL_GTOD_FIRST_PAGE);
	__set_fixmap(FIX_VSYSCALL_GTOD_FIRST_PAGE, physaddr_page0, PAGE_KERNEL_VSYSCALL);

	return 0;
}

int __init vsyscall_init(void)
{
	printk("VSYSCALL: consistency checks...");
	if ((unsigned long) &vgettimeofday != VSYSCALL_ADDR(__NR_vgettimeofday)) {
		printk("vgettimeofday link addr broken\n");
		printk("VSYSCALL: vsyscall_init failed!\n");
		return -EFAULT;
	}
	if ((unsigned long) &vtime != VSYSCALL_ADDR(__NR_vtime)) {
		printk("vtime link addr broken\n");
		printk("VSYSCALL: vsyscall_init failed!\n");
		return -EFAULT;
	}
	if (VSYSCALL_ADDR(0) != __fix_to_virt(FIX_VSYSCALL_GTOD_FIRST_PAGE)) {
		printk("fixmap first vsyscall 0x%lx should be 0x%x\n",
			__fix_to_virt(FIX_VSYSCALL_GTOD_FIRST_PAGE),
			VSYSCALL_ADDR(0));
		printk("VSYSCALL: vsyscall_init failed!\n");
		return -EFAULT;
	}


	printk("passed...mapping...");
	map_vsyscall();
	printk("done.\n");
	vsyscall_mapped = 1;
	printk("VSYSCALL: fixmap virt addr: 0x%lx\n",
		__fix_to_virt(FIX_VSYSCALL_GTOD_FIRST_PAGE));

	return 0;
}

__initcall(remap_vsyscall);
