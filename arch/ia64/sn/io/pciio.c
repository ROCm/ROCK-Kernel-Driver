/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

#define	USRPCI	0

#include <linux/types.h>
#include <linux/config.h>
#include <linux/slab.h>
#include <asm/sn/sgi.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/hcl_util.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/pci/bridge.h>
#include <asm/sn/ioerror_handling.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/pciio_private.h>

#define DEBUG_PCIIO
#undef DEBUG_PCIIO	/* turn this on for yet more console output */


#define NEW(ptr)	(ptr = kmalloc(sizeof (*(ptr)), GFP_KERNEL))
#define DEL(ptr)	(kfree(ptr))

char                    pciio_info_fingerprint[] = "pciio_info";

cdl_p                   pciio_registry = NULL;

int
badaddr_val(volatile void *addr, int len, volatile void *ptr)
{
	switch (len) {
		case 4: *(volatile u32*)ptr = *(((volatile u32*)(((u64) addr)^4)));
		default: printk("FIXME: argh fix badaddr_val\n");
	}
	/* no such thing as a bad addr .... */
	return(0);
}


void
cmn_err_tag(int seqnumber, register int level, char *fmt, ...)
{
}

nasid_t
get_console_nasid(void)
{
#ifdef IRIX
	return console_nasid;
#else
	return 0;
#endif
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

#if CONFIG_SGI_IP35 || CONFIG_IA64_SGI_SN1 || CONFIG_IA64_GENERIC
/*
 *    For the moment, we will assume that IP27
 *      only use Bridge ASICs to provide PCI support.
 */
#include <asm/sn/pci/pcibr.h>
#define DEV_FUNC(dev,func)	pcibr_##func
#define CAST_PIOMAP(x)		((pcibr_piomap_t)(x))
#define CAST_DMAMAP(x)		((pcibr_dmamap_t)(x))
#define CAST_INTR(x)		((pcibr_intr_t)(x))
#endif /* CONFIG_SGI_IP35 || CONFIG_IA64_SGI_SN1 */

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
int                     pciio_intr_connect(pciio_intr_t, intr_func_t, intr_arg_t, void *thread);
void                    pciio_intr_disconnect(pciio_intr_t);
devfs_handle_t            pciio_intr_cpu_get(pciio_intr_t);

void			pciio_slot_func_to_name(char *, pciio_slot_t, pciio_function_t);
static pciio_info_t     pciio_cardinfo_get(devfs_handle_t, pciio_slot_t);
int                     pciio_error_handler(devfs_handle_t, int, ioerror_mode_t, ioerror_t *);
int                     pciio_error_devenable(devfs_handle_t, int);

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
int                     pciio_device_attach(devfs_handle_t);
int			pciio_device_detach(devfs_handle_t);
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

    card_info = pciio_info_get(dev);
    ASSERT(card_info != NULL);

    provider_fns = pciio_info_pops_get(card_info);
    ASSERT(provider_fns != NULL);

    return (provider_fns);
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
pciio_intr_connect(pciio_intr_t intr_hdl,	/* pciio intr resource handle */
		   intr_func_t intr_func,	/* pciio intr handler */
		   intr_arg_t intr_arg,	/* arg to intr handler */
		   void *thread)
{					/* intr thread to use */
    return INTR_FUNC(intr_hdl, intr_connect)
	(CAST_INTR(intr_hdl), intr_func, intr_arg, thread);
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

/* =====================================================================
 *          ERROR MANAGEMENT
 */

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

/*
 * pciio_cardinfo_get
 *
 * Get the pciio info structure corresponding to the
 * specified PCI "slot" (we like it when the same index
 * number is used for the PCI IDSEL, the REQ/GNT pair,
 * and the interrupt line being used for INTA. We like
 * it so much we call it the slot number).
 */
static pciio_info_t
pciio_cardinfo_get(
		      devfs_handle_t pciio_vhdl,
		      pciio_slot_t pci_slot)
{
    char                    namebuf[16];
    pciio_info_t	    info = 0;
    devfs_handle_t	    conn;

    pciio_slot_func_to_name(namebuf, pci_slot, PCIIO_FUNC_NONE);
    if (GRAPH_SUCCESS ==
	hwgraph_traverse(pciio_vhdl, namebuf, &conn)) {
	info = pciio_info_chk(conn);
	hwgraph_vertex_unref(conn);
    }

    return info;
}

/*
 * pciio_error_handler:
 * dispatch an error to the appropriate
 * pciio connection point, or process
 * it as a generic pci error.
 * Yes, the first parameter is the
 * provider vertex at the middle of
 * the bus; we get to the pciio connect
 * point using the ioerror widgetdev field.
 *
 * This function is called by the
 * specific PCI provider, after it has figured
 * out where on the PCI bus (including which slot,
 * if it can tell) the error came from.
 */
/*ARGSUSED */
int
pciio_error_handler(
		       devfs_handle_t pciio_vhdl,
		       int error_code,
		       ioerror_mode_t mode,
		       ioerror_t *ioerror)
{
    pciio_info_t            pciio_info;
    devfs_handle_t            pconn_vhdl;
#if USRPCI
    devfs_handle_t            usrpci_v;
#endif
    pciio_slot_t            slot;

    int                     retval;
#if defined(CONFIG_SGI_IO_ERROR_HANDLING)
    error_state_t	    e_state;
#endif

#ifdef IRIX
#if DEBUG && ERROR_DEBUG
    cmn_err(CE_CONT, "%v: pciio_error_handler\n", pciio_vhdl);
#endif
#endif

    IOERR_PRINTF(cmn_err(CE_NOTE,
			 "%v: PCI Bus Error: Error code: %d Error mode: %d\n",
			 pciio_vhdl, error_code, mode));

    /* If there is an error handler sitting on
     * the "no-slot" connection point, give it
     * first crack at the error. NOTE: it is
     * quite possible that this function may
     * do further refining of the ioerror.
     */
    pciio_info = pciio_cardinfo_get(pciio_vhdl, PCIIO_SLOT_NONE);
    if (pciio_info && pciio_info->c_efunc) {
	pconn_vhdl = pciio_info_dev_get(pciio_info);
#if defined(CONFIG_SGI_IO_ERROR_HANDLING)
	e_state = error_state_get(pciio_vhdl);

	if (e_state == ERROR_STATE_ACTION)
	    (void)error_state_set(pciio_vhdl, ERROR_STATE_NONE);

	if (error_state_set(pconn_vhdl,e_state) ==
	    ERROR_RETURN_CODE_CANNOT_SET_STATE)
	    return(IOERROR_UNHANDLED);
#endif
	retval = pciio_info->c_efunc
	    (pciio_info->c_einfo, error_code, mode, ioerror);
	if (retval != IOERROR_UNHANDLED)
	    return retval;
    }

    /* Is the error associated with a particular slot?
     */
    if (IOERROR_FIELDVALID(ioerror, widgetdev)) {
	/*
	 * NOTE : 
	 * widgetdev is a 4byte value encoded as slot in the higher order
	 * 2 bytes and function in the lower order 2 bytes.
	 */
#ifdef IRIX
	slot = pciio_widgetdev_slot_get(IOERROR_GETVALUE(ioerror, widgetdev));
#else
	slot = 0;
#endif

	/* If this slot has an error handler,
	 * deliver the error to it.
	 */
	pciio_info = pciio_cardinfo_get(pciio_vhdl, slot);
	if (pciio_info != NULL) {
	    if (pciio_info->c_efunc != NULL) {

		pconn_vhdl = pciio_info_dev_get(pciio_info);
#if defined(CONFIG_SGI_IO_ERROR_HANDLING)
		e_state = error_state_get(pciio_vhdl);

		if (e_state == ERROR_STATE_ACTION)
		    (void)error_state_set(pciio_vhdl, ERROR_STATE_NONE);

		if (error_state_set(pconn_vhdl,e_state) ==
		    ERROR_RETURN_CODE_CANNOT_SET_STATE)
		    return(IOERROR_UNHANDLED);
#endif
		retval = pciio_info->c_efunc
		    (pciio_info->c_einfo, error_code, mode, ioerror);
		if (retval != IOERROR_UNHANDLED)
		    return retval;
	    }

#if USRPCI
	    /* If the USRPCI driver is available and
	     * knows about this connection point,
	     * deliver the error to it.
	     *
	     * OK to use pconn_vhdl here, even though we
	     * have already UNREF'd it, since we know that
	     * it is not going away.
	     */
	    pconn_vhdl = pciio_info_dev_get(pciio_info);
	    if (GRAPH_SUCCESS ==
		hwgraph_traverse(pconn_vhdl, EDGE_LBL_USRPCI, &usrpci_v)) {
		retval = usrpci_error_handler
		    (usrpci_v, error_code, IOERROR_GETVALUE(ioerror, busaddr));
		hwgraph_vertex_unref(usrpci_v);
		if (retval != IOERROR_UNHANDLED) {
		    /*
		     * This unref is not needed.  If this code is called often enough,
		     * the system will crash, due to vertex reference count reaching 0,
		     * causing vertex to be unallocated.  -jeremy
		     * hwgraph_vertex_unref(pconn_vhdl);
		     */
		    return retval;
		}
	    }
#endif
	}
    }

    return (mode == MODE_DEVPROBE)
	? IOERROR_HANDLED	/* probes are OK */
	: IOERROR_UNHANDLED;	/* otherwise, foo! */
}

int
pciio_error_devenable(devfs_handle_t pconn_vhdl, int error_code)
{
    return DEV_FUNC(pconn_vhdl, error_devenable)
	(pconn_vhdl, error_code);
    /* no cleanup specific to this layer. */
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
    cmn_err(CE_ALERT,
	    "%v: pciio_endian_set is going away.\n"
	    "\tplease use PCIIO_BYTE_STREAM or PCIIO_WORD_VALUES in your\n"
	    "\tpciio_dmamap_alloc and pciio_dmatrans calls instead.\n",
	    dev);
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
pciio_slot_t
pciio_error_extract(devfs_handle_t 	dev,
		   pciio_space_t 	*space,
		   iopaddr_t		*offset)
{
	ASSERT(dev != NODEV);
	return DEV_FUNC(dev,error_extract)(dev,space,offset);
}

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

#ifdef BRINGUP
    if ((pciio_info != NULL) &&
	(pciio_info->c_fingerprint != pciio_info_fingerprint)
	&& (pciio_info->c_fingerprint != NULL)) {
#else
    if ((pciio_info != NULL) &&
	(pciio_info->c_fingerprint != pciio_info_fingerprint)) {
#endif /* BRINGUP */

	printk("pciio_info_get: Found fastinfo 0x%p but wrong fingerprint %s\n", pciio_info,
	pciio_info->c_fingerprint);
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
    cmn_err(CE_CONT, "%v: pciio_attach\n", pciio);
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

#ifdef IRIX
    hwgraph_info_remove_LBL(provider, INFO_LBL_PFUNCS, &ainfo);
#else
    hwgraph_info_remove_LBL(provider, INFO_LBL_PFUNCS, (long *) &ainfo);
#endif
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
			  driver_prefix, flags);
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

    cdl_del_driver(pciio_registry, driver_prefix);
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
	NEW(pciio_info);
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

    pciio_slot_func_to_name(name,
			    pciio_info->c_slot,
			    pciio_info->c_func);

    printk("pciio_device_info_register: connectpt 0x%p, pciio_info 0x%p\n", connectpt, pciio_info);

    if (GRAPH_SUCCESS !=
	hwgraph_path_add(connectpt, name, &pconn))
	return pconn;

    pciio_info->c_vertex = pconn;
    pciio_info_set(pconn, pciio_info);
#ifdef BRINGUP
    {
	int pos;
	char dname[256];
	pos = devfs_generate_path(pconn, dname, 256);
	printk("%s : pconn path= %s \n", __FUNCTION__, &dname[pos]);
    }
#endif /* BRINGUP */

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
#ifdef IRIX
    hwgraph_inventory_remove(pconn_vhdl,-1,-1,-1,-1,-1);
#endif
}

/*ARGSUSED */
int
pciio_device_attach(devfs_handle_t pconn)
{
    pciio_info_t            pciio_info;
    pciio_vendor_id_t       vendor_id;
    pciio_device_id_t       device_id;

    pciio_device_inventory_add(pconn);
    pciio_info = pciio_info_get(pconn);

    vendor_id = pciio_info->c_vendor;
    device_id = pciio_info->c_device;

    printk("pciio_device_attach: Function 0x%p, vendor 0x%x, device_id %x\n", pconn, vendor_id, device_id);

    /* we don't start attaching things until
     * all the driver init routines (including
     * pciio_init) have been called; so we
     * can assume here that we have a registry.
     */
    ASSERT(pciio_registry != NULL);

    return(cdl_add_connpt(pciio_registry, vendor_id, device_id, pconn));

}

int
pciio_device_detach(devfs_handle_t pconn)
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

    cdl_del_connpt(pciio_registry, vendor_id, device_id, pconn);

    return(0);
    
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
