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

#ifndef  _H_LPFC_DISC
#define  _H_LPFC_DISC

#include "elx_clock.h"
#include "elx_disc.h"
#include "elx_scsi.h"
#include "lpfc_hw.h"

#define FC_MAX_HOLD_RSCN     32	/* max number of deferred RSCNs */
#define FC_MAX_NS_RSP        65536	/* max size NameServer rsp */

#define FC_MAXLOOP           126	/* max devices supported on a single fc loop */

#define LPFC_DISC_FLOGI_TMO  15	/* Discovery FLOGI ratov */

/* Provide an enumeration for the Types of addresses a FARP can resolve. */
typedef enum lpfc_farp_addr_type {
	LPFC_FARP_BY_IEEE,
	LPFC_FARP_BY_WWPN,
	LPFC_FARP_BY_WWNN,
} LPFC_FARP_ADDR_TYPE;

/* This is the protocol dependent definition for a Node List Entry.
 * This is used by Fibre Channel protocol to support FCP and IP.
 */

struct lpfc_bindlist {
	struct lpfc_bindlist *nlp_listp_next;
	struct lpfc_bindlist *nlp_listp_prev;
	ELXSCSITARGET_t *nlp_Target;	/* Pointer to the target structure */
	NAME_TYPE nlp_portname;	/* port name */
	NAME_TYPE nlp_nodename;	/* node name */
	uint16_t nlp_bind_type;
	uint16_t nlp_pan;	/* pseudo adapter number */
	uint16_t nlp_sid;	/* scsi id */
	uint32_t nlp_DID;	/* fibre channel D_ID of entry */
};
typedef struct lpfc_bindlist LPFC_BINDLIST_t;

struct lpfc_nodelist {
	ELX_NODELIST_t nle;	/* This MUST be first. Common information */
	uint16_t nlp_state;	/* state transition indicator */
	uint16_t nlp_xri;	/* output exchange id for RPI */
	uint32_t nlp_flag;	/* entry  flags */
	uint32_t nlp_DID;	/* fibre channel D_ID of entry */
	uint32_t nlp_oldDID;	/* old fibre channel D_ID */
	NAME_TYPE nlp_portname;	/* port name */
	NAME_TYPE nlp_nodename;	/* node name */
	uint16_t nlp_pan;	/* pseudo adapter number */
	uint16_t nlp_sid;	/* scsi id */
	uint8_t nlp_retry;	/* used for ELS retries */
	ELXCLOCK_t *nlp_tmofunc;	/* Used for delayed ELS cmds, nodev tmo */
	ELXSCSITARGET_t *nlp_Target;	/* Pointer to the target structure */
	ELXCLOCK_t *nlp_xri_tmofunc;	/* Timer function for XRI create timeout. */
	 ELX_TQS_LINK(lpfc_ip_buf) nlp_listp_ipbuf;	/* Linked list of IP Buffers waiting for an XRI Create to Finish */
	LPFC_BINDLIST_t *nlp_listp_bind;	/* Linked list of bounded remote ports */
	struct lpfc_nodelist *nlp_rpi_hash_next;
};

typedef struct lpfc_nodelist LPFC_NODELIST_t;

/* Defines for nlp_flag (uint32) */
#define NLP_MAPPED_LIST    0x1	/* Node is now mapped */
#define NLP_UNMAPPED_LIST  0x2	/* Node is now unmapped */
#define NLP_PLOGI_LIST     0x4	/* Flg to indicate send PLOGI */
#define NLP_ADISC_LIST     0x8	/* Flg to indicate send PLOGI */
#define NLP_LIST_MASK      0xf	/* mask to see what list node is on */
#define NLP_BIND_ASSOC     0x10	/* Node is now bound */
#define NLP_PLOGI_SND      0x20	/* sent PLOGI request for this entry */
#define NLP_PRLI_SND       0x40	/* sent PRLI request for this entry */
#define NLP_ADISC_SND      0x80	/* sent ADISC request for this entry */
#define NLP_LOGO_SND       0x100	/* sent LOGO request for this entry */
#define NLP_FARP_SND       0x200	/* sent FARP request for this entry */
#define NLP_RNID_SND       0x400	/* sent RNID request for this entry */
#define NLP_ELS_SND_MASK   0x7e0	/* sent ELS request for this entry */
#define NLP_AUTOMAP        0x800	/* Entry was automap'ed */
#define NLP_SEED_WWPN      0x1000	/* Entry scsi id is seeded for WWPN */
#define NLP_SEED_WWNN      0x2000	/* Entry scsi id is seeded for WWNN */
#define NLP_SEED_DID       0x4000	/* Entry scsi id is seeded for DID */
#define NLP_SEED_MASK      0x807000	/* mask for seeded flags */
#define NLP_NS_NODE        0x8000	/* Authenticated entry by NameServer */
#define NLP_NODEV_TMO      0x10000	/* nodev timeout is running for node */
#define NLP_DELAY_TMO      0x20000	/* delay timeout is running for node */
#define NLP_DISC_NODE      0x40000	/* node is included in num_disc_nodes */
#define NLP_RCV_PLOGI      0x80000	/* Rcv'ed PLOGI from remote system */
#define NLP_LOGO_ACC       0x100000	/* Process LOGO after ACC completes */
#define NLP_TGT_NO_SCSIID  0x200000	/* good PRLI but no binding for scsid */
#define NLP_CREATE_XRI_INP 0x400000	/* in process of creating an XRI */
#define NLP_SEED_ALPA      0x800000	/* SCSI id is derived from alpa array */
#define NLP_ACC_REGLOGIN   0x1000000	/* Issue Reg Login after successful ACC */

