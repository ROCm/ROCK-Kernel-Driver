/*
 * probe.c - PCI detection and setup code
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/module.h>

#undef DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

LIST_HEAD(pci_root_buses);
LIST_HEAD(pci_devices);

/*
 * Translate the low bits of the PCI base
 * to the resource type
 */
static inline unsigned int pci_calc_resource_flags(unsigned int flags)
{
	if (flags & PCI_BASE_ADDRESS_SPACE_IO)
		return IORESOURCE_IO;

	if (flags & PCI_BASE_ADDRESS_MEM_PREFETCH)
		return IORESOURCE_MEM | IORESOURCE_PREFETCH;

	return IORESOURCE_MEM;
}

/*
 * Find the extent of a PCI decode..
 */
static u32 pci_size(u32 base, unsigned long mask)
{
	u32 size = mask & base;		/* Find the significant bits */
	size = size & ~(size-1);	/* Get the lowest of them to find the decode size */
	return size-1;			/* extent = size - 1 */
}

static void pci_read_bases(struct pci_dev *dev, unsigned int howmany, int rom)
{
	unsigned int pos, reg, next;
	u32 l, sz;
	struct resource *res;

	for(pos=0; pos<howmany; pos = next) {
		next = pos+1;
		res = &dev->resource[pos];
		res->name = dev->name;
		reg = PCI_BASE_ADDRESS_0 + (pos << 2);
		pci_read_config_dword(dev, reg, &l);
		pci_write_config_dword(dev, reg, ~0);
		pci_read_config_dword(dev, reg, &sz);
		pci_write_config_dword(dev, reg, l);
		if (!sz || sz == 0xffffffff)
			continue;
		if (l == 0xffffffff)
			l = 0;
		if ((l & PCI_BASE_ADDRESS_SPACE) == PCI_BASE_ADDRESS_SPACE_MEMORY) {
			res->start = l & PCI_BASE_ADDRESS_MEM_MASK;
			res->flags |= l & ~PCI_BASE_ADDRESS_MEM_MASK;
			sz = pci_size(sz, PCI_BASE_ADDRESS_MEM_MASK);
		} else {
			res->start = l & PCI_BASE_ADDRESS_IO_MASK;
			res->flags |= l & ~PCI_BASE_ADDRESS_IO_MASK;
			sz = pci_size(sz, PCI_BASE_ADDRESS_IO_MASK & 0xffff);
		}
		res->end = res->start + (unsigned long) sz;
		res->flags |= pci_calc_resource_flags(l);
		if ((l & (PCI_BASE_ADDRESS_SPACE | PCI_BASE_ADDRESS_MEM_TYPE_MASK))
		    == (PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64)) {
			pci_read_config_dword(dev, reg+4, &l);
			next++;
#if BITS_PER_LONG == 64
			res->start |= ((unsigned long) l) << 32;
			res->end = res->start + sz;
			pci_write_config_dword(dev, reg+4, ~0);
			pci_read_config_dword(dev, reg+4, &sz);
			pci_write_config_dword(dev, reg+4, l);
			if (~sz)
				res->end = res->start + 0xffffffff +
						(((unsigned long) ~sz) << 32);
#else
			if (l) {
				printk(KERN_ERR "PCI: Unable to handle 64-bit address for device %s\n", dev->slot_name);
				res->start = 0;
				res->flags = 0;
				continue;
			}
#endif
		}
	}
	if (rom) {
		dev->rom_base_reg = rom;
		res = &dev->resource[PCI_ROM_RESOURCE];
		pci_read_config_dword(dev, rom, &l);
		pci_write_config_dword(dev, rom, ~PCI_ROM_ADDRESS_ENABLE);
		pci_read_config_dword(dev, rom, &sz);
		pci_write_config_dword(dev, rom, l);
		if (l == 0xffffffff)
			l = 0;
		if (sz && sz != 0xffffffff) {
			res->flags = (l & PCI_ROM_ADDRESS_ENABLE) |
			  IORESOURCE_MEM | IORESOURCE_PREFETCH | IORESOURCE_READONLY | IORESOURCE_CACHEABLE;
			res->start = l & PCI_ROM_ADDRESS_MASK;
			sz = pci_size(sz, PCI_ROM_ADDRESS_MASK);
			res->end = res->start + (unsigned long) sz;
		}
		res->name = dev->name;
	}
}

