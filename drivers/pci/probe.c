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

#define CARDBUS_LATENCY_TIMER	176	/* secondary latency timer */
#define CARDBUS_RESERVE_BUSNR	3

/* Ugh.  Need to stop exporting this to modules. */
LIST_HEAD(pci_root_buses);
EXPORT_SYMBOL(pci_root_buses);

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
static u32 pci_size(u32 base, u32 maxbase, unsigned long mask)
{
	u32 size = mask & maxbase;	/* Find the significant bits */
	if (!size)
		return 0;

	/* Get the lowest of them to find the decode size, and
	   from that the extent.  */
	size = (size & ~(size-1)) - 1;

	/* base == maxbase can be valid only if the BAR has
	   already been programmed with all 1s.  */
	if (base == maxbase && ((base | size) & mask) != mask)
		return 0;

	return size;
}

static void pci_read_bases(struct pci_dev *dev, unsigned int howmany, int rom)
{
	unsigned int pos, reg, next;
	u32 l, sz;
	struct resource *res;

	for(pos=0; pos<howmany; pos = next) {
		next = pos+1;
		res = &dev->resource[pos];
		res->name = pci_name(dev);
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
			sz = pci_size(l, sz, PCI_BASE_ADDRESS_MEM_MASK);
			if (!sz)
				continue;
			res->start = l & PCI_BASE_ADDRESS_MEM_MASK;
			res->flags |= l & ~PCI_BASE_ADDRESS_MEM_MASK;
		} else {
			sz = pci_size(l, sz, PCI_BASE_ADDRESS_IO_MASK & 0xffff);
			if (!sz)
				continue;
			res->start = l & PCI_BASE_ADDRESS_IO_MASK;
			res->flags |= l & ~PCI_BASE_ADDRESS_IO_MASK;
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
				printk(KERN_ERR "PCI: Unable to handle 64-bit address for device %s\n", pci_name(dev));
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
		res->name = pci_name(dev);
		pci_read_config_dword(dev, rom, &l);
		pci_write_config_dword(dev, rom, ~PCI_ROM_ADDRESS_ENABLE);
		pci_read_config_dword(dev, rom, &sz);
		pci_write_config_dword(dev, rom, l);
		if (l == 0xffffffff)
			l = 0;
		if (sz && sz != 0xffffffff) {
			sz = pci_size(l, sz, PCI_ROM_ADDRESS_MASK);
			if (sz) {
				res->flags = (l & PCI_ROM_ADDRESS_ENABLE) |
				  IORESOURCE_MEM | IORESOURCE_PREFETCH |
				  IORESOURCE_READONLY | IORESOURCE_CACHEABLE;
				res->start = l & PCI_ROM_ADDRESS_MASK;
				res->end = res->start + (unsigned long) sz;
			}
		}
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

	if (dev->transparent) {
		printk("Transparent bridge - %s\n", pci_name(dev));
		for(i = 0; i < PCI_BUS_NUM_RESOURCES; i++)
			child->resource[i] = child->parent->resource[i];
		return;
	}

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
	}
}

static struct pci_bus * __devinit pci_alloc_bus(void)
{
	struct pci_bus *b;

	b = kmalloc(sizeof(*b), GFP_KERNEL);
	if (b) {
		memset(b, 0, sizeof(*b));
		INIT_LIST_HEAD(&b->node);
		INIT_LIST_HEAD(&b->children);
		INIT_LIST_HEAD(&b->devices);
	}
	return b;
}

static struct pci_bus * __devinit
pci_alloc_child_bus(struct pci_bus *parent, struct pci_dev *bridge, int busnr)
{
	struct pci_bus *child;

	/*
	 * Allocate a new bus, and inherit stuff from the parent..
	 */
	child = pci_alloc_bus();

	if (child) {
		int i;

		child->self = bridge;
		child->parent = parent;
		child->ops = parent->ops;
		child->sysdata = parent->sysdata;
		child->dev = &bridge->dev;

		/*
		 * Set up the primary, secondary and subordinate
		 * bus numbers.
		 */
		child->number = child->secondary = busnr;
		child->primary = parent->secondary;
		child->subordinate = 0xff;

		/* Set up default resource pointers and names.. */
		for (i = 0; i < 4; i++) {
			child->resource[i] = &bridge->resource[PCI_BRIDGE_RESOURCES+i];
			child->resource[i]->name = child->name;
		}

		bridge->subordinate = child;
	}

	return child;
}

struct pci_bus * __devinit pci_add_new_bus(struct pci_bus *parent, struct pci_dev *dev, int busnr)
{
	struct pci_bus *child;

	child = pci_alloc_child_bus(parent, dev, busnr);
	if (child)
		list_add_tail(&child->node, &parent->children);
	return child;
}

