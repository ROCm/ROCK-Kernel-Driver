#ifndef _ASM_IA64_SOFTIRQ_H
#define _ASM_IA64_SOFTIRQ_H

/*
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 */
#include <asm/hardirq.h>

#define cpu_bh_disable(cpu)	do { local_bh_count(cpu)++; barrier(); } while (0)
#define cpu_bh_enable(cpu)	do { barrier(); local_bh_count(cpu)--; } while (0)

#define local_bh_disable()	cpu_bh_disable(smp_processor_id())
#define local_bh_enable()	cpu_bh_enable(smp_processor_id())

#define in_softirq()		(local_bh_count(smp_processor_id()) != 0)

#endif /* _ASM_IA64_SOFTIRQ_H */
