/*
 * pSeries_pci.c
 *
 * Copyright (C) 2001 Dave Engebretsen, IBM Corporation
 * Copyright (C) 2003 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 * pSeries specific routines for PCI.
 * 
 * Based on code from pci.c and chrp_pci.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *    
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/ppcdebug.h>
#include <asm/naca.h>
#include <asm/pci_dma.h>

#include "open_pic.h"
#include "pci.h"

/* RTAS tokens */
static int read_pci_config;
static int write_pci_config;
static int ibm_read_pci_config;
static int ibm_write_pci_config;

static int s7a_workaround;

extern unsigned long pci_probe_only;

static int rtas_read_config(struct device_node *dn, int where, int size, u32 *val)
{
	unsigned long returnval = ~0L;
	unsigned long buid, addr;
	int ret;

	if (!dn)
		return -2;

	addr = (dn->busno << 16) | (dn->devfn << 8) | where;
	buid = dn->phb->buid;
	if (buid) {
		ret = rtas_call(ibm_read_pci_config, 4, 2, &returnval, addr, buid >> 32, buid & 0xffffffff, size);
	} else {
		ret = rtas_call(read_pci_config, 2, 2, &returnval, addr, size);
	}
	*val = returnval;
	return ret;
}

static int rtas_pci_read_config(struct pci_bus *bus,
				unsigned int devfn,
				int where, int size, u32 *val)
{
	struct device_node *busdn, *dn;

	if (bus->self)
		busdn = pci_device_to_OF_node(bus->self);
	else
		busdn = bus->sysdata;	/* must be a phb */

	/* Search only direct children of the bus */
	for (dn = busdn->child; dn; dn = dn->sibling)
		if (dn->devfn == devfn)
			return rtas_read_config(dn, where, size, val);
	return PCIBIOS_DEVICE_NOT_FOUND;
}

static int rtas_write_config(struct device_node *dn, int where, int size, u32 val)
{
	unsigned long buid, addr;
	int ret;

	if (!dn)
		return -2;

	addr = (dn->busno << 16) | (dn->devfn << 8) | where;
	buid = dn->phb->buid;
	if (buid) {
		ret = rtas_call(ibm_write_pci_config, 5, 1, NULL, addr, buid >> 32, buid & 0xffffffff, size, (ulong) val);
	} else {
		ret = rtas_call(write_pci_config, 3, 1, NULL, addr, size, (ulong)val);
	}
	return ret;
}

static int rtas_pci_write_config(struct pci_bus *bus,
				 unsigned int devfn,
				 int where, int size, u32 val)
{
	struct device_node *busdn, *dn;

	if (bus->self)
		busdn = pci_device_to_OF_node(bus->self);
	else
		busdn = bus->sysdata;	/* must be a phb */

	/* Search only direct children of the bus */
	for (dn = busdn->child; dn; dn = dn->sibling)
		if (dn->devfn == devfn)
			return rtas_write_config(dn, where, size, val);
	return PCIBIOS_DEVICE_NOT_FOUND;
}

struct pci_ops rtas_pci_ops = {
	rtas_pci_read_config,
	rtas_pci_write_config
};

/******************************************************************
 * pci_read_irq_line
 *
 * Reads the Interrupt Pin to determine if interrupt is use by card.
 * If the interrupt is used, then gets the interrupt line from the 
 * openfirmware and sets it in the pci_dev and pci_config line.
 *
 ******************************************************************/
int pci_read_irq_line(struct pci_dev *pci_dev)
{
	u8 intpin;
	struct device_node *node;

    	pci_read_config_byte(pci_dev, PCI_INTERRUPT_PIN, &intpin);

	if (intpin == 0) {
		PPCDBG(PPCDBG_BUSWALK,"\tDevice: %s No Interrupt used by device.\n", pci_name(pci_dev));
		return 0;	
	}

	node = pci_device_to_OF_node(pci_dev);
	if (node == NULL) { 
		PPCDBG(PPCDBG_BUSWALK,"\tDevice: %s Device Node not found.\n",
		       pci_name(pci_dev));
		return -1;	
	}
	if (node->n_intrs == 0) 	{
		PPCDBG(PPCDBG_BUSWALK,"\tDevice: %s No Device OF interrupts defined.\n", pci_name(pci_dev));
		return -1;	
	}
	pci_dev->irq = node->intrs[0].line;

	if (s7a_workaround) {
		if (pci_dev->irq > 16)
			pci_dev->irq -= 3;
	}

	pci_write_config_byte(pci_dev, PCI_INTERRUPT_LINE, pci_dev->irq);
	
	PPCDBG(PPCDBG_BUSWALK,"\tDevice: %s pci_dev->irq = 0x%02X\n",
	       pci_name(pci_dev), pci_dev->irq);
	return 0;
}

