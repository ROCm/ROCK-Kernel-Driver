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

#endif				/* _ASM_SN_PCI_CVLINK_H */
