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

#ifndef _H_ELX_SCSI
#define _H_ELX_SCSI

/*
 * SCSI node structure for each open Fibre Channel node
 * used by scsi transport.
 */

typedef struct elxScsiTarget {
	elxHBA_t *pHba;		/* adapter structure ptr */
	ELX_SLINK_t lunlist;
	void *pcontext;		/* ELX_NODELIST_t * for device */
	void *tmofunc;
	void *rptlunfunc;
	ELX_SCHED_TARGET_t targetSched;	/* Scheduling Info for this target */

	uint16_t scsi_cap;
	uint16_t scsi_sync;
	uint32_t scsi_options;
	uint16_t max_lun;	/* max lun supported */
	uint16_t scsi_id;	/* SCSI ID of this device */

	uint16_t rpi;

	uint16_t targetFlags;
#define FC_NODEV_TMO        0x1	/* nodev-tmo tmr started and expired */
#define FC_FCP2_RECOVERY    0x2	/* set FCP2 Recovery for commands */
#define FC_RETRY_RPTLUN     0x4	/* Report Lun has been retried */
#define FC_NPR_ACTIVE       0x10	/* NPort Recovery active */

	uint16_t addrMode;	/* SCSI address method */
#define PERIPHERAL_DEVICE_ADDRESSING    0
#define VOLUME_SET_ADDRESSING           1
#define LOGICAL_UNIT_ADDRESSING         2

	uint16_t rptLunState;	/* For report lun SCSI command */
#define REPORT_LUN_REQUIRED     0
#define REPORT_LUN_ONGOING      1
#define REPORT_LUN_COMPLETE     2
#define REPORT_LUN_ERRORED      3

	DMABUF_t *RptLunData;

	void *pTargetProto;	/* target struc for driver type */
	void *pTargetOSEnv;

	uint32_t node_flag;	/* match node on WWPN WWNN or DID */
	union {
		uint32_t dev_did;	/* SCSI did */
	} un;

} ELXSCSITARGET_t;

#define MAX_ELX_SNS       128
#define ELX_SCSI_BUF_SZ   1024	/* used for driver generated scsi cmds */
#define ELX_INQSN_SZ      64	/* Max size of Inquiry serial number */

struct fcPathId;
struct fcRouteId;

struct elxScsiLun {
	ELX_NODELIST_t *pnode;	/* Pointer to the node structure. */
	elxHBA_t *pHBA;		/* Pointer to the HBA with
				   which this LUN is
				   associated. */
	ELXSCSITARGET_t *pTarget;	/* Pointer to the target structure */
	struct elxScsiLun *pnextLun;	/* Used for list of LUNs on this node */

	uint64_t lun_id;	/* LUN ID of this device */

	uint8_t first_check;	/* flag for first check condition */
#define FIRST_CHECK_COND        0x1
#define FIRST_IO                0x2

	uint8_t opened;
	uint8_t ioctl_wakeup;	/* wakeup sleeping ioctl call */
	uint32_t ioctl_event;
	uint32_t ioctl_errno;
	uint32_t stop_event;

	ELX_SLINK_t lun_waiting;	/* luns waiting for us to send io to */
	ELX_DLINK_t lun_abort_bdr;	/* luns waiting for abdr */

	/* list of dma-able memory to be used for fcp_cmd and fcp_rsp */
	ELX_SLINK_t scsi_buf_list;

	uint32_t qfullcnt;
	uint32_t qcmdcnt;
	uint32_t iodonecnt;
	uint32_t errorcnt;

	void *pLunOSEnv;

	/*
	 *  A command lives in a pending queue until it is sent to the HBA.
	 *  Throttling constraints apply:
	 *          No more than N commands total to a single target
	 *          No more than M commands total to a single LUN on that target
	 *
	 *  A command that has left the pending queue and been sent to the HBA
	 *  is an "underway" command.  We count underway commands, per-LUN,
	 *  to obey the LUN throttling constraint.
	 *
	 *  Because we only allocate enough fc_buf_t structures to handle N
	 *  commands, per target, we implicitly obey the target throttling
	 *  constraint by being unable to send a command when we run out of
	 *  free fc_buf_t structures.
	 *
	 *  We count the number of pending commands to determine whether the
	 *  target has I/O to be issued at all.
	 *
	 *  We use next_pending to rotor through the LUNs, issuing one I/O at
	 *  a time for each LUN.  This mechanism guarantees a fair distribution
	 *  of I/Os across LUNs in the face of a target queue_depth lower than
	 *  #LUNs*fcp_lun_queue_depth.
	 */