static void __init pci_process_bridge_OF_ranges(struct pci_controller *hose,
						struct device_node *dev,
						int primary)
{
	unsigned int *ranges;
	unsigned long size;
	int rlen = 0;
	int memno = 0;
	struct resource *res;
	int np, na = prom_n_addr_cells(dev);
	unsigned long pci_addr, cpu_phys_addr;

	np = na + 5;

	/*
	 * The ranges property is laid out as an array of elements,
	 * each of which comprises:
	 *   cells 0 - 2:	a PCI address
	 *   cells 3 or 3+4:	a CPU physical address
	 *			(size depending on dev->n_addr_cells)
	 *   cells 4+5 or 5+6:	the size of the range
	 */
	rlen = 0;
	hose->io_base_phys = 0;
	ranges = (unsigned int *) get_property(dev, "ranges", &rlen);
	while ((rlen -= np * sizeof(unsigned int)) >= 0) {
		res = NULL;
		pci_addr = (unsigned long)ranges[1] << 32 | ranges[2];

		cpu_phys_addr = ranges[3];
		if (na == 2)
			cpu_phys_addr = cpu_phys_addr << 32 | ranges[4];

		size = (unsigned long)ranges[na+3] << 32 | ranges[na+4];

		switch (ranges[0] >> 24) {
		case 1:		/* I/O space */
			hose->io_base_phys = cpu_phys_addr;
			hose->io_base_virt = __ioremap(hose->io_base_phys,
						       size, _PAGE_NO_CACHE);
			if (primary) {
				pci_io_base = (unsigned long)hose->io_base_virt;
				if (find_type_devices("isa"))
					isa_io_base = pci_io_base;
			}

			res = &hose->io_resource;
			res->flags = IORESOURCE_IO;
			res->start = pci_addr;
			res->start += (unsigned long)hose->io_base_virt -
				pci_io_base;
			break;
		case 2:		/* memory space */
			memno = 0;
			while (memno < 3 && hose->mem_resources[memno].flags)
				++memno;

			if (memno == 0)
				hose->pci_mem_offset = cpu_phys_addr - pci_addr;
			if (memno < 3) {
				res = &hose->mem_resources[memno];
				res->flags = IORESOURCE_MEM;
				res->start = cpu_phys_addr;
			}
			break;
		}
		if (res != NULL) {
			res->name = dev->full_name;
			res->end = res->start + size - 1;
			res->parent = NULL;
			res->sibling = NULL;
			res->child = NULL;
		}
		ranges += np;
	}
}

static void python_countermeasures(unsigned long addr)
{
	void *chip_regs;
	volatile u32 *tmp, i;

	/* Python's register file is 1 MB in size. */
	chip_regs = ioremap(addr & ~(0xfffffUL), 0x100000); 

	/* 
	 * Firmware doesn't always clear this bit which is critical
	 * for good performance - Anton
	 */

#define PRG_CL_RESET_VALID 0x00010000

	tmp = (u32 *)((unsigned long)chip_regs + 0xf6030);

	if (*tmp & PRG_CL_RESET_VALID) {
		printk(KERN_INFO "Python workaround: ");
		*tmp &= ~PRG_CL_RESET_VALID;
		/*
		 * We must read it back for changes to
		 * take effect
		 */
		i = *tmp;
		printk("reg0: %x\n", i);
	}

	iounmap(chip_regs);
}