/* Defines for list searchs */
#define NLP_SEARCH_MAPPED    0x1	/* search mapped */
#define NLP_SEARCH_UNMAPPED  0x2	/* search unmapped */
#define NLP_SEARCH_PLOGI     0x4	/* search plogi */
#define NLP_SEARCH_ADISC     0x8	/* search adisc */
#define NLP_SEARCH_ALL       0xf	/* search all lists */
#define NLP_SEARCH_DEQUE     0x10	/* deque node if found */

/* There are 4 different double linked lists nodelist entries can reside on.
 * The Port Login (PLOGI) list and Address Discovery (ADISC) list are used 
 * when Link Up discovery or Registered State Change Notification (RSCN) 
 * processing is needed.  Each list holds the nodes that require a PLOGI or 
 * ADISC Extended Link Service (ELS) request.  These lists keep track of the
 * nodes affected by an RSCN, or a Link Up (Typically, all nodes are effected 
 * by Link Up) event.  The unmapped_list contains all nodes that have 
 * successfully logged into at the Fibre Channel level.  The
 * mapped_list will contain all nodes that are mapped FCP targets.
 *
 * The bind list is a list of undiscovered (potentially non-existent) nodes
 * that we have saved binding information on. This information is used when
 * nodes transition from the unmapped to the mapped list.
 */

/* Defines for nlp_state */
#define NLP_STE_UNUSED_NODE       0x0	/* node is just allocated */
#define NLP_STE_PLOGI_ISSUE       0x1	/* PLOGI was sent to NL_PORT */
#define NLP_STE_REG_LOGIN_ISSUE   0x2	/* REG_LOGIN was issued for NL_PORT */
#define NLP_STE_PRLI_ISSUE        0x3	/* PRLI was sent to NL_PORT */
#define NLP_STE_PRLI_COMPL        0x4	/* PRLI completed from NL_PORT */
#define NLP_STE_MAPPED_NODE       0x5	/* Identified as a FCP Target */
#define NLP_STE_MAX_STATE         0x6
#define NLP_STE_FREED_NODE        0xff	/* node entry was freed to MEM_NLP */

/* For UNUSED_NODE state, the node has just been allocated from the MEM_NLP
 * driver memory pool. For PLOGI_ISSUE and REG_LOGIN_ISSUE, the node is on
 * the PLOGI list. For REG_LOGIN_COMPL, the node is taken off the PLOGI list
 * and put on the unmapped list. For ADISC processing, the node is taken off 
 * the ADISC list and placed on either the mapped or unmapped list (depending
 * on its previous state). Once on the unmapped list, a PRLI is issued and the
 * state changed to PRLI_ISSUE. When the PRLI completion occurs, the state is
 * changed to PRLI_COMPL. If the completion indicates a mapped
 * node, the node is taken off the unmapped list. The binding list is checked
 * for a valid binding, or a binding is automatically assigned. If binding
 * assignment is unsuccessful, the node is left on the unmapped list. If
 * binding assignment is successful, the associated binding list entry (if
 * any) is removed, and the node is placed on the mapped list. 
 */
