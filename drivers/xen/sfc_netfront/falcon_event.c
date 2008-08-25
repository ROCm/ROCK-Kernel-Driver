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
 *  \brief  Routine to poll event queues.
 *   \date  2003/03/04
 */

/*! \cidoxg_lib_ef */
#include "ef_vi_internal.h"

/* Be worried about this on byteswapped machines */
/* Due to crazy chipsets, we see the event words being written in
** arbitrary order (bug4539).  So test for presence of event must ensure
** that both halves have changed from the null.
*/
# define EF_VI_IS_EVENT(evp)						\
	( (((evp)->opaque.a != (uint32_t)-1) &&				\
	   ((evp)->opaque.b != (uint32_t)-1)) )


#ifdef NDEBUG
# define IS_DEBUG 0
#else
# define IS_DEBUG 1
#endif


/*! Check for RX events with inconsistent SOP/CONT
**
** Returns true if this event should be discarded
*/
ef_vi_inline int ef_eventq_is_rx_sop_cont_bad_efab(ef_vi* vi,
						   const ef_vi_qword* ev)
{
	ef_rx_dup_state_t* rx_dup_state;
	uint8_t* bad_sop;

	unsigned label = QWORD_GET_U(RX_EV_Q_LABEL, *ev);
	unsigned sop   = QWORD_TEST_BIT(RX_SOP, *ev);
  
	ef_assert(vi);
	ef_assert_lt(label, EFAB_DMAQS_PER_EVQ_MAX);

	rx_dup_state = &vi->evq_state->rx_dup_state[label];
	bad_sop = &rx_dup_state->bad_sop;

	if( ! ((vi->vi_flags & EF_VI_BUG5692_WORKAROUND) || IS_DEBUG) ) {
		*bad_sop = (*bad_sop && !sop);
	}
	else {
		unsigned cont  = QWORD_TEST_BIT(RX_JUMBO_CONT, *ev);
		uint8_t *frag_num = &rx_dup_state->frag_num;

		/* bad_sop should latch till the next sop */
		*bad_sop = (*bad_sop && !sop) || ( !!sop != (*frag_num==0) );

		/* we do not check the number of bytes relative to the
		 * fragment number and size of the user rx buffer here
		 * because we don't know the size of the user rx
		 * buffer - we probably should perform this check in
		 * the nearest code calling this though.
		 */
		*frag_num = cont ? (*frag_num + 1) : 0;
	}

	return *bad_sop;
}


ef_vi_inline int falcon_rx_check_dup(ef_vi* evq, ef_event* ev_out,
				     const ef_vi_qword* ev)
{
	unsigned q_id = QWORD_GET_U(RX_EV_Q_LABEL, *ev);
	unsigned desc_ptr = QWORD_GET_U(RX_EV_DESC_PTR, *ev);
	ef_rx_dup_state_t* rx_dup_state = &evq->evq_state->rx_dup_state[q_id];

	if(likely( desc_ptr != rx_dup_state->rx_last_desc_ptr )) {
		rx_dup_state->rx_last_desc_ptr = desc_ptr;
		return 0;
	}

	rx_dup_state->rx_last_desc_ptr = desc_ptr;
	rx_dup_state->bad_sop = 1;
#ifndef NDEBUG
	rx_dup_state->frag_num = 0;
#endif
	BUG_ON(!QWORD_TEST_BIT(RX_EV_FRM_TRUNC, *ev));
	BUG_ON( QWORD_TEST_BIT(RX_EV_PKT_OK, *ev));
	BUG_ON(!QWORD_GET_U(RX_EV_BYTE_CNT, *ev) == 0);
	ev_out->rx_no_desc_trunc.type = EF_EVENT_TYPE_RX_NO_DESC_TRUNC;
	ev_out->rx_no_desc_trunc.q_id = q_id;
	return 1;
}