struct pci_controller *alloc_phb(struct device_node *dev,
				 unsigned int addr_size_words)
{
	struct pci_controller *phb;
	unsigned int *ui_ptr = NULL, len;
	struct reg_property64 reg_struct;
	int *bus_range;
	int *buid_vals;
	char *model;
	enum phb_types phb_type;
 	struct property *of_prop;

	model = (char *)get_property(dev, "model", NULL);

	if (!model) {
		printk(KERN_ERR "alloc_phb: phb has no model property\n");
		model = "<empty>";
	}

	/* Found a PHB, now figure out where his registers are mapped. */
	ui_ptr = (unsigned int *) get_property(dev, "reg", &len);
	if (ui_ptr == NULL) {
		PPCDBG(PPCDBG_PHBINIT, "\tget reg failed.\n"); 
		return NULL;
	}

	if (addr_size_words == 1) {
		reg_struct.address = ((struct reg_property32 *)ui_ptr)->address;
		reg_struct.size    = ((struct reg_property32 *)ui_ptr)->size;
	} else {
		reg_struct = *((struct reg_property64 *)ui_ptr);
	}

	if (strstr(model, "Python")) {
		phb_type = phb_type_python;
	} else if (strstr(model, "Speedwagon")) {
		phb_type = phb_type_speedwagon;
	} else if (strstr(model, "Winnipeg")) {
		phb_type = phb_type_winnipeg;
	} else {
		printk(KERN_ERR "alloc_phb: unknown PHB %s\n", model);
		phb_type = phb_type_unknown;
	}

	phb = pci_alloc_pci_controller(phb_type);
	if (phb == NULL)
		return NULL;

	if (phb_type == phb_type_python)
		python_countermeasures(reg_struct.address);

	bus_range = (int *) get_property(dev, "bus-range", &len);
	if (bus_range == NULL || len < 2 * sizeof(int)) {
		kfree(phb);
		return NULL;
	}

	of_prop = (struct property *)alloc_bootmem(sizeof(struct property) +
			sizeof(phb->global_number));        

	if (!of_prop) {
		kfree(phb);
		return NULL;
	}

	memset(of_prop, 0, sizeof(struct property));
	of_prop->name = "linux,pci-domain";
	of_prop->length = sizeof(phb->global_number);
	of_prop->value = (unsigned char *)&of_prop[1];
	memcpy(of_prop->value, &phb->global_number, sizeof(phb->global_number));
	prom_add_property(dev, of_prop);

	phb->first_busno =  bus_range[0];
	phb->last_busno  =  bus_range[1];

	phb->arch_data   = dev;
	phb->ops = &rtas_pci_ops;

	buid_vals = (int *) get_property(dev, "ibm,fw-phb-id", &len);

	if (buid_vals == NULL) {
		phb->buid = 0;
	} else {
		if (len < 2 * sizeof(int))
			// Support for new OF that only has 1 integer for buid.
			phb->buid = (unsigned long)buid_vals[0];
		else
			phb->buid = (((unsigned long)buid_vals[0]) << 32UL) |
				(((unsigned long)buid_vals[1]) & 0xffffffff);
	}

	return phb;
}

unsigned long __init find_and_init_phbs(void)
{
	struct device_node *node;
	struct pci_controller *phb;
	unsigned int root_size_cells = 0;
	unsigned int index;
	unsigned int *opprop;
	struct device_node *root = find_path_device("/");

	read_pci_config = rtas_token("read-pci-config");
	write_pci_config = rtas_token("write-pci-config");
	ibm_read_pci_config = rtas_token("ibm,read-pci-config");
	ibm_write_pci_config = rtas_token("ibm,write-pci-config");

	if (naca->interrupt_controller == IC_OPEN_PIC) {
		opprop = (unsigned int *)get_property(root,
				"platform-open-pic", NULL);
	}

	root_size_cells = prom_n_size_cells(root);

	index = 0;

	for (node = root->child; node != NULL; node = node->sibling) {
		if (node->type == NULL || strcmp(node->type, "pci") != 0)
			continue;

		phb = alloc_phb(node, root_size_cells);
		if (!phb)
			continue;

		pci_process_bridge_OF_ranges(phb, node, index == 0);

		if (naca->interrupt_controller == IC_OPEN_PIC) {
			int addr = root_size_cells * (index + 2) - 1;
			openpic_setup_ISU(index, opprop[addr]); 
		}

		index++;
	}

	pci_devs_phb_init();

	return 0;
}

