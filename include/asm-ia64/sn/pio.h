/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_PIO_H
#define _ASM_SN_PIO_H

#include <linux/types.h>
#include <asm/sn/sgi.h>
#include <asm/sn/iobus.h>

/*
 * pioaddr_t	- The kernel virtual address that a PIO can be done upon.
 *		  Should probably be (volatile void*) but EVEREST would do PIO
 *		  to long mostly, just cast for other sizes.
 */

typedef volatile ulong*	pioaddr_t;

/*
 * iopaddr_t	- the physical io space relative address (e.g. VME A16S 0x0800).
 * iosapce_t	- specifies the io address space to be mapped/accessed.
 * piomap_t	- the handle returned by pio_alloc() and used with all the pio
 *		  access functions.
 */


typedef struct piomap {
	uint		pio_bus;
	uint		pio_adap;
#ifdef IRIX
	iospace_t	pio_iospace;
#endif
	int		pio_flag;
	int		pio_reg;
	char		pio_name[7];	/* to identify the mapped device */
	struct piomap	*pio_next;	/* dlist to link active piomap's */
	struct piomap	*pio_prev;	/* for debug and error reporting */
#ifdef IRIX
	void		(*pio_errfunc)(); /* Pointer to an error function */
					  /* Used only for piomaps allocated
					   * in user level vme driver     */
#endif
	iopaddr_t	pio_iopmask;	/* valid iop address bit mask */
	iobush_t	pio_bushandle;	/* bus-level handle */
} piomap_t;


/* Macro to get/set PIO error function */
#define	pio_seterrf(p,f)	(p)->pio_errfunc = (f)
#define	pio_geterrf(p)		(p)->pio_errfunc


/*
 * pio_mapalloc() - allocates a handle that specifies a mapping from kernel
 *		    virtual to io space. The returned handle piomap is used
 *		    with the access functions to make sure that the mapping
 *		    to the iospace exists.
 * pio_mapfree()  - frees the mapping as specified in the piomap handle.
 * pio_mapaddr()  - returns the kv address that maps to piomap'ed io address.
 */
#ifdef IRIX
extern piomap_t	*pio_mapalloc(uint,uint,iospace_t*,int,char*);
extern void	 pio_mapfree(piomap_t*);
extern caddr_t	 pio_mapaddr(piomap_t*,iopaddr_t);
extern piomap_t *pio_ioaddr(int, iobush_t, iopaddr_t, piomap_t *);

/*
 * PIO access functions.
 */
extern int  pio_badaddr(piomap_t*,iopaddr_t,int);
extern int  pio_badaddr_val(piomap_t*,iopaddr_t,int,void*);
extern int  pio_wbadaddr(piomap_t*,iopaddr_t,int);
extern int  pio_wbadaddr_val(piomap_t*,iopaddr_t,int,int);
extern int  pio_bcopyin(piomap_t*,iopaddr_t,void *,int, int, int);
extern int  pio_bcopyout(piomap_t*,iopaddr_t,void *,int, int, int);


/*
 * PIO RMW functions using piomap.
 */
extern void pio_orb_rmw(piomap_t*, iopaddr_t, unsigned char);
extern void pio_orh_rmw(piomap_t*, iopaddr_t, unsigned short);
extern void pio_orw_rmw(piomap_t*, iopaddr_t, unsigned long);
extern void pio_andb_rmw(piomap_t*, iopaddr_t, unsigned char);
extern void pio_andh_rmw(piomap_t*, iopaddr_t, unsigned short); 
extern void pio_andw_rmw(piomap_t*, iopaddr_t, unsigned long); 


/*
 * Old RMW function interface
 */
extern void orb_rmw(volatile void*, unsigned int);
extern void orh_rmw(volatile void*, unsigned int);
extern void orw_rmw(volatile void*, unsigned int);
extern void andb_rmw(volatile void*, unsigned int);
extern void andh_rmw(volatile void*, unsigned int);
extern void andw_rmw(volatile void*, unsigned int);
#endif	/* IRIX */


/*
 * piomap_t type defines
 */

#define PIOMAP_NTYPES	7

#define PIOMAP_A16N	VME_A16NP
#define PIOMAP_A16S	VME_A16S
#define PIOMAP_A24N	VME_A24NP
#define PIOMAP_A24S	VME_A24S
#define PIOMAP_A32N	VME_A32NP
#define PIOMAP_A32S	VME_A32S
#define PIOMAP_A64	6

#define PIOMAP_EISA_IO	0
#define PIOMAP_EISA_MEM	1

#define PIOMAP_PCI_IO	0
#define PIOMAP_PCI_MEM	1
#define PIOMAP_PCI_CFG	2
#define PIOMAP_PCI_ID	3

/* IBUS piomap types */
#define PIOMAP_FCI	0

/* dang gio piomap types */

#define	PIOMAP_GIO32	0
#define	PIOMAP_GIO64	1

#define ET_MEM         	0
#define ET_IO          	1
#define LAN_RAM         2
#define LAN_IO          3

#define PIOREG_NULL	-1

/* standard flags values for pio_map routines,
 * including {xtalk,pciio}_piomap calls.
 * NOTE: try to keep these in step with DMAMAP flags.
 */
#define PIOMAP_UNFIXED	0x0
#define PIOMAP_FIXED	0x1
#define PIOMAP_NOSLEEP	0x2
#define	PIOMAP_INPLACE	0x4

#define	PIOMAP_FLAGS	0x7

#endif	/* _ASM_SN_PIO_H */
