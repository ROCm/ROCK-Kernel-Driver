#ifndef _ASM_X86_64_VSYSCALL_H_
#define _ASM_X86_64_VSYSCALL_H_

#include <linux/time.h>

enum vsyscall_num {
	__NR_vgettimeofday,
	__NR_vtime,
};

#define VSYSCALL_START (-10UL << 20)
#define VSYSCALL_SIZE 1024
#define VSYSCALL_END (-2UL << 20)
#define VSYSCALL_ADDR(vsyscall_nr) (VSYSCALL_START+VSYSCALL_SIZE*(vsyscall_nr))

#ifdef __KERNEL__

#define __section_hpet __attribute__ ((unused, __section__ (".hpet"), aligned(16)))
#define __section_wall_jiffies __attribute__ ((unused, __section__ (".wall_jiffies"), aligned(16)))
#define __section_jiffies __attribute__ ((unused, __section__ (".jiffies"), aligned(16)))
#define __section_sys_tz __attribute__ ((unused, __section__ (".sys_tz"), aligned(16)))
#define __section_xtime __attribute__ ((unused, __section__ (".xtime"), aligned(16)))
#define __section_vxtime_sequence __attribute__ ((unused, __section__ (".vxtime_sequence"), aligned(16)))

struct hpet_data {
	long address;		/* base address */
	unsigned long hz;	/* HPET clocks / sec */
	int trigger;		/* value at last interrupt */
	int last;
	int offset;
	unsigned long last_tsc;
	long ticks;
};

#define hpet_readl(a)           readl(fix_to_virt(FIX_HPET_BASE) + a)
#define hpet_writel(d,a)        writel(d, fix_to_virt(FIX_HPET_BASE) + a)

/* vsyscall space (readonly) */
extern long __vxtime_sequence[2];
extern struct hpet_data __hpet;
extern struct timespec __xtime;
extern volatile unsigned long __jiffies;
extern unsigned long __wall_jiffies;
extern struct timezone __sys_tz;

/* kernel space (writeable) */
extern long vxtime_sequence[2];
extern struct hpet_data hpet;
extern unsigned long wall_jiffies;
extern struct timezone sys_tz;

#define vxtime_lock() do { vxtime_sequence[0]++; wmb(); } while(0)
#define vxtime_unlock() do { wmb(); vxtime_sequence[1]++; } while (0)

#endif /* __KERNEL__ */

#endif /* _ASM_X86_64_VSYSCALL_H_ */
