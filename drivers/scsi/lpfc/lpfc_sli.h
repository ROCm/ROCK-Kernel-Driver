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
 * $Id: lpfc_sli.h 1.33 2004/09/29 19:53:31EDT sf_support Exp  $
 */

#ifndef _H_LPFC_SLI
#define _H_LPFC_SLI

#include "lpfc_hw.h"

/* forward declaration for LPFC_IOCB_t's use */
struct lpfc_hba;

/* This structure is used to handle IOCB requests / responses */
struct lpfc_iocbq {
	/* lpfc_iocbqs are used in double linked lists */
	struct list_head list;
	IOCB_t iocb;		/* IOCB cmd */
	uint8_t retry;		/* retry counter for IOCB cmd - if needed */
	uint8_t iocb_flag;
#define LPFC_IO_POLL	1	/* Polling mode iocb */
#define LPFC_IO_LIBDFC	2	/* libdfc iocb */
#define LPFC_IO_WAIT	4
#define LPFC_IO_HIPRI	8	/* High Priority Queue signal flag */

	uint8_t abort_count;
	uint8_t rsvd2;
	uint32_t drvrTimeout;	/* driver timeout in seconds */
	void *context1;		/* caller context information */
	void *context2;		/* caller context information */
	void *context3;		/* caller context information */
	union {
		wait_queue_head_t *hipri_wait_queue; /* High Priority Queue wait
							queue */
		struct lpfc_iocbq  *rsp_iocb;
		struct lpfcMboxq   *mbox;
	} context_un;

	void (*iocb_cmpl) (struct lpfc_hba *, struct lpfc_iocbq *,
			   struct lpfc_iocbq *);

};

#define SLI_IOCB_RET_IOCB      1	/* Return IOCB if cmd ring full */
#define SLI_IOCB_HIGH_PRIORITY 2	/* High priority command */

#define IOCB_SUCCESS        0
#define IOCB_BUSY           1
#define IOCB_ERROR          2
#define IOCB_TIMEDOUT       3

typedef struct lpfcMboxq {
	/* MBOXQs are used in single linked lists */
	struct list_head list;	/* ptr to next mailbox command */
	MAILBOX_t mb;		/* Mailbox cmd */
	void *context1;		/* caller context information */
	void *context2;		/* caller context information */

	void (*mbox_cmpl) (struct lpfc_hba *, struct lpfcMboxq *);

} LPFC_MBOXQ_t;

#define MBX_POLL        1	/* poll mailbox till command done, then
				   return */
#define MBX_NOWAIT      2	/* issue command then return immediately */
#define MBX_STOP_IOCB   4	/* Stop iocb processing till mbox cmds
				   complete */

#define LPFC_MAX_RING_MASK  4	/* max num of rctl/type masks allowed per
				   ring */
#define LPFC_MAX_RING       4	/* max num of SLI rings used by driver */

/* Structure used to hold SLI ring information */
struct lpfc_sli_ring {
	uint16_t flag;		/* ring flags */
#define LPFC_DEFERRED_RING_EVENT 0x001	/* Deferred processing a ring event */
#define LPFC_CALL_RING_AVAILABLE 0x002	/* indicates cmd was full */
#define LPFC_STOP_IOCB_MBX       0x010	/* Stop processing IOCB cmds mbox */
#define LPFC_STOP_IOCB_EVENT     0x020	/* Stop processing IOCB cmds event */
#define LPFC_STOP_IOCB_MASK      0x030	/* Stop processing IOCB cmds mask */
	uint16_t abtsiotag;	/* tracks next iotag to use for ABTS */

	uint8_t rsvd;
	uint8_t ringno;		/* ring number */
	uint8_t rspidx;		/* current index in response ring */
	uint8_t cmdidx;		/* current index in command ring */
	struct lpfc_iocbq *fast_lookup;	/* array of IOCB ptrs indexed by
					   iotag */
	struct list_head txq;
	uint16_t txq_cnt;	/* current length of queue */
	uint16_t txq_max;	/* max length */
	struct list_head txcmplq;
	uint16_t txcmplq_cnt;	/* current length of queue */
	uint16_t txcmplq_max;	/* max length */
	volatile uint32_t *cmdringaddr;	/* virtual address for cmd rings */
	volatile uint32_t *rspringaddr;	/* virtual address for rsp rings */
	uint32_t missbufcnt;	/* keep track of buffers to post */
	struct list_head postbufq;
	uint16_t postbufq_cnt;	/* current length of queue */
	uint16_t postbufq_max;	/* max length */
	struct list_head iocb_continueq;
	uint16_t iocb_continueq_cnt;	/* current length of queue */
	uint16_t iocb_continueq_max;	/* max length */
};

