#ifndef _ASM_IA64_PERCPU_H
#define _ASM_IA64_PERCPU_H

/*
 * Copyright (C) 2002 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#ifdef __ASSEMBLY__

#define THIS_CPU(var)	(var)	/* use this to mark accesses to per-CPU variables... */

#else /* !__ASSEMBLY__ */

#include <linux/threads.h>

extern unsigned long __per_cpu_offset[NR_CPUS];

#define per_cpu(var, cpu)	(*(__typeof__(&(var))) ((void *) &(var) + __per_cpu_offset[cpu]))
#define __get_cpu_var(var)	(var)

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_IA64_PERCPU_H */
