/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Enterprise Fibre Channel Host Bus Adapters.                     *
 * Refer to the README file included with this package for         *
 * driver version and adapter support.                             *
 * Copyright (C) 2004 Emulex Corporation.                          *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of the GNU General Public License     *
 * as published by the Free Software Foundation; either version 2  *
 * of the License, or (at your option) any later version.          *
 *                                                                 *
 * This program is distributed in the hope that it will be useful, *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
 * GNU General Public License for more details, a copy of which    *
 * can be found in the file COPYING included with this package.    *
 *******************************************************************/

#ifndef _H_ELX_IOCTL
#define _H_ELX_IOCTL

#define _ELX_DFC_64BIT 1

#ifdef BITS_PER_LONG
#if BITS_PER_LONG < 64
#undef _ELX_DFC_64BIT
#endif
#endif

#ifdef i386
#undef _ELX_DFC_64BIT
#endif

#ifdef powerpc
#ifndef CONFIG_PPC64
#undef _ELX_DFC_64BIT
#endif
#endif

/* ELX Ioctls() 0x00 - 0x3F. Redefine later when macro IOCTL_WORD is used */

/* ELX_FIRST_COMMAND_USED		0x00	    First defines Ioctl used  */
#define ELX_DISPLAY_PCI_ALL		0x00	/* Display configuration registers */
#define ELX_WRITE_PCI             	0x01	/* Write to PCI */
#define ELX_WRITE_HC             	0x02	/* Write to Host Control register */
#define ELX_WRITE_HS              	0x03	/* Write to Host Status register */
#define ELX_WRITE_HA              	0x04	/* Write to Host attention register */
#define ELX_WRITE_CA              	0x05	/* Write capacity of target */
#define ELX_READ_PCI              	0x06	/* Read from PCI */
#define ELX_READ_HC               	0x07	/* Read Host Control register */
#define ELX_READ_HS               	0x08	/* Read Host Status register */
#define ELX_READ_HA               	0x09	/* Read Host Attention register */
#define ELX_READ_CA               	0x0a	/* Read Capacity of target */
#define ELX_READ_MB               	0x0b	/* Read mailbox information */

/* ELX COMMAND POSITION 0xc available.  Used to be Read ring information. */

#define ELX_READ_MEM              	0x0d	/* Read memory */
#define ELX_READ_IOCB             	0x0e	/* Read IOCB information */

/* ELX COMMAND POSITION 0xf available. */

#define ELX_READ_MEMSEG           	0x11	/* Get  memory segment info */
#define ELX_MBOX                  	0x12	/* Issue a MB cmd */
#define ELX_RESET                 	0x13	/* Reset the adapter */
#define ELX_READ_HBA	           	0x14	/* Get adapter info */

/* ELX COMMAND POSITION 0x15 available.  Used to be Get NDD stats. */

#define ELX_WRITE_MEM             	0x16	/* Write to SLIM memory */
#define ELX_WRITE_CTLREG          	0x17	/* Write to Control register */
#define ELX_READ_CTLREG           	0x18	/* Read from Control control register */
#define ELX_INITBRDS              	0x19	/* Initialize the adapters */
#define ELX_SETDIAG              	0x1a	/* Set/get board online/offline */
#define ELX_INST			0x1b	/* get instance info */

#define ELX_DEVP                	0x1c	/* Get Device infotmation */

#define ELX_READ_BINFO			0x2c	/* Number of outstanding I/Os */
#define ELX_READ_BPLIST			0x2d	/* Number of outstanding I/Os */
#define ELX_INVAL			0x2e	/* Number of outstanding I/Os */
#define ELX_LINKINFO			0x2f	/* Number of outstanding I/Os */
#define ELX_IOINFO  			0x30	/* Number of outstanding I/Os */
#define ELX_NODEINFO  			0x31	/* Number of outstanding I/Os */
#define ELX_READ_LHBA	           	0x32	/* Get adapter info */
#define ELX_READ_LXHBA	           	0x33	/* Get adapter info */
#define ELX_SET		           	0x34	/* Select adapter  */
#define ELX_DBG		           	0x35	/* set dbg trace val */
#define ELX_ADD_BIND             	0x36	/* Add a new binding */
#define ELX_DEL_BIND             	0x37	/* Del a binding */
#define ELX_LIST_BIND                   0x38	/* List binding */
/*	ELX_LAST_IOCTL_USED		0x38	Last ELX Ioctl used  */

#define LPFC_DFC_IOCTL			0x01	/* For DFC invocation */

/*
Data structure definitions:

Macro converting  to word format to uniquesly identify and group the Ioctls.
*/

#define IOCTL_WORD(ioctl_type1, ioctl_type2, ioctl_val) ((ioctl_type1 << 24 | ioctl_type2 << 16) | ioctl_val)

/*
 * Dignostic (DFC) Command & Input structures: (LPFC)
 */

typedef struct elxCmdInput {	/* For 64-bit copy_in */
#ifdef _ELX_DFC_64BIT
	short elx_brd;
	short elx_ring;
	short elx_iocb;
	short elx_flag;
	void *elx_arg1;
	void *elx_arg2;
	void *elx_arg3;
	char *elx_dataout;
	uint32_t elx_cmd;
	uint32_t elx_outsz;
	uint32_t elx_arg4;
	uint32_t elx_arg5;
#else
	short elx_brd;
	short elx_ring;
	short elx_iocb;
	short elx_flag;
	void *elx_filler1;
	void *elx_arg1;
	void *elx_filler2;
	void *elx_arg2;
	void *elx_filler3;
	void *elx_arg3;
	void *elx_filler4;
	char *elx_dataout;
	uint32_t elx_cmd;
	uint32_t elx_outsz;
	uint32_t elx_arg4;
	uint32_t elx_arg5;
#endif				/* _ELX_DFC_64BIT */
} ELXCMDINPUT_t;

#endif				/*  _H_ELX_IOCTL */
