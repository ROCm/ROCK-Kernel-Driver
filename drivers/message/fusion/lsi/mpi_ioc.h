/*
 *  Copyright (c) 2000-2001 LSI Logic Corporation.
 *
 *
 *           Name:  MPI_IOC.H
 *          Title:  MPI IOC, Port, Event, FW Load, and ToolBox messages
 *  Creation Date:  August 11, 2000
 *
 *    MPI Version:  01.01.05
 *
 *  Version History
 *  ---------------
 *
 *  Date      Version   Description
 *  --------  --------  ------------------------------------------------------
 *  05-08-00  00.10.01  Original release for 0.10 spec dated 4/26/2000.
 *  05-24-00  00.10.02  Added _MSG_IOC_INIT_REPLY structure.
 *  06-06-00  01.00.01  Added CurReplyFrameSize field to _MSG_IOC_FACTS_REPLY.
 *  06-12-00  01.00.02  Added _MSG_PORT_ENABLE_REPLY structure.
 *                      Added _MSG_EVENT_ACK_REPLY structure.
 *                      Added _MSG_FW_DOWNLOAD_REPLY structure.
 *                      Added _MSG_TOOLBOX_REPLY structure.
 *  06-30-00  01.00.03  Added MaxLanBuckets to _PORT_FACT_REPLY structure.
 *  07-27-00  01.00.04  Added _EVENT_DATA structure definitions for _SCSI,
 *                      _LINK_STATUS, _LOOP_STATE and _LOGOUT.
 *  08-11-00  01.00.05  Switched positions of MsgLength and Function fields in
 *                      _MSG_EVENT_ACK_REPLY structure to match specification.
 *  11-02-00  01.01.01  Original release for post 1.0 work.
 *                      Added a value for Manufacturer to WhoInit.
 *  12-04-00  01.01.02  Modified IOCFacts reply, added FWUpload messages, and
 *                      removed toolbox message.
 *  01-09-01  01.01.03  Added event enabled and disabled defines.
 *                      Added structures for FwHeader and DataHeader.
 *                      Added ImageType to FwUpload reply.
 *  02-20-01  01.01.04  Started using MPI_POINTER.
 *  02-27-01  01.01.05  Added event for RAID status change and its event data.
 *                      Added IocNumber field to MSG_IOC_FACTS_REPLY.
 *  --------------------------------------------------------------------------
 */

#ifndef MPI_IOC_H
#define MPI_IOC_H


/*****************************************************************************
*
*               I O C    M e s s a g e s
*
*****************************************************************************/

/****************************************************************************/
/*  IOCInit message                                                         */
/****************************************************************************/

typedef struct _MSG_IOC_INIT
{
    U8                      WhoInit;
    U8                      Reserved;
    U8                      ChainOffset;
    U8                      Function;
    U8                      Flags;
    U8                      MaxDevices;
    U8                      MaxBuses;
    U8                      MsgFlags;
    U32                     MsgContext;
    U16                     ReplyFrameSize;
    U8                      Reserved1[2];
    U32                     HostMfaHighAddr;
    U32                     SenseBufferHighAddr;
} MSG_IOC_INIT, MPI_POINTER PTR_MSG_IOC_INIT,
  IOCInit_t, MPI_POINTER pIOCInit_t;

typedef struct _MSG_IOC_INIT_REPLY
{
    U8                      WhoInit;
    U8                      Reserved;
    U8                      MsgLength;
    U8                      Function;
    U8                      Flags;
    U8                      MaxDevices;
    U8                      MaxBuses;
    U8                      MsgFlags;
    U32                     MsgContext;
    U16                     Reserved2;
    U16                     IOCStatus;
    U32                     IOCLogInfo;
} MSG_IOC_INIT_REPLY, MPI_POINTER PTR_MSG_IOC_INIT_REPLY,
  IOCInitReply_t, MPI_POINTER pIOCInitReply_t;

/* WhoInit values */

#define MPI_WHOINIT_NO_ONE                      (0x00)
#define MPI_WHOINIT_SYSTEM_BIOS                 (0x01)
#define MPI_WHOINIT_ROM_BIOS                    (0x02)
#define MPI_WHOINIT_PCI_PEER                    (0x03)
#define MPI_WHOINIT_HOST_DRIVER                 (0x04)
#define MPI_WHOINIT_MANUFACTURER                (0x05)


/****************************************************************************/
/*  IOC Facts message                                                       */
/****************************************************************************/

