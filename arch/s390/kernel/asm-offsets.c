/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed to extract
 * and format the required data.
 */

#include <linux/config.h>
#include <linux/sched.h>

/* Use marker if you need to separate the values later */

#define DEFINE(sym, val, marker) \
	asm volatile("\n->" #sym " %0 " #val " " #marker : : "i" (val))

#define BLANK() asm volatile("\n->" : : )

int main(void)
{
	DEFINE(__THREAD_info, offsetof(struct task_struct, thread_info),);
	DEFINE(__THREAD_ar2, offsetof(struct task_struct, thread.ar2),);
	DEFINE(__THREAD_ar4, offsetof(struct task_struct, thread.ar4),);
	DEFINE(__THREAD_ksp, offsetof(struct task_struct, thread.ksp),);
	DEFINE(__THREAD_per, offsetof(struct task_struct, thread.per_info),);
	BLANK();
	DEFINE(__TI_task, offsetof(struct thread_info, task),);
	DEFINE(__TI_domain, offsetof(struct thread_info, exec_domain),);
	DEFINE(__TI_flags, offsetof(struct thread_info, flags),);
	DEFINE(__TI_cpu, offsetof(struct thread_info, cpu),);
	DEFINE(__TI_precount, offsetof(struct thread_info, preempt_count),);
	return 0;
}