void __devinit pci_read_bridge_bases(struct pci_bus *child)
{
	struct pci_dev *dev = child->self;
	u8 io_base_lo, io_limit_lo;
	u16 mem_base_lo, mem_limit_lo;
	unsigned long base, limit;
	struct resource *res;
	int i;

	if (!dev)		/* It's a host bus, nothing to read */
		return;

	for(i=0; i<3; i++)
		child->resource[i] = &dev->resource[PCI_BRIDGE_RESOURCES+i];

	res = child->resource[0];
	pci_read_config_byte(dev, PCI_IO_BASE, &io_base_lo);
	pci_read_config_byte(dev, PCI_IO_LIMIT, &io_limit_lo);
	base = (io_base_lo & PCI_IO_RANGE_MASK) << 8;
	limit = (io_limit_lo & PCI_IO_RANGE_MASK) << 8;

	if ((io_base_lo & PCI_IO_RANGE_TYPE_MASK) == PCI_IO_RANGE_TYPE_32) {
		u16 io_base_hi, io_limit_hi;
		pci_read_config_word(dev, PCI_IO_BASE_UPPER16, &io_base_hi);
		pci_read_config_word(dev, PCI_IO_LIMIT_UPPER16, &io_limit_hi);
		base |= (io_base_hi << 16);
		limit |= (io_limit_hi << 16);
	}

	if (base && base <= limit) {
		res->flags = (io_base_lo & PCI_IO_RANGE_TYPE_MASK) | IORESOURCE_IO;
		res->start = base;
		res->end = limit + 0xfff;
	} else {
		/*
		 * Ugh. We don't know enough about this bridge. Just assume
		 * that it's entirely transparent.
		 */
		printk(KERN_ERR "Unknown bridge resource %d: assuming transparent\n", 0);
		child->resource[0] = child->parent->resource[0];
	}

	res = child->resource[1];
	pci_read_config_word(dev, PCI_MEMORY_BASE, &mem_base_lo);
	pci_read_config_word(dev, PCI_MEMORY_LIMIT, &mem_limit_lo);
	base = (mem_base_lo & PCI_MEMORY_RANGE_MASK) << 16;
	limit = (mem_limit_lo & PCI_MEMORY_RANGE_MASK) << 16;
	if (base && base <= limit) {
		res->flags = (mem_base_lo & PCI_MEMORY_RANGE_TYPE_MASK) | IORESOURCE_MEM;
		res->start = base;
		res->end = limit + 0xfffff;
	} else {
		/* See comment above. Same thing */
		printk(KERN_ERR "Unknown bridge resource %d: assuming transparent\n", 1);
		child->resource[1] = child->parent->resource[1];
	}

	res = child->resource[2];
	pci_read_config_word(dev, PCI_PREF_MEMORY_BASE, &mem_base_lo);
	pci_read_config_word(dev, PCI_PREF_MEMORY_LIMIT, &mem_limit_lo);
	base = (mem_base_lo & PCI_PREF_RANGE_MASK) << 16;
	limit = (mem_limit_lo & PCI_PREF_RANGE_MASK) << 16;

	if ((mem_base_lo & PCI_PREF_RANGE_TYPE_MASK) == PCI_PREF_RANGE_TYPE_64) {
		u32 mem_base_hi, mem_limit_hi;
		pci_read_config_dword(dev, PCI_PREF_BASE_UPPER32, &mem_base_hi);
		pci_read_config_dword(dev, PCI_PREF_LIMIT_UPPER32, &mem_limit_hi);
#if BITS_PER_LONG == 64
		base |= ((long) mem_base_hi) << 32;
		limit |= ((long) mem_limit_hi) << 32;
#else
		if (mem_base_hi || mem_limit_hi) {
			printk(KERN_ERR "PCI: Unable to handle 64-bit address space for %s\n", child->name);
			return;
		}
#endif
	}
	if (base && base <= limit) {
		res->flags = (mem_base_lo & PCI_MEMORY_RANGE_TYPE_MASK) | IORESOURCE_MEM | IORESOURCE_PREFETCH;
		res->start = base;
		res->end = limit + 0xfffff;
	} else {
		/* See comments above */
		printk(KERN_ERR "Unknown bridge resource %d: assuming transparent\n", 2);
		child->resource[2] = child->parent->resource[2];
	}
}

static struct pci_bus * __devinit pci_alloc_bus(void)
{
	struct pci_bus *b;

	b = kmalloc(sizeof(*b), GFP_KERNEL);
	if (b) {
		memset(b, 0, sizeof(*b));
		INIT_LIST_HEAD(&b->children);
		INIT_LIST_HEAD(&b->devices);
	}
	return b;
}

