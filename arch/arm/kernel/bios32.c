/*
 *  linux/arch/arm/kernel/bios32.c
 *
 *  PCI bios-type initialisation for PCI machines
 *
 *  Bits taken from various places.
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/page.h> /* for BUG() */
#include <asm/irq.h>
#include <asm/mach-types.h>
#include <asm/mach/pci.h>

static int debug_pci;
int have_isa_bridge;

void pcibios_report_status(u_int status_mask, int warn)
{
	struct pci_dev *dev;

	pci_for_each_dev(dev) {
		u16 status;

		/*
		 * ignore host bridge - we handle
		 * that separately
		 */
		if (dev->bus->number == 0 && dev->devfn == 0)
			continue;

		pci_read_config_word(dev, PCI_STATUS, &status);

		status &= status_mask;
		if (status == 0)
			continue;

		/* clear the status errors */
		pci_write_config_word(dev, PCI_STATUS, status);

		if (warn)
			printk("(%02x:%02x.%d: %04X) ", dev->bus->number,
				PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn),
				status);
	}
}

/*
 * We don't use this to fix the device, but initialisation of it.
 * It's not the correct use for this, but it works.
 * Note that the arbiter/ISA bridge appears to be buggy, specifically in
 * the following area:
 * 1. park on CPU
 * 2. ISA bridge ping-pong
 * 3. ISA bridge master handling of target RETRY
 *
 * Bug 3 is responsible for the sound DMA grinding to a halt.  We now
 * live with bug 2.
 */
static void __init pci_fixup_83c553(struct pci_dev *dev)
{
	/*
	 * Set memory region to start at address 0, and enable IO
	 */
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_SPACE_MEMORY);
	pci_write_config_word(dev, PCI_COMMAND, PCI_COMMAND_IO);

	dev->resource[0].end -= dev->resource[0].start;
	dev->resource[0].start = 0;

	/*
	 * All memory requests from ISA to be channelled to PCI
	 */
	pci_write_config_byte(dev, 0x48, 0xff);

	/*
	 * Enable ping-pong on bus master to ISA bridge transactions.
	 * This improves the sound DMA substantially.  The fixed
	 * priority arbiter also helps (see below).
	 */
	pci_write_config_byte(dev, 0x42, 0x01);

	/*
	 * Enable PCI retry
	 */
	pci_write_config_byte(dev, 0x40, 0x22);

	/*
	 * We used to set the arbiter to "park on last master" (bit
	 * 1 set), but unfortunately the CyberPro does not park the
	 * bus.  We must therefore park on CPU.  Unfortunately, this
	 * may trigger yet another bug in the 553.
	 */
	pci_write_config_byte(dev, 0x83, 0x02);

	/*
	 * Make the ISA DMA request lowest priority, and disable
	 * rotating priorities completely.
	 */
	pci_write_config_byte(dev, 0x80, 0x11);
	pci_write_config_byte(dev, 0x81, 0x00);

	/*
	 * Route INTA input to IRQ 11, and set IRQ11 to be level
	 * sensitive.
	 */
	pci_write_config_word(dev, 0x44, 0xb000);
	outb(0x08, 0x4d1);
}

static void __init pci_fixup_unassign(struct pci_dev *dev)
{
	dev->resource[0].end -= dev->resource[0].start;
	dev->resource[0].start = 0;
}

/*
 * Prevent the PCI layer from seeing the resources
 * allocated to this device.  These resources are
 * of no consequence to the PCI layer (they are
 * handled elsewhere).
 */
static void __init pci_fixup_disable(struct pci_dev *dev)
{
	int i;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		dev->resource[i].start = 0;
		dev->resource[i].end   = 0;
		dev->resource[i].flags = 0;
	}
}

/*
 * PCI IDE controllers use non-standard I/O port
 * decoding, respect it.
 */
static void __init pci_fixup_ide_bases(struct pci_dev *dev)
{
	struct resource *r;
	int i;

	if ((dev->class >> 8) != PCI_CLASS_STORAGE_IDE)
		return;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		r = dev->resource + i;
		if ((r->start & ~0x80) == 0x374) {
			r->start |= 2;
			r->end = r->start;
		}
	}
}

