/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI specific setup.
 *
 * Copyright (C) 1995-1997,1999,2001-2003 Silicon Graphics, Inc.  All rights reserved.
 * Copyright (C) 1999 Ralf Baechle (ralf@gnu.org)
 */
#ifndef _ASM_IA64_SN_ARCH_H
#define _ASM_IA64_SN_ARCH_H

#include <asm/types.h>
#include <asm/sn/types.h>
#include <asm/sn/sn_cpuid.h>

typedef u64	shubreg_t;
typedef u64	hubreg_t;
typedef u64	mmr_t;
typedef u64	nic_t;

#define NASID_TO_COMPACT_NODEID(nasid)  (nasid_to_cnodeid(nasid))
#define COMPACT_TO_NASID_NODEID(cnode)  (cnodeid_to_nasid(cnode))


#define INVALID_NASID		((nasid_t)-1)
#define INVALID_CNODEID		((cnodeid_t)-1)
#define INVALID_PNODEID		((pnodeid_t)-1)
#define INVALID_SLAB            (slabid_t)-1
#define INVALID_MODULE		((moduleid_t)-1)
#define	INVALID_PARTID		((partid_t)-1)

extern cpuid_t cnodetocpu(cnodeid_t);
extern void sn_flush_all_caches(long addr, long bytes);
extern int is_fine_dirmode(void);

#endif /* _ASM_IA64_SN_ARCH_H */
