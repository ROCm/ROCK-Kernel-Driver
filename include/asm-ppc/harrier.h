/*
 * include/asm-ppc/harrier.h
 *
 * Definitions for Motorola MCG Harrier North Bridge & Memory controller
 *
 * Author: Dale Farnsworth
 *         dale.farnsworth@mvista.com
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef __ASMPPC_HARRIER_H
#define __ASMPPC_HARRIER_H

#include <asm/pci-bridge.h>

#define	HARRIER_VEND_DEV_ID			0x480b1057

/*
 * Define outbound register offsets.
 */
#define HARRIER_OTAD0_OFF			0x220
#define HARRIER_OTOF0_OFF			0x224
#define HARRIER_OTAD1_OFF			0x228
#define HARRIER_OTOF1_OFF			0x22c
#define HARRIER_OTAD2_OFF			0x230
#define HARRIER_OTOF2_OFF			0x234
#define HARRIER_OTAD3_OFF			0x238
#define HARRIER_OTOF3_OFF			0x23c

/*
 * Define inbound register offsets.
 */
#define HARRIER_ITSZ0_OFF			0x348
#define HARRIER_ITSZ1_OFF			0x350
#define HARRIER_ITSZ2_OFF			0x358
#define HARRIER_ITSZ3_OFF			0x360

/*
 * Define the Memory Controller register offsets.
 */
#define HARRIER_SDBA_OFF			0x110
#define HARRIER_SDBB_OFF			0x114
#define HARRIER_SDBC_OFF			0x118
#define HARRIER_SDBD_OFF			0x11c
#define HARRIER_SDBE_OFF			0x120
#define HARRIER_SDBF_OFF			0x124
#define HARRIER_SDBG_OFF			0x128
#define HARRIER_SDBH_OFF			0x12c

#define HARRIER_SDB_ENABLE			0x00000100
#define HARRIER_SDB_SIZE_MASK			0xf
#define HARRIER_SDB_SIZE_SHIFT			16
#define HARRIER_SDB_BASE_MASK			0xff
#define HARRIER_SDB_BASE_SHIFT			24

#define HARRIER_SERIAL_0_OFF			0xc0

#define HARRIER_REVI_OFF			0x05
#define HARRIER_UCTL_OFF			0xd0
#define HARRIER_XTAL64_MASK			0x02

#define HARRIER_MISC_CSR_OFF			0x1c
#define HARRIER_RSTOUT_MASK			0x01

#define HARRIER_MBAR_OFF			0xe0
#define HARRIER_MPIC_CSR_OFF			0xe4
#define HARRIER_MPIC_OPI_ENABLE			0x40
#define HARRIER_MPIC_IFEVP_OFF			0x10200
#define HARRIER_MPIC_IFEDE_OFF			0x10210
#define HARRIER_FEEN_OFF			0x40
#define HARRIER_FEST_OFF			0x44
#define HARRIER_FEMA_OFF			0x48

#define HARRIER_FE_DMA				0x80
#define HARRIER_FE_MIDB				0x40
#define HARRIER_FE_MIM0				0x20
#define HARRIER_FE_MIM1				0x10
#define HARRIER_FE_MIP				0x08
#define HARRIER_FE_UA0				0x04
#define HARRIER_FE_UA1				0x02
#define HARRIER_FE_ABT				0x01


int harrier_init(struct pci_controller *hose,
		 uint ppc_reg_base,
		 ulong processor_pci_mem_start,
		 ulong processor_pci_mem_end,
		 ulong processor_pci_io_start,
		 ulong processor_pci_io_end,
		 ulong processor_mpic_base);

unsigned long harrier_get_mem_size(uint smc_base);

int harrier_mpic_init(unsigned int pci_mem_offset);

#endif /* __ASMPPC_HARRIER_H */
