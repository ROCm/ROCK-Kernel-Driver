/*
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001-2002 Silicon Graphics, Inc. All rights reserved.
 */


#include <linux/config.h>

#ifndef _ASM_IA64_PIO_FLUSH_H
#define _ASM_IA64_PIO_FLUSH_H

/*
 * This macro flushes all outstanding PIOs performed by this cpu to the 
 * intended destination SHUB.  This in essence ensures that all PIO's
 * issues by this cpu has landed at it's destination.
 *
 * This macro expects the caller:
 *	1.  The thread is locked.
 *	2.  All prior PIO operations has been fenced.
 *
 */

#if defined (CONFIG_IA64_SGI_SN)

#include <asm/sn/pda.h>

#if defined (CONFIG_IA64_SGI_SN2)

#define PIO_FLUSH() \
	{ \
	while ( !((volatile unsigned long) (*pda.pio_write_status_addr)) & 0x8000000000000000) { \
			udelay(5); \
	} \
	__ia64_mf_a(); \
	}

#elif defined (CONFIG_IA64_SGI_SN1)

/*
 * For SN1 we need to first read any local Bedrock's MMR and then poll on the 
 * Synergy MMR.
 */
#define PIO_FLUSH() \
	{ \
	(volatile unsigned long) (*pda.bedrock_rev_id); \
	while (!(volatile unsigned long) (*pda.pio_write_status_addr)) { \
		udelay(5); \
	} \
	__ia64_mf_a(); \
	} 
#endif
#else
/*
 * For all ARCHITECTURE type, this is a NOOP.
 */

#define PIO_FLUSH()

#endif

#endif /* _ASM_IA64_PIO_FLUSH_H */
