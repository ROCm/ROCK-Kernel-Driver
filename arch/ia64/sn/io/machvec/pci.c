/* 
 *
 * SNI64 specific PCI support for SNI IO.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1997, 1998, 2000-2003 Silicon Graphics, Inc.  All rights reserved.
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

extern vertex_hdl_t pci_bus_to_vertex(unsigned char);
extern vertex_hdl_t devfn_to_vertex(unsigned char bus, unsigned char devfn);

int sn_read_config(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *val)
{
	unsigned long res = 0;
	vertex_hdl_t device_vertex;

	device_vertex = devfn_to_vertex(bus->number, devfn);
	res = pciio_config_get(device_vertex, (unsigned) where, size);
	*val = (unsigned int) res;
	return PCIBIOS_SUCCESSFUL;
}

int sn_write_config(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 val)
{
	vertex_hdl_t device_vertex;

	device_vertex = devfn_to_vertex(bus->number, devfn);
	pciio_config_set( device_vertex, (unsigned)where, size, (uint64_t) val);
	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops sn_pci_ops = {
	.read = sn_read_config,
	.write = sn_write_config
};

/*
 * sn_pci_find_bios - SNIA64 pci_find_bios() platform specific code.
 */
void __init
sn_pci_find_bios(void)
{
	extern struct pci_ops *pci_root_ops;
	/*
	 * Go initialize our IO Infrastructure ..
	 */
	extern void sgi_master_io_infr_init(void);

	sgi_master_io_infr_init();

	/* sn_io_infrastructure_init(); */
	pci_root_ops = &sn_pci_ops;
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

        d->subsystem_vendor = 0;
        d->subsystem_device = 0;

}

#else
void sn_pci_find_bios(void) {}
void pci_fixup_ioc3(struct pci_dev *d) {}
struct list_head pci_root_buses;
struct list_head pci_root_buses;
struct list_head pci_devices;

#endif /* CONFIG_PCI */
