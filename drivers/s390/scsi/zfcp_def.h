/* 
 * 
 * linux/drivers/s390/scsi/zfcp_def.h
 * 
 * FCP adapter driver for IBM eServer zSeries 
 * 
 * Copyright 2002 IBM Corporation 
 * Author(s): Martin Peschke <mpeschke@de.ibm.com> 
 *            Raimund Schroeder <raimund.schroeder@de.ibm.com> 
 *            Aron Zeh <arzeh@de.ibm.com> 
 *            Wolfgang Taphorn <taphorn@de.ibm.com> 
 *            Stefan Bader <stefan.bader@de.ibm.com> 
 *            Heiko Carstens <heiko.carstens@de.ibm.com> 
 * 
 * This program is free software; you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation; either version 2, or (at your option) 
 * any later version. 
 * 
 * This program is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 * GNU General Public License for more details. 
 * 
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 */ 


#ifndef ZFCP_DEF_H
#define ZFCP_DEF_H

#ifdef __KERNEL__

/* this drivers version (do not edit !!! generated and updated by cvs) */
#define ZFCP_DEF_REVISION "$Revision: 1.41 $"

/*************************** INCLUDES *****************************************/

#include <linux/blkdev.h>
#include "../../scsi/scsi.h"
#include "../../scsi/hosts.h"
#include "../../fc4/fc.h"
#include "zfcp_fsf.h"			/* FSF SW Interface */
#include <asm/ccwdev.h>
#include <asm/qdio.h>
#include <asm/debug.h>
#include <linux/reboot.h>

/************************ DEBUG FLAGS *****************************************/

#define	ZFCP_PRINT_FLAGS
#define	ZFCP_DEBUG_REQUESTS     /* fsf_req tracing */
#define ZFCP_DEBUG_COMMANDS     /* host_byte tracing */
#define ZFCP_DEBUG_ABORTS       /* scsi_cmnd abort tracing */
#define ZFCP_DEBUG_INCOMING_ELS /* incoming ELS tracing */
#define	ZFCP_STAT_REQSIZES
#define	ZFCP_STAT_QUEUES

/********************* SCSI SPECIFIC DEFINES *********************************/

/* 32 bit for SCSI ID and LUN as long as the SCSI stack uses this type */
typedef u32 scsi_id_t;
typedef u32 scsi_lun_t;

#define ZFCP_FAKE_SCSI_COMPLETION_TIME	        (HZ / 3)
#define ZFCP_ERP_SCSI_LOW_MEM_TIMEOUT           (100*HZ)
#define ZFCP_SCSI_ER_TIMEOUT                    (100*HZ)
#define ZFCP_SCSI_HOST_FLUSH_TIMEOUT            (1*HZ)

/********************* CIO/QDIO SPECIFIC DEFINES *****************************/

/* Adapter Identification Parameters */
#define ZFCP_CONTROL_UNIT_TYPE  0x1731
#define ZFCP_CONTROL_UNIT_MODEL 0x03
#define ZFCP_DEVICE_TYPE        0x1732
#define ZFCP_DEVICE_MODEL       0x03
 
/* allow as many chained SBALs as are supported by hardware */
#define ZFCP_MAX_SBALS_PER_REQ		FSF_MAX_SBALS_PER_REQ

/* DMQ bug workaround: don't use last SBALE */
#define ZFCP_MAX_SBALES_PER_SBAL	(QDIO_MAX_ELEMENTS_PER_BUFFER - 1)

/* index of last SBALE (with respect to DMQ bug workaround) */
#define ZFCP_LAST_SBALE_PER_SBAL	(ZFCP_MAX_SBALES_PER_SBAL - 1)

/* max. number of (data buffer) SBALEs in largest SBAL chain */
#define ZFCP_MAX_SBALES_PER_REQ		\
	(ZFCP_MAX_SBALS_PER_REQ * ZFCP_MAX_SBALES_PER_SBAL - 2)
        /* request ID + QTCB in SBALE 0 + 1 of first SBAL in chain */

/* FIXME(tune): free space should be one max. SBAL chain plus what? */
#define ZFCP_QDIO_PCI_INTERVAL		(QDIO_MAX_BUFFERS_PER_Q \
                                         - (ZFCP_MAX_SBALS_PER_REQ + 4))

#define ZFCP_SBAL_TIMEOUT               (5*HZ)

#define ZFCP_TYPE2_RECOVERY_TIME        (8*HZ)

/* queue polling (values in microseconds) */
#define ZFCP_MAX_INPUT_THRESHOLD 	5000	/* FIXME: tune */
#define ZFCP_MAX_OUTPUT_THRESHOLD 	1000	/* FIXME: tune */
#define ZFCP_MIN_INPUT_THRESHOLD 	1	/* ignored by QDIO layer */
#define ZFCP_MIN_OUTPUT_THRESHOLD 	1	/* ignored by QDIO layer */

#define QDIO_SCSI_QFMT			1	/* 1 for FSF */

/********************* FSF SPECIFIC DEFINES *********************************/

#define ZFCP_ULP_INFO_VERSION                   26
#define ZFCP_QTCB_VERSION	FSF_QTCB_CURRENT_VERSION
/* ATTENTION: value must not be used by hardware */
#define FSF_QTCB_UNSOLICITED_STATUS		0x6305
#define ZFCP_STATUS_READ_FAILED_THRESHOLD	3
#define ZFCP_STATUS_READS_RECOM		        FSF_STATUS_READS_RECOM
#define ZFCP_EXCHANGE_CONFIG_DATA_RETRIES	6
#define ZFCP_EXCHANGE_CONFIG_DATA_SLEEP		50

