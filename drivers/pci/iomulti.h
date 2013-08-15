/*
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
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright (c) 2009 Isaku Yamahata
 *                    VA Linux Systems Japan K.K.
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/pci.h>

#define PCI_NUM_BARS		6
#define PCI_NUM_FUNC		8

struct pci_iomul_func {
	int		segment;
	uint8_t		bus;
	uint8_t		devfn;

	/* only start and end are used */
	unsigned long	io_size;
	uint8_t		io_bar;
	struct resource	resource[PCI_NUM_BARS];
	struct resource dummy_parent;
};

struct pci_iomul_switch {
	struct list_head	list;	/* bus_list_lock protects */

	/*
	 * This lock the following entry and following
	 * pci_iomul_slot/pci_iomul_func.
	 */
	struct mutex		lock;
	struct kref		kref;

	struct resource		io_resource;
	struct resource		*io_region;
	unsigned int		count;
	struct pci_dev		*current_pdev;

	int			segment;
	uint8_t			bus;

	uint32_t		io_base;
	uint32_t		io_limit;

	/* func which has the largeset io size*/
	struct pci_iomul_func	*func;

	struct list_head	slots;
};

static inline void pci_iomul_switch_get(struct pci_iomul_switch *sw)
{
	kref_get(&sw->kref);
}

static inline void pci_iomul_switch_release(struct kref *kref)
{
	struct pci_iomul_switch *sw = container_of(kref,
						   struct pci_iomul_switch,
						   kref);
	kfree(sw);
}

static inline void pci_iomul_switch_put(struct pci_iomul_switch *sw)
{
	kref_put(&sw->kref, &pci_iomul_switch_release);
}

struct pci_iomul_slot {
	struct list_head	sibling;
	struct kref		kref;
	/*
	 * busnr
	 * when pcie, the primary busnr of the PCI-PCI bridge on which
	 * this devices sits.
	 */
	uint8_t			switch_busnr;
	struct resource		dummy_parent[PCI_NUM_RESOURCES - PCI_BRIDGE_RESOURCES];

	/* device */
	int			segment;
	uint8_t			bus;
	uint8_t			dev;

	struct pci_iomul_func	*func[PCI_NUM_FUNC];
};

static inline void pci_iomul_slot_get(struct pci_iomul_slot *slot)
{
	kref_get(&slot->kref);
}

static inline void pci_iomul_slot_release(struct kref *kref)
{
	struct pci_iomul_slot *slot = container_of(kref, struct pci_iomul_slot,
						   kref);
	kfree(slot);
}

static inline void pci_iomul_slot_put(struct pci_iomul_slot *slot)
{
	kref_put(&slot->kref, &pci_iomul_slot_release);
}

int pci_iomul_switch_io_allocated(const struct pci_iomul_switch *);
void pci_iomul_get_lock_switch(struct pci_dev *, struct pci_iomul_switch **,
			       struct pci_iomul_slot **);