struct pci_bus * __devinit pci_add_new_bus(struct pci_bus *parent, struct pci_dev *dev, int busnr)
{
	struct pci_bus *child;
	int i;

	/*
	 * Allocate a new bus, and inherit stuff from the parent..
	 */
	child = pci_alloc_bus();

	list_add_tail(&child->node, &parent->children);
	child->self = dev;
	dev->subordinate = child;
	child->parent = parent;
	child->ops = parent->ops;
	child->sysdata = parent->sysdata;
	child->dev = &dev->dev;

	/*
	 * Set up the primary, secondary and subordinate
	 * bus numbers.
	 */
	child->number = child->secondary = busnr;
	child->primary = parent->secondary;
	child->subordinate = 0xff;

	/* Set up default resource pointers and names.. */
	for (i = 0; i < 4; i++) {
		child->resource[i] = &dev->resource[PCI_BRIDGE_RESOURCES+i];
		child->resource[i]->name = child->name;
	}

	return child;
}

/*
 * If it's a bridge, configure it and scan the bus behind it.
 * For CardBus bridges, we don't scan behind as the devices will
 * be handled by the bridge driver itself.
 *
 * We need to process bridges in two passes -- first we scan those
 * already configured by the BIOS and after we are done with all of
 * them, we proceed to assigning numbers to the remaining buses in
 * order to avoid overlaps between old and new bus numbers.
 */
static int __devinit pci_scan_bridge(struct pci_bus *bus, struct pci_dev * dev, int max, int pass)
{
	unsigned int buses;
	unsigned short cr;
	struct pci_bus *child;
	int is_cardbus = (dev->hdr_type == PCI_HEADER_TYPE_CARDBUS);

	pci_read_config_dword(dev, PCI_PRIMARY_BUS, &buses);
	DBG("Scanning behind PCI bridge %s, config %06x, pass %d\n", dev->slot_name, buses & 0xffffff, pass);
	if ((buses & 0xffff00) && !pcibios_assign_all_busses()) {
		/*
		 * Bus already configured by firmware, process it in the first
		 * pass and just note the configuration.
		 */
		if (pass)
			return max;
		child = pci_add_new_bus(bus, dev, 0);
		child->primary = buses & 0xFF;
		child->secondary = (buses >> 8) & 0xFF;
		child->subordinate = (buses >> 16) & 0xFF;
		child->number = child->secondary;
		if (!is_cardbus) {
			unsigned int cmax = pci_do_scan_bus(child);
			if (cmax > max) max = cmax;
		} else {
			unsigned int cmax = child->subordinate;
			if (cmax > max) max = cmax;
		}
	} else {
		/*
		 * We need to assign a number to this bus which we always
		 * do in the second pass. We also keep all address decoders
		 * on the bridge disabled during scanning.  FIXME: Why?
		 */
		if (!pass)
			return max;
		pci_read_config_word(dev, PCI_COMMAND, &cr);
		pci_write_config_word(dev, PCI_COMMAND, 0x0000);
		pci_write_config_word(dev, PCI_STATUS, 0xffff);

		child = pci_add_new_bus(bus, dev, ++max);
		buses = (buses & 0xff000000)
		      | ((unsigned int)(child->primary)     <<  0)
		      | ((unsigned int)(child->secondary)   <<  8)
		      | ((unsigned int)(child->subordinate) << 16);
		/*
		 * We need to blast all three values with a single write.
		 */
		pci_write_config_dword(dev, PCI_PRIMARY_BUS, buses);
		if (!is_cardbus) {
			/* Now we can scan all subordinate buses... */
			max = pci_do_scan_bus(child);
		} else {
			/*
			 * For CardBus bridges, we leave 4 bus numbers
			 * as cards with a PCI-to-PCI bridge can be
			 * inserted later.
			 */
			max += 3;
		}
		/*
		 * Set the subordinate bus number to its real value.
		 */
		child->subordinate = max;
		pci_write_config_byte(dev, PCI_SUBORDINATE_BUS, max);
		pci_write_config_word(dev, PCI_COMMAND, cr);
	}
	sprintf(child->name, (is_cardbus ? "PCI CardBus #%02x" : "PCI Bus #%02x"), child->number);

	return max;
}

/*
 * Read interrupt line and base address registers.
 * The architecture-dependent code can tweak these, of course.
 */
static void pci_read_irq(struct pci_dev *dev)
{
	unsigned char irq;

	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &irq);
	if (irq)
		pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &irq);
	dev->irq = irq;
}

