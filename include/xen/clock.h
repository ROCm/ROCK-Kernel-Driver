#ifndef __XEN_CPU_CLOCK_H__
#define __XEN_CPU_CLOCK_H__

void setup_runstate_area(unsigned int cpu);

extern struct pvclock_vsyscall_time_info *pvclock_vsyscall_time;
void setup_vsyscall_time_area(unsigned int cpu);

unsigned long long xen_local_clock(void);
void xen_check_wallclock_update(void);

#ifdef CONFIG_GENERIC_CLOCKEVENTS
void xen_clockevents_init(void);
void xen_setup_cpu_clockevents(void);
void xen_clockevents_resume(bool late);
#else
static inline void xen_setup_cpu_clockevents(void) {}
static inline void xen_clockevents_resume(bool late) {}
#endif

#endif /* __XEN_CPU_CLOCK_H__ */