static unsigned int __devinit pci_scan_child_bus(struct pci_bus *bus);

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
int __devinit pci_scan_bridge(struct pci_bus *bus, struct pci_dev * dev, int max, int pass)
{
	struct pci_bus *child;
	int is_cardbus = (dev->hdr_type == PCI_HEADER_TYPE_CARDBUS);
	u32 buses;

	pci_read_config_dword(dev, PCI_PRIMARY_BUS, &buses);

	DBG("Scanning behind PCI bridge %s, config %06x, pass %d\n",
	    pci_name(dev), buses & 0xffffff, pass);

	if ((buses & 0xffff00) && !pcibios_assign_all_busses() && !is_cardbus) {
		unsigned int cmax;
		/*
		 * Bus already configured by firmware, process it in the first
		 * pass and just note the configuration.
		 */
		if (pass)
			return max;
		child = pci_alloc_child_bus(bus, dev, 0);
		child->primary = buses & 0xFF;
		child->secondary = (buses >> 8) & 0xFF;
		child->subordinate = (buses >> 16) & 0xFF;
		child->number = child->secondary;
		cmax = pci_scan_child_bus(child);
		if (cmax > max) max = cmax;
	} else {
		/*
		 * We need to assign a number to this bus which we always
		 * do in the second pass.
		 */
		if (!pass)
			return max;

		/* Clear errors */
		pci_write_config_word(dev, PCI_STATUS, 0xffff);

		child = pci_alloc_child_bus(bus, dev, ++max);
		buses = (buses & 0xff000000)
		      | ((unsigned int)(child->primary)     <<  0)
		      | ((unsigned int)(child->secondary)   <<  8)
		      | ((unsigned int)(child->subordinate) << 16);

		/*
		 * yenta.c forces a secondary latency timer of 176.
		 * Copy that behaviour here.
		 */
		if (is_cardbus) {
			buses &= ~0xff000000;
			buses |= CARDBUS_LATENCY_TIMER << 24;
		}
			
		/*
		 * We need to blast all three values with a single write.
		 */
		pci_write_config_dword(dev, PCI_PRIMARY_BUS, buses);

		if (!is_cardbus) {
			/* Now we can scan all subordinate buses... */
			max = pci_scan_child_bus(child);
		} else {
			/*
			 * For CardBus bridges, we leave 4 bus numbers
			 * as cards with a PCI-to-PCI bridge can be
			 * inserted later.
			 */
			max += CARDBUS_RESERVE_BUSNR;
		}
		/*
		 * Set the subordinate bus number to its real value.
		 */
		child->subordinate = max;
		pci_write_config_byte(dev, PCI_SUBORDINATE_BUS, max);
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
static int pci_setup_device(struct pci_dev * dev)
{
	u32 class;

	dev->slot_name = dev->dev.bus_id;
	sprintf(pci_name(dev), "%04x:%02x:%02x.%d", pci_domain_nr(dev->bus),
		dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));

	INIT_LIST_HEAD(&dev->pools);

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class);
	class >>= 8;				    /* upper 3 bytes */
	dev->class = class;
	class >>= 8;

	DBG("Found %02x:%02x [%04x/%04x] %06x %02x\n", dev->bus->number,
	    dev->devfn, dev->vendor, dev->device, class, dev->hdr_type);

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
		/* The PCI-to-PCI bridge spec requires that subtractive
		   decoding (i.e. transparent) bridge must have programming
		   interface code of 0x01. */ 
		dev->transparent = ((dev->class & 0xff) == 1);
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
			pci_name(dev), dev->hdr_type);
		return -1;

	bad:
		printk(KERN_ERR "PCI: %s: class %x doesn't match header type %02x. Ignoring class.\n",
		       pci_name(dev), class, dev->hdr_type);
		dev->class = PCI_CLASS_NOT_DEFINED;
	}

	/* We found a fine healthy device, go go go... */
	return 0;
}

/**
 * pci_release_dev - free a pci device structure when all users of it are finished.
 * @dev: device that's been disconnected
 *
 * Will be called only by the device core when all users of this pci device are
 * done.
 */
static void pci_release_dev(struct device *dev)
{
	struct pci_dev *pci_dev;

	pci_dev = to_pci_dev(dev);
	kfree(pci_dev);
}

/*
 * Read the config data for a PCI device, sanity-check it
 * and fill in the dev structure...
 */
