/* softirq.h: 32-bit Sparc soft IRQ support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998-99 Anton Blanchard (anton@progsoc.uts.edu.au)
 */

#ifndef __SPARC_SOFTIRQ_H
#define __SPARC_SOFTIRQ_H

#include <linux/threads.h>	/* For NR_CPUS */

#include <asm/atomic.h>
#include <asm/smp.h>
#include <asm/hardirq.h>

#define local_bh_disable()	(local_bh_count(smp_processor_id())++)
#define local_bh_enable()	(local_bh_count(smp_processor_id())--)

#define in_softirq() (local_bh_count(smp_processor_id()) != 0)

#endif	/* __SPARC_SOFTIRQ_H */
