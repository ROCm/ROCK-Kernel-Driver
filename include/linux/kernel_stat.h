#ifndef _LINUX_KERNEL_STAT_H
#define _LINUX_KERNEL_STAT_H

#include <linux/config.h>
#include <asm/irq.h>
#include <linux/smp.h>
#include <linux/threads.h>

/*
 * 'kernel_stat.h' contains the definitions needed for doing
 * some kernel statistics (CPU usage, context switches ...),
 * used by rstatd/perfmeter
 */

#define DK_MAX_MAJOR 16
#define DK_MAX_DISK 16

struct kernel_stat {
	unsigned int per_cpu_user[NR_CPUS],
	             per_cpu_nice[NR_CPUS],
	             per_cpu_system[NR_CPUS];
	unsigned int dk_drive[DK_MAX_MAJOR][DK_MAX_DISK];
	unsigned int dk_drive_rio[DK_MAX_MAJOR][DK_MAX_DISK];
	unsigned int dk_drive_wio[DK_MAX_MAJOR][DK_MAX_DISK];
	unsigned int dk_drive_rblk[DK_MAX_MAJOR][DK_MAX_DISK];
	unsigned int dk_drive_wblk[DK_MAX_MAJOR][DK_MAX_DISK];
	unsigned int pgpgin, pgpgout;
	unsigned int pswpin, pswpout;
	unsigned int pgalloc, pgfree;
	unsigned int pgactivate, pgdeactivate;
	unsigned int pgfault, pgmajfault;
	unsigned int pgscan, pgsteal;
	unsigned int pageoutrun, allocstall;
#if !defined(CONFIG_ARCH_S390)
	unsigned int irqs[NR_CPUS][NR_IRQS];
#endif
};

extern struct kernel_stat kstat;

extern unsigned long nr_context_switches(void);

/*
 * Maybe we need to smp-ify kernel_stat some day. It would be nice to do
 * that without having to modify all the code that increments the stats.
 */
#define KERNEL_STAT_INC(x) kstat.x++
#define KERNEL_STAT_ADD(x, y) kstat.x += y

#if !defined(CONFIG_ARCH_S390)
/*
 * Number of interrupts per specific IRQ source, since bootup
 */
static inline int kstat_irqs (int irq)
{
	int i, sum=0;

	for (i = 0 ; i < NR_CPUS ; i++)
		sum += kstat.irqs[i][irq];

	return sum;
}
#endif

#endif /* _LINUX_KERNEL_STAT_H */
