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

#ifndef _H_ELX
#define _H_ELX

#include "elx_hw.h"
#include "elx_sli.h"
#include "elx_mem.h"
#include "elx_clock.h"

#include "elx_sched.h"

#define ELX_SLIM2_PAGE_AREA  8192

/* used for memory allocation */
#define ELX_MEM_DELAY   0
#define ELX_MEM_NDELAY  1

/*****************************************************************************/
/*                      device states                                        */
/*****************************************************************************/
#define CLOSED          0	/* initial device state */
#define DEAD            1	/* fatal hardware error encountered */
#define OPENED          4	/* opened successfully, functioning */

#define NORMAL_OPEN     0x0	/* opened in normal mode */

/***************************************************************************/
/*
 * This is the global device driver control structure
 */
/***************************************************************************/

struct elx_driver {
	ELXCLOCK_INFO_t elx_clock_info;	/* clock setup */
	unsigned long cflag;	/* used to hold context for clock lock, if needed */
	struct elxHBA *pHba[MAX_ELX_BRDS];	/* hba array */

	void *pDrvrOSEnv;

	uint16_t num_devs;	/* count of devices configed */
};
typedef struct elx_driver elxDRVR_t;

#if LITTLE_ENDIAN_HOST
#define SWAP_SHORT(x)   (x)
#define SWAP_LONG(x)    (x)
#define SWAP_DATA(x)    ((((x) & 0xFF)<<24) | (((x) & 0xFF00)<<8) | \
                        (((x) & 0xFF0000)>>8) | (((x) & 0xFF000000)>>24))
#define SWAP_DATA16(x)  ((((x) & 0xFF) << 8) | ((x) >> 8))
#define PCIMEM_SHORT(x) SWAP_SHORT(x)
#define PCIMEM_LONG(x)  SWAP_LONG(x)
#define PCIMEM_DATA(x)  SWAP_DATA(x)

#define putLunLow(lunlow, lun)              \
   {                                        \
   lunlow = 0;                              \
   }

#define putLunHigh(lunhigh, lun)            \
   {                                        \
   lunhigh = (uint32_t)(lun << 8);          \
   }

#else				/* BIG_ENDIAN_HOST */
#define SWAP_SHORT(x)   ((((x) & 0xFF) << 8) | ((x) >> 8))
#define SWAP_LONG(x)    ((((x) & 0xFF)<<24) | (((x) & 0xFF00)<<8) | \
                        (((x) & 0xFF0000)>>8) | (((x) & 0xFF000000)>>24))
#define SWAP_DATA(x)    (x)
#define SWAP_DATA16(x)  (x)

#ifdef BIU_BSE			/* This feature only makes sense for Big Endian */
#define PCIMEM_SHORT(x) (x)
#define PCIMEM_LONG(x)  (x)
#define PCIMEM_DATA(x)  ((((x) & 0xFF)<<24) | (((x) & 0xFF00)<<8) | \
                        (((x) & 0xFF0000)>>8) | (((x) & 0xFF000000)>>24))
#else
#define PCIMEM_SHORT(x) SWAP_SHORT(x)
#define PCIMEM_LONG(x)  SWAP_LONG(x)
#define PCIMEM_DATA(x)  SWAP_DATA(x)
#endif

#define putLunLow(lunlow, lun)              \
   {                                        \
   lunlow = 0;                              \
   }

#define putLunHigh(lunhigh, lun)            \
   {                                        \
   lunhigh = (uint32_t)(lun << 16);         \
   }
#endif

#define SWAP_ALWAYS(x)  ((((x) & 0xFF)<<24) | (((x) & 0xFF00)<<8) | \
                        (((x) & 0xFF0000)>>8) | (((x) & 0xFF000000)>>24))

#define SWAP_ALWAYS16(x) ((((x) & 0xFF) << 8) | ((x) >> 8))

/* CHECK */
#define FC_SCSID(pan, sid)    ((uint32_t)((pan << 16) | sid))	/* For logging */

