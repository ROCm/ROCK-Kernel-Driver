/*
 * Machine dependent access functions for RTC registers.
 */
#ifndef _ASM_MC146818RTC_H
#define _ASM_MC146818RTC_H

#include <linux/config.h>
#include <asm/atarihw.h>

#ifdef CONFIG_ATARI
/* RTC in Atari machines */

#include <asm/atarihw.h>
#include <asm/atariints.h>
#include <asm/io.h>
#define RTC_HAS_IRQ	(ATARIHW_PRESENT(TT_MFP))
#define RTC_IRQ 	IRQ_TT_MFP_RTC
#define RTC_IRQ_FLAGS	IRQ_TYPE_FAST
#define RTC_PORT(x)	(TT_RTC_BAS + 2*(x))
#define RTC_ALWAYS_BCD	0	/* TOS uses binary mode, Linux should be able
				 * to deal with both modes */

#define RTC_CHECK_DRIVER_INIT() (MACH_IS_ATARI && ATARIHW_PRESENT(TT_CLK))
#define RTC_MACH_INIT()							\
    do {								\
	epoch = atari_rtc_year_offset + 1900;				\
	if (RTC_HAS_IRQ)						\
	    /* select RTC int on H->L edge */				\
	    tt_mfp.active_edge &= ~0x40;				\
    } while(0)
#define RTC_MACH_EXIT()

/* On Atari, the year was stored with base 1970 in old TOS versions (before
 * 3.06). Later, Atari recognized that this broke leap year recognition, and
 * changed the base to 1968. Medusa and Hades always use the new version. */
#define RTC_CENTURY_SWITCH	-1	/* no century switch */
#define RTC_MINYEAR		epoch

#define CMOS_READ(addr) ({ \
atari_outb_p((addr),RTC_PORT(0)); \
atari_inb_p(RTC_PORT(1)); \
})
#define CMOS_WRITE(val, addr) ({ \
atari_outb_p((addr),RTC_PORT(0)); \
atari_outb_p((val),RTC_PORT(1)); \
})
#endif /* CONFIG_ATARI */

#endif /* _ASM_MC146818RTC_H */