/*
 * Put the DEC21142 to sleep
 */
static void __init pci_fixup_dec21142(struct pci_dev *dev)
{
	pci_write_config_dword(dev, 0x40, 0x80000000);
}

struct pci_fixup pcibios_fixups[] = {
	{
		PCI_FIXUP_HEADER,
		PCI_VENDOR_ID_DEC,	PCI_DEVICE_ID_DEC_21285,
		pci_fixup_disable
	}, {
		PCI_FIXUP_HEADER,
		PCI_VENDOR_ID_WINBOND,	PCI_DEVICE_ID_WINBOND_83C553,
		pci_fixup_83c553
	}, {
		PCI_FIXUP_HEADER,
		PCI_VENDOR_ID_WINBOND2,	PCI_DEVICE_ID_WINBOND2_89C940F,
		pci_fixup_unassign
	}, {
		PCI_FIXUP_HEADER,
		PCI_ANY_ID,		PCI_ANY_ID,
		pci_fixup_ide_bases
	}, {
		PCI_FIXUP_HEADER,
		PCI_VENDOR_ID_DEC,	PCI_DEVICE_ID_DEC_21142,
		pci_fixup_dec21142
	}, { 0 }
};

/*
 * Allocate resources for all PCI devices that have been enabled.
 * We need to do that before we try to fix up anything.
 */
static void __init pcibios_claim_resources(void)
{
	struct pci_dev *dev;
	int idx;

	pci_for_each_dev(dev) {
		for (idx = 0; idx < PCI_NUM_RESOURCES; idx++)
			if (dev->resource[idx].flags &&
			    dev->resource[idx].start)
				pci_claim_resource(dev, idx);
	}
}

void __init
pcibios_update_resource(struct pci_dev *dev, struct resource *root,
			struct resource *res, int resource)
{
	u32 val, check;
	int reg;

	if (debug_pci)
		printk("PCI: Assigning %3s %08lx to %s\n",
			res->flags & IORESOURCE_IO ? "IO" : "MEM",
			res->start, dev->name);

	val = res->start | (res->flags & PCI_REGION_FLAG_MASK);
	if (resource < 6) {
		reg = PCI_BASE_ADDRESS_0 + 4*resource;
	} else if (resource == PCI_ROM_RESOURCE) {
		res->flags |= PCI_ROM_ADDRESS_ENABLE;
		val |= PCI_ROM_ADDRESS_ENABLE;
		reg = dev->rom_base_reg;
	} else {
		/* Somebody might have asked allocation of a
		 * non-standard resource.
		 */
		return;
	}
	pci_write_config_dword(dev, reg, val);
	pci_read_config_dword(dev, reg, &check);
	if ((val ^ check) & ((val & PCI_BASE_ADDRESS_SPACE_IO) ?
	    PCI_BASE_ADDRESS_IO_MASK : PCI_BASE_ADDRESS_MEM_MASK)) {
		printk(KERN_ERR "PCI: Error while updating region "
			"%s/%d (%08x != %08x)\n", dev->slot_name,
			resource, val, check);
	}
}

void __init pcibios_update_irq(struct pci_dev *dev, int irq)
{
	if (debug_pci)
		printk("PCI: Assigning IRQ %02d to %s\n", irq, dev->name);
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);
}

/**
 * pcibios_fixup_bus - Called after each bus is probed, but before its children
 * are examined.
 */
