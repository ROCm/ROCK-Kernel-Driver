/*
 * arch/ppc/platforms/4xx/ibm440gp.c
 *
 * PPC440GP I/O descriptions
 *
 * Matt Porter <mporter@mvista.com>
 *
 * Copyright 2002 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <platforms/4xx/ibm440gp.h>
#include <asm/ocp.h>
#include <linux/init.h>

struct ocp_def core_ocp[] __initdata = {
	{OCP_VENDOR_IBM, OCP_FUNC_OPB, PPC440GP_OPB_BASE_START, OCP_IRQ_NA, OCP_CPM_NA},
	{OCP_VENDOR_IBM, OCP_FUNC_16550, PPC440GP_UART0_ADDR, UART0_INT, IBM_CPM_UART0},
	{OCP_VENDOR_IBM, OCP_FUNC_16550, PPC440GP_UART1_ADDR, UART1_INT, IBM_CPM_UART1},
	{OCP_VENDOR_IBM, OCP_FUNC_IIC, PPC440GP_IIC0_ADDR, IIC0_IRQ, IBM_CPM_IIC0},
	{OCP_VENDOR_IBM, OCP_FUNC_IIC, PPC440GP_IIC1_ADDR, IIC1_IRQ, IBM_CPM_IIC1},
	{OCP_VENDOR_IBM, OCP_FUNC_GPIO, PPC440GP_GPIO0_ADDR, OCP_IRQ_NA, IBM_CPM_GPIO0},
	{OCP_VENDOR_IBM, OCP_FUNC_EMAC, PPC440GP_EMAC0_ADDR, BL_MAC_ETH0, OCP_CPM_NA},
	{OCP_VENDOR_IBM, OCP_FUNC_EMAC, PPC440GP_EMAC1_ADDR, BL_MAC_ETH1, OCP_CPM_NA},
	{OCP_VENDOR_IBM, OCP_FUNC_ZMII, PPC440GP_ZMII_ADDR, OCP_IRQ_NA, OCP_CPM_NA},
	{OCP_VENDOR_INVALID, OCP_FUNC_INVALID, 0x0, OCP_IRQ_NA, OCP_CPM_NA},
};
