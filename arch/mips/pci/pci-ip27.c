/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 Christoph Hellwig (hch@lst.de)
 * Copyright (C) 1999, 2000 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <asm/sn/arch.h>
#include <asm/pci/bridge.h>
#include <asm/pci_channel.h>
#include <asm/paccess.h>
#include <asm/sn/intr.h>
#include <asm/sn/sn0/hub.h>

/*
 * Max #PCI busses we can handle; ie, max #PCI bridges.
 */
#define MAX_PCI_BUSSES		40

/*
 * Max #PCI devices (like scsi controllers) we handle on a bus.
 */
#define MAX_DEVICES_PER_PCIBUS	8

/*
 * XXX: No kmalloc available when we do our crosstalk scan,
 * 	we should try to move it later in the boot process.
 */
static struct bridge_controller bridges[MAX_PCI_BUSSES];

/*
 * Translate from irq to software PCI bus number and PCI slot.
 */
struct bridge_controller *irq_to_bridge[MAX_PCI_BUSSES * MAX_DEVICES_PER_PCIBUS];
int irq_to_slot[MAX_PCI_BUSSES * MAX_DEVICES_PER_PCIBUS];

/*
 * The Bridge ASIC supports both type 0 and type 1 access.  Type 1 is
 * not really documented, so right now I can't write code which uses it.
 * Therefore we use type 0 accesses for now even though they won't work
 * correcly for PCI-to-PCI bridges.
 *
 * The function is complicated by the ultimate brokeness of the IOC3 chip
 * which is used in SGI systems.  The IOC3 can only handle 32-bit PCI
 * accesses and does only decode parts of it's address space.
 */

