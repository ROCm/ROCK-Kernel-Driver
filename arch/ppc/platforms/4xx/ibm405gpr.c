/*
 * arch/ppc/platforms/4xx/ibm405gpr.c
 *
 * Author: Armin Kuster <akuster@mvista.com>
 *
 * 2002 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <asm/ocp.h>

#include "ibm405gpr.h"

struct ocp_def core_ocp[]  __initdata = {
	{OCP_VENDOR_IBM, OCP_FUNC_OPB, OPB_BASE_START, OCP_IRQ_NA, OCP_CPM_NA},
	{OCP_VENDOR_IBM, OCP_FUNC_16550, UART0_IO_BASE, UART0_INT,IBM_CPM_UART0},
	{OCP_VENDOR_IBM, OCP_FUNC_16550, UART1_IO_BASE, UART1_INT, IBM_CPM_UART1},
	{OCP_VENDOR_IBM, OCP_FUNC_IIC, IIC0_BASE, IIC0_IRQ, IBM_CPM_IIC0},
	{OCP_VENDOR_IBM, OCP_FUNC_GPIO, GPIO0_BASE, OCP_IRQ_NA, IBM_CPM_GPIO0},
	{OCP_VENDOR_IBM, OCP_FUNC_EMAC, EMAC0_BASE, BL_MAC_ETH0, IBM_CPM_EMAC0},
	{OCP_VENDOR_INVALID, OCP_FUNC_INVALID, 0x0, OCP_IRQ_NA, OCP_CPM_NA},

};
