/*
 * arch/ppc64/kernel/pSeries_iommu.c
 *
 * Copyright (C) 2001 Mike Corrigan & Dave Engebretsen, IBM Corporation
 *
 * Rewrite, cleanup: 
 *
 * Copyright (C) 2004 Olof Johansson <olof@austin.ibm.com>, IBM Corporation
 *
 * Dynamic DMA mapping support, pSeries-specific parts, both SMP and LPAR.
 *
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

#include <linux/config.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/ppcdebug.h>
#include <asm/iommu.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/abs_addr.h>
#include "pci.h"


static void tce_build_pSeries(struct iommu_table *tbl, long index, 
			      long npages, unsigned long uaddr, 
			      enum dma_data_direction direction)
{
	union tce_entry t;
	union tce_entry *tp;

	t.te_word = 0;
	t.te_rdwr = 1; // Read allowed 

	if (direction != DMA_TO_DEVICE)
		t.te_pciwr = 1;

	tp = ((union tce_entry *)tbl->it_base) + index;

	while (npages--) {
		/* can't move this out since we might cross LMB boundary */
		t.te_rpn = (virt_to_abs(uaddr)) >> PAGE_SHIFT;
	
		tp->te_word = t.te_word;

		uaddr += PAGE_SIZE;
		tp++;
	}
}


static void tce_free_pSeries(struct iommu_table *tbl, long index, long npages)
{
	union tce_entry t;
	union tce_entry *tp;

	t.te_word = 0;
	tp  = ((union tce_entry *)tbl->it_base) + index;
		
	while (npages--) {
		tp->te_word = t.te_word;
		
		tp++;
	}
}


static void iommu_buses_init(void)
{
	struct pci_controller* phb;
	struct device_node *dn, *first_dn;
	int num_slots, num_slots_ilog2;
	int first_phb = 1;
	unsigned long tcetable_ilog2;

	/*
	 * We default to a TCE table that maps 2GB (4MB table, 22 bits),
	 * however some machines have a 3GB IO hole and for these we
	 * create a table that maps 1GB (2MB table, 21 bits)
	 */
	if (io_hole_start < 0x80000000UL)
		tcetable_ilog2 = 21;
	else
		tcetable_ilog2 = 22;

	/* XXX Should we be using pci_root_buses instead?  -ojn 
	 */

	for (phb=hose_head; phb; phb=phb->next) {
		first_dn = ((struct device_node *)phb->arch_data)->child;

		/* Carve 2GB into the largest dma_window_size possible */
		for (dn = first_dn, num_slots = 0; dn != NULL; dn = dn->sibling)
			num_slots++;
		num_slots_ilog2 = __ilog2(num_slots);

		if ((1<<num_slots_ilog2) != num_slots)
			num_slots_ilog2++;

		phb->dma_window_size = 1 << (tcetable_ilog2 - num_slots_ilog2);

		/* Reserve 16MB of DMA space on the first PHB.
		 * We should probably be more careful and use firmware props.
		 * In reality this space is remapped, not lost.  But we don't
		 * want to get that smart to handle it -- too much work.
		 */
		phb->dma_window_base_cur = first_phb ? (1 << 12) : 0;
		first_phb = 0;

		for (dn = first_dn; dn != NULL; dn = dn->sibling)
			iommu_devnode_init(dn);
	}
}


static void iommu_buses_init_lpar(struct list_head *bus_list)
{
	struct list_head *ln;
	struct pci_bus *bus;
	struct device_node *busdn;
	unsigned int *dma_window;

	for (ln=bus_list->next; ln != bus_list; ln=ln->next) {
		bus = pci_bus_b(ln);
		busdn = PCI_GET_DN(bus);

		dma_window = (unsigned int *)get_property(busdn, "ibm,dma-window", NULL);
		if (dma_window) {
			/* Bussubno hasn't been copied yet.
			 * Do it now because iommu_table_setparms_lpar needs it.
			 */
			busdn->bussubno = bus->number;
			iommu_devnode_init(busdn);
		}

		/* look for a window on a bridge even if the PHB had one */
		iommu_buses_init_lpar(&bus->children);
	}
}


static void iommu_table_setparms(struct pci_controller *phb,
				 struct device_node *dn,
				 struct iommu_table *tbl) 
{
	phandle node;
	unsigned long i;
	struct of_tce_table *oft;

