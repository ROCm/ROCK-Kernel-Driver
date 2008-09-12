/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2006 Emulex.  All rights reserved.                *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *******************************************************************/

#define DFC_MAJOR_REV   81
#define DFC_MINOR_REV   0

#define LPFC_MAX_EVENT 128

#define LPFC_CT                         0x42    /* Send CT passthru command */
#define LPFC_HBA_RNID                   0x52    /* Send an RNID request */
#define LPFC_HBA_REFRESHINFO            0x56    /* Do a refresh of the stats */
#define LPFC_SEND_ELS                   0x57    /* Send out an ELS command */
#define LPFC_HBA_SET_EVENT              0x59    /* Set FCP event(s) */
#define LPFC_HBA_GET_EVENT              0x5a    /* Get  FCP event(s) */
#define LPFC_HBA_SEND_MGMT_CMD          0x5b    /* Send a management command */
#define LPFC_HBA_SEND_MGMT_RSP          0x5c    /* Send a management response */

#define LPFC_GET_DFC_REV                0x68    /* Get the rev of the ioctl
							driver */
#define LPFC_LOOPBACK_TEST              0x72    /* Run Loopback test */
#define LPFC_LOOPBACK_MODE              0x73    /* Enter Loopback mode */
/* LPFC_LAST_IOCTL_USED                 0x73       Last LPFC Ioctl used  */

#define INTERNAL_LOOP_BACK              0x1
#define EXTERNAL_LOOP_BACK              0x2

/* the DfcRevInfo structure */
struct DfcRevInfo {
	uint32_t a_Major;
	uint32_t a_Minor;
} ;

#define LPFC_WWPN_TYPE		0
#define LPFC_PORTID_TYPE	1
#define LPFC_WWNN_TYPE		2

struct nport_id {
   uint32_t    idType;         /* 0 - wwpn, 1 - d_id, 2 - wwnn */
   uint32_t    d_id;
   uint8_t     wwpn[8];
};

#define LPFC_EVENT_LIP_OCCURRED		1
#define LPFC_EVENT_LINK_UP		2
#define LPFC_EVENT_LINK_DOWN		3
#define LPFC_EVENT_LIP_RESET_OCCURRED	4
#define LPFC_EVENT_RSCN			5
#define LPFC_EVENT_PROPRIETARY		0xFFFF

struct lpfc_hba_event_info {
	uint32_t event_code;
	uint32_t port_id;
	union {
		uint32_t rscn_event_info;
		uint32_t pty_event_info;
	} event;
};


#define LPFC_CHAR_DEV_NAME "lpfcdfc"

/*
 * Diagnostic (DFC) Command & Input structures: (LPFC)
 */
struct lpfcCmdInput {
	short    lpfc_brd;
	short    lpfc_ring;
	short    lpfc_iocb;
	short    lpfc_flag;
	void    *lpfc_arg1;
	void    *lpfc_arg2;
	void    *lpfc_arg3;
	char    *lpfc_dataout;
	uint32_t lpfc_cmd;
	uint32_t lpfc_outsz;
	uint32_t lpfc_arg4;
	uint32_t lpfc_arg5;
};
/* Used for ioctl command */
#define LPFC_DFC_CMD_IOCTL_MAGIC 0xFC
#define LPFC_DFC_CMD_IOCTL _IOWR(LPFC_DFC_CMD_IOCTL_MAGIC, 0x1,\
		struct lpfcCmdInput)

#ifdef CONFIG_COMPAT
/* 32 bit version */
struct lpfcCmdInput32 {
	short    lpfc_brd;
	short    lpfc_ring;
	short    lpfc_iocb;
	short    lpfc_flag;
	u32     lpfc_arg1;
	u32     lpfc_arg2;
	u32     lpfc_arg3;
	u32     lpfc_dataout;
	uint32_t lpfc_cmd;
	uint32_t lpfc_outsz;
	uint32_t lpfc_arg4;
	uint32_t lpfc_arg5;
};
#endif

#define SLI_CT_ELX_LOOPBACK 0x10

enum ELX_LOOPBACK_CMD {
	ELX_LOOPBACK_XRI_SETUP,
	ELX_LOOPBACK_DATA,
};


struct lpfc_link_info {
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
};

enum lpfc_host_event_code  {
	LPFCH_EVT_LIP            = 0x1,
	LPFCH_EVT_LINKUP         = 0x2,
	LPFCH_EVT_LINKDOWN       = 0x3,
	LPFCH_EVT_LIPRESET       = 0x4,
	LPFCH_EVT_RSCN           = 0x5,
	LPFCH_EVT_ADAPTER_CHANGE = 0x103,
	LPFCH_EVT_PORT_UNKNOWN   = 0x200,
	LPFCH_EVT_PORT_OFFLINE   = 0x201,
	LPFCH_EVT_PORT_ONLINE    = 0x202,
	LPFCH_EVT_PORT_FABRIC    = 0x204,
	LPFCH_EVT_LINK_UNKNOWN   = 0x500,
	LPFCH_EVT_VENDOR_UNIQUE  = 0xffff,
};

#define ELX_LOOPBACK_HEADER_SZ \
	(size_t)(&((struct lpfc_sli_ct_request *)NULL)->un)

struct lpfc_host_event {
	uint32_t seq_num;
	enum lpfc_host_event_code event_code;
	uint32_t data;
};

struct lpfc_timedout_iocb_ctxt {
	struct lpfc_iocbq *rspiocbq;
	struct lpfc_dmabuf *mp;
	struct lpfc_dmabuf *bmp;
	struct lpfc_scsi_buf *lpfc_cmd;
	struct lpfc_dmabufext *outdmp;
	struct lpfc_dmabufext *indmp;
};

#ifdef __KERNEL__
struct lpfcdfc_host;

/* Initialize/Un-initialize char device */
int lpfc_cdev_init(void);
void lpfc_cdev_exit(void);
void lpfcdfc_host_del(struct lpfcdfc_host *);
struct lpfcdfc_host *lpfcdfc_host_add(struct pci_dev *, struct Scsi_Host *,
				      struct lpfc_hba *);
#endif	/* __KERNEL__ */
