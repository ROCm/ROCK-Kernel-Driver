/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2002 Silicon Graphics, Inc. All rights reserved.
 */

#define	USRPCI	0

#include <linux/init.h>
#include <linux/types.h>
#include <linux/config.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <asm/sn/sgi.h>
#include <asm/sn/xtalk/xbow.h>	/* Must be before iograph.h to get MAX_PORT_NUM */
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/hcl_util.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/pci/bridge.h>
#include <asm/sn/ioerror_handling.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/pciio_private.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/io.h>
#include <asm/sn/pci/pci_bus_cvlink.h>
#include <asm/sn/simulator.h>

#define DEBUG_PCIIO
#undef DEBUG_PCIIO	/* turn this on for yet more console output */


#define GET_NEW(ptr)	(ptr = kmalloc(sizeof (*(ptr)), GFP_KERNEL))
#define DO_DEL(ptr)	(kfree(ptr))

char                    pciio_info_fingerprint[] = "pciio_info";

cdl_p                   pciio_registry = NULL;

int
badaddr_val(volatile void *addr, int len, volatile void *ptr)
{
	int ret = 0;
	volatile void *new_addr;

	switch (len) {
		case 4:
			new_addr = (void *)(((u64) addr)^4);
			ret = ia64_sn_probe_io_slot((long)new_addr, len, (void *)ptr);
			break;
		default:
			printk(KERN_WARNING "badaddr_val given len %x but supports len of 4 only\n", len);
	}

	if (ret < 0)
		panic("badaddr_val: unexpected status (%d) in probing", ret);
	return(ret);

}


nasid_t
get_console_nasid(void)
{
	extern nasid_t console_nasid;
	if (console_nasid < 0) {
		console_nasid = ia64_sn_get_console_nasid();
		if (console_nasid < 0) {
// ZZZ What do we do if we don't get a console nasid on the hardware????
			if (IS_RUNNING_ON_SIMULATOR() )
				console_nasid = master_nasid;
		}
	} 
	return console_nasid;
}

int
hub_dma_enabled(devfs_handle_t xconn_vhdl)
{
	return(0);
}

int
hub_error_devenable(devfs_handle_t xconn_vhdl, int devnum, int error_code)
{
	return(0);
}

void
ioerror_dump(char *name, int error_code, int error_mode, ioerror_t *ioerror)
{
}

/******
 ****** end hack defines ......
 ******/




/* =====================================================================
 *    PCI Generic Bus Provider
 * Implement PCI provider operations.  The pciio* layer provides a
 * platform-independent interface for PCI devices.  This layer
 * switches among the possible implementations of a PCI adapter.
 */

/* =====================================================================
 *    Provider Function Location SHORTCUT
 *
 * On platforms with only one possible PCI provider, macros can be
 * set up at the top that cause the table lookups and indirections to
 * completely disappear.
 */

#if defined(CONFIG_IA64_SGI_SN1)
/*
 *    For the moment, we will assume that IP27
 *      only use Bridge ASICs to provide PCI support.
 */
#include <asm/sn/pci/pcibr.h>
#define DEV_FUNC(dev,func)	pcibr_##func
#define CAST_PIOMAP(x)		((pcibr_piomap_t)(x))
#define CAST_DMAMAP(x)		((pcibr_dmamap_t)(x))
#define CAST_INTR(x)		((pcibr_intr_t)(x))
#endif /* CONFIG_IA64_SGI_SN1 */

/* =====================================================================
 *    Function Table of Contents
 */

#if !defined(DEV_FUNC)
static pciio_provider_t *pciio_to_provider_fns(devfs_handle_t dev);
#endif

pciio_piomap_t          pciio_piomap_alloc(devfs_handle_t, device_desc_t, pciio_space_t, iopaddr_t, size_t, size_t, unsigned);
void                    pciio_piomap_free(pciio_piomap_t);
caddr_t                 pciio_piomap_addr(pciio_piomap_t, iopaddr_t, size_t);

void                    pciio_piomap_done(pciio_piomap_t);
caddr_t                 pciio_piotrans_addr(devfs_handle_t, device_desc_t, pciio_space_t, iopaddr_t, size_t, unsigned);
caddr_t			pciio_pio_addr(devfs_handle_t, device_desc_t, pciio_space_t, iopaddr_t, size_t, pciio_piomap_t *, unsigned);

iopaddr_t               pciio_piospace_alloc(devfs_handle_t, device_desc_t, pciio_space_t, size_t, size_t);
void                    pciio_piospace_free(devfs_handle_t, pciio_space_t, iopaddr_t, size_t);

pciio_dmamap_t          pciio_dmamap_alloc(devfs_handle_t, device_desc_t, size_t, unsigned);
void                    pciio_dmamap_free(pciio_dmamap_t);
iopaddr_t               pciio_dmamap_addr(pciio_dmamap_t, paddr_t, size_t);
alenlist_t              pciio_dmamap_list(pciio_dmamap_t, alenlist_t, unsigned);
void                    pciio_dmamap_done(pciio_dmamap_t);
iopaddr_t               pciio_dmatrans_addr(devfs_handle_t, device_desc_t, paddr_t, size_t, unsigned);
alenlist_t              pciio_dmatrans_list(devfs_handle_t, device_desc_t, alenlist_t, unsigned);
void			pciio_dmamap_drain(pciio_dmamap_t);
void			pciio_dmaaddr_drain(devfs_handle_t, paddr_t, size_t);
void			pciio_dmalist_drain(devfs_handle_t, alenlist_t);
iopaddr_t               pciio_dma_addr(devfs_handle_t, device_desc_t, paddr_t, size_t, pciio_dmamap_t *, unsigned);

pciio_intr_t            pciio_intr_alloc(devfs_handle_t, device_desc_t, pciio_intr_line_t, devfs_handle_t);
void                    pciio_intr_free(pciio_intr_t);
int                     pciio_intr_connect(pciio_intr_t);
void                    pciio_intr_disconnect(pciio_intr_t);
devfs_handle_t            pciio_intr_cpu_get(pciio_intr_t);

void			pciio_slot_func_to_name(char *, pciio_slot_t, pciio_function_t);