ef_vi_inline void falcon_rx_event(ef_event* ev_out, const ef_vi_qword* ev)
{
	if(likely( QWORD_TEST_BIT(RX_EV_PKT_OK, *ev) )) {
		ev_out->rx.type = EF_EVENT_TYPE_RX;
		ev_out->rx.q_id = QWORD_GET_U(RX_EV_Q_LABEL, *ev);
		ev_out->rx.len  = QWORD_GET_U(RX_EV_BYTE_CNT, *ev);
		if( QWORD_TEST_BIT(RX_SOP, *ev) )
			ev_out->rx.flags = EF_EVENT_FLAG_SOP;
		else
			ev_out->rx.flags = 0;
		if( QWORD_TEST_BIT(RX_JUMBO_CONT, *ev) )
			ev_out->rx.flags |= EF_EVENT_FLAG_CONT;
		if( QWORD_TEST_BIT(RX_iSCSI_PKT_OK, *ev) )
			ev_out->rx.flags |= EF_EVENT_FLAG_ISCSI_OK;
	}
	else {
		ev_out->rx_discard.type = EF_EVENT_TYPE_RX_DISCARD;
		ev_out->rx_discard.q_id = QWORD_GET_U(RX_EV_Q_LABEL, *ev);
		ev_out->rx_discard.len  = QWORD_GET_U(RX_EV_BYTE_CNT, *ev);
#if 1  /* hack for ptloop compatability: ?? TODO purge */
		if( QWORD_TEST_BIT(RX_SOP, *ev) )
			ev_out->rx_discard.flags = EF_EVENT_FLAG_SOP;
		else
			ev_out->rx_discard.flags = 0;
		if( QWORD_TEST_BIT(RX_JUMBO_CONT, *ev) )
			ev_out->rx_discard.flags |= EF_EVENT_FLAG_CONT;
		if( QWORD_TEST_BIT(RX_iSCSI_PKT_OK, *ev) )
			ev_out->rx_discard.flags |= EF_EVENT_FLAG_ISCSI_OK;
#endif
		/* Order matters here: more fundamental errors first. */
		if( QWORD_TEST_BIT(RX_EV_BUF_OWNER_ID_ERR, *ev) )
			ev_out->rx_discard.subtype = 
				EF_EVENT_RX_DISCARD_RIGHTS;
		else if( QWORD_TEST_BIT(RX_EV_FRM_TRUNC, *ev) )
			ev_out->rx_discard.subtype = 
				EF_EVENT_RX_DISCARD_TRUNC;
		else if( QWORD_TEST_BIT(RX_EV_ETH_CRC_ERR, *ev) )
			ev_out->rx_discard.subtype = 
				EF_EVENT_RX_DISCARD_CRC_BAD;
		else if( QWORD_TEST_BIT(RX_EV_IP_HDR_CHKSUM_ERR, *ev) )
			ev_out->rx_discard.subtype = 
				EF_EVENT_RX_DISCARD_CSUM_BAD;
		else if( QWORD_TEST_BIT(RX_EV_TCP_UDP_CHKSUM_ERR, *ev) )
			ev_out->rx_discard.subtype = 
				EF_EVENT_RX_DISCARD_CSUM_BAD;
		else
			ev_out->rx_discard.subtype = 
				EF_EVENT_RX_DISCARD_OTHER;
	}
}


ef_vi_inline void falcon_tx_event(ef_event* ev_out, const ef_vi_qword* ev)
{
	/* Danger danger!  No matter what we ask for wrt batching, we
	** will get a batched event every 16 descriptors, and we also
	** get dma-queue-empty events.  i.e. Duplicates are expected.
	**
	** In addition, if it's been requested in the descriptor, we
	** get an event per descriptor.  (We don't currently request
	** this).
	*/
	if(likely( QWORD_TEST_BIT(TX_EV_COMP, *ev) )) {
		ev_out->tx.type = EF_EVENT_TYPE_TX;
		ev_out->tx.q_id = QWORD_GET_U(TX_EV_Q_LABEL, *ev);
	}
	else {
		ev_out->tx_error.type = EF_EVENT_TYPE_TX_ERROR;
		ev_out->tx_error.q_id = QWORD_GET_U(TX_EV_Q_LABEL, *ev);
		if(likely( QWORD_TEST_BIT(TX_EV_BUF_OWNER_ID_ERR, *ev) ))
			ev_out->tx_error.subtype = EF_EVENT_TX_ERROR_RIGHTS;
		else if(likely( QWORD_TEST_BIT(TX_EV_WQ_FF_FULL, *ev) ))
			ev_out->tx_error.subtype = EF_EVENT_TX_ERROR_OFLOW;
		else if(likely( QWORD_TEST_BIT(TX_EV_PKT_TOO_BIG, *ev) ))
			ev_out->tx_error.subtype = EF_EVENT_TX_ERROR_2BIG;
		else if(likely( QWORD_TEST_BIT(TX_EV_PKT_ERR, *ev) ))
			ev_out->tx_error.subtype = EF_EVENT_TX_ERROR_BUS;
	}
}


static void mark_bad(ef_event* ev)
{
	ev->generic.ev.u64[0] &=~ ((uint64_t) 1u << RX_EV_PKT_OK_LBN);
}


