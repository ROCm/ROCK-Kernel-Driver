/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2003 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <asm/sn/sgi.h>
#include <asm/sn/pci/pci_bus_cvlink.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/simulator.h>

extern int bridge_rev_b_data_check_disable;

vertex_hdl_t busnum_to_pcibr_vhdl[MAX_PCI_XWIDGET];
nasid_t busnum_to_nid[MAX_PCI_XWIDGET];
void * busnum_to_atedmamaps[MAX_PCI_XWIDGET];
unsigned char num_bridges;
static int done_probing;
extern irqpda_t *irqpdaindr;

static int pci_bus_map_create(struct pcibr_list_s *softlistp, moduleid_t io_moduleid);
vertex_hdl_t devfn_to_vertex(unsigned char busnum, unsigned int devfn);

extern void register_pcibr_intr(int irq, pcibr_intr_t intr);

static struct sn_flush_device_list *sn_dma_flush_init(unsigned long start,
				unsigned long end,
				int idx, int pin, int slot);
extern int cbrick_type_get_nasid(nasid_t);
extern void ioconfig_bus_new_entries(void);
extern void ioconfig_get_busnum(char *, int *);
extern int iomoduleid_get(nasid_t);
extern int pcibr_widget_to_bus(vertex_hdl_t);
extern int isIO9(int);

#define IS_OPUS(nasid) (cbrick_type_get_nasid(nasid) == MODULE_OPUSBRICK)
#define IS_ALTIX(nasid) (cbrick_type_get_nasid(nasid) == MODULE_CBRICK)

/*
 * Init the provider asic for a given device
 */

static inline void __init
set_pci_provider(struct sn_device_sysdata *device_sysdata)
{
	pciio_info_t pciio_info = pciio_info_get(device_sysdata->vhdl);

	device_sysdata->pci_provider = pciio_info_pops_get(pciio_info);
}

/*
 * pci_bus_cvlink_init() - To be called once during initialization before
 *	SGI IO Infrastructure init is called.
 */
int
pci_bus_cvlink_init(void)
{

	extern int ioconfig_bus_init(void);

	memset(busnum_to_pcibr_vhdl, 0x0, sizeof(vertex_hdl_t) * MAX_PCI_XWIDGET);
	memset(busnum_to_nid, 0x0, sizeof(nasid_t) * MAX_PCI_XWIDGET);

	memset(busnum_to_atedmamaps, 0x0, sizeof(void *) * MAX_PCI_XWIDGET);

	num_bridges = 0;

	return ioconfig_bus_init();
}

/*
 * pci_bus_to_vertex() - Given a logical Linux Bus Number returns the associated
 *	pci bus vertex from the SGI IO Infrastructure.
 */
static inline vertex_hdl_t
pci_bus_to_vertex(unsigned char busnum)
{

	vertex_hdl_t	pci_bus = NULL;


	/*
	 * First get the xwidget vertex.
	 */
	pci_bus = busnum_to_pcibr_vhdl[busnum];
	return(pci_bus);
}

/*
 * devfn_to_vertex() - returns the vertex of the device given the bus, slot,
 *	and function numbers.
 */
vertex_hdl_t
devfn_to_vertex(unsigned char busnum, unsigned int devfn)
{

	int slot = 0;
	int func = 0;
	char	name[16];
	vertex_hdl_t  pci_bus = NULL;
	vertex_hdl_t	device_vertex = (vertex_hdl_t)NULL;

	/*
	 * Go get the pci bus vertex.
	 */
	pci_bus = pci_bus_to_vertex(busnum);
	if (!pci_bus) {
		/*
		 * During probing, the Linux pci code invents non-existent
		 * bus numbers and pci_dev structures and tries to access
		 * them to determine existence. Don't crib during probing.
		 */
		if (done_probing)
			printk("devfn_to_vertex: Invalid bus number %d given.\n", busnum);
		return(NULL);
	}


	/*
	 * Go get the slot&function vertex.
	 * Should call pciio_slot_func_to_name() when ready.
	 */
	slot = PCI_SLOT(devfn);
	func = PCI_FUNC(devfn);

	/*
	 * For a NON Multi-function card the name of the device looks like:
	 * ../pci/1, ../pci/2 ..
	 */
	if (func == 0) {
		sprintf(name, "%d", slot);
		if (hwgraph_traverse(pci_bus, name, &device_vertex) ==
			GRAPH_SUCCESS) {
			if (device_vertex) {
				return(device_vertex);
			}
		}
	}
			
	/*
	 * This maybe a multifunction card.  It's names look like:
	 * ../pci/1a, ../pci/1b, etc.
	 */
	sprintf(name, "%d%c", slot, 'a'+func);
	if (hwgraph_traverse(pci_bus, name, &device_vertex) != GRAPH_SUCCESS) {
		if (!device_vertex) {
			return(NULL);
		}
	}

	return(device_vertex);
}

