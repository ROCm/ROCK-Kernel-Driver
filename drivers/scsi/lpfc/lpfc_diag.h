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

#ifndef _H_LPFC_DIAG
#define _H_LPFC_DIAG

/* the brdinfo structure */
typedef struct BRDINFO {
	uint32_t a_mem_hi;	/* memory identifier for adapter access */
	uint32_t a_mem_low;	/* memory identifier for adapter access */
	uint32_t a_flash_hi;	/* memory identifier for adapter access */
	uint32_t a_flash_low;	/* memory identifier for adapter access */
	uint32_t a_ctlreg_hi;	/* memory identifier for adapter access */
	uint32_t a_ctlreg_low;	/* memory identifier for adapter access */
	uint32_t a_intrlvl;	/* interrupt level for adapter */
	uint32_t a_pci;		/* PCI identifier (device / vendor id) */
	uint32_t a_busid;	/* identifier of PCI bus adapter is on */
	uint32_t a_devid;	/* identifier of PCI device number */
	uint8_t a_rsvd1;	/* reserved for future use */
	uint8_t a_rsvd2;	/* reserved for future use */
	uint8_t a_siglvl;	/* signal handler used by library */
	uint8_t a_ddi;		/* identifier device driver instance number */
	uint32_t a_onmask;	/* mask of ONDI primatives supported */
	uint32_t a_offmask;	/* mask of OFFDI primatives supported */
	uint8_t a_drvrid[16];	/* driver version */
	uint8_t a_fwname[32];	/* firmware version */
} brdinfo;

   /*
      Define interface for the dynamic persistent binding ioctl
    */
#define       ELX_WWNN_BIND 0x0
#define       ELX_WWPN_BIND 0x1
#define       ELX_DID_BIND  0x2
#define       ELX_SCSI_ID   0x3

typedef struct {
	uint8_t bind_type;
	uint8_t wwpn[8];
	uint8_t wwnn[8];
	uint32_t did;
	uint32_t scsi_id;
} bind_ctl_t;

/* bits in a_onmask */
#define ONDI_MBOX       0x1	/* allows non-destructive mailbox commands */
#define ONDI_IOINFO     0x2	/* supports retrieval of I/O info */
#define ONDI_LNKINFO    0x4	/* supports retrieval of link info */
#define ONDI_NODEINFO   0x8	/* supports retrieval of node info */
#define ONDI_TRACEINFO  0x10	/* supports retrieval of trace info */
#define ONDI_SETTRACE   0x20	/* supports configuration of trace info */
#define ONDI_SLI1       0x40	/* hardware supports SLI-1 interface */
#define ONDI_SLI2       0x80	/* hardware supports SLI-2 interface */
#define ONDI_BIG_ENDIAN 0x100	/* DDI interface is BIG Endian */
#define ONDI_LTL_ENDIAN 0x200	/* DDI interface is LITTLE Endian */
#define ONDI_RMEM       0x400	/* allows reading of adapter shared memory */
#define ONDI_RFLASH     0x800	/* allows reading of adapter flash */
#define ONDI_RPCI       0x1000	/* allows reading of adapter pci registers */
#define ONDI_RCTLREG    0x2000	/* allows reading of adapter cntrol registers */
#define ONDI_CFGPARAM   0x4000	/* supports get/set configuration parameters */
#define ONDI_CT         0x8000	/* supports passthru CT interface */
#define ONDI_HBAAPI     0x10000	/* supports HBA API interface */

/* bits in a_offmask */
#define OFFDI_MBOX      0x1	/* allows all mailbox commands */
#define OFFDI_RMEM      0x2	/* allows reading of adapter shared memory */
#define OFFDI_WMEM      0x4	/* allows writing of adapter shared memory */
#define OFFDI_RFLASH    0x8	/* allows reading of adapter flash */
#define OFFDI_WFLASH    0x10	/* allows writing of adapter flash */
#define OFFDI_RPCI      0x20	/* allows reading of adapter pci registers */
#define OFFDI_WPCI      0x40	/* allows writing of adapter pci registers */
#define OFFDI_RCTLREG   0x80	/* allows reading of adapter cntrol registers */
#define OFFDI_WCTLREG   0x100	/* allows writing of adapter cntrol registers */
#define OFFDI_OFFLINE   0x80000000	/* if set, adapter is in offline state */

/* values for flag in SetDiagEnv */
#define DDI_SHOW        0x0
#define DDI_ONDI        0x1
#define DDI_OFFDI       0x2

#define DDI_BRD_SHOW    0x10
#define DDI_BRD_ONDI    0x11
#define DDI_BRD_OFFDI   0x12

#define DDI_UNUSED      0xFFFFFFFFL