void                    pciio_provider_startup(devfs_handle_t);
void                    pciio_provider_shutdown(devfs_handle_t);

pciio_endian_t          pciio_endian_set(devfs_handle_t, pciio_endian_t, pciio_endian_t);
pciio_priority_t        pciio_priority_set(devfs_handle_t, pciio_priority_t);
devfs_handle_t            pciio_intr_dev_get(pciio_intr_t);

devfs_handle_t            pciio_pio_dev_get(pciio_piomap_t);
pciio_slot_t            pciio_pio_slot_get(pciio_piomap_t);
pciio_space_t           pciio_pio_space_get(pciio_piomap_t);
iopaddr_t               pciio_pio_pciaddr_get(pciio_piomap_t);
ulong                   pciio_pio_mapsz_get(pciio_piomap_t);
caddr_t                 pciio_pio_kvaddr_get(pciio_piomap_t);

devfs_handle_t            pciio_dma_dev_get(pciio_dmamap_t);
pciio_slot_t            pciio_dma_slot_get(pciio_dmamap_t);

pciio_info_t            pciio_info_chk(devfs_handle_t);
pciio_info_t            pciio_info_get(devfs_handle_t);
void                    pciio_info_set(devfs_handle_t, pciio_info_t);
devfs_handle_t            pciio_info_dev_get(pciio_info_t);
pciio_slot_t            pciio_info_slot_get(pciio_info_t);
pciio_function_t        pciio_info_function_get(pciio_info_t);
pciio_vendor_id_t       pciio_info_vendor_id_get(pciio_info_t);
pciio_device_id_t       pciio_info_device_id_get(pciio_info_t);
devfs_handle_t            pciio_info_master_get(pciio_info_t);
arbitrary_info_t        pciio_info_mfast_get(pciio_info_t);
pciio_provider_t       *pciio_info_pops_get(pciio_info_t);
error_handler_f	       *pciio_info_efunc_get(pciio_info_t);
error_handler_arg_t    *pciio_info_einfo_get(pciio_info_t);
pciio_space_t		pciio_info_bar_space_get(pciio_info_t, int);
iopaddr_t		pciio_info_bar_base_get(pciio_info_t, int);
size_t			pciio_info_bar_size_get(pciio_info_t, int);
iopaddr_t		pciio_info_rom_base_get(pciio_info_t);
size_t			pciio_info_rom_size_get(pciio_info_t);

void                    pciio_init(void);
int                     pciio_attach(devfs_handle_t);

void                    pciio_provider_register(devfs_handle_t, pciio_provider_t *pciio_fns);
void                    pciio_provider_unregister(devfs_handle_t);
pciio_provider_t       *pciio_provider_fns_get(devfs_handle_t);

int                     pciio_driver_register(pciio_vendor_id_t, pciio_device_id_t, char *driver_prefix, unsigned);
void                    pciio_driver_unregister(char *driver_prefix);

devfs_handle_t            pciio_device_register(devfs_handle_t, devfs_handle_t, pciio_slot_t, pciio_function_t, pciio_vendor_id_t, pciio_device_id_t);

void			pciio_device_unregister(devfs_handle_t);
pciio_info_t		pciio_device_info_new(pciio_info_t, devfs_handle_t, pciio_slot_t, pciio_function_t, pciio_vendor_id_t, pciio_device_id_t);
void			pciio_device_info_free(pciio_info_t);
devfs_handle_t		pciio_device_info_register(devfs_handle_t, pciio_info_t);
void			pciio_device_info_unregister(devfs_handle_t, pciio_info_t);
int                     pciio_device_attach(devfs_handle_t, int);
int			pciio_device_detach(devfs_handle_t, int);
void                    pciio_error_register(devfs_handle_t, error_handler_f *, error_handler_arg_t);

int                     pciio_reset(devfs_handle_t);
int                     pciio_write_gather_flush(devfs_handle_t);
int                     pciio_slot_inuse(devfs_handle_t);

/* =====================================================================
 *    Provider Function Location
 *
 *      If there is more than one possible provider for
 *      this platform, we need to examine the master
 *      vertex of the current vertex for a provider
 *      function structure, and indirect through the
 *      appropriately named member.
 */

#if !defined(DEV_FUNC)

static pciio_provider_t *
pciio_to_provider_fns(devfs_handle_t dev)
{
    pciio_info_t            card_info;
    pciio_provider_t       *provider_fns;

    /*
     * We're called with two types of vertices, one is
     * the bridge vertex (ends with "pci") and the other is the
     * pci slot vertex (ends with "pci/[0-8]").  For the first type
     * we need to get the provider from the PFUNCS label.  For
     * the second we get it from fastinfo/c_pops.
     */
    provider_fns = pciio_provider_fns_get(dev);
    if (provider_fns == NULL) {
	card_info = pciio_info_get(dev);
	if (card_info != NULL) {
		provider_fns = pciio_info_pops_get(card_info);
	}
    }

    if (provider_fns == NULL)
#if defined(SUPPORT_PRINTING_V_FORMAT)
	PRINT_PANIC("%v: provider_fns == NULL", dev);
#else
	PRINT_PANIC("0x%p: provider_fns == NULL", (void *)dev);
#endif

    return provider_fns;

}

#define DEV_FUNC(dev,func)	pciio_to_provider_fns(dev)->func
#define CAST_PIOMAP(x)		((pciio_piomap_t)(x))
#define CAST_DMAMAP(x)		((pciio_dmamap_t)(x))
#define CAST_INTR(x)		((pciio_intr_t)(x))
#endif

/*
 * Many functions are not passed their vertex
 * information directly; rather, they must
 * dive through a resource map. These macros
 * are available to coordinate this detail.
 */
#define PIOMAP_FUNC(map,func)		DEV_FUNC((map)->pp_dev,func)
#define DMAMAP_FUNC(map,func)		DEV_FUNC((map)->pd_dev,func)
#define INTR_FUNC(intr_hdl,func)	DEV_FUNC((intr_hdl)->pi_dev,func)