/*
 * For a Link Down, all nodes on the ADISC, PLOGI, unmapped or mapped
 * lists will receive a DEVICE_UNK event. If the linkdown or nodev timers
 * expire, all effected nodes will receive a DEVICE_RM event.
 */
/*
 * For a Link Up or RSCN, all nodes will move from the mapped / unmapped 
 * lists to either the ADISC or PLOGI list.  After a Nameserver
 * query or ALPA loopmap check, additional nodes may be added (DEVICE_ADD)
 * or removed (DEVICE_RM) to / from the PLOGI or ADISC lists. Once the PLOGI
 * and ADISC lists are populated, we will first process the ADISC list.
 * 32 entries are processed initially and ADISC is initited for each one.
 * Completions / Events for each node are funnelled thru the state machine.
 * As each node finishes ADISC processing, it starts ADISC for any nodes
 * waiting for ADISC processing. If no nodes are waiting, and the ADISC
 * list count is identically 0, then we are done. For Link Up discovery, since all nodes
 * on the PLOGI list are UNREG_LOGIN'ed, we can issue a CLEAR_LA and reenable
 * Link Events. Next we will process the PLOGI list.
 * 32 entries are processed initially and PLOGI is initited for each one.
 * Completions / Events for each node are funnelled thru the state machine.
 * As each node finishes PLOGI processing, it starts PLOGI for any nodes
 * waiting for PLOGI processing. If no nodes are waiting, and the PLOGI
 * list count is identically 0, then we are done. We have now completed discovery /
 * RSCN handling. Upon completion, ALL nodes should be on either the mapped
 * or unmapped lists.
 */

/* Defines for Node List Entry Events that could happen */
#define NLP_EVT_RCV_PLOGI         0x0	/* Rcv'd an ELS PLOGI command */
#define NLP_EVT_RCV_PRLI          0x1	/* Rcv'd an ELS PRLI  command */
#define NLP_EVT_RCV_LOGO          0x2	/* Rcv'd an ELS LOGO  command */
#define NLP_EVT_RCV_ADISC         0x3	/* Rcv'd an ELS ADISC command */
#define NLP_EVT_RCV_PDISC         0x4	/* Rcv'd an ELS PDISC command */
#define NLP_EVT_RCV_PRLO          0x5	/* Rcv'd an ELS PRLO  command */
#define NLP_EVT_CMPL_PLOGI        0x6	/* Sent an ELS PLOGI command */
#define NLP_EVT_CMPL_PRLI         0x7	/* Sent an ELS PRLI  command */
#define NLP_EVT_CMPL_LOGO         0x8	/* Sent an ELS LOGO  command */
#define NLP_EVT_CMPL_ADISC        0x9	/* Sent an ELS ADISC command */
#define NLP_EVT_CMPL_REG_LOGIN    0xa	/* REG_LOGIN mbox cmd completed */
#define NLP_EVT_DEVICE_RM         0xb	/* Device not found in NS / ALPAmap */
#define NLP_EVT_DEVICE_ADD        0xc	/* Device found in NS / ALPAmap */
#define NLP_EVT_DEVICE_UNK        0xd	/* Device existence unknown */
#define NLP_EVT_MAX_EVENT         0xe

#define LPFC_SYNTAX_OK                      0
#define LPFC_SYNTAX_OK_BUT_NOT_THIS_BRD     1
#define LPFC_SYNTAX_ERR_ASC_CONVERT         2
#define LPFC_SYNTAX_ERR_EXP_COLON           3
#define LPFC_SYNTAX_ERR_EXP_LPFC            4
#define LPFC_SYNTAX_ERR_INV_LPFC_NUM        5
#define LPFC_SYNTAX_ERR_EXP_T               6
#define LPFC_SYNTAX_ERR_INV_TARGET_NUM      7
#define LPFC_SYNTAX_ERR_EXP_D               8
#define LPFC_SYNTAX_ERR_INV_DEVICE_NUM      9
#define LPFC_SYNTAX_ERR_INV_TRAFFIC_RATIO  10
#define LPFC_SYNTAX_ERR_INV_ROUTE_FLAGS    11
#define LPFC_SYNTAX_ERR_TOO_MANY_PATHS     12
#define LPFC_SYNTAX_ERR_EXP_NULL_TERM      13

/* Definitions for Binding Entry Type for lpfc_parse_binding_entry()  */
#define LPFC_BIND_WW_NN_PN   0
#define LPFC_BIND_DID        1

#endif				/* _H_LPFC_DISC */
