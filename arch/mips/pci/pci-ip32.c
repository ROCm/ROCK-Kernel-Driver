/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000, 2001 Keith M Wesolowski
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <asm/pci.h>
#include <asm/ip32/mace.h>
#include <asm/ip32/crime.h>
#include <asm/ip32/ip32_ints.h>
#include <linux/delay.h>

#undef DEBUG_MACE_PCI

/*
 * O2 has up to 5 PCI devices connected into the MACE bridge.  The device
 * map looks like this:
 *
 * 0  aic7xxx 0
 * 1  aic7xxx 1
 * 2  expansion slot
 * 3  N/C
 * 4  N/C
 */

#define chkslot(_bus,_devfn)					\
do {							        \
	if ((_bus)->number > 0 || PCI_SLOT (_devfn) < 1	\
	    || PCI_SLOT (_devfn) > 3)			        \
		return PCIBIOS_DEVICE_NOT_FOUND;		\
} while (0)

#define mkaddr(_devfn, where) \
((((_devfn) & 0xffUL) << 8) | ((where) & 0xfcUL))

void macepci_error(int irq, void *dev, struct pt_regs *regs);

static int macepci_read_config(struct pci_bus *bus, unsigned int devfn,
			       int where, int size, u32 * val)
{
	switch (size) {
	case 1:
		*val = 0xff;
		chkslot(bus, devfn);
		mace_write_32(MACEPCI_CONFIG_ADDR, mkaddr(devfn, where));
		*val =
		    mace_read_8(MACEPCI_CONFIG_DATA +
				((where & 3UL) ^ 3UL));

		return PCIBIOS_SUCCESSFUL;

	case 2:
		*val = 0xffff;
		chkslot(bus, devfn);
		if (where & 1)
			return PCIBIOS_BAD_REGISTER_NUMBER;
		mace_write_32(MACEPCI_CONFIG_ADDR, mkaddr(devfn, where));
		*val =
		    mace_read_16(MACEPCI_CONFIG_DATA +
				 ((where & 2UL) ^ 2UL));

		return PCIBIOS_SUCCESSFUL;

	case 4:
		*val = 0xffffffff;
		chkslot(bus, devfn);
		if (where & 3)
			return PCIBIOS_BAD_REGISTER_NUMBER;
		mace_write_32(MACEPCI_CONFIG_ADDR, mkaddr(devfn, where));
		*val = mace_read_32(MACEPCI_CONFIG_DATA);

		return PCIBIOS_SUCCESSFUL;
	}
}

static int macepci_write_config(struct pci_bus *bus, unsigned int devfn,
				int where, int size, u32 val)
{
	switch (size) {
	case 1:
		chkslot(bus, devfn);
		mace_write_32(MACEPCI_CONFIG_ADDR, mkaddr(devfn, where));
		mace_write_8(MACEPCI_CONFIG_DATA + ((where & 3UL) ^ 3UL),
			     val);

		return PCIBIOS_SUCCESSFUL;

	case 2:
		chkslot(bus, devfn);
		if (where & 1)
			return PCIBIOS_BAD_REGISTER_NUMBER;
		mace_write_32(MACEPCI_CONFIG_ADDR, mkaddr(devfn, where));
		mace_write_16(MACEPCI_CONFIG_DATA + ((where & 2UL) ^ 2UL),
			      val);

		return PCIBIOS_SUCCESSFUL;

	case 4:
		chkslot(bus, devfn);
		if (where & 3)
			return PCIBIOS_BAD_REGISTER_NUMBER;
		mace_write_32(MACEPCI_CONFIG_ADDR, mkaddr(devfn, where));
		mace_write_32(MACEPCI_CONFIG_DATA, val);

		return PCIBIOS_SUCCESSFUL;
	}
}

static struct pci_ops macepci_ops = {
	.read = macepci_read_config,
	.write = macepci_write_config,
};

struct pci_fixup pcibios_fixups[] = { {0} };

