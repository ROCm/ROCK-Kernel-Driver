/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_PCI_PCIIO_PRIVATE_H
#define _ASM_SN_PCI_PCIIO_PRIVATE_H

#ifdef colin
#include <ksys/xthread.h>
#endif

/*
 * pciio_private.h -- private definitions for pciio
 * PCI drivers should NOT include this file.
 */

#ident "sys/PCI/pciio_private: $Revision: 1.13 $"

/*
 * All PCI providers set up PIO using this information.
 */
struct pciio_piomap_s {
    unsigned                pp_flags;	/* PCIIO_PIOMAP flags */
    devfs_handle_t            pp_dev;	/* associated pci card */
    pciio_slot_t            pp_slot;	/* which slot the card is in */
    pciio_space_t           pp_space;	/* which address space */
    iopaddr_t               pp_pciaddr;		/* starting offset of mapping */
    size_t                  pp_mapsz;	/* size of this mapping */
    caddr_t                 pp_kvaddr;	/* kernel virtual address to use */
};

/*
 * All PCI providers set up DMA using this information.
 */
struct pciio_dmamap_s {
    unsigned                pd_flags;	/* PCIIO_DMAMAP flags */
    devfs_handle_t            pd_dev;	/* associated pci card */
    pciio_slot_t            pd_slot;	/* which slot the card is in */
};

/*
 * All PCI providers set up interrupts using this information.
 */

struct pciio_intr_s {
    unsigned                pi_flags;	/* PCIIO_INTR flags */
    devfs_handle_t            pi_dev;	/* associated pci card */
    device_desc_t	    pi_dev_desc;	/* override device descriptor */
    pciio_intr_line_t       pi_lines;	/* which interrupt line(s) */
    intr_func_t             pi_func;	/* handler function (when connected) */
    intr_arg_t              pi_arg;	/* handler parameter (when connected) */
#ifdef IRIX
    thd_int_t               pi_tinfo;	/* Thread info (when connected) */
#endif
    cpuid_t                 pi_mustruncpu; /* Where we must run. */
    int			    pi_irq;	/* IRQ assigned */
    int			    pi_cpu;	/* cpu assigned */
};

/* PCIIO_INTR (pi_flags) flags */
#define PCIIO_INTR_CONNECTED	1	/* interrupt handler/thread has been connected */
#define PCIIO_INTR_NOTHREAD	2	/* interrupt handler wants to be called at interrupt level */

/*
 * Each PCI Card has one of these.
 */

struct pciio_info_s {
    char                   *c_fingerprint;
    devfs_handle_t            c_vertex;	/* back pointer to vertex */
    pciio_bus_t             c_bus;	/* which bus the card is in */
    pciio_slot_t            c_slot;	/* which slot the card is in */
    pciio_function_t        c_func;	/* which func (on multi-func cards) */
    pciio_vendor_id_t       c_vendor;	/* PCI card "vendor" code */
    pciio_device_id_t       c_device;	/* PCI card "device" code */
    devfs_handle_t            c_master;	/* PCI bus provider */
    arbitrary_info_t        c_mfast;	/* cached fastinfo from c_master */
    pciio_provider_t       *c_pops;	/* cached provider from c_master */
    error_handler_f        *c_efunc;	/* error handling function */
    error_handler_arg_t     c_einfo;	/* first parameter for efunc */

    struct {				/* state of BASE regs */
	pciio_space_t		w_space;
	iopaddr_t		w_base;
	size_t			w_size;
    }			    c_window[6];

    unsigned		    c_rbase;	/* EXPANSION ROM base addr */
    unsigned		    c_rsize;	/* EXPANSION ROM size (bytes) */

    pciio_piospace_t	    c_piospace;	/* additional I/O spaces allocated */
};

extern char             pciio_info_fingerprint[];
#endif				/* _ASM_SN_PCI_PCIIO_PRIVATE_H */