	ELX_SCHED_LUN_t lunSched;	/* Used to schedule I/O to HBA */
	uint16_t fcp_lun_queue_depth;	/* maximum # cmds to each lun */
	uint8_t stop_send_io;	/* stop sending any io to this dev */
	uint8_t lunQState;	/* device general queue state */
#define ACTIVE                  0
#define STOPPING                1
#define HALTED                  2
#define RESTART_WHEN_READY      3
#define ACTIVE_PASSTHRU         4
#define WAIT_RESUME             8
#define WAIT_INFO               10
#define WAIT_ACA                11
#define WAIT_FLUSH              12
#define WAIT_HEAD_RESUME        13

	uint32_t lunFlag;	/* flags for the drive */
#define SCSI_TQ_HALTED        0x0001	/* The transaction Q is halted */
#define SCSI_TQ_CLEARING      0x0002	/* The transaction Q is clearing */
#define SCSI_TQ_CLEAR_ACA     0x0004	/* a CLEAR_ACA is PENDING      */
#define SCSI_LUN_RESET        0x0008	/* sent LUN_RESET not of TARGET_RESET */
#define SCSI_ABORT_TSET       0x0010	/* BDR requested but not yet sent */
#define SCSI_TARGET_RESET     0x0020	/* a SCSI BDR is active for device */
#define CHK_SCSI_ABDR         0x0038	/* value used to check tm flags */
#define QUEUED_FOR_ABDR       0x0040	/* dev_ptr is on ABORT_BDR queue */
#define NORPI_RESET_DONE      0x0100	/* BOGUS_RPI Bus Reset attempted */
#define LUN_BLOCKED           0x0200	/* if flag is set, this lun has been blocked */
#define SCSI_IOCTL_INPROGRESS 0x0400	/* An ioctl is in progress  */
#define SCSI_BUMP_QDEPTH      0x0800	/* bump qdepth to max after cmpl */
#define SCSI_SEND_INQUIRY_SN  0x1000	/* Serial number inq should be sent */
#define SCSI_INQUIRY_SN       0x2000	/* Serial number inq has been sent */
#define SCSI_INQUIRY_P0       0x4000	/* Page 0 inq has been sent */
#define SCSI_INQUIRY_CMD      0x6000	/* Serial number or Page 0 inq sent */
#define SCSI_P0_INFO          0x20000	/* device has good P0 info */

	uint8_t sense[MAX_ELX_SNS];	/* Temporary request sense buffer */
	uint8_t sense_valid;	/* flag to indicate new sense data */
	uint32_t sense_length;	/* new sense data length */

	uint16_t qfull_retries;	/* # of retries on qfull condition */
#define MAX_QFULL_RETRIES   255
#define MAX_QFULL_RETRY_INTERVAL 1000	/* 1000 (ms) */

	uint16_t qfull_retry_interval;	/* the interval for qfull retry */
	void *qfull_tmo_id;

	uint32_t failMask;	/* failure mask for device */

	uint8_t InquirySN[ELX_INQSN_SZ];	/* serial number from Inquiry */
	uint8_t Vendor[8];	/* From Page 0 Inquiry */
	uint8_t Product[16];	/* From Page 0 Inquiry */
	uint8_t Rev[4];		/* From Page 0 Inquiry */
	uint8_t sizeSN;		/* size of InquirySN */
};

typedef struct elxScsiLun ELXSCSILUN_t;

#define ELX_MIN_QFULL    1	/* lowest we can decrement throttle */

#define FCP_CONTINUE    0x01	/* flag for issue_fcp_cmd */
#define FCP_REQUEUE     0x02	/* flag for issue_fcp_cmd */
#define FCP_EXIT        0x04	/* flag for issue_fcp_cmd */

