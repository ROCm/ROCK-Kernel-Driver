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

#ifndef _H_ELX_CFGPARAM
#define _H_ELX_CFGPARAM

/* These are common configuration parameters */
/* The order of the ELX_CFG defs must match that of icfgparam[] entries */

#define ELX_CFG_LOG_VERBOSE               0	/* log-verbose */
#define ELX_CFG_LOG_ONLY                  1	/* log-only */
#define ELX_CFG_NUM_IOCBS                 2	/* num-iocbs */
#define ELX_CFG_NUM_BUFS                  3	/* num-bufs */
#define ELX_CFG_DFT_TGT_Q_DEPTH           4	/* tgt_queue_depth */
#define ELX_CFG_DFT_LUN_Q_DEPTH           5	/* lun_queue_depth */
#define ELX_CFG_EXTRA_IO_TMO              6	/* extra-io-tmo */
#define ELX_CFG_FIRST_CHECK               7	/* first-check */
#define ELX_CFG_NO_DEVICE_DELAY           8	/* no-device-delay */
#define ELX_CFG_LINKDOWN_TMO              9	/* linkdown-tmo */
#define ELX_CFG_HOLDIO                   10	/* nodev-holdio */
#define ELX_CFG_DELAY_RSP_ERR            11	/* delay-rsp-err */
#define ELX_CFG_CHK_COND_ERR             12	/* check-cond-err */
#define ELX_CFG_NODEV_TMO                13	/* nodev-tmo */
#define ELX_CFG_DQFULL_THROTTLE_UP_TIME  14	/* dqfull-throttle-up-time */
#define ELX_CFG_DQFULL_THROTTLE_UP_INC   15	/* dqfull-throttle-up-inc */
#define ELX_CFG_MAX_LUN                  16	/* max-lun */
#define ELX_CFG_DFT_HBA_Q_DEPTH          17	/* dft_hba_q_depth */
#define ELX_CFG_LUN_SKIP		 18
#define ELX_CORE_NUM_OF_CFG_PARAM        19

#endif				/* _H_ELX_CFGPARAM */