#define ZFCP_QTCB_SIZE		(sizeof(struct fsf_qtcb) + FSF_QTCB_LOG_SIZE)
#define ZFCP_QTCB_AND_REQ_SIZE	(sizeof(struct zfcp_fsf_req) + ZFCP_QTCB_SIZE)

/*************** FIBRE CHANNEL PROTOCOL SPECIFIC DEFINES ********************/

typedef unsigned long long wwn_t;
typedef unsigned int       fc_id_t;
typedef unsigned long long fcp_lun_t;
/* data length field may be at variable position in FCP-2 FCP_CMND IU */
typedef unsigned int       fcp_dl_t;

#define ZFCP_FC_SERVICE_CLASS_DEFAULT	FSF_CLASS_3

/* timeout for name-server lookup (in seconds) */
#define ZFCP_NAMESERVER_TIMEOUT		10

/* largest SCSI command we can process */
/* FCP-2 (FCP_CMND IU) allows up to (255-3+16) */
#define ZFCP_MAX_SCSI_CMND_LENGTH	255
/* maximum number of commands in LUN queue (tagged queueing) */
#define ZFCP_CMND_PER_LUN               32

/* task attribute values in FCP-2 FCP_CMND IU */
#define SIMPLE_Q	0
#define HEAD_OF_Q	1
#define ORDERED_Q	2
#define ACA_Q		4
#define UNTAGGED	5

/* task management flags in FCP-2 FCP_CMND IU */
#define CLEAR_ACA		0x40
#define TARGET_RESET		0x20
#define LOGICAL_UNIT_RESET	0x10
#define CLEAR_TASK_SET		0x04
#define ABORT_TASK_SET		0x02

#define FCP_CDB_LENGTH		16

#define ZFCP_DID_MASK           0x00FFFFFF

/* FCP(-2) FCP_CMND IU */
struct fcp_cmnd_iu {
	fcp_lun_t fcp_lun;	   /* FCP logical unit number */
	u8  crn;	           /* command reference number */
	u8  reserved0:5;	   /* reserved */
	u8  task_attribute:3;	   /* task attribute */
	u8  task_management_flags; /* task management flags */
	u8  add_fcp_cdb_length:6;  /* additional FCP_CDB length */
	u8  rddata:1;              /* read data */
	u8  wddata:1;              /* write data */
	u8  fcp_cdb[FCP_CDB_LENGTH];
} __attribute__((packed));

/* FCP(-2) FCP_RSP IU */
struct fcp_rsp_iu {
	u8  reserved0[10];
	union {
		struct {
			u8 reserved1:3;
			u8 fcp_conf_req:1;
			u8 fcp_resid_under:1;
			u8 fcp_resid_over:1;
			u8 fcp_sns_len_valid:1;
			u8 fcp_rsp_len_valid:1;
		} bits;
		u8 value;
	} validity;
	u8  scsi_status;
	u32 fcp_resid;
	u32 fcp_sns_len;
	u32 fcp_rsp_len;
} __attribute__((packed));


#define RSP_CODE_GOOD		 0
#define RSP_CODE_LENGTH_MISMATCH 1
#define RSP_CODE_FIELD_INVALID	 2
#define RSP_CODE_RO_MISMATCH	 3
#define RSP_CODE_TASKMAN_UNSUPP	 4
#define RSP_CODE_TASKMAN_FAILED	 5

/* see fc-fs */
#define LS_FAN 0x60000000
#define LS_RSCN 0x61040000

struct fcp_rscn_head {
        u8  command;
        u8  page_length; /* always 0x04 */
        u16 payload_len;
} __attribute__((packed));

struct fcp_rscn_element {
        u8  reserved:2;
        u8  event_qual:4;
        u8  addr_format:2;
        u32 nport_did:24;
} __attribute__((packed));

#define ZFCP_PORT_ADDRESS   0x0
#define ZFCP_AREA_ADDRESS   0x1
#define ZFCP_DOMAIN_ADDRESS 0x2
#define ZFCP_FABRIC_ADDRESS 0x3

#define ZFCP_PORTS_RANGE_PORT   0xFFFFFF
#define ZFCP_PORTS_RANGE_AREA   0xFFFF00
#define ZFCP_PORTS_RANGE_DOMAIN 0xFF0000
#define ZFCP_PORTS_RANGE_FABRIC 0x000000

#define ZFCP_NO_PORTS_PER_AREA    0x100
#define ZFCP_NO_PORTS_PER_DOMAIN  0x10000
#define ZFCP_NO_PORTS_PER_FABRIC  0x1000000

struct fcp_fan {
        u32 command;
        u32 fport_did;
        wwn_t fport_wwpn;
        wwn_t fport_wwname;
} __attribute__((packed));

/* see fc-ph */
struct fcp_logo {
        u32 command;
        u32 nport_did;
        wwn_t nport_wwpn;
} __attribute__((packed));

struct fc_ct_iu {
	u8	revision;	/* 0x01 */
	u8	in_id[3];	/* 0x00 */
	u8	gs_type;	/* 0xFC	Directory Service */
	u8	gs_subtype;	/* 0x02	Name Server */
	u8	options;	/* 0x10 synchronous/single exchange */
	u8	reserved0;
	u16	cmd_rsp_code;	/* 0x0121 GID_PN */
	u16	max_res_size;	/* <= (4096 - 16) / 4 */
	u8	reserved1;
	u8	reason_code;
	u8	reason_code_expl;
	u8	vendor_unique;
	union {
		wwn_t	wwpn;
		fc_id_t	d_id;
	} data;
} __attribute__ ((packed));

