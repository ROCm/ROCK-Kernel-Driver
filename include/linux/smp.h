#ifndef __LINUX_SMP_H
#define __LINUX_SMP_H

/*
 *	Generic SMP support
 *		Alan Cox. <alan@redhat.com>
 */

#include <linux/config.h>

#ifdef CONFIG_SMP

#include <linux/preempt.h>
#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/thread_info.h>
#include <asm/smp.h>
#include <asm/bug.h>

/*
 * main cross-CPU interfaces, handles INIT, TLB flush, STOP, etc.
 * (defined in asm header):
 */ 

/*
 * stops all CPUs but the current one:
 */
extern void smp_send_stop(void);

/*
 * sends a 'reschedule' event to another CPU:
 */
extern void smp_send_reschedule(int cpu);


/*
 * Prepare machine for booting other CPUs.
 */
extern void smp_prepare_cpus(unsigned int max_cpus);

/*
 * Bring a CPU up
 */
extern int __cpu_up(unsigned int cpunum);

/*
 * Final polishing of CPUs
 */
extern void smp_cpus_done(unsigned int max_cpus);

/*
 * Call a function on all other processors
 */
extern int smp_call_function (void (*func) (void *info), void *info,
			      int retry, int wait);

/*
 * Call a function on all processors
 */
static inline int on_each_cpu(void (*func) (void *info), void *info,
			      int retry, int wait)
{
	int ret = 0;

	preempt_disable();
	ret = smp_call_function(func, info, retry, wait);
	func(info);
	preempt_enable();
	return ret;
}

/*
 * True once the per process idle is forked
 */
extern int smp_threads_ready;

#define MSG_ALL_BUT_SELF	0x8000	/* Assume <32768 CPU's */
#define MSG_ALL			0x8001

#define MSG_INVALIDATE_TLB	0x0001	/* Remote processor TLB invalidate */
#define MSG_STOP_CPU		0x0002	/* Sent to shut down slave CPU's
					 * when rebooting
					 */
#define MSG_RESCHEDULE		0x0003	/* Reschedule request from master CPU*/
#define MSG_CALL_FUNCTION       0x0004  /* Call function on all other CPUs */

/*
 * Mark the boot cpu "online" so that it can call console drivers in
 * printk() and can access its per-cpu storage.
 */
void smp_prepare_boot_cpu(void);

#else /* !SMP */

/*
 *	These macros fold the SMP functionality into a single CPU system
 */
 
#define smp_processor_id()			0
#define hard_smp_processor_id()			0
#define smp_threads_ready			1
#define smp_call_function(func,info,retry,wait)	({ 0; })
#define on_each_cpu(func,info,retry,wait)	({ func(info); 0; })
static inline void smp_send_reschedule(int cpu) { }
#define num_booting_cpus()			1
#define smp_prepare_boot_cpu()			do {} while (0)

#endif /* !SMP */

#define get_cpu()		({ preempt_disable(); smp_processor_id(); })
#define put_cpu()		preempt_enable()
#define put_cpu_no_resched()	preempt_enable_no_resched()

#endif /* __LINUX_SMP_H */
