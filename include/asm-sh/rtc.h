#ifndef _ASM_RTC_H
#define _ASM_RTC_H

#include <asm/machvec.h>
#include <asm/cpu/rtc.h>

#define rtc_gettimeofday sh_mv.mv_rtc_gettimeofday
#define rtc_settimeofday sh_mv.mv_rtc_settimeofday

extern void sh_rtc_gettimeofday(struct timespec *ts);
extern int sh_rtc_settimeofday(const time_t secs);

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

#endif /* _ASM_RTC_H */

