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

#ifndef _H_ELX_SLI
#define _H_ELX_SLI

#define IOCB_CMD_WSIZE  8	/* Length, in words, of a IOCB command */
#define MBOX_CMD_WSIZE  32	/* max length, in words, of a mailbox command */

/* forward declaration for ELX_IOCB_t's use */
struct elxHBA;
struct elxclock;

/* This structure is used to handle IOCB requests / responses */
typedef struct elxIocbq {
	/* IOCBQs are used in double linked lists */
	struct elxIocbq *q_f;	/* ptr to next iocb entry */
	struct elxIocbq *q_b;	/* ptr to previous iocb entry */
	IOCB_t iocb;		/* IOCB cmd */
	uint8_t retry;		/* retry counter for IOCB cmd - if needed */
	uint8_t iocb_flag;
#define ELX_IO_POLL	1	/* Polling mode iocb */
#define ELX_IO_IOCTL	2	/* IOCTL iocb */
#define ELX_IO_WAIT	4

	uint8_t abort_count;
	uint8_t rsvd2;
	uint32_t drvrTimeout;	/* driver timeout in seconds */
	void *context1;		/* caller context information */
	void *context2;		/* caller context information */
	void *context3;		/* caller context information */

	void (*iocb_cmpl) (struct elxHBA *, struct elxIocbq *,
			   struct elxIocbq *);

} ELX_IOCBQ_t;

#define SLI_IOCB_USE_TXQ       1	/* Queue IOCB to txq if cmd ring full */
#define SLI_IOCB_RET_IOCB      2	/* Return IOCB if cmd ring full */
#define SLI_IOCB_POLL          4	/* poll for completion */
#define SLI_IOCB_HIGH_PRIORITY 8	/* High priority command */

#define IOCB_SUCCESS        0
#define IOCB_BUSY           1
#define IOCB_ERROR          2
#define IOCB_TIMEDOUT       3

typedef struct elxMboxq {
	/* MBOXQs are used in single linked lists */
	struct elxMboxq *q_f;	/* ptr to next mailbox command */
	MAILBOX_t mb;		/* Mailbox cmd */
	void *context1;		/* caller context information */
	void *context2;		/* caller context information */

	void (*mbox_cmpl) (struct elxHBA *, struct elxMboxq *);

} ELX_MBOXQ_t;

#define MBX_POLL        1	/* poll mailbox till command done, then return */
#define MBX_NOWAIT      2	/* issue command then return immediately */
#define MBX_STOP_IOCB   4	/* Stop iocb processing till mbox cmds complete */

#define ELX_MAX_RING_MASK  4	/* max num of rctl/type masks allowed per ring */
#define ELX_MAX_RING       4	/* max num of SLI rings used by driver */

/* Structure used to hold SLI ring information */
typedef struct {
	uint16_t flag;		/* ring flags */
#define ELX_DEFERRED_RING_EVENT 0x001	/* Deferred processing a ring event */
#define ELX_CALL_RING_AVAILABLE 0x002	/* indicates cmd was full */
#define ELX_STOP_IOCB_MBX       0x010	/* Stop processing IOCB cmds mbox */
#define ELX_STOP_IOCB_EVENT     0x020	/* Stop processing IOCB cmds event */
#define ELX_STOP_IOCB_MASK      0x030	/* Stop processing IOCB cmds mask */
	uint16_t abtsiotag;	/* tracks next iotag to use for ABTS */

	uint8_t rsvd;
	uint8_t ringno;		/* ring number */
	uint8_t rspidx;		/* current index in response ring */
	uint8_t cmdidx;		/* current index in command ring */
	ELX_IOCBQ_t *fast_lookup;	/* array of IOCB ptrs indexed by iotag */
	ELX_DLINK_t txq;	/* iocb command queue */
	ELX_DLINK_t txcmplq;	/* iocb pending queue */
	volatile uint32_t *cmdringaddr;	/* virtual address for cmd rings */
	volatile uint32_t *rspringaddr;	/* virtual address for rsp rings */
	uint32_t missbufcnt;	/* keep track of buffers to post */
	ELX_SLINK_t postbufq;	/* posted buffer list */
	ELX_SLINK_t iocb_continueq;	/* track IOCB continues between interrupts */
} ELX_SLI_RING_t;

/* Structure used for configuring rings to a specific profile or rctl / type */
typedef struct {
	struct {
		uint8_t profile;	/* profile associated with ring */
		uint8_t rctl;	/* rctl / type pair configured for ring */
		uint8_t type;	/* rctl / type pair configured for ring */
		uint8_t rsvd;
		/* rcv'd unsol event */
		void (*elx_sli_rcv_unsol_event) (struct elxHBA *,
						 ELX_SLI_RING_t *,
						 ELX_IOCBQ_t *);
	} prt[ELX_MAX_RING_MASK];
	uint32_t num_mask;	/* number of mask entries in prt array */
	uint32_t iotag_ctr;	/* keeps track of the next iotag to use */
	uint32_t iotag_max;	/* keeps track of the next iotag to use */
	uint32_t fast_iotag;	/* fast lookup based on iotag */
	uint16_t numCiocb;	/* number of command iocb's per ring */
	uint16_t numRiocb;	/* number of rsp iocb's per ring */
	/* cmd ring available */
	void (*elx_sli_cmd_available) (struct elxHBA *, ELX_SLI_RING_t *);
} ELX_RING_INIT_t;

