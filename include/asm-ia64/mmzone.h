/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2000,2003 Silicon Graphics, Inc.  All rights reserved.
 * Copyright (c) 2002 NEC Corp.
 * Copyright (c) 2002 Erich Focht <efocht@ess.nec.de>
 * Copyright (c) 2002 Kimio Suganuma <k-suganuma@da.jp.nec.com>
 */
#ifndef _ASM_IA64_MMZONE_H
#define _ASM_IA64_MMZONE_H

#include <linux/config.h>
#include <asm/page.h>
#include <asm/meminit.h>

#ifdef CONFIG_DISCONTIGMEM

#ifdef CONFIG_IA64_DIG /* DIG systems are small */
# define MAX_PHYSNODE_ID	8
# define NR_NODES		8
# define NR_MEMBLKS		(NR_NODES * 32)
#else /* sn2 is the biggest case, so we use that if !DIG */
# define MAX_PHYSNODE_ID	2048
# define NR_NODES		256
# define NR_MEMBLKS		(NR_NODES)
#endif

extern unsigned long max_low_pfn;

#define pfn_valid(pfn)		(((pfn) < max_low_pfn) && ia64_pfn_valid(pfn))
#define page_to_pfn(page)	((unsigned long) (page - vmem_map))
#define pfn_to_page(pfn)	(vmem_map + (pfn))

#endif /* CONFIG_DISCONTIGMEM */
#endif /* _ASM_IA64_MMZONE_H */
