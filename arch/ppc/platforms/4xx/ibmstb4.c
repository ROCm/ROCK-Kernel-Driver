/*
 * arch/ppc/platforms/4xx/ibmstb4.c
 *
 * Author: Armin Kuster <akuster@mvista.com>
 *
 * 2000-2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/config.h>
#include <linux/module.h>
#include "ibmstb4.h"
#include <asm/ocp.h>

struct ocp_def core_ocp[] = {
	{UART, UART0_IO_BASE, UART0_INT,IBM_CPM_UART0},
	{UART, UART1_IO_BASE, UART1_INT, IBM_CPM_UART1},
	{UART, UART2_IO_BASE, UART2_INT, IBM_CPM_UART2},
	{IIC, IIC0_BASE, IIC0_IRQ, IBM_CPM_IIC0},
	{IIC, IIC1_BASE, IIC1_IRQ, IBM_CPM_IIC1},
	{GPIO, GPIO0_BASE, OCP_IRQ_NA, IBM_CPM_GPIO0},
	{IDE, IDE0_BASE, IDE0_IRQ, OCP_CPM_NA},
	{USB, USB0_BASE, USB0_IRQ, IBM_CPM_USB0},
	{OCP_NULL_TYPE, 0x0, OCP_IRQ_NA, OCP_CPM_NA},
};
