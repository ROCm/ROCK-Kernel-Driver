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

#ifndef _H_LPFC_HBA
#define _H_LPFC_HBA

#include "elx_clock.h"
#include "elx_util.h"
#include "elx_sli.h"
#include "elx_scsi.h"
#include "lpfc_disc.h"

/* Check if WWN is 0 */
#define isWWNzero(wwn) ((wwn.nameType == 0) && (wwn.IEEE[0] == 0) && (wwn.IEEE[1] == 0) && (wwn.IEEE[2] == 0) && (wwn.IEEE[3] == 0) && (wwn.IEEE[4] == 0) && (wwn.IEEE[5] == 0))

#define FC_MAX_HOLD_RSCN      32
#define FC_MAX_MCAST          16

#define MAX_HBAEVT 32

/* This should correspond with the HBA API event structure */
typedef struct hbaevt {
	uint32_t fc_eventcode;
	uint32_t fc_evdata1;
	uint32_t fc_evdata2;
	uint32_t fc_evdata3;
	uint32_t fc_evdata4;
} HBAEVT_t;

/*
 * lpfc stat counters
 */
struct lpfc_stats {
	/* Statistics for ELS commands */
	uint32_t elsLogiCol;
	uint32_t elsRetryExceeded;
	uint32_t elsXmitRetry;
	uint32_t elsDelayRetry;
	uint32_t elsRcvDrop;
	uint32_t elsRcvFrame;
	uint32_t elsRcvRSCN;
	uint32_t elsRcvRNID;
	uint32_t elsRcvFARP;
	uint32_t elsRcvFARPR;
	uint32_t elsRcvFLOGI;
	uint32_t elsRcvPLOGI;
	uint32_t elsRcvADISC;
	uint32_t elsRcvPDISC;
	uint32_t elsRcvFAN;
	uint32_t elsRcvLOGO;
	uint32_t elsRcvPRLO;
	uint32_t elsRcvPRLI;
	uint32_t elsRcvRRQ;
	uint32_t elsXmitFLOGI;
	uint32_t elsXmitPLOGI;
	uint32_t elsXmitPRLI;
	uint32_t elsXmitADISC;
	uint32_t elsXmitLOGO;
	uint32_t elsXmitSCR;
	uint32_t elsXmitRNID;
	uint32_t elsXmitFARP;
	uint32_t elsXmitFARPR;
	uint32_t elsXmitACC;
	uint32_t elsXmitLSRJT;

	uint32_t frameRcvBcast;
	uint32_t frameRcvMulti;
	uint32_t strayXmitCmpl;
	uint32_t frameXmitDelay;
	uint32_t xriCmdCmpl;
	uint32_t xriStatErr;
	uint32_t LinkUp;
	uint32_t LinkDown;
	uint32_t LinkMultiEvent;
	uint32_t NoRcvBuf;
	uint32_t fcpCmd;
	uint32_t fcpCmpl;
	uint32_t fcpRspErr;
	uint32_t fcpRemoteStop;
	uint32_t fcpPortRjt;
	uint32_t fcpPortBusy;
	uint32_t fcpError;
	uint32_t fcpLocalErr;
};
typedef struct lpfc_stats LPFC_STAT_t;

/*
 * IP stat counters
 */
struct lpip_stats {
	uint32_t lpfn_ipackets_lsw;	/* # packets received */
	uint32_t lpfn_ipackets_msw;
	uint32_t lpfn_ierrors;	/* # total input errors */
	uint32_t lpfn_opackets_lsw;	/* # packets sent */
	uint32_t lpfn_opackets_msw;
	uint32_t lpfn_oerrors;	/* # total output errors */
	uint32_t lpfn_rx_dropped;	/* # dropped receive packets. */
	uint32_t lpfn_tx_dropped;	/* # dropped txmit packets.   */
	uint32_t lpfn_rcvbytes_lsw;	/* # bytes received */
	uint32_t lpfn_rcvbytes_msw;
	uint32_t lpfn_xmtbytes_lsw;	/* # bytes transmitted */
	uint32_t lpfn_xmtbytes_msw;
	uint32_t lpfn_multircv_lsw;	/* # multicast packets received */
	uint32_t lpfn_multircv_msw;
	uint32_t lpfn_multixmt_lsw;	/* # multicast packets for xmit */
	uint32_t lpfn_multixmt_msw;
	uint32_t lpfn_brdcstrcv_lsw;	/* # broadcast packets received */
	uint32_t lpfn_brdcstrcv_msw;
	uint32_t lpfn_brdcstxmt_lsw;	/* # broadcast packets for xmit */
	uint32_t lpfn_brdcstxmt_msw;
	uint32_t lpfn_Ucstxmt_lsw;	/* # Unicast packets for xmit */
	uint32_t lpfn_Ucstxmt_msw;
	uint32_t lpfn_xmitintr_lsw;	/* number of transmit interrupts(lsw) */
	uint32_t lpfn_xmitintr_msw;	/* number of transmit interrupts(msw) */
	uint32_t lpfn_recvintr_lsw;	/* number of receive interrupts(lsw) */
	uint32_t lpfn_recvintr_msw;	/* number of receive interrupts(msw) */
	uint32_t lpfn_NoRcvBuf;
	uint32_t lpfn_xmitque_cur;
};