	node = ((struct device_node *)(phb->arch_data))->node;

	oft = NULL;

	for (i=0; of_tce_table[i].node; i++)
		if(of_tce_table[i].node == node) {
			oft = &of_tce_table[i];
			break;
		}

	if (!oft)
		panic("PCI_DMA: iommu_table_setparms: Can't find phb named '%s' in of_tce_table\n", dn->full_name);

	memset((void *)oft->base, 0, oft->size);

	tbl->it_busno = phb->bus->number;
	
	/* Units of tce entries */
	tbl->it_offset = phb->dma_window_base_cur;
	
	/* Adjust the current table offset to the next
	 * region.  Measured in TCE entries. Force an
	 * alignment to the size allotted per IOA. This
	 * makes it easier to remove the 1st 16MB.
      	 */
	phb->dma_window_base_cur += (phb->dma_window_size>>3);
	phb->dma_window_base_cur &= 
		~((phb->dma_window_size>>3)-1);
	
	/* Set the tce table size - measured in pages */
	tbl->it_size = ((phb->dma_window_base_cur -
			 tbl->it_offset) << 3) >> PAGE_SHIFT;
	
	/* Test if we are going over 2GB of DMA space */
	if (phb->dma_window_base_cur > (1 << 19))
		panic("PCI_DMA: Unexpected number of IOAs under this PHB.\n"); 
	
	tbl->it_base = oft->base;
	tbl->it_index = 0;
	tbl->it_entrysize = sizeof(union tce_entry);
	tbl->it_blocksize = 16;
}

/*
 * iommu_table_setparms_lpar
 *
 * Function: On pSeries LPAR systems, return TCE table info, given a pci bus.
 *
 * ToDo: properly interpret the ibm,dma-window property.  The definition is:
 *	logical-bus-number	(1 word)
 *	phys-address		(#address-cells words)
 *	size			(#cell-size words)
 *
 * Currently we hard code these sizes (more or less).
 */
static void iommu_table_setparms_lpar(struct pci_controller *phb,
				      struct device_node *dn,
				      struct iommu_table *tbl)
{
	unsigned int *dma_window;

	dma_window = (unsigned int *)get_property(dn, "ibm,dma-window", NULL);

	if (!dma_window)
		panic("iommu_table_setparms_lpar: device %s has no"
		      " ibm,dma-window property!\n", dn->full_name);

	tbl->it_busno  = dn->bussubno;
	tbl->it_size   = (((((unsigned long)dma_window[4] << 32) | 
			   (unsigned long)dma_window[5]) >> PAGE_SHIFT) << 3) >> PAGE_SHIFT;
	tbl->it_offset = ((((unsigned long)dma_window[2] << 32) | 
			   (unsigned long)dma_window[3]) >> 12);
	tbl->it_base   = 0;
	tbl->it_index  = dma_window[0];
	tbl->it_entrysize = sizeof(union tce_entry);
	tbl->it_blocksize  = 16;
}


void iommu_devnode_init(struct device_node *dn)
{
	struct iommu_table *tbl;

	tbl = (struct iommu_table *)kmalloc(sizeof(struct iommu_table), 
					    GFP_KERNEL);
	
	if (systemcfg->platform == PLATFORM_PSERIES_LPAR)
		iommu_table_setparms_lpar(dn->phb, dn, tbl);
	else
		iommu_table_setparms(dn->phb, dn, tbl);
	
	dn->iommu_table = iommu_init_table(tbl);
}


void iommu_setup_pSeries(void)
{
	struct pci_dev *dev = NULL;
	struct device_node *dn, *mydn;

	if (systemcfg->platform == PLATFORM_PSERIES_LPAR)
		iommu_buses_init_lpar(&pci_root_buses);
	else
		iommu_buses_init();

	/* Now copy the iommu_table ptr from the bus devices down to every
	 * pci device_node.  This means get_iommu_table() won't need to search
	 * up the device tree to find it.
	 */
	while ((dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		mydn = dn = PCI_GET_DN(dev);

		while (dn && dn->iommu_table == NULL)
			dn = dn->parent;
		if (dn)
			mydn->iommu_table = dn->iommu_table;
	}
}


/* These are called very early. */
void tce_init_pSeries(void)
{
	ppc_md.tce_build = tce_build_pSeries;
	ppc_md.tce_free  = tce_free_pSeries;

	pci_iommu_init();
}

