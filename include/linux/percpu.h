#ifndef __LINUX_PERCPU_H
#define __LINUX_PERCPU_H
#include <linux/spinlock.h> /* For preempt_disable() */
#include <asm/percpu.h>

/* Must be an lvalue. */
#define get_cpu_var(var) (*({ preempt_disable(); &__get_cpu_var(var); }))
#define put_cpu_var(var) preempt_enable()

#endif /* __LINUX_PERCPU_H */