void pcibios_name_device(struct pci_dev *dev)
{
#if 0
	struct device_node *dn;

	/*
	 * Add IBM loc code (slot) as a prefix to the device names for service
	 */
	dn = pci_device_to_OF_node(dev);
	if (dn) {
		char *loc_code = get_property(dn, "ibm,loc-code", 0);
		if (loc_code) {
			int loc_len = strlen(loc_code);
			if (loc_len < sizeof(dev->dev.name)) {
				memmove(dev->dev.name+loc_len+1, dev->dev.name,
					sizeof(dev->dev.name)-loc_len-1);
				memcpy(dev->dev.name, loc_code, loc_len);
				dev->dev.name[loc_len] = ' ';
				dev->dev.name[sizeof(dev->dev.name)-1] = '\0';
			}
		}
	}
#endif
}   

void __init pcibios_fixup_device_resources(struct pci_dev *dev,
					   struct pci_bus *bus)
{
	/* Update device resources.  */
	struct pci_controller *hose = PCI_GET_PHB_PTR(bus);
	int i;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		if (dev->resource[i].flags & IORESOURCE_IO) {
			unsigned long offset = (unsigned long)hose->io_base_virt - pci_io_base;
			dev->resource[i].start += offset;
			dev->resource[i].end += offset;
		}
                else if (dev->resource[i].flags & IORESOURCE_MEM) {
			dev->resource[i].start += hose->pci_mem_offset;
			dev->resource[i].end += hose->pci_mem_offset;
		}
        }
}

void __init pcibios_fixup_bus(struct pci_bus *bus)
{
	struct pci_controller *hose = PCI_GET_PHB_PTR(bus);
	struct list_head *ln;

	/* XXX or bus->parent? */
	struct pci_dev *dev = bus->self;
	struct resource *res;
	int i;

	if (!dev) {
		/* Root bus. */

		hose->bus = bus;
		bus->resource[0] = res = &hose->io_resource;
		if (!res->flags)
			BUG();	/* No I/O resource for this PHB? */

		if (request_resource(&ioport_resource, res))
			printk(KERN_ERR "Failed to request IO"
					"on hose %d\n", 0 /* FIXME */);

		for (i = 0; i < 3; ++i) {
			res = &hose->mem_resources[i];
			if (!res->flags && i == 0)
				BUG();	/* No memory resource for this PHB? */
			bus->resource[i+1] = res;
			if (res->flags && request_resource(&iomem_resource, res))
				printk(KERN_ERR "Failed to request MEM"
						"on hose %d\n", 0 /* FIXME */);
		}
	} else if (pci_probe_only &&
		   (dev->class >> 8) == PCI_CLASS_BRIDGE_PCI) {
		/* This is a subordinate bridge */

		pci_read_bridge_bases(bus);
		pcibios_fixup_device_resources(dev, bus);
	}

	/* XXX Need to check why Alpha doesnt do this - Anton */
	if (!pci_probe_only)
		return;

	for (ln = bus->devices.next; ln != &bus->devices; ln = ln->next) {
		struct pci_dev *dev = pci_dev_b(ln);
		if ((dev->class >> 8) != PCI_CLASS_BRIDGE_PCI)
			pcibios_fixup_device_resources(dev, bus);
	}
}

static void check_s7a(void)
{
	struct device_node *root;
	char *model;

	root = find_path_device("/");
	if (root) {
		model = get_property(root, "model", NULL);
		if (model && !strcmp(model, "IBM,7013-S7A"))
			s7a_workaround = 1;
	}
}

extern void chrp_request_regions(void);

void __init pcibios_final_fixup(void)
{
	struct pci_dev *dev = NULL;

	check_s7a();

	while ((dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL)
		pci_read_irq_line(dev);

	chrp_request_regions();
	pci_fix_bus_sysdata();
	create_tce_tables();
}

/*********************************************************************** 
 * pci_find_hose_for_OF_device
 *
 * This function finds the PHB that matching device_node in the 
 * OpenFirmware by scanning all the pci_controllers.
 * 
 ***********************************************************************/
struct pci_controller*
pci_find_hose_for_OF_device(struct device_node *node)
{
	while (node) {
		struct pci_controller *hose;
		for (hose=hose_head;hose;hose=hose->next)
			if (hose->arch_data == node)
				return hose;
		node=node->parent;
	}
	return NULL;
}