void __init pcibios_fixup_bus(struct pci_bus *bus)
{
	struct list_head *walk = &bus->devices;
	struct arm_pci_sysdata *sysdata =
			(struct arm_pci_sysdata *)bus->sysdata;
	struct arm_bus_sysdata *busdata;

	if (bus->number < MAX_NR_BUS)
		busdata = sysdata->bus + bus->number;
	else
		BUG();

	busdata->max_lat = 255;

	/*
	 * Walk the devices on this bus, working out what we can
	 * and can't support.
	 */
	for (walk = walk->next; walk != &bus->devices; walk = walk->next) {
		struct pci_dev *dev = pci_dev_b(walk);
		u16 status;
		u8 max_lat, min_gnt;

		pci_read_config_word(dev, PCI_STATUS, &status);

		/*
		 * If this device does not support fast back to back
		 * transfers, the bus as a whole cannot support them.
		 */
		if (!(status & PCI_STATUS_FAST_BACK))
			busdata->features &= ~PCI_COMMAND_FAST_BACK;

		/*
		 * If we encounter a CyberPro 2000, then we disable
		 * SERR and PERR reporting - this chip doesn't drive the
		 * parity line correctly.
		 */
		if (dev->vendor == PCI_VENDOR_ID_INTERG &&
		    dev->device == PCI_DEVICE_ID_INTERG_2000)
			busdata->features &= ~(PCI_COMMAND_SERR |
					       PCI_COMMAND_PARITY);

		/*
		 * Calculate the maximum devsel latency.
		 */
		if (busdata->maxdevsel < (status & PCI_STATUS_DEVSEL_MASK))
			busdata->maxdevsel = (status & PCI_STATUS_DEVSEL_MASK);

		/*
		 * If this device is an ISA bridge, set the have_isa_bridge
		 * flag.  We will then go looking for things like keyboard,
		 * etc
		 */
		if (dev->class >> 8 == PCI_CLASS_BRIDGE_ISA ||
		    dev->class >> 8 == PCI_CLASS_BRIDGE_EISA)
			have_isa_bridge = !0;

		/*
		 * Calculate the maximum latency on this bus.  Note
		 * that we ignore any device which reports its max
		 * latency is the same as its use.
		 */
		pci_read_config_byte(dev, PCI_MAX_LAT, &max_lat);
		pci_read_config_byte(dev, PCI_MIN_GNT, &min_gnt);
		if (max_lat && max_lat != min_gnt && max_lat < busdata->max_lat)
			busdata->max_lat = max_lat;
	}

	/*
	 * Now walk the devices again, this time setting them up.
	 */
	walk = &bus->devices;
	for (walk = walk->next; walk != &bus->devices; walk = walk->next) {
		struct pci_dev *dev = pci_dev_b(walk);
		u16 cmd;
		u8 min_gnt, latency;

		/*
		 * Calculate this masters latency timer value.
		 * This is rather primitive - it does not take
		 * account of the number of masters in a system
		 * wanting to use the bus.
		 */
		pci_read_config_byte(dev, PCI_MIN_GNT, &min_gnt);
		if (min_gnt) {
			if (min_gnt > busdata->max_lat)
				min_gnt = busdata->max_lat;

			latency = (int)min_gnt * 25 / 3;
		} else
			latency = 32; /* 1us */

		pci_write_config_byte(dev, PCI_LATENCY_TIMER, latency);

		/*
		 * Set the cache line size to 32 bytes.
		 * Also, set system error enable, parity error enable.
		 * Disable ROM.
		 */
		pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, 8);
		pci_read_config_word(dev, PCI_COMMAND, &cmd);

		cmd |= busdata->features;

		pci_write_config_word(dev, PCI_COMMAND, cmd);
		pci_read_config_word(dev, PCI_COMMAND, &cmd);
		pci_write_config_dword(dev, PCI_ROM_ADDRESS, 0);
	}
}

void __init
pcibios_fixup_pbus_ranges(struct pci_bus *bus, struct pbus_set_ranges_data *ranges)
{
	ranges->io_start -= bus->resource[0]->start;
	ranges->io_end -= bus->resource[0]->start;
	ranges->mem_start -= bus->resource[1]->start;
	ranges->mem_end -= bus->resource[1]->start;
}

u8 __init no_swizzle(struct pci_dev *dev, u8 *pin)
{
	return 0;
}

extern struct hw_pci ebsa285_pci;
extern struct hw_pci cats_pci;
extern struct hw_pci netwinder_pci;
extern struct hw_pci personal_server_pci;
extern struct hw_pci ftv_pci;
extern struct hw_pci integrator_pci;

