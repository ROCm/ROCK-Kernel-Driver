/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004 by Ralf Baechle
 *
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <asm/gt64240.h>
#include <asm/pci_channel.h>

extern struct pci_ops titan_pci_ops;

static struct resource py_mem_resource = {
	"Titan PCI MEM", 0xe0000000UL, 0xe3ffffffUL, IORESOURCE_MEM
};

static struct resource py_io_resource = {
	"Titan IO MEM", 0x00000000UL, 0x00ffffffUL, IORESOURCE_IO,
};

static struct pci_controller py_controller = {
	.pci_ops	= &titan_pci_ops,
	.mem_resource	= &py_mem_resource,
	.mem_offset	= 0x10000000UL,
	.io_resource	= &py_io_resource,
	.io_offset	= 0x00000000UL
};

static int __init pmc_yosemite_setup(void)
{
	register_pci_controller(&py_controller);
}