/* =====================================================================
 *          PIO MANAGEMENT
 *
 *      For mapping system virtual address space to
 *      pciio space on a specified card
 */

pciio_piomap_t
pciio_piomap_alloc(devfs_handle_t dev,	/* set up mapping for this device */
		   device_desc_t dev_desc,	/* device descriptor */
		   pciio_space_t space,	/* CFG, MEM, IO, or a device-decoded window */
		   iopaddr_t addr,	/* lowest address (or offset in window) */
		   size_t byte_count,	/* size of region containing our mappings */
		   size_t byte_count_max,	/* maximum size of a mapping */
		   unsigned flags)
{					/* defined in sys/pio.h */
    return (pciio_piomap_t) DEV_FUNC(dev, piomap_alloc)
	(dev, dev_desc, space, addr, byte_count, byte_count_max, flags);
}

void
pciio_piomap_free(pciio_piomap_t pciio_piomap)
{
    PIOMAP_FUNC(pciio_piomap, piomap_free)
	(CAST_PIOMAP(pciio_piomap));
}

caddr_t
pciio_piomap_addr(pciio_piomap_t pciio_piomap,	/* mapping resources */
		  iopaddr_t pciio_addr,	/* map for this pciio address */
		  size_t byte_count)
{					/* map this many bytes */
    pciio_piomap->pp_kvaddr = PIOMAP_FUNC(pciio_piomap, piomap_addr)
	(CAST_PIOMAP(pciio_piomap), pciio_addr, byte_count);

    return pciio_piomap->pp_kvaddr;
}

void
pciio_piomap_done(pciio_piomap_t pciio_piomap)
{
    PIOMAP_FUNC(pciio_piomap, piomap_done)
	(CAST_PIOMAP(pciio_piomap));
}

caddr_t
pciio_piotrans_addr(devfs_handle_t dev,	/* translate for this device */
		    device_desc_t dev_desc,	/* device descriptor */
		    pciio_space_t space,	/* CFG, MEM, IO, or a device-decoded window */
		    iopaddr_t addr,	/* starting address (or offset in window) */
		    size_t byte_count,	/* map this many bytes */
		    unsigned flags)
{					/* (currently unused) */
    return DEV_FUNC(dev, piotrans_addr)
	(dev, dev_desc, space, addr, byte_count, flags);
}

caddr_t
pciio_pio_addr(devfs_handle_t dev,	/* translate for this device */
	       device_desc_t dev_desc,	/* device descriptor */
	       pciio_space_t space,	/* CFG, MEM, IO, or a device-decoded window */
	       iopaddr_t addr,		/* starting address (or offset in window) */
	       size_t byte_count,	/* map this many bytes */
	       pciio_piomap_t *mapp,	/* where to return the map pointer */
	       unsigned flags)
{					/* PIO flags */
    pciio_piomap_t          map = 0;
    int			    errfree = 0;
    caddr_t                 res;

    if (mapp) {
	map = *mapp;			/* possible pre-allocated map */
	*mapp = 0;			/* record "no map used" */
    }

    res = pciio_piotrans_addr
	(dev, dev_desc, space, addr, byte_count, flags);
    if (res)
	return res;			/* pciio_piotrans worked */

    if (!map) {
	map = pciio_piomap_alloc
	    (dev, dev_desc, space, addr, byte_count, byte_count, flags);
	if (!map)
	    return res;			/* pciio_piomap_alloc failed */
	errfree = 1;
    }

    res = pciio_piomap_addr
	(map, addr, byte_count);
    if (!res) {
	if (errfree)
	    pciio_piomap_free(map);
	return res;			/* pciio_piomap_addr failed */
    }
    if (mapp)
	*mapp = map;			/* pass back map used */

    return res;				/* pciio_piomap_addr succeeded */
}

iopaddr_t
pciio_piospace_alloc(devfs_handle_t dev,	/* Device requiring space */
		     device_desc_t dev_desc,	/* Device descriptor */
		     pciio_space_t space,	/* MEM32/MEM64/IO */
		     size_t byte_count,	/* Size of mapping */
		     size_t align)
{					/* Alignment needed */
    if (align < NBPP)
	align = NBPP;
    return DEV_FUNC(dev, piospace_alloc)
	(dev, dev_desc, space, byte_count, align);
}

void
pciio_piospace_free(devfs_handle_t dev,	/* Device freeing space */
		    pciio_space_t space,	/* Type of space        */
		    iopaddr_t pciaddr,	/* starting address */
		    size_t byte_count)
{					/* Range of address   */
    DEV_FUNC(dev, piospace_free)
	(dev, space, pciaddr, byte_count);
}

/* =====================================================================
 *          DMA MANAGEMENT
 *
 *      For mapping from pci space to system
 *      physical space.
 */

pciio_dmamap_t
pciio_dmamap_alloc(devfs_handle_t dev,	/* set up mappings for this device */
		   device_desc_t dev_desc,	/* device descriptor */
		   size_t byte_count_max,	/* max size of a mapping */
		   unsigned flags)
{					/* defined in dma.h */
    return (pciio_dmamap_t) DEV_FUNC(dev, dmamap_alloc)
	(dev, dev_desc, byte_count_max, flags);
}

void
pciio_dmamap_free(pciio_dmamap_t pciio_dmamap)
{
    DMAMAP_FUNC(pciio_dmamap, dmamap_free)
	(CAST_DMAMAP(pciio_dmamap));
}

iopaddr_t
pciio_dmamap_addr(pciio_dmamap_t pciio_dmamap,	/* use these mapping resources */
		  paddr_t paddr,	/* map for this address */
		  size_t byte_count)
{					/* map this many bytes */
    return DMAMAP_FUNC(pciio_dmamap, dmamap_addr)
	(CAST_DMAMAP(pciio_dmamap), paddr, byte_count);
}

alenlist_t
pciio_dmamap_list(pciio_dmamap_t pciio_dmamap,	/* use these mapping resources */
		  alenlist_t alenlist,	/* map this Address/Length List */
		  unsigned flags)
{
    return DMAMAP_FUNC(pciio_dmamap, dmamap_list)
	(CAST_DMAMAP(pciio_dmamap), alenlist, flags);
}

