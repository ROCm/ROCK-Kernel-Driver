#ifndef __LINUX_PERCPU_H
#define __LINUX_PERCPU_H
#include <linux/config.h>

#ifdef CONFIG_SMP
#define __per_cpu_data	__attribute__((section(".data.percpu")))
#include <asm/percpu.h>
#else
#define __per_cpu_data
#define per_cpu(var, cpu)			var
#define this_cpu(var)				var
#endif

#endif /* __LINUX_PERCPU_H */
