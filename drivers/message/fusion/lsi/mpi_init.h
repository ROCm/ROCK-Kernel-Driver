/*
 *  Copyright (c) 2000-2001 LSI Logic Corporation.
 *
 *
 *           Name:  MPI_INIT.H
 *          Title:  MPI initiator mode messages and structures
 *  Creation Date:  June 8, 2000
 *
 *    MPI Version:  01.01.03
 *
 *  Version History
 *  ---------------
 *
 *  Date      Version   Description
 *  --------  --------  ------------------------------------------------------
 *  05-08-00  00.10.01  Original release for 0.10 spec dated 4/26/2000.
 *  05-24-00  00.10.02  Added SenseBufferLength to _MSG_SCSI_IO_REPLY.
 *  06-06-00  01.00.01  Update version number for 1.0 release.
 *  06-08-00  01.00.02  Added MPI_SCSI_RSP_INFO_ definitions.
 *  11-02-00  01.01.01  Original release for post 1.0 work.
 *  12-04-00  01.01.02  Added MPI_SCSIIO_CONTROL_NO_DISCONNECT.
 *  02-20-01  01.01.03  Started using MPI_POINTER.
 *  --------------------------------------------------------------------------
 */

#ifndef MPI_INIT_H
#define MPI_INIT_H


/*****************************************************************************
*
*               S C S I    I n i t i a t o r    M e s s a g e s
*
*****************************************************************************/

/****************************************************************************/
/*  SCSI IO messages and assocaited structures                              */
/****************************************************************************/

typedef struct _MSG_SCSI_IO_REQUEST
{
    U8                      TargetID;
    U8                      Bus;
    U8                      ChainOffset;
    U8                      Function;
    U8                      CDBLength;
    U8                      SenseBufferLength;
    U8                      Reserved;
    U8                      MsgFlags;
    U32                     MsgContext;
    U8                      LUN[8];
    U32                     Control;
    U8                      CDB[16];
    U32                     DataLength;
    U32                     SenseBufferLowAddr;
    SGE_IO_UNION            SGL;
} MSG_SCSI_IO_REQUEST, MPI_POINTER PTR_MSG_SCSI_IO_REQUEST,
  SCSIIORequest_t, MPI_POINTER pSCSIIORequest_t;


/* SCSIO MsgFlags bits */

#define MPI_SCSIIO_MSGFLGS_SENSE_WIDTH          (0x01)
#define MPI_SCSIIO_MSGFLGS_SENSE_WIDTH_32       (0x00)
#define MPI_SCSIIO_MSGFLGS_SENSE_WIDTH_64       (0x01)
#define MPI_SCSIIO_MSGFLGS_SENSE_LOCATION       (0x02)
#define MPI_SCSIIO_MSGFLGS_SENSE_LOC_HOST       (0x00)
#define MPI_SCSIIO_MSGFLGS_SENSE_LOC_IOC        (0x02)

/* SCSIIO LUN fields */

#define MPI_SCSIIO_LUN_FIRST_LEVEL_ADDRESSING   (0x0000FFFF)
#define MPI_SCSIIO_LUN_SECOND_LEVEL_ADDRESSING  (0xFFFF0000)
#define MPI_SCSIIO_LUN_THIRD_LEVEL_ADDRESSING   (0x0000FFFF)
#define MPI_SCSIIO_LUN_FOURTH_LEVEL_ADDRESSING  (0xFFFF0000)
#define MPI_SCSIIO_LUN_LEVEL_1_WORD             (0xFF00)
#define MPI_SCSIIO_LUN_LEVEL_1_DWORD            (0x0000FF00)

/* SCSIO Control bits */

#define MPI_SCSIIO_CONTROL_DATADIRECTION_MASK   (0x03000000)
#define MPI_SCSIIO_CONTROL_NODATATRANSFER       (0x00000000)
#define MPI_SCSIIO_CONTROL_WRITE                (0x01000000)
#define MPI_SCSIIO_CONTROL_READ                 (0x02000000)

#define MPI_SCSIIO_CONTROL_ADDCDBLEN_MASK       (0x3C000000)
#define MPI_SCSIIO_CONTROL_ADDCDBLEN_SHIFT      (26)

#define MPI_SCSIIO_CONTROL_TASKATTRIBUTE_MASK   (0x00000700)
#define MPI_SCSIIO_CONTROL_SIMPLEQ              (0x00000000)
#define MPI_SCSIIO_CONTROL_HEADOFQ              (0x00000100)
#define MPI_SCSIIO_CONTROL_ORDEREDQ             (0x00000200)
#define MPI_SCSIIO_CONTROL_ACAQ                 (0x00000400)
#define MPI_SCSIIO_CONTROL_UNTAGGED             (0x00000500)
#define MPI_SCSIIO_CONTROL_NO_DISCONNECT        (0x00000700)

#define MPI_SCSIIO_CONTROL_TASKMANAGE_MASK      (0x00FF0000)
#define MPI_SCSIIO_CONTROL_OBSOLETE             (0x00800000)
#define MPI_SCSIIO_CONTROL_CLEAR_ACA_RSV        (0x00400000)
#define MPI_SCSIIO_CONTROL_TARGET_RESET         (0x00200000)
#define MPI_SCSIIO_CONTROL_LUN_RESET_RSV        (0x00100000)
#define MPI_SCSIIO_CONTROL_RESERVED             (0x00080000)
#define MPI_SCSIIO_CONTROL_CLR_TASK_SET_RSV     (0x00040000)
#define MPI_SCSIIO_CONTROL_ABORT_TASK_SET       (0x00020000)
#define MPI_SCSIIO_CONTROL_RESERVED2            (0x00010000)


