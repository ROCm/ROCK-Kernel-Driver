/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_PCI_PCIBR_H
#define _ASM_SN_PCI_PCIBR_H

#if defined(__KERNEL__)

#include <asm/sn/dmamap.h>
#include <asm/sn/iobus.h>
#include <asm/sn/pio.h>

#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/bridge.h>

/* =====================================================================
 *    symbolic constants used by pcibr's xtalk bus provider
 */

#define PCIBR_PIOMAP_BUSY		0x80000000

#define PCIBR_DMAMAP_BUSY		0x80000000
#define	PCIBR_DMAMAP_SSRAM		0x40000000

#define PCIBR_INTR_BLOCKED		0x40000000
#define PCIBR_INTR_BUSY			0x80000000

#if LANGUAGE_C

/* =====================================================================
 *    opaque types used by pcibr's xtalk bus provider
 */

typedef struct pcibr_piomap_s *pcibr_piomap_t;
typedef struct pcibr_dmamap_s *pcibr_dmamap_t;
typedef struct pcibr_intr_s *pcibr_intr_t;

/* =====================================================================
 *    primary entry points: Bridge (pcibr) device driver
 *
 *	These functions are normal device driver entry points
 *	and are called along with the similar entry points from
 *	other device drivers. They are included here as documentation
 *	of their existance and purpose.
 *
 *	pcibr_init() is called to inform us that there is a pcibr driver
 *	configured into the kernel; it is responsible for registering
 *	as a crosstalk widget and providing a routine to be called
 *	when a widget with the proper part number is observed.
 *
 *	pcibr_attach() is called for each vertex in the hardware graph
 *	corresponding to a crosstalk widget with the manufacturer
 *	code and part number registered by pcibr_init().
 */

extern void		pcibr_init(void);

extern int		pcibr_attach(devfs_handle_t);

/* =====================================================================
 *    bus provider function table
 *
 *	Normally, this table is only handed off explicitly
 *	during provider initialization, and the PCI generic
 *	layer will stash a pointer to it in the vertex; however,
 *	exporting it explicitly enables a performance hack in
 *	the generic PCI provider where if we know at compile
 *	time that the only possible PCI provider is a
 *	pcibr, we can go directly to this ops table.
 */

extern pciio_provider_t pcibr_provider;

/* =====================================================================
 *    secondary entry points: pcibr PCI bus provider
 *
 *	These functions are normally exported explicitly by
 *	a direct call from the pcibr initialization routine
 *	into the generic crosstalk provider; they are included
 *	here to enable a more aggressive performance hack in
 *	the generic crosstalk layer, where if we know that the
 *	only possible crosstalk provider is pcibr, and we can
 *	guarantee that all entry points are properly named, and
 *	we can deal with the implicit casting properly, then
 *	we can turn many of the generic provider routines into
 *	plain brances, or even eliminate them (given sufficient
 *	smarts on the part of the compilation system).
 */

extern pcibr_piomap_t	pcibr_piomap_alloc(devfs_handle_t dev,
					   device_desc_t dev_desc,
					   pciio_space_t space,
					   iopaddr_t pci_addr,
					   size_t byte_count,
					   size_t byte_count_max,
					   unsigned flags);

extern void		pcibr_piomap_free(pcibr_piomap_t piomap);

extern caddr_t		pcibr_piomap_addr(pcibr_piomap_t piomap,
					  iopaddr_t xtalk_addr,
					  size_t byte_count);

extern void		pcibr_piomap_done(pcibr_piomap_t piomap);

extern caddr_t		pcibr_piotrans_addr(devfs_handle_t dev,
					    device_desc_t dev_desc,
					    pciio_space_t space,
					    iopaddr_t pci_addr,
					    size_t byte_count,
					    unsigned flags);

extern iopaddr_t	pcibr_piospace_alloc(devfs_handle_t dev,
					     device_desc_t dev_desc,
					     pciio_space_t space,
					     size_t byte_count,
					     size_t alignment);
extern void		pcibr_piospace_free(devfs_handle_t dev,
					    pciio_space_t space,
					    iopaddr_t pciaddr,
					    size_t byte_count);

extern pcibr_dmamap_t	pcibr_dmamap_alloc(devfs_handle_t dev,
					   device_desc_t dev_desc,
					   size_t byte_count_max,
					   unsigned flags);

extern void		pcibr_dmamap_free(pcibr_dmamap_t dmamap);

extern iopaddr_t	pcibr_dmamap_addr(pcibr_dmamap_t dmamap,
					  paddr_t paddr,
					  size_t byte_count);

extern alenlist_t	pcibr_dmamap_list(pcibr_dmamap_t dmamap,
					  alenlist_t palenlist,
					  unsigned flags);