void
pciio_dmamap_done(pciio_dmamap_t pciio_dmamap)
{
    DMAMAP_FUNC(pciio_dmamap, dmamap_done)
	(CAST_DMAMAP(pciio_dmamap));
}

iopaddr_t
pciio_dmatrans_addr(devfs_handle_t dev,	/* translate for this device */
		    device_desc_t dev_desc,	/* device descriptor */
		    paddr_t paddr,	/* system physical address */
		    size_t byte_count,	/* length */
		    unsigned flags)
{					/* defined in dma.h */
    return DEV_FUNC(dev, dmatrans_addr)
	(dev, dev_desc, paddr, byte_count, flags);
}

alenlist_t
pciio_dmatrans_list(devfs_handle_t dev,	/* translate for this device */
		    device_desc_t dev_desc,	/* device descriptor */
		    alenlist_t palenlist,	/* system address/length list */
		    unsigned flags)
{					/* defined in dma.h */
    return DEV_FUNC(dev, dmatrans_list)
	(dev, dev_desc, palenlist, flags);
}

iopaddr_t
pciio_dma_addr(devfs_handle_t dev,	/* translate for this device */
	       device_desc_t dev_desc,	/* device descriptor */
	       paddr_t paddr,		/* system physical address */
	       size_t byte_count,	/* length */
	       pciio_dmamap_t *mapp,	/* map to use, then map we used */
	       unsigned flags)
{					/* PIO flags */
    pciio_dmamap_t          map = 0;
    int			    errfree = 0;
    iopaddr_t               res;

    if (mapp) {
	map = *mapp;			/* possible pre-allocated map */
	*mapp = 0;			/* record "no map used" */
    }

    res = pciio_dmatrans_addr
	(dev, dev_desc, paddr, byte_count, flags);
    if (res)
	return res;			/* pciio_dmatrans worked */

    if (!map) {
	map = pciio_dmamap_alloc
	    (dev, dev_desc, byte_count, flags);
	if (!map)
	    return res;			/* pciio_dmamap_alloc failed */
	errfree = 1;
    }

    res = pciio_dmamap_addr
	(map, paddr, byte_count);
    if (!res) {
	if (errfree)
	    pciio_dmamap_free(map);
	return res;			/* pciio_dmamap_addr failed */
    }
    if (mapp)
	*mapp = map;			/* pass back map used */

    return res;				/* pciio_dmamap_addr succeeded */
}

void
pciio_dmamap_drain(pciio_dmamap_t map)
{
    DMAMAP_FUNC(map, dmamap_drain)
	(CAST_DMAMAP(map));
}

void
pciio_dmaaddr_drain(devfs_handle_t dev, paddr_t addr, size_t size)
{
    DEV_FUNC(dev, dmaaddr_drain)
	(dev, addr, size);
}

void
pciio_dmalist_drain(devfs_handle_t dev, alenlist_t list)
{
    DEV_FUNC(dev, dmalist_drain)
	(dev, list);
}

/* =====================================================================
 *          INTERRUPT MANAGEMENT
 *
 *      Allow crosstalk devices to establish interrupts
 */

/*
 * Allocate resources required for an interrupt as specified in intr_desc.
 * Return resource handle in intr_hdl.
 */
pciio_intr_t
pciio_intr_alloc(devfs_handle_t dev,	/* which Crosstalk device */
		 device_desc_t dev_desc,	/* device descriptor */
		 pciio_intr_line_t lines,	/* INTR line(s) to attach */
		 devfs_handle_t owner_dev)
{					/* owner of this interrupt */
    return (pciio_intr_t) DEV_FUNC(dev, intr_alloc)
	(dev, dev_desc, lines, owner_dev);
}

/*
 * Free resources consumed by intr_alloc.
 */
void
pciio_intr_free(pciio_intr_t intr_hdl)
{
    INTR_FUNC(intr_hdl, intr_free)
	(CAST_INTR(intr_hdl));
}

/*
 * Associate resources allocated with a previous pciio_intr_alloc call with the
 * described handler, arg, name, etc.
 *
 * Returns 0 on success, returns <0 on failure.
 */
int
pciio_intr_connect(pciio_intr_t intr_hdl)	/* pciio intr resource handle */
{
    return INTR_FUNC(intr_hdl, intr_connect)
	(CAST_INTR(intr_hdl));
}

/*
 * Disassociate handler with the specified interrupt.
 */
void
pciio_intr_disconnect(pciio_intr_t intr_hdl)
{
    INTR_FUNC(intr_hdl, intr_disconnect)
	(CAST_INTR(intr_hdl));
}

/*
 * Return a hwgraph vertex that represents the CPU currently
 * targeted by an interrupt.
 */
devfs_handle_t
pciio_intr_cpu_get(pciio_intr_t intr_hdl)
{
    return INTR_FUNC(intr_hdl, intr_cpu_get)
	(CAST_INTR(intr_hdl));
}

void
pciio_slot_func_to_name(char		       *name,
			pciio_slot_t		slot,
			pciio_function_t	func)
{
    /*
     * standard connection points:
     *
     * PCIIO_SLOT_NONE:	.../pci/direct
     * PCIIO_FUNC_NONE: .../pci/<SLOT>			ie. .../pci/3
     * multifunction:   .../pci/<SLOT><FUNC>		ie. .../pci/3c
     */

    if (slot == PCIIO_SLOT_NONE)
	sprintf(name, "direct");
    else if (func == PCIIO_FUNC_NONE)
	sprintf(name, "%d", slot);
    else
	sprintf(name, "%d%c", slot, 'a'+func);
}

/* =====================================================================
 *          CONFIGURATION MANAGEMENT
 */

/*
 * Startup a crosstalk provider
 */
void
pciio_provider_startup(devfs_handle_t pciio_provider)
{
    DEV_FUNC(pciio_provider, provider_startup)
	(pciio_provider);
}

/*
 * Shutdown a crosstalk provider
 */
void
pciio_provider_shutdown(devfs_handle_t pciio_provider)
{
    DEV_FUNC(pciio_provider, provider_shutdown)
	(pciio_provider);
}

