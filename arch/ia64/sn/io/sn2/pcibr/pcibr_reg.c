/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 Silicon Graphics, Inc. All rights reserved.
 */


#include <linux/types.h>
#include <asm/sn/sgi.h>
#include <asm/sn/iograph.h>
#include <asm/sn/addrs.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/pci/pcibr_private.h>
#include <asm/sn/pci/pci_defs.h>

#define IS_IOADDR(ptr) (!(((uint64_t)(ptr) & CAC_BASE) == CAC_BASE))

/*
 * Control Register Access -- Read/Write                            0000_0020
 */

uint64_t
pcireg_control_get(void *ptr)
{
	uint64_t    ret = 0;
	pic_t       *bridge;

	if ( IS_IOADDR(ptr) )
		bridge = (pic_t *)ptr;
	else
		bridge = (pic_t *)((pcibr_soft_t)(ptr))->bs_base;

	ret = ((pic_t *)bridge)->p_wid_control;
	return ret;
}

void
pcireg_intr_enable_bit_clr(void *ptr, uint64_t bits)
{
	pic_t       *bridge;

	if ( IS_IOADDR(ptr) )
		bridge = (pic_t *)ptr;
	else
		bridge = (pic_t *)((pcibr_soft_t)(ptr))->bs_base;
	bridge->p_int_enable &= ~bits;
}

void
pcireg_intr_enable_bit_set(void *ptr, uint64_t bits)
{
	pic_t       *bridge;

	if ( IS_IOADDR(ptr) )
		bridge = (pic_t *)ptr;
	else
		bridge = (pic_t *)((pcibr_soft_t)(ptr))->bs_base;
	bridge->p_int_enable |= bits;
}

void
pcireg_intr_addr_addr_set(void *ptr, int int_n, uint64_t addr)
{
	pic_t       *bridge;

	if ( IS_IOADDR(ptr) )
		bridge = (pic_t *)ptr;
	else
		bridge = (pic_t *)((pcibr_soft_t)(ptr))->bs_base;
	bridge->p_int_addr[int_n] &= ~(0x0000FFFFFFFFFFFF);
	bridge->p_int_addr[int_n] |= (addr & 0x0000FFFFFFFFFFFF);
}

/*
 * Force Interrupt Register Access -- Write Only        0000_01C0 - 0000_01F8
 */
void
pcireg_force_intr_set(void *ptr, int int_n)
{
	pic_t       *bridge;

	if ( IS_IOADDR(ptr) )
		bridge = (pic_t *)ptr;
	else
		bridge = (pic_t *)((pcibr_soft_t)(ptr))->bs_base;
        bridge->p_force_pin[int_n] = 1;
}