typedef struct lpip_stats LPIP_STAT_t;

/*++
 * lpfc_node_farp_list: 
 *   This data structure defines the attributes associated with
 *   an outstanding FARP REQ to a remote node.
 *
 *   rnode_addr - The address of the remote node.  Either the IEEE, WWPN, or WWNN.
 *                Used in the FARP request.
 *   fc_ipfarp_tmo - The timer associated with the FARP request.  This
 *                   timer limits the amount of time spent waiting for
 *                   the FARP to complete.
 *   fc_ipbuf_list_farp_wait - A list of IP buffers waiting for the FARP
 *                             request to complete.
 *
 --*/
struct lpfc_node_farp_pend {
	struct lpfc_node_farp_pend *pnext;
	NAME_TYPE rnode_addr;
	ELXCLOCK_t *fc_ipfarp_tmo;
	 ELX_TQS_LINK(lpfc_ip_buf) fc_ipbuf_list_farp_wait;
};

typedef struct lpfc_node_farp_pend LPFC_NODE_FARP_PEND_t;

typedef struct lpfcHba {
	uint8_t fc_linkspeed;	/* Link speed after last READ_LA */
	uint8_t fc_max_data_rate;	/* max_data_rate                 */

	uint8_t fc_process_LA;	/* flag to process Link Attention */
	uint32_t fc_eventTag;	/* event tag for link attention */
	uint32_t fc_prli_sent;	/* cntr for outstanding PRLIs */

	LPIP_STAT_t *ip_stat;
	uint8_t phys_addr[8];	/* actual network address in use */

	uint32_t disc_state;	/*in addition to hba_state */
	uint32_t num_disc_nodes;	/*in addition to hba_state */

	uint8_t fcp_mapping;	/* Map FCP devices based on WWNN WWPN or DID */
#define FCP_SEED_WWNN   0x1
#define FCP_SEED_WWPN   0x2
#define FCP_SEED_DID    0x4
#define FCP_SEED_MASK   0x7
#define FCP_SEED_AUTO   0x8	/* binding was created by auto mapping */

	uint32_t power_up;
	ELXCLOCK_t *fc_estabtmo;	/* link establishment timer */
	ELXCLOCK_t *fc_disctmo;	/* Discovery rescue timer */
	ELXCLOCK_t *fc_linkdown;	/* link down timer */
	ELXCLOCK_t *fc_fdmitmo;	/* fdmi timer */

	void *lpfn_dev;
	int lpfn_max_mtu;
	int lpfn_rcv_buf_size;
	void (*lpfn_ip_rcv) (struct elxHBA *, void *, uint32_t);

	void *fc_evt_head;	/* waiting for event queue */
	void *fc_evt_tail;	/* waiting for event queue */

	struct buf *timeout_head;	/* bufs to iodone after RLIP done */

	uint16_t timeout_count;
	uint16_t init_eventTag;	/* initial READ_LA eventtag from cfg */
	uint16_t hba_event_put;	/* hbaevent event put word anchor */
	uint16_t hba_event_get;	/* hbaevent event get word anchor */
	uint32_t hba_event_missed;	/* hbaevent missed event word anchor */
	uint8_t pan_cnt;	/* pseudo adapter number counter */
	uint16_t sid_cnt;	/* SCSI ID counter */

	HBAEVT_t hbaevt[MAX_HBAEVT];

#define FC_CPQ_LUNMAP   0x1	/* SCSI passthru interface LUN 0 mapping */

	/* These fields used to be binfo */
	NAME_TYPE fc_nodename;	/* fc nodename */
	NAME_TYPE fc_portname;	/* fc portname */
	uint32_t fc_pref_DID;	/* preferred D_ID */
	uint8_t fc_pref_ALPA;	/* preferred AL_PA */
	uint8_t fc_deferip;	/* defer IP processing */
	uint8_t ipAddr[16];	/* For RNID support */
	uint16_t ipVersion;	/* For RNID support */
	uint16_t UDPport;	/* For RNID support */
	uint32_t fc_edtov;	/* E_D_TOV timer value */
	uint32_t fc_arbtov;	/* ARB_TOV timer value */
	uint32_t fc_ratov;	/* R_A_TOV timer value */
	uint32_t fc_rttov;	/* R_T_TOV timer value */
	uint32_t fc_altov;	/* AL_TOV timer value */
	uint32_t fc_crtov;	/* C_R_TOV timer value */
	uint32_t fc_citov;	/* C_I_TOV timer value */
	uint32_t fc_myDID;	/* fibre channel S_ID */
	uint32_t fc_prevDID;	/* previous fibre channel S_ID */

	SERV_PARM fc_sparam;	/* buffer for our service parameters */
	SERV_PARM fc_fabparam;	/* fabric service parameters buffer */
	uint8_t alpa_map[128];	/* AL_PA map from READ_LA */

	uint8_t fc_ns_retry;	/* retries for fabric nameserver */
	uint32_t fc_nlp_cnt;	/* outstanding NODELIST requests */
	uint32_t fc_rscn_id_cnt;	/* count of RSCNs payloads in list */
	DMABUF_t *fc_rscn_id_list[FC_MAX_HOLD_RSCN];
	ELX_SLINK_t fc_plogi;	/* ELS PLOGI cmd queue */
	ELX_SLINK_t fc_rscn;	/* RSCN cmd queue */
	ELX_SLINK_t fc_defer_rscn;	/* deferred RSCN cmd queue */

	uint32_t fc_flag;	/* FC flags */
#define FC_FCP_WWNN             0x0	/* Match FCP targets on WWNN */
#define FC_FCP_WWPN             0x1	/* Match FCP targets on WWPN */
#define FC_FCP_DID              0x2	/* Match FCP targets on DID */
#define FC_FCP_MATCH            0x3	/* Mask for match FCP targets */
#define FC_PENDING_RING0        0x4	/* Defer ring 0 IOCB processing */
#define FC_LNK_DOWN             0x8	/* Link is down */
#define FC_PT2PT                0x10	/* pt2pt with no fabric */
#define FC_PT2PT_PLOGI          0x20	/* pt2pt initiate PLOGI */
#define FC_DELAY_DISC           0x40	/* Delay discovery till after cfglnk */
#define FC_PUBLIC_LOOP          0x80	/* Public loop */
#define FC_INTR_THREAD          0x100	/* In interrupt code */
#define FC_LBIT                 0x200	/* LOGIN bit in loopinit set */
#define FC_RSCN_MODE            0x400	/* RSCN cmd rcv'ed */
#define FC_RSCN_DISC_TMR        0x800	/* wait edtov before processing RSCN */
#define FC_NLP_MORE             0x1000	/* More node to process in node tbl */
#define FC_OFFLINE_MODE         0x2000	/* Interface is offline for diag */
#define FC_LD_TIMER             0x4000	/* Linkdown timer has been started */
#define FC_LD_TIMEOUT           0x8000	/* Linkdown timeout has occurred */
#define FC_FABRIC               0x10000	/* We are fabric attached */
#define FC_DELAY_PLOGI          0x20000	/* Delay login till unreglogin */
#define FC_SLI2                 0x40000	/* SLI-2 CONFIG_PORT cmd completed */
#define FC_INTR_WORK            0x80000	/* Was there work last intr */
#define FC_NO_ROOM_IP           0x100000	/* No room on IP xmit queue */
#define FC_NO_RCV_BUF           0x200000	/* No Rcv Buffers posted IP ring */
#define FC_BUS_RESET            0x400000	/* SCSI BUS RESET */
#define FC_ESTABLISH_LINK       0x800000	/* Reestablish Link */
#define FC_SCSI_RLIP            0x1000000	/* SCSI rlip routine called */
#define FC_DELAY_NSLOGI         0x2000000	/* Delay NameServer till ureglogin */
#define FC_NSLOGI_TMR           0x4000000	/* NameServer in process of logout */
#define FC_DELAY_RSCN           0x8000000	/* Delay RSCN till ureg/reg login */
#define FC_RSCN_DISCOVERY       0x10000000	/* Authenticate all devices after RSCN */
#define FC_2G_CAPABLE           0x20000000	/* HBA is 2 Gig capable */
#define FC_POLL_MODE        0x40000000	/* [SYNC] I/O is in the polling mode */
#define FC_BYPASSED_MODE        0x80000000	/* Interface is offline for diag */

/* CHECK */
	uint32_t fc_open_count;	/* count of devices opened */
#define FC_LAN_OPEN 0x1		/* LAN open completed */
#define FC_FCP_OPEN 0x2		/* FCP open completed */

	uint32_t fc_cnt;	/* generic counter for board */
	uint32_t fc_msgidx;	/* current index to adapter msg buf */
	volatile uint32_t fc_BCregaddr;	/* virtual offset for BIU config reg */
	uint16_t fc_rpi_used;

	uint32_t fc_topology;	/* link topology, from LINK INIT */
	uint32_t fc_fabrictmo;	/* timeout for fabric timer */
	LPFC_NODELIST_t fc_nlp_bcast;	/* used for IP bcast's */

	ELXCLOCK_t *fc_fabric_wdt;	/* timer for fabric    */
	ELXCLOCK_t *fc_rscn_disc_wdt;	/* timer for RSCN discovery */
	LPFC_STAT_t fc_stat;

	uint32_t fc_ipfarp_timeout;	/* timeout in seconds for farp req completion. */
	uint32_t fc_ipxri_timeout;	/* timeout in seconds for ip XRI create completions. */
	 ELX_TQS_LINK(lpfc_node_farp_pend) fc_node_farp_list;

	LPFC_BINDLIST_t *fc_nlpbind_start;	/* ptr to bind list */
	LPFC_BINDLIST_t *fc_nlpbind_end;	/* ptr to bind list */
	LPFC_NODELIST_t *fc_plogi_start;	/* ptr to plogi list */
	LPFC_NODELIST_t *fc_plogi_end;	/* ptr to plogi list */
	LPFC_NODELIST_t *fc_adisc_start;	/* ptr to adisc list */
	LPFC_NODELIST_t *fc_adisc_end;	/* ptr to adisc list */
	LPFC_NODELIST_t *fc_nlpunmap_start;	/* ptr to unmap list */
	LPFC_NODELIST_t *fc_nlpunmap_end;	/* ptr to unmap list */
	LPFC_NODELIST_t *fc_nlpmap_start;	/* ptr to map list */
	LPFC_NODELIST_t *fc_nlpmap_end;	/* ptr to map list */
	uint16_t fc_bind_cnt;
	uint16_t fc_plogi_cnt;
	uint16_t fc_adisc_cnt;
	uint16_t fc_unmap_cnt;
	uint16_t fc_map_cnt;
	LPFC_NODELIST_t fc_fcpnodev;	/* nodelist entry for no device */
	uint32_t nlptimer;	/* timestamp for nlplist entry */
	uint16_t fc_capabilities;	/* default value for NODELIST caps */
	uint16_t fc_sync;	/* default value for NODELIST sync */

	ELX_IOCBQ_t *fc_delayxmit;	/* List of IOCBs for delayed xmit */

	ELXSCSITARGET_t *device_queue_hash[MAX_FCP_TARGET];
#define LPFC_RPI_HASH_SIZE     64
#define LPFC_RPI_HASH_FUNC(x)  ((x) & (0x3f))
	LPFC_NODELIST_t *fc_nlplookup[LPFC_RPI_HASH_SIZE];	/* ptr to active 
								   D_ID / RPIs */
	uint32_t wwnn[2];
	uint32_t RandomData[7];
	uint32_t hbainitEx[5];
} LPFCHBA_t;

typedef struct lpfcTarget {
	ELXCLOCK_t *nodevTmr;	/* Timer for nodev-tmo */
} LPFCTARGET_t;

#endif				/* _H_LPFC_HBA */
