#ifndef _ASM_RTC_H
#define _ASM_RTC_H

#include <asm/machvec.h>

#define rtc_gettimeofday sh_mv.mv_rtc_gettimeofday
#define rtc_settimeofday sh_mv.mv_rtc_settimeofday

extern void sh_rtc_gettimeofday(struct timeval *tv);
extern int sh_rtc_settimeofday(const struct timeval *tv);

#endif /* _ASM_RTC_H */
