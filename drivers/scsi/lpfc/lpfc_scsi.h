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

/*
 * $Id: lpfc_scsi.h 1.66 2004/10/18 17:54:40EDT sf_support Exp  $
 */

#ifndef _H_LPFC_SCSI
#define _H_LPFC_SCSI

#include "lpfc_disc.h"
#include "lpfc_mem.h"
#include "lpfc_sli.h"

struct lpfc_hba;


struct lpfc_target {
	struct lpfc_nodelist *pnode;	/* Pointer to the node structure. */
	uint16_t  scsi_id;
	uint32_t  qcmdcnt;
	uint32_t  iodonecnt;
	uint32_t  errorcnt;
#if defined(FC_TRANS_VER1) || defined(FC_TRANS_265_BLKPATCH)
	uint16_t  blocked;
#endif
#if defined(FC_TRANS_265_BLKPATCH)
	struct timer_list dev_loss_timer;
#endif
};

struct lpfc_scsi_buf {
	struct scsi_cmnd *pCmd;
	struct lpfc_hba *scsi_hba;
	struct lpfc_target *target;

	uint32_t timeout;

	uint16_t status;	/* From IOCB Word 7- ulpStatus */
	uint32_t result;	/* From IOCB Word 4. */

	uint32_t   seg_cnt;	/* Number of scatter-gather segments returned by
				 * dma_map_sg.  The driver needs this for calls
				 * to dma_unmap_sg. */
	dma_addr_t nonsg_phys;	/* Non scatter-gather physical address. */

	/* dma_ext has both virt, phys to dma-able buffer
	 * which contains fcp_cmd, fcp_rsp and scatter gather list fro upto
	 * 68 (LPFC_SCSI_BPL_SIZE) BDE entries,
	 * xfer length, cdb, data direction....
	 */
	struct lpfc_dmabuf dma_ext;
	struct fcp_cmnd *fcp_cmnd;
	struct fcp_rsp *fcp_rsp;
	struct ulp_bde64 *fcp_bpl;

	/* cur_iocbq has phys of the dma-able buffer.
	 * Iotag is in here
	 */
	struct lpfc_iocbq cur_iocbq;
};

#define LPFC_SCSI_INITIAL_BPL_SIZE  4	/* Number of scsi buf BDEs in fcp_bpl */

#define LPFC_SCSI_DMA_EXT_SIZE 264
#define LPFC_BPL_SIZE          1024

#define MDAC_DIRECT_CMD                  0x22

#endif				/* _H_LPFC_SCSI */