static int __init pcibios_init(void)
{
	struct pci_dev *dev = NULL;
	u32 start, size;
	u16 cmd;
	u32 base_io = 0x3000;	/* The first i/o address to assign after SCSI */
	u32 base_mem = 0x80100000;	/* Likewise */
	u32 rev = mace_read_32(MACEPCI_REV);
	int i;

	printk("MACE: PCI rev %d detected at %016lx\n", rev,
	       (u64) MACE_BASE + MACE_PCI);

	/* These are *bus* addresses */
	ioport_resource.start = 0;
	ioport_resource.end = 0xffffffffUL;
	iomem_resource.start = 0x80000000UL;
	iomem_resource.end = 0xffffffffUL;

	/* Clear any outstanding errors and enable interrupts */
	mace_write_32(MACEPCI_ERROR_ADDR, 0);
	mace_write_32(MACEPCI_ERROR_FLAGS, 0);
	mace_write_32(MACEPCI_CONTROL, 0xff008500);
	crime_write_64(CRIME_HARD_INT, 0UL);
	crime_write_64(CRIME_SOFT_INT, 0UL);
	crime_write_64(CRIME_INT_STAT, 0x000000000000ff00UL);

	if (request_irq(MACE_PCI_BRIDGE_IRQ, macepci_error, 0,
			"MACE PCI error", NULL))
		panic("PCI bridge can't get interrupt; can't happen.");

	pci_scan_bus(0, &macepci_ops, NULL);

#ifdef DEBUG_MACE_PCI
	while ((dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		printk("Device: %d/%d/%d ARCS-assigned bus resource map\n",
		       dev->bus->number, PCI_SLOT(dev->devfn),
		       PCI_FUNC(dev->devfn));
		for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
			if (dev->resource[i].start == 0)
				continue;
			printk("%d: %016lx - %016lx (flags %04lx)\n",
			       i, dev->resource[i].start,
			       dev->resource[i].end,
			       dev->resource[i].flags);
		}
	}
#endif
	/*
	 * Assign sane resources to and enable all devices.  The requirement
	 * for the SCSI controllers is well-known: a 256-byte I/O region
	 * which we must assign, and a 1-page memory region which is
	 * assigned by the system firmware.
	 */
	dev = NULL;
	while ((dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		switch (PCI_SLOT(dev->devfn)) {
		case 1:	/* SCSI bus 0 */
			dev->resource[0].start = 0x1000UL;
			dev->resource[0].end = 0x10ffUL;
			break;
		case 2:	/* SCSI bus 1 */
			dev->resource[0].start = 0x2000UL;
			dev->resource[0].end = 0x20ffUL;
			break;
		default:	/* Slots - I guess we have only 1 */
			for (i = 0; i < 6; i++) {
				size = dev->resource[i].end
				    - dev->resource[i].start;
				if (!size
				    || !(dev->resource[i].flags
					 & (IORESOURCE_IO |
					    IORESOURCE_MEM))) {
					dev->resource[i].start =
					    dev->resource[i].end = 0UL;
					continue;
				}
				if (dev->resource[i].flags & IORESOURCE_IO) {
					dev->resource[i].start = base_io;
					base_io += PAGE_ALIGN(size);
				} else {
					dev->resource[i].start = base_mem;
					base_mem += 0x100000UL;
				}
				dev->resource[i].end =
				    dev->resource[i].start + size;
			}
			break;
		}
		for (i = 0; i < 6; i++) {
			if (dev->resource[i].start == 0)
				continue;
			start = dev->resource[i].start;
			if (dev->resource[i].flags & IORESOURCE_IO)
				start |= 1;
			pci_write_config_dword(dev,
					       PCI_BASE_ADDRESS_0 +
					       (i << 2), (u32) start);
		}
		pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, 0x20);
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x30);
		pci_read_config_word(dev, PCI_COMMAND, &cmd);
		cmd |=
		    (PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
		     PCI_COMMAND_SPECIAL | PCI_COMMAND_INVALIDATE |
		     PCI_COMMAND_PARITY);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
		pci_set_master(dev);
	}
	/*
	 * Fixup O2 PCI slot. Bad hack.
	 */
