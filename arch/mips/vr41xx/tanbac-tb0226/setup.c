/*
 *  setup.c, Setup for the TANBAC TB0226.
 *
 *  Copyright (C) 2002-2004  Yoichi Yuasa <yuasa@hh.iij4u.or.jp>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/config.h>
#include <linux/ioport.h>

#include <asm/io.h>
#include <asm/pci_channel.h>
#include <asm/vr41xx/tb0226.h>

#ifdef CONFIG_PCI
static struct resource vr41xx_pci_io_resource = {
	.name	= "PCI I/O space",
	.start	= VR41XX_PCI_IO_START,
	.end	= VR41XX_PCI_IO_END,
	.flags	= IORESOURCE_IO,
};

static struct resource vr41xx_pci_mem_resource = {
	.name	= "PCI memory space",
	.start	= VR41XX_PCI_MEM_START,
	.end	= VR41XX_PCI_MEM_END,
	.flags	= IORESOURCE_MEM,
};

extern struct pci_ops vr41xx_pci_ops;

struct pci_controller vr41xx_controller[] = {
	.pci_ops	= &vr41xx_pci_ops,
	.io_resource	= &vr41xx_pci_io_resource,
	.mem_resource	= &vr41xx_pci_mem_resource,
};

struct vr41xx_pci_address_space vr41xx_pci_mem1 = {
	.internal_base	= VR41XX_PCI_MEM1_BASE,
	.address_mask	= VR41XX_PCI_MEM1_MASK,
	.pci_base	= IO_MEM1_RESOURCE_START,
};

struct vr41xx_pci_address_space vr41xx_pci_mem2 = {
	.internal_base	= VR41XX_PCI_MEM2_BASE,
	.address_mask	= VR41XX_PCI_MEM2_MASK,
	.pci_base	= IO_MEM2_RESOURCE_START,
};

struct vr41xx_pci_address_space vr41xx_pci_io = {
	.internal_base	= VR41XX_PCI_IO_BASE,
	.address_mask	= VR41XX_PCI_IO_MASK,
	.pci_base	= IO_PORT_RESOURCE_START,
};

static struct vr41xx_pci_address_map pci_address_map = {
	.mem1	= &vr41xx_pci_mem1,
	.mem2	= &vr41xx_pci_mem2,
	.io	= &vr41xx_pci_io,
};
#endif

const char *get_system_type(void)
{
	return "TANBAC TB0226";
}

static int tanbac_tb0226_setup(void)
{
	set_io_port_base(IO_PORT_BASE);
	ioport_resource.start = IO_PORT_RESOURCE_START;
	ioport_resource.end = IO_PORT_RESOURCE_END;

#ifdef CONFIG_SERIAL_8250
	vr41xx_select_siu_interface(SIU_RS232C, IRDA_NONE);
	vr41xx_siu_init();
#endif

#ifdef CONFIG_PCI
	vr41xx_pciu_init(&pci_address_map);
#endif

	return 0;
}

early_initcall(tanbac_tb0226_setup);
