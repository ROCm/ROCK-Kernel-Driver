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

#ifndef _H_ELX_CLOCK
#define _H_ELX_CLOCK

#define MIN_CLK_BLKS    256

/* Structures using for clock / timeout handling */
struct elxclock {
	struct elxclock *cl_fw;	/* forward linkage */
	union {
		struct {
			uint16_t cl_soft_arg;
			uint16_t cl_soft_cmd;
		} c1;
		struct elxclock *cl_bw;	/* backward linkage */
	} un;
	uint32_t cl_tix;	/* differential number of clock ticks */
	void (*cl_func) (void *, void *, void *);
	void *cl_phba;
	void *cl_arg1;		/* argument 1 to function */
	void *cl_arg2;		/* argument 2 to function */
};

typedef struct elxclock ELXCLOCK_t;

#define cl_bw         un.cl_bw

typedef struct elxclock_info {
	ELX_DLINK_t elx_clkhdr;
	uint32_t ticks;		/* elapsed time since initialization */
	uint32_t tmr_ct;	/* Timer expired count */
	uint32_t timestamp[2];	/* SMT 64 bit timestamp */
	void *clktimer;		/* used for scheduling clock routine */
} ELXCLOCK_INFO_t;

#endif				/* _H_ELX_CLOCK */
