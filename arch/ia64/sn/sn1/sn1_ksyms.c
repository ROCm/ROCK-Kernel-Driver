/* 
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 Jesse Barnes (jbarnes@sgi.com)
 */


/*
 * Architecture-specific kernel symbols
 */

#include <linux/module.h>

#include <asm/machvec.h>

/*
 * I/O routines
 */
EXPORT_SYMBOL(sn1_outb);
EXPORT_SYMBOL(sn1_outl);
EXPORT_SYMBOL(sn1_outw);
EXPORT_SYMBOL(sn1_inw);
EXPORT_SYMBOL(sn1_inb);
EXPORT_SYMBOL(sn1_inl);

/*
 * other stuff (more to be added later, cleanup then)
 */
EXPORT_SYMBOL(sn1_pci_map_sg);
EXPORT_SYMBOL(sn1_pci_unmap_sg);
EXPORT_SYMBOL(sn1_pci_alloc_consistent);
EXPORT_SYMBOL(sn1_pci_free_consistent);
EXPORT_SYMBOL(sn1_dma_address);

#include <linux/mm.h>
EXPORT_SYMBOL(alloc_pages);
