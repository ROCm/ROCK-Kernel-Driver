#ifndef _ASM_IA64_PERCPU_H
#define _ASM_IA64_PERCPU_H

#include <linux/config.h>
#include <linux/compiler.h>

/*
 * Copyright (C) 2002-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */
#define PERCPU_ENOUGH_ROOM PERCPU_PAGE_SIZE

#ifdef __ASSEMBLY__

#define THIS_CPU(var)	(var##__per_cpu)  /* use this to mark accesses to per-CPU variables... */

#else /* !__ASSEMBLY__ */

#include <linux/threads.h>

extern unsigned long __per_cpu_offset[NR_CPUS];

#define DEFINE_PER_CPU(type, name) \
    __attribute__((__section__(".data.percpu"))) __typeof__(type) name##__per_cpu
#define DECLARE_PER_CPU(type, name) extern __typeof__(type) name##__per_cpu

#define __get_cpu_var(var)	(var##__per_cpu)
#ifdef CONFIG_SMP
# define per_cpu(var, cpu)	(*RELOC_HIDE(&var##__per_cpu, __per_cpu_offset[cpu]))

extern void percpu_modcopy(void *pcpudst, const void *src, unsigned long size);
#else
# define per_cpu(var, cpu)	((void)cpu, __get_cpu_var(var))
#endif

#define EXPORT_PER_CPU_SYMBOL(var) EXPORT_SYMBOL(var##__per_cpu)
#define EXPORT_PER_CPU_SYMBOL_GPL(var) EXPORT_SYMBOL_GPL(var##__per_cpu)

extern void setup_per_cpu_areas (void);

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_IA64_PERCPU_H */
