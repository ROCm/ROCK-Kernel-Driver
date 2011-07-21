#ifndef _XEN_ASM_TIME_H
#define _XEN_ASM_TIME_H

unsigned long xen_read_wallclock(void);
int xen_write_wallclock(unsigned long);

struct timespec;
#ifdef CONFIG_XEN_PRIVILEGED_GUEST
int xen_update_wallclock(const struct timespec *);
#else
static inline int xen_update_wallclock(const struct timespec *tv) {
	return -EPERM;
}
#endif

#endif /* _XEN_ASM_TIME_H */

#include_next <asm/time.h>