static struct pci_dev * __devinit
pci_scan_device(struct pci_bus *bus, int devfn)
{
	struct pci_dev *dev;
	u32 l;
	u8 hdr_type;

	if (pci_bus_read_config_byte(bus, devfn, PCI_HEADER_TYPE, &hdr_type))
		return NULL;

	if (pci_bus_read_config_dword(bus, devfn, PCI_VENDOR_ID, &l))
		return NULL;

	/* some broken boards return 0 or ~0 if a slot is empty: */
	if (l == 0xffffffff || l == 0x00000000 ||
	    l == 0x0000ffff || l == 0xffff0000)
		return NULL;

	dev = kmalloc(sizeof(struct pci_dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	memset(dev, 0, sizeof(struct pci_dev));
	dev->bus = bus;
	dev->sysdata = bus->sysdata;
	dev->dev.parent = bus->dev;
	dev->dev.bus = &pci_bus_type;
	dev->devfn = devfn;
	dev->hdr_type = hdr_type & 0x7f;
	dev->multifunction = !!(hdr_type & 0x80);
	dev->vendor = l & 0xffff;
	dev->device = (l >> 16) & 0xffff;

	/* Assume 32-bit PCI; let 64-bit PCI cards (which are far rarer)
	   set this higher, assuming the system even supports it.  */
	dev->dma_mask = 0xffffffff;
	dev->consistent_dma_mask = 0xffffffff;
	if (pci_setup_device(dev) < 0) {
		kfree(dev);
		return NULL;
	}
	device_initialize(&dev->dev);
	dev->dev.release = pci_release_dev;
	pci_dev_get(dev);

	pci_name_device(dev);

	dev->dev.dma_mask = &dev->dma_mask;

	return dev;
}

/**
 * pci_scan_slot - scan a PCI slot on a bus for devices.
 * @bus: PCI bus to scan
 * @devfn: slot number to scan (must have zero function.)
 *
 * Scan a PCI slot on the specified PCI bus for devices, adding
 * discovered devices to the @bus->devices list.  New devices
 * will have an empty dev->global_list head.
 */
int __devinit pci_scan_slot(struct pci_bus *bus, int devfn)
{
	int func, nr = 0;

	for (func = 0; func < 8; func++, devfn++) {
		struct pci_dev *dev;

		dev = pci_scan_device(bus, devfn);
		if (func == 0) {
			if (!dev)
				break;
		} else {
			if (!dev)
				continue;
			dev->multifunction = 1;
		}

		/* Fix up broken headers */
		pci_fixup_device(PCI_FIXUP_HEADER, dev);

		/*
		 * Add the device to our list of discovered devices
		 * and the bus list for fixup functions, etc.
		 */
		INIT_LIST_HEAD(&dev->global_list);
		list_add_tail(&dev->bus_list, &bus->devices);
		nr++;

		/*
		 * If this is a single function device,
		 * don't scan past the first function.
		 */
		if (!dev->multifunction)
			break;
	}
	return nr;
}

static unsigned int __devinit pci_scan_child_bus(struct pci_bus *bus)
{
	unsigned int devfn, pass, max = bus->secondary;
	struct pci_dev *dev;

	DBG("Scanning bus %02x\n", bus->number);

	/* Go find them, Rover! */
	for (devfn = 0; devfn < 0x100; devfn += 8)
		pci_scan_slot(bus, devfn);

	/*
	 * After performing arch-dependent fixup of the bus, look behind
	 * all PCI-to-PCI bridges on this bus.
	 */
	DBG("Fixups for bus %02x\n", bus->number);
	pcibios_fixup_bus(bus);
	for (pass=0; pass < 2; pass++)
		list_for_each_entry(dev, &bus->devices, bus_list) {
			if (dev->hdr_type == PCI_HEADER_TYPE_BRIDGE ||
			    dev->hdr_type == PCI_HEADER_TYPE_CARDBUS)
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

unsigned int __devinit pci_do_scan_bus(struct pci_bus *bus)
{
	unsigned int max;

	max = pci_scan_child_bus(bus);

	/*
	 * Make the discovered devices available.
	 */
	pci_bus_add_devices(bus);

	return max;
}

struct pci_bus * __devinit pci_scan_bus_parented(struct device *parent, int bus, struct pci_ops *ops, void *sysdata)
{
	struct pci_bus *b;

	b = pci_alloc_bus();
	if (!b)
		return NULL;

	b->dev = kmalloc(sizeof(*(b->dev)),GFP_KERNEL);
	if (!b->dev){
		kfree(b);
		return NULL;
	}

	b->sysdata = sysdata;
	b->ops = ops;

	if (pci_find_bus(pci_domain_nr(b), bus)) {
		/* If we already got to this bus through a different bridge, ignore it */
		DBG("PCI: Bus %02x already known\n", bus);
		kfree(b->dev);
		kfree(b);
		return NULL;
	}

	list_add_tail(&b->node, &pci_root_buses);

	memset(b->dev,0,sizeof(*(b->dev)));
	b->dev->parent = parent;
	sprintf(b->dev->bus_id,"pci%04x:%02x", pci_domain_nr(b), bus);
	device_register(b->dev);

	b->number = b->secondary = bus;
	b->resource[0] = &ioport_resource;
	b->resource[1] = &iomem_resource;

	b->subordinate = pci_scan_child_bus(b);

	pci_bus_add_devices(b);

	return b;
}
EXPORT_SYMBOL(pci_scan_bus_parented);

#ifdef CONFIG_HOTPLUG
EXPORT_SYMBOL(pci_add_new_bus);
EXPORT_SYMBOL(pci_do_scan_bus);
EXPORT_SYMBOL(pci_scan_slot);
EXPORT_SYMBOL(pci_scan_bridge);
#endif
