#ifndef __LINUX_PERCPU_H
#define __LINUX_PERCPU_H
#include <linux/config.h>

#ifdef CONFIG_SMP
#define __per_cpu_data	__attribute__((section(".data.percpu")))
#include <asm/percpu.h>
#else
#define __per_cpu_data
#define per_cpu(var, cpu)			var
#define __get_cpu_var(var)			var
#endif

#define get_cpu_var(var) ({ preempt_disable(); __get_cpu_var(var); })
#define put_cpu_var(var) preempt_enable()
#endif /* __LINUX_PERCPU_H */
