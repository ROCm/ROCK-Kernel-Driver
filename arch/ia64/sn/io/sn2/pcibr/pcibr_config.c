/*
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001-2002 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <asm/sn/sgi.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/addrs.h>
#include <asm/sn/arch.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/xtalk/xwidget.h>
#include <asm/sn/pci/bridge.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/pci/pcibr_private.h>
#include <asm/sn/pci/pci_defs.h>
#include <asm/sn/prio.h>
#include <asm/sn/xtalk/xbow.h>
#include <asm/sn/ioc3.h>
#include <asm/sn/eeprom.h>
#include <asm/sn/io.h>
#include <asm/sn/sn_private.h>

extern pcibr_info_t      pcibr_info_get(devfs_handle_t);

uint64_t          pcibr_config_get(devfs_handle_t, unsigned, unsigned);
uint64_t          do_pcibr_config_get(cfg_p, unsigned, unsigned);
void              pcibr_config_set(devfs_handle_t, unsigned, unsigned, uint64_t);
void              do_pcibr_config_set(cfg_p, unsigned, unsigned, uint64_t);

#define	CB(b,r)	(((volatile uint8_t *) cfgbase)[(r)^3])
#define	CS(b,r)	(((volatile uint16_t *) cfgbase)[((r)/2)^1])
#define	CW(b,r)	(((volatile uint32_t *) cfgbase)[(r)/4])


cfg_p
pcibr_config_addr(devfs_handle_t conn,
		  unsigned reg)
{
    pcibr_info_t            pcibr_info;
    pciio_slot_t            pciio_slot;
    pciio_function_t        pciio_func;
    pcibr_soft_t            pcibr_soft;
    bridge_t               *bridge;
    cfg_p                   cfgbase = (cfg_p)0;

    pcibr_info = pcibr_info_get(conn);

    pciio_slot = pcibr_info->f_slot;
    if (pciio_slot == PCIIO_SLOT_NONE)
	pciio_slot = PCI_TYPE1_SLOT(reg);

    pciio_func = pcibr_info->f_func;
    if (pciio_func == PCIIO_FUNC_NONE)
	pciio_func = PCI_TYPE1_FUNC(reg);

    pcibr_soft = (pcibr_soft_t) pcibr_info->f_mfast;

    bridge = pcibr_soft->bs_base;

    cfgbase = bridge->b_type0_cfg_dev[pciio_slot].f[pciio_func].l;

    return cfgbase;
}

uint64_t
pcibr_config_get(devfs_handle_t conn,
		 unsigned reg,
		 unsigned size)
{
    return do_pcibr_config_get(pcibr_config_addr(conn, reg),
			       PCI_TYPE1_REG(reg), size);
}

uint64_t
do_pcibr_config_get(
		       cfg_p cfgbase,
		       unsigned reg,
		       unsigned size)
{
    unsigned                value;

    value = CW(cfgbase, reg);

    if (reg & 3)
	value >>= 8 * (reg & 3);
    if (size < 4)
	value &= (1 << (8 * size)) - 1;
    return value;
}

void
pcibr_config_set(devfs_handle_t conn,
		 unsigned reg,
		 unsigned size,
		 uint64_t value)
{
    do_pcibr_config_set(pcibr_config_addr(conn, reg),
			PCI_TYPE1_REG(reg), size, value);
}

void
do_pcibr_config_set(cfg_p cfgbase,
		    unsigned reg,
		    unsigned size,
		    uint64_t value)
{
    switch (size) {
    case 1:
	CB(cfgbase, reg) = value;
	break;
    case 2:
	if (reg & 1) {
	    CB(cfgbase, reg) = value;
	    CB(cfgbase, reg + 1) = value >> 8;
	} else
	    CS(cfgbase, reg) = value;
	break;
    case 3:
	if (reg & 1) {
	    CB(cfgbase, reg) = value;
	    CS(cfgbase, (reg + 1)) = value >> 8;
	} else {
	    CS(cfgbase, reg) = value;
	    CB(cfgbase, reg + 2) = value >> 16;
	}
	break;

    case 4:
	CW(cfgbase, reg) = value;
	break;
    }
}
