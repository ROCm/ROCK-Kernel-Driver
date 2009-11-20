/*
 * Copyright (c) 2001-2007 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *
 * This file contains Itanium Processor Family specific definitions
 * for the perfmon interface.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307 USA
 */
#ifndef _ASM_IA64_PERFMON_H_
#define _ASM_IA64_PERFMON_H_

/*
 * arch-specific user visible interface definitions
 */

#define PFM_ARCH_MAX_PMCS	(256+64)
#define PFM_ARCH_MAX_PMDS	(256+64)

#define PFM_ARCH_PMD_STK_ARG	8
#define PFM_ARCH_PMC_STK_ARG	8

/*
 * Itanium specific context flags
 *
 * bits[00-15]: generic flags (see asm/perfmon.h)
 * bits[16-31]: arch-specific flags
 */
#define PFM_ITA_FL_INSECURE 0x10000 /* clear psr.sp on non system, non self */

/*
 * Itanium specific public event set flags (set_flags)
 *
 * event set flags layout:
 * bits[00-15] : generic flags
 * bits[16-31] : arch-specific flags
 */
#define PFM_ITA_SETFL_EXCL_INTR	0x10000	 /* exclude interrupt execution */
#define PFM_ITA_SETFL_INTR_ONLY	0x20000	 /* include only interrupt execution */
#define PFM_ITA_SETFL_IDLE_EXCL 0x40000  /* stop monitoring in idle loop */

/*
 * compatibility for version v2.0 of the interface
 */
#include <asm/perfmon_compat.h>

#endif /* _ASM_IA64_PERFMON_H_ */