static int pci_conf0_read_config(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 * value)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(bus);
	bridge_t *bridge = bc->base;
	int slot = PCI_SLOT(devfn);
	int fn = PCI_FUNC(devfn);
	volatile void *addr;
	u32 cf, shift, mask;
	int res;

	addr = &bridge->b_type0_cfg_dev[slot].f[fn].c[PCI_VENDOR_ID];
	if (get_dbe(cf, (u32 *) addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	/*
	 * IOC3 is fucked fucked beyond believe ...  Don't even give the
	 * generic PCI code a chance to look at it for real ...
	 */
	if (cf == (PCI_VENDOR_ID_SGI | (PCI_DEVICE_ID_SGI_IOC3 << 16)))
		goto oh_my_gawd;

	addr = &bridge->b_type0_cfg_dev[slot].f[fn].c[where ^ (4 - size)];

	if (size == 1)
		res = get_dbe(*value, (u8 *) addr);
	else if (size == 2)
		res = get_dbe(*value, (u16 *) addr);
	else
		res = get_dbe(*value, (u32 *) addr);

	return PCIBIOS_SUCCESSFUL;

oh_my_gawd:

	/*
	 * IOC3 is fucked fucked beyond believe ...  Don't even give the
	 * generic PCI code a chance to look at the wrong register.
	 */
	if ((where >= 0x14 && where < 0x40) || (where >= 0x48)) {
		*value = 0;
		return PCIBIOS_SUCCESSFUL;
	}

	/*
	 * IOC3 is fucked fucked beyond believe ...  Don't try to access
	 * anything but 32-bit words ...
	 */
	addr = &bridge->b_type0_cfg_dev[slot].f[fn].l[where >> 2];

	if (get_dbe(cf, (u32 *) addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	shift = ((where & 3) << 3);
	mask = (0xffffffffU >> ((4 - size) << 3));
	*value = (cf >> shift) & mask;

	return PCIBIOS_SUCCESSFUL;
}

static int pci_conf0_write_config(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 value)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(bus);
	bridge_t *bridge = bc->base;
	int slot = PCI_SLOT(devfn);
	int fn = PCI_FUNC(devfn);
	volatile void *addr;
	u32 cf, shift, mask, smask;
	int res;

	addr = &bridge->b_type0_cfg_dev[slot].f[fn].c[PCI_VENDOR_ID];
	if (get_dbe(cf, (u32 *) addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	/*
	 * IOC3 is fucked fucked beyond believe ...  Don't even give the
	 * generic PCI code a chance to look at it for real ...
	 */
	if (cf == (PCI_VENDOR_ID_SGI | (PCI_DEVICE_ID_SGI_IOC3 << 16)))
		goto oh_my_gawd;

	addr = &bridge->b_type0_cfg_dev[slot].f[fn].c[where ^ (4 - size)];

	if (size == 1) {
		res = put_dbe(value, (u8 *) addr);
	} else if (size == 2) {
		res = put_dbe(value, (u16 *) addr);
	} else {
		res = put_dbe(value, (u32 *) addr);
	}

	if (res)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;

oh_my_gawd:

	/*
	 * IOC3 is fucked fucked beyond believe ...  Don't even give the
	 * generic PCI code a chance to touch the wrong register.
	 */
	if ((where >= 0x14 && where < 0x40) || (where >= 0x48))
		return PCIBIOS_SUCCESSFUL;

	/*
	 * IOC3 is fucked fucked beyond believe ...  Don't try to access
	 * anything but 32-bit words ...
	 */
	addr = &bridge->b_type0_cfg_dev[slot].f[fn].l[where >> 2];

	if (get_dbe(cf, (u32 *) addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	shift = ((where & 3) << 3);
	mask = (0xffffffffU >> ((4 - size) << 3));
	smask = mask << shift;

	cf = (cf & ~smask) | ((value & mask) << shift);
	if (put_dbe(cf, (u32 *) addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops bridge_pci_ops = {
	.read = pci_conf0_read_config,
	.write = pci_conf0_write_config,
};

int __init bridge_probe(nasid_t nasid, int widget_id, int masterwid)
{
	struct bridge_controller *bc;
	bridge_t *bridge;
	static int num_bridges = 0;

	printk("a bridge\n");

	/* XXX: kludge alert.. */
	if (!num_bridges)
		ioport_resource.end = ~0UL;

	bc = &bridges[num_bridges++];

	bc->pc.pci_ops		= &bridge_pci_ops;
	bc->pc.mem_resource	= &bc->mem;
	bc->pc.io_resource	= &bc->io;

	bc->mem.name		= "Bridge PCI MEM";
	bc->pc.mem_offset	= 0;
	bc->mem.start		= 0;
	bc->mem.end		= ~0UL;
	bc->mem.flags		= IORESOURCE_MEM;

	bc->io.name		= "Bridge IO MEM";
	bc->io.start		= 0UL;
	bc->pc.io_offset	= 0UL;
	bc->io.end		= ~0UL;
	bc->io.flags		= IORESOURCE_IO;

	bc->irq_cpu = smp_processor_id();
	bc->widget_id = widget_id;
	bc->nasid = nasid;

	bc->baddr = (u64)masterwid << 60;
	bc->baddr |= (1UL << 56);	/* Barrier set */

	/*
	 * point to this bridge
	 */
	bridge = (bridge_t *) RAW_NODE_SWIN_BASE(nasid, widget_id);

	/*
 	 * Clear all pending interrupts.
 	 */
	bridge->b_int_rst_stat = BRIDGE_IRR_ALL_CLR;

	/*
 	 * Until otherwise set up, assume all interrupts are from slot 0
 	 */
	bridge->b_int_device = (u32) 0x0;

	/*
 	 * swap pio's to pci mem and io space (big windows)
 	 */
	bridge->b_wid_control |= BRIDGE_CTRL_IO_SWAP |
	                         BRIDGE_CTRL_MEM_SWAP;

	/*
	 * Hmm...  IRIX sets additional bits in the address which
	 * are documented as reserved in the bridge docs.
	 */
	bridge->b_wid_int_upper = 0x8000 | (masterwid << 16);
	bridge->b_wid_int_lower = 0x01800090;	/* PI_INT_PEND_MOD off*/
	bridge->b_dir_map = (masterwid << 20);	/* DMA */
	bridge->b_int_enable = 0;

	bridge->b_wid_tflush;     /* wait until Bridge PIO complete */

	bc->base = bridge;

	register_pci_controller(&bc->pc);
	return 0;
}

/*
 * All observed requests have pin == 1. We could have a global here, that
 * gets incremented and returned every time - unfortunately, pci_map_irq
 * may be called on the same device over and over, and need to return the
 * same value. On O2000, pin can be 0 or 1, and PCI slots can be [0..7].
 *
 * A given PCI device, in general, should be able to intr any of the cpus
 * on any one of the hubs connected to its xbow.
 */
int __devinit pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(dev->bus);
	int irq;

	irq = allocate_irqno();

	/*
	 * Argh...  This API doesn't handle with errors at all ...
	 */
	if (irq == -1) {
		printk(KERN_ERR "Can't allocate interrupt for PCI device %s\n",
		       pci_name(dev));
		return -1;
	}

	irq_to_bridge[irq] = bc;
	irq_to_slot[irq] = slot;

	return irq;
}

/*
 * Device might live on a subordinate PCI bus.  XXX Walk up the chain of buses
 * to find the slot number in sense of the bridge device register.
 * XXX This also means multiple devices might rely on conflicting bridge
 * settings.
 */

static void __init pci_disable_swapping(struct pci_dev *dev)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(dev->bus);
	bridge_t *bridge = bc->base;
	int slot = PCI_SLOT(dev->devfn);

	/* Turn off byte swapping */
	bridge->b_device[slot].reg &= ~BRIDGE_DEV_SWAP_DIR;
	bridge->b_widget.w_tflush;	/* Flush */
}

static void __init pci_enable_swapping(struct pci_dev *dev)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(dev->bus);
	bridge_t *bridge = bc->base;
	int slot = PCI_SLOT(dev->devfn);

	/* Turn on byte swapping */
	bridge->b_device[slot].reg |= BRIDGE_DEV_SWAP_DIR;
	bridge->b_widget.w_tflush;	/* Flush */
}

static void __init pci_fixup_ioc3(struct pci_dev *d)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(d->bus);
	unsigned long offset = NODE_OFFSET(bc->nasid);

	printk("PCI: Fixing base addresses for IOC3 device %s\n", pci_name(d));

	d->resource[0].start |= offset;
	d->resource[0].end |= offset;

	pci_disable_swapping(d);
}

static void __init pci_fixup_isp1020(struct pci_dev *d)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(d->bus);
	unsigned short command;

	d->resource[0].start |= (unsigned long) bc->nasid << 32;
	printk("PCI: Fixing isp1020 in [bus:slot.fn] %s\n", pci_name(d));

	/*
	 * Configure device to allow bus mastering, i/o and memory mapping.
	 * Older qlogicisp driver expects to have the IO space enable
	 * bit set. Things stop working if we program the controllers as not
	 * having PCI_COMMAND_MEMORY, so we have to fudge the mem_flags.
	 */
	pci_set_master(d);
	pci_read_config_word(d, PCI_COMMAND, &command);
	command |= PCI_COMMAND_MEMORY;
	command |= PCI_COMMAND_IO;
	pci_write_config_word(d, PCI_COMMAND, command);
	d->resource[1].flags |= 1;

	pci_enable_swapping(d);
}

static void __init pci_fixup_isp2x00(struct pci_dev *d)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(d->bus);
	bridge_t *bridge = bc->base;
	bridgereg_t devreg;
	int i;
	int slot = PCI_SLOT(d->devfn);
	unsigned int start;
	unsigned short command;

	printk("PCI: Fixing isp2x00 in [bus:slot.fn] %s\n", pci_name(d));

	/* set the resource struct for this device */
	start = (u32) (u64) bridge;	/* yes, we want to lose the upper 32 bits here */
	start |= BRIDGE_DEVIO(slot);

	d->resource[0].start = start;
	d->resource[0].end = d->resource[0].start + 0xff;
	d->resource[0].flags = IORESOURCE_IO;

	d->resource[1].start = start;
	d->resource[1].end = d->resource[0].start + 0xfff;
	d->resource[1].flags = IORESOURCE_MEM;

	/*
	 * set the bridge device(x) reg for this device
	 */
	devreg = bridge->b_device[slot].reg;
	/* point device(x) to it appropriate small window */
	devreg &= ~BRIDGE_DEV_OFF_MASK;
	devreg |= (start >> 20) & BRIDGE_DEV_OFF_MASK;
	bridge->b_device[slot].reg = devreg;

	pci_enable_swapping(d);

	/* set card's base addr reg */
	//pci_write_config_dword(d, PCI_BASE_ADDRESS_0, 0x500001);
	//pci_write_config_dword(d, PCI_BASE_ADDRESS_1, 0x8b00000);
	//pci_write_config_dword(d, PCI_ROM_ADDRESS, 0x8b20000);

	/* I got these from booting irix on system... */
	pci_write_config_dword(d, PCI_BASE_ADDRESS_0, 0x200001);
	//pci_write_config_dword(d, PCI_BASE_ADDRESS_1, 0xf800000);
	pci_write_config_dword(d, PCI_ROM_ADDRESS, 0x10200000);

	pci_write_config_dword(d, PCI_BASE_ADDRESS_1, start);
	//pci_write_config_dword(d, PCI_ROM_ADDRESS, (start | 0x20000));

	/* set cache line size */
	pci_write_config_dword(d, PCI_CACHE_LINE_SIZE, 0xf080);

	/* set pci bus timeout */
	bridge->b_bus_timeout |= BRIDGE_BUS_PCI_RETRY_HLD(0x3);
	bridge->b_wid_tflush;
	printk("PCI: bridge bus timeout= 0x%x \n", bridge->b_bus_timeout);

	/* set host error field */
	bridge->b_int_host_err = 0x44;
	bridge->b_wid_tflush;

	bridge->b_wid_tflush;	/* wait until Bridge PIO complete */
	for (i = 0; i < 8; i++)
		printk("PCI: device(%d)= 0x%x\n", i,
		       bridge->b_device[i].reg);

	/* configure device to allow bus mastering, i/o and memory mapping */
	pci_set_master(d);
	pci_read_config_word(d, PCI_COMMAND, &command);
	command |= PCI_COMMAND_MEMORY;
	command |= PCI_COMMAND_IO;
	pci_write_config_word(d, PCI_COMMAND, command);
	/*d->resource[1].flags |= 1; */
}

struct pci_fixup pcibios_fixups[] = {
	{PCI_FIXUP_HEADER, PCI_VENDOR_ID_SGI, PCI_DEVICE_ID_SGI_IOC3,
	 pci_fixup_ioc3},
	{PCI_FIXUP_HEADER, PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP1020,
	 pci_fixup_isp1020},
	{PCI_FIXUP_HEADER, PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2100,
	 pci_fixup_isp2x00},
	{PCI_FIXUP_HEADER, PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2200,
	 pci_fixup_isp2x00},
	{0}
};