#define ZFCP_CT_REVISION		0x01
#define ZFCP_CT_DIRECTORY_SERVICE	0xFC
#define ZFCP_CT_NAME_SERVER		0x02
#define ZFCP_CT_SYNCHRONOUS		0x00
#define ZFCP_CT_GID_PN			0x0121
#define ZFCP_CT_MAX_SIZE		0x1020
#define ZFCP_CT_ACCEPT			0x8002

/***************** S390 DEBUG FEATURE SPECIFIC DEFINES ***********************/

/* debug feature entries per adapter */
#define ZFCP_ERP_DBF_INDEX     1 
#define ZFCP_ERP_DBF_AREAS     2
#define ZFCP_ERP_DBF_LENGTH    16
#define ZFCP_ERP_DBF_LEVEL     3
#define ZFCP_ERP_DBF_NAME      "zfcperp"

#define ZFCP_REQ_DBF_INDEX     1
#define ZFCP_REQ_DBF_AREAS     1
#define ZFCP_REQ_DBF_LENGTH    8
#define ZFCP_REQ_DBF_LEVEL     1
#define ZFCP_REQ_DBF_NAME      "zfcpreq"

#define ZFCP_CMD_DBF_INDEX     2
#define ZFCP_CMD_DBF_AREAS     1
#define ZFCP_CMD_DBF_LENGTH    8
#define ZFCP_CMD_DBF_LEVEL     3
#define ZFCP_CMD_DBF_NAME      "zfcpcmd"


#define ZFCP_ABORT_DBF_INDEX   2
#define ZFCP_ABORT_DBF_AREAS   1
#define ZFCP_ABORT_DBF_LENGTH  8
#define ZFCP_ABORT_DBF_LEVEL   6
#define ZFCP_ABORT_DBF_NAME    "zfcpabt"

#define ZFCP_IN_ELS_DBF_INDEX  2
#define ZFCP_IN_ELS_DBF_AREAS  1
#define ZFCP_IN_ELS_DBF_LENGTH 8
#define ZFCP_IN_ELS_DBF_LEVEL  6
#define ZFCP_IN_ELS_DBF_NAME   "zfcpels"

#define ZFCP_ADAPTER_REQ_DBF_INDEX  4 
#define ZFCP_ADAPTER_REQ_DBF_AREAS  1
#define ZFCP_ADAPTER_REQ_DBF_LENGTH 8
#define ZFCP_ADAPTER_REQ_DBF_LEVEL  6

/******************** LOGGING MACROS AND DEFINES *****************************/

/*
 * Logging may be applied on certain kinds of driver operations
 * independently. Additionally, different log-levels are supported for
 * each of these areas.
 */

#define ZFCP_NAME               "zfcp"

/* independent log areas */
#define ZFCP_LOG_AREA_OTHER	0
#define ZFCP_LOG_AREA_SCSI	1
#define ZFCP_LOG_AREA_FSF	2
#define ZFCP_LOG_AREA_CONFIG	3
#define ZFCP_LOG_AREA_CIO	4
#define ZFCP_LOG_AREA_QDIO	5
#define ZFCP_LOG_AREA_ERP	6
#define ZFCP_LOG_AREA_FC	7

/* log level values*/
#define ZFCP_LOG_LEVEL_NORMAL	0
#define ZFCP_LOG_LEVEL_INFO	1
#define ZFCP_LOG_LEVEL_DEBUG	2
#define ZFCP_LOG_LEVEL_TRACE	3

/* default log levels for different log areas */
#define ZFCP_LOG_LEVEL_DEFAULT_OTHER	ZFCP_LOG_LEVEL_INFO
#define ZFCP_LOG_LEVEL_DEFAULT_SCSI	ZFCP_LOG_LEVEL_INFO
#define ZFCP_LOG_LEVEL_DEFAULT_FSF	ZFCP_LOG_LEVEL_INFO
#define ZFCP_LOG_LEVEL_DEFAULT_CONFIG	ZFCP_LOG_LEVEL_INFO
#define ZFCP_LOG_LEVEL_DEFAULT_CIO	ZFCP_LOG_LEVEL_INFO
#define ZFCP_LOG_LEVEL_DEFAULT_QDIO	ZFCP_LOG_LEVEL_INFO
#define ZFCP_LOG_LEVEL_DEFAULT_ERP	ZFCP_LOG_LEVEL_INFO
#define ZFCP_LOG_LEVEL_DEFAULT_FC	ZFCP_LOG_LEVEL_INFO

/*
 * this allows removal of logging code by the preprocessor
 * (the most detailed log level still to be compiled in is specified, 
 * higher log levels are removed)
 */
#define ZFCP_LOG_LEVEL_LIMIT	ZFCP_LOG_LEVEL_TRACE

/* positional "loglevel" nibble assignment */
#define ZFCP_LOG_VALUE(zfcp_lognibble) \
	       ((atomic_read(&zfcp_data.loglevel) >> (zfcp_lognibble<<2)) & 0xF)

