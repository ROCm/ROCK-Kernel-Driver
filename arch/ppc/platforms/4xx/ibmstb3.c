/*
 * arch/ppc/platforms/4xx/ibmstb3.c
 *
 * Author: Armin Kuster <akuster@mvista.com>
 *
 * 2000-2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/config.h>
#include "ibmstb3.h"
#include <asm/ocp.h>

struct ocp_def core_ocp[] = {
	{UART, UART0_IO_BASE, UART0_INT, IBM_CPM_UART0},
	{IIC, IIC0_BASE, IIC0_IRQ, IBM_CPM_IIC0},
	{IIC, IIC1_BASE, IIC1_IRQ, IBM_CPM_IIC1},
	{GPIO, GPIO0_BASE, OCP_IRQ_NA, IBM_CPM_GPIO0},
	{OPB, OPB0_BASE, OCP_IRQ_NA, OCP_CPM_NA},
	{OCP_NULL_TYPE, 0x0, OCP_IRQ_NA, OCP_CPM_NA},

};
