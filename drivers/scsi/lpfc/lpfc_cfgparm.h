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

#ifndef _H_LPFC_CFGPARAM
#define _H_LPFC_CFGPARAM

#define LPFC_DFT_POST_IP_BUF            128
#define LPFC_MIN_POST_IP_BUF            64
#define LPFC_MAX_POST_IP_BUF            1024
#define LPFC_DFT_XMT_QUE_SIZE           256
#define LPFC_MIN_XMT_QUE_SIZE           128
#define LPFC_MAX_XMT_QUE_SIZE           10240
#define LPFC_DFT_NUM_IOCBS              256
#define LPFC_MIN_NUM_IOCBS              128
#define LPFC_MAX_NUM_IOCBS              10240
#define LPFC_DFT_NUM_BUFS               128
#define LPFC_MIN_NUM_BUFS               64
#define LPFC_MAX_NUM_BUFS               4096
#define LPFC_DFT_NUM_NODES              510
#define LPFC_MIN_NUM_NODES              64
#define LPFC_MAX_NUM_NODES              4096
#define LPFC_DFT_TOPOLOGY               0
#define LPFC_DFT_FC_CLASS               3

#define LPFC_DFT_NO_DEVICE_DELAY        1	/* 1 sec */
#define LPFC_MAX_NO_DEVICE_DELAY        30	/* 30 sec */
#define LPFC_DFT_EXTRA_IO_TIMEOUT       0
#define LPFC_MAX_EXTRA_IO_TIMEOUT       255	/* 255 sec */
#define LPFC_DFT_LNKDWN_TIMEOUT         30
#define LPFC_MAX_LNKDWN_TIMEOUT         255	/* 255 sec */
#define LPFC_DFT_NODEV_TIMEOUT          30
#define LPFC_MAX_NODEV_TIMEOUT          255	/* 255 sec */
#define LPFC_DFT_RSCN_NS_DELAY          0
#define LPFC_MAX_RSCN_NS_DELAY          255	/* 255 sec */

#define LPFC_MAX_HBA_Q_DEPTH            10240	/* max cmds allowed per hba */
#define LPFC_DFT_HBA_Q_DEPTH            2048	/* max cmds per hba */
#define LPFC_LC_HBA_Q_DEPTH             1024	/* max cmds per low cost hba */
#define LPFC_LP101_HBA_Q_DEPTH          128	/* max cmds per low cost hba */

#define LPFC_MAX_TGT_Q_DEPTH            10240	/* max cmds allowed per tgt */
#define LPFC_DFT_TGT_Q_DEPTH            0	/* default max cmds per tgt */

#define LPFC_MAX_LUN_Q_DEPTH            128	/* max cmds to allow per lun */
#define LPFC_DFT_LUN_Q_DEPTH            30	/* default max cmds per lun */

#define LPFC_MAX_DQFULL_THROTTLE        1	/* Boolean (max value) */

#define LPFC_MAX_DISC_THREADS           64	/* max outstanding discovery els requests */
#define LPFC_DFT_DISC_THREADS           1	/* default outstanding discovery els requests */

#define LPFC_MAX_NS_RETRY               3	/* Try to get to the NameServer 3 times and then give up. */

#define LPFC_MAX_SCSI_REQ_TMO           255	/* Max timeout value for SCSI passthru requests */
#define LPFC_DFT_SCSI_REQ_TMO           30	/* Default timeout value for SCSI passthru requests */

#define LPFC_MAX_TARGET                 255	/* max nunber of targets supported */
#define LPFC_DFT_MAX_TARGET             255	/* default max number of targets supported */