/*        devtag = pci_make_tag(0, 0, 3, 0);

        slot = macepci_conf_read(0, devtag, PCI_COMMAND_STATUS_REG);
        slot |= PCI_COMMAND_IO_ENABLE | PCI_COMMAND_MEM_ENABLE;
        macepci_conf_write(0, devtag, PCI_COMMAND_STATUS_REG, slot);

        slot = macepci_conf_read(0, devtag, PCI_MAPREG_START);
        if (slot == 0xffffffe1)
                macepci_conf_write(0, devtag, PCI_MAPREG_START, 0x00001000);

        slot = macepci_conf_read(0, devtag, PCI_MAPREG_START + (2 << 2));
        if ((slot & 0xffff0000) == 0) {
                slot += 0x00010000;
                macepci_conf_write(0, devtag, PCI_MAPREG_START + (2 << 2),
                    0x00000000);
        }
 */
#ifdef DEBUG_MACE_PCI
	printk("Triggering PCI bridge interrupt...\n");
	mace_write_32(MACEPCI_ERROR_FLAGS, MACEPCI_ERROR_INTERRUPT_TEST);

	dev = NULL;
	while ((dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		printk("Device: %d/%d/%d final bus resource map\n",
		       dev->bus->number, PCI_SLOT(dev->devfn),
		       PCI_FUNC(dev->devfn));
		for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
			if (dev->resource[i].start == 0)
				continue;
			printk("%d: %016lx - %016lx (flags %04lx)\n",
			       i, dev->resource[i].start,
			       dev->resource[i].end,
			       dev->resource[i].flags);
		}
	}
#endif

	return 0;
}

subsys_initcall(pcibios_init);

/*
 * Given a PCI slot number (a la PCI_SLOT(...)) and the interrupt pin of
 * the device (1-4 => A-D), tell what irq to use.  Note that we don't
 * in theory have slots 4 and 5, and we never normally use the shared
 * irqs.  I suppose a device without a pin A will thank us for doing it
 * right if there exists such a broken piece of crap.
 */
static int __devinit macepci_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	chkslot(dev->bus, dev->devfn);
	if (pin == 0)
		pin = 1;
	switch (slot) {
	case 1:
		return MACEPCI_SCSI0_IRQ;
	case 2:
		return MACEPCI_SCSI1_IRQ;
	case 3:
		switch (pin) {
		case 2:
			return MACEPCI_SHARED0_IRQ;
		case 3:
			return MACEPCI_SHARED1_IRQ;
		case 4:
			return MACEPCI_SHARED2_IRQ;
		case 1:
		default:
			return MACEPCI_SLOT0_IRQ;
		}
	case 4:
		switch (pin) {
		case 2:
			return MACEPCI_SHARED2_IRQ;
		case 3:
			return MACEPCI_SHARED0_IRQ;
		case 4:
			return MACEPCI_SHARED1_IRQ;
		case 1:
		default:
			return MACEPCI_SLOT1_IRQ;
		}
		return MACEPCI_SLOT1_IRQ;
	case 5:
		switch (pin) {
		case 2:
			return MACEPCI_SHARED1_IRQ;
		case 3:
			return MACEPCI_SHARED2_IRQ;
		case 4:
			return MACEPCI_SHARED0_IRQ;
		case 1:
		default:
			return MACEPCI_SLOT2_IRQ;
		}
	default:
		return 0;
	}
}

/*
 * It's not entirely clear what this does in a system with no bridges.
 * In any case, bridges are not supported by Linux in O2.
 */
static u8 __init macepci_swizzle(struct pci_dev *dev, u8 * pinp)
{
	if (PCI_SLOT(dev->devfn) == 2)
		*pinp = 2;
	else
		*pinp = 1;
	return PCI_SLOT(dev->devfn);
}

/* All devices are enabled during initialization. */
int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	return PCIBIOS_SUCCESSFUL;
}

char *__init pcibios_setup(char *str)
{
	return str;
}

void pcibios_align_resource(void *data, struct resource *res,
			    unsigned long size, unsigned long align)
{
}

void __init pcibios_update_irq(struct pci_dev *dev, int irq)
{
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);
}

void __devinit pcibios_fixup_bus(struct pci_bus *b)
{
	pci_fixup_irqs(macepci_swizzle, macepci_map_irq);
}