/*
 * Specify endianness constraints.  The driver tells us what the device
 * does and how it would like to see things in memory.  We reply with
 * how things will actually appear in memory.
 */
pciio_endian_t
pciio_endian_set(devfs_handle_t dev,
		 pciio_endian_t device_end,
		 pciio_endian_t desired_end)
{
    ASSERT((device_end == PCIDMA_ENDIAN_BIG) || (device_end == PCIDMA_ENDIAN_LITTLE));
    ASSERT((desired_end == PCIDMA_ENDIAN_BIG) || (desired_end == PCIDMA_ENDIAN_LITTLE));

#if DEBUG
#if defined(SUPPORT_PRINTING_V_FORMAT)
    printk(KERN_ALERT  "%v: pciio_endian_set is going away.\n"
	    "\tplease use PCIIO_BYTE_STREAM or PCIIO_WORD_VALUES in your\n"
	    "\tpciio_dmamap_alloc and pciio_dmatrans calls instead.\n",
	    dev);
#else
    printk(KERN_ALERT  "0x%x: pciio_endian_set is going away.\n"
	    "\tplease use PCIIO_BYTE_STREAM or PCIIO_WORD_VALUES in your\n"
	    "\tpciio_dmamap_alloc and pciio_dmatrans calls instead.\n",
	    dev);
#endif
#endif

    return DEV_FUNC(dev, endian_set)
	(dev, device_end, desired_end);
}

/*
 * Specify PCI arbitration priority.
 */
pciio_priority_t
pciio_priority_set(devfs_handle_t dev,
		   pciio_priority_t device_prio)
{
    ASSERT((device_prio == PCI_PRIO_HIGH) || (device_prio == PCI_PRIO_LOW));

    return DEV_FUNC(dev, priority_set)
	(dev, device_prio);
}

/*
 * Read value of configuration register
 */
uint64_t
pciio_config_get(devfs_handle_t	dev,
		 unsigned	reg,
		 unsigned	size)
{
    uint64_t	value = 0;
    unsigned	shift = 0;

    /* handle accesses that cross words here,
     * since that's common code between all
     * possible providers.
     */
    while (size > 0) {
	unsigned	biw = 4 - (reg&3);
	if (biw > size)
	    biw = size;

	value |= DEV_FUNC(dev, config_get)
	    (dev, reg, biw) << shift;

	shift += 8*biw;
	reg += biw;
	size -= biw;
    }
    return value;
}

/*
 * Change value of configuration register
 */
void
pciio_config_set(devfs_handle_t	dev,
		 unsigned	reg,
		 unsigned	size,
		 uint64_t	value)
{
    /* handle accesses that cross words here,
     * since that's common code between all
     * possible providers.
     */
    while (size > 0) {
	unsigned	biw = 4 - (reg&3);
	if (biw > size)
	    biw = size;
	    
	DEV_FUNC(dev, config_set)
	    (dev, reg, biw, value);
	reg += biw;
	size -= biw;
	value >>= biw * 8;
    }
}

/* =====================================================================
 *          GENERIC PCI SUPPORT FUNCTIONS
 */

/*
 * Issue a hardware reset to a card.
 */
int
pciio_reset(devfs_handle_t dev)
{
    return DEV_FUNC(dev, reset) (dev);
}

/*
 * flush write gather buffers
 */
int
pciio_write_gather_flush(devfs_handle_t dev)
{
    return DEV_FUNC(dev, write_gather_flush) (dev);
}

devfs_handle_t
pciio_intr_dev_get(pciio_intr_t pciio_intr)
{
    return (pciio_intr->pi_dev);
}

/****** Generic crosstalk pio interfaces ******/
devfs_handle_t
pciio_pio_dev_get(pciio_piomap_t pciio_piomap)
{
    return (pciio_piomap->pp_dev);
}

pciio_slot_t
pciio_pio_slot_get(pciio_piomap_t pciio_piomap)
{
    return (pciio_piomap->pp_slot);
}

pciio_space_t
pciio_pio_space_get(pciio_piomap_t pciio_piomap)
{
    return (pciio_piomap->pp_space);
}

iopaddr_t
pciio_pio_pciaddr_get(pciio_piomap_t pciio_piomap)
{
    return (pciio_piomap->pp_pciaddr);
}

ulong
pciio_pio_mapsz_get(pciio_piomap_t pciio_piomap)
{
    return (pciio_piomap->pp_mapsz);
}

caddr_t
pciio_pio_kvaddr_get(pciio_piomap_t pciio_piomap)
{
    return (pciio_piomap->pp_kvaddr);
}

/****** Generic crosstalk dma interfaces ******/
devfs_handle_t
pciio_dma_dev_get(pciio_dmamap_t pciio_dmamap)
{
    return (pciio_dmamap->pd_dev);
}

pciio_slot_t
pciio_dma_slot_get(pciio_dmamap_t pciio_dmamap)
{
    return (pciio_dmamap->pd_slot);
}

/****** Generic pci slot information interfaces ******/

pciio_info_t
pciio_info_chk(devfs_handle_t pciio)
{
    arbitrary_info_t        ainfo = 0;

    hwgraph_info_get_LBL(pciio, INFO_LBL_PCIIO, &ainfo);
    return (pciio_info_t) ainfo;
}

pciio_info_t
pciio_info_get(devfs_handle_t pciio)
{
    pciio_info_t            pciio_info;

    pciio_info = (pciio_info_t) hwgraph_fastinfo_get(pciio);

#ifdef DEBUG_PCIIO
    {
	int pos;
	char dname[256];
	pos = devfs_generate_path(pciio, dname, 256);
	printk("%s : path= %s\n", __FUNCTION__, &dname[pos]);
    }
#endif /* DEBUG_PCIIO */

    if ((pciio_info != NULL) &&
	(pciio_info->c_fingerprint != pciio_info_fingerprint)
	&& (pciio_info->c_fingerprint != NULL)) {

	return((pciio_info_t)-1); /* Should panic .. */
    }
	

    return pciio_info;
}