/* LPFC specific parameters start at ELX_CORE_NUM_OF_CFG_PARAM */
#define LPFC_CFG_AUTOMAP          ELX_CORE_NUM_OF_CFG_PARAM + 0	/* automap */
#define LPFC_CFG_FCP_CLASS        ELX_CORE_NUM_OF_CFG_PARAM + 1	/* fcp-class */
#define LPFC_CFG_USE_ADISC        ELX_CORE_NUM_OF_CFG_PARAM + 2	/* use-adisc */
#define LPFC_CFG_NETWORK_ON       ELX_CORE_NUM_OF_CFG_PARAM + 3	/* network-on */
#define LPFC_CFG_POST_IP_BUF      ELX_CORE_NUM_OF_CFG_PARAM + 4	/* post-ip-buf */
#define LPFC_CFG_XMT_Q_SIZE       ELX_CORE_NUM_OF_CFG_PARAM + 5	/* xmt-que-size */
#define LPFC_CFG_IP_CLASS         ELX_CORE_NUM_OF_CFG_PARAM + 6	/* ip-class */
#define LPFC_CFG_ACK0             ELX_CORE_NUM_OF_CFG_PARAM + 7	/* ack0 */
#define LPFC_CFG_TOPOLOGY         ELX_CORE_NUM_OF_CFG_PARAM + 8	/* topology */
#define LPFC_CFG_SCAN_DOWN        ELX_CORE_NUM_OF_CFG_PARAM + 9	/* scan-down */
#define LPFC_CFG_LINK_SPEED       ELX_CORE_NUM_OF_CFG_PARAM + 10	/* link-speed */
#define LPFC_CFG_CR_DELAY         ELX_CORE_NUM_OF_CFG_PARAM + 11	/* cr-delay */
#define LPFC_CFG_CR_COUNT         ELX_CORE_NUM_OF_CFG_PARAM + 12	/* cr-count */
#define LPFC_CFG_FDMI_ON          ELX_CORE_NUM_OF_CFG_PARAM + 13	/* fdmi-on-count */
#define LPFC_CFG_BINDMETHOD       ELX_CORE_NUM_OF_CFG_PARAM + 14	/* fcp-bind-method */
#define LPFC_CFG_DISC_THREADS     ELX_CORE_NUM_OF_CFG_PARAM + 15	/* discovery-threads */
#define LPFC_CFG_SCSI_REQ_TMO     ELX_CORE_NUM_OF_CFG_PARAM + 16	/* timeout value for SCSI passtru */

#define LPFC_CFG_MAX_TARGET       ELX_CORE_NUM_OF_CFG_PARAM + 17	/* max-target */

#define LPFC_NUM_OF_CFG_PARAM     18

/* Note: The following define LPFC_TOTAL_NUM_OF_CFG_PARAM represents the total number
         of user configuration params. This define is used to specify the number 
         of entries in the array lpfc_icfgparam[].
 */
#define LPFC_TOTAL_NUM_OF_CFG_PARAM  ELX_CORE_NUM_OF_CFG_PARAM + LPFC_NUM_OF_CFG_PARAM

