/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI specific setup.
 *
 * Copyright (C) 1995-1997,1999,2001-2002 Silicon Graphics, Inc.  All rights reserved.
 * Copyright (C) 1999 Ralf Baechle (ralf@gnu.org)
 */
#ifndef _ASM_IA64_SN_ARCH_H
#define _ASM_IA64_SN_ARCH_H

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/mmzone.h>
#include <asm/sn/types.h>

#if defined(CONFIG_IA64_SGI_SN1) 
#include <asm/sn/sn1/arch.h>
#elif defined(CONFIG_IA64_SGI_SN2)
#include <asm/sn/sn2/arch.h>
#endif


#if defined(CONFIG_IA64_SGI_SN1) 
typedef u64	bdrkreg_t;
#elif defined(CONFIG_IA64_SGI_SN2)
typedef u64	shubreg_t;
#endif

typedef u64	hubreg_t;
typedef u64	mmr_t;
typedef u64	nic_t;

#define CNODE_TO_CPU_BASE(_cnode)	(NODEPDA(_cnode)->node_first_cpu)

#define NASID_TO_COMPACT_NODEID(nasid)  (nasid_to_cnodeid(nasid))
#define COMPACT_TO_NASID_NODEID(cnode)  (cnodeid_to_nasid(cnode))


#define INVALID_NASID		((nasid_t)-1)
#define INVALID_CNODEID		((cnodeid_t)-1)
#define INVALID_PNODEID		((pnodeid_t)-1)
#define INVALID_MODULE		((moduleid_t)-1)
#define	INVALID_PARTID		((partid_t)-1)

extern cpuid_t cnodetocpu(cnodeid_t);
void   sn_flush_all_caches(long addr, long bytes);

extern int     is_fine_dirmode(void);


#endif /* _ASM_IA64_SN_ARCH_H */