extern void		pcibr_dmamap_done(pcibr_dmamap_t dmamap);

extern iopaddr_t	pcibr_dmatrans_addr(devfs_handle_t dev,
					    device_desc_t dev_desc,
					    paddr_t paddr,
					    size_t byte_count,
					    unsigned flags);

extern alenlist_t	pcibr_dmatrans_list(devfs_handle_t dev,
					    device_desc_t dev_desc,
					    alenlist_t palenlist,
					    unsigned flags);

extern void		pcibr_dmamap_drain(pcibr_dmamap_t map);

extern void		pcibr_dmaaddr_drain(devfs_handle_t vhdl,
					    paddr_t addr,
					    size_t bytes);

extern void		pcibr_dmalist_drain(devfs_handle_t vhdl,
					    alenlist_t list);

typedef unsigned	pcibr_intr_ibit_f(pciio_info_t info,
					  pciio_intr_line_t lines);

extern void		pcibr_intr_ibit_set(devfs_handle_t, pcibr_intr_ibit_f *);

extern pcibr_intr_t	pcibr_intr_alloc(devfs_handle_t dev,
					 device_desc_t dev_desc,
					 pciio_intr_line_t lines,
					 devfs_handle_t owner_dev);

extern void		pcibr_intr_free(pcibr_intr_t intr);

extern int		pcibr_intr_connect(pcibr_intr_t intr,
					   intr_func_t intr_func,
					   intr_arg_t intr_arg,
					   void *thread);

extern void		pcibr_intr_disconnect(pcibr_intr_t intr);

extern devfs_handle_t	pcibr_intr_cpu_get(pcibr_intr_t intr);

extern void		pcibr_provider_startup(devfs_handle_t pcibr);

extern void		pcibr_provider_shutdown(devfs_handle_t pcibr);

extern int		pcibr_reset(devfs_handle_t dev);

extern int              pcibr_write_gather_flush(devfs_handle_t dev);

extern pciio_endian_t	pcibr_endian_set(devfs_handle_t dev,
					 pciio_endian_t device_end,
					 pciio_endian_t desired_end);

extern pciio_priority_t pcibr_priority_set(devfs_handle_t dev,
					   pciio_priority_t device_prio);

extern uint64_t		pcibr_config_get(devfs_handle_t conn,
					 unsigned reg,
					 unsigned size);

extern void		pcibr_config_set(devfs_handle_t conn,
					 unsigned reg,
					 unsigned size,
					 uint64_t value);

extern int		pcibr_error_devenable(devfs_handle_t pconn_vhdl,
					      int error_code);

extern pciio_slot_t	pcibr_error_extract(devfs_handle_t pcibr_vhdl,
					    pciio_space_t *spacep,
					    iopaddr_t *addrp);

extern int		pcibr_rrb_alloc(devfs_handle_t pconn_vhdl,
					int *count_vchan0,
					int *count_vchan1);

extern int		pcibr_wrb_flush(devfs_handle_t pconn_vhdl);
extern int		pcibr_rrb_check(devfs_handle_t pconn_vhdl,
					int *count_vchan0,
					int *count_vchan1,
					int *count_reserved,
					int *count_pool);

extern int		pcibr_alloc_all_rrbs(devfs_handle_t vhdl, int even_odd,
					     int dev_1_rrbs, int virt1,
					     int dev_2_rrbs, int virt2,
					     int dev_3_rrbs, int virt3,
					     int dev_4_rrbs, int virt4);

typedef void
rrb_alloc_funct_f	(devfs_handle_t xconn_vhdl,
			 int *vendor_list);

typedef rrb_alloc_funct_f      *rrb_alloc_funct_t;

void			pcibr_set_rrb_callback(devfs_handle_t xconn_vhdl,
					       rrb_alloc_funct_f *func);

extern void		pcibr_device_unregister(devfs_handle_t);
extern int		pcibr_dma_enabled(devfs_handle_t);
/*
 * Bridge-specific flags that can be set via pcibr_device_flags_set
 * and cleared via pcibr_device_flags_clear.  Other flags are
 * more generic and are maniuplated through PCI-generic interfaces.
 *
 * Note that all PCI implementation-specific flags (Bridge flags, in
 * this case) are in bits 15-31.  The lower 15 bits are reserved
 * for PCI-generic flags.
 *
 * Some of these flags have been "promoted" to the
 * generic layer, so they can be used without having
 * to "know" that the PCI bus is hosted by a Bridge.
 *
 * PCIBR_NO_ATE_ROUNDUP: Request that no rounding up be done when 
 * allocating ATE's. ATE count computation will assume that the
 * address to be mapped will start on a page boundary.
 */
