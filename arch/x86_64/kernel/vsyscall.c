/*
 *  linux/arch/x86_64/kernel/vsyscall.c
 *
 *  Copyright (C) 2001 Andrea Arcangeli <andrea@suse.de> SuSE
 *
 *  Thanks to hpa@transmeta.com for some useful hint.
 *  Special thanks to Ingo Molnar for his early experience with
 *  a different vsyscall implementation for Linux/IA32 and for the name.
 *
 *  vsyscall 1 is located at -10Mbyte, vsyscall 2 is located
 *  at virtual address -10Mbyte+1024bytes etc... There are at max 8192
 *  vsyscalls. One vsyscall can reserve more than 1 slot to avoid
 *  jumping out of line if necessary.
 *
 *  $Id: vsyscall.c,v 1.4 2001/09/27 17:58:13 ak Exp $
 */

/*
 * TODO 2001-03-20:
 *
 * 1) make page fault handler detect faults on page1-page-last of the vsyscall
 *    virtual space, and make it increase %rip and write -ENOSYS in %rax (so
 *    we'll be able to upgrade to a new glibc without upgrading kernel after
 *    we add more vsyscalls.
 * 2) Possibly we need a fixmap table for the vsyscalls too if we want
 *    to avoid SIGSEGV and we want to return -EFAULT from the vsyscalls as well.
 *    Can we segfault inside a "syscall"? We can fix this anytime and those fixes
 *    won't be visible for userspace. Not fixing this is a noop for correct programs,
 *    broken programs will segfault and there's no security risk until we choose to
 *    fix it.
 *
 * These are not urgent things that we need to address only before shipping the first
 * production binary kernels.
 */

#include <linux/time.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/timer.h>

#include <asm/vsyscall.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/fixmap.h>
#include <asm/errno.h>

#define __vsyscall(nr) __attribute__ ((unused,__section__(".vsyscall_" #nr)))

static inline void timeval_normalize(struct timeval * tv)
{
	time_t __sec;

	__sec = tv->tv_usec / 1000000;
	if (__sec)
	{
		tv->tv_usec %= 1000000;
		tv->tv_sec += __sec;
	}
}

long __vxtime_sequence[2] __section_vxtime_sequence;

inline void do_vgettimeofday(struct timeval * tv)
{
	long sequence;
	unsigned long usec, sec;

	do {
		unsigned long eax, edx;

		sequence = __vxtime_sequence[1];
		rmb();
		
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

		edx = (eax*__fast_gettimeoffset_quotient) >> 32;

		/* our adjusted time offset in microseconds */
		usec = __delay_at_last_interrupt + edx;

		{
			unsigned long lost = __jiffies - __wall_jiffies;
			if (lost)
				usec += lost * (1000000 / HZ);
		}
		sec = __xtime.tv_sec;
		usec += __xtime.tv_usec;

		rmb();
	} while (sequence != __vxtime_sequence[0]);

	tv->tv_sec = sec;
	tv->tv_usec = usec;
	timeval_normalize(tv);
}

static inline void do_get_tz(struct timezone * tz)
{
	long sequence;

	do {
		sequence = __vxtime_sequence[1];
		rmb();

		*tz = __sys_tz;

		rmb();
	} while (sequence != __vxtime_sequence[0]);
}

static int __vsyscall(0) vgettimeofday(struct timeval * tv, struct timezone * tz)
{
	if (tv)
		do_vgettimeofday(tv);
	if (tz)
		do_get_tz(tz);
	return 0;
}

static time_t __vsyscall(1) vtime(time_t * time)
{
	long sequence;
	time_t __time;

	do {
		sequence = __vxtime_sequence[1];
		rmb();

		__time = __xtime.tv_sec;

		rmb();
	} while (sequence != __vxtime_sequence[0]);

	if (time)
		*time = __time;
	return __time;
}

static long __vsyscall(2) venosys_0(void)
{
	return -ENOSYS;
}

static long __vsyscall(3) venosys_1(void)
{
	return -ENOSYS;
}

static void __init map_vsyscall(void)
{
	extern char __vsyscall_0;
	unsigned long physaddr_page0 = (unsigned long) &__vsyscall_0 - __START_KERNEL_map;

	__set_fixmap(VSYSCALL_FIRST_PAGE, physaddr_page0, PAGE_KERNEL_VSYSCALL);
}

static int __init vsyscall_init(void)
{
	printk("VSYSCALL: consistency checks...");
	if ((unsigned long) &vgettimeofday != VSYSCALL_ADDR(__NR_vgettimeofday))
		panic("vgettimeofday link addr broken");
	if ((unsigned long) &vtime != VSYSCALL_ADDR(__NR_vtime))
		panic("vtime link addr broken");
	if (VSYSCALL_ADDR(0) != __fix_to_virt(VSYSCALL_FIRST_PAGE))
		panic("fixmap first vsyscall %lx should be %lx", __fix_to_virt(VSYSCALL_FIRST_PAGE),
		      VSYSCALL_ADDR(0));
	printk("passed...mapping...");
	map_vsyscall();
	printk("done.\n");

	return 0;
}

__initcall(vsyscall_init);