typedef struct _FCP_RSP {
	uint32_t rspRsvd1;	/* FC Word 0, byte 0:3 */
	uint32_t rspRsvd2;	/* FC Word 1, byte 0:3 */

	uint8_t rspStatus0;	/* FCP_STATUS byte 0 (reserved) */
	uint8_t rspStatus1;	/* FCP_STATUS byte 1 (reserved) */
	uint8_t rspStatus2;	/* FCP_STATUS byte 2 field validity */
#define RSP_LEN_VALID  0x01	/* bit 0 */
#define SNS_LEN_VALID  0x02	/* bit 1 */
#define RESID_OVER     0x04	/* bit 2 */
#define RESID_UNDER    0x08	/* bit 3 */
	uint8_t rspStatus3;	/* FCP_STATUS byte 3 SCSI status byte */
#define SCSI_STAT_GOOD        0x00
#define SCSI_STAT_CHECK_COND  0x02
#define SCSI_STAT_COND_MET    0x04
#define SCSI_STAT_BUSY        0x08
#define SCSI_STAT_INTERMED    0x10
#define SCSI_STAT_INTERMED_CM 0x14
#define SCSI_STAT_RES_CNFLCT  0x18
#define SCSI_STAT_CMD_TERM    0x22
#define SCSI_STAT_QUE_FULL    0x28

	uint32_t rspResId;	/* Residual xfer if residual count field set in fcpStatus2 */
	/* Received in Big Endian format */
	uint32_t rspSnsLen;	/* Length of sense data in fcpSnsInfo */
	/* Received in Big Endian format */
	uint32_t rspRspLen;	/* Length of FCP response data in fcpRspInfo */
	/* Received in Big Endian format */

	uint8_t rspInfo0;	/* FCP_RSP_INFO byte 0 (reserved) */
	uint8_t rspInfo1;	/* FCP_RSP_INFO byte 1 (reserved) */
	uint8_t rspInfo2;	/* FCP_RSP_INFO byte 2 (reserved) */
	uint8_t rspInfo3;	/* FCP_RSP_INFO RSP_CODE byte 3 */

#define RSP_NO_FAILURE       0x00
#define RSP_DATA_BURST_ERR   0x01
#define RSP_CMD_FIELD_ERR    0x02
#define RSP_RO_MISMATCH_ERR  0x03
#define RSP_TM_NOT_SUPPORTED 0x04	/* Task mgmt function not supported */
#define RSP_TM_NOT_COMPLETED 0x05	/* Task mgmt function not performed */

	uint32_t rspInfoRsvd;	/* FCP_RSP_INFO bytes 4-7 (reserved) */

	uint8_t rspSnsInfo[MAX_ELX_SNS];
#define SNS_ILLEGAL_REQ 0x05	/* sense key is byte 3 ([2]) */
#define SNSCOD_BADCMD 0x20	/* sense code is byte 13 ([12]) */
} FCP_RSP, *PFCP_RSP;

typedef struct _FCP_CMND {
	uint32_t fcpLunMsl;	/* most  significant lun word (32 bits) */
	uint32_t fcpLunLsl;	/* least significant lun word (32 bits) */
	/* # of bits to shift lun id to end up in right
	 * payload word, little endian = 8, big = 16.
	 */
#if LITTLE_ENDIAN_HW
#define FC_LUN_SHIFT         8
#define FC_ADDR_MODE_SHIFT   0
#endif
#if BIG_ENDIAN_HW
#define FC_LUN_SHIFT         16
#define FC_ADDR_MODE_SHIFT   24
#endif

	uint8_t fcpCntl0;	/* FCP_CNTL byte 0 (reserved) */
	uint8_t fcpCntl1;	/* FCP_CNTL byte 1 task codes */
#define  SIMPLE_Q        0x00
#define  HEAD_OF_Q       0x01
#define  ORDERED_Q       0x02
#define  ACA_Q           0x04
#define  UNTAGGED        0x05
	uint8_t fcpCntl2;	/* FCP_CTL byte 2 task management codes */
#define  ABORT_TASK_SET  0x02	/* Bit 1 */
#define  CLEAR_TASK_SET  0x04	/* bit 2 */
#define  BUS_RESET       0x08	/* bit 3 */
#define  LUN_RESET       0x10	/* bit 4 */
#define  TARGET_RESET    0x20	/* bit 5 */
#define  CLEAR_ACA       0x40	/* bit 6 */
#define  TERMINATE_TASK  0x80	/* bit 7 */
	uint8_t fcpCntl3;
#define  WRITE_DATA      0x01	/* Bit 0 */
#define  READ_DATA       0x02	/* Bit 1 */

	uint8_t fcpCdb[16];	/* SRB cdb field is copied here */
	uint32_t fcpDl;		/* Total transfer length */

} FCP_CMND, *PFCP_CMND;