/* the ioinfo structure */
typedef struct IOINFO {
	uint32_t a_mbxCmd;	/* mailbox commands issued */
	uint32_t a_mboxCmpl;	/* mailbox commands completed */
	uint32_t a_mboxErr;	/* mailbox commands completed, error status */
	uint32_t a_iocbCmd;	/* iocb command ring issued */
	uint32_t a_iocbRsp;	/* iocb rsp ring received */
	uint32_t a_adapterIntr;	/* adapter interrupt events */
	uint32_t a_fcpCmd;	/* FCP commands issued */
	uint32_t a_fcpCmpl;	/* FCP command completions received */
	uint32_t a_fcpErr;	/* FCP command completions errors */
	uint32_t a_seqXmit;	/* IP xmit sequences sent */
	uint32_t a_seqRcv;	/* IP sequences received */
	uint32_t a_bcastXmit;	/* cnt of successful xmit bcast cmds issued */
	uint32_t a_bcastRcv;	/* cnt of receive bcast cmds received */
	uint32_t a_elsXmit;	/* cnt of successful ELS req cmds issued */
	uint32_t a_elsRcv;	/* cnt of ELS request commands received */
	uint32_t a_RSCNRcv;	/* cnt of RSCN commands received */
	uint32_t a_seqXmitErr;	/* cnt of unsuccessful xmit bcast cmds issued */
	uint32_t a_elsXmitErr;	/* cnt of unsuccessful ELS req cmds issued  */
	uint32_t a_elsBufPost;	/* cnt of ELS buffers posted to adapter */
	uint32_t a_ipBufPost;	/* cnt of IP buffers posted to adapter */
	uint32_t a_cnt1;	/* generic counter */
	uint32_t a_cnt2;	/* generic counter */
	uint32_t a_cnt3;	/* generic counter */
	uint32_t a_cnt4;	/* generic counter */
} IOinfo;

/* the linkinfo structure */
typedef struct LINKINFO {
	uint32_t a_linkEventTag;
	uint32_t a_linkUp;
	uint32_t a_linkDown;
	uint32_t a_linkMulti;
	uint32_t a_DID;
	uint8_t a_topology;
	uint8_t a_linkState;
	uint8_t a_alpa;
	uint8_t a_alpaCnt;
	uint8_t a_alpaMap[128];
	uint8_t a_wwpName[8];
	uint8_t a_wwnName[8];
} LinkInfo;

/* values for a_topology */
#define LNK_LOOP                0x1
#define LNK_PUBLIC_LOOP         0x2
#define LNK_FABRIC              0x3
#define LNK_PT2PT               0x4

/* values for a_linkState */
#define LNK_DOWN                0x1
#define LNK_UP                  0x2
#define LNK_FLOGI               0x3
#define LNK_DISCOVERY           0x4
#define LNK_REDISCOVERY         0x5
#define LNK_READY               0x6

/* the traceinfo structure */
typedef struct TRACEINFO {
	uint8_t a_event;
	uint8_t a_cmd;
	uint16_t a_status;
	uint32_t a_information;
} TraceInfo;

/* values for flag */
#define TRC_SHOW        0x0
#define TRC_MBOX        0x1
#define TRC_IOCB        0x2
#define TRC_INTR        0x4
#define TRC_EVENT       0x8

/* values for a_event */
#define TRC_MBOX_CMD    0x1
#define TRC_MBOX_CMPL   0x2
#define TRC_IOCB_CMD    0x3
#define TRC_IOCB_RSP    0x4
#define TRC_INTR_RCV    0x5
#define TRC_EVENT1      0x6
#define TRC_EVENT2      0x7
#define TRC_EVENT_MASK  0x7
#define TRC_RING0       0x0
#define TRC_RING1       0x40
#define TRC_RING2       0x80
#define TRC_RING3       0xC0
#define TRC_RING_MASK   0xC0

/* the cfgparam structure */
typedef struct CFGPARAM {
	char a_string[32];
	uint32_t a_low;
	uint32_t a_hi;
	uint32_t a_default;
	uint32_t a_current;
	uint16_t a_flag;
	uint16_t a_changestate;
	char a_help[80];
} CfgParam;

#define MAX_CFG_PARAM 64

/* values for a_flag */
#define CFG_EXPORT      0x1	/* Export this parameter to the end user */
#define CFG_IGNORE      0x2	/* Ignore this parameter */
#define CFG_DEFAULT     0x8000	/* Reestablishing Link */

/* values for a_changestate */
#define CFG_REBOOT      0x0	/* Changes effective after ystem reboot */
#define CFG_DYNAMIC     0x1	/* Changes effective immediately */
#define CFG_RESTART     0x2	/* Changes effective after driver restart */

/* the icfgparam structure - internal use only */
typedef struct ICFGPARAM {
	char *a_string;
	uint32_t a_low;
	uint32_t a_hi;
	uint32_t a_default;
	uint32_t a_current;
	uint16_t a_flag;
	uint16_t a_changestate;
	char *a_help;
} iCfgParam;