int ef_eventq_poll_evs(ef_vi* evq, ef_event* evs, int evs_len,
		       ef_event_handler_fn *exception, void *expt_priv)
{
	int evs_len_orig = evs_len;

	EF_VI_CHECK_EVENT_Q(evq);
	ef_assert(evs);
	ef_assert_gt(evs_len, 0);

	if(unlikely( EF_VI_IS_EVENT(EF_VI_EVENT_PTR(evq, 1)) ))
		goto overflow;

	do {
		{ /* Read the event out of the ring, then fiddle with
		   * copied version.  Reason is that the ring is
		   * likely to get pushed out of cache by another
		   * event being delivered by hardware. */
			ef_vi_event* ev = EF_VI_EVENT_PTR(evq, 0);
			if( ! EF_VI_IS_EVENT(ev) )
				break;
			evs->generic.ev.u64[0] = cpu_to_le64 (ev->u64);
			evq->evq_state->evq_ptr += sizeof(ef_vi_event);
			ev->u64 = (uint64_t)(int64_t) -1;
		}

		/* Ugly: Exploit the fact that event code lies in top
		 * bits of event. */
		ef_assert_ge(EV_CODE_LBN, 32u);
		switch( evs->generic.ev.u32[1] >> (EV_CODE_LBN - 32u) ) {
		case RX_IP_EV_DECODE:
			/* Look for duplicate desc_ptr: it signals
			 * that a jumbo frame was truncated because we
			 * ran out of descriptors. */
			if(unlikely( falcon_rx_check_dup
					   (evq, evs, &evs->generic.ev) )) {
				--evs_len;
				++evs;
				break;
			}
			else {
				/* Cope with FalconA1 bugs where RX
				 * gives inconsistent RX events Mark
				 * events as bad until SOP becomes
				 * consistent again
				 * ef_eventq_is_rx_sop_cont_bad() has
				 * side effects - order is important
				 */
				if(unlikely
				   (ef_eventq_is_rx_sop_cont_bad_efab
				    (evq, &evs->generic.ev) )) {
					mark_bad(evs);
				}
			}
			falcon_rx_event(evs, &evs->generic.ev);
			--evs_len;	
			++evs;
			break;

		case TX_IP_EV_DECODE:
			falcon_tx_event(evs, &evs->generic.ev);
			--evs_len;
			++evs;
			break;

		default:
			break;
		}
	} while( evs_len );

	return evs_len_orig - evs_len;


 overflow:
	evs->generic.type = EF_EVENT_TYPE_OFLOW;
	evs->generic.ev.u64[0] = (uint64_t)((int64_t)-1);
	return 1;
}


int/*bool*/ ef_eventq_poll_exception(void* priv, ef_vi* evq, ef_event* ev)
{
	int /*bool*/ handled = 0;
  
	switch( ev->generic.ev.u32[1] >> (EV_CODE_LBN - 32u) ) {
	case DRIVER_EV_DECODE:
		if( QWORD_GET_U(DRIVER_EV_SUB_CODE, ev->generic.ev) ==
		    EVQ_INIT_DONE_EV_DECODE )
			/* EVQ initialised event: ignore. */
			handled = 1;
		break;
	}
	return handled;
}


void ef_eventq_iterate(ef_vi* vi,
		       void (*fn)(void* arg, ef_vi*, int rel_pos,
				  int abs_pos, void* event),
		       void* arg, int stop_at_end)
{
	int i, size_evs = (vi->evq_mask + 1) / sizeof(ef_vi_event);

	for( i = 0; i < size_evs; ++i ) {
		ef_vi_event* e = EF_VI_EVENT_PTR(vi, -i);
		if( EF_VI_IS_EVENT(e) )
			fn(arg, vi, i, 
			   EF_VI_EVENT_OFFSET(vi, -i) / sizeof(ef_vi_event),
			   e);
		else if( stop_at_end )
			break;
	}
}


int ef_eventq_has_event(ef_vi* vi)
{
	return EF_VI_IS_EVENT(EF_VI_EVENT_PTR(vi, 0));
}


int ef_eventq_has_many_events(ef_vi* vi, int look_ahead)
{
	ef_assert_ge(look_ahead, 0);
	return EF_VI_IS_EVENT(EF_VI_EVENT_PTR(vi, -look_ahead));
}


int ef_eventq_has_rx_event(ef_vi* vi)
{
	ef_vi_event* ev;
	int i, n_evs = 0;

	for( i = 0;  EF_VI_IS_EVENT(EF_VI_EVENT_PTR(vi, i)); --i ) {
		ev = EF_VI_EVENT_PTR(vi, i);
		if( EFVI_FALCON_EVENT_CODE(ev) == EF_EVENT_TYPE_RX )  n_evs++;
	}
	return n_evs;
}

/*! \cidoxg_end */