/* SCSI INQUIRY Command Structure */

typedef struct inquiryDataType {
	uint8_t DeviceType:5;
	uint8_t DeviceTypeQualifier:3;

	uint8_t DeviceTypeModifier:7;
	uint8_t RemovableMedia:1;

	uint8_t Versions;
	uint8_t ResponseDataFormat;
	uint8_t AdditionalLength;
	uint8_t Reserved[2];

	uint8_t SoftReset:1;
	uint8_t CommandQueue:1;
	uint8_t Reserved2:1;
	uint8_t LinkedCommands:1;
	uint8_t Synchronous:1;
	uint8_t Wide16Bit:1;
	uint8_t Wide32Bit:1;
	uint8_t RelativeAddressing:1;

	uint8_t VendorId[8];
	uint8_t ProductId[16];
	uint8_t ProductRevisionLevel[4];
	uint8_t VendorSpecific[20];
	uint8_t Reserved3[40];
} INQUIRY_DATA_DEF;

typedef struct _READ_CAPACITY_DATA {
	uint32_t LogicalBlockAddress;
	uint32_t BytesPerBlock;
} READ_CAPACITY_DATA_DEF;

/* SCSI CDB command codes */
#define FCP_SCSI_FORMAT_UNIT                  0x04
#define FCP_SCSI_INQUIRY                      0x12
#define FCP_SCSI_MODE_SELECT                  0x15
#define FCP_SCSI_MODE_SENSE                   0x1A
#define FCP_SCSI_PAUSE_RESUME                 0x4B
#define FCP_SCSI_PLAY_AUDIO                   0x45
#define FCP_SCSI_PLAY_AUDIO_EXT               0xA5
#define FCP_SCSI_PLAY_AUDIO_MSF               0x47
#define FCP_SCSI_PLAY_AUDIO_TRK_INDX          0x48
#define FCP_SCSI_PREVENT_ALLOW_REMOVAL        0x1E
#define FCP_SCSI_READ                         0x08
#define FCP_SCSI_READ_BUFFER                  0x3C
#define FCP_SCSI_READ_CAPACITY                0x25
#define FCP_SCSI_READ_DEFECT_LIST             0x37
#define FCP_SCSI_READ_EXTENDED                0x28
#define FCP_SCSI_READ_HEADER                  0x44
#define FCP_SCSI_READ_LONG                    0xE8
#define FCP_SCSI_READ_SUB_CHANNEL             0x42
#define FCP_SCSI_READ_TOC                     0x43
#define FCP_SCSI_REASSIGN_BLOCK               0x07
#define FCP_SCSI_RECEIVE_DIAGNOSTIC_RESULTS   0x1C
#define FCP_SCSI_RELEASE_UNIT                 0x17
#define FCP_SCSI_REPORT_LUNS                  0xa0
#define FCP_SCSI_REQUEST_SENSE                0x03
#define FCP_SCSI_RESERVE_UNIT                 0x16
#define FCP_SCSI_REZERO_UNIT                  0x01
#define FCP_SCSI_SEEK                         0x0B
#define FCP_SCSI_SEEK_EXTENDED                0x2B
#define FCP_SCSI_SEND_DIAGNOSTIC              0x1D
#define FCP_SCSI_START_STOP_UNIT              0x1B
#define FCP_SCSI_TEST_UNIT_READY              0x00
#define FCP_SCSI_VERIFY                       0x2F
#define FCP_SCSI_WRITE                        0x0A
#define FCP_SCSI_WRITE_AND_VERIFY             0x2E
#define FCP_SCSI_WRITE_BUFFER                 0x3B
#define FCP_SCSI_WRITE_EXTENDED               0x2A
#define FCP_SCSI_WRITE_LONG                   0xEA
#define FCP_SCSI_RELEASE_LUNR                 0xBB
#define FCP_SCSI_RELEASE_LUNV                 0xBF

#define HPVA_SETPASSTHROUGHMODE               0x27
#define HPVA_EXECUTEPASSTHROUGH               0x29
#define HPVA_CREATELUN                        0xE2
#define HPVA_SETLUNSECURITYLIST               0xED
#define HPVA_SETCLOCK                         0xF9
#define HPVA_RECOVER                          0xFA
#define HPVA_GENERICSERVICEOUT                0xFD