void
pciio_info_set(devfs_handle_t pciio, pciio_info_t pciio_info)
{
    if (pciio_info != NULL)
	pciio_info->c_fingerprint = pciio_info_fingerprint;
    hwgraph_fastinfo_set(pciio, (arbitrary_info_t) pciio_info);

    /* Also, mark this vertex as a PCI slot
     * and use the pciio_info, so pciio_info_chk
     * can work (and be fairly efficient).
     */
    hwgraph_info_add_LBL(pciio, INFO_LBL_PCIIO,
			 (arbitrary_info_t) pciio_info);
}

devfs_handle_t
pciio_info_dev_get(pciio_info_t pciio_info)
{
    return (pciio_info->c_vertex);
}

/*ARGSUSED*/
pciio_bus_t
pciio_info_bus_get(pciio_info_t pciio_info)
{
    /* XXX for now O2 always gets back bus 0 */
    return (pciio_bus_t)0;
}

pciio_slot_t
pciio_info_slot_get(pciio_info_t pciio_info)
{
    return (pciio_info->c_slot);
}

pciio_function_t
pciio_info_function_get(pciio_info_t pciio_info)
{
    return (pciio_info->c_func);
}

pciio_vendor_id_t
pciio_info_vendor_id_get(pciio_info_t pciio_info)
{
    return (pciio_info->c_vendor);
}

pciio_device_id_t
pciio_info_device_id_get(pciio_info_t pciio_info)
{
    return (pciio_info->c_device);
}

devfs_handle_t
pciio_info_master_get(pciio_info_t pciio_info)
{
    return (pciio_info->c_master);
}

arbitrary_info_t
pciio_info_mfast_get(pciio_info_t pciio_info)
{
    return (pciio_info->c_mfast);
}

pciio_provider_t       *
pciio_info_pops_get(pciio_info_t pciio_info)
{
    return (pciio_info->c_pops);
}

error_handler_f	       *
pciio_info_efunc_get(pciio_info_t pciio_info)
{
    return (pciio_info->c_efunc);
}

error_handler_arg_t    *
pciio_info_einfo_get(pciio_info_t pciio_info)
{
    return (pciio_info->c_einfo);
}

pciio_space_t
pciio_info_bar_space_get(pciio_info_t info, int win)
{
    return info->c_window[win].w_space;
}

iopaddr_t
pciio_info_bar_base_get(pciio_info_t info, int win)
{
    return info->c_window[win].w_base;
}

size_t
pciio_info_bar_size_get(pciio_info_t info, int win)
{
    return info->c_window[win].w_size;
}

iopaddr_t
pciio_info_rom_base_get(pciio_info_t info)
{
    return info->c_rbase;
}

size_t
pciio_info_rom_size_get(pciio_info_t info)
{
    return info->c_rsize;
}


/* =====================================================================
 *          GENERIC PCI INITIALIZATION FUNCTIONS
 */

/*
 *    pciioinit: called once during device driver
 *      initializtion if this driver is configured into
 *      the system.
 */
void
pciio_init(void)
{
    cdl_p                   cp;

#if DEBUG && ATTACH_DEBUG
    printf("pciio_init\n");
#endif
    /* Allocate the registry.
     * We might already have one.
     * If we don't, go get one.
     * MPness: someone might have
     * set one up for us while we
     * were not looking; use an atomic
     * compare-and-swap to commit to
     * using the new registry if and
     * only if nobody else did first.
     * If someone did get there first,
     * toss the one we allocated back
     * into the pool.
     */
    if (pciio_registry == NULL) {
	cp = cdl_new(EDGE_LBL_PCI, "vendor", "device");
	if (!compare_and_swap_ptr((void **) &pciio_registry, NULL, (void *) cp)) {
	    cdl_del(cp);
	}
    }
    ASSERT(pciio_registry != NULL);
}

/*
 *    pciioattach: called for each vertex in the graph
 *      that is a PCI provider.
 */
/*ARGSUSED */
int
pciio_attach(devfs_handle_t pciio)
{
#if DEBUG && ATTACH_DEBUG
#if defined(SUPPORT_PRINTING_V_FORMAT)
    printk("%v: pciio_attach\n", pciio);
#else
    printk("0x%x: pciio_attach\n", pciio);
#endif
#endif
    return 0;
}

/*
 * Associate a set of pciio_provider functions with a vertex.
 */
void
pciio_provider_register(devfs_handle_t provider, pciio_provider_t *pciio_fns)
{
    hwgraph_info_add_LBL(provider, INFO_LBL_PFUNCS, (arbitrary_info_t) pciio_fns);
}

/*
 * Disassociate a set of pciio_provider functions with a vertex.
 */
void
pciio_provider_unregister(devfs_handle_t provider)
{
    arbitrary_info_t        ainfo;

    hwgraph_info_remove_LBL(provider, INFO_LBL_PFUNCS, (long *) &ainfo);
}

/*
 * Obtain a pointer to the pciio_provider functions for a specified Crosstalk
 * provider.
 */
pciio_provider_t       *
pciio_provider_fns_get(devfs_handle_t provider)
{
    arbitrary_info_t        ainfo = 0;

    (void) hwgraph_info_get_LBL(provider, INFO_LBL_PFUNCS, &ainfo);
    return (pciio_provider_t *) ainfo;
}

/*ARGSUSED4 */
int
pciio_driver_register(
			 pciio_vendor_id_t vendor_id,
			 pciio_device_id_t device_id,
			 char *driver_prefix,
			 unsigned flags)
{
    /* a driver's init routine might call
     * pciio_driver_register before the
     * system calls pciio_init; so we
     * make the init call ourselves here.
     */
    if (pciio_registry == NULL)
	pciio_init();

    return cdl_add_driver(pciio_registry,
			  vendor_id, device_id,
			  driver_prefix, flags, NULL);
}

/*
 * Remove an initialization function.
 */
void
pciio_driver_unregister(
			   char *driver_prefix)
{
    /* before a driver calls unregister,
     * it must have called register; so
     * we can assume we have a registry here.
     */
    ASSERT(pciio_registry != NULL);

    cdl_del_driver(pciio_registry, driver_prefix, NULL);
}