typedef struct _MSG_IOC_FACTS
{
    U8                      Reserved[2];
    U8                      ChainOffset;
    U8                      Function;
    U8                      Reserved1[3];
    U8                      MsgFlags;
    U32                     MsgContext;
} MSG_IOC_FACTS, MPI_POINTER PTR_IOC_FACTS,
  IOCFacts_t, MPI_POINTER pIOCFacts_t;

/* IOC Facts Reply */

typedef struct _MSG_IOC_FACTS_REPLY
{
    U16                     MsgVersion;
    U8                      MsgLength;
    U8                      Function;
    U16                     Reserved;
    U8                      IOCNumber;
    U8                      MsgFlags;
    U32                     MsgContext;
    U16                     Reserved2;
    U16                     IOCStatus;
    U32                     IOCLogInfo;
    U8                      MaxChainDepth;
    U8                      WhoInit;
    U8                      BlockSize;
    U8                      Flags;
    U16                     ReplyQueueDepth;
    U16                     RequestFrameSize;
    U16                     FWVersion;
    U16                     ProductID;
    U32                     CurrentHostMfaHighAddr;
    U16                     GlobalCredits;
    U8                      NumberOfPorts;
    U8                      EventState;
    U32                     CurrentSenseBufferHighAddr;
    U16                     CurReplyFrameSize;
    U8                      MaxDevices;
    U8                      MaxBuses;
    U32                     FWImageSize;
    U32                     DataImageSize;
} MSG_IOC_FACTS_REPLY, MPI_POINTER PTR_MSG_IOC_FACTS_REPLY,
  IOCFactsReply_t, MPI_POINTER pIOCFactsReply_t;

#define MPI_IOCFACTS_MSGVERSION_MAJOR_MASK      (0xFF00)
#define MPI_IOCFACTS_MSGVERSION_MINOR_MASK      (0x00FF)

#define MPI_IOCFACTS_FLAGS_FW_DOWNLOAD_BOOT     (0x01)
#define MPI_IOCFACTS_FLAGS_DATA_IMAGE_UPLOAD    (0x02)

#define MPI_IOCFACTS_EVENTSTATE_DISABLED        (0x00)
#define MPI_IOCFACTS_EVENTSTATE_ENABLED         (0x01)



/*****************************************************************************
*
*               P o r t    M e s s a g e s
*
*****************************************************************************/

/****************************************************************************/
/*  Port Facts message and Reply                                            */
/****************************************************************************/

typedef struct _MSG_PORT_FACTS
{
     U8                     Reserved[2];
     U8                     ChainOffset;
     U8                     Function;
     U8                     Reserved1[2];
     U8                     PortNumber;
     U8                     MsgFlags;
     U32                    MsgContext;
} MSG_PORT_FACTS, MPI_POINTER PTR_MSG_PORT_FACTS,
  PortFacts_t, MPI_POINTER pPortFacts_t;

typedef struct _MSG_PORT_FACTS_REPLY
{
     U16                    Reserved;
     U8                     MsgLength;
     U8                     Function;
     U16                    Reserved1;
     U8                     PortNumber;
     U8                     MsgFlags;
     U32                    MsgContext;
     U16                    Reserved2;
     U16                    IOCStatus;
     U32                    IOCLogInfo;
     U8                     Reserved3;
     U8                     PortType;
     U16                    MaxDevices;
     U16                    PortSCSIID;
     U16                    ProtocolFlags;
     U16                    MaxPostedCmdBuffers;
     U16                    MaxPersistentIDs;
     U16                    MaxLanBuckets;
     U16                    Reserved4;
     U32                    Reserved5;
} MSG_PORT_FACTS_REPLY, MPI_POINTER PTR_MSG_PORT_FACTS_REPLY,
  PortFactsReply_t, MPI_POINTER pPortFactsReply_t;


/* PortTypes values */

#define MPI_PORTFACTS_PORTTYPE_INACTIVE         (0x00)
#define MPI_PORTFACTS_PORTTYPE_SCSI             (0x01)
#define MPI_PORTFACTS_PORTTYPE_FC               (0x10)

/* ProtocolFlags values */

#define MPI_PORTFACTS_PROTOCOL_LOGBUSADDR       (0x01)
#define MPI_PORTFACTS_PROTOCOL_LAN              (0x02)
#define MPI_PORTFACTS_PROTOCOL_TARGET           (0x04)
#define MPI_PORTFACTS_PROTOCOL_INITIATOR        (0x08)


/****************************************************************************/
/*  Port Enable Message                                                     */
/****************************************************************************/

