#ifndef _ASM_i386_VSYSCALL_GTOD_H_
#define _ASM_i386_VSYSCALL_GTOD_H_

#ifdef CONFIG_VSYSCALL_GTOD

/* VSYSCALL_GTOD_START must be the same as
 * __fix_to_virt(FIX_VSYSCALL_GTOD FIRST_PAGE)
 * and must also be same as addr in vmlinux.lds.S */
#define VSYSCALL_GTOD_START 0xffffc000
#define VSYSCALL_GTOD_SIZE 1024
#define VSYSCALL_GTOD_END (VSYSCALL_GTOD_START + PAGE_SIZE)
#define VSYSCALL_GTOD_NUMPAGES \
	((VSYSCALL_GTOD_END-VSYSCALL_GTOD_START) >> PAGE_SHIFT)
#define VSYSCALL_ADDR(vsyscall_nr) \
	(VSYSCALL_GTOD_START+VSYSCALL_GTOD_SIZE*(vsyscall_nr))

#ifdef __KERNEL__
#ifndef __ASSEMBLY__
#include <linux/seqlock.h>

#define __vsyscall(nr) __attribute__ ((unused,__section__(".vsyscall_" #nr)))

/* ReadOnly generic time value attributes*/
#define __section_vsyscall_timesource __attribute__ ((unused, __section__ (".vsyscall_timesource")))
#define __section_xtime_lock __attribute__ ((unused, __section__ (".xtime_lock")))
#define __section_xtime __attribute__ ((unused, __section__ (".xtime")))
#define __section_jiffies __attribute__ ((unused, __section__ (".jiffies")))
#define __section_wall_jiffies __attribute__ ((unused, __section__ (".wall_jiffies")))
#define __section_sys_tz __attribute__ ((unused, __section__ (".sys_tz")))

/* ReadOnly NTP variables */
#define __section_tickadj __attribute__ ((unused, __section__ (".tickadj")))
#define __section_time_adjust __attribute__ ((unused, __section__ (".time_adjust")))


/* ReadOnly TSC time value attributes*/
#define __section_last_tsc_low	__attribute__ ((unused, __section__ (".last_tsc_low")))
#define __section_tsc_delay_at_last_interrupt	__attribute__ ((unused, __section__ (".tsc_delay_at_last_interrupt")))
#define __section_fast_gettimeoffset_quotient	__attribute__ ((unused, __section__ (".fast_gettimeoffset_quotient")))

/* ReadOnly Cyclone time value attributes*/
#define __section_cyclone_timer __attribute__ ((unused, __section__ (".cyclone_timer")))
#define __section_last_cyclone_low __attribute__ ((unused, __section__ (".last_cyclone_low")))
#define __section_cyclone_delay_at_last_interrupt	__attribute__ ((unused, __section__ (".cyclone_delay_at_last_interrupt")))

enum vsyscall_num {
	__NR_vgettimeofday,
	__NR_vtime,
};

enum vsyscall_timesource_e {
	VSYSCALL_GTOD_NONE,
	VSYSCALL_GTOD_TSC,
	VSYSCALL_GTOD_CYCLONE,
};

int vsyscall_init(void);
void vsyscall_set_timesource(char* name);

extern char __vsyscall_0;
#endif /* __ASSEMBLY__ */
#endif /* __KERNEL__ */
#else /* CONFIG_VSYSCALL_GTOD */
#define vsyscall_init()
#define vsyscall_set_timesource(x)
#endif /* CONFIG_VSYSCALL_GTOD */
#endif /* _ASM_i386_VSYSCALL_GTOD_H_ */

