/*
 * $Id: time.h,v 1.12 1999/08/27 04:21:23 cort Exp $
 * Common time prototypes and such for all ppc machines.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu) to merge
 * Paul Mackerras' version and mine for PReP and Pmac.
 */

#ifdef __KERNEL__
#include <linux/config.h>
#include <linux/mc146818rtc.h>

#include <asm/processor.h>

/* time.c */
extern unsigned tb_ticks_per_jiffy;
extern unsigned tb_to_us;
extern unsigned tb_last_stamp;

extern void to_tm(int tim, struct rtc_time * tm);
extern time_t last_rtc_update;

int via_calibrate_decr(void);

/* Accessor functions for the decrementer register. */
static __inline__ unsigned int get_dec(void)
{
#if defined(CONFIG_4xx)
	return (mfspr(SPRN_PIT));
#else
	return (mfspr(SPRN_DEC));
#endif
}

static __inline__ void set_dec(unsigned int val)
{
#if defined(CONFIG_4xx)
	mtspr(SPRN_PIT, val);
#else
#ifdef CONFIG_8xx_CPU6
	set_dec_cpu6(val);
#else
	mtspr(SPRN_DEC, val);
#endif
#endif
}

/* Accessor functions for the timebase (RTC on 601) registers. */
/* If one day CONFIG_POWER is added just define __USE_RTC as 1 */
#ifdef CONFIG_6xx
extern __inline__ int const __USE_RTC(void) {
	return (mfspr(SPRN_PVR)>>16) == 1;
}
#else
#define __USE_RTC() 0
#endif

extern __inline__ unsigned long get_tbl(void) {
	unsigned long tbl;
	asm volatile("mftb %0" : "=r" (tbl));
	return tbl;
}

extern __inline__ unsigned long get_rtcl(void) {
	unsigned long rtcl;
	asm volatile("mfrtcl %0" : "=r" (rtcl));
	return rtcl;
}

extern __inline__ unsigned get_native_tbl(void) {
	if (__USE_RTC())
		return get_rtcl();
	else
	  	return get_tbl();
}

/* On machines with RTC, this function can only be used safely
 * after the timestamp and for 1 second. It is only used by gettimeofday
 * however so it should not matter.
 */
extern __inline__ unsigned tb_ticks_since(unsigned tstamp) {
	if (__USE_RTC()) {
		int delta = get_rtcl() - tstamp;
		return delta<0 ? delta + 1000000000 : delta;
	} else {
        	return get_tbl() - tstamp;
	}
}

#if 0
extern __inline__ unsigned long get_bin_rtcl(void) {
      unsigned long rtcl, rtcu1, rtcu2;
      asm volatile("\
1:    mfrtcu  %0\n\
      mfrtcl  %1\n\
      mfrtcu  %2\n\
      cmpw    %0,%2\n\
      bne-    1b\n"
      : "=r" (rtcu1), "=r" (rtcl), "=r" (rtcu2)
      : : "cr0");
      return rtcu2*1000000000+rtcl;
}

extern __inline__ unsigned binary_tbl(void) {
      if (__USE_RTC())
              return get_bin_rtcl();
      else
              return get_tbl();
}
#endif

/* Use mulhwu to scale processor timebase to timeval */
#define mulhwu(x,y) \
({unsigned z; asm ("mulhwu %0,%1,%2" : "=r" (z) : "r" (x), "r" (y)); z;})

unsigned mulhwu_scale_factor(unsigned, unsigned);
#endif /* __KERNEL__ */