/*
 * sn_alloc_pci_sysdata() - This routine allocates a pci controller
 *	which is expected as the pci_dev and pci_bus sysdata by the Linux
 *      PCI infrastructure.
 */
static struct pci_controller *
sn_alloc_pci_sysdata(void)
{
	struct pci_controller *pci_sysdata;

	pci_sysdata = kmalloc(sizeof(*pci_sysdata), GFP_KERNEL);
	if (!pci_sysdata)
		return NULL;

	memset(pci_sysdata, 0, sizeof(*pci_sysdata));
	return pci_sysdata;
}

/*
 * sn_pci_fixup_bus() - This routine sets up a bus's resources
 * consistent with the Linux PCI abstraction layer.
 */
static int __init
sn_pci_fixup_bus(struct pci_bus *bus)
{
	struct pci_controller *pci_sysdata;
	struct sn_widget_sysdata *widget_sysdata;

	pci_sysdata = sn_alloc_pci_sysdata();
	if  (!pci_sysdata) {
		printk(KERN_WARNING "sn_pci_fixup_bus(): Unable to "
			       "allocate memory for pci_sysdata\n");
		return -ENOMEM;
	}
	widget_sysdata = kmalloc(sizeof(struct sn_widget_sysdata),
				 GFP_KERNEL);
	if (!widget_sysdata) {
		printk(KERN_WARNING "sn_pci_fixup_bus(): Unable to "
			       "allocate memory for widget_sysdata\n");
		kfree(pci_sysdata);
		return -ENOMEM;
	}

	widget_sysdata->vhdl = pci_bus_to_vertex(bus->number);
	pci_sysdata->platform_data = (void *)widget_sysdata;
	bus->sysdata = pci_sysdata;
	return 0;
}


/*
 * sn_pci_fixup_slot() - This routine sets up a slot's resources
 * consistent with the Linux PCI abstraction layer.  Resources acquired
 * from our PCI provider include PIO maps to BAR space and interrupt
 * objects.
 */
