/*
 * include/asm-ppc/platforms/powerpmc250.h
 *
 * Definitions for Force PowerPMC-250 board support
 *
 * Author: Troy Benjegerdes <tbenjegerdes@mvista.com>
 *
 * Borrowed heavily from prpmc750.h by Matt Porter <mporter@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef __ASMPPC_POWERPMC250_H
#define __ASMPPC_POWERPMC250_H

#include <linux/serial_reg.h>

#define POWERPMC250_PCI_CONFIG_ADDR	0x80000cf8
#define POWERPMC250_PCI_CONFIG_DATA	0x80000cfc

#define POWERPMC250_PCI_PHY_MEM_BASE	0xc0000000
#define POWERPMC250_PCI_MEM_BASE		0xf0000000
#define POWERPMC250_PCI_IO_BASE		0x80000000

#define POWERPMC250_ISA_IO_BASE		POWERPMC250_PCI_IO_BASE
#define POWERPMC250_ISA_MEM_BASE		POWERPMC250_PCI_MEM_BASE
#define POWERPMC250_PCI_MEM_OFFSET		POWERPMC250_PCI_PHY_MEM_BASE

#define POWERPMC250_SYS_MEM_BASE		0x80000000

#define POWERPMC250_HAWK_SMC_BASE		0xfef80000

#define POWERPMC250_BASE_BAUD		12288000
#define POWERPMC250_SERIAL		0xff000000
#define POWERPMC250_SERIAL_IRQ		20

#endif /* __ASMPPC_POWERPMC250_H */
