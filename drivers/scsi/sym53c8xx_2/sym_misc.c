/*
 * Device driver for the SYMBIOS/LSILOGIC 53C8XX and 53C1010 family 
 * of PCI-SCSI IO processors.
 *
 * Copyright (C) 1999-2001  Gerard Roudier <groudier@free.fr>
 *
 * This driver is derived from the Linux sym53c8xx driver.
 * Copyright (C) 1998-2000  Gerard Roudier
 *
 * The sym53c8xx driver is derived from the ncr53c8xx driver that had been 
 * a port of the FreeBSD ncr driver to Linux-1.2.13.
 *
 * The original ncr driver has been written for 386bsd and FreeBSD by
 *         Wolfgang Stanglmeier        <wolf@cologne.de>
 *         Stefan Esser                <se@mi.Uni-Koeln.de>
 * Copyright (C) 1994  Wolfgang Stanglmeier
 *
 * Other major contributions:
 *
 * NVRAM detection and reading.
 * Copyright (C) 1997 Richard Waltham <dormouse@farsrobt.demon.co.uk>
 *
 *-----------------------------------------------------------------------------
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef __FreeBSD__
#include <dev/sym/sym_glue.h>
#else
#include "sym_glue.h"
#endif

#ifdef	SYM_OPT_HANDLE_IO_TIMEOUT
/*
 *  Optional CCB timeout handling.
 *
 *  This code is useful for O/Ses that allow or expect 
 *  SIMs (low-level drivers) to handle SCSI IO timeouts.
 *  It uses a power-of-two based algorithm of my own:) 
 *  that avoids scanning of lists, provided that:
 *
 *  - The IO does complete in less than half the associated
 *    timeout value.
 *  - The greatest delay between the queuing of the IO and 
 *    its completion is less than 
 *          (1<<(SYM_CONF_TIMEOUT_ORDER_MAX-1))/2 ticks.
 *
 *  For example, if tick is 1 second and the max order is 8,
 *  any IO that is completed within less than 64 seconds will 
 *  just be put into some list at queuing and be removed 
 *  at completion without any additionnal overhead.
 */

/*
 *  Set a timeout condition on a CCB.
 */ 
void sym_timeout_ccb(hcb_p np, ccb_p cp, u_int ticks)
{
	sym_remque(&cp->tmo_linkq);
	cp->tmo_clock = np->tmo_clock + ticks;
	if (!ticks) {
		sym_insque_head(&cp->tmo_linkq, &np->tmo0_ccbq);
	}
	else {
		int i = SYM_CONF_TIMEOUT_ORDER_MAX - 1;
		while (i > 0) {
			if (ticks >= (1<<(i+1)))
				break;
			--i;
		}
		if (!(np->tmo_actq & (1<<i)))
			i += SYM_CONF_TIMEOUT_ORDER_MAX;
		sym_insque_head(&cp->tmo_linkq, &np->tmo_ccbq[i]);
	}
}

/*
 *  Walk a list of CCB and handle timeout conditions.
 *  Should never be called in normal situations.
 */
static void sym_walk_ccb_tmo_list(hcb_p np, SYM_QUEHEAD *tmoq)
{
	SYM_QUEHEAD qtmp, *qp;
	ccb_p cp;

	sym_que_move(tmoq, &qtmp);
	while ((qp = sym_remque_head(&qtmp)) != 0) {
		sym_insque_head(qp, &np->tmo0_ccbq);
		cp = sym_que_entry(qp, struct sym_ccb, tmo_linkq);
		if (cp->tmo_clock     != np->tmo_clock &&
		    cp->tmo_clock + 1 != np->tmo_clock)
			sym_timeout_ccb(np, cp, cp->tmo_clock - np->tmo_clock);
		else
			sym_abort_ccb(np, cp, 1);
	}
}

/*
 * Our clock handler called from the O/S specific side.
 */
void sym_clock(hcb_p np)
{
	int i, j;
	u_int tmp;

	tmp = np->tmo_clock;
	tmp ^= (++np->tmo_clock);

	for (i = 0; i < SYM_CONF_TIMEOUT_ORDER_MAX; i++, tmp >>= 1) {
		if (!(tmp & 1))
			continue;
		j = i;
		if (np->tmo_actq & (1<<i))
			j += SYM_CONF_TIMEOUT_ORDER_MAX;

		if (!sym_que_empty(&np->tmo_ccbq[j])) {
			sym_walk_ccb_tmo_list(np, &np->tmo_ccbq[j]);
		}
		np->tmo_actq ^= (1<<i);
	}
}
#endif	/* SYM_OPT_HANDLE_IO_TIMEOUT */


#ifdef	SYM_OPT_ANNOUNCE_TRANSFER_RATE
/*
 *  Announce transfer rate if anything changed since last announcement.
 */
void sym_announce_transfer_rate(hcb_p np, int target)
{
	tcb_p tp = &np->target[target];

#define __tprev	tp->tinfo.prev
#define __tcurr	tp->tinfo.curr

	if (__tprev.options  == __tcurr.options &&
	    __tprev.width    == __tcurr.width   &&
	    __tprev.offset   == __tcurr.offset  &&
	    !(__tprev.offset && __tprev.period != __tcurr.period))
		return;

	__tprev.options  = __tcurr.options;
	__tprev.width    = __tcurr.width;
	__tprev.offset   = __tcurr.offset;
	__tprev.period   = __tcurr.period;

	if (__tcurr.offset && __tcurr.period) {
		u_int period, f10, mb10;
		char *scsi;

		period = f10 = mb10 = 0;
		scsi = "FAST-5";

		if (__tcurr.period <= 9) {
			scsi = "FAST-80";
			period = 125;
			mb10 = 1600;
		}
		else {
			if	(__tcurr.period <= 11) {
				scsi = "FAST-40";
				period = 250;
				if (__tcurr.period == 11)
					period = 303;
			}
			else if	(__tcurr.period < 25) {
				scsi = "FAST-20";
				if (__tcurr.period == 12)
					period = 500;
			}
			else if	(__tcurr.period <= 50) {
				scsi = "FAST-10";
			}
			if (!period)
				period = 40 * __tcurr.period;
			f10 = 100000 << (__tcurr.width ? 1 : 0);
			mb10 = (f10 + period/2) / period;
		}
		printf_info (
		    "%s:%d: %s %sSCSI %d.%d MB/s %s%s%s (%d.%d ns, offset %d)\n",
		    sym_name(np), target, scsi, __tcurr.width? "WIDE " : "",
		    mb10/10, mb10%10,
		    (__tcurr.options & PPR_OPT_DT) ? "DT" : "ST",
		    (__tcurr.options & PPR_OPT_IU) ? " IU" : "",
		    (__tcurr.options & PPR_OPT_QAS) ? " QAS" : "",
		    period/10, period%10, __tcurr.offset);
	}
	else
		printf_info ("%s:%d: %sasynchronous.\n", 
		             sym_name(np), target, __tcurr.width? "wide " : "");
}
#undef __tprev
#undef __tcurr
#endif	/* SYM_OPT_ANNOUNCE_TRANSFER_RATE */
