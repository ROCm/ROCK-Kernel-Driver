/* $Id$
 *
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
#include <asm/sn/hack.h>
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

extern int bridge_rev_b_data_check_disable;

devfs_handle_t busnum_to_pcibr_vhdl[MAX_PCI_XWIDGET];
nasid_t busnum_to_nid[MAX_PCI_XWIDGET];
void * busnum_to_atedmamaps[MAX_PCI_XWIDGET];
unsigned char num_bridges;
static int done_probing = 0;

static int pci_bus_map_create(devfs_handle_t xtalk, char * io_moduleid);
devfs_handle_t devfn_to_vertex(unsigned char busnum, unsigned int devfn);

extern unsigned char Is_pic_on_this_nasid[512];

extern void sn_init_irq_desc(void);
extern void register_pcibr_intr(int irq, pcibr_intr_t intr);


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

	memset(busnum_to_pcibr_vhdl, 0x0, sizeof(devfs_handle_t) * MAX_PCI_XWIDGET);
	memset(busnum_to_nid, 0x0, sizeof(nasid_t) * MAX_PCI_XWIDGET);

	memset(busnum_to_atedmamaps, 0x0, sizeof(void *) * MAX_PCI_XWIDGET);

	num_bridges = 0;

	ioconfig_bus_init();
}

/*
 * pci_bus_to_vertex() - Given a logical Linux Bus Number returns the associated 
 *	pci bus vertex from the SGI IO Infrastructure.
 */