/**
 * pci_setup_device - fill in class and map information of a device
 * @dev: the device structure to fill
 *
 * Initialize the device structure with information about the device's 
 * vendor,class,memory and IO-space addresses,IRQ lines etc.
 * Called at initialisation of the PCI subsystem and by CardBus services.
 * Returns 0 on success and -1 if unknown type of device (not normal, bridge
 * or CardBus).
 */
int pci_setup_device(struct pci_dev * dev)
{
	u32 class;

	sprintf(dev->slot_name, "%02x:%02x.%d", dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
	sprintf(dev->name, "PCI device %04x:%04x", dev->vendor, dev->device);
	INIT_LIST_HEAD(&dev->pools);
	
	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class);
	class >>= 8;				    /* upper 3 bytes */
	dev->class = class;
	class >>= 8;

	DBG("Found %02x:%02x [%04x/%04x] %06x %02x\n", dev->bus->number, dev->devfn, dev->vendor, dev->device, class, dev->hdr_type);

	/* "Unknown power state" */
	dev->current_state = 4;

	switch (dev->hdr_type) {		    /* header type */
	case PCI_HEADER_TYPE_NORMAL:		    /* standard header */
		if (class == PCI_CLASS_BRIDGE_PCI)
			goto bad;
		pci_read_irq(dev);
		pci_read_bases(dev, 6, PCI_ROM_ADDRESS);
		pci_read_config_word(dev, PCI_SUBSYSTEM_VENDOR_ID, &dev->subsystem_vendor);
		pci_read_config_word(dev, PCI_SUBSYSTEM_ID, &dev->subsystem_device);
		break;

	case PCI_HEADER_TYPE_BRIDGE:		    /* bridge header */
		if (class != PCI_CLASS_BRIDGE_PCI)
			goto bad;
		pci_read_bases(dev, 2, PCI_ROM_ADDRESS1);
		break;

	case PCI_HEADER_TYPE_CARDBUS:		    /* CardBus bridge header */
		if (class != PCI_CLASS_BRIDGE_CARDBUS)
			goto bad;
		pci_read_irq(dev);
		pci_read_bases(dev, 1, 0);
		pci_read_config_word(dev, PCI_CB_SUBSYSTEM_VENDOR_ID, &dev->subsystem_vendor);
		pci_read_config_word(dev, PCI_CB_SUBSYSTEM_ID, &dev->subsystem_device);
		break;

	default:				    /* unknown header */
		printk(KERN_ERR "PCI: device %s has unknown header type %02x, ignoring.\n",
			dev->slot_name, dev->hdr_type);
		return -1;

	bad:
		printk(KERN_ERR "PCI: %s: class %x doesn't match header type %02x. Ignoring class.\n",
		       dev->slot_name, class, dev->hdr_type);
		dev->class = PCI_CLASS_NOT_DEFINED;
	}

	/* We found a fine healthy device, go go go... */
	return 0;
}

/*
 * Read the config data for a PCI device, sanity-check it
 * and fill in the dev structure...
 */
struct pci_dev * __devinit pci_scan_device(struct pci_dev *temp)
{
	struct pci_dev *dev;
	u32 l;

	if (pci_read_config_dword(temp, PCI_VENDOR_ID, &l))
		return NULL;

	/* some broken boards return 0 or ~0 if a slot is empty: */
	if (l == 0xffffffff || l == 0x00000000 || l == 0x0000ffff || l == 0xffff0000)
		return NULL;

	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	memcpy(dev, temp, sizeof(*dev));
	dev->vendor = l & 0xffff;
	dev->device = (l >> 16) & 0xffff;

	/* Assume 32-bit PCI; let 64-bit PCI cards (which are far rarer)
	   set this higher, assuming the system even supports it.  */
	dev->dma_mask = 0xffffffff;
	if (pci_setup_device(dev) < 0) {
		kfree(dev);
		return NULL;
	}

	/* now put in global tree */
	strcpy(dev->dev.name,dev->name);
	strcpy(dev->dev.bus_id,dev->slot_name);

	device_register(&dev->dev);
	return dev;
}

struct pci_dev * __devinit pci_scan_slot(struct pci_dev *temp)
{
	struct pci_bus *bus = temp->bus;
	struct pci_dev *dev;
	struct pci_dev *first_dev = NULL;
	int func = 0;
	int is_multi = 0;
	u8 hdr_type;

