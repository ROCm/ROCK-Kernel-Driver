/* 
 *
 * SNI64 specific PCI support for SNI IO.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1997, 1998, 2000-2001 Silicon Graphics, Inc.  All rights reserved.
 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/config.h>
#include <linux/pci.h>
#include <asm/sn/types.h>
#include <asm/sn/sgi.h>
#include <asm/sn/io.h>
#include <asm/sn/driver.h>
#include <asm/sn/iograph.h>
#include <asm/param.h>
#include <asm/sn/pio.h>
#include <asm/sn/xtalk/xwidget.h>
#include <asm/sn/sn_private.h>
#include <asm/sn/addrs.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/hcl_util.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/pci/pcibr_private.h>
#include <asm/sn/pci/bridge.h>

#ifdef DEBUG_CONFIG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif



#ifdef CONFIG_PCI

extern devfs_handle_t pci_bus_to_vertex(unsigned char);
extern devfs_handle_t devfn_to_vertex(unsigned char bus, unsigned char devfn);

/*
 * snia64_read - Read from the config area of the device.
 */
static int snia64_read (struct pci_bus *bus, unsigned char devfn,
                                   int where, int size, unsigned char *val)
{
	unsigned long res = 0;
	devfs_handle_t device_vertex;

	if ( (bus == NULL) || (val == (unsigned char *)0) ) {
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	device_vertex = devfn_to_vertex(bus->number, devfn);
	if (!device_vertex) {
		DBG("%s : nonexistent device: bus= 0x%x  slot= 0x%x  func= 0x%x\n", 
		__FUNCTION__, bus->number, PCI_SLOT(devfn), PCI_FUNC(devfn));
		return(-1);
	}
	res = pciio_config_get(device_vertex, (unsigned) where, size);
	*val = (unsigned char) res;
	return PCIBIOS_SUCCESSFUL;
}

/*
 * snia64_write - Writes to the config area of the device.
 */
static int snia64_write (struct pci_bus *bus, unsigned char devfn,
                                    int where, int size, unsigned char val)
{
	devfs_handle_t device_vertex;

	if ( bus == NULL) {
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	/* 
	 * if it's an IOC3 then we bail out, we special
	 * case them with pci_fixup_ioc3
	 */
	/* Starting 2.5.32 struct pci_dev is not passed down */
	/*if (dev->vendor == PCI_VENDOR_ID_SGI && 
	    dev->device == PCI_DEVICE_ID_SGI_IOC3 )
		return PCIBIOS_SUCCESSFUL;
	*/

	device_vertex = devfn_to_vertex(bus->number, devfn);
	if (!device_vertex) {
		DBG("%s : nonexistent device: bus= 0x%x  slot= 0x%x  func= 0x%x\n", 
		__FUNCTION__, bus->number, PCI_SLOT(devfn), PCI_FUNC(devfn));
		return(-1);
	}
	pciio_config_set( device_vertex, (unsigned)where, size, (uint64_t) val);

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops snia64_pci_ops = {
	.read =		snia64_read,
	.write = 	snia64_write,
};

/*
 * snia64_pci_find_bios - SNIA64 pci_find_bios() platform specific code.
 */
void __init
sn1_pci_find_bios(void)
{
	extern struct pci_ops pci_conf;
	/*
	 * Go initialize our IO Infrastructure ..
	 */
	extern void sgi_master_io_infr_init(void);

	sgi_master_io_infr_init();

	/* sn1_io_infrastructure_init(); */
	pci_conf = snia64_pci_ops;
}

void
pci_fixup_ioc3(struct pci_dev *d)
{
        int 		i;
	unsigned int 	size;

        /* IOC3 only decodes 0x20 bytes of the config space, reading
	 * beyond that is relatively benign but writing beyond that
	 * (especially the base address registers) will shut down the
	 * pci bus...so avoid doing so.
	 * NOTE: this means we can't program the intr_pin into the device,
	 *       currently we hack this with special code in 
	 *	 sgi_pci_intr_support()
	 */
        DBG("pci_fixup_ioc3: Fixing base addresses for ioc3 device %s\n", d->slot_name);

	/* I happen to know from the spec that the ioc3 needs only 0xfffff 
	 * The standard pci trick of writing ~0 to the baddr and seeing
	 * what comes back doesn't work with the ioc3
	 */
	size = 0xfffff;
	d->resource[0].end = (unsigned long) d->resource[0].start + (unsigned long) size;

	/*
	 * Zero out the resource structure .. because we did not go through 
	 * the normal PCI Infrastructure Init, garbbage are left in these 
	 * fileds.
	 */
        for (i = 1; i <= PCI_ROM_RESOURCE; i++) {
                d->resource[i].start = 0UL;
                d->resource[i].end = 0UL;
                d->resource[i].flags = 0UL;
        }

	/*
	 * Hardcode Device 4 register(IOC3 is in Slot 4) to set the 
	 * DEV_DIRECT bit.  This will not work if IOC3 is not on Slot 
	 * 4.
	 */
	DBG("pci_fixup_ioc3: FIXME .. need to take NASID into account when setting IOC3 devreg 0x%x\n", *(volatile u32 *)0xc0000a000f000220);

 	*(volatile u32 *)0xc0000a000f000220 |= 0x90000; 

        d->subsystem_vendor = 0;
        d->subsystem_device = 0;

}

#else
void sn1_pci_find_bios(void) {}
void pci_fixup_ioc3(struct pci_dev *d) {}
struct list_head pci_root_buses;
struct list_head pci_root_buses;
struct list_head pci_devices;

#endif /* CONFIG_PCI */
