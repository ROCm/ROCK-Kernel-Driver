#ifndef __ASM_SOFTIRQ_H
#define __ASM_SOFTIRQ_H

#include <asm/atomic.h>
#include <asm/hardirq.h>

#define local_bh_disable()      (local_bh_count(smp_processor_id())++)
#define local_bh_enable()       (local_bh_count(smp_processor_id())--)

#define in_softirq() (local_bh_count(smp_processor_id()) != 0)

#endif	/* __ASM_SOFTIRQ_H */