/* The order of the icfgparam[] entries must match that of ELX_CORE_CFG defs */
#ifdef LPFC_DEF_ICFG
iCfgParam lpfc_icfgparam[LPFC_TOTAL_NUM_OF_CFG_PARAM] = {

	/* general driver parameters */
	{"log-verbose",
	 0, 0xffff, FALSE, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Verbose logging bit-mask"},

	{"num-iocbs",
	 LPFC_MIN_NUM_IOCBS, LPFC_MAX_NUM_IOCBS, LPFC_DFT_NUM_IOCBS, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "Number of outstanding IOCBs driver can queue to adapter"},

	{"num-bufs",
	 LPFC_MIN_NUM_BUFS, LPFC_MAX_NUM_BUFS, LPFC_DFT_NUM_BUFS, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "Number of buffers driver uses for ELS commands and Buffer Pointer Lists."},

	{"tgt-queue-depth",
	 0, LPFC_MAX_TGT_Q_DEPTH, LPFC_DFT_TGT_Q_DEPTH, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "Max number of FCP commands we can queue to a specific target"},

	{"lun-queue-depth",
	 1, LPFC_MAX_LUN_Q_DEPTH, LPFC_DFT_LUN_Q_DEPTH, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "Max number of FCP commands we can queue to a specific LUN"},

	{"extra-io-tmo",
	 0, LPFC_MAX_EXTRA_IO_TIMEOUT, 0, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Extra FCP command timeout"},

	{"no-device-delay",
	 0, LPFC_MAX_NO_DEVICE_DELAY, LPFC_DFT_NO_DEVICE_DELAY, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Length of interval in seconds for FCP device I/O failure"},

	{"linkdown-tmo",
	 0, LPFC_MAX_LNKDWN_TIMEOUT, LPFC_DFT_LNKDWN_TIMEOUT, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Seconds driver will wait before deciding link is really down"},

	{"nodev-holdio",
	 0, 1, FALSE, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Hold I/O errors if device disappears "},

	{"delay-rsp-err",
	 0, 1, FALSE, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Delay FCP error return for FCP RSP error and Check Condition"},

	{"check-cond-err",
	 0, 1, FALSE, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Treat special Check Conditions as a FCP error"},

	{"nodev-tmo",
	 0, LPFC_MAX_NODEV_TIMEOUT, LPFC_DFT_NODEV_TIMEOUT, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Seconds driver will hold I/O waiting for a device to come back"},

	{"dqfull-throttle-up-time",
	 0, 30, 1, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "When to increment the current Q depth "},

	{"dqfull-throttle-up-inc",
	 0, LPFC_MAX_LUN_Q_DEPTH, 1, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Increment the current Q depth by dqfull-throttle-up-inc"},

	{"max-lun",
	 0, 127, 127, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "The maximun LUN number a target can support"},

	{"hba-queue-depth",
	 0, LPFC_MAX_HBA_Q_DEPTH, 0, 0,
	 (ushort) (CFG_IGNORE),
	 (ushort) CFG_RESTART,
	 "Max number of FCP commands we can queue to a specific HBA"},

	{"lun-skip",
	 0, 1, FALSE, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "Enable SCSI layer to scan past lun holes"},

	/* Start of product specific (lpfc) config params */

	{"automap",
	 0, 1, 1, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "Automatically bind FCP devices as they are discovered"},

	{"fcp-class",
	 2, 3, LPFC_DFT_FC_CLASS, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Select Fibre Channel class of service for FCP sequences"},

	{"use-adisc",
	 0, 1, FALSE, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Use ADISC on rediscovery to authenticate FCP devices"},

	/* IP specific parameters */
	{"network-on",
	 0, 1, FALSE, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_REBOOT,
	 "Enable IP processing"},

	{"post-ip-buf",
	 LPFC_MIN_POST_IP_BUF, LPFC_MAX_POST_IP_BUF, LPFC_DFT_POST_IP_BUF, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "Number of IP buffers to post to adapter"},

	{"xmt-que-size",
	 LPFC_MIN_XMT_QUE_SIZE, LPFC_MAX_XMT_QUE_SIZE, LPFC_DFT_XMT_QUE_SIZE, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "Number of outstanding IP cmds for an adapter"},

	{"ip-class",
	 2, 3, LPFC_DFT_FC_CLASS, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Select Fibre Channel class of service for IP sequences"},

	/* Fibre Channel specific parameters */
	{"ack0",
	 0, 1, FALSE, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "Enable ACK0 support"},

	{"topology",
	 0, 6, LPFC_DFT_TOPOLOGY, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "Select Fibre Channel topology"},

	{"scan-down",
	 0, 1, 1, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Start scanning for devices from highest ALPA to lowest"},

	{"link-speed",
	 0, 2, 0, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "Select link speed"},

	{"cr-delay",
	 0, 63, 0, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "A count of milliseconds after which an interrupt response is generated"},

	{"cr-count",
	 1, 255, 1, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "A count of I/O completions after which an interrupt response is generated"},

	{"fdmi-on",
	 0, 2, FALSE, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "Enable FDMI support"},

	{"fcp-bind-method",
	 1, 4, 2, 2,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "Select the bind method to be used."},

	{"discovery-threads",
	 1, LPFC_MAX_DISC_THREADS, LPFC_DFT_DISC_THREADS, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "Maximum number of ELS commands during discovery"},

	{"scsi-req-tmo",
	 0, LPFC_MAX_SCSI_REQ_TMO, LPFC_DFT_SCSI_REQ_TMO, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Timeout value for SCSI passthru requests"},

	{"max-target",
	 0, LPFC_MAX_TARGET, LPFC_DFT_MAX_TARGET, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "The maximun number of targets an adapter can support"},
};
#endif				/* LPFC_DEF_ICFG */

#endif				/* _H_LPFC_CFGPARAM */