#define ZFCP_LOG_VALUE_OTHER	ZFCP_LOG_VALUE(ZFCP_LOG_AREA_OTHER)
#define ZFCP_LOG_VALUE_SCSI	ZFCP_LOG_VALUE(ZFCP_LOG_AREA_SCSI)
#define ZFCP_LOG_VALUE_FSF	ZFCP_LOG_VALUE(ZFCP_LOG_AREA_FSF)
#define ZFCP_LOG_VALUE_CONFIG	ZFCP_LOG_VALUE(ZFCP_LOG_AREA_CONFIG)
#define ZFCP_LOG_VALUE_CIO	ZFCP_LOG_VALUE(ZFCP_LOG_AREA_CIO)
#define ZFCP_LOG_VALUE_QDIO	ZFCP_LOG_VALUE(ZFCP_LOG_AREA_QDIO)
#define ZFCP_LOG_VALUE_ERP	ZFCP_LOG_VALUE(ZFCP_LOG_AREA_ERP)
#define ZFCP_LOG_VALUE_FC	ZFCP_LOG_VALUE(ZFCP_LOG_AREA_FC)

/* all log-level defaults are combined to generate initial log-level */
#define ZFCP_LOG_LEVEL_DEFAULTS \
	((ZFCP_LOG_LEVEL_DEFAULT_OTHER	<< (ZFCP_LOG_AREA_OTHER<<2))	| \
	 (ZFCP_LOG_LEVEL_DEFAULT_SCSI	<< (ZFCP_LOG_AREA_SCSI<<2))	| \
	 (ZFCP_LOG_LEVEL_DEFAULT_FSF	<< (ZFCP_LOG_AREA_FSF<<2))	| \
	 (ZFCP_LOG_LEVEL_DEFAULT_CONFIG	<< (ZFCP_LOG_AREA_CONFIG<<2))	| \
	 (ZFCP_LOG_LEVEL_DEFAULT_CIO	<< (ZFCP_LOG_AREA_CIO<<2))	| \
	 (ZFCP_LOG_LEVEL_DEFAULT_QDIO	<< (ZFCP_LOG_AREA_QDIO<<2))	| \
	 (ZFCP_LOG_LEVEL_DEFAULT_ERP	<< (ZFCP_LOG_AREA_ERP<<2))      | \
	 (ZFCP_LOG_LEVEL_DEFAULT_FC	<< (ZFCP_LOG_AREA_FC<<2)))

/* the prefix placed at the beginning of each driver message */
#define ZFCP_LOG_PREFIX ZFCP_NAME": "

/* log area specific prefixes */
#define ZFCP_LOG_AREA_PREFIX_OTHER	""
#define ZFCP_LOG_AREA_PREFIX_SCSI	"SCSI: "
#define ZFCP_LOG_AREA_PREFIX_FSF	"FSF: "
#define ZFCP_LOG_AREA_PREFIX_CONFIG	"config: "
#define ZFCP_LOG_AREA_PREFIX_CIO	"common I/O: "
#define ZFCP_LOG_AREA_PREFIX_QDIO	"QDIO: "
#define ZFCP_LOG_AREA_PREFIX_ERP	"ERP: "
#define ZFCP_LOG_AREA_PREFIX_FC 	"FC: "

/* check whether we have the right level for logging */
#define ZFCP_LOG_CHECK(ll)	(ZFCP_LOG_VALUE(ZFCP_LOG_AREA)) >= ll

/* As we have two printks it is possible for them to be seperated by another
 * message. This holds true even for printks from within this module.
 * In any case there should only be a small readability hit, however.
 */
#define _ZFCP_LOG(m...) \
		{ \
			printk( "%s%s: ", \
				ZFCP_LOG_PREFIX ZFCP_LOG_AREA_PREFIX, \
				__FUNCTION__); \
			printk(m); \
		}

#define ZFCP_LOG(ll, m...) \
		if (ZFCP_LOG_CHECK(ll)) \
			_ZFCP_LOG(m)
	
#if ZFCP_LOG_LEVEL_LIMIT < ZFCP_LOG_LEVEL_NORMAL
#define ZFCP_LOG_NORMAL(m...)
#else	/* ZFCP_LOG_LEVEL_LIMIT >= ZFCP_LOG_LEVEL_NORMAL */
#define ZFCP_LOG_NORMAL(m...)		ZFCP_LOG(ZFCP_LOG_LEVEL_NORMAL, m)
#endif

#if ZFCP_LOG_LEVEL_LIMIT < ZFCP_LOG_LEVEL_INFO
#define ZFCP_LOG_INFO(m...)
#else	/* ZFCP_LOG_LEVEL_LIMIT >= ZFCP_LOG_LEVEL_INFO */
#define ZFCP_LOG_INFO(m...)		ZFCP_LOG(ZFCP_LOG_LEVEL_INFO, m)
#endif

#if ZFCP_LOG_LEVEL_LIMIT < ZFCP_LOG_LEVEL_DEBUG
#define ZFCP_LOG_DEBUG(m...)
#else	/* ZFCP_LOG_LEVEL_LIMIT >= ZFCP_LOG_LEVEL_DEBUG */
#define ZFCP_LOG_DEBUG(m...)		ZFCP_LOG(ZFCP_LOG_LEVEL_DEBUG, m)
#endif

#if ZFCP_LOG_LEVEL_LIMIT < ZFCP_LOG_LEVEL_TRACE
#define ZFCP_LOG_TRACE(m...)
#else	/* ZFCP_LOG_LEVEL_LIMIT >= ZFCP_LOG_LEVEL_TRACE */
#define ZFCP_LOG_TRACE(m...)		ZFCP_LOG(ZFCP_LOG_LEVEL_TRACE, m)
#endif

#ifdef ZFCP_PRINT_FLAGS
extern u32 flags_dump;
#define ZFCP_LOG_FLAGS(ll, m...) \
		if (ll<=flags_dump) \
			_ZFCP_LOG(m)
#else
#define ZFCP_LOG_FLAGS(ll, m...)
#endif

