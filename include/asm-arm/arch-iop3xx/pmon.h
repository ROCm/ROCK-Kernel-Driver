/*
 * Definitions for XScale 80312 PMON
 * (C) 2001 Intel Corporation
 * Author: Chen Chen(chen.chen@intel.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _IOP310_PMON_H_
#define _IOP310_PMON_H_

/*
 *  Different modes for Event Select Register for intel 80312
 */

#define IOP310_PMON_MODE0                0x00000000
#define IOP310_PMON_MODE1                0x00000001
#define IOP310_PMON_MODE2                0x00000002
#define IOP310_PMON_MODE3                0x00000003
#define IOP310_PMON_MODE4                0x00000004
#define IOP310_PMON_MODE5                0x00000005
#define IOP310_PMON_MODE6                0x00000006
#define IOP310_PMON_MODE7                0x00000007

typedef struct _iop310_pmon_result
{
	u32 timestamp;			/* Global Time Stamp Register */
	u32 timestamp_overflow;		/* Time Stamp overflow count */
	u32 event_count[14];		/* Programmable Event Counter
					   Registers 1-14 */
	u32 event_overflow[14];		/* Overflow counter for PECR1-14 */
} iop310_pmon_res_t;

/* function prototypes */

/* Claim IQ80312 PMON for usage */
int iop310_pmon_claim(void);

/* Start IQ80312 PMON */
int iop310_pmon_start(int, int);

/* Stop Performance Monitor Unit */
int iop310_pmon_stop(iop310_pmon_res_t *);

/* Release IQ80312 PMON */
int iop310_pmon_release(int);

#endif
