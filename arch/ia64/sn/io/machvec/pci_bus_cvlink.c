/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2003 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/sched.h>
#include <linux/ioport.h>
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
#include <asm/sn/intr.h>
#include <asm/sn/xtalk/xtalkaddrs.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/nodepda.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/pci/pcibr_private.h>
#include <asm/sn/pci/pci_bus_cvlink.h>
#include <asm/sn/simulator.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/arch.h>

extern int bridge_rev_b_data_check_disable;

vertex_hdl_t busnum_to_pcibr_vhdl[MAX_PCI_XWIDGET];
nasid_t busnum_to_nid[MAX_PCI_XWIDGET];
void * busnum_to_atedmamaps[MAX_PCI_XWIDGET];
unsigned char num_bridges;
static int done_probing;
extern irqpda_t *irqpdaindr;

static int pci_bus_map_create(vertex_hdl_t xtalk, char * io_moduleid);
vertex_hdl_t devfn_to_vertex(unsigned char busnum, unsigned int devfn);

extern void register_pcibr_intr(int irq, pcibr_intr_t intr);

void sn_dma_flush_init(unsigned long start, unsigned long end, int idx, int pin, int slot);


/*
 * For the given device, initialize whether it is a PIC device.
 */
static void
set_isPIC(struct sn_device_sysdata *device_sysdata)
{
	pciio_info_t pciio_info = pciio_info_get(device_sysdata->vhdl);
	pcibr_soft_t pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);

	device_sysdata->isPIC = IS_PIC_SOFT(pcibr_soft);;
}

/*
 * pci_bus_cvlink_init() - To be called once during initialization before 
 *	SGI IO Infrastructure init is called.
 */
void
pci_bus_cvlink_init(void)
{

	extern void ioconfig_bus_init(void);

	memset(busnum_to_pcibr_vhdl, 0x0, sizeof(vertex_hdl_t) * MAX_PCI_XWIDGET);
	memset(busnum_to_nid, 0x0, sizeof(nasid_t) * MAX_PCI_XWIDGET);

	memset(busnum_to_atedmamaps, 0x0, sizeof(void *) * MAX_PCI_XWIDGET);

	num_bridges = 0;

	ioconfig_bus_init();
}

/*
 * pci_bus_to_vertex() - Given a logical Linux Bus Number returns the associated 
 *	pci bus vertex from the SGI IO Infrastructure.
 */
vertex_hdl_t
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
 * For the given device, initialize the addresses for both the Device(x) Flush 
 * Write Buffer register and the Xbow Flush Register for the port the PCI bus 
 * is connected.
 */
static void
set_flush_addresses(struct pci_dev *device_dev, 
	struct sn_device_sysdata *device_sysdata)
{
	pciio_info_t pciio_info = pciio_info_get(device_sysdata->vhdl);
	pciio_slot_t pciio_slot = pciio_info_slot_get(pciio_info);
	pcibr_soft_t pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    	bridge_t               *bridge = pcibr_soft->bs_base;
	nasid_t			nasid;

	/*
	 * Get the nasid from the bridge.
	 */
	nasid = NASID_GET(device_sysdata->dma_buf_sync);
	if (IS_PIC_DEVICE(device_dev)) {
		device_sysdata->dma_buf_sync = (volatile unsigned int *)
			&bridge->b_wr_req_buf[pciio_slot].reg;
		device_sysdata->xbow_buf_sync = (volatile unsigned int *)
			XBOW_PRIO_LINKREGS_PTR(NODE_SWIN_BASE(nasid, 0),
			pcibr_soft->bs_xid);
	} else {
		/*
		 * Accessing Xbridge and Xbow register when SHUB swapoper is on!.
		 */
		device_sysdata->dma_buf_sync = (volatile unsigned int *)
			((uint64_t)&(bridge->b_wr_req_buf[pciio_slot].reg)^4);
		device_sysdata->xbow_buf_sync = (volatile unsigned int *)
			((uint64_t)(XBOW_PRIO_LINKREGS_PTR(
			NODE_SWIN_BASE(nasid, 0), pcibr_soft->bs_xid)) ^ 4);
	}

#ifdef DEBUG
	printk("set_flush_addresses: dma_buf_sync %p xbow_buf_sync %p\n", 
		device_sysdata->dma_buf_sync, device_sysdata->xbow_buf_sync);

printk("set_flush_addresses: dma_buf_sync\n");
	while((volatile unsigned int )*device_sysdata->dma_buf_sync);
printk("set_flush_addresses: xbow_buf_sync\n");
	while((volatile unsigned int )*device_sysdata->xbow_buf_sync);
#endif

}