/*************** ADAPTER/PORT/UNIT AND FSF_REQ STATUS FLAGS ******************/

/* 
 * Note, the leftmost status byte is common among adapter, port 
 * and unit
 */
#define ZFCP_COMMON_FLAGS                       0xff000000
#define ZFCP_SPECIFIC_FLAGS                     0x00ffffff

/* common status bits */
#define ZFCP_STATUS_COMMON_REMOVE		0x80000000
#define ZFCP_STATUS_COMMON_RUNNING		0x40000000
#define ZFCP_STATUS_COMMON_ERP_FAILED		0x20000000
#define ZFCP_STATUS_COMMON_UNBLOCKED		0x10000000
#define ZFCP_STATUS_COMMON_OPENING              0x08000000
#define ZFCP_STATUS_COMMON_OPEN                 0x04000000
#define ZFCP_STATUS_COMMON_CLOSING              0x02000000
#define ZFCP_STATUS_COMMON_ERP_INUSE		0x01000000

/* adapter status */
#define ZFCP_STATUS_ADAPTER_QDIOUP		0x00000002
#define ZFCP_STATUS_ADAPTER_REGISTERED		0x00000004
#define ZFCP_STATUS_ADAPTER_XCONFIG_OK		0x00000008
#define ZFCP_STATUS_ADAPTER_HOST_CON_INIT	0x00000010
#define ZFCP_STATUS_ADAPTER_ERP_THREAD_UP	0x00000020
#define ZFCP_STATUS_ADAPTER_ERP_THREAD_KILL	0x00000080
#define ZFCP_STATUS_ADAPTER_ERP_PENDING		0x00000100
#define ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED	0x00000200
#define ZFCP_STATUS_ADAPTER_QUEUECOMMAND_BLOCK	0x00000400 

#define ZFCP_STATUS_ADAPTER_SCSI_UP			\
		(ZFCP_STATUS_COMMON_UNBLOCKED |	\
		 ZFCP_STATUS_ADAPTER_REGISTERED)


#define ZFCP_DID_NAMESERVER			0xFFFFFC

/* remote port status */
#define ZFCP_STATUS_PORT_PHYS_OPEN		0x00000001
#define ZFCP_STATUS_PORT_DID_DID		0x00000002
#define ZFCP_STATUS_PORT_PHYS_CLOSING		0x00000004
#define ZFCP_STATUS_PORT_NO_WWPN		0x00000008
#define ZFCP_STATUS_PORT_NO_SCSI_ID		0x00000010
#define ZFCP_STATUS_PORT_INVALID_WWPN		0x00000020

#define ZFCP_STATUS_PORT_NAMESERVER \
		(ZFCP_STATUS_PORT_NO_WWPN | \
		 ZFCP_STATUS_PORT_NO_SCSI_ID)

/* logical unit status */
#define ZFCP_STATUS_UNIT_NOTSUPPUNITRESET	0x00000001


/* FSF request status (this does not have a common part) */
#define ZFCP_STATUS_FSFREQ_NOT_INIT		0x00000000
#define ZFCP_STATUS_FSFREQ_POOL  		0x00000001
#define ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT	0x00000002
#define ZFCP_STATUS_FSFREQ_COMPLETED		0x00000004
#define ZFCP_STATUS_FSFREQ_ERROR		0x00000008
#define ZFCP_STATUS_FSFREQ_CLEANUP		0x00000010
#define ZFCP_STATUS_FSFREQ_ABORTING		0x00000020
#define ZFCP_STATUS_FSFREQ_ABORTSUCCEEDED	0x00000040
#define ZFCP_STATUS_FSFREQ_ABORTNOTNEEDED       0x00000080
#define ZFCP_STATUS_FSFREQ_ABORTED              0x00000100
#define ZFCP_STATUS_FSFREQ_TMFUNCFAILED         0x00000200
#define ZFCP_STATUS_FSFREQ_TMFUNCNOTSUPP        0x00000400
#define ZFCP_STATUS_FSFREQ_RETRY                0x00000800
#define ZFCP_STATUS_FSFREQ_DISMISSED            0x00001000
#define ZFCP_STATUS_FSFREQ_POOLBUF              0x00002000

/*********************** ERROR RECOVERY PROCEDURE DEFINES ********************/

#define ZFCP_MAX_ERPS                   3

#define ZFCP_ERP_FSFREQ_TIMEOUT		(100 * HZ)
#define ZFCP_ERP_MEMWAIT_TIMEOUT	HZ

#define ZFCP_STATUS_ERP_TIMEDOUT	0x10000000
#define ZFCP_STATUS_ERP_CLOSE_ONLY	0x01000000
#define ZFCP_STATUS_ERP_DISMISSING	0x00100000
#define ZFCP_STATUS_ERP_DISMISSED	0x00200000

#define ZFCP_ERP_STEP_UNINITIALIZED	0x00000000
#define ZFCP_ERP_STEP_FSF_XCONFIG	0x00000001
#define ZFCP_ERP_STEP_PHYS_PORT_CLOSING	0x00000010
#define ZFCP_ERP_STEP_PORT_CLOSING	0x00000100
#define ZFCP_ERP_STEP_NAMESERVER_OPEN	0x00000200
#define ZFCP_ERP_STEP_NAMESERVER_LOOKUP	0x00000400
#define ZFCP_ERP_STEP_PORT_OPENING	0x00000800
#define ZFCP_ERP_STEP_UNIT_CLOSING	0x00001000
#define ZFCP_ERP_STEP_UNIT_OPENING	0x00002000

