/* 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SNI64 specific PCI support for SNI IO.
 *
 * Copyright (C) 1997, 1998, 2000 Colin Ngam
 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/config.h>
#include <linux/pci.h>
#include <asm/sn/types.h>
#include <asm/sn/sgi.h>
#include <asm/sn/cmn_err.h>
#include <asm/sn/iobus.h>
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
 * snia64_read_config_byte - Read a byte from the config area of the device.
 */
static int snia64_read_config_byte (struct pci_dev *dev,
                                   int where, unsigned char *val)
{
	unsigned long res = 0;
	unsigned size = 1;
	devfs_handle_t device_vertex;

	if ( (dev == (struct pci_dev *)0) || (val == (unsigned char *)0) ) {
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	device_vertex = devfn_to_vertex(dev->bus->number, dev->devfn);
	if (!device_vertex) {
		DBG("%s : nonexistent device: bus= 0x%x  slot= 0x%x  func= 0x%x\n", 
		__FUNCTION__, dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
		return(-1);
	}
	res = pciio_config_get(device_vertex, (unsigned) where, size);
	*val = (unsigned char) res;
	return PCIBIOS_SUCCESSFUL;
}

/*
 * snia64_read_config_word - Read 2 bytes from the config area of the device.
 */
static int snia64_read_config_word (struct pci_dev *dev,
                                   int where, unsigned short *val)
{
	unsigned long res = 0;
	unsigned size = 2; /* 2 bytes */
	devfs_handle_t device_vertex;

	if ( (dev == (struct pci_dev *)0) || (val == (unsigned short *)0) ) {
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	device_vertex = devfn_to_vertex(dev->bus->number, dev->devfn);
	if (!device_vertex) {
		DBG("%s : nonexistent device: bus= 0x%x  slot= 0x%x  func= 0x%x\n", 
		__FUNCTION__, dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
		return(-1);
	}
	res = pciio_config_get(device_vertex, (unsigned) where, size);
	*val = (unsigned short) res;
	return PCIBIOS_SUCCESSFUL;
}

/*
 * snia64_read_config_dword - Read 4 bytes from the config area of the device.
 */
static int snia64_read_config_dword (struct pci_dev *dev,
                                    int where, unsigned int *val)
{
	unsigned long res = 0;
	unsigned size = 4; /* 4 bytes */
	devfs_handle_t device_vertex;

	if (where & 3) {
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}
	if ( (dev == (struct pci_dev *)0) || (val == (unsigned int *)0) ) {
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	device_vertex = devfn_to_vertex(dev->bus->number, dev->devfn);
	if (!device_vertex) {
		DBG("%s : nonexistent device: bus= 0x%x  slot= 0x%x  func= 0x%x\n", 
		__FUNCTION__, dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
		return(-1);
	}
	res = pciio_config_get(device_vertex, (unsigned) where, size);
	*val = (unsigned int) res;
	return PCIBIOS_SUCCESSFUL;
}

/*
 * snia64_write_config_byte - Writes 1 byte to the config area of the device.
 */
static int snia64_write_config_byte (struct pci_dev *dev,
                                    int where, unsigned char val)
{
	devfs_handle_t device_vertex;

	if ( dev == (struct pci_dev *)0 ) {
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	/* 
	 * if it's an IOC3 then we bail out, we special
	 * case them with pci_fixup_ioc3
	 */
	if (dev->vendor == PCI_VENDOR_ID_SGI && 
	    dev->device == PCI_DEVICE_ID_SGI_IOC3 )
		return PCIBIOS_SUCCESSFUL;

	device_vertex = devfn_to_vertex(dev->bus->number, dev->devfn);
	if (!device_vertex) {
		DBG("%s : nonexistent device: bus= 0x%x  slot= 0x%x  func= 0x%x\n", 
		__FUNCTION__, dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
		return(-1);
	}
	pciio_config_set( device_vertex, (unsigned)where, 1, (uint64_t) val);

	return PCIBIOS_SUCCESSFUL;
}

/*
 * snia64_write_config_word - Writes 2 bytes to the config area of the device.
 */
static int snia64_write_config_word (struct pci_dev *dev,
                                    int where, unsigned short val)
{
	devfs_handle_t device_vertex = NULL;

	if (where & 1) {
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}
	if ( dev == (struct pci_dev *)0 ) {
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	/* 
	 * if it's an IOC3 then we bail out, we special
	 * case them with pci_fixup_ioc3
	 */
	if (dev->vendor == PCI_VENDOR_ID_SGI && 
	    dev->device == PCI_DEVICE_ID_SGI_IOC3)
		return PCIBIOS_SUCCESSFUL;

	device_vertex = devfn_to_vertex(dev->bus->number, dev->devfn);
	if (!device_vertex) {
		DBG("%s : nonexistent device: bus= 0x%x  slot= 0x%x  func= 0x%x\n", 
		__FUNCTION__, dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
		return(-1);
	}
	pciio_config_set( device_vertex, (unsigned)where, 2, (uint64_t) val);

	return PCIBIOS_SUCCESSFUL;
}

/*
 * snia64_write_config_dword - Writes 4 bytes to the config area of the device.
 */
static int snia64_write_config_dword (struct pci_dev *dev,
                                     int where, unsigned int val)
{
	devfs_handle_t device_vertex;

	if (where & 3) {
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}
	if ( dev == (struct pci_dev *)0 ) {
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	/* 
	 * if it's an IOC3 then we bail out, we special
	 * case them with pci_fixup_ioc3
	 */
	if (dev->vendor == PCI_VENDOR_ID_SGI && 
	    dev->device == PCI_DEVICE_ID_SGI_IOC3)
		return PCIBIOS_SUCCESSFUL;

	device_vertex = devfn_to_vertex(dev->bus->number, dev->devfn);
	if (!device_vertex) {
		DBG("%s : nonexistent device: bus= 0x%x  slot= 0x%x  func= 0x%x\n", 
		__FUNCTION__, dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
		return(-1);
	}
	pciio_config_set( device_vertex, (unsigned)where, 4, (uint64_t) val);

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops snia64_pci_ops = {
	snia64_read_config_byte,
	snia64_read_config_word,
	snia64_read_config_dword,
	snia64_write_config_byte,
	snia64_write_config_word,
	snia64_write_config_dword
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

#ifdef BRINGUP
	if ( IS_RUNNING_ON_SIMULATOR() )
		return;
#endif
	/* sn1_io_infrastructure_init(); */
	pci_conf = snia64_pci_ops;
}

void
pci_fixup_ioc3(struct pci_dev *d)
{
        int 		i;
	int 		slot;
	unsigned long 	res = 0;
	unsigned int 	val, size;
	int 		ret;
	u_short 	command;

	devfs_handle_t 	device_vertex;
	devfs_handle_t	bridge_vhdl = pci_bus_to_vertex(d->bus->number);
	pcibr_soft_t 	pcibr_soft = (pcibr_soft_t) hwgraph_fastinfo_get(bridge_vhdl);
	devfs_handle_t  xconn_vhdl = pcibr_soft->bs_conn;
	bridge_t 	*bridge = pcibr_soft->bs_base;
	bridgereg_t 	devreg;

        /* IOC3 only decodes 0x20 bytes of the config space, reading
	 * beyond that is relatively benign but writing beyond that
	 * (especially the base address registers) will shut down the
	 * pci bus...so avoid doing so.
	 * NOTE: this means we can't program the intr_pin into the device,
	 *       currently we hack this with special code in 
	 *	 sgi_pci_intr_support()
	 */
        printk("pci_fixup_ioc3: Fixing base addresses for ioc3 device %s\n", d->slot_name);

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
	*(volatile u32 *)0xc0000a000f000220 |= 0x90000;

        d->subsystem_vendor = 0;
        d->subsystem_device = 0;

}

#endif /* CONFIG_PCI */
