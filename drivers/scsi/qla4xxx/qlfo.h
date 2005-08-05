/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP4xxx device driver for Linux 2.6.x
 * Copyright (C) 2003-2004 QLogic Corporation
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

#endif	/* ifndef _FO_H */
