#ifndef __ASM_MIPS_SMP_H
#define __ASM_MIPS_SMP_H

#include <linux/config.h>

#ifdef CONFIG_SMP

#include <asm/spinlock.h>
#include <asm/atomic.h>
#include <asm/current.h>


/* Mappings are straight across.  If we want
   to add support for disabling cpus and such,
   we'll have to do what the mips64 port does here */
#define cpu_logical_map(cpu)	(cpu)
#define cpu_number_map(cpu)     (cpu)

#define smp_processor_id()  (current->processor)


/* I've no idea what the real meaning of this is */
#define PROC_CHANGE_PENALTY	20

#define NO_PROC_ID	(-1)

struct smp_fn_call_struct {
	spinlock_t lock;
	atomic_t   finished;
	void (*fn)(void *);
	void *data;
};

extern struct smp_fn_call_struct smp_fn_call;

#endif /* CONFIG_SMP */
#endif /* __ASM_MIPS_SMP_H */