/*
 * Handle errors from the bridge.  This includes master and target aborts,
 * various command and address errors, and the interrupt test.  This gets
 * registered on the bridge error irq.  It's conceivable that some of these
 * conditions warrant a panic.  Anybody care to say which ones?
 */
void macepci_error(int irq, void *dev, struct pt_regs *regs)
{
	u32 flags, error_addr;
	char space;

	flags = mace_read_32(MACEPCI_ERROR_FLAGS);
	error_addr = mace_read_32(MACEPCI_ERROR_ADDR);

	if (flags & MACEPCI_ERROR_MEMORY_ADDR)
		space = 'M';
	else if (flags & MACEPCI_ERROR_CONFIG_ADDR)
		space = 'C';
	else
		space = 'X';

	if (flags & MACEPCI_ERROR_MASTER_ABORT) {
		printk("MACEPCI: Master abort at 0x%08x (%c)\n",
		       error_addr, space);
		mace_write_32(MACEPCI_ERROR_FLAGS,
			      flags & ~MACEPCI_ERROR_MASTER_ABORT);
	}
	if (flags & MACEPCI_ERROR_TARGET_ABORT) {
		printk("MACEPCI: Target abort at 0x%08x (%c)\n",
		       error_addr, space);
		mace_write_32(MACEPCI_ERROR_FLAGS,
			      flags & ~MACEPCI_ERROR_TARGET_ABORT);
	}
	if (flags & MACEPCI_ERROR_DATA_PARITY_ERR) {
		printk("MACEPCI: Data parity error at 0x%08x (%c)\n",
		       error_addr, space);
		mace_write_32(MACEPCI_ERROR_FLAGS, flags
			      & ~MACEPCI_ERROR_DATA_PARITY_ERR);
	}
	if (flags & MACEPCI_ERROR_RETRY_ERR) {
		printk("MACEPCI: Retry error at 0x%08x (%c)\n", error_addr,
		       space);
		mace_write_32(MACEPCI_ERROR_FLAGS, flags
			      & ~MACEPCI_ERROR_RETRY_ERR);
	}
	if (flags & MACEPCI_ERROR_ILLEGAL_CMD) {
		printk("MACEPCI: Illegal command at 0x%08x (%c)\n",
		       error_addr, space);
		mace_write_32(MACEPCI_ERROR_FLAGS,
			      flags & ~MACEPCI_ERROR_ILLEGAL_CMD);
	}
	if (flags & MACEPCI_ERROR_SYSTEM_ERR) {
		printk("MACEPCI: System error at 0x%08x (%c)\n",
		       error_addr, space);
		mace_write_32(MACEPCI_ERROR_FLAGS, flags
			      & ~MACEPCI_ERROR_SYSTEM_ERR);
	}
	if (flags & MACEPCI_ERROR_PARITY_ERR) {
		printk("MACEPCI: Parity error at 0x%08x (%c)\n",
		       error_addr, space);
		mace_write_32(MACEPCI_ERROR_FLAGS,
			      flags & ~MACEPCI_ERROR_PARITY_ERR);
	}
	if (flags & MACEPCI_ERROR_OVERRUN) {
		printk("MACEPCI: Overrun error at 0x%08x (%c)\n",
		       error_addr, space);
		mace_write_32(MACEPCI_ERROR_FLAGS, flags
			      & ~MACEPCI_ERROR_OVERRUN);
	}
	if (flags & MACEPCI_ERROR_SIG_TABORT) {
		printk("MACEPCI: Signaled target abort (clearing)\n");
		mace_write_32(MACEPCI_ERROR_FLAGS, flags
			      & ~MACEPCI_ERROR_SIG_TABORT);
	}
	if (flags & MACEPCI_ERROR_INTERRUPT_TEST) {
		printk("MACEPCI: Interrupt test triggered (clearing)\n");
		mace_write_32(MACEPCI_ERROR_FLAGS, flags
			      & ~MACEPCI_ERROR_INTERRUPT_TEST);
	}
}

unsigned int pcibios_assign_all_busses(void)
{
	return 0;
}
