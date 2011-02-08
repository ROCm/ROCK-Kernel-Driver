#ifndef __XEN_CPU_CLOCK_H__
#define __XEN_CPU_CLOCK_H__

struct vcpu_runstate_info *setup_runstate_area(unsigned int cpu);
void get_runstate_snapshot(struct vcpu_runstate_info *);

unsigned long long xen_local_clock(void);
void xen_check_wallclock_update(void);

#ifdef CONFIG_GENERIC_CLOCKEVENTS
void xen_clockevents_init(void);
void xen_setup_cpu_clockevents(void);
void xen_clockevents_resume(void);
#else
static inline void xen_setup_cpu_clockevents(void) {}
static inline void xen_clockevents_resume(void) {}
#endif

#endif /* __XEN_CPU_CLOCK_H__ */