/* Ordered by escalation level (necessary for proper erp-code operation) */
#define ZFCP_ERP_ACTION_REOPEN_ADAPTER		0x4
#define ZFCP_ERP_ACTION_REOPEN_PORT_FORCED	0x3
#define ZFCP_ERP_ACTION_REOPEN_PORT		0x2
#define ZFCP_ERP_ACTION_REOPEN_UNIT		0x1

#define ZFCP_ERP_ACTION_RUNNING			0x1
#define ZFCP_ERP_ACTION_READY			0x2

#define ZFCP_ERP_SUCCEEDED	0x0
#define ZFCP_ERP_FAILED		0x1
#define ZFCP_ERP_CONTINUES	0x2
#define ZFCP_ERP_EXIT		0x3
#define ZFCP_ERP_DISMISSED	0x4
#define ZFCP_ERP_NOMEM		0x5

/************************* STRUCTURE DEFINITIONS *****************************/


struct zfcp_fsf_req;
typedef void zfcp_send_generic_handler_t(struct zfcp_fsf_req*);

struct zfcp_adapter_mempool {
        mempool_t *status_read_fsf;
	mempool_t *status_read_buf;
        mempool_t *nameserver;
        mempool_t *erp_fsf;
        mempool_t *fcp_command_fsf;
        struct timer_list fcp_command_fsf_timer;
};

struct  zfcp_exchange_config_data{
};

struct zfcp_open_port {
        struct zfcp_port *port;
};

struct zfcp_close_port {
	struct zfcp_port *port;
};

struct zfcp_open_unit {
	struct zfcp_unit *unit;
};

struct zfcp_close_unit {
	struct zfcp_unit *unit;
};

struct zfcp_close_physical_port {
        struct zfcp_port *port;
};

struct zfcp_send_fcp_command_task {
	struct zfcp_fsf_req *fsf_req;
	struct zfcp_unit *unit;
 	Scsi_Cmnd *scsi_cmnd;
	unsigned long start_jiffies;
};

struct zfcp_send_fcp_command_task_management {
	struct zfcp_unit *unit;
};

struct zfcp_abort_fcp_command {
	struct zfcp_fsf_req *fsf_req;
	struct zfcp_unit *unit;
};

struct zfcp_send_generic {
        struct zfcp_port *port;
	char *outbuf;
	char *inbuf;
	int outbuf_length;
	int inbuf_length;
	zfcp_send_generic_handler_t *handler;
	unsigned long handler_data;
};

struct zfcp_status_read {
	struct fsf_status_read_buffer *buffer;
};

/* request specific data */
union zfcp_req_data {
	struct zfcp_exchange_config_data exchange_config_data;
	struct zfcp_open_port		  open_port;
	struct zfcp_close_port		  close_port;
	struct zfcp_open_unit		  open_unit;
	struct zfcp_close_unit		  close_unit;
	struct zfcp_close_physical_port	  close_physical_port;
	struct zfcp_send_fcp_command_task send_fcp_command_task;
        struct zfcp_send_fcp_command_task_management
					  send_fcp_command_task_management;
	struct zfcp_abort_fcp_command	  abort_fcp_command;
	struct zfcp_send_generic	  send_generic;
	struct zfcp_status_read 	  status_read;
};

struct zfcp_qdio_queue {
	struct qdio_buffer *buffer[QDIO_MAX_BUFFERS_PER_Q]; /* SBALs */
	u8		   free_index;	      /* index of next free bfr
						 in queue (free_count>0) */
	atomic_t           free_count;	      /* number of free buffers
						 in queue */
	rwlock_t	   queue_lock;	      /* lock for operations on queue */
        int                distance_from_int; /* SBALs used since PCI indication
						 was last set */
};

struct zfcp_erp_action {
	struct list_head list;
	int action;	              /* requested action code */
	struct zfcp_adapter *adapter; /* device which should be recovered */
	struct zfcp_port *port;
	struct zfcp_unit *unit;
	volatile u32 status;	      /* recovery status */
	u32 step;	              /* active step of this erp action */
	struct zfcp_fsf_req *fsf_req; /* fsf request currently pending
					 for this action */
	struct timer_list timer;
};


