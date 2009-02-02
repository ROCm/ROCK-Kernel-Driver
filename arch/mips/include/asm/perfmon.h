/*
 * Copyright (c) 2007 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *
 * This file contains mips64 specific definitions for the perfmon
 * interface.
 *
 * This file MUST never be included directly. Use linux/perfmon.h.
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
#ifndef _ASM_MIPS64_PERFMON_H_
#define _ASM_MIPS64_PERFMON_H_

/*
 * arch-specific user visible interface definitions
 */

#define PFM_ARCH_MAX_PMCS	(256+64) /* 256 HW 64 SW */
#define PFM_ARCH_MAX_PMDS	(256+64) /* 256 HW 64 SW */

#endif /* _ASM_MIPS64_PERFMON_H_ */
