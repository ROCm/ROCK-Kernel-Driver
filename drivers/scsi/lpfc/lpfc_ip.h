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

#ifndef  _H_LPFC_IP
#define _H_LPFC_IP

#include  "lpfc_hw.h"
#include  "lpfc_disc.h"

#define  LPFC_IP_TOV 32

/* Defines Structures used to support IP profile */
typedef struct fc_networkhdr {
	NAME_TYPE fc_destname;	/* destination port name */
	NAME_TYPE fc_srcname;	/* source port name */
} LPFC_NETHDR_t;

#define FC_MIN_MTU      0	/* minimum size FC message */
#define FC_MAX_MTU      65280	/* maximum size FC message */
#ifndef FC_MAC_ADDRLEN
#define FC_MAC_ADDRLEN  6
#endif

typedef enum {
	FLUSH_NODE,
	FLUSH_RING,
	FLUSH_XRI
} LPFC_IP_FLUSH_EVENT;

/* structure for MAC header */
typedef struct {
	uint8_t dest_addr[FC_MAC_ADDRLEN];	/* 48 bit unique address */
	uint8_t src_addr[FC_MAC_ADDRLEN];	/* 48 bit unique address */
	uint16_t llc_len;	/* length of LLC data */
} LPFC_EMAC_t;

#define HDR_LEN         14	/* MAC header size */

/* structure for LLC/SNAP header */
typedef struct {
	uint8_t dsap;		/* DSAP                         */
	uint8_t ssap;		/* SSAP                         */
	uint8_t ctrl;		/* control field                */
	uint8_t prot_id[3];	/* protocol id                  */
	uint16_t type;		/* type field                   */
} LPFC_SNAPHDR_t;

typedef struct lpfc_hdr {
	LPFC_EMAC_t mac;
	LPFC_SNAPHDR_t llc;
} LPFC_EMACHDR_t;

typedef struct lpfc_iphdr {
	LPFC_NETHDR_t fcnet;
	LPFC_SNAPHDR_t llc;
} LPFC_IPHDR_t;

#define FC_LLC_SSAP             0xaa	/* specifies LLC SNAP header */
#define FC_LLC_DSAP             0xaa	/* specifies LLC SNAP header */
#define FC_LLC_CTRL             3	/* UI */

/*
 * The lpfc_ip_buf structure is used to communicate IP commands
 * to the ip transport module.
 */
struct lpfc_ip_buf {
	struct lpfc_ip_buf *ip_buf_next;
	uint32_t timeout;	/* IN */
	elxHBA_t *ip_hba;	/* IN */

	/* Pointer to OS-specific area of lpfc_ip_buf. It could just be
	 * the cmd OS pass to us or another structure if we have other
	 * OS-specific info we want to maintain. 
	 */
	void *pOSCmd;		/* IN */

	uint32_t status;	/* From IOCB Word 7- ulpStatus */
	uint32_t result;	/* From IOCB Word 4. */
	uint32_t totalSize;	/* total size of packet */

	LPFC_NODELIST_t *ndlp;	/* ptr to NPort I/O is destined for */

	/* Dma_ext has both virt, phys to dma-able buffer
	 * which contains FCNET header and scatter gather list for
	 * a maximum of 80 (LPFC_IP_BPL_SIZE) BDE entries,
	 */
	DMABUF_t *dma_ext;
	LPFC_IPHDR_t *net_hdr;
	ULP_BDE64 *ip_bpl;

	 ELX_TQS_LINK(elx_phys_net_map) elx_phys_net_map_list;
	ELX_PHYS_NET_MAP_t elx_phys_net_map;

	/* Cur_iocbq has phys of the dma-able buffer.
	 * Iotag is in here 
	 */
	ELX_IOCBQ_t cur_iocbq;
};

typedef struct lpfc_ip_buf LPFC_IP_BUF_t;

#define LPFC_IP_INITIAL_BPL_SIZE  80	/* Number of ip buf BDEs in ip_bpl */
#define LPFC_IP_RCV_BUF_SIZE      4096	/* IP rcv buffer size */

#endif				/* _H_LPFC_IP */
