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
#include <linux/byteorder/swab.h>
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
uint64_t          do_pcibr_config_get(int, cfg_p, unsigned, unsigned);
void              pcibr_config_set(devfs_handle_t, unsigned, unsigned, uint64_t);
void       	  do_pcibr_config_set(int, cfg_p, unsigned, unsigned, uint64_t);
static void	  swap_do_pcibr_config_set(cfg_p, unsigned, unsigned, uint64_t);

#ifdef LITTLE_ENDIAN
/*
 * on sn-ia we need to twiddle the the addresses going out
 * the pci bus because we use the unswizzled synergy space
 * (the alternative is to use the swizzled synergy space
 * and byte swap the data)
 */
#define CB(b,r) (((volatile uint8_t *) b)[((r)^4)])
#define CS(b,r) (((volatile uint16_t *) b)[((r^4)/2)])
#define CW(b,r) (((volatile uint32_t *) b)[((r^4)/4)])

#define	CBP(b,r) (((volatile uint8_t *) b)[(r)^3])
#define	CSP(b,r) (((volatile uint16_t *) b)[((r)/2)^1])
#define	CWP(b,r) (((volatile uint32_t *) b)[(r)/4])

#define SCB(b,r) (((volatile uint8_t *) b)[((r)^3)])
#define SCS(b,r) (((volatile uint16_t *) b)[((r^2)/2)])
#define SCW(b,r) (((volatile uint32_t *) b)[((r)/4)])
#else
#define	CB(b,r)	(((volatile uint8_t *) cfgbase)[(r)^3])
#define	CS(b,r)	(((volatile uint16_t *) cfgbase)[((r)/2)^1])
#define	CW(b,r)	(((volatile uint32_t *) cfgbase)[(r)/4])
#endif

/*
 * Return a config space address for given slot / func / offset.  Note the
 * returned pointer is a 32bit word (ie. cfg_p) aligned pointer pointing to
 * the 32bit word that contains the "offset" byte.
 */
cfg_p
pcibr_func_config_addr(bridge_t *bridge, pciio_bus_t bus, pciio_slot_t slot, 
					pciio_function_t func, int offset)
{
	/*
	 * Type 1 config space
	 */
	if (bus > 0) {
		bridge->b_pci_cfg = ((bus << 16) | (slot << 11));
		return &bridge->b_type1_cfg.f[func].l[(offset)];
	}

	/*
	 * Type 0 config space
	 */
	if (is_pic(bridge))
		slot++;
	return &bridge->b_type0_cfg_dev[slot].f[func].l[offset];
}

/*
 * Return config space address for given slot / offset.  Note the returned
 * pointer is a 32bit word (ie. cfg_p) aligned pointer pointing to the
 * 32bit word that contains the "offset" byte.
 */
cfg_p
pcibr_slot_config_addr(bridge_t *bridge, pciio_slot_t slot, int offset)
{
	return pcibr_func_config_addr(bridge, 0, slot, 0, offset);
}

/*
 * Return config space data for given slot / offset
 */
unsigned
pcibr_slot_config_get(bridge_t *bridge, pciio_slot_t slot, int offset)
{
	cfg_p  cfg_base;
	
	cfg_base = pcibr_slot_config_addr(bridge, slot, 0);
	return (do_pcibr_config_get(is_pic(bridge), cfg_base, offset, sizeof(unsigned)));
}

/*
 * Return config space data for given slot / func / offset
 */
unsigned
pcibr_func_config_get(bridge_t *bridge, pciio_slot_t slot, 
					pciio_function_t func, int offset)
{
	cfg_p  cfg_base;

	cfg_base = pcibr_func_config_addr(bridge, 0, slot, func, 0);
	return (do_pcibr_config_get(is_pic(bridge), cfg_base, offset, sizeof(unsigned)));
}

/*
 * Set config space data for given slot / offset
 */
void
pcibr_slot_config_set(bridge_t *bridge, pciio_slot_t slot, 
					int offset, unsigned val)
{
	cfg_p  cfg_base;

	cfg_base = pcibr_slot_config_addr(bridge, slot, 0);
	do_pcibr_config_set(is_pic(bridge), cfg_base, offset, sizeof(unsigned), val);
}