typedef struct {
	uint8_t profile;	/* profile associated with ring */
	uint8_t rctl;	/* rctl / type pair configured for ring */
	uint8_t type;	/* rctl / type pair configured for ring */
	uint8_t rsvd;
	/* rcv'd unsol event */
	void (*lpfc_sli_rcv_unsol_event) (struct lpfc_hba *,
					 struct lpfc_sli_ring *,
					 struct lpfc_iocbq *);
} LPFC_RING_MASK_t;

/* Structure used for configuring rings to a specific profile or rctl / type */
typedef struct {
	LPFC_RING_MASK_t prt[LPFC_MAX_RING_MASK];
	uint32_t num_mask;	/* number of mask entries in prt array */
	uint32_t iotag_ctr;	/* keeps track of the next iotag to use */
	uint32_t iotag_max;	/* keeps track of the next iotag to use */
	uint32_t fast_iotag;	/* fast lookup based on iotag */
	uint16_t numCiocb;	/* number of command iocb's per ring */
	uint16_t numRiocb;	/* number of rsp iocb's per ring */
	/* cmd ring available */
	void (*lpfc_sli_cmd_available) (struct lpfc_hba *,
					struct lpfc_sli_ring *);
} LPFC_RING_INIT_t;

typedef struct {
	LPFC_RING_INIT_t ringinit[LPFC_MAX_RING]; /* ring initialization info */
	uint32_t num_rings;
	uint32_t sli_flag;
} LPFC_SLI_INIT_t;

/* Structure used to hold SLI statistical counters and info */
typedef struct {
	uint64_t iocbEvent[LPFC_MAX_RING];	/* IOCB event counters */
	uint64_t iocbCmd[LPFC_MAX_RING];	/* IOCB cmd issued */
	uint64_t iocbRsp[LPFC_MAX_RING];	/* IOCB rsp received */
	uint64_t iocbCmdDelay[LPFC_MAX_RING];	/* IOCB cmd ring delay */
	uint64_t iocbCmdFull[LPFC_MAX_RING];	/* IOCB cmd ring full */
	uint64_t iocbCmdEmpty[LPFC_MAX_RING];	/* IOCB cmd ring is now empty */
	uint64_t iocbRspFull[LPFC_MAX_RING];	/* IOCB rsp ring full */
	uint64_t mboxStatErr;	/* Mbox cmds completed status error */
	uint64_t mboxCmd;	/* Mailbox commands issued */
	uint64_t sliIntr;	/* Count of Host Attention interrupts */
	uint32_t errAttnEvent;	/* Error Attn event counters */
	uint32_t linkEvent;	/* Link event counters */
	uint32_t mboxEvent;	/* Mailbox event counters */
	uint32_t mboxBusy;	/* Mailbox cmd busy */
} LPFC_SLI_STAT_t;

/* Structure used to hold SLI information */
struct lpfc_sli {
	LPFC_SLI_INIT_t sliinit;	/* initialization info */
	/* Additional sli_flags */
#define LPFC_SLI_MBOX_ACTIVE      0x100	/* HBA mailbox is currently active */
#define LPFC_SLI2_ACTIVE          0x200	/* SLI2 overlay in firmware is active */
#define LPFC_PROCESS_LA           0x400	/* Able to process link attention */

	struct lpfc_sli_ring ring[LPFC_MAX_RING];
	int fcp_ring;		/* ring used for FCP initiator commands */
	int next_ring;

	int ip_ring;		/* ring used for IP network drv cmds */

	LPFC_SLI_STAT_t slistat;	/* SLI statistical info */
	struct list_head mboxq;
	uint16_t mboxq_cnt;	/* current length of queue */
	uint16_t mboxq_max;	/* max length */
	LPFC_MBOXQ_t *mbox_active;	/* active mboxq information */

	struct timer_list mbox_tmo;	/* Hold clk to timeout active mbox
					   cmd */

	volatile uint32_t *MBhostaddr;	/* virtual address for mbox cmds */
};

/* Given a pointer to the start of the ring, and the slot number of
 * the desired iocb entry, calc a pointer to that entry.
 * (assume iocb entry size is 32 bytes, or 8 words)
 */
#define IOCB_ENTRY(ring,slot) ((IOCB_t *)(((char *)(ring)) + ((slot) * 32)))

#define LPFC_SLI_ABORT_IMED	0	/* Immediate abort of IOCB, deque and
					   call compl routine immediately. */
#define LPFC_MBOX_TMO           30	/* Sec tmo for outstanding mbox
					   command */

#endif				/* _H_LPFC_SLI */