/* SCSIIO reply structure */
typedef struct _MSG_SCSI_IO_REPLY
{
    U8                      TargetID;
    U8                      Bus;
    U8                      MsgLength;
    U8                      Function;
    U8                      CDBLength;
    U8                      SenseBufferLength;
    U8                      Reserved;
    U8                      MsgFlags;
    U32                     MsgContext;
    U8                      SCSIStatus;
    U8                      SCSIState;
    U16                     IOCStatus;
    U32                     IOCLogInfo;
    U32                     TransferCount;
    U32                     SenseCount;
    U32                     ResponseInfo;
} MSG_SCSI_IO_REPLY, MPI_POINTER PTR_MSG_SCSI_IO_REPLY,
  SCSIIOReply_t, MPI_POINTER pSCSIIOReply_t;


/* SCSIIO Reply SCSIStatus values (SAM-2 status codes) */

#define MPI_SCSI_STATUS_SUCCESS                 (0x00)
#define MPI_SCSI_STATUS_CHECK_CONDITION         (0x02)
#define MPI_SCSI_STATUS_CONDITION_MET           (0x04)
#define MPI_SCSI_STATUS_BUSY                    (0x08)
#define MPI_SCSI_STATUS_INTERMEDIATE            (0x10)
#define MPI_SCSI_STATUS_INTERMEDIATE_CONDMET    (0x14)
#define MPI_SCSI_STATUS_RESERVATION_CONFLICT    (0x18)
#define MPI_SCSI_STATUS_COMMAND_TERMINATED      (0x22)
#define MPI_SCSI_STATUS_TASK_SET_FULL           (0x28)
#define MPI_SCSI_STATUS_ACA_ACTIVE              (0x30)


/* SCSIIO Reply SCSIState values */

#define MPI_SCSI_STATE_AUTOSENSE_VALID          (0x01)
#define MPI_SCSI_STATE_AUTOSENSE_FAILED         (0x02)
#define MPI_SCSI_STATE_NO_SCSI_STATUS           (0x04)
#define MPI_SCSI_STATE_TERMINATED               (0x08)
#define MPI_SCSI_STATE_RESPONSE_INFO_VALID      (0x10)

/* SCSIIO Reply ResponseInfo values */
/* (FCP-1 RSP_CODE values and SPI-3 Packetized Failure codes) */

#define MPI_SCSI_RSP_INFO_FUNCTION_COMPLETE     (0x00000000)
#define MPI_SCSI_RSP_INFO_FCP_BURST_LEN_ERROR   (0x01000000)
#define MPI_SCSI_RSP_INFO_CMND_FIELDS_INVALID   (0x02000000)
#define MPI_SCSI_RSP_INFO_FCP_DATA_RO_ERROR     (0x03000000)
#define MPI_SCSI_RSP_INFO_TASK_MGMT_UNSUPPORTED (0x04000000)
#define MPI_SCSI_RSP_INFO_TASK_MGMT_FAILED      (0x05000000)
#define MPI_SCSI_RSP_INFO_SPI_LQ_INVALID_TYPE   (0x06000000)


/****************************************************************************/
/*  SCSI Task Management messages                                           */
/****************************************************************************/

typedef struct _MSG_SCSI_TASK_MGMT
{
    U8                      TargetID;
    U8                      Bus;
    U8                      ChainOffset;
    U8                      Function;
    U8                      Reserved;
    U8                      TaskType;
    U8                      Reserved1;
    U8                      MsgFlags;
    U32                     MsgContext;
    U8                      LUN[8];
    U32                     Reserved2[7];
    U32                     TaskMsgContext;
} MSG_SCSI_TASK_MGMT, MPI_POINTER PTR_SCSI_TASK_MGMT,
  SCSITaskMgmt_t, MPI_POINTER pSCSITaskMgmt_t;

/* TaskType values */

#define MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK        (0x00000001)
#define MPI_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET     (0x00000002)
#define MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET      (0x00000003)
#define MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS         (0x00000004)

/* MsgFlags bits */
#define MPI_SCSITASKMGMT_MSGFLAGS_LIP_RESET_OPTION  (0x00000002)

/* SCSI Task Management Reply */
typedef struct _MSG_SCSI_TASK_MGMT_REPLY
{
    U8                      TargetID;
    U8                      Bus;
    U8                      MsgLength;
    U8                      Function;
    U8                      Reserved;
    U8                      TaskType;
    U8                      Reserved1;
    U8                      MsgFlags;
    U32                     MsgContext;
    U8                      Reserved2[2];
    U16                     IOCStatus;
    U32                     IOCLogInfo;
    U32                     TerminationCount;
} MSG_SCSI_TASK_MGMT_REPLY, MPI_POINTER PTR_MSG_SCSI_TASK_MGMT_REPLY,
  SCSITaskMgmtReply_t, MPI_POINTER pSCSITaskMgmtReply_t;

#endif