struct zfcp_adapter {
	u32			common_magic;	   /* driver common magic */
	u32			specific_magic;	   /* struct specific magic */
	struct list_head	list;              /* list of adapters */
	atomic_t                refcount;          /* reference count */
	wait_queue_head_t	remove_wq;         /* can be used to wait for
						      refcount drop to zero */
	wwn_t			wwnn;	           /* WWNN */
	wwn_t			wwpn;	           /* WWPN */
	fc_id_t			s_id;	           /* N_Port ID */
	struct ccw_device       *ccw_device;	   /* S/390 ccw device */
	u8			fc_service_class;
	u32			fc_topology;	   /* FC topology */
	u32			fc_link_speed;	   /* FC interface speed */
	u32			hydra_version;	   /* Hydra version */
	u32			fsf_lic_version;
	struct Scsi_Host	*scsi_host;	   /* Pointer to mid-layer */
        Scsi_Cmnd               *first_fake_cmnd;  /* Packets in flight list */
	rwlock_t		fake_list_lock;    /* Lock for the above */
	struct timer_list       fake_scsi_timer;   /* Starts processing of
						      faked commands */
	unsigned char		name[9];
	struct list_head	port_list_head;	   /* remote port list */
	struct list_head        port_remove_lh;    /* head of ports to be
						      removed */
	u32			ports;	           /* number of remote ports */
	scsi_id_t		max_scsi_id;	   /* largest SCSI ID */
	scsi_lun_t		max_scsi_lun;	   /* largest SCSI LUN */
        struct timer_list       scsi_er_timer;     /* SCSI err recovery watch */
	struct list_head	fsf_req_list_head; /* head of FSF req list */
	rwlock_t		fsf_req_list_lock; /* lock for ops on list of
						      FSF requests */
        atomic_t       		fsf_reqs_active;   /* # active FSF reqs */
        atomic_t       		scsi_reqs_active;  /* # active SCSI reqs */
	wait_queue_head_t	scsi_reqs_active_wq; /* can be used to wait for
							fsf_reqs_active chngs */
	struct zfcp_qdio_queue	request_queue;	   /* request queue */
	u32			fsf_req_seq_no;	   /* FSF cmnd seq number */
	wait_queue_head_t	request_wq;	   /* can be used to wait for
						      more avaliable SBALs */
	struct zfcp_qdio_queue	response_queue;	   /* response queue */
	rwlock_t		abort_lock;        /* Protects against SCSI
						      stack abort/command
						      completion races */
	u16			status_read_failed; /* # failed status reads */
	atomic_t		status;	           /* status of this adapter */
	struct list_head	erp_ready_head;	   /* error recovery for this
						      adapter/devices */
	struct list_head	erp_running_head;
	rwlock_t		erp_lock;
	struct semaphore	erp_ready_sem;
	wait_queue_head_t	erp_thread_wqh;
	wait_queue_head_t	erp_done_wqh;
	struct zfcp_erp_action	erp_action;	   /* pending error recovery */
        atomic_t                erp_counter;
	struct zfcp_port	*nameserver_port;  /* adapter's nameserver */
        debug_info_t            *erp_dbf;          /* S/390 debug features */
	debug_info_t            *abort_dbf;
	debug_info_t            *req_dbf;
	debug_info_t            *in_els_dbf;
	debug_info_t            *cmd_dbf;
	rwlock_t                cmd_dbf_lock;
	struct zfcp_adapter_mempool	pool;      /* Adapter memory pools */
	struct qdio_initialize  qdio_init_data;    /* for qdio_establish */
};

struct zfcp_port {
	u32		       common_magic;   /* driver wide common magic */
	u32		       specific_magic; /* structure specific magic */
	struct list_head       list;	       /* list of remote ports */
	atomic_t               refcount;       /* reference count */
	wait_queue_head_t      remove_wq;      /* can be used to wait for
						  refcount drop to zero */
	struct zfcp_adapter    *adapter;       /* adapter used to access port */
	struct list_head       unit_list_head; /* head of logical unit list */
	struct list_head       unit_remove_lh; /* head of luns to be removed
						  list */
	u32		       units;	       /* # of logical units in list */
	atomic_t	       status;	       /* status of this remote port */
	scsi_id_t	       scsi_id;	       /* own SCSI ID */
	wwn_t		       wwnn;	       /* WWNN if known */
	wwn_t		       wwpn;	       /* WWPN */
	fc_id_t		       d_id;	       /* D_ID */
	scsi_lun_t	       max_scsi_lun;   /* largest SCSI LUN */
	u32		       handle;	       /* handle assigned by FSF */
	struct zfcp_erp_action erp_action;     /* pending error recovery */
        atomic_t               erp_counter;
	struct device          sysfs_device;   /* sysfs device */
};

struct zfcp_unit {
	u32		       common_magic;   /* driver wide common magic */
	u32		       specific_magic; /* structure specific magic */
	struct list_head       list;	       /* list of logical units */
	atomic_t               refcount;       /* reference count */
	wait_queue_head_t      remove_wq;      /* can be used to wait for
						  refcount drop to zero */
	struct zfcp_port       *port;	       /* remote port of unit */
	atomic_t	       status;	       /* status of this logical unit */
	scsi_lun_t	       scsi_lun;       /* own SCSI LUN */
	fcp_lun_t	       fcp_lun;	       /* own FCP_LUN */
	u32		       handle;	       /* handle assigned by FSF */
        Scsi_Device            *device;        /* scsi device struct pointer */
	struct zfcp_erp_action erp_action;     /* pending error recovery */
        atomic_t               erp_counter;
	struct device          sysfs_device;   /* sysfs device */
};

/* FSF request */
struct zfcp_fsf_req {
	u32		       common_magic;   /* driver wide common magic */
	u32		       specific_magic; /* structure specific magic */
	struct list_head       list;	       /* list of FSF requests */
	struct zfcp_adapter    *adapter;       /* adapter request belongs to */
	u8		       sbal_count;     /* # of SBALs in FSF request */
	u8		       sbal_index;     /* position of 1st SBAL */
	wait_queue_head_t      completion_wq;  /* can be used by a routine
						  to wait for completion */
	volatile u32	       status;	       /* status of this request */
	u32		       fsf_command;    /* FSF Command copy */
	struct fsf_qtcb	       *qtcb;	       /* address of associated QTCB */
	u32		       seq_no;         /* Sequence number of request */
        union zfcp_req_data    data;           /* Info fields of request */ 
	struct zfcp_erp_action *erp_action;    /* used if this request is
						  issued on behalf of erp */
};

typedef void zfcp_fsf_req_handler_t(struct zfcp_fsf_req*);