typedef struct _MSG_PORT_ENABLE
{
    U8                      Reserved[2];
    U8                      ChainOffset;
    U8                      Function;
    U8                      Reserved1[2];
    U8                      PortNumber;
    U8                      MsgFlags;
    U32                     MsgContext;
} MSG_PORT_ENABLE, MPI_POINTER PTR_MSG_PORT_ENABLE,
  PortEnable_t, MPI_POINTER pPortEnable_t;

typedef struct _MSG_PORT_ENABLE_REPLY
{
    U8                      Reserved[2];
    U8                      MsgLength;
    U8                      Function;
    U8                      Reserved1[2];
    U8                      PortNumber;
    U8                      MsgFlags;
    U32                     MsgContext;
    U16                     Reserved2;
    U16                     IOCStatus;
    U32                     IOCLogInfo;
} MSG_PORT_ENABLE_REPLY, MPI_POINTER PTR_MSG_PORT_ENABLE_REPLY,
  PortEnableReply_t, MPI_POINTER pPortEnableReply_t;


/*****************************************************************************
*
*               E v e n t    M e s s a g e s
*
*****************************************************************************/

/****************************************************************************/
/*  Event Notification messages                                             */
/****************************************************************************/

typedef struct _MSG_EVENT_NOTIFY
{
    U8                      Switch;
    U8                      Reserved;
    U8                      ChainOffset;
    U8                      Function;
    U8                      Reserved1[3];
    U8                      MsgFlags;
    U32                     MsgContext;
} MSG_EVENT_NOTIFY, MPI_POINTER PTR_MSG_EVENT_NOTIFY,
  EventNotification_t, MPI_POINTER pEventNotification_t;

/* Event Notification Reply */

typedef struct _MSG_EVENT_NOTIFY_REPLY
{
     U16                    EventDataLength;
     U8                     MsgLength;
     U8                     Function;
     U8                     Reserved1[2];
     U8                     AckRequired;
     U8                     MsgFlags;
     U32                    MsgContext;
     U8                     Reserved2[2];
     U16                    IOCStatus;
     U32                    IOCLogInfo;
     U32                    Event;
     U32                    EventContext;
     U32                    Data[1];
} MSG_EVENT_NOTIFY_REPLY, MPI_POINTER PTR_MSG_EVENT_NOTIFY_REPLY,
  EventNotificationReply_t, MPI_POINTER pEventNotificationReply_t;

/* Event Acknowledge */

typedef struct _MSG_EVENT_ACK
{
    U8                      Reserved[2];
    U8                      ChainOffset;
    U8                      Function;
    U8                      Reserved1[3];
    U8                      MsgFlags;
    U32                     MsgContext;
    U32                     Event;
    U32                     EventContext;
} MSG_EVENT_ACK, MPI_POINTER PTR_MSG_EVENT_ACK,
  EventAck_t, MPI_POINTER pEventAck_t;

typedef struct _MSG_EVENT_ACK_REPLY
{
    U8                      Reserved[2];
    U8                      MsgLength;
    U8                      Function;
    U8                      Reserved1[3];
    U8                      MsgFlags;
    U32                     MsgContext;
    U16                     Reserved2;
    U16                     IOCStatus;
    U32                     IOCLogInfo;
} MSG_EVENT_ACK_REPLY, MPI_POINTER PTR_MSG_EVENT_ACK_REPLY,
  EventAckReply_t, MPI_POINTER pEventAckReply_t;


/* Switch */

#define MPI_EVENT_NOTIFICATION_SWITCH_OFF   (0x00)
#define MPI_EVENT_NOTIFICATION_SWITCH_ON    (0x01)

/* Event */

#define MPI_EVENT_NONE                      (0x00000000)
#define MPI_EVENT_LOG_DATA                  (0x00000001)
#define MPI_EVENT_STATE_CHANGE              (0x00000002)
#define MPI_EVENT_UNIT_ATTENTION            (0x00000003)
#define MPI_EVENT_IOC_BUS_RESET             (0x00000004)
#define MPI_EVENT_EXT_BUS_RESET             (0x00000005)
#define MPI_EVENT_RESCAN                    (0x00000006)
#define MPI_EVENT_LINK_STATUS_CHANGE        (0x00000007)
#define MPI_EVENT_LOOP_STATE_CHANGE         (0x00000008)
#define MPI_EVENT_LOGOUT                    (0x00000009)
#define MPI_EVENT_EVENT_CHANGE              (0x0000000A)
#define MPI_EVENT_RAID_STATUS_CHANGE        (0x0000000B)

/* AckRequired field values */

#define MPI_EVENT_NOTIFICATION_ACK_NOT_REQUIRED (0x00)
#define MPI_EVENT_NOTIFICATION_ACK_REQUIRED     (0x01)

