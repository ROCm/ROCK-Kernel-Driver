/****************************************************************************
 * Copyright 2002-2005: Level 5 Networks Inc.
 * Copyright 2005-2008: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Maintained by Solarflare Communications
 *  <linux-xen-drivers@solarflare.com>
 *  <onload-dev@solarflare.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 ****************************************************************************
 */

/*
 * \author  djr
 *  \brief  Packet-mode transmit interface.
 *   \date  2003/04/02
 */

/*! \cidoxg_lib_ef */
#include "ef_vi_internal.h"


int ef_vi_transmit_init(ef_vi* vi, ef_addr base, int len, ef_request_id dma_id)
{
	ef_iovec iov = { base, len };
	return ef_vi_transmitv_init(vi, &iov, 1, dma_id);
}


int ef_vi_transmit(ef_vi* vi, ef_addr base, int len, ef_request_id dma_id)
{
	ef_iovec iov = { base, len };
	int rc = ef_vi_transmitv_init(vi, &iov, 1, dma_id);
	if( rc == 0 )  ef_vi_transmit_push(vi);
	return rc;
}


int ef_vi_transmitv(ef_vi* vi, const ef_iovec* iov, int iov_len,
                    ef_request_id dma_id)
{
	int rc = ef_vi_transmitv_init(vi, iov, iov_len, dma_id);
	if( rc == 0 )  ef_vi_transmit_push(vi);
	return rc;
}


int ef_vi_transmit_unbundle(ef_vi* vi, const ef_event* __ev,
			    ef_request_id* ids)
{
	ef_request_id* ids_in = ids;
	ef_vi_txq* q = &vi->vi_txq;
	ef_vi_txq_state* qs = &vi->ep_state->txq;
	const ef_vi_qword* ev = EF_GET_HW_EV_PTR(*__ev);
	unsigned i, stop = (ev->u32[0] + 1) & q->mask;

	ef_assert(EF_EVENT_TYPE(*__ev) == EF_EVENT_TYPE_TX ||
		  EF_EVENT_TYPE(*__ev) == EF_EVENT_TYPE_TX_ERROR);

	/* Shouldn't be batching more than 64 descriptors, and should not go
	** backwards. */
	ef_assert_le((((ev->u32[0] + 1) - qs->removed) & q->mask), 64);
	/* Should not complete more than we've posted. */
	ef_assert_le((((ev->u32[0] + 1) - qs->removed) & q->mask),
		     qs->added - qs->removed);

	for( i = qs->removed & q->mask; i != stop; i = ++qs->removed & q->mask )
		if( q->ids[i] != 0xffff ) {
			*ids++ = q->ids[i];
			q->ids[i] = 0xffff;
		}

	ef_assert_le(ids - ids_in, EF_VI_TRANSMIT_BATCH);

	return (int) (ids - ids_in);
}

/*! \cidoxg_end */
