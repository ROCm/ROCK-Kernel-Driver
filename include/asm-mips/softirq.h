/* $Id: softirq.h,v 1.11 2000/02/23 00:41:38 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1997, 1998, 1999, 2000 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_SOFTIRQ_H
#define _ASM_SOFTIRQ_H

#include <asm/atomic.h>
#include <asm/hardirq.h>

#define cpu_bh_disable(cpu)	do { local_bh_count(cpu)++; barrier(); } while (0)
#define cpu_bh_enable(cpu)	do { barrier(); local_bh_count(cpu)--; } while (0)

#define local_bh_disable()	cpu_bh_disable(smp_processor_id())
#define local_bh_enable()	cpu_bh_enable(smp_processor_id())

#define in_softirq() (local_bh_count(smp_processor_id()) != 0)

#endif /* _ASM_SOFTIRQ_H */