/*
 * Set config space data for given slot / func / offset
 */
void
pcibr_func_config_set(bridge_t *bridge, pciio_slot_t slot, 
			pciio_function_t func, int offset, unsigned val)
{
	cfg_p  cfg_base;

	cfg_base = pcibr_func_config_addr(bridge, 0, slot, func, 0);
	do_pcibr_config_set(is_pic(bridge), cfg_base, offset, sizeof(unsigned), val);
}

int pcibr_config_debug = 0;

cfg_p
pcibr_config_addr(devfs_handle_t conn,
		  unsigned reg)
{
    pcibr_info_t            pcibr_info;
    pciio_bus_t		    pciio_bus;
    pciio_slot_t            pciio_slot;
    pciio_function_t        pciio_func;
    pcibr_soft_t            pcibr_soft;
    bridge_t               *bridge;
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
#if 0
    } else if (conn != pciio_info_hostdev_get(pciio_info)) {
	/*
	 * Conn is on a subordinate bus, so get bus/slot/func directly from
	 * its pciio_info_t structure.
	 */
	pciio_bus = pciio_info->c_bus;
	pciio_slot = pciio_info->c_slot;
	pciio_func = pciio_info->c_func;
	if (pciio_func == PCIIO_FUNC_NONE) {
		pciio_func = 0;
	}
#endif
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

    pcibr_soft = (pcibr_soft_t) pcibr_info->f_mfast;

    bridge = pcibr_soft->bs_base;

    cfgbase = pcibr_func_config_addr(bridge,
			pciio_bus, pciio_slot, pciio_func, 0);

    return cfgbase;
}

extern unsigned char Is_pic_on_this_nasid[];
uint64_t
pcibr_config_get(devfs_handle_t conn,
		 unsigned reg,
		 unsigned size)
{
    if ( !Is_pic_on_this_nasid[ NASID_GET((pcibr_config_addr(conn, reg)))] )
    	return do_pcibr_config_get(0, pcibr_config_addr(conn, reg),
				PCI_TYPE1_REG(reg), size);
    else
    	return do_pcibr_config_get(1, pcibr_config_addr(conn, reg),
				PCI_TYPE1_REG(reg), size);
}

uint64_t
do_pcibr_config_get(
		       int pic,
		       cfg_p cfgbase,
		       unsigned reg,
		       unsigned size)
{
    unsigned                value;

    if ( pic ) {
	value = CWP(cfgbase, reg);
    }
    else {
	if ( io_get_sh_swapper(NASID_GET(cfgbase)) ) {
	    /*
	     * Shub Swapper on - 0 returns PCI Offset 0 but byte swapped!
	     * Do not swizzle address and byte swap the result.
	     */
	    value = SCW(cfgbase, reg);
	    value = __swab32(value);
	} else {
    	    value = CW(cfgbase, reg);
	}
    }
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
    if ( Is_pic_on_this_nasid[ NASID_GET((pcibr_config_addr(conn, reg)))] )
    	do_pcibr_config_set(1, pcibr_config_addr(conn, reg),
			PCI_TYPE1_REG(reg), size, value);
    else
	swap_do_pcibr_config_set(pcibr_config_addr(conn, reg),
			PCI_TYPE1_REG(reg), size, value);
}

void
do_pcibr_config_set(int pic,
		    cfg_p cfgbase,
		    unsigned reg,
		    unsigned size,
		    uint64_t value)
{
	if ( pic ) {
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
	else {
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
}

void
swap_do_pcibr_config_set(cfg_p cfgbase,
                    unsigned reg,
                    unsigned size,
                    uint64_t value)
{

    uint64_t temp_value = 0;

    switch (size) {
    case 1:
        SCB(cfgbase, reg) = value;
        break;
    case 2:
	temp_value = __swab16(value);
        if (reg & 1) {
            SCB(cfgbase, reg) = temp_value;
            SCB(cfgbase, reg + 1) = temp_value >> 8;
        } else
            SCS(cfgbase, reg) = temp_value;
        break;
    case 3:
	BUG();
        break;

    case 4:
	temp_value = __swab32(value);
        SCW(cfgbase, reg) = temp_value;
        break;
    }
}
