#ifndef __LINUX_SMP_H
#define __LINUX_SMP_H

/*
 *	Generic SMP support
 *		Alan Cox. <alan@redhat.com>
 */

#include <linux/config.h>

#ifdef CONFIG_SMP

#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/threads.h>
#include <asm/smp.h>

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
extern void FASTCALL(smp_send_reschedule(int cpu));


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
 * True once the per process idle is forked
 */
extern int smp_threads_ready;

extern volatile unsigned long smp_msg_data;
extern volatile int smp_src_cpu;
extern volatile int smp_msg_id;

#define MSG_ALL_BUT_SELF	0x8000	/* Assume <32768 CPU's */
#define MSG_ALL			0x8001

#define MSG_INVALIDATE_TLB	0x0001	/* Remote processor TLB invalidate */
#define MSG_STOP_CPU		0x0002	/* Sent to shut down slave CPU's
					 * when rebooting
					 */
#define MSG_RESCHEDULE		0x0003	/* Reschedule request from master CPU*/
#define MSG_CALL_FUNCTION       0x0004  /* Call function on all other CPUs */

struct notifier_block;

/* Need to know about CPUs going up/down? */
extern int register_cpu_notifier(struct notifier_block *nb);
extern void unregister_cpu_notifier(struct notifier_block *nb);

int cpu_up(unsigned int cpu);
#else /* !SMP */

/*
 *	These macros fold the SMP functionality into a single CPU system
 */
 
#define smp_processor_id()			0
#define hard_smp_processor_id()			0
#define smp_threads_ready			1
#define smp_call_function(func,info,retry,wait)	({ 0; })
static inline void smp_send_reschedule(int cpu) { }
static inline void smp_send_reschedule_all(void) { }
#define cpu_online_map				1
#define cpu_online(cpu)				({ BUG_ON((cpu) != 0); 1; })
#define num_online_cpus()			1
#define num_booting_cpus()			1
#define cpu_possible(cpu)				({ BUG_ON((cpu) != 0); 1; })

struct notifier_block;

/* Need to know about CPUs going up/down? */
static inline int register_cpu_notifier(struct notifier_block *nb)
{
	return 0;
}
static inline void unregister_cpu_notifier(struct notifier_block *nb)
{
}
#endif /* !SMP */

#define get_cpu()		({ preempt_disable(); smp_processor_id(); })
#define put_cpu()		preempt_enable()
#define put_cpu_no_resched()	preempt_enable_no_resched()

#endif /* __LINUX_SMP_H */