void __init pcibios_init(void)
{
	struct hw_pci *hw_pci = NULL;
	struct arm_pci_sysdata sysdata;
	int i;

	do {
#ifdef CONFIG_ARCH_EBSA285
		if (machine_is_ebsa285()) {
			hw_pci = &ebsa285_pci;
			break;
		}
#endif
#ifdef CONFIG_ARCH_CATS
		if (machine_is_cats()) {
			hw_pci = &cats_pci;
			break;
		}
#endif
#ifdef CONFIG_ARCH_NETWINDER
		if (machine_is_netwinder()) {
			hw_pci = &netwinder_pci;
			break;
		}
#endif
#ifdef CONFIG_ARCH_PERSONAL_SERVER
		if (machine_is_personal_server()) {
			hw_pci = &personal_server_pci;
			break;
		}
#endif
#ifdef CONFIG_ARCH_NEXUSPCI
		if (machine_is_nexuspci()) {
			hw_pci = &ftv_pci;
			break;
		}
#endif
#ifdef CONFIG_ARCH_INTEGRATOR
		if (machine_is_integrator()) {
			hw_pci = &integrator_pci;
			break;
		}
#endif
	} while (0);

	if (hw_pci == NULL)
		return;

	for (i = 0; i < MAX_NR_BUS; i++) {
		sysdata.bus[i].features  = PCI_COMMAND_FAST_BACK |
					   PCI_COMMAND_SERR |
					   PCI_COMMAND_PARITY;
		sysdata.bus[i].maxdevsel = PCI_STATUS_DEVSEL_FAST;
	}

	/*
	 * Set up the host bridge, and scan the bus.
	 */
	hw_pci->init(&sysdata);

	/*
	 * Other architectures don't seem to do this... should we?
	 */
	pcibios_claim_resources();

	/*
	 * Assign any unassigned resources.
	 */
	pci_assign_unassigned_resources();
	pci_fixup_irqs(hw_pci->swizzle, hw_pci->map_irq);
}

char * __init pcibios_setup(char *str)
{
	if (!strcmp(str, "debug")) {
		debug_pci = 1;
		return NULL;
	}
	return str;
}

/*
 * From arch/i386/kernel/pci-i386.c:
 *
 * We need to avoid collisions with `mirrored' VGA ports
 * and other strange ISA hardware, so we always want the
 * addresses to be allocated in the 0x000-0x0ff region
 * modulo 0x400.
 *
 * Why? Because some silly external IO cards only decode
 * the low 10 bits of the IO address. The 0x00-0xff region
 * is reserved for motherboard devices that decode all 16
 * bits, so it's ok to allocate at, say, 0x2800-0x28ff,
 * but we want to try to avoid allocating at 0x2900-0x2bff
 * which might have be mirrored at 0x0100-0x03ff..
 */
void pcibios_align_resource(void *data, struct resource *res, unsigned long size)
{
	if (res->flags & IORESOURCE_IO) {
		unsigned long start = res->start;

		if (start & 0x300)
			res->start = (start + 0x3ff) & ~0x3ff;
	}
}

/**
 * pcibios_set_master - Setup device for bus mastering.
 * @dev: PCI device to be setup
 */
void pcibios_set_master(struct pci_dev *dev)
{
}

/**
 * pcibios_enable_device - Enable I/O and memory.
 * @dev: PCI device to be enabled
 */
int pcibios_enable_device(struct pci_dev *dev)
{
	u16 cmd, old_cmd;
	int idx;
	struct resource *r;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;
	for (idx = 0; idx < 6; idx++) {
		r = dev->resource + idx;
		if (!r->start && r->end) {
			printk(KERN_ERR "PCI: Device %s not available because"
			       " of resource collisions\n", dev->slot_name);
			return -EINVAL;
		}
		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}
	if (cmd != old_cmd) {
		printk("PCI: enabling device %s (%04x -> %04x)\n",
		       dev->slot_name, old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
}
