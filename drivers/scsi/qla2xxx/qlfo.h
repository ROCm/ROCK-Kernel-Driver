/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 device driver for Linux 2.6.x
 * Copyright (C) 2003 QLogic Corporation
 * (www.qlogic.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 ******************************************************************************/

/*
 * San/Device Management Failover Ioctl Header
 * File is created to adhere to Solaris requirement using 8-space tabs.
 *
 * !!!!! PLEASE DO NOT REMOVE THE TABS !!!!!
 * !!!!! PLEASE NO SINGLE LINE COMMENTS: // !!!!!
 * !!!!! PLEASE NO MORE THAN 80 CHARS PER LINE !!!!!
 *
 * Revision History:
 *
 * Rev. 0.00	August 8, 2000
 * WTR	- Created.
 *
 * Rev. 0.01	August 8, 2000
 * WTR	- Made size of HbaInstance fields consistant as UINT8.
 *        Made command codes as 300 upward to be consistant with definitions
 *        in ExIoct.h.
 * Rev. 0.01	October 3, 2000
 * TLE  - Exclusion of ExIoct.h
 *
 * Rev. 0.01	October 6, 2000
 * TLE  - Made size of HbaInstance fields UINT8
 *
 * Rev. 0.01	October 10, 2000
 * TLE  - Add _FO_DRIVER_VERSION data structure
 */



#ifndef _FO_H
#define _FO_H

/*
 * ***********************************************************************
 * X OS type definitions
 * ***********************************************************************
 */
#ifdef _MSC_VER						/* NT */

#pragma pack(1)
#include "qlfont.h"

#elif defined(linux)					/* Linux */

#include "qlfoln.h"

#elif defined(sun) || defined(__sun)			/* Solaris */

#include "qlfoso.h"

#endif

#define SDM_DEF_MAX_DEVICES		16
#define SDM_DEF_MAX_PATHS_PER_TARGET	4
#define SDM_DEF_MAX_TARGETS_PER_DEVICE	4
#define SDM_DEF_MAX_PATHS_PER_DEVICE (SDM_DEF_MAX_PATHS_PER_TARGET * SDM_DEF_MAX_TARGETS_PER_DEVICE)

#define FO_MAX_LUNS_PER_DEVICE	MAX_LUNS_OS
#define FO_MAX_PATHS (SDM_DEF_MAX_PATHS_PER_DEVICE * SDM_DEF_MAX_DEVICES)
#define FO_MAX_ADAPTERS		32
#define FO_ADAPTER_ALL		0xFF
#define FO_DEF_WWN_SIZE             8
#define FO_MAX_GEN_INFO_STRING_LEN  32

#if 0	/* defined in qlfolimits.h */
#define FO_NOTIFY_TYPE_NONE                   0
#define FO_NOTIFY_TYPE_LUN_RESET              1
#define FO_NOTIFY_TYPE_CDB                    2
#define FO_NOTIFY_TYPE_LOGOUT_OR_LUN_RESET    3
#define FO_NOTIFY_TYPE_LOGOUT_OR_CDB          4
#define FO_NOTIFY_TYPE_SPINUP          	      5

#define FO_NOTIFY_TYPE_MIN                FO_NOTIFY_TYPE_NONE
#define FO_NOTIFY_TYPE_MAX                FO_NOTIFY_TYPE_LOGOUT_OR_CDB
#define FO_NOTIFY_TYPE_DEF                FO_NOTIFY_TYPE_SPINUP

#define FO_NOTIFY_CDB_LENGTH_MIN              6
#define FO_NOTIFY_CDB_LENGTH_MAX             16
#endif

/*
 * IOCTL Commands
 */

#define FO_CC_GET_PARAMS		        FO_CC_GET_PARAMS_OS
#define FO_CC_SET_PARAMS		        FO_CC_SET_PARAMS_OS
#define FO_CC_GET_PATHS		            FO_CC_GET_PATHS_OS
#define FO_CC_SET_CURRENT_PATH	        FO_CC_SET_CURRENT_PATH_OS
#define FO_CC_GET_HBA_STAT		        FO_CC_GET_HBA_STAT_OS
#define FO_CC_RESET_HBA_STAT	        FO_CC_RESET_HBA_STAT_OS
#define FO_CC_GET_LUN_DATA              FO_CC_GET_LUN_DATA_OS
#define FO_CC_SET_LUN_DATA              FO_CC_SET_LUN_DATA_OS
#define FO_CC_GET_TARGET_DATA           FO_CC_GET_TARGET_DATA_OS
#define FO_CC_SET_TARGET_DATA           FO_CC_SET_TARGET_DATA_OS
#define FO_CC_GET_FO_DRIVER_VERSION     FO_CC_GET_FO_DRIVER_VERSION_OS


/* Systemwide failover parameters. */

typedef struct _FO_PARAMS
{
        UINT32      InspectionInterval;     /* Timer interval to check for failover.*/
        UINT8       MaxPathsPerDevice;	    /* Max paths to any single device. */
        UINT8       MaxRetriesPerPath;	    /* Max retries on a path before */

        /* Failover. */
        UINT8       MaxRetriesPerIo;	    /* Max retries per i/o request. */
        UINT8       Reserved1;
        UINT32      Flags;			        /* Control flags. */
        UINT8       DeviceErrorThreshold;   /* Max device errors. */
        UINT8       DeviceTimeoutThreshold; /* Max device timeouts.*/
        UINT8       FrameErrorThreshold;    /* Max frame errors.*/
        UINT8       LinkErrorThreshold;     /* Max link errors.*/
        UINT32      Reserved2[4];           /* Spares.*/

        /* Load balancing parameters.*/

        UINT8       RollingAverageIntervals;/* Intervals to sum for rolling average.*/
        UINT8       MaxDevicesToMigrate;    /* Max devices to migrate in any interval.*/
        UINT8       BalanceMethod;          /* Method to use for load balancing.*/
        UINT8       Reserved3;              /* Memory alignment.*/

        UINT16      LoadShareMinPercentage; /* Load balancing parameter.*/
        UINT16      LoadShareMaxPercentage; /* Load balancing parameter.*/

        /* Failover notify parameters. */

        UINT8       FailoverNotifyType;	/* Type of notification. */
        UINT8       FailoverNotifyCdbLength;/* Length of notification CDB. */
        UINT16      Reserved4;
        UINT8       FailoverNotifyCdb[16];	/* CDB if notification by CDB. */
        UINT32      Reserved5;

}
FO_PARAMS, *PFO_PARAMS, SysFoParams_t, *SysFoParams_p;

extern SysFoParams_t qla_fo_params;

typedef struct _FO_GET_PATHS
{
        UINT8       HbaInstance;
        EXT_DEST_ADDR HbaAddr;       /* Lun field is ignored */
        UINT32      Reserved[5];

}
FO_GET_PATHS, *PFO_GET_PATHS;


typedef struct _FO_PATH_ENTRY
{
        UINT8   Reserved1;
        UINT8   Visible;		/* Path is visible path. */
        UINT8   Priority;
        UINT8   Reserved2;
        UINT8   HbaInstance;
        UINT8   PortName[EXT_DEF_WWN_NAME_SIZE];
        UINT16  Reserved3;
        UINT32  Reserved[3];

}
FO_PATH_ENTRY, *PFO_PATH_ENTRY;


typedef struct _FO_PATHS_INFO
{
        /* These first fields in the output buffer are specifically the
         * same as the fields in the input buffer.  This is because the
         * same system buffer holds both, and this allows us to reference
         * the input buffer parameters while filling the output buffer. */

        UINT8       HbaInstance;
        EXT_DEST_ADDR HbaAddr;
        UINT32      Reserved[5];
        UINT8       PathCount;          /* Number of Paths in PathEntry array */
        UINT8       Reserved3;
        UINT8       VisiblePathIndex;   /* Which index has BOOLEAN "visible" flag set */
        UINT8       Reserved4;

        UINT8       CurrentPathIndex[FO_MAX_LUNS_PER_DEVICE];   /* Current Path Index for each Lun */

        FO_PATH_ENTRY   PathEntry[FO_MAX_PATHS];

        UINT32      Reserved5[4];

}
FO_PATHS_INFO, *PFO_PATHS_INFO;

typedef struct _FO_SET_CURRENT_PATH
{
        UINT8       HbaInstance;
        EXT_DEST_ADDR HbaAddr;
        UINT8       NewCurrentPathIndex;    /* Path index to make current path. */
        UINT8       FailoverType;           /* Reason for failover. */
        UINT32      Reserved[3];

}
FO_SET_CURRENT_PATH, *PFO_SET_CURRENT_PATH;

typedef union _FO_PATHS {
        FO_GET_PATHS input;
        FO_SET_CURRENT_PATH set
                ;
        FO_PATHS_INFO info;
} FO_PATHS;


typedef struct  _FO_HBA_STAT_INPUT
{
        /* The first field in the input buffer is specifically the
         * same as the field in the output buffer.  This is because the
         * same system buffer holds both, and this allows us to reference
         * the input buffer parameters while filling the output buffer. */

        UINT8       HbaInstance;		/* Port number or ADAPTER_ALL. */
        UINT8       Reserved1[3];
        UINT32      Reserved2[7];

}
FO_HBA_STAT_INPUT, *PFO_HBA_STAT_INPUT;


typedef struct _FO_HBA_STAT_ENTRY
{
        UINT8       HbaInstance;
        UINT8       Reserved1[3];
        UINT32      Reserved2;
        UINT64      IosRequested; /* IOs requested on this adapter. */
        UINT64      BytesRequested;		/* Bytes requested on this adapter. */
        UINT64      IosExecuted; /* IOs executed on this adapter. */
        UINT64      BytesExecuted;		/* Bytes executed on this adapter. */
        UINT32      Reserved3[22];

}
FO_HBA_STAT_ENTRY, *PFO_HBA_STAT_ENTRY;


typedef struct _FO_HBA_STAT_INFO
{
        /* The first fields in the output buffer is specifically the
         * same as the field in the input buffer.  This is because the
         * same system buffer holds both, and this allows us to reference
         * the input buffer parameters while filling the output buffer. */

        UINT8       HbaInstance; /* Port number or ADAPTER_ALL. */
        UINT8       HbaCount; /* Count of adapters returned. */
        UINT8       Reserved1[2];
        UINT32      Reserved2[7];

        FO_HBA_STAT_ENTRY StatEntry[FO_MAX_ADAPTERS];

}
FO_HBA_STAT_INFO, *PFO_HBA_STAT_INFO;



/*  The "external" LUN data refers to the LUNs as represented in our
  configuration utility, where one physical target can support up to
  2048 LUNs, which are mapped around internally.  This is in comparison
  to an "internal" LUN data, which is 256 LUNs, after being mapped
  inside the driver to multiple target slots. */

#define EXTERNAL_LUN_COUNT          2048

/* Structure as used in the IOCTL.*/

typedef struct _FO_EXTERNAL_LUN_DATA_ENTRY
{
        UINT8       NodeName[EXT_DEF_WWN_NAME_SIZE];
        UINT8       PortName[EXT_DEF_WWP_NAME_SIZE];  //sri

        UINT16      LunCount;   /* Entries in Lun Data array. */
        UINT8       TargetId;
        UINT8       Dev_No;
        UINT32      Reserved3;
        UINT32      Reserved4;
        UINT32      Reserved5;                     /* Pad to 32-byte header.*/

        UINT8       Data[EXTERNAL_LUN_COUNT];
}
FO_EXTERNAL_LUN_DATA_ENTRY, *PFO_EXTERNAL_LUN_DATA_ENTRY;

//  Structure as it is stored in the NT registry.

typedef struct _FO_LUN_DATA_LIST
{
        UINT16      Version;                       /* Should be LUN_DATA_REGISTRY_VERSION.*/
        UINT16      EntryCount;                    /* Count of variable entries following.*/
        UINT32      Reserved1;
        UINT32      Reserved2;
        UINT32      Reserved3;
        UINT32      Reserved4;
        UINT32      Reserved5;
        UINT32      Reserved6;
        UINT32      Reserved7;                     /* Pad to 32-byte header.*/

        FO_EXTERNAL_LUN_DATA_ENTRY DataEntry[1];   /* Variable-length data.*/

}
FO_LUN_DATA_LIST, *PFO_LUN_DATA_LIST;

typedef struct  _FO_LUN_DATA_INPUT
{
        /* The first field in the input buffer is specifically the
         * same as the field in the output buffer.  This is because the
         * same system buffer holds both, and this allows us to reference
         * the input buffer parameters while filling the output buffer. */

        UINT8       HbaInstance;		/* Port number */
        UINT8       Reserved1[3];
        UINT32      Reserved2[7];

}
FO_LUN_DATA_INPUT, *PFO_LUN_DATA_INPUT;

typedef struct _FO_REQUEST_ADDR
{
        UINT8           HbaInstance;
        EXT_DEST_ADDR   TargetAddr;
        UINT32          Reserved[5];

}
FO_REQUEST_ADDR, *PFO_REQUEST_ADDR;

typedef struct  _FO_TARGET_DATA_INPUT
{
        UINT8       HbaInstance;		/* Port number */
        UINT8       Reserved1[3];
        UINT32      Reserved2[7];

}
FO_TARGET_DATA_INPUT, *PFO_TARGET_DATA_INPUT;

#define FO_INTERNAL_LUN_COUNT          256
#define FO_INTERNAL_LUN_BITMASK_BYTES  (FO_INTERNAL_LUN_COUNT / 8)

typedef struct _FO_INTERNAL_LUN_BITMASK
{
        UINT8       Bitmask[FO_INTERNAL_LUN_BITMASK_BYTES];
}
FO_INTERNAL_LUN_BITMASK, *PFO_INTERNAL_LUN_BITMASK;

typedef struct _FO_DEVICE_DATA
{
        UINT32      DeviceFlags;        /* Device flags */
        UINT16      LoopId;             /* Current loop ID */
        UINT16      BaseLunNumber;      /* Base LUN number */
        UINT8       WorldWideName[8];   /* World Wide Name for device */
        UINT8       PortId[3];          /* Port ID */
        UINT8       MultipathControl;   /* Multipath control byte. */
        UINT16      DeviceState;        /* Device state */
        UINT16      LoginRetryCount;    /* Number of login retries */
        UINT8       PortName[8];        /* Port name for device */
        UINT16      TimeoutCount;       /* Command timeout count */
        UINT8       TargetId;
        UINT8       Dev_No;
        FO_INTERNAL_LUN_BITMASK    LunBitmask; /* LUN bitmask */
}
FO_DEVICE_DATA, *PFO_DEVICE_DATA;

typedef struct _FO_DEVICE_DATABASE
{
        FO_DEVICE_DATA  DeviceData[256];
}
FO_DEVICE_DATABASE, *PFO_DEVICE_DATABASE;

typedef struct _FO_DRIVER_VERSION
{
        // Numeric version.
        UINT8       Version;                       // Major version number.
        UINT8       Revision;                      // Minor version number.
        UINT8       Subrevision;                   // Subminor version number.
        UINT8       Reserved1;                      // Memory alignment.

        // String version.
        UINT8       VersionStr[FO_MAX_GEN_INFO_STRING_LEN];

        // Reserved fields.
        UINT32      Reserved2[16];

}
FO_DRIVER_VERSION, *PFO_DRIVER_VERSION;


#define FO_LUN_DATA_LIST_MIN_ENTRIES      1
#define FO_LUN_DATA_LIST_MAX_ENTRIES    256
#ifdef _WIN64
#define FO_LUN_DATA_LIST_HEADER_SIZE 32
#else
#define FO_LUN_DATA_LIST_HEADER_SIZE offsetof(FO_LUN_DATA_LIST, DataEntry)
#endif

#define FO_LUN_DATA_LIST_MIN_SIZE (FO_LUN_DATA_LIST_HEADER_SIZE + (sizeof(FO_EXTERNAL_LUN_DATA_ENTRY) * FO_LUN_DATA_LIST_MIN_ENTRIES))
#define FO_LUN_DATA_LIST_MAX_SIZE (FO_LUN_DATA_LIST_HEADER_SIZE + (sizeof(FO_EXTERNAL_LUN_DATA_ENTRY) * FO_LUN_DATA_LIST_MAX_ENTRIES))


#endif	/* ifndef _FO_H */
