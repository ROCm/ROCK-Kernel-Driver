/*
 * BK Id: SCCS/s.walnut.h 1.10 09/14/01 17:37:56 trini
 */
/*
 *
 *    Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *
 *    Copyright 2000 MontaVista Software Inc.
 *	PPC405 modifications
 * 	Author: MontaVista Software, Inc.
 *         	frank_rowand@mvista.com or source@mvista.com
 * 	   	debbie_chu@mvista.com
 *
 *    Module name: ppc405.h
 *
 *    Description:
 *      Macros, definitions, and data structures specific to the IBM PowerPC
 *      based boards.
 *
 *      This includes:
 *
 *         405GP "Walnut" evaluation board
 *
 */

#ifdef __KERNEL__
#ifndef	__WALNUT_H__
#define	__WALNUT_H__

#ifndef __ASSEMBLY__
/*
 * Data structure defining board information maintained by the boot
 * ROM on IBM's "Walnut" evaluation board. An effort has been made to
 * keep the field names consistent with the 8xx 'bd_t' board info
 * structures.
 */

typedef struct board_info {
	unsigned char	 bi_s_version[4];	/* Version of this structure */
	unsigned char	 bi_r_version[30];	/* Version of the IBM ROM */
	unsigned int	 bi_memsize;		/* DRAM installed, in bytes */
	unsigned char	 bi_enetaddr[6];	/* Local Ethernet MAC address */
	unsigned char	 bi_pci_enetaddr[6];	/* PCI Ethernet MAC address */
	unsigned int	 bi_procfreq;		/* Processor speed, in Hz */
	unsigned int	 bi_plb_busfreq;	/* PLB Bus speed, in Hz */
	unsigned int	 bi_pci_busfreq;	/* PCI Bus speed, in Hz */
} bd_t;

#endif /* !__ASSEMBLY__ */

/* Memory map for the IBM "Walnut" 405GP evaluation board.
 * Generic 4xx plus RTC.
 */
#define WALNUT_RTC_ADDR		((uint)0xf0001000)
#define WALNUT_RTC_SIZE		((uint)4*1024)

#endif /* __WALNUT_H__ */
#endif /* __KERNEL__ */
