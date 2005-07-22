/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE                                     *
 *                                                                            *
 * QLogic ISP4xxx device driver for Linux 2.4.x                               *
 * Copyright (C) 2004 Qlogic Corporation                                      *
 * (www.qlogic.com)                                                           *
 *                                                                            *
 * This program is free software; you can redistribute it and/or modify it    *
 * under the terms of the GNU General Public License as published by the      *
 * Free Software Foundation; either version 2, or (at your option) any        *
 * later version.                                                             *
 *                                                                            *
 * This program is distributed in the hope that it will be useful, but        *
 * WITHOUT ANY WARRANTY; without even the implied warranty of                 *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU          *
 * General Public License for more details.                                   *
 *                                                                            *
 ******************************************************************************
 *             Please see release.txt for revision history.                   *
 *                                                                            *
 ******************************************************************************
 * Function Table of Contents:
 *
 ****************************************************************************/

/*
 * Compile time Options:
 *            0 - Disable and 1 - Enable
 ****************************************/

/*
 * The following compile time options are temporary,
 * used for debug purposes only.
 ****************************************/
#define ISP_RESET_TEST		0 /* Issues BIG HAMMER (reset) every 3 minutes */
#define BYTE_ORDER_SUPPORT_ENABLED 0 /* In the process of translating IOCTL structures */

/*
 * Under heavy I/O on SMP systems (8-way and IA64) with many command
 * timeouts, the scsi mid-layer will sometimes not wake-up the
 * error-handling thread when an error-condition occurs.
 *
 * This workaround, if enabled, will wakeup the error-handler if it is
 * stuck in this condition for sixty seconds.
 ****************************************/
#define EH_WAKEUP_WORKAROUND		0
#if SH_HAS_ATOMIC_HOST_BUSY  /* defined in makefile */
#define HOST_BUSY(ha)	atomic_read(&ha->host->host_busy)
#else
#define HOST_BUSY(ha)	ha->host->host_busy
#endif


/*
 * Compile time Options:
 *     0 - Disable and 1 - Enable
 */
#define DEBUG_QLA4xx		0	/* For Debug of qla4xxx */

/* Failover options */
#define MAX_RECOVERYTIME	10	/*
					 * Max suspend time for a lun recovery
					 * time
					 */
#define MAX_FAILBACKTIME	5	/* Max suspend time before fail back */

#define EXTEND_CMD_TIMEOUT	60
#if 0
/* 
 * When a lun is suspended for the "Not Ready" condition then it will suspend
 * the lun for increments of 6 sec delays.  SUSPEND_COUNT is that count.
 */
#define SUSPEND_COUNT		10	/* 6 secs * 10 retries = 60 secs */

/*
 * Defines the time in seconds that the driver extends the command timeout to
 * get around the problem where the mid-layer only allows 5 retries for
 * commands that return BUS_BUSY
 */

#define MAX_RETRIES_OF_ISP_ABORT	5

//#include "ql4_version.h"
#endif
