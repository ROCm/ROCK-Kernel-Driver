#ifndef _ASM_X86_TIME_H
#define _ASM_X86_TIME_H

extern void hpet_time_init(void);

#include <asm/mc146818rtc.h>

extern void time_init(void);

#ifdef CONFIG_XEN
struct timespec;
extern int xen_independent_wallclock(void);
extern void xen_read_persistent_clock(struct timespec *);
extern int xen_update_persistent_clock(void);
#endif

#endif /* _ASM_X86_TIME_H */