/* 
 * Set the slot status for a device supported by the 
 * driver being registered.
 */
void
pciio_driver_reg_callback(
                           devfs_handle_t pconn_vhdl,
			   int key1,
			   int key2,
                           int error)
{
}

/* 
 * Set the slot status for a device supported by the 
 * driver being unregistered.
 */
void
pciio_driver_unreg_callback(
                           devfs_handle_t pconn_vhdl,
			   int key1,
			   int key2,
                           int error)
{
}

/*
 * Call some function with each vertex that
 * might be one of this driver's attach points.
 */
void
pciio_iterate(char *driver_prefix,
	      pciio_iter_f * func)
{
    /* a driver's init routine might call
     * pciio_iterate before the
     * system calls pciio_init; so we
     * make the init call ourselves here.
     */
    if (pciio_registry == NULL)
	pciio_init();

    ASSERT(pciio_registry != NULL);

    cdl_iterate(pciio_registry, driver_prefix, (cdl_iter_f *) func);
}

devfs_handle_t
pciio_device_register(
		devfs_handle_t connectpt,	/* vertex for /hw/.../pciio/%d */
		devfs_handle_t master,	/* card's master ASIC (PCI provider) */
		pciio_slot_t slot,	/* card's slot */
		pciio_function_t func,	/* card's func */
		pciio_vendor_id_t vendor_id,
		pciio_device_id_t device_id)
{
    return pciio_device_info_register
	(connectpt, pciio_device_info_new (NULL, master, slot, func,
					   vendor_id, device_id));
}

void
pciio_device_unregister(devfs_handle_t pconn)
{
    DEV_FUNC(pconn,device_unregister)(pconn);
}

pciio_info_t
pciio_device_info_new(
		pciio_info_t pciio_info,
		devfs_handle_t master,
		pciio_slot_t slot,
		pciio_function_t func,
		pciio_vendor_id_t vendor_id,
		pciio_device_id_t device_id)
{
    if (!pciio_info)
	GET_NEW(pciio_info);
    ASSERT(pciio_info != NULL);

    pciio_info->c_slot = slot;
    pciio_info->c_func = func;
    pciio_info->c_vendor = vendor_id;
    pciio_info->c_device = device_id;
    pciio_info->c_master = master;
    pciio_info->c_mfast = hwgraph_fastinfo_get(master);
    pciio_info->c_pops = pciio_provider_fns_get(master);
    pciio_info->c_efunc = 0;
    pciio_info->c_einfo = 0;

    return pciio_info;
}

void
pciio_device_info_free(pciio_info_t pciio_info)
{
    /* NOTE : pciio_info is a structure within the pcibr_info
     *	      and not a pointer to memory allocated on the heap !!
     */
    BZERO((char *)pciio_info,sizeof(pciio_info));
}

devfs_handle_t
pciio_device_info_register(
		devfs_handle_t connectpt,		/* vertex at center of bus */
		pciio_info_t pciio_info)	/* details about the connectpt */
{
    char		name[32];
    devfs_handle_t	pconn;
    int device_master_set(devfs_handle_t, devfs_handle_t);

    pciio_slot_func_to_name(name,
			    pciio_info->c_slot,
			    pciio_info->c_func);

    if (GRAPH_SUCCESS !=
	hwgraph_path_add(connectpt, name, &pconn))
	return pconn;

    pciio_info->c_vertex = pconn;
    pciio_info_set(pconn, pciio_info);
#ifdef DEBUG_PCIIO
    {
	int pos;
	char dname[256];
	pos = devfs_generate_path(pconn, dname, 256);
	printk("%s : pconn path= %s \n", __FUNCTION__, &dname[pos]);
    }
#endif /* DEBUG_PCIIO */

    /*
     * create link to our pci provider
     */

    device_master_set(pconn, pciio_info->c_master);

#if USRPCI
    /*
     * Call into usrpci provider to let it initialize for
     * the given slot.
     */
    if (pciio_info->c_slot != PCIIO_SLOT_NONE)
	usrpci_device_register(pconn, pciio_info->c_master, pciio_info->c_slot);
#endif

    return pconn;
}

void
pciio_device_info_unregister(devfs_handle_t connectpt,
			     pciio_info_t pciio_info)
{
    char		name[32];
    devfs_handle_t	pconn;

    if (!pciio_info)
	return;

    pciio_slot_func_to_name(name,
			    pciio_info->c_slot,
			    pciio_info->c_func);

    hwgraph_edge_remove(connectpt,name,&pconn);
    pciio_info_set(pconn,0);

    /* Remove the link to our pci provider */
    hwgraph_edge_remove(pconn, EDGE_LBL_MASTER, NULL);


    hwgraph_vertex_unref(pconn);
    hwgraph_vertex_destroy(pconn);
    
}
/* Add the pci card inventory information to the hwgraph
 */
static void
pciio_device_inventory_add(devfs_handle_t pconn_vhdl)
{
    pciio_info_t	pciio_info = pciio_info_get(pconn_vhdl);

    ASSERT(pciio_info);
    ASSERT(pciio_info->c_vertex == pconn_vhdl);

    /* Donot add inventory  for non-existent devices */
    if ((pciio_info->c_vendor == PCIIO_VENDOR_ID_NONE)	||
	(pciio_info->c_device == PCIIO_DEVICE_ID_NONE))
	return;
    device_inventory_add(pconn_vhdl,INV_IOBD,INV_PCIADAP,
			 pciio_info->c_vendor,pciio_info->c_device,
			 pciio_info->c_slot);
}

static void
pciio_device_inventory_remove(devfs_handle_t pconn_vhdl)
{
#ifdef LATER
    hwgraph_inventory_remove(pconn_vhdl,-1,-1,-1,-1,-1);
#endif
}

/*ARGSUSED */
int
pciio_device_attach(devfs_handle_t pconn,
		    int          drv_flags)
{
    pciio_info_t            pciio_info;
    pciio_vendor_id_t       vendor_id;
    pciio_device_id_t       device_id;


    pciio_device_inventory_add(pconn);
    pciio_info = pciio_info_get(pconn);

    vendor_id = pciio_info->c_vendor;
    device_id = pciio_info->c_device;

    /* we don't start attaching things until
     * all the driver init routines (including
     * pciio_init) have been called; so we
     * can assume here that we have a registry.
     */
    ASSERT(pciio_registry != NULL);

    return(cdl_add_connpt(pciio_registry, vendor_id, device_id, pconn, drv_flags));
}