typedef struct {
	ELX_RING_INIT_t ringinit[ELX_MAX_RING];	/* ring initialization info */

	/* HBA initialization callback routines */
	int (*elx_sli_config_port_prep) (struct elxHBA *);
	int (*elx_sli_config_port_post) (struct elxHBA *);
	uint32_t *(*elx_sli_config_pcb_setup) (struct elxHBA *);
	int (*elx_sli_hba_down_prep) (struct elxHBA *);

	/* HBA interrupt callback routines */
	 uint32_t(*elx_sli_intr_prep) (struct elxHBA *);
	void (*elx_sli_handle_eratt) (struct elxHBA *, uint32_t);
	void (*elx_sli_handle_latt) (struct elxHBA *);
	void (*elx_sli_intr_post) (struct elxHBA *);
	 uint32_t(*elx_sli_register_intr) (struct elxHBA *);
	void (*elx_sli_unregister_intr) (struct elxHBA *);

	/* HBA SLIM / Register access routines */
	 uint32_t(*elx_sli_read_HA) (struct elxHBA *);
	 uint32_t(*elx_sli_read_CA) (struct elxHBA *);
	 uint32_t(*elx_sli_read_HS) (struct elxHBA *);
	 uint32_t(*elx_sli_read_HC) (struct elxHBA *);
	void (*elx_sli_write_HA) (struct elxHBA *, uint32_t);
	void (*elx_sli_write_CA) (struct elxHBA *, uint32_t);
	void (*elx_sli_write_HS) (struct elxHBA *, uint32_t);
	void (*elx_sli_write_HC) (struct elxHBA *, uint32_t);
	void (*elx_sli_write_titan_HS) (struct elxHBA *, uint32_t);
	void (*elx_sli_read_slim) (struct elxHBA *, uint8_t *, int, int);
	void (*elx_sli_write_slim) (struct elxHBA *, uint8_t *, int, int);
	 uint32_t(*elx_sli_read_pci) (struct elxHBA *, int);
	 uint16_t(*elx_sli_read_pci_cmd) (struct elxHBA *);
	void (*elx_sli_write_pci_cmd) (struct elxHBA *, uint16_t);
	void (*elx_sli_setup_slim_access) (struct elxHBA *);

	/* General purpose callback routines */
	void (*elx_sli_brdreset) (struct elxHBA *);

	uint32_t num_rings;
	uint32_t sli_flag;
#define ELX_HGP_HOSTSLIM    1	/* Use Host Group pointers in HOST SLIM */
} ELX_SLI_INIT_t;

/* Structure used to hold SLI statistical counters and info */
typedef struct {
	uint64_t iocbEvent[ELX_MAX_RING];	/* IOCB event counters */
	uint64_t iocbCmd[ELX_MAX_RING];	/* IOCB cmd issued */
	uint64_t iocbRsp[ELX_MAX_RING];	/* IOCB rsp received */
	uint64_t iocbCmdDelay[ELX_MAX_RING];	/* IOCB cmd ring delay */
	uint64_t iocbCmdFull[ELX_MAX_RING];	/* IOCB cmd ring full */
	uint64_t iocbCmdEmpty[ELX_MAX_RING];	/* IOCB cmd ring is now empty */
	uint64_t iocbRspFull[ELX_MAX_RING];	/* IOCB rsp ring full */
	uint64_t mboxStatErr;	/* Mbox cmds completed status error */
	uint64_t mboxCmd;	/* Mailbox commands issued */
	uint64_t sliIntr;	/* Count of Host Attention interrupts */
	uint32_t errAttnEvent;	/* Error Attn event counters */
	uint32_t linkEvent;	/* Link event counters */
	uint32_t mboxEvent;	/* Mailbox event counters */
	uint32_t mboxBusy;	/* Mailbox cmd busy */
} ELX_SLI_STAT_t;

/* Structure used to hold SLI information */
typedef struct elx_sli {
	ELX_SLI_INIT_t sliinit;	/* initialization info */
	/* Additional sli_flags */
#define ELX_SLI_MBOX_ACTIVE      0x100	/* HBA mailbox is currently active */
#define ELX_SLI2_ACTIVE          0x200	/* SLI2 overlay in firmware is active */
#define ELX_PROCESS_LA           0x400	/* Able to process link attention */

	ELX_SLI_RING_t ring[ELX_MAX_RING];
	int fcp_ring;		/* ring used for FCP initiator commands */
	int next_ring;

	int ip_ring;		/* ring used for IP network drv cmds */

	ELX_SLI_STAT_t slistat;	/* SLI statistical info */
	ELX_SLINK_t mboxq;	/* mbox command queue */
	ELX_MBOXQ_t *mbox_active;	/* active mboxq information */
	struct elxclock *mbox_tmo;	/* Hold clk to timeout active mbox cmd */

	volatile uint32_t *MBhostaddr;	/* virtual address for mbox cmds */
} ELX_SLI_t;

/* Given a pointer to the start of the ring, and the slot number of
 * the desired iocb entry, calc a pointer to that entry.
 * (assume iocb entry size is 32 bytes, or 8 words)
 */
#define IOCB_ENTRY(ring,slot) ((uint32_t *)(((char *)(ring)) + \
                               (((uint32_t)(slot)) << 5)))

#define ELX_SLI_ABORT_WAIT	0	/* Wait for rsp ring completion of IOCBs */
#define ELX_SLI_ABORT_IMED	0	/* Imediate abort of IOCB, deque and call
					 * compl routine immediately.
					 */
#define ELX_MBOX_TMO           30	/* Sec tmo for outstanding mbox command */

#endif				/* _H_ELX_SLI */