/* SCSI Event data for Port, Bus and Device forms) */

typedef struct _EVENT_DATA_SCSI
{
    U8                      TargetID;
    U8                      BusPort;
    U16                     Reserved;
} EVENT_DATA_SCSI, MPI_POINTER PTR_EVENT_DATA_SCSI,
  EventDataScsi_t, MPI_POINTER pEventDataScsi_t;

/* MPI Link Status Change Event data */

typedef struct _EVENT_DATA_LINK_STATUS
{
    U8                      State;
    U8                      Reserved;
    U16                     Reserved1;
    U8                      Reserved2;
    U8                      Port;
    U16                     Reserved3;
} EVENT_DATA_LINK_STATUS, MPI_POINTER PTR_EVENT_DATA_LINK_STATUS,
  EventDataLinkStatus_t, MPI_POINTER pEventDataLinkStatus_t;

#define MPI_EVENT_LINK_STATUS_FAILURE       (0x00000000)
#define MPI_EVENT_LINK_STATUS_ACTIVE        (0x00000001)

/* MPI Loop State Change Event data */

typedef struct _EVENT_DATA_LOOP_STATE
{
    U8                      Character4;
    U8                      Character3;
    U8                      Type;
    U8                      Reserved;
    U8                      Reserved1;
    U8                      Port;
    U16                     Reserved2;
} EVENT_DATA_LOOP_STATE, MPI_POINTER PTR_EVENT_DATA_LOOP_STATE,
  EventDataLoopState_t, MPI_POINTER pEventDataLoopState_t;

#define MPI_EVENT_LOOP_STATE_CHANGE_LIP     (0x0001)
#define MPI_EVENT_LOOP_STATE_CHANGE_LPE     (0x0002)
#define MPI_EVENT_LOOP_STATE_CHANGE_LPB     (0x0003)

/* MPI LOGOUT Event data */

typedef struct _EVENT_DATA_LOGOUT
{
    U32                     NPortID;
    U8                      Reserved;
    U8                      Port;
    U16                     Reserved1;
} EVENT_DATA_LOGOUT, MPI_POINTER PTR_EVENT_DATA_LOGOUT,
  EventDataLogout_t, MPI_POINTER pEventDataLogout_t;

/* MPI RAID Status Change Event data */

typedef struct _EVENT_DATA_RAID_STATUS_CHANGE
{
    U8                      VolumeTargetID;
    U8                      VolumeBus;
    U8                      ReasonCode;
    U8                      PhysDiskNum;
    U8                      ASC;
    U8                      ASCQ;
    U16                     Reserved;
} EVENT_DATA_RAID_STATUS_CHANGE, MPI_POINTER PTR_EVENT_DATA_RAID_STATUS_CHANGE,
  MpiEventDataRaidStatusChange_t, MPI_POINTER pMpiEventDataRaidStatusChange_t;


/* MPI RAID Status Change Event data ReasonCode values */

#define MPI_EVENT_RAID_DATA_RC_VOLUME_OPTIMAL       (0x00)
#define MPI_EVENT_RAID_DATA_RC_VOLUME_DEGRADED      (0x01)
#define MPI_EVENT_RAID_DATA_RC_STARTED_RESYNC       (0x02)
#define MPI_EVENT_RAID_DATA_RC_DISK_ADDED           (0x03)
#define MPI_EVENT_RAID_DATA_RC_DISK_NOT_RESPONDING  (0x04)
#define MPI_EVENT_RAID_DATA_RC_SMART_DATA           (0x05)


/*****************************************************************************
*
*               F i r m w a r e    L o a d    M e s s a g e s
*
*****************************************************************************/

/****************************************************************************/
/*  Firmware Download message and associated structures                     */
/****************************************************************************/

typedef struct _MSG_FW_DOWNLOAD
{
    U8                      ImageType;
    U8                      Reserved;
    U8                      ChainOffset;
    U8                      Function;
    U8                      Reserved1[3];
    U8                      MsgFlags;
    U32                     MsgContext;
    SGE_MPI_UNION           SGL;
} MSG_FW_DOWNLOAD, MPI_POINTER PTR_MSG_FW_DOWNLOAD,
  FWDownload_t, MPI_POINTER pFWDownload_t;

#define MPI_FW_DOWNLOAD_ITYPE_RESERVED      (0x00)
#define MPI_FW_DOWNLOAD_ITYPE_FW            (0x01)
#define MPI_FW_DOWNLOAD_ITYPE_BIOS          (0x02)