#define PCIBR_NO_ATE_ROUNDUP    0x00008000
#define PCIBR_WRITE_GATHER	0x00010000	/* please use PCIIO version */
#define PCIBR_NOWRITE_GATHER	0x00020000	/* please use PCIIO version */
#define PCIBR_PREFETCH		0x00040000	/* please use PCIIO version */
#define PCIBR_NOPREFETCH	0x00080000	/* please use PCIIO version */
#define PCIBR_PRECISE		0x00100000
#define PCIBR_NOPRECISE		0x00200000
#define PCIBR_BARRIER		0x00400000
#define PCIBR_NOBARRIER		0x00800000
#define PCIBR_VCHAN0		0x01000000
#define PCIBR_VCHAN1		0x02000000
#define PCIBR_64BIT		0x04000000
#define PCIBR_NO64BIT		0x08000000
#define PCIBR_SWAP		0x10000000
#define PCIBR_NOSWAP		0x20000000

#define	PCIBR_EXTERNAL_ATES	0x40000000	/* uses external ATEs */
#define	PCIBR_ACTIVE		0x80000000	/* need a "done" */

/* Flags that have meaning to pcibr_device_flags_{set,clear} */
#define PCIBR_DEVICE_FLAGS (	\
	PCIBR_WRITE_GATHER	|\
	PCIBR_NOWRITE_GATHER	|\
	PCIBR_PREFETCH		|\
	PCIBR_NOPREFETCH	|\
	PCIBR_PRECISE		|\
	PCIBR_NOPRECISE		|\
	PCIBR_BARRIER		|\
	PCIBR_NOBARRIER		\
)

/* Flags that have meaning to *_dmamap_alloc, *_dmatrans_{addr,list} */
#define PCIBR_DMA_FLAGS (	\
	PCIBR_PREFETCH		|\
	PCIBR_NOPREFETCH	|\
	PCIBR_PRECISE		|\
	PCIBR_NOPRECISE		|\
	PCIBR_BARRIER		|\
	PCIBR_NOBARRIER		|\
	PCIBR_VCHAN0		|\
	PCIBR_VCHAN1		\
)

typedef int		pcibr_device_flags_t;

/*
 * Set bits in the Bridge Device(x) register for this device.
 * "flags" are defined above. NOTE: this includes turning
 * things *OFF* as well as turning them *ON* ...
 */
extern int		pcibr_device_flags_set(devfs_handle_t dev,
					     pcibr_device_flags_t flags);

/*
 * Allocate Read Response Buffers for use by the specified device.
 * count_vchan0 is the total number of buffers desired for the
 * "normal" channel.  count_vchan1 is the total number of buffers
 * desired for the "virtual" channel.  Returns 0 on success, or
 * <0 on failure, which occurs when we're unable to allocate any
 * buffers to a channel that desires at least one buffer.
 */
extern int		pcibr_rrb_alloc(devfs_handle_t pconn_vhdl,
					int *count_vchan0,
					int *count_vchan1);

/*
 * Get the starting PCIbus address out of the given DMA map.
 * This function is supposed to be used by a close friend of PCI bridge
 * since it relies on the fact that the starting address of the map is fixed at
 * the allocation time in the current implementation of PCI bridge.
 */
extern iopaddr_t	pcibr_dmamap_pciaddr_get(pcibr_dmamap_t);

extern xwidget_intr_preset_f pcibr_xintr_preset;

extern void		pcibr_hints_fix_rrbs(devfs_handle_t);
extern void		pcibr_hints_dualslot(devfs_handle_t, pciio_slot_t, pciio_slot_t);
extern void		pcibr_hints_subdevs(devfs_handle_t, pciio_slot_t, uint64_t);
extern void		pcibr_hints_handsoff(devfs_handle_t);

typedef unsigned	pcibr_intr_bits_f(pciio_info_t, pciio_intr_line_t);
extern void		pcibr_hints_intr_bits(devfs_handle_t, pcibr_intr_bits_f *);

extern int		pcibr_asic_rev(devfs_handle_t);

#endif 	/* _LANGUAGE_C */
#endif	/* #if defined(__KERNEL__) */
/* 
 * Some useful ioctls into the pcibr driver
 */
#define PCIBR			'p'
#define _PCIBR(x)		((PCIBR << 8) | (x))

#define PCIBR_SLOT_POWERUP	_PCIBR(1)
#define PCIBR_SLOT_SHUTDOWN	_PCIBR(2)
#define PCIBR_SLOT_INQUIRY	_PCIBR(3)

#endif				/* _ASM_SN_PCI_PCIBR_H */
