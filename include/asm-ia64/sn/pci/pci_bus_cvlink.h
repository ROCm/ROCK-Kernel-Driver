/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_PCI_CVLINK_H
#define _ASM_SN_PCI_CVLINK_H

#define SET_PCIA64(dev) \
	(((struct sn1_device_sysdata *)((dev)->sysdata))->isa64) = 1
#define IS_PCIA64(dev)	(((dev)->dma_mask == 0xffffffffffffffffUL) || \
		(((struct sn1_device_sysdata *)((dev)->sysdata))->isa64))
#define IS_PCI32G(dev)	((dev)->dma_mask >= 0xffffffff)
#define IS_PCI32L(dev)	((dev)->dma_mask < 0xffffffff)

struct sn1_widget_sysdata {
        devfs_handle_t  vhdl;
};

struct sn1_device_sysdata {
        devfs_handle_t  vhdl;
	int		isa64;
};

struct sn1_dma_maps_s{
	struct pcibr_dmamap_s dma_map;
        dma_addr_t      dma_addr;
};

struct ioports_to_tlbs_s {
	unsigned long	p:1,
			rv_1:1,
			ma:3,
			a:1,
			d:1,
			pl:2,
			ar:3,
			ppn:38,
			rv_2:2,
			ed:1,
			ig:11;
};

#endif				/* _ASM_SN_PCI_CVLINK_H */