static int
sn_pci_fixup_slot(struct pci_dev *dev)
{
	extern int bit_pos_to_irq(int);
	unsigned int irq;
	int idx;
	u16 cmd;
	vertex_hdl_t vhdl;
	unsigned long size;
	struct pci_controller *pci_sysdata;
	struct sn_device_sysdata *device_sysdata;
	pciio_intr_line_t lines = 0;
	vertex_hdl_t device_vertex;
	pciio_provider_t *pci_provider;
	pciio_intr_t intr_handle;

	/* Allocate a controller structure */
	pci_sysdata = sn_alloc_pci_sysdata();
	if (!pci_sysdata) {
		printk(KERN_WARNING "sn_pci_fixup_slot: Unable to "
			       "allocate memory for pci_sysdata\n");
		return -ENOMEM;
	}

	/* Set the device vertex */
	device_sysdata = kmalloc(sizeof(struct sn_device_sysdata), GFP_KERNEL);
	if (!device_sysdata) {
		printk(KERN_WARNING "sn_pci_fixup_slot: Unable to "
			       "allocate memory for device_sysdata\n");
		kfree(pci_sysdata);
		return -ENOMEM;
	}

	device_sysdata->vhdl = devfn_to_vertex(dev->bus->number, dev->devfn);
	pci_sysdata->platform_data = (void *) device_sysdata;
	dev->sysdata = pci_sysdata;
	set_pci_provider(device_sysdata);

	pci_read_config_word(dev, PCI_COMMAND, &cmd);

	/*
	 * Set the resources address correctly.  The assumption here
	 * is that the addresses in the resource structure has been
	 * read from the card and it was set in the card by our
	 * Infrastructure.  NOTE: PIC and TIOCP don't have big-window
	 * upport for PCI I/O space.  So by mapping the I/O space
	 * first we will attempt to use Device(x) registers for I/O
	 * BARs (which can't use big windows like MEM BARs can).
	 */
	vhdl = device_sysdata->vhdl;

	/* Allocate the IORESOURCE_IO space first */
	for (idx = 0; idx < PCI_ROM_RESOURCE; idx++) {
		unsigned long start, end, addr;

		device_sysdata->pio_map[idx] = NULL;

		if (!(dev->resource[idx].flags & IORESOURCE_IO))
			continue;

		start = dev->resource[idx].start;
		end = dev->resource[idx].end;
		size = end - start;
		if (!size)
			continue;

		addr = (unsigned long)pciio_pio_addr(vhdl, 0,
		PCIIO_SPACE_WIN(idx), 0, size,
				&device_sysdata->pio_map[idx], 0);

		if (!addr) {
			dev->resource[idx].start = 0;
			dev->resource[idx].end = 0;
			printk("sn_pci_fixup(): pio map failure for "
				"%s bar%d\n", dev->slot_name, idx);
		} else {
			addr |= __IA64_UNCACHED_OFFSET;
			dev->resource[idx].start = addr;
			dev->resource[idx].end = addr + size;
		}

		if (dev->resource[idx].flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
	}

	/* Allocate the IORESOURCE_MEM space next */
	for (idx = 0; idx < PCI_ROM_RESOURCE; idx++) {
		unsigned long start, end, addr;

		if ((dev->resource[idx].flags & IORESOURCE_IO))
			continue;

		start = dev->resource[idx].start;
		end = dev->resource[idx].end;
		size = end - start;
		if (!size)
			continue;

		addr = (unsigned long)pciio_pio_addr(vhdl, 0,
		PCIIO_SPACE_WIN(idx), 0, size,
				&device_sysdata->pio_map[idx], 0);

		if (!addr) {
			dev->resource[idx].start = 0;
			dev->resource[idx].end = 0;
			printk("sn_pci_fixup(): pio map failure for "
				"%s bar%d\n", dev->slot_name, idx);
		} else {
			addr |= __IA64_UNCACHED_OFFSET;
			dev->resource[idx].start = addr;
			dev->resource[idx].end = addr + size;
		}

		if (dev->resource[idx].flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}

        /*
	 * Assign addresses to the ROMs, but don't enable them yet
	 * Also note that we only map display card ROMs due to PIO mapping
	 * space scarcity.
	 */
        if ((dev->class >> 16) == PCI_BASE_CLASS_DISPLAY) {
                unsigned long addr;
                size = dev->resource[PCI_ROM_RESOURCE].end -
                        dev->resource[PCI_ROM_RESOURCE].start;

                if (size) {
                        addr = (unsigned long) pciio_pio_addr(vhdl, 0,
					      PCIIO_SPACE_ROM,
					      0, size, 0, PIOMAP_FIXED);
                        if (!addr) {
                                dev->resource[PCI_ROM_RESOURCE].start = 0;
                                dev->resource[PCI_ROM_RESOURCE].end = 0;
                                printk("sn_pci_fixup(): ROM pio map failure "
				       "for %s\n", dev->slot_name);
                        }
                        addr |= __IA64_UNCACHED_OFFSET;
                        dev->resource[PCI_ROM_RESOURCE].start = addr;
                        dev->resource[PCI_ROM_RESOURCE].end = addr + size;
                        if (dev->resource[PCI_ROM_RESOURCE].flags & IORESOURCE_MEM)
                                cmd |= PCI_COMMAND_MEMORY;
                }
        }

	/*
	 * Update the Command Word on the Card.
	 */
	cmd |= PCI_COMMAND_MASTER; /* If the device doesn't support */
				   /* bit gets dropped .. no harm */
	pci_write_config_word(dev, PCI_COMMAND, cmd);

	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, (unsigned char *)&lines);
	device_vertex = device_sysdata->vhdl;
	pci_provider = device_sysdata->pci_provider;
	device_sysdata->intr_handle = NULL;

	if (!lines)
		return 0;

	irqpdaindr->curr = dev;

	intr_handle = (pci_provider->intr_alloc)(device_vertex, NULL, lines, device_vertex);
	if (intr_handle == NULL) {
		printk(KERN_WARNING "sn_pci_fixup:  pcibr_intr_alloc() failed\n");
		kfree(pci_sysdata);
		kfree(device_sysdata);
		return -ENOMEM;
	}

	device_sysdata->intr_handle = intr_handle;
	irq = intr_handle->pi_irq;
	irqpdaindr->device_dev[irq] = dev;
	(pci_provider->intr_connect)(intr_handle, (intr_func_t)0, (intr_arg_t)0);
	dev->irq = irq;

	register_pcibr_intr(irq, (pcibr_intr_t)intr_handle);

	for (idx = 0; idx < PCI_ROM_RESOURCE; idx++) {
		int ibits = ((pcibr_intr_t)intr_handle)->bi_ibits;
		int i;

		size = dev->resource[idx].end -
			dev->resource[idx].start;
		if (size == 0) continue;

		for (i=0; i<8; i++) {
			if (ibits & (1 << i) ) {
				extern pcibr_info_t pcibr_info_get(vertex_hdl_t);
				device_sysdata->dma_flush_list =
				 sn_dma_flush_init(dev->resource[idx].start,
						   dev->resource[idx].end,
						   idx,
						   i,
						   PCIBR_INFO_SLOT_GET_EXT(pcibr_info_get(device_sysdata->vhdl)));
			}
		}
	}
	return 0;
}

#ifdef CONFIG_HOTPLUG_PCI_SGI

void
sn_dma_flush_clear(struct sn_flush_device_list *dma_flush_list,
                   unsigned long start, unsigned long end)
{

        int i;

        dma_flush_list->pin = -1;
        dma_flush_list->bus = -1;
        dma_flush_list->slot = -1;

        for (i = 0; i < PCI_ROM_RESOURCE; i++)
                if ((dma_flush_list->bar_list[i].start == start) &&
                    (dma_flush_list->bar_list[i].end == end)) {
                        dma_flush_list->bar_list[i].start = 0;
                        dma_flush_list->bar_list[i].end = 0;
                        break;
                }           

}

/*
 * sn_pci_unfixup_slot() - This routine frees a slot's resources
 * consistent with the Linux PCI abstraction layer.  Resources released
 * back to our PCI provider include PIO maps to BAR space and interrupt
 * objects.
 */
void
sn_pci_unfixup_slot(struct pci_dev *dev)
{
	struct sn_device_sysdata *device_sysdata;
	vertex_hdl_t vhdl;
	pciio_intr_t intr_handle;
	unsigned int irq;
	unsigned long size;
	int idx;

	device_sysdata = SN_DEVICE_SYSDATA(dev);

	vhdl = device_sysdata->vhdl;

	if (device_sysdata->dma_flush_list)
		for (idx = 0; idx < PCI_ROM_RESOURCE; idx++) {
			size = dev->resource[idx].end -
				dev->resource[idx].start;
			if (size == 0) continue;

			sn_dma_flush_clear(device_sysdata->dma_flush_list,
				   	   dev->resource[idx].start,
				   	   dev->resource[idx].end);
		}

	intr_handle = device_sysdata->intr_handle;
	if (intr_handle) {
		extern void unregister_pcibr_intr(int, pcibr_intr_t);
		irq = intr_handle->pi_irq;
		irqpdaindr->device_dev[irq] = NULL;
		unregister_pcibr_intr(irq, (pcibr_intr_t) intr_handle);
		pciio_intr_disconnect(intr_handle);
		pciio_intr_free(intr_handle);
	}

	for (idx = 0; idx < PCI_ROM_RESOURCE; idx++) {
		if (device_sysdata->pio_map[idx]) {
			pciio_piomap_done (device_sysdata->pio_map[idx]);
			pciio_piomap_free (device_sysdata->pio_map[idx]);
		}
	}

}
#endif /* CONFIG_HOTPLUG_PCI_SGI */

struct sn_flush_nasid_entry flush_nasid_list[MAX_NASIDS];

/* Initialize the data structures for flushing write buffers after a PIO read.
 * The theory is:
 * Take an unused int. pin and associate it with a pin that is in use.
 * After a PIO read, force an interrupt on the unused pin, forcing a write buffer flush
 * on the in use pin.  This will prevent the race condition between PIO read responses and
 * DMA writes.
 */
static struct sn_flush_device_list *
sn_dma_flush_init(unsigned long start, unsigned long end, int idx, int pin, int slot)
{
	nasid_t nasid;
	unsigned long dnasid;
	int wid_num;
	int bus;
	struct sn_flush_device_list *p;
	void *b;
	int bwin;
	int i;

	nasid = NASID_GET(start);
	wid_num = SWIN_WIDGETNUM(start);
	bus = (start >> 23) & 0x1;
	bwin = BWIN_WINDOWNUM(start);

	if (flush_nasid_list[nasid].widget_p == NULL) {
		flush_nasid_list[nasid].widget_p = (struct sn_flush_device_list **)kmalloc((HUB_WIDGET_ID_MAX+1) *
			sizeof(struct sn_flush_device_list *), GFP_KERNEL);
		if (!flush_nasid_list[nasid].widget_p) {
			printk(KERN_WARNING "sn_dma_flush_init: Cannot allocate memory for nasid list\n");
			return NULL;
		}
		memset(flush_nasid_list[nasid].widget_p, 0, (HUB_WIDGET_ID_MAX+1) * sizeof(struct sn_flush_device_list *));
	}
	if (bwin > 0) {
		int itte_index = bwin - 1;
		unsigned long itte;

		itte = HUB_L(IIO_ITTE_GET(nasid, itte_index));
		flush_nasid_list[nasid].iio_itte[bwin] = itte;
		wid_num = (itte >> IIO_ITTE_WIDGET_SHIFT)
				& IIO_ITTE_WIDGET_MASK;
		bus = itte & IIO_ITTE_OFFSET_MASK;
		if (bus == 0x4 || bus == 0x8) {
			bus = 0;
		} else {
			bus = 1;
		}
	}

	/* if it's IO9, bus 1, we don't care about slots 1 and 4.  This is
	 * because these are the IOC4 slots and we don't flush them.
	 */
	if (isIO9(nasid) && bus == 0 && (slot == 1 || slot == 4)) {
		return NULL;
	}
	if (flush_nasid_list[nasid].widget_p[wid_num] == NULL) {
		flush_nasid_list[nasid].widget_p[wid_num] = (struct sn_flush_device_list *)kmalloc(
			DEV_PER_WIDGET * sizeof (struct sn_flush_device_list), GFP_KERNEL);
		if (!flush_nasid_list[nasid].widget_p[wid_num]) {
			printk(KERN_WARNING "sn_dma_flush_init: Cannot allocate memory for nasid sub-list\n");
			return NULL;
		}
		memset(flush_nasid_list[nasid].widget_p[wid_num], 0,
			DEV_PER_WIDGET * sizeof (struct sn_flush_device_list));
		p = &flush_nasid_list[nasid].widget_p[wid_num][0];
		for (i=0; i<DEV_PER_WIDGET;i++) {
			p->bus = -1;
			p->pin = -1;
			p->slot = -1;
			p++;
		}
	}

	p = &flush_nasid_list[nasid].widget_p[wid_num][0];
	for (i=0;i<DEV_PER_WIDGET; i++) {
		if (p->pin == pin && p->bus == bus && p->slot == slot) break;
		if (p->pin < 0) {
			p->pin = pin;
			p->bus = bus;
			p->slot = slot;
			break;
		}
		p++;
	}

	for (i=0; i<PCI_ROM_RESOURCE; i++) {
		if (p->bar_list[i].start == 0) {
			p->bar_list[i].start = start;
			p->bar_list[i].end = end;
			break;
		}
	}
	b = (void *)(NODE_SWIN_BASE(nasid, wid_num) | (bus << 23) );

	/* If it's IO9, then slot 2 maps to slot 7 and slot 6 maps to slot 8.
	 * To see this is non-trivial.  By drawing pictures and reading manuals and talking
	 * to HW guys, we can see that on IO9 bus 1, slots 7 and 8 are always unused.
	 * Further, since we short-circuit slots  1, 3, and 4 above, we only have to worry
	 * about the case when there is a card in slot 2.  A multifunction card will appear
	 * to be in slot 6 (from an interrupt point of view) also.  That's the  most we'll
	 * have to worry about.  A four function card will overload the interrupt lines in
	 * slot 2 and 6.
	 * We also need to special case the 12160 device in slot 3.  Fortunately, we have
	 * a spare intr. line for pin 4, so we'll use that for the 12160.
	 * All other buses have slot 3 and 4 and slots 7 and 8 unused.  Since we can only
	 * see slots 1 and 2 and slots 5 and 6 coming through here for those buses (this
	 * is true only on Pxbricks with 2 physical slots per bus), we just need to add
	 * 2 to the slot number to find an unused slot.
	 * We have convinced ourselves that we will never see a case where two different cards
	 * in two different slots will ever share an interrupt line, so there is no need to
	 * special case this.
	 */

	if (isIO9(nasid) && ( (IS_ALTIX(nasid) && wid_num == 0xc)
				|| (IS_OPUS(nasid) && wid_num == 0xf) )
				&& bus == 0) {
		if (pin == 1) {
			p->force_int_addr = (unsigned long)pcireg_bridge_force_always_addr_get(b, 6);
			pcireg_bridge_intr_device_bit_set(b, (1<<18));
			dnasid = NASID_GET(virt_to_phys(&p->flush_addr));
			pcireg_bridge_intr_addr_set(b, 6, ((virt_to_phys(&p->flush_addr) & 0xfffffffff) |
					(dnasid << 36) | (0xfUL << 48)));
		} else if (pin == 2) { /* 12160 SCSI device in IO9 */
			p->force_int_addr = (unsigned long)pcireg_bridge_force_always_addr_get(b, 4);
			pcireg_bridge_intr_device_bit_set(b, (2<<12));
			dnasid = NASID_GET(virt_to_phys(&p->flush_addr));
			pcireg_bridge_intr_addr_set(b, 4,
					((virt_to_phys(&p->flush_addr) & 0xfffffffff) |
					(dnasid << 36) | (0xfUL << 48)));
		} else { /* slot == 6 */
			p->force_int_addr = (unsigned long)pcireg_bridge_force_always_addr_get(b, 7);
			pcireg_bridge_intr_device_bit_set(b, (5<<21));
			dnasid = NASID_GET(virt_to_phys(&p->flush_addr));
			pcireg_bridge_intr_addr_set(b, 7,
					((virt_to_phys(&p->flush_addr) & 0xfffffffff) |
					(dnasid << 36) | (0xfUL << 48)));
		}
	} else {
		p->force_int_addr = (unsigned long)pcireg_bridge_force_always_addr_get(b, (pin +2));
		pcireg_bridge_intr_device_bit_set(b, (pin << (pin * 3)));
		dnasid = NASID_GET(virt_to_phys(&p->flush_addr));
		pcireg_bridge_intr_addr_set(b, (pin + 2),
				((virt_to_phys(&p->flush_addr) & 0xfffffffff) |
				(dnasid << 36) | (0xfUL << 48)));
	}
	return p;
}


/*
 * linux_bus_cvlink() Creates a link between the Linux PCI Bus number
 *	to the actual hardware component that it represents:
 *	/dev/hw/linux/busnum/0 -> ../../../hw/module/001c01/slab/0/Ibrick/xtalk/15/pci
 *
 *	The bus vertex, when called to devfs_generate_path() returns:
 *		hw/module/001c01/slab/0/Ibrick/xtalk/15/pci
 *		hw/module/001c01/slab/1/Pbrick/xtalk/12/pci-x/0
 *		hw/module/001c01/slab/1/Pbrick/xtalk/12/pci-x/1
 */
void
linux_bus_cvlink(void)
{
	char name[8];
	int index;
	
	for (index=0; index < MAX_PCI_XWIDGET; index++) {
		if (!busnum_to_pcibr_vhdl[index])
			continue;

		sprintf(name, "%x", index);
		(void) hwgraph_edge_add(linux_busnum, busnum_to_pcibr_vhdl[index],
				name);
	}
}

/*
 * pci_bus_map_create() - Called by pci_bus_to_hcl_cvlink() to finish the job.
 *
 *	Linux PCI Bus numbers are assigned from lowest module_id numbers
 *	(rack/slot etc.)
 */
static int
pci_bus_map_create(struct pcibr_list_s *softlistp, moduleid_t moduleid)
{
	
	int basebus_num, bus_number;
	vertex_hdl_t pci_bus = softlistp->bl_vhdl;
	char moduleid_str[16];

	memset(moduleid_str, 0, 16);
	format_module_id(moduleid_str, moduleid, MODULE_FORMAT_BRIEF);
	(void) ioconfig_get_busnum((char *)moduleid_str, &basebus_num);

	/*
	 * Assign the correct bus number and also the nasid of this
	 * pci Xwidget.
	 */
	bus_number = basebus_num + pcibr_widget_to_bus(pci_bus);
#ifdef DEBUG
	{
	char hwpath[MAXDEVNAME] = "\0";
	extern int hwgraph_vertex_name_get(vertex_hdl_t, char *, uint);

	pcibr_soft_t pcibr_soft = softlistp->bl_soft;
	hwgraph_vertex_name_get(pci_bus, hwpath, MAXDEVNAME);
	printk("%s:\n\tbus_num %d, basebus_num %d, brick_bus %d, "
		"bus_vhdl 0x%lx, brick_type %d\n", hwpath, bus_number,
		basebus_num, pcibr_widget_to_bus(pci_bus),
		(uint64_t)pci_bus, pcibr_soft->bs_bricktype);
	}
#endif
	busnum_to_pcibr_vhdl[bus_number] = pci_bus;

	/*
	 * Pre assign DMA maps needed for 32 Bits Page Map DMA.
	 */
	busnum_to_atedmamaps[bus_number] = (void *) vmalloc(
			sizeof(struct pcibr_dmamap_s)*MAX_ATE_MAPS);
	if (busnum_to_atedmamaps[bus_number] <= 0) {
		printk("pci_bus_map_create: Cannot allocate memory for ate maps\n");
		return -1;
	}
	memset(busnum_to_atedmamaps[bus_number], 0x0,
			sizeof(struct pcibr_dmamap_s) * MAX_ATE_MAPS);
	return(0);
}

/*
 * pci_bus_to_hcl_cvlink() - This routine is called after SGI IO Infrastructure
 *      initialization has completed to set up the mappings between PCI BRIDGE
 *      ASIC and logical pci bus numbers.
 *
 *      Must be called before pci_init() is invoked.
 */
int
pci_bus_to_hcl_cvlink(void)
{
	int i;
	extern pcibr_list_p pcibr_list;

	for (i = 0; i < nummodules; i++) {
		struct pcibr_list_s *softlistp = pcibr_list;
		struct pcibr_list_s *first_in_list = NULL;
		struct pcibr_list_s *last_in_list = NULL;

		/* Walk the list of pcibr_soft structs looking for matches */
		while (softlistp) {
			struct pcibr_soft_s *pcibr_soft = softlistp->bl_soft;
			moduleid_t moduleid;
			
			/* Is this PCI bus associated with this moduleid? */
			moduleid = NODE_MODULEID(
				nasid_to_cnodeid(pcibr_soft->bs_nasid));
			if (sn_modules[i]->id == moduleid) {
				struct pcibr_list_s *new_element;

				new_element = kmalloc(sizeof (struct pcibr_soft_s), GFP_KERNEL);
				if (new_element == NULL) {
					printk("%s: Couldn't allocate memory\n",__FUNCTION__);
					return -ENOMEM;
				}
				new_element->bl_soft = softlistp->bl_soft;
				new_element->bl_vhdl = softlistp->bl_vhdl;
				new_element->bl_next = NULL;

				/* list empty so just put it on the list */
				if (first_in_list == NULL) {
					first_in_list = new_element;
					last_in_list = new_element;
					softlistp = softlistp->bl_next;
					continue;
				}

				/*
				 * BASEIO IObricks attached to a module have
				 * a higher priority than non BASEIO IOBricks
				 * when it comes to persistant pci bus
				 * numbering, so put them on the front of the
				 * list.
				 */
				if (isIO9(pcibr_soft->bs_nasid)) {
					new_element->bl_next = first_in_list;
					first_in_list = new_element;
				} else {
					last_in_list->bl_next = new_element;
					last_in_list = new_element;
				}
			}
			softlistp = softlistp->bl_next;
		}
				
		/*
		 * We now have a list of all the pci bridges associated with
		 * the module_id, sn_modules[i].  Call pci_bus_map_create() for
		 * each pci bridge
		 */
		softlistp = first_in_list;
		while (softlistp) {
			moduleid_t iobrick;
			struct pcibr_list_s *next = softlistp->bl_next;
			iobrick = iomoduleid_get(softlistp->bl_soft->bs_nasid);
			pci_bus_map_create(softlistp, iobrick);
			kfree(softlistp);
			softlistp = next;
		}
	}

	/*
	 * Create the Linux PCI bus number vertex link.
	 */
	(void)linux_bus_cvlink();
	(void)ioconfig_bus_new_entries();

	return(0);
}

/*
 * Ugly hack to get PCI setup until we have a proper ACPI namespace.
 */

#define PCI_BUSES_TO_SCAN 256

extern struct pci_ops sn_pci_ops;
int __init
sn_pci_init (void)
{
	int i = 0;
	struct pci_controller *controller;
	struct list_head *ln;
	struct pci_bus *pci_bus = NULL;
	struct pci_dev *pci_dev = NULL;
	int ret;
#ifdef CONFIG_PROC_FS
	extern void register_sn_procfs(void);
#endif
	extern void sgi_master_io_infr_init(void);
	extern void sn_init_cpei_timer(void);


	if (!ia64_platform_is("sn2") || IS_RUNNING_ON_SIMULATOR())
		return 0;

	/*
	 * This is needed to avoid bounce limit checks in the blk layer
	 */
	ia64_max_iommu_merge_mask = ~PAGE_MASK;

	/*
	 * set pci_raw_ops, etc.
	 */
	sgi_master_io_infr_init();

	sn_init_cpei_timer();

#ifdef CONFIG_PROC_FS
	register_sn_procfs();
#endif

	controller = kmalloc(sizeof(struct pci_controller), GFP_KERNEL);
	if (!controller) {
		printk(KERN_WARNING "cannot allocate PCI controller\n");
		return 0;
	}

	memset(controller, 0, sizeof(struct pci_controller));

	for (i = 0; i < PCI_BUSES_TO_SCAN; i++)
		if (pci_bus_to_vertex(i))
			pci_scan_bus(i, &sn_pci_ops, controller);

	done_probing = 1;

	/*
	 * Initialize the pci bus vertex in the pci_bus struct.
	 */
	for( ln = pci_root_buses.next; ln != &pci_root_buses; ln = ln->next) {
		pci_bus = pci_bus_b(ln);
		ret = sn_pci_fixup_bus(pci_bus);
		if ( ret ) {
			printk(KERN_WARNING
				"sn_pci_fixup: sn_pci_fixup_bus fails : error %d\n",
					ret);
			return 0;
		}
	}

	/*
	 * set the root start and end so that drivers calling check_region()
	 * won't see a conflict
	 */
	ioport_resource.start = 0xc000000000000000;
	ioport_resource.end = 0xcfffffffffffffff;

	/*
	 * Set the root start and end for Mem Resource.
	 */
	iomem_resource.start = 0;
	iomem_resource.end = 0xffffffffffffffff;

	/*
	 * Initialize the device vertex in the pci_dev struct.
	 */
	while ((pci_dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, pci_dev)) != NULL) {
		ret = sn_pci_fixup_slot(pci_dev);
		if ( ret ) {
			printk(KERN_WARNING
				"sn_pci_fixup: sn_pci_fixup_slot fails : error %d\n",
					ret);
			return 0;
		}
	}

	return 0;
}

subsys_initcall(sn_pci_init);