/* driver data */
struct zfcp_data {
	Scsi_Host_Template	scsi_host_template;
        atomic_t                status;             /* Module status flags */
	struct list_head	adapter_list_head;  /* head of adapter list */
	struct list_head	adapter_remove_lh;  /* head of adapters to be
						       removed */
        rwlock_t                status_read_lock;   /* for status read thread */
        struct list_head        status_read_receive_head;
        struct list_head        status_read_send_head;
        struct semaphore        status_read_sema;
	wait_queue_head_t	status_read_thread_wqh;
	u16			adapters;	    /* # of adapters in list */
	rwlock_t                config_lock;        /* serialises changes
						       to adapter/port/unit
						       lists */
	struct semaphore        config_sema;        /* serialises configuration
						       changes */
	struct notifier_block	reboot_notifier;     /* used to register cleanup
							functions */
	atomic_t		loglevel;            /* current loglevel */
#ifdef ZFCP_STAT_REQSIZES                            /* Statistical accounting
							of processed data */
	struct list_head	read_req_head;
	struct list_head	write_req_head;
	struct list_head	read_sg_head;
	struct list_head	write_sg_head;
	struct list_head	read_sguse_head;
	struct list_head	write_sguse_head;
	unsigned long		stat_errors;
	rwlock_t		stat_lock;
#endif
#ifdef ZFCP_STAT_QUEUES
        atomic_t                outbound_queue_full;
	atomic_t		outbound_total;
#endif
};

#ifdef ZFCP_STAT_REQSIZES
struct zfcp_statistics {
        struct list_head list;
        u32 num;
        u32 occurrence;
};
#endif

/********************** ZFCP SPECIFIC DEFINES ********************************/

#define ZFCP_FSFREQ_CLEANUP_TIMEOUT	HZ/10

#define ZFCP_KNOWN              0x00000001
#define ZFCP_REQ_AUTO_CLEANUP	0x00000002
#define ZFCP_WAIT_FOR_SBAL	0x00000004

#define ZFCP_SET                0x00000100
#define ZFCP_CLEAR              0x00000200

#define ZFCP_INTERRUPTIBLE	1
#define ZFCP_UNINTERRUPTIBLE	0

/* some magics which may be used to authenticate data structures */
#define ZFCP_MAGIC		0xFCFCFCFC
#define ZFCP_MAGIC_ADAPTER	0xAAAAAAAA
#define ZFCP_MAGIC_PORT		0xBBBBBBBB
#define ZFCP_MAGIC_UNIT		0xCCCCCCCC
#define ZFCP_MAGIC_FSFREQ	0xEEEEEEEE

#ifndef atomic_test_mask
#define atomic_test_mask(mask, target) \
           (atomic_read(target) & mask)
#endif

extern void _zfcp_hex_dump(char *, int);
#define ZFCP_HEX_DUMP(level, addr, count) \
		if (ZFCP_LOG_CHECK(level)) { \
			_zfcp_hex_dump(addr, count); \
		}
/*
 * Not yet optimal but useful:
 * Waits until the condition is met or the timeout occurs.
 * The condition may be a function call. This allows to
 * execute some additional instructions in addition
 * to a simple condition check.
 * The timeout is modified on exit and holds the remaining time.
 * Thus it is zero if a timeout ocurred, i.e. the condition was 
 * not met in the specified interval.
 */
#define __ZFCP_WAIT_EVENT_TIMEOUT(timeout, condition) \
do { \
	set_current_state(TASK_UNINTERRUPTIBLE); \
	while (!(condition) && timeout) \
		timeout = schedule_timeout(timeout); \
	current->state = TASK_RUNNING; \
} while (0);

#define ZFCP_WAIT_EVENT_TIMEOUT(waitqueue, timeout, condition) \
do { \
	wait_queue_t entry; \
	init_waitqueue_entry(&entry, current); \
	add_wait_queue(&waitqueue, &entry); \
	__ZFCP_WAIT_EVENT_TIMEOUT(timeout, condition) \
	remove_wait_queue(&waitqueue, &entry); \
} while (0);

#define zfcp_get_busid_by_adapter(adapter) (adapter->ccw_device->dev.bus_id)
#define zfcp_get_busid_by_port(port) (zfcp_get_busid_by_adapter(port->adapter))
#define zfcp_get_busid_by_unit(unit) (zfcp_get_busid_by_port(unit->port))

/*
 *  functions needed for reference/usage counting
 */

static inline void
zfcp_unit_get(struct zfcp_unit *unit)
{
	atomic_inc(&unit->refcount);
}

static inline void
zfcp_unit_put(struct zfcp_unit *unit)
{
	if (atomic_dec_return(&unit->refcount) == 0)
		wake_up(&unit->remove_wq);
}

static inline void
zfcp_unit_wait(struct zfcp_unit *unit)
{
	wait_event(unit->remove_wq, atomic_read(&unit->refcount) == 0);
}

static inline void
zfcp_port_get(struct zfcp_port *port)
{
	atomic_inc(&port->refcount);
}

static inline void
zfcp_port_put(struct zfcp_port *port)
{
	if (atomic_dec_return(&port->refcount) == 0)
		wake_up(&port->remove_wq);
}

static inline void
zfcp_port_wait(struct zfcp_port *port)
{
	wait_event(port->remove_wq, atomic_read(&port->refcount) == 0);
}

static inline void
zfcp_adapter_get(struct zfcp_adapter *adapter)
{
	atomic_inc(&adapter->refcount);
}

static inline void
zfcp_adapter_put(struct zfcp_adapter *adapter)
{
	if (atomic_dec_return(&adapter->refcount) == 0)
		wake_up(&adapter->remove_wq);
}

static inline void
zfcp_adapter_wait(struct zfcp_adapter *adapter)
{
	wait_event(adapter->remove_wq, atomic_read(&adapter->refcount) == 0);
}

#endif /* __KERNEL_- */
#endif /* ZFCP_DEF_H */
