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

#ifndef _H_PROD_CRTN
#define _H_PROD_CRTN

#include <linux/delay.h>
#include <asm/uaccess.h>

#include "elx_sli.h"
#include "elx_logmsg.h"

/* Function prototypes. */

uint8_t *elx_malloc(elxHBA_t *, struct mbuf_info *);
void elx_free(elxHBA_t *, struct mbuf_info *);
int elx_print(char *, void *, void *);
void elx_sleep_ms(elxHBA_t *, int);
void elx_pci_dma_sync(void *, void *, int, int);
void *elx_kmem_alloc(unsigned int, int);
void elx_kmem_free(void *, unsigned int);
void *elx_kmem_zalloc(unsigned int, int);
void elx_sli_init_lock(elxHBA_t *);
void elx_sli_lock(elxHBA_t *, unsigned long *);
void elx_sli_unlock(elxHBA_t *, unsigned long *);
void elx_mem_init_lock(elxHBA_t *);
void elx_mem_lock(elxHBA_t *, unsigned long *);
void elx_mem_unlock(elxHBA_t *, unsigned long *);
void elx_sch_init_lock(elxHBA_t *);
void elx_sch_lock(elxHBA_t *, unsigned long *);
void elx_sch_unlock(elxHBA_t *, unsigned long *);
void elx_ioc_init_lock(elxHBA_t *);
void elx_ioc_lock(elxHBA_t *, unsigned long *);
void elx_ioc_unlock(elxHBA_t *, unsigned long *);
void elx_drvr_init_lock(elxHBA_t *);
void elx_drvr_lock(elxHBA_t *, unsigned long *);
void elx_drvr_unlock(elxHBA_t *, unsigned long *);
void elx_clk_init_lock(elxHBA_t *);
void elx_clk_lock(elxHBA_t *, unsigned long *);
void elx_clk_unlock(elxHBA_t *, unsigned long *);
void elx_disc_init_lock(elxHBA_t *);
void elx_disc_lock(elxHBA_t *, unsigned long *);
void elx_disc_unlock(elxHBA_t *, unsigned long *);
void elx_hipri_init_lock(elxHBA_t *);
void elx_hipri_lock(elxHBA_t *, unsigned long *);
void elx_hipri_unlock(elxHBA_t *, unsigned long *);
uint16_t elx_read_pci_cmd(elxHBA_t *);
uint32_t elx_read_pci(elxHBA_t *, int);
void elx_cnt_read_pci(elxHBA_t *, uint32_t, uint32_t, uint32_t *);
void elx_cnt_write_pci(elxHBA_t *, uint32_t, uint32_t, uint32_t *);
void elx_write_pci_cmd(elxHBA_t *, uint16_t);
void elx_write_cnt_pci_cmd(elxHBA_t *, int, uint16_t);
void *elx_remap_pci_mem(unsigned long, unsigned long);
void elx_unmap_pci_mem(unsigned long);
void elx_write_toio(uint32_t *, uint32_t *, uint32_t);
void elx_read_fromio(uint32_t *, uint32_t *, uint32_t);
int elx_in_intr(void);

void *elx_alloc_bigbuf(elxHBA_t *, elx_dma_addr_t *, uint32_t);
void elx_free_bigbuf(elxHBA_t *, void *, elx_dma_addr_t, uint32_t);
void elx_nodev(unsigned long);

void elx_ip_get_rcv_buf(elxHBA_t *, DMABUFIP_t *, uint32_t);
void elx_ip_free_rcv_buf(elxHBA_t *, DMABUFIP_t *, uint32_t);

void elx_iodone(elxHBA_t *, ELX_SCSI_BUF_t *);
int elx_scsi_delay_iodone(elxHBA_t *, ELX_SCSI_BUF_t *);
void elx_unblock_requests(elxHBA_t *);
void elx_block_requests(elxHBA_t *);
uint64_t elx_pci_map(elxHBA_t *, void *, int, int);
void myprint(char *, void *, void *, void *, void *);

void lpfc_set_pkt_len(void *, uint32_t);
void *lpfc_get_pkt_data(void *);

#endif				/* _H_PROD_CRTN */
