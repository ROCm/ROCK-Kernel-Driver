/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001-2003 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <asm/sn/sgi.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/pci/pcibr_private.h>
#include <asm/sn/pci/pci_defs.h>

extern pcibr_info_t      pcibr_info_get(vertex_hdl_t);

uint64_t          pcibr_config_get(vertex_hdl_t, unsigned, unsigned);
uint64_t          do_pcibr_config_get(cfg_p, unsigned, unsigned);
void              pcibr_config_set(vertex_hdl_t, unsigned, unsigned, uint64_t);
void       	  do_pcibr_config_set(cfg_p, unsigned, unsigned, uint64_t);

/*
 * fancy snia bit twiddling....
 */
#define	CBP(b,r) (((volatile uint8_t *) b)[(r)])
#define	CSP(b,r) (((volatile uint16_t *) b)[((r)/2)])
#define	CWP(b,r) (((volatile uint32_t *) b)[(r)/4])

/*
 * Return a config space address for given slot / func / offset.  Note the
 * returned pointer is a 32bit word (ie. cfg_p) aligned pointer pointing to
 * the 32bit word that contains the "offset" byte.
 */
cfg_p
pcibr_func_config_addr(pcibr_soft_t soft, pciio_bus_t bus, pciio_slot_t slot, 
					pciio_function_t func, int offset)
{
	/*
	 * Type 1 config space
	 */
	if (bus > 0) {
		pcireg_type1_cntr_set(soft, ((bus << 16) | (slot << 11)));
		return (pcireg_type1_cfg_addr(soft, func, offset));
	}

	/*
	 * Type 0 config space
	 */
	return (pcireg_type0_cfg_addr(soft, slot, func, offset));
}

/*
 * Return config space address for given slot / offset.  Note the returned
 * pointer is a 32bit word (ie. cfg_p) aligned pointer pointing to the
 * 32bit word that contains the "offset" byte.
 */
cfg_p
pcibr_slot_config_addr(pcibr_soft_t soft, pciio_slot_t slot, int offset)
{
	return pcibr_func_config_addr(soft, 0, slot, 0, offset);
}

/*
 * Set config space data for given slot / func / offset
 */
void
pcibr_func_config_set(pcibr_soft_t soft, pciio_slot_t slot, 
			pciio_function_t func, int offset, unsigned val)
{
	cfg_p  cfg_base;

	cfg_base = pcibr_func_config_addr(soft, 0, slot, func, 0);
	do_pcibr_config_set(cfg_base, offset, sizeof(unsigned), val);
}

int pcibr_config_debug = 0;

cfg_p
pcibr_config_addr(vertex_hdl_t conn,
		  unsigned reg)
{
    pcibr_info_t            pcibr_info;
    pciio_bus_t		    pciio_bus;
    pciio_slot_t            pciio_slot;
    pciio_function_t        pciio_func;
    cfg_p                   cfgbase = (cfg_p)0;
    pciio_info_t	    pciio_info;

    pciio_info = pciio_info_get(conn);
    pcibr_info = pcibr_info_get(conn);

    /*
     * Determine the PCI bus/slot/func to generate a config address for.
     */

    if (pciio_info_type1_get(pciio_info)) {
	/*
	 * Conn is a vhdl which uses TYPE 1 addressing explicitly passed 
	 * in reg.
	 */
	pciio_bus = PCI_TYPE1_BUS(reg);
	pciio_slot = PCI_TYPE1_SLOT(reg);
	pciio_func = PCI_TYPE1_FUNC(reg);

	ASSERT(pciio_bus != 0);
    } else {
	/*
	 * Conn is directly connected to the host bus.  PCI bus number is
	 * hardcoded to 0 (even though it may have a logical bus number != 0)
	 * and slot/function are derived from the pcibr_info_t associated
	 * with the device.
	 */
	pciio_bus = 0;

    pciio_slot = PCIBR_INFO_SLOT_GET_INT(pcibr_info);
    if (pciio_slot == PCIIO_SLOT_NONE)
	pciio_slot = PCI_TYPE1_SLOT(reg);

    pciio_func = pcibr_info->f_func;
    if (pciio_func == PCIIO_FUNC_NONE)
	pciio_func = PCI_TYPE1_FUNC(reg);
    }

    cfgbase = pcibr_func_config_addr((pcibr_soft_t) pcibr_info->f_mfast,
			pciio_bus, pciio_slot, pciio_func, 0);

    return cfgbase;
}

uint64_t
pcibr_config_get(vertex_hdl_t conn,
		 unsigned reg,
		 unsigned size)
{
	return do_pcibr_config_get(pcibr_config_addr(conn, reg),
				PCI_TYPE1_REG(reg), size);
}

uint64_t
do_pcibr_config_get(cfg_p cfgbase,
		       unsigned reg,
		       unsigned size)
{
    unsigned                value;

    value = CWP(cfgbase, reg);
    if (reg & 3)
	value >>= 8 * (reg & 3);
    if (size < 4)
	value &= (1 << (8 * size)) - 1;
    return value;
}

void
pcibr_config_set(vertex_hdl_t conn,
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
		CBP(cfgbase, reg) = value;
		break;
	case 2:
		if (reg & 1) {
			CBP(cfgbase, reg) = value;
			CBP(cfgbase, reg + 1) = value >> 8;
		} else
			CSP(cfgbase, reg) = value;
		break;
	case 3:
		if (reg & 1) {
			CBP(cfgbase, reg) = value;
			CSP(cfgbase, (reg + 1)) = value >> 8;
		} else {
			CSP(cfgbase, reg) = value;
			CBP(cfgbase, reg + 2) = value >> 16;
		}
		break;
	case 4:
		CWP(cfgbase, reg) = value;
		break;
 	}
}