#define DMEP_EXPORT_IN                        0x85
#define DMEP_EXPORT_OUT                       0x89

#define MDACIOCTL_DIRECT_CMD                  0x22
#define MDACIOCTL_STOREIMAGE                  0x2C
#define MDACIOCTL_WRITESIGNATURE              0xA6
#define MDACIOCTL_SETREALTIMECLOCK            0xAC
#define MDACIOCTL_PASS_THRU_CDB               0xAD
#define MDACIOCTL_PASS_THRU_INITIATE          0xAE
#define MDACIOCTL_CREATENEWCONF               0xC0
#define MDACIOCTL_ADDNEWCONF                  0xC4
#define MDACIOCTL_MORE                        0xC6
#define MDACIOCTL_SETPHYSDEVPARAMETER         0xC8
#define MDACIOCTL_SETLOGDEVPARAMETER          0xCF
#define MDACIOCTL_SETCONTROLLERPARAMETER      0xD1
#define MDACIOCTL_WRITESANMAP                 0xD4
#define MDACIOCTL_SETMACADDRESS               0xD5

struct elx_scsi_buf {
/*   ELX_DLINK_t          scsibuf_list;     replacing fc_fwd, fc_bkwd */
	ELX_SCHED_SCSI_BUF_t commandSched;	/* used by Scheduler */
	uint32_t scsitmo;	/* IN */
	uint32_t timeout;	/* IN */
	elxHBA_t *scsi_hba;	/* IN */
	uint8_t scsi_bus;	/* IN */
	uint16_t scsi_target;	/* IN */
	uint64_t scsi_lun;	/* IN */

	void *pOSCmd;		/* IN */

	uint32_t qfull_retry_count;	/* internal to scsi xport */
	uint16_t flags;		/* flags for this cmd */
#define DATA_MAPPED     0x0001	/* data buffer has been D_MAPed */
#define FCBUF_ABTS      0x0002	/* ABTS has been sent for this cmd */
#define FCBUF_ABTS2     0x0004	/* ABTS has been sent twice */
#define FCBUF_INTERNAL  0x0008	/* Internal generated driver command */
#define ELX_SCSI_ERR    0x0010
	uint16_t IOxri;		/* From IOCB Word 6- ulpContext */
	uint16_t status;	/* From IOCB Word 7- ulpStatus */
	uint32_t result;	/* From IOCB Word 4. */

	/*
	 * Define an OS-specific structure to capture the extra buffer
	 * pOSCmd requires of the driver.
	 */
	ELX_OS_IO_t OS_io_info;	/* bp, resid, cmd_flags, cmd_dmahandle etc. */

	ELXSCSILUN_t *pLun;

	/* dma_ext has both virt, phys to dma-able buffer
	 * which contains fcp_cmd, fcp_rsp and scatter gather list fro upto 
	 * 68 (ELX_SCSI_BPL_SIZE) BDE entries,
	 * xfer length, cdb, data direction....
	 */
	DMABUF_t *dma_ext;
	struct _FCP_CMND *fcp_cmnd;
	struct _FCP_RSP *fcp_rsp;
	ULP_BDE64 *fcp_bpl;

	/* cur_iocbq has phys of the dma-able buffer.
	 * Iotag is in here */
	ELX_IOCBQ_t cur_iocbq;

	void (*cmd_cmpl) (elxHBA_t *, struct elx_scsi_buf *);	/* IN */
};

typedef struct elx_scsi_buf ELX_SCSI_BUF_t;

#define ELX_SCSI_INITIAL_BPL_SIZE  65	/* Number of scsi buf BDEs in fcp_bpl */

#define FAILURE -1
#define ELX_CMD_STATUS_ABORTED -1

#define ELX_INTERNAL_RESET   0	/* internal reset */
#define ELX_EXTERNAL_RESET   1	/* external reset, scsi layer */
#define ELX_ISSUE_LUN_RESET  2	/* flag for reset routine to issue LUN_RESET */
#define ELX_ISSUE_ABORT_TSET 4	/* flag for reset routine to issue ABORT_TSET */

#endif				/* _H_ELX_SCSI */
