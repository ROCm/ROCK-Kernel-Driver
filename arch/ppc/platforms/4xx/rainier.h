/*
 *    Copyright 2002 MontaVista Software Inc.
 *	NP405GS modifications
 * 	Author: MontaVista Software, Inc.
 *         	Akuster@mvista.com or source@mvista.com
 *
 *    Module name: rainier.h
 *
 *    Description:
 *      Macros, definitions, and data structures specific to the IBM PowerPC
 *      Network processor based boards.
 *
 *      This includes:
 *
 *         NP405GS "Rainier" evaluation board
 *
 */

#ifdef __KERNEL__
#ifndef __ASM_RAINIER_H__
#define __ASM_RAINIER_H__

/* We have a N405GS core */
#include <platforms/ibmnp4gs.h>

#ifndef __ASSEMBLY__
/*
 * Data structure defining board information maintained 
 * manuals since the rainer uses vxworks
 */

typedef struct board_info {
	unsigned char bi_s_version[4];		/* Version of this structure */
	unsigned char bi_r_version[30];		/* Version of the IBM ROM */
	unsigned int bi_memsize;		/* DRAM installed, in bytes */
	unsigned char bi_enetaddr[6];		/* Local Ethernet MAC address */
	unsigned char bi_pci_enetaddr[6];	/* PCI Ethernet MAC address */
	unsigned int bi_intfreq;		/* Processor speed, in Hz */
	unsigned int bi_busfreq;		/* PLB Bus speed, in Hz */
	unsigned int bi_pci_busfreq;		/* PCI Bus speed, in Hz */
} bd_t;

#define bi_tbfreq bi_intfreq

extern void *rainer_rtc_base;
#define RAINIER_RTC_PADDR	((uint)0xf0000000)
#define RAINIER_RTC_VADDR	RAINIER_RTC_PADDR
#define RAINIER_RTC_SIZE	((uint)8*1024)

#define BASE_BAUD		115200

#define PPC4xx_MACHINE_NAME	"IBM Rainier"

#endif /* !__ASSEMBLY__ */
#endif /* __ASM_RAINIER_H__ */
#endif /* __KERNEL__ */