/* the nodeinfo structure */
typedef struct NODEINFO {
	uint16_t a_flag;
	uint16_t a_state;
	uint32_t a_did;
	uint8_t a_wwpn[8];
	uint8_t a_wwnn[8];
	uint32_t a_targetid;
} NodeInfo;

#define MAX_NODES 512

/* Defines for a_state */
#define NODE_UNUSED     0
#define NODE_LIMBO      0x1	/* entry needs to hang around for wwpn / sid */
#define NODE_LOGOUT     0x2	/* NL_PORT is not logged in - entry is cached */
#define NODE_PLOGI      0x3	/* PLOGI was sent to NL_PORT */
#define NODE_LOGIN      0x4	/* NL_PORT is logged in / login REG_LOGINed */
#define NODE_PRLI       0x5	/* PRLI was sent to NL_PORT */
#define NODE_ALLOC      0x6	/* NL_PORT is  ready to initiate adapter I/O */
#define NODE_SEED       0x7	/* seed scsi id bind in table */

/* Defines for a_flag */
#define NODE_RPI_XRI        0x1	/* creating xri for entry */
#define NODE_REQ_SND        0x2	/* sent ELS request for this entry */
#define NODE_ADDR_AUTH      0x4	/* Authenticating addr for this entry */
#define NODE_RM_ENTRY       0x8	/* Remove this entry */
#define NODE_FARP_SND       0x10	/* sent FARP request for this entry */
#define NODE_FABRIC         0x20	/* this entry represents the Fabric */
#define NODE_FCP_TARGET     0x40	/* this entry is an FCP target */
#define NODE_IP_NODE        0x80	/* this entry is an IP node */
#define NODE_DISC_START     0x100	/* start discovery on this entry */
#define NODE_SEED_WWPN      0x200	/* Entry scsi id is seeded for WWPN */
#define NODE_SEED_WWNN      0x400	/* Entry scsi id is seeded for WWNN */
#define NODE_SEED_DID       0x800	/* Entry scsi id is seeded for DID */
#define NODE_SEED_MASK      0xe00	/* mask for seeded flags */
#define NODE_AUTOMAP        0x1000	/* This entry was automap'ed */
#define NODE_NS_REMOVED     0x2000	/* This entry removed from NameServer */

/* Defines for RegisterForEvent mask */
#define FC_REG_LINK_EVENT       0x1	/* Register for link up / down events */
#define FC_REG_RSCN_EVENT       0x2	/* Register for RSCN events */
#define FC_REG_CT_EVENT         0x4	/* Register for CT request events */

#define FC_REG_EVENT_MASK       0x2f	/* event mask */
#define FC_REG_ALL_PORTS        0x80	/* Register for all ports */

#define MAX_FC_EVENTS 8		/* max events user process can wait for per HBA */
#define FC_FSTYPE_ALL 0xffff	/* match on all fsTypes */

/* Defines for error codes */
#define FC_ERROR_BUFFER_OVERFLOW          0xff
#define FC_ERROR_RESPONSE_TIMEOUT         0xfe
#define FC_ERROR_LINK_UNAVAILABLE         0xfd
#define FC_ERROR_INSUFFICIENT_RESOURCES   0xfc
#define FC_ERROR_EXISTING_REGISTRATION    0xfb
#define FC_ERROR_INVALID_TAG              0xfa
#define FC_ERROR_INVALID_WWN              0xf9
#define FC_ERROR_CREATEVENT_FAILED        0xf8

/* User Library level Event structure */
typedef struct reg_evt {
	uint32_t e_mask;
	uint32_t e_gstype;
	uint32_t e_pid;
	uint32_t e_firstchild;
	uint32_t e_outsz;
	uint32_t e_pad;
	void (*e_func) (uint32_t, ...);
	void *e_ctx;
	void *e_out;
} RegEvent;

/* Defines for portid for CT interface */
#define CT_FabricCntlServer ((uint32_t)0xfffffd)
#define CT_NameServer       ((uint32_t)0xfffffc)
#define CT_TimeServer       ((uint32_t)0xfffffb)
#define CT_MgmtServer       ((uint32_t)0xfffffa)

struct dfc_info {
	brdinfo fc_ba;
	char *fc_iomap_io;	/* starting address for registers */
	char *fc_iomap_mem;	/* starting address for SLIM */
	uint8_t *fc_hmap;	/* handle for mapping memory */
	uint32_t fc_refcnt;
	uint32_t fc_flag;
};

struct dfc {
	uint32_t dfc_init;
	uint32_t dfc_pad;
	struct dfc_info dfc_info[MAX_ELX_BRDS];
};

/* Define for fc_flag */
#define DFC_STOP_IOCTL   1	/* Stop processing dfc ioctls */
#define DFC_MBOX_ACTIVE  2	/* mailbox is active thru dfc */

/* Define for dfc 'riocb' function */
#define FC_RING(ringoff,sa)     ((volatile uint8_t *)((volatile uint8_t *)sa + (unsigned long)(ringoff)))

#endif				/* _H_LPFC_DIAG */
