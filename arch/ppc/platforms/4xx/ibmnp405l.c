/*
 * arch/ppc/platforms/4xx/ibmnp405l.c
 *
 * Author: Armin Kuster <akuster@mvista.com>
 *
 * 2000-2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/threads.h>
#include <linux/param.h>
#include <linux/string.h>
#include <platforms/4xx/ibmnp405l.h>

struct ocp_def core_ocp[] = {
	{UART, UART0_IO_BASE, UART0_INT, IBM_CPM_UART0},
	{UART, UART1_IO_BASE, UART1_INT, IBM_CPM_UART1},
	{IIC, IIC0_BASE, IIC0_IRQ, IBM_CPM_IIC0},
	{GPIO, GPIO0_BASE, OCP_IRQ_NA, IBM_CPM_GPIO0},
	{OPB, OPB0_BASE, OCP_IRQ_NA, IBM_CPM_OPB},
	{EMAC, EMAC0_BASE, BL_MAC_ETH0, IBM_CPM_EMAC0},
	{EMAC, EMAC1_BASE, BL_MAC_ETH1, IBM_CPM_EMAC1},
	{ZMII, ZMII0_BASE, OCP_IRQ_NA, OCP_CPM_NA},
	{OCP_NULL_TYPE, 0x0, OCP_IRQ_NA, OCP_CPM_NA},

};