devfs_handle_t
pci_bus_to_vertex(unsigned char busnum)
{

	devfs_handle_t	pci_bus = NULL;


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
devfs_handle_t
devfn_to_vertex(unsigned char busnum, unsigned int devfn)
{

	int slot = 0;
	int func = 0;
	char	name[16];
	devfs_handle_t  pci_bus = NULL;
	devfs_handle_t	device_vertex = (devfs_handle_t)NULL;

	/*
	 * Go get the pci bus vertex.
	 */
	pci_bus = pci_bus_to_vertex(busnum);
	if (!pci_bus) {
		/*
		 * During probing, the Linux pci code invents non existant
		 * bus numbers and pci_dev structures and tries to access
		 * them to determine existance. Don't crib during probing.
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

/*
 * Most drivers currently do not properly tell the arch specific pci dma
 * interfaces whether they can handle A64. Here is where we privately
 * keep track of this.
 */
static void __init
set_sn_pci64(struct pci_dev *dev)
{
	unsigned short vendor = dev->vendor;
	unsigned short device = dev->device;

	if (vendor == PCI_VENDOR_ID_QLOGIC) {
		if ((device == PCI_DEVICE_ID_QLOGIC_ISP2100) ||
				(device == PCI_DEVICE_ID_QLOGIC_ISP2200)) {
			SET_PCIA64(dev);
			return;
		}
	}

	if (vendor == PCI_VENDOR_ID_SGI) {
		if (device == PCI_DEVICE_ID_SGI_IOC3) {
			SET_PCIA64(dev);
			return;
		}
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
	pciio_intr_t intr_handle;
	int cpuid, bit;
	devfs_handle_t device_vertex;
	pciio_intr_line_t lines;
	extern void sn_pci_find_bios(void);
	extern int numnodes;
	int cnode;
	extern void io_sh_swapper(int, int);

	for (cnode = 0; cnode < numnodes; cnode++) {
		if ( !Is_pic_on_this_nasid[cnodeid_to_nasid(cnode)] )
			io_sh_swapper((cnodeid_to_nasid(cnode)), 0);
	}

	if (arg == 0) {
#ifdef CONFIG_PROC_FS
		extern void register_sn_procfs(void);
#endif

		sn_init_irq_desc();
		sn_pci_find_bios();
		for (cnode = 0; cnode < numnodes; cnode++) {
			extern void intr_init_vecblk(nodepda_t *npda, cnodeid_t, int);
			intr_init_vecblk(NODEPDA(cnode), cnode, 0);
		} 

		/*
		 * When we return to generic Linux, Swapper is always on ..
		 */
		for (cnode = 0; cnode < numnodes; cnode++) {
			if ( !Is_pic_on_this_nasid[cnodeid_to_nasid(cnode)] )
				io_sh_swapper((cnodeid_to_nasid(cnode)), 1);
		}
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
	 * Initialize the device vertex in the pci_dev struct.
	 */
	while ((device_dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, device_dev)) != NULL) {
		unsigned int irq;
		int idx;
		u16 cmd;
		devfs_handle_t vhdl;
		unsigned long size;
		extern int bit_pos_to_irq(int);

		if (device_dev->vendor == PCI_VENDOR_ID_SGI &&
				device_dev->device == PCI_DEVICE_ID_SGI_IOC3) {
			extern void pci_fixup_ioc3(struct pci_dev *d);
			pci_fixup_ioc3(device_dev);
		}

		/* Set the device vertex */

		device_sysdata = kmalloc(sizeof(struct sn_device_sysdata),
					GFP_KERNEL);
		device_sysdata->vhdl = devfn_to_vertex(device_dev->bus->number, device_dev->devfn);
		device_sysdata->isa64 = 0;
		/*
		 * Set the xbridge Device(X) Write Buffer Flush and Xbow Flush 
		 * register addresses.
		 */
		(void) set_flush_addresses(device_dev, device_sysdata);

		device_dev->sysdata = (void *) device_sysdata;
		set_sn_pci64(device_dev);
		set_isPIC(device_sysdata);

		pci_read_config_word(device_dev, PCI_COMMAND, &cmd);

		/*
		 * Set the resources address correctly.  The assumption here 
		 * is that the addresses in the resource structure has been
		 * read from the card and it was set in the card by our
		 * Infrastructure ..
		 */
		vhdl = device_sysdata->vhdl;
		for (idx = 0; idx < PCI_ROM_RESOURCE; idx++) {
			size = 0;
			size = device_dev->resource[idx].end -
				device_dev->resource[idx].start;
			if (size) {
				device_dev->resource[idx].start = (unsigned long)pciio_pio_addr(vhdl, 0, PCIIO_SPACE_WIN(idx), 0, size, 0, (IS_PIC_DEVICE(device_dev)) ? 0 : PCIIO_BYTE_STREAM);
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
#if 0
	/*
	 * Software WAR for a Software BUG.
	 * This is only temporary.
	 * See PV 872791
	 */

		/*
		 * Now handle the ROM resource ..
		 */
		size = device_dev->resource[PCI_ROM_RESOURCE].end -
			device_dev->resource[PCI_ROM_RESOURCE].start;

		if (size) {
			device_dev->resource[PCI_ROM_RESOURCE].start =
			(unsigned long) pciio_pio_addr(vhdl, 0, PCIIO_SPACE_ROM, 0, 
				size, 0, (IS_PIC_DEVICE(device_dev)) ? 0 : PCIIO_BYTE_STREAM);
			device_dev->resource[PCI_ROM_RESOURCE].start |= __IA64_UNCACHED_OFFSET;
			device_dev->resource[PCI_ROM_RESOURCE].end =
			device_dev->resource[PCI_ROM_RESOURCE].start + size;
		}
#endif

		/*
		 * Update the Command Word on the Card.
		 */
		cmd |= PCI_COMMAND_MASTER; /* If the device doesn't support */
					   /* bit gets dropped .. no harm */
		pci_write_config_word(device_dev, PCI_COMMAND, cmd);

		pci_read_config_byte(device_dev, PCI_INTERRUPT_PIN, (unsigned char *)&lines);
		if (device_dev->vendor == PCI_VENDOR_ID_SGI &&
			device_dev->device == PCI_DEVICE_ID_SGI_IOC3 ) {
				lines = 1;
		}
 
		device_sysdata = (struct sn_device_sysdata *)device_dev->sysdata;
		device_vertex = device_sysdata->vhdl;
 
		intr_handle = pciio_intr_alloc(device_vertex, NULL, lines, device_vertex);

		bit = intr_handle->pi_irq;
		cpuid = intr_handle->pi_cpu;
		irq = bit;
		irq = irq + (cpuid << 8);
		pciio_intr_connect(intr_handle, (intr_func_t)0, (intr_arg_t)0);
		device_dev->irq = irq;
		register_pcibr_intr(irq, (pcibr_intr_t)intr_handle);
#ifdef ajmtestintr
		{
			int slot = PCI_SLOT(device_dev->devfn);
			static int timer_set = 0;
			pcibr_intr_t	pcibr_intr = (pcibr_intr_t)intr_handle;
			pcibr_soft_t	pcibr_soft = pcibr_intr->bi_soft;
			extern void intr_test_handle_intr(int, void*, struct pt_regs *);

			if (!timer_set) {
				intr_test_set_timer();
				timer_set = 1;
			}
			intr_test_register_irq(irq, pcibr_soft, slot);
			request_irq(irq, intr_test_handle_intr,0,NULL, NULL);
		}
#endif

	}

	for (cnode = 0; cnode < numnodes; cnode++) {
		if ( !Is_pic_on_this_nasid[cnodeid_to_nasid(cnode)] )
			io_sh_swapper((cnodeid_to_nasid(cnode)), 1);
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
pci_bus_map_create(devfs_handle_t xtalk, char * io_moduleid)
{

	devfs_handle_t master_node_vertex = NULL;
	devfs_handle_t xwidget = NULL;
	devfs_handle_t pci_bus = NULL;
	hubinfo_t hubinfo = NULL;
	xwidgetnum_t widgetnum;
	char pathname[128];
	graph_error_t rv;
	int bus;
	int basebus_num;
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
			sizeof(struct sn_dma_maps_s) * MAX_ATE_MAPS, GFP_KERNEL);
		if (!busnum_to_atedmamaps[num_bridges - 1])
			printk("WARNING: pci_bus_map_create: Unable to precreate ATE DMA Maps for busnum %d vertex 0x%p\n", num_bridges - 1, (void *)xwidget);

		memset(busnum_to_atedmamaps[num_bridges - 1], 0x0, 
			sizeof(struct sn_dma_maps_s) * MAX_ATE_MAPS);

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
				sizeof(struct sn_dma_maps_s) * MAX_ATE_MAPS, GFP_KERNEL);
			if (!busnum_to_atedmamaps[bus_number])
				printk("WARNING: pci_bus_map_create: Unable to precreate ATE DMA Maps for busnum %d vertex 0x%p\n", num_bridges - 1, (void *)xwidget);
	
			memset(busnum_to_atedmamaps[bus_number], 0x0, 
				sizeof(struct sn_dma_maps_s) * MAX_ATE_MAPS);
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

	devfs_handle_t devfs_hdl = NULL;
	devfs_handle_t xtalk = NULL;
	int rv = 0;
	char name[256];
	char tmp_name[256];
	int i, ii;

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
				
	devfs_hdl = hwgraph_path_to_vertex("/dev/hw/module");
	for (i = 0; i < nummodules ; i++) {
		for ( ii = 0; ii < 2 ; ii++ ) {
			memset(name, 0, 256);
			memset(tmp_name, 0, 256);
			format_module_id(name, modules[i]->id, MODULE_FORMAT_BRIEF);
			sprintf(tmp_name, "/slab/%d/Pbrick/xtalk", geo_slab(modules[i]->geoid[ii]));
			strcat(name, tmp_name);
			xtalk = NULL;
			rv = hwgraph_edge_get(devfs_hdl, name, &xtalk);
			pci_bus_map_create(xtalk, (char *)&(modules[i]->io[ii].moduleid));
		}
	}

	/*
	 * Create the Linux PCI bus number vertex link.
	 */
	(void)linux_bus_cvlink();
	(void)ioconfig_bus_new_entries();

	return(0);
}
