/*
 * Copyright (c) 2002 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it 
 * under the terms of version 2 of the GNU General Public License 
 * as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it would be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 * 
 * Further, this software is distributed without any warranty that it is 
 * free of the rightful claim of any third person regarding infringement 
 * or the like.  Any license provided herein, whether implied or 
 * otherwise, applies only to this software file.  Patent licenses, if 
 * any, provided herein do not apply to combinations of this program with 
 * other software, or any other product whatsoever.
 * 
 * You should have received a copy of the GNU General Public 
 * License along with this program; if not, write the Free Software 
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 * 
 * Contact information:  Silicon Graphics, Inc., 1600 Amphitheatre Pkwy, 
 * Mountain View, CA  94043, or:
 * 
 * http://www.sgi.com 
 * 
 * For further information regarding this notice, see: 
 * 
 * http://oss.sgi.com/projects/GenInfo/NoticeExplan
 */

#include <linux/config.h>

#ifdef CONFIG_IA64_SGI_SN1
#define MACHVEC_PLATFORM_NAME	sn1
#else CONFIG_IA64_SGI_SN1
#define MACHVEC_PLATFORM_NAME	sn2
#else
#error "unknown platform"
#endif

#include <asm/machvec_init.h>
#include <asm/io.h>
#include <linux/pci.h>
void*
sn_mk_io_addr_MACRO

dma_addr_t
sn_pci_map_single_MACRO

int
sn_pci_map_sg_MACRO

unsigned long
sn_virt_to_phys_MACRO

void *
sn_phys_to_virt_MACRO
