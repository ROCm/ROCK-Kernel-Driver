/* softirq.h: 64-bit Sparc soft IRQ support.
 *
 * Copyright (C) 1997, 1998 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC64_SOFTIRQ_H
#define __SPARC64_SOFTIRQ_H

#include <asm/atomic.h>
#include <asm/hardirq.h>
#include <asm/system.h>		/* for membar() */

#define local_bh_disable()	(local_bh_count(smp_processor_id())++)
#define local_bh_enable()	(local_bh_count(smp_processor_id())--)

#define in_softirq() (local_bh_count(smp_processor_id()) != 0)

#endif /* !(__SPARC64_SOFTIRQ_H) */