/****************************************************************************/
/*      Device VPD save area                                                */
/****************************************************************************/
typedef struct elx_vpd {
	uint32_t status;	/* vpd status value */
	uint32_t length;	/* number of bytes actually returned */
	struct {
		uint32_t rsvd1;	/* Revision numbers */
		uint32_t biuRev;
		uint32_t smRev;
		uint32_t smFwRev;
		uint32_t endecRev;
		uint16_t rBit;
		uint8_t fcphHigh;
		uint8_t fcphLow;
		uint8_t feaLevelHigh;
		uint8_t feaLevelLow;
		uint32_t postKernRev;
		uint32_t opFwRev;
		uint8_t opFwName[16];
		uint32_t sli1FwRev;
		uint8_t sli1FwName[16];
		uint32_t sli2FwRev;
		uint8_t sli2FwName[16];
	} rev;
} elx_vpd_t;

typedef struct elx_cfgparam {
	char *a_string;
	uint32_t a_low;
	uint32_t a_hi;
	uint32_t a_default;
	uint32_t a_current;
	uint16_t a_flag;
	uint16_t a_changestate;
	char *a_help;
} elxCfgParam_t;

struct elxScsiLun;
struct elx_scsi_buf;

typedef struct elxHBA {
	uint8_t intr_inited;	/* flag for interrupt registration */
	struct elxHBA *nextHba;	/* point to the next device */
	uint32_t fc_ipri;	/* save priority */
	uint32_t hba_flag;	/* device flags */
#define FC_SCHED_CFG_INIT   0x2	/* schedule a call to fc_cfg_init() */
#define FC_STOP_IO          0x8	/* set for offline call */
#define FC_POLL_CMD         0x10	/* indicate to poll for command completion */
#define FC_LFR_ACTIVE       0x20	/* Link Failure recovery activated */
#define FC_NDISC_ACTIVE     0x40	/* Node discovery mode activated */

	struct elx_sli sli;
	DMABUF_t slim2p;

	uint32_t hba_state;

#define ELX_INIT_START           1	/* Initial state after board reset */
#define ELX_INIT_MBX_CMDS        2	/* Initialize HBA with mbox commands */
#define ELX_LINK_DOWN            3	/* HBA initialized, link is down */
#define ELX_LINK_UP              4	/* Link is up  - issue READ_LA */
#define ELX_LOCAL_CFG_LINK       5	/* local NPORT Id configured */
#define ELX_FLOGI                6	/* FLOGI sent to Fabric */
#define ELX_FABRIC_CFG_LINK      7	/* Fabric assigned NPORT Id configured */
#define ELX_NS_REG               8	/* Register with NameServer */
#define ELX_NS_QRY               9	/* Query NameServer for NPort ID list */
#define ELX_BUILD_DISC_LIST      10	/* Build ADISC and PLOGI lists for
					 * device authentication / discovery */
#define ELX_DISC_AUTH            11	/* Processing ADISC list */
#define ELX_CLEAR_LA             12	/* authentication cmplt - issue CLEAR_LA */
#define ELX_HBA_READY            32
#define ELX_HBA_ERROR            0xff

	void *pHbaProto;	/* Private hba struct pointer per driver type.  */
	elxCfgParam_t *config;	/* Configuration parameters */

	uint32_t bus_intr_lvl;
	uint32_t pci_id;
	elx_vpd_t vpd;		/* vital product data */

	unsigned long iflag;	/* used to hold context for drvr lock, if needed */

	void *pHbaOSEnv;

	uint8_t brd_no;		/* FC board number */
	uint8_t fc_busflag;	/* bus access flags */

	ELX_SCHED_HBA_t hbaSched;

	uint16_t maxRpi;

	uint32_t fcp_timeout_offset;

	char adaptermsg[FC_MAX_ADPTMSG];	/* adapter printf messages */

	char SerialNumber[32];	/* adapter Serial Number */
	char OptionROMVersion[32];	/* adapter BIOS / Fcode version */
	MEMSEG_t memseg[ELX_MAX_SEG];	/* memory for buffers / structures */

	struct elxScsiLun *(*elx_tran_find_lun) (struct elx_scsi_buf *);
	ELXCLOCK_t *dqfull_clk;
	ELXCLOCK_t *els_tmofunc;
	ELXCLOCK_t *ip_tmofunc;
	ELXCLOCK_t *scsi_tmofunc;

	/*
	 * HBA API 2.0 specific counters
	 */
	uint64_t fc4InputRequests;
	uint64_t fc4OutputRequests;
	uint64_t fc4ControlRequests;
} elxHBA_t;

#endif				/* _H_ELX */