struct sn_flush_nasid_entry flush_nasid_list[MAX_NASIDS];

// Initialize the data structures for flushing write buffers after a PIO read.
// The theory is: 
// Take an unused int. pin and associate it with a pin that is in use.
// After a PIO read, force an interrupt on the unused pin, forcing a write buffer flush
// on the in use pin.  This will prevent the race condition between PIO read responses and 
// DMA writes.
void
sn_dma_flush_init(unsigned long start, unsigned long end, int idx, int pin, int slot) {
	nasid_t nasid; 
	unsigned long dnasid;
	int wid_num;
	int bus;
	struct sn_flush_device_list *p;
	bridge_t *b;
	bridgereg_t dev_sel;
	extern int isIO9(int);
	int bwin;
	int i;

	nasid = NASID_GET(start);
	wid_num = SWIN_WIDGETNUM(start);
	bus = (start >> 23) & 0x1;
	bwin = BWIN_WINDOWNUM(start);

	if (flush_nasid_list[nasid].widget_p == NULL) {
		flush_nasid_list[nasid].widget_p = (struct sn_flush_device_list **)kmalloc((HUB_WIDGET_ID_MAX+1) *
			sizeof(struct sn_flush_device_list *), GFP_KERNEL);
		memset(flush_nasid_list[nasid].widget_p, 0, (HUB_WIDGET_ID_MAX+1) * sizeof(struct sn_flush_device_list *));
	}
	if (bwin > 0) {
		bwin--;
		switch (bwin) {
			case 0:
				flush_nasid_list[nasid].iio_itte1 = HUB_L(IIO_ITTE_GET(nasid, 0));
				wid_num = ((flush_nasid_list[nasid].iio_itte1) >> 8) & 0xf;
				bus = flush_nasid_list[nasid].iio_itte1 & 0xf;
				if (bus == 0x4 || bus == 0x8)
					bus = 0;
				else
					bus = 1;
				break;
			case 1:
				flush_nasid_list[nasid].iio_itte2 = HUB_L(IIO_ITTE_GET(nasid, 1));
				wid_num = ((flush_nasid_list[nasid].iio_itte2) >> 8) & 0xf;
				bus = flush_nasid_list[nasid].iio_itte2 & 0xf;
				if (bus == 0x4 || bus == 0x8)
					bus = 0;
				else
					bus = 1;
				break;
			case 2:
				flush_nasid_list[nasid].iio_itte3 = HUB_L(IIO_ITTE_GET(nasid, 2));
				wid_num = ((flush_nasid_list[nasid].iio_itte3) >> 8) & 0xf;
				bus = flush_nasid_list[nasid].iio_itte3 & 0xf;
				if (bus == 0x4 || bus == 0x8)
					bus = 0;
				else
					bus = 1;
				break;
			case 3:
				flush_nasid_list[nasid].iio_itte4 = HUB_L(IIO_ITTE_GET(nasid, 3));
				wid_num = ((flush_nasid_list[nasid].iio_itte4) >> 8) & 0xf;
				bus = flush_nasid_list[nasid].iio_itte4 & 0xf;
				if (bus == 0x4 || bus == 0x8)
					bus = 0;
				else
					bus = 1;
				break;
			case 4:
				flush_nasid_list[nasid].iio_itte5 = HUB_L(IIO_ITTE_GET(nasid, 4));
				wid_num = ((flush_nasid_list[nasid].iio_itte5) >> 8) & 0xf;
				bus = flush_nasid_list[nasid].iio_itte5 & 0xf;
				if (bus == 0x4 || bus == 0x8)
					bus = 0;
				else
					bus = 1;
				break;
			case 5:
				flush_nasid_list[nasid].iio_itte6 = HUB_L(IIO_ITTE_GET(nasid, 5));
				wid_num = ((flush_nasid_list[nasid].iio_itte6) >> 8) & 0xf;
				bus = flush_nasid_list[nasid].iio_itte6 & 0xf;
				if (bus == 0x4 || bus == 0x8)
					bus = 0;
				else
					bus = 1;
				break;
			case 6:
				flush_nasid_list[nasid].iio_itte7 = HUB_L(IIO_ITTE_GET(nasid, 6));
				wid_num = ((flush_nasid_list[nasid].iio_itte7) >> 8) & 0xf;
				bus = flush_nasid_list[nasid].iio_itte7 & 0xf;
				if (bus == 0x4 || bus == 0x8)
					bus = 0;
				else
					bus = 1;
				break;
		}
	}

	// if it's IO9, bus 1, we don't care about slots 1, 3, and 4.  This is
	// because these are the IOC4 slots and we don't flush them.
	if (isIO9(nasid) && bus == 0 && (slot == 1 || slot == 4)) {
		return;
	}
	if (flush_nasid_list[nasid].widget_p[wid_num] == NULL) {
		flush_nasid_list[nasid].widget_p[wid_num] = (struct sn_flush_device_list *)kmalloc(
			DEV_PER_WIDGET * sizeof (struct sn_flush_device_list), GFP_KERNEL);
		memset(flush_nasid_list[nasid].widget_p[wid_num], 0, 
			DEV_PER_WIDGET * sizeof (struct sn_flush_device_list));
		p = &flush_nasid_list[nasid].widget_p[wid_num][0];
		for (i=0; i<DEV_PER_WIDGET;i++) {
			p->bus = -1;
			p->pin = -1;
			p++;
		}
	}

	p = &flush_nasid_list[nasid].widget_p[wid_num][0];
	for (i=0;i<DEV_PER_WIDGET; i++) {
		if (p->pin == pin && p->bus == bus) break;
		if (p->pin < 0) {
			p->pin = pin;
			p->bus = bus;
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
	b = (bridge_t *)(NODE_SWIN_BASE(nasid, wid_num) | (bus << 23) );

	// If it's IO9, then slot 2 maps to slot 7 and slot 6 maps to slot 8.
	// To see this is non-trivial.  By drawing pictures and reading manuals and talking
	// to HW guys, we can see that on IO9 bus 1, slots 7 and 8 are always unused.
	// Further, since we short-circuit slots  1, 3, and 4 above, we only have to worry
	// about the case when there is a card in slot 2.  A multifunction card will appear
	// to be in slot 6 (from an interrupt point of view) also.  That's the  most we'll
	// have to worry about.  A four function card will overload the interrupt lines in
	// slot 2 and 6.  
	// We also need to special case the 12160 device in slot 3.  Fortunately, we have
	// a spare intr. line for pin 4, so we'll use that for the 12160.
	// All other buses have slot 3 and 4 and slots 7 and 8 unused.  Since we can only
	// see slots 1 and 2 and slots 5 and 6 coming through here for those buses (this
	// is true only on Pxbricks with 2 physical slots per bus), we just need to add
	// 2 to the slot number to find an unused slot.
	// We have convinced ourselves that we will never see a case where two different cards
	// in two different slots will ever share an interrupt line, so there is no need to
	// special case this.

	if (isIO9(nasid) && wid_num == 0xc && bus == 0) {
		if (slot == 2) {
			p->force_int_addr = (unsigned long)&b->b_force_always[6].intr;
			dev_sel = b->b_int_device;
			dev_sel |= (1<<18);
			b->b_int_device = dev_sel;
			dnasid = NASID_GET(virt_to_phys(&p->flush_addr));
			b->p_int_addr_64[6] = (virt_to_phys(&p->flush_addr) & 0xfffffffff) | 
				(dnasid << 36) | (0xfUL << 48);
		} else  if (slot == 3) { /* 12160 SCSI device in IO9 */
			p->force_int_addr = (unsigned long)&b->b_force_always[4].intr;
			dev_sel = b->b_int_device;
			dev_sel |= (2<<12);
			b->b_int_device = dev_sel;
			dnasid = NASID_GET(virt_to_phys(&p->flush_addr));
			b->p_int_addr_64[4] = (virt_to_phys(&p->flush_addr) & 0xfffffffff) | 
				(dnasid << 36) | (0xfUL << 48);
		} else { /* slot == 6 */
			p->force_int_addr = (unsigned long)&b->b_force_always[7].intr;
			dev_sel = b->b_int_device;
			dev_sel |= (5<<21);
			b->b_int_device = dev_sel;
			dnasid = NASID_GET(virt_to_phys(&p->flush_addr));
			b->p_int_addr_64[7] = (virt_to_phys(&p->flush_addr) & 0xfffffffff) | 
				(dnasid << 36) | (0xfUL << 48);
		}
	} else {
		p->force_int_addr = (unsigned long)&b->b_force_always[pin + 2].intr;
		dev_sel = b->b_int_device;
		dev_sel |= ((slot - 1) << ( pin * 3) );
		b->b_int_device = dev_sel;
		dnasid = NASID_GET(virt_to_phys(&p->flush_addr));
		b->p_int_addr_64[pin + 2] = (virt_to_phys(&p->flush_addr) & 0xfffffffff) | 
			(dnasid << 36) | (0xfUL << 48);
	}
}

/*
 * sn_pci_fixup() - This routine is called when platform_pci_fixup() is 
 *	invoked at the end of pcibios_init() to link the Linux pci 
 *	infrastructure to SGI IO Infrasturcture - ia64/kernel/pci.c
 *
 *	Other platform specific fixup can also be done here.
 */
void
sn_pci_fixup(int arg)
{
	struct list_head *ln;
	struct pci_bus *pci_bus = NULL;
	struct pci_dev *device_dev = NULL;
	struct sn_widget_sysdata *widget_sysdata;
	struct sn_device_sysdata *device_sysdata;
	pcibr_intr_t intr_handle;
	int cpuid;
	vertex_hdl_t device_vertex;
	pciio_intr_line_t lines;
	extern int numnodes;
	int cnode;

	if (arg == 0) {
#ifdef CONFIG_PROC_FS
		extern void register_sn_procfs(void);
#endif
		extern void irix_io_init(void);
		extern void sn_init_cpei_timer(void);
		
		init_hcl();
		irix_io_init();
		
		for (cnode = 0; cnode < numnodes; cnode++) {
			extern void intr_init_vecblk(cnodeid_t);
			intr_init_vecblk(cnode);
		} 

		sn_init_cpei_timer();

#ifdef CONFIG_PROC_FS
		register_sn_procfs();
#endif
		return;
	}


	done_probing = 1;

	/*
	 * Initialize the pci bus vertex in the pci_bus struct.
	 */
	for( ln = pci_root_buses.next; ln != &pci_root_buses; ln = ln->next) {
		pci_bus = pci_bus_b(ln);
		widget_sysdata = kmalloc(sizeof(struct sn_widget_sysdata), 
					GFP_KERNEL);
		widget_sysdata->vhdl = pci_bus_to_vertex(pci_bus->number);
		pci_bus->sysdata = (void *)widget_sysdata;
	}

	/*
 	 * set the root start and end so that drivers calling check_region()
	 * won't see a conflict
	 */
	ioport_resource.start  = 0xc000000000000000;
	ioport_resource.end =    0xcfffffffffffffff;

	/*
	 * Set the root start and end for Mem Resource.
	 */
	iomem_resource.start = 0;
	iomem_resource.end = 0xffffffffffffffff;

	/*
	 * Initialize the device vertex in the pci_dev struct.
	 */
	while ((device_dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, device_dev)) != NULL) {
		unsigned int irq;
		int idx;
		u16 cmd;
		unsigned long size;
		extern int bit_pos_to_irq(int);

		/* Set the device vertex */

		device_sysdata = kmalloc(sizeof(struct sn_device_sysdata),
					 GFP_KERNEL);
		device_sysdata->vhdl = devfn_to_vertex(device_dev->bus->number, device_dev->devfn);
		device_sysdata->isa64 = 0;
		device_vertex = device_sysdata->vhdl;

		device_dev->sysdata = (void *) device_sysdata;
		set_isPIC(device_sysdata);

		/*
		 * Set the xbridge Device(X) Write Buffer Flush and Xbow Flush 
		 * register addresses.
		 */
		set_flush_addresses(device_dev, device_sysdata);
		pci_read_config_word(device_dev, PCI_COMMAND, &cmd);

		/*
		 * Set the resources address correctly.  The assumption here 
		 * is that the addresses in the resource structure has been
		 * read from the card and it was set in the card by our
		 * Infrastructure ..
		 */
		for (idx = 0; idx < PCI_ROM_RESOURCE; idx++) {
			size = 0;
			size = device_dev->resource[idx].end -
				device_dev->resource[idx].start;
			if (size) {
				device_dev->resource[idx].start = (unsigned long)pciio_pio_addr(device_vertex, 0, PCIIO_SPACE_WIN(idx), 0, size, 0, (IS_PIC_DEVICE(device_dev)) ? 0 : PCIIO_BYTE_STREAM);
				device_dev->resource[idx].start |= __IA64_UNCACHED_OFFSET;
			}
			else
				continue;

			device_dev->resource[idx].end = 
				device_dev->resource[idx].start + size;

			if (device_dev->resource[idx].flags & IORESOURCE_IO)
				cmd |= PCI_COMMAND_IO;

			if (device_dev->resource[idx].flags & IORESOURCE_MEM)
				cmd |= PCI_COMMAND_MEMORY;
		}

		/*
		 * Update the Command Word on the Card.
		 */
		cmd |= PCI_COMMAND_MASTER; /* If the device doesn't support */
					   /* bit gets dropped .. no harm */
		pci_write_config_word(device_dev, PCI_COMMAND, cmd);
		
		pci_read_config_byte(device_dev, PCI_INTERRUPT_PIN,
				     (unsigned char *)&lines);
	 
		irqpdaindr->curr = device_dev;
		intr_handle = pcibr_intr_alloc(device_vertex, NULL, lines, device_vertex);

		irq = intr_handle->bi_irq;
		irqpdaindr->device_dev[irq] = device_dev;
		cpuid = intr_handle->bi_cpu;
		pcibr_intr_connect(intr_handle, (intr_func_t)0, (intr_arg_t)0);
		device_dev->irq = irq;
		register_pcibr_intr(irq, intr_handle);

		for (idx = 0; idx < PCI_ROM_RESOURCE; idx++) {
			int ibits = intr_handle->bi_ibits;
			int i;

			size = device_dev->resource[idx].end -
				device_dev->resource[idx].start;
			if (size == 0)
				continue;

			for (i=0; i<8; i++) {
				if (ibits & (1 << i) ) {
					sn_dma_flush_init(device_dev->resource[idx].start, 
							device_dev->resource[idx].end,
							idx,
							i,
							PCI_SLOT(device_dev->devfn));
				}
			}
		}

	}
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
 *	(rack/slot etc.) starting from HUB_WIDGET_ID_MAX down to 
 *	HUB_WIDGET_ID_MIN:
 *		widgetnum 15 gets lower Bus Number than widgetnum 14 etc.
 *
 *	Given 2 modules 001c01 and 001c02 we get the following mappings:
 *		001c01, widgetnum 15 = Bus number 0
 *		001c01, widgetnum 14 = Bus number 1
 *		001c02, widgetnum 15 = Bus number 3
 *		001c02, widgetnum 14 = Bus number 4
 *		etc.
 *
 * The rational for starting Bus Number 0 with Widget number 15 is because 
 * the system boot disks are always connected via Widget 15 Slot 0 of the 
 * I-brick.  Linux creates /dev/sd* devices(naming) strating from Bus Number 0 
 * Therefore, /dev/sda1 will be the first disk, on Widget 15 of the lowest 
 * module id(Master Cnode) of the system.
 *	
 */
static int 
pci_bus_map_create(vertex_hdl_t xtalk, char * io_moduleid)
{

	vertex_hdl_t master_node_vertex = NULL;
	vertex_hdl_t xwidget = NULL;
	vertex_hdl_t pci_bus = NULL;
	hubinfo_t hubinfo = NULL;
	xwidgetnum_t widgetnum;
	char pathname[128];
	graph_error_t rv;
	int bus;
	int basebus_num;
	extern void ioconfig_get_busnum(char *, int *);

	int bus_number;

	/*
	 * Loop throught this vertex and get the Xwidgets ..
	 */


	/* PCI devices */

	for (widgetnum = HUB_WIDGET_ID_MAX; widgetnum >= HUB_WIDGET_ID_MIN; widgetnum--) {
		sprintf(pathname, "%d", widgetnum);
		xwidget = NULL;
		
		/*
		 * Example - /hw/module/001c16/Pbrick/xtalk/8 is the xwidget
		 *	     /hw/module/001c16/Pbrick/xtalk/8/pci/1 is device
		 */
		rv = hwgraph_traverse(xtalk, pathname, &xwidget);
		if ( (rv != GRAPH_SUCCESS) ) {
			if (!xwidget) {
				continue;
			}
		}

		sprintf(pathname, "%d/"EDGE_LBL_PCI, widgetnum);
		pci_bus = NULL;
		if (hwgraph_traverse(xtalk, pathname, &pci_bus) != GRAPH_SUCCESS)
			if (!pci_bus) {
				continue;
}

		/*
		 * Assign the correct bus number and also the nasid of this 
		 * pci Xwidget.
		 * 
		 * Should not be any race here ...
		 */
		num_bridges++;
		busnum_to_pcibr_vhdl[num_bridges - 1] = pci_bus;

		/*
		 * Get the master node and from there get the NASID.
		 */
		master_node_vertex = device_master_get(xwidget);
		if (!master_node_vertex) {
			printk("WARNING: pci_bus_map_create: Unable to get .master for vertex 0x%p\n", (void *)xwidget);
		}
	
		hubinfo_get(master_node_vertex, &hubinfo);
		if (!hubinfo) {
			printk("WARNING: pci_bus_map_create: Unable to get hubinfo for master node vertex 0x%p\n", (void *)master_node_vertex);
			return(1);
		} else {
			busnum_to_nid[num_bridges - 1] = hubinfo->h_nasid;
		}

		/*
		 * Pre assign DMA maps needed for 32 Bits Page Map DMA.
		 */
		busnum_to_atedmamaps[num_bridges - 1] = (void *) kmalloc(
			sizeof(struct pcibr_dmamap_s) * MAX_ATE_MAPS, GFP_KERNEL);
		if (!busnum_to_atedmamaps[num_bridges - 1])
			printk("WARNING: pci_bus_map_create: Unable to precreate ATE DMA Maps for busnum %d vertex 0x%p\n", num_bridges - 1, (void *)xwidget);

		memset(busnum_to_atedmamaps[num_bridges - 1], 0x0, 
			sizeof(struct pcibr_dmamap_s) * MAX_ATE_MAPS);

	}

	/*
	 * PCIX devices
	 * We number busses differently for PCI-X devices.
	 * We start from Lowest Widget on up ..
	 */

        (void) ioconfig_get_busnum((char *)io_moduleid, &basebus_num);

	for (widgetnum = HUB_WIDGET_ID_MIN; widgetnum <= HUB_WIDGET_ID_MAX; widgetnum++) {

		/* Do both buses */
		for ( bus = 0; bus < 2; bus++ ) {
			sprintf(pathname, "%d", widgetnum);
			xwidget = NULL;
			
			/*
			 * Example - /hw/module/001c16/Pbrick/xtalk/8 is the xwidget
			 *	     /hw/module/001c16/Pbrick/xtalk/8/pci-x/0 is the bus
			 *	     /hw/module/001c16/Pbrick/xtalk/8/pci-x/0/1 is device
			 */
			rv = hwgraph_traverse(xtalk, pathname, &xwidget);
			if ( (rv != GRAPH_SUCCESS) ) {
				if (!xwidget) {
					continue;
				}
			}
	
			if ( bus == 0 )
				sprintf(pathname, "%d/"EDGE_LBL_PCIX_0, widgetnum);
			else
				sprintf(pathname, "%d/"EDGE_LBL_PCIX_1, widgetnum);
			pci_bus = NULL;
			if (hwgraph_traverse(xtalk, pathname, &pci_bus) != GRAPH_SUCCESS)
				if (!pci_bus) {
					continue;
				}
	
			/*
			 * Assign the correct bus number and also the nasid of this 
			 * pci Xwidget.
			 * 
			 * Should not be any race here ...
			 */
			bus_number = basebus_num + bus + io_brick_map_widget(MODULE_PXBRICK, widgetnum);
#ifdef DEBUG
			printk("bus_number %d basebus_num %d bus %d io %d\n", 
				bus_number, basebus_num, bus, 
				io_brick_map_widget(MODULE_PXBRICK, widgetnum));
#endif
			busnum_to_pcibr_vhdl[bus_number] = pci_bus;
	
			/*
			 * Pre assign DMA maps needed for 32 Bits Page Map DMA.
			 */
			busnum_to_atedmamaps[bus_number] = (void *) kmalloc(
				sizeof(struct pcibr_dmamap_s) * MAX_ATE_MAPS, GFP_KERNEL);
			if (!busnum_to_atedmamaps[bus_number])
				printk("WARNING: pci_bus_map_create: Unable to precreate ATE DMA Maps for busnum %d vertex 0x%p\n", num_bridges - 1, (void *)xwidget);
	
			memset(busnum_to_atedmamaps[bus_number], 0x0, 
				sizeof(struct pcibr_dmamap_s) * MAX_ATE_MAPS);
		}
	}

        return(0);
}

/*
 * pci_bus_to_hcl_cvlink() - This routine is called after SGI IO Infrastructure   
 *      initialization has completed to set up the mappings between Xbridge
 *      and logical pci bus numbers.  We also set up the NASID for each of these
 *      xbridges.
 *
 *      Must be called before pci_init() is invoked.
 */
int
pci_bus_to_hcl_cvlink(void) 
{

	vertex_hdl_t devfs_hdl = NULL;
	vertex_hdl_t xtalk = NULL;
	int rv = 0;
	char name[256];
	char tmp_name[256];
	int i, ii, j;
	char *brick_name;
	extern void ioconfig_bus_new_entries(void);

	/*
	 * Figure out which IO Brick is connected to the Compute Bricks.
	 */
	for (i = 0; i < nummodules; i++) {
		extern int iomoduleid_get(nasid_t);
		moduleid_t iobrick_id;
		nasid_t nasid = -1;
		int nodecnt;
		int n = 0;

		nodecnt = modules[i]->nodecnt;
		for ( n = 0; n < nodecnt; n++ ) {
			nasid = cnodeid_to_nasid(modules[i]->nodes[n]);
			iobrick_id = iomoduleid_get(nasid);
			if ((int)iobrick_id > 0) { /* Valid module id */
				char name[12];
				memset(name, 0, 12);
				format_module_id((char *)&(modules[i]->io[n].moduleid), iobrick_id, MODULE_FORMAT_BRIEF);
			}
		}
	}
				
	devfs_hdl = hwgraph_path_to_vertex("hw/module");
	for (i = 0; i < nummodules ; i++) {
	    for ( j = 0; j < 3; j++ ) {
		if ( j == 0 )
			brick_name = EDGE_LBL_PBRICK;
		else if ( j == 1 )
			brick_name = EDGE_LBL_PXBRICK;
		else
			brick_name = EDGE_LBL_IXBRICK;

		for ( ii = 0; ii < 2 ; ii++ ) {
			memset(name, 0, 256);
			memset(tmp_name, 0, 256);
			format_module_id(name, modules[i]->id, MODULE_FORMAT_BRIEF);
			sprintf(tmp_name, "/slab/%d/%s/xtalk", geo_slab(modules[i]->geoid[ii]), brick_name);
			strcat(name, tmp_name);
			xtalk = NULL;
			rv = hwgraph_edge_get(devfs_hdl, name, &xtalk);
			if ( rv == 0 ) 
				pci_bus_map_create(xtalk, (char *)&(modules[i]->io[ii].moduleid));
		}
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
extern struct pci_ops sn_pci_ops;
int __init
sn_pci_init (void)
{
#	define PCI_BUSES_TO_SCAN 256
	int i = 0;
	struct pci_controller *controller;

	if (!ia64_platform_is("sn2"))
	    return 0;

	/*
	 * set pci_raw_ops, etc.
	 */
	sn_pci_fixup(0);

	controller = kmalloc(sizeof(struct pci_controller), GFP_KERNEL);
	if (controller) {
		memset(controller, 0, sizeof(struct pci_controller));
		/* just allocate some devices and fill in the pci_dev structs */
		for (i = 0; i < PCI_BUSES_TO_SCAN; i++)
			pci_scan_bus(i, &sn_pci_ops, controller);
	}

	/*
	 * actually find devices and fill in hwgraph structs
	 */
	sn_pci_fixup(1);

	return 0;
}

subsys_initcall(sn_pci_init);
