#ifndef _ASM_X86_64_VSYSCALL_H_
#define _ASM_X86_64_VSYSCALL_H_

enum vsyscall_num {
	__NR_vgettimeofday,
	__NR_vtime,
};

#define VSYSCALL_START (-10UL << 20)
#define VSYSCALL_SIZE 1024
#define VSYSCALL_END (-2UL << 20)
#define VSYSCALL_ADDR(vsyscall_nr) (VSYSCALL_START+VSYSCALL_SIZE*(vsyscall_nr))

#ifdef __KERNEL__

#define __section_last_tsc_low	__attribute__ ((unused, __section__ (".last_tsc_low")))
#define __section_delay_at_last_interrupt	__attribute__ ((unused, __section__ (".delay_at_last_interrupt")))
#define __section_fast_gettimeoffset_quotient	__attribute__ ((unused, __section__ (".fast_gettimeoffset_quotient")))
#define __section_wall_jiffies __attribute__ ((unused, __section__ (".wall_jiffies")))
#define __section_jiffies __attribute__ ((unused, __section__ (".jiffies")))
#define __section_sys_tz __attribute__ ((unused, __section__ (".sys_tz")))
#define __section_xtime __attribute__ ((unused, __section__ (".xtime")))
#define __section_vxtime_sequence __attribute__ ((unused, __section__ (".vxtime_sequence")))

/* vsyscall space (readonly) */
extern long __vxtime_sequence[2];
extern int __delay_at_last_interrupt;
extern unsigned long __last_tsc_low;
extern unsigned long __fast_gettimeoffset_quotient;
extern struct timeval __xtime;
extern volatile unsigned long __jiffies;
extern unsigned long __wall_jiffies;
extern struct timezone __sys_tz;

/* kernel space (writeable) */
extern unsigned long last_tsc_low;
extern int delay_at_last_interrupt;
extern unsigned long fast_gettimeoffset_quotient;
extern unsigned long wall_jiffies;
extern struct timezone sys_tz;
extern long vxtime_sequence[2];

#define vxtime_lock() do { vxtime_sequence[0]++; wmb(); } while(0)
#define vxtime_unlock() do { wmb(); vxtime_sequence[1]++; } while (0)

#endif /* __KERNEL__ */

#endif /* _ASM_X86_64_VSYSCALL_H_ */
