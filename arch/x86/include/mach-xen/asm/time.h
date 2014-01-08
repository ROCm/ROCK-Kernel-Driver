#ifndef _XEN_ASM_TIME_H
#define _XEN_ASM_TIME_H

struct timespec;
void xen_read_wallclock(struct timespec *);
int xen_write_wallclock(const struct timespec *);

#ifdef CONFIG_XEN_PRIVILEGED_GUEST
int xen_update_wallclock(const struct timespec *);
#else
static inline int xen_update_wallclock(const struct timespec *tv) {
	return -EPERM;
}
#endif

#endif /* _XEN_ASM_TIME_H */

#include_next <asm/time.h>
