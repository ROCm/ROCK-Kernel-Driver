/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2003 Silicon Graphics, Inc. All rights reserved.
 */
#ifndef _ASM_IA64_SN_PCI_CVLINK_H
#define _ASM_IA64_SN_PCI_CVLINK_H

#include <asm/sn/types.h>
#include <asm/sn/sgi.h>
#include <asm/sn/driver.h>
#include <asm/sn/iograph.h>
#include <asm/param.h>
#include <asm/sn/pio.h>
#include <asm/sn/xtalk/xwidget.h>
#include <asm/sn/sn_private.h>
#include <asm/sn/addrs.h>
#include <asm/sn/hcl.h>
#include <asm/sn/hcl_util.h>
#include <asm/sn/intr.h>
#include <asm/sn/xtalk/xtalkaddrs.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/io.h>

#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/pci/pcibr_private.h>

#define MAX_PCI_XWIDGET 256
#define MAX_ATE_MAPS 1024

#define SN_DEVICE_SYSDATA(dev) \
	((struct sn_device_sysdata *) \
	(((struct pci_controller *) ((dev)->sysdata))->platform_data))

#define IS_PCI32G(dev)	((dev)->dma_mask >= 0xffffffff)
#define IS_PCI32L(dev)	((dev)->dma_mask < 0xffffffff)

#define PCIDEV_VERTEX(pci_dev) \
	((SN_DEVICE_SYSDATA(pci_dev))->vhdl)

struct sn_widget_sysdata {
        vertex_hdl_t  vhdl;
};

struct sn_device_sysdata {
        vertex_hdl_t		vhdl;
	pciio_provider_t	*pci_provider;
	pciio_intr_t		intr_handle;
	struct sn_flush_device_list *dma_flush_list;
        pciio_piomap_t		pio_map[PCI_ROM_RESOURCE];
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

#endif				/* _ASM_IA64_SN_PCI_CVLINK_H */