	for (func = 0; func < 8; func++, temp->devfn++) {
		if (func && !is_multi)		/* not a multi-function device */
			continue;
		if (pci_read_config_byte(temp, PCI_HEADER_TYPE, &hdr_type))
			continue;
		temp->hdr_type = hdr_type & 0x7f;

		dev = pci_scan_device(temp);
		if (!dev)
			continue;
		pci_name_device(dev);
		if (!func) {
			is_multi = hdr_type & 0x80;
			first_dev = dev;
		}

		/*
		 * Link the device to both the global PCI device chain and
		 * the per-bus list of devices.
		 */
		list_add_tail(&dev->global_list, &pci_devices);
		list_add_tail(&dev->bus_list, &bus->devices);

		/* Fix up broken headers */
		pci_fixup_device(PCI_FIXUP_HEADER, dev);
	}
	return first_dev;
}

unsigned int __devinit pci_do_scan_bus(struct pci_bus *bus)
{
	unsigned int devfn, max, pass;
	struct list_head *ln;
	struct pci_dev *dev, dev0;

	DBG("Scanning bus %02x\n", bus->number);
	max = bus->secondary;

	/* Create a device template */
	memset(&dev0, 0, sizeof(dev0));
	dev0.bus = bus;
	dev0.sysdata = bus->sysdata;
	dev0.dev.parent = bus->dev;
	dev0.dev.bus = &pci_bus_type;

	/* Go find them, Rover! */
	for (devfn = 0; devfn < 0x100; devfn += 8) {
		dev0.devfn = devfn;
		pci_scan_slot(&dev0);
	}

	/*
	 * After performing arch-dependent fixup of the bus, look behind
	 * all PCI-to-PCI bridges on this bus.
	 */
	DBG("Fixups for bus %02x\n", bus->number);
	pcibios_fixup_bus(bus);
	for (pass=0; pass < 2; pass++)
		for (ln=bus->devices.next; ln != &bus->devices; ln=ln->next) {
			dev = pci_dev_b(ln);
			if (dev->hdr_type == PCI_HEADER_TYPE_BRIDGE || dev->hdr_type == PCI_HEADER_TYPE_CARDBUS)
				max = pci_scan_bridge(bus, dev, max, pass);
		}

	/*
	 * We've scanned the bus and so we know all about what's on
	 * the other side of any bridges that may be on this bus plus
	 * any devices.
	 *
	 * Return how far we've got finding sub-buses.
	 */
	DBG("Bus scan for %02x returning with max=%02x\n", bus->number, max);
	return max;
}

int __devinit pci_bus_exists(const struct list_head *list, int nr)
{
	const struct list_head *l;

	for(l=list->next; l != list; l = l->next) {
		const struct pci_bus *b = pci_bus_b(l);
		if (b->number == nr || pci_bus_exists(&b->children, nr))
			return 1;
	}
	return 0;
}

struct pci_bus * __devinit pci_alloc_primary_bus(int bus)
{
	struct pci_bus *b;

	if (pci_bus_exists(&pci_root_buses, bus)) {
		/* If we already got to this bus through a different bridge, ignore it */
		DBG("PCI: Bus %02x already known\n", bus);
		return NULL;
	}

	b = pci_alloc_bus();
	if (!b)
		return NULL;
	list_add_tail(&b->node, &pci_root_buses);

	b->dev = kmalloc(sizeof(*(b->dev)),GFP_KERNEL);
	memset(b->dev,0,sizeof(*(b->dev)));
	sprintf(b->dev->bus_id,"pci%d",bus);
	strcpy(b->dev->name,"Host/PCI Bridge");
	device_register(b->dev);

	b->number = b->secondary = bus;
	b->resource[0] = &ioport_resource;
	b->resource[1] = &iomem_resource;
	return b;
}

struct pci_bus * __devinit pci_scan_bus(int bus, struct pci_ops *ops, void *sysdata)
{
	struct pci_bus *b = pci_alloc_primary_bus(bus);
	if (b) {
		b->sysdata = sysdata;
		b->ops = ops;
		b->subordinate = pci_do_scan_bus(b);
	}
	return b;
}

EXPORT_SYMBOL(pci_devices);
EXPORT_SYMBOL(pci_root_buses);

#ifdef CONFIG_HOTPLUG
EXPORT_SYMBOL(pci_setup_device);
EXPORT_SYMBOL(pci_add_new_bus);
EXPORT_SYMBOL(pci_do_scan_bus);
EXPORT_SYMBOL(pci_scan_slot);
#endif
