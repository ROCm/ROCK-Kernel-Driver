/*
 * Some of the code in this file has been gleaned from the 64 bit 
 * discontigmem support code base.
 *
 * Copyright (C) 2002, IBM Corp.
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to Pat Gaughen <gone@us.ibm.com>
 */

#ifndef _ASM_SRAT_H_
#define _ASM_SRAT_H_

/*
 * each element in pfnnode_map represents 256 MB (2^28) of pages.
 * so, to represent 64GB we need 256 elements.
 */
#define MAX_ELEMENTS 256
#define PFN_TO_ELEMENT(pfn) ((pfn)>>(28 - PAGE_SHIFT))

extern int pfnnode_map[];
#define pfn_to_nid(pfn) ({ pfnnode_map[PFN_TO_ELEMENT(pfn)]; })
#define pfn_to_pgdat(pfn) NODE_DATA(pfn_to_nid(pfn))
#define PHYSADDR_TO_NID(pa) pfn_to_nid(pa >> PAGE_SHIFT)
#define MAX_NUMNODES		8
extern void get_memcfg_from_srat(void);
extern unsigned long *get_zholes_size(int);
#define get_memcfg_numa() get_memcfg_from_srat()

#endif /* _ASM_SRAT_H_ */