int
pciio_device_detach(devfs_handle_t pconn,
		    int          drv_flags)
{
    pciio_info_t            pciio_info;
    pciio_vendor_id_t       vendor_id;
    pciio_device_id_t       device_id;

    pciio_device_inventory_remove(pconn);
    pciio_info = pciio_info_get(pconn);

    vendor_id = pciio_info->c_vendor;
    device_id = pciio_info->c_device;

    /* we don't start attaching things until
     * all the driver init routines (including
     * pciio_init) have been called; so we
     * can assume here that we have a registry.
     */
    ASSERT(pciio_registry != NULL);

    return(cdl_del_connpt(pciio_registry, vendor_id, device_id,
		          pconn, drv_flags));

}

/*
 * pciio_error_register:
 * arrange for a function to be called with
 * a specified first parameter plus other
 * information when an error is encountered
 * and traced to the pci slot corresponding
 * to the connection point pconn.
 *
 * may also be called with a null function
 * pointer to "unregister" the error handler.
 *
 * NOTE: subsequent calls silently overwrite
 * previous data for this vertex. We assume that
 * cooperating drivers, well, cooperate ...
 */
void
pciio_error_register(devfs_handle_t pconn,
		     error_handler_f *efunc,
		     error_handler_arg_t einfo)
{
    pciio_info_t            pciio_info;

    pciio_info = pciio_info_get(pconn);
    ASSERT(pciio_info != NULL);
    pciio_info->c_efunc = efunc;
    pciio_info->c_einfo = einfo;
}

/*
 * Check if any device has been found in this slot, and return
 * true or false
 * vhdl is the vertex for the slot
 */
int
pciio_slot_inuse(devfs_handle_t pconn_vhdl)
{
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);

    ASSERT(pciio_info);
    ASSERT(pciio_info->c_vertex == pconn_vhdl);
    if (pciio_info->c_vendor) {
	/*
	 * Non-zero value for vendor indicate
	 * a board being found in this slot.
	 */
	return 1;
    }
    return 0;
}

int
pciio_dma_enabled(devfs_handle_t pconn_vhdl)
{
	return DEV_FUNC(pconn_vhdl, dma_enabled)(pconn_vhdl);
}

/*
 * These are complementary Linux interfaces that takes in a pci_dev * as the 
 * first arguement instead of devfs_handle_t.
 */
iopaddr_t               snia_pciio_dmatrans_addr(struct pci_dev *, device_desc_t, paddr_t, size_t, unsigned);
pciio_dmamap_t          snia_pciio_dmamap_alloc(struct pci_dev *, device_desc_t, size_t, unsigned);
void                    snia_pciio_dmamap_free(pciio_dmamap_t);
iopaddr_t               snia_pciio_dmamap_addr(pciio_dmamap_t, paddr_t, size_t);
void                    snia_pciio_dmamap_done(pciio_dmamap_t);
pciio_endian_t          snia_pciio_endian_set(struct pci_dev *pci_dev, pciio_endian_t device_end,
					      pciio_endian_t desired_end);

#include <linux/module.h>
EXPORT_SYMBOL(snia_pciio_dmatrans_addr);
EXPORT_SYMBOL(snia_pciio_dmamap_alloc);
EXPORT_SYMBOL(snia_pciio_dmamap_free);
EXPORT_SYMBOL(snia_pciio_dmamap_addr);
EXPORT_SYMBOL(snia_pciio_dmamap_done);
EXPORT_SYMBOL(snia_pciio_endian_set);

pciio_endian_t
snia_pciio_endian_set(struct pci_dev *pci_dev,
	pciio_endian_t device_end,
	pciio_endian_t desired_end)
{
	devfs_handle_t dev = PCIDEV_VERTEX(pci_dev);
	
	return DEV_FUNC(dev, endian_set)
		(dev, device_end, desired_end);
}

iopaddr_t
snia_pciio_dmatrans_addr(struct pci_dev *pci_dev, /* translate for this device */
                    device_desc_t dev_desc,     /* device descriptor */
                    paddr_t paddr,      /* system physical address */
                    size_t byte_count,  /* length */
                    unsigned flags)
{                                       /* defined in dma.h */

    devfs_handle_t dev = PCIDEV_VERTEX(pci_dev);

    return DEV_FUNC(dev, dmatrans_addr)
        (dev, dev_desc, paddr, byte_count, flags);
}

pciio_dmamap_t
snia_pciio_dmamap_alloc(struct pci_dev *pci_dev,  /* set up mappings for this device */
                   device_desc_t dev_desc,      /* device descriptor */
                   size_t byte_count_max,       /* max size of a mapping */
                   unsigned flags)
{                                       /* defined in dma.h */

    devfs_handle_t dev = PCIDEV_VERTEX(pci_dev);

    return (pciio_dmamap_t) DEV_FUNC(dev, dmamap_alloc)
        (dev, dev_desc, byte_count_max, flags);
}

void
snia_pciio_dmamap_free(pciio_dmamap_t pciio_dmamap)
{
    DMAMAP_FUNC(pciio_dmamap, dmamap_free)
        (CAST_DMAMAP(pciio_dmamap));
}

iopaddr_t
snia_pciio_dmamap_addr(pciio_dmamap_t pciio_dmamap,  /* use these mapping resources */
                  paddr_t paddr,        /* map for this address */
                  size_t byte_count)
{                                       /* map this many bytes */
    return DMAMAP_FUNC(pciio_dmamap, dmamap_addr)
        (CAST_DMAMAP(pciio_dmamap), paddr, byte_count);
}

void
snia_pciio_dmamap_done(pciio_dmamap_t pciio_dmamap)
{
    DMAMAP_FUNC(pciio_dmamap, dmamap_done)
        (CAST_DMAMAP(pciio_dmamap));
}