typedef struct _FWDownloadTCSGE
{
    U8                      Reserved;
    U8                      ContextSize;
    U8                      DetailsLength;
    U8                      Flags;
    U32                     Reserved1;
    U32                     ImageOffset;
    U32                     ImageSize;
} FW_DOWNLOAD_TCSGE, MPI_POINTER PTR_FW_DOWNLOAD_TCSGE,
  FWDownloadTCSGE_t, MPI_POINTER pFWDownloadTCSGE_t;

/* Firmware Download reply */
typedef struct _MSG_FW_DOWNLOAD_REPLY
{
    U8                      ImageType;
    U8                      Reserved;
    U8                      MsgLength;
    U8                      Function;
    U8                      Reserved1[3];
    U8                      MsgFlags;
    U32                     MsgContext;
    U16                     Reserved2;
    U16                     IOCStatus;
    U32                     IOCLogInfo;
} MSG_FW_DOWNLOAD_REPLY, MPI_POINTER PTR_MSG_FW_DOWNLOAD_REPLY,
  FWDownloadReply_t, MPI_POINTER pFWDownloadReply_t;


/****************************************************************************/
/*  Firmware Upload message and associated structures                       */
/****************************************************************************/

typedef struct _MSG_FW_UPLOAD
{
    U8                      ImageType;
    U8                      Reserved;
    U8                      ChainOffset;
    U8                      Function;
    U8                      Reserved1[3];
    U8                      MsgFlags;
    U32                     MsgContext;
    SGE_MPI_UNION           SGL;
} MSG_FW_UPLOAD, MPI_POINTER PTR_MSG_FW_UPLOAD,
  FWUpload_t, MPI_POINTER pFWUpload_t;

#define MPI_FW_UPLOAD_ITYPE_FW_IOC_MEM      (0x00)
#define MPI_FW_UPLOAD_ITYPE_FW_FLASH        (0x01)
#define MPI_FW_UPLOAD_ITYPE_BIOS_FLASH      (0x02)
#define MPI_FW_UPLOAD_ITYPE_DATA_IOC_MEM    (0x03)

typedef struct _FWUploadTCSGE
{
    U8                      Reserved;
    U8                      ContextSize;
    U8                      DetailsLength;
    U8                      Flags;
    U32                     Reserved1;
    U32                     ImageOffset;
    U32                     ImageSize;
} FW_UPLOAD_TCSGE, MPI_POINTER PTR_FW_UPLOAD_TCSGE,
  FWUploadTCSGE_t, MPI_POINTER pFWUploadTCSGE_t;

/* Firmware Upload reply */
typedef struct _MSG_FW_UPLOAD_REPLY
{
    U8                      ImageType;
    U8                      Reserved;
    U8                      MsgLength;
    U8                      Function;
    U8                      Reserved1[3];
    U8                      MsgFlags;
    U32                     MsgContext;
    U16                     Reserved2;
    U16                     IOCStatus;
    U32                     IOCLogInfo;
    U32                     ActualImageSize;
} MSG_FW_UPLOAD_REPLY, MPI_POINTER PTR_MSG_FW_UPLOAD_REPLY,
  FWUploadReply_t, MPI_POINTER pFWUploadReply_t;


typedef struct _MPI_FW_HEADER
{
    U32                     ArmBranchInstruction0;
    U32                     Signature0;
    U32                     Signature1;
    U32                     Signature2;
    U32                     ArmBranchInstruction1;
    U32                     ArmBranchInstruction2;
    U32                     Reserved;
    U32                     Checksum;
    U16                     VendorId;
    U16                     ProductId;
    U16                     FwVersion;
    U16                     Reserved1;
    U32                     SeqCodeVersion;
    U32                     ImageSize;
    U32                     Reserved2;
    U32                     LoadStartAddress;
    U32                     IopResetVectorValue;
    U32                     IopResetRegAddr;
    U32                     VersionNameWhat;
    U8                      VersionName[32];
    U32                     VendorNameWhat;
    U8                      VendorName[32];
} MPI_FW_HEADER, MPI_POINTER PTR_MPI_FW_HEADER,
  MpiFwHeader_t, MPI_POINTER pMpiFwHeader_t;

#define MPI_FW_HEADER_WHAT_SIGNATURE    (0x29232840)


typedef struct _MPI_DATA_HEADER
{
    U32                     Signature;
    U16                     FunctionNumber;
    U16                     Length;
    U32                     Checksum;
    U32                     LoadStartAddress;
} MPI_DATA_HEADER, MPI_POINTER PTR_MPI_DATA_HEADER,
  MpiDataHeader_t, MPI_POINTER pMpiDataHeader_t;

#define MPI_DATA_HEADER_SIGNATURE       (0x43504147)

#endif
