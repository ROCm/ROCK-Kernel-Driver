/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file contains event queue support.
 *
 * Copyright 2005-2007: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Developed and maintained by Solarflare Communications:
 *                      <linux-xen-drivers@solarflare.com>
 *                      <onload-dev@solarflare.com>
 *
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

#include <ci/efhw/debug.h>
#include <ci/efhw/iopage.h>
#include <ci/driver/efab/hardware.h>
#include <ci/efhw/eventq.h>
#include <ci/efhw/falcon.h>
#include <ci/efhw/nic.h>

#define KEVENTQ_MAGIC 0x07111974

/*! Helper function to allocate the iobuffer needed by an eventq
 *   - it ensures the eventq has the correct alignment for the NIC
 *
 * \param rm        Event-queue resource manager
 * \param instance  Event-queue instance (index)
 * \param buf_bytes Requested size of eventq
 * \return          < 0 if iobuffer allocation fails
 */
int
efhw_nic_event_queue_alloc_iobuffer(struct efhw_nic *nic,
				    struct eventq_resource_hardware *h,
				    int evq_instance, unsigned buf_bytes)
{
	unsigned int page_order;
	int rc;

	/* Allocate an iobuffer. */
	page_order = get_order(buf_bytes);

	h->iobuff_off = 0;

	EFHW_TRACE("allocating eventq size %x",
		   1u << (page_order + PAGE_SHIFT));
	rc = efhw_iopages_alloc(nic, &h->iobuff, page_order);
	if (rc < 0) {
		EFHW_WARN("%s: failed to allocate %u pages",
			  __func__, 1u << page_order);
		return rc;
	}

	/* Set the eventq pages to match EFHW_CLEAR_EVENT() */
	if (EFHW_CLEAR_EVENT_VALUE)
		memset(efhw_iopages_ptr(&h->iobuff) + h->iobuff_off,
		       EFHW_CLEAR_EVENT_VALUE, (1u << page_order) * PAGE_SIZE);

	EFHW_TRACE("%s: allocated %u pages", __func__, 1u << (page_order));

	/* For Falcon the NIC is programmed with the base buffer address of a
	 * contiguous region of buffer space. This means that larger than a
	 * PAGE event queues can be expected to allocate even when the host's
	 * physical memory is fragmented */
	EFHW_ASSERT(efhw_nic_have_hw(nic));
	EFHW_ASSERT(page_order <= h->buf_tbl_alloc.order);

	/* Initialise the buffer table entries. */
	falcon_nic_buffer_table_set_n(nic, h->buf_tbl_alloc.base,
				      efhw_iopages_dma_addr(&h->iobuff) +
				      h->iobuff_off, EFHW_NIC_PAGE_SIZE, 0,
				      1 << page_order, 0);

	if (evq_instance >= FALCON_EVQ_TBL_RESERVED)
		falcon_nic_buffer_table_confirm(nic);
	return 0;
}

/**********************************************************************
 * Kernel event queue management.
 */

/* Values for [struct efhw_keventq::lock] field. */
#define KEVQ_UNLOCKED      0
#define KEVQ_LOCKED        1
#define KEVQ_RECHECK       2

int
efhw_keventq_ctor(struct efhw_nic *nic, int instance,
		  struct efhw_keventq *evq,
		  struct efhw_ev_handler *ev_handlers)
{
	int rc;
	unsigned buf_bytes = evq->hw.capacity * sizeof(efhw_event_t);

	evq->instance = instance;
	evq->ev_handlers = ev_handlers;

	/* allocate an IObuffer for the eventq */
	rc = efhw_nic_event_queue_alloc_iobuffer(nic, &evq->hw, evq->instance,
						 buf_bytes);
	if (rc < 0)
		return rc;

	/* Zero the timer-value for this queue.
	   AND Tell the nic about the event queue. */
	efhw_nic_event_queue_enable(nic, evq->instance, evq->hw.capacity,
				    efhw_iopages_dma_addr(&evq->hw.iobuff) +
				    evq->hw.iobuff_off,
				    evq->hw.buf_tbl_alloc.base,
				    1 /* interrupting */);

	evq->lock = KEVQ_UNLOCKED;
	evq->evq_base = efhw_iopages_ptr(&evq->hw.iobuff) + evq->hw.iobuff_off;
	evq->evq_ptr = 0;
	evq->evq_mask = (evq->hw.capacity * sizeof(efhw_event_t)) - 1u;

	EFHW_TRACE("%s: [%d] base=%p end=%p", __func__, evq->instance,
		   evq->evq_base, evq->evq_base + buf_bytes);

	return 0;
}

void efhw_keventq_dtor(struct efhw_nic *nic, struct efhw_keventq *evq)
{
	EFHW_ASSERT(evq);

	EFHW_TRACE("%s: [%d]", __func__, evq->instance);

	/* Zero the timer-value for this queue.
	   And Tell NIC to stop using this event queue. */
	efhw_nic_event_queue_disable(nic, evq->instance, 0);

	/* free the pages used by the eventq itself */
	efhw_iopages_free(nic, &evq->hw.iobuff);
}

void
efhw_handle_txdmaq_flushed(struct efhw_nic *nic, struct efhw_ev_handler *h,
			   efhw_event_t *evp)
{
	int instance = (int)FALCON_EVENT_TX_FLUSH_Q_ID(evp);
	EFHW_TRACE("%s: instance=%d", __func__, instance);

	if (!h->dmaq_flushed_fn) {
		EFHW_WARN("%s: no handler registered", __func__);
		return;
	}

	h->dmaq_flushed_fn(nic, instance, false);
}

void
efhw_handle_rxdmaq_flushed(struct efhw_nic *nic, struct efhw_ev_handler *h,
			   efhw_event_t *evp)
{
	unsigned instance = (unsigned)FALCON_EVENT_RX_FLUSH_Q_ID(evp);
	EFHW_TRACE("%s: instance=%d", __func__, instance);

	if (!h->dmaq_flushed_fn) {
		EFHW_WARN("%s: no handler registered", __func__);
		return;
	}

	h->dmaq_flushed_fn(nic, instance, true);
}

void
efhw_handle_wakeup_event(struct efhw_nic *nic, struct efhw_ev_handler *h,
			 efhw_event_t *evp)
{
	unsigned instance = (unsigned)FALCON_EVENT_WAKE_EVQ_ID(evp);

	if (!h->wakeup_fn) {
		EFHW_WARN("%s: no handler registered", __func__);
		return;
	}

	h->wakeup_fn(nic, instance);
}

void
efhw_handle_timeout_event(struct efhw_nic *nic, struct efhw_ev_handler *h,
			  efhw_event_t *evp)
{
	unsigned instance = (unsigned)FALCON_EVENT_WAKE_EVQ_ID(evp);

	if (!h->timeout_fn) {
		EFHW_WARN("%s: no handler registered", __func__);
		return;
	}

	h->timeout_fn(nic, instance);
}

/**********************************************************************
 * Kernel event queue event handling.
 */

int efhw_keventq_poll(struct efhw_nic *nic, struct efhw_keventq *q)
{
	efhw_event_t *ev;
	int l, count = 0;

	EFHW_ASSERT(nic);
	EFHW_ASSERT(q);
	EFHW_ASSERT(q->ev_handlers);

	/* Acquire the lock, or mark the queue as needing re-checking. */
	for (;;) {
		l = q->lock;
		if (l == KEVQ_UNLOCKED) {
			if ((int)cmpxchg(&q->lock, l, KEVQ_LOCKED) == l)
				break;
		} else if (l == KEVQ_LOCKED) {
			if ((int)cmpxchg(&q->lock, l, KEVQ_RECHECK) == l)
				return 0;
		} else {	/* already marked for re-checking */
			EFHW_ASSERT(l == KEVQ_RECHECK);
			return 0;
		}
	}

	if (unlikely(EFHW_EVENT_OVERFLOW(q, q)))
		goto overflow;

	ev = EFHW_EVENT_PTR(q, q, 0);

#ifndef NDEBUG
	if (!EFHW_IS_EVENT(ev))
		EFHW_TRACE("%s: %d NO EVENTS!", __func__, q->instance);
#endif

	for (;;) {
		/* Convention for return codes for handlers is:
		 **   0   - no error, event consumed
		 **   1   - no error, event not consumed
		 **   -ve - error,    event not consumed
		 */
		if (likely(EFHW_IS_EVENT(ev))) {
			count++;

			switch (FALCON_EVENT_CODE(ev)) {

			case FALCON_EVENT_CODE_CHAR:
				falcon_handle_char_event(nic, q->ev_handlers,
							 ev);
				break;

			default:
				EFHW_ERR("efhw_keventq_poll: [%d] UNEXPECTED "
					 "EVENT:"FALCON_EVENT_FMT,
					 q->instance,
					 FALCON_EVENT_PRI_ARG(*ev));
			}

			EFHW_CLEAR_EVENT(ev);
			EFHW_EVENTQ_NEXT(q);

			ev = EFHW_EVENT_PTR(q, q, 0);
		} else {
			/* No events left.  Release the lock (checking if we
			 * need to re-poll to avoid race). */
			l = q->lock;
			if (l == KEVQ_LOCKED) {
				if ((int)cmpxchg(&q->lock, l, KEVQ_UNLOCKED)
				    == l) {
					EFHW_TRACE
					    ("efhw_keventq_poll: %d clean exit",
					     q->instance);
					goto clean_exit;
				}
			}

			/* Potentially more work to do. */
			l = q->lock;
			EFHW_ASSERT(l == KEVQ_RECHECK);
			EFHW_TEST((int)cmpxchg(&q->lock, l, KEVQ_LOCKED) == l);
			EFHW_TRACE("efhw_keventq_poll: %d re-poll required",
				   q->instance);
		}
	}

	/* shouldn't get here */
	EFHW_ASSERT(0);

overflow:
	/* ?? Oh dear.  Should we poll everything that could have possibly
	 ** happened?  Or merely cry out in anguish...
	 */
	EFHW_WARN("efhw_keventq_poll: %d ***** OVERFLOW nic %d *****",
		  q->instance, nic->index);

	q->lock = KEVQ_UNLOCKED;
	return count;

clean_exit:
	/* Ack the processed events so that this event queue can potentially
	   raise interrupts again */
	falcon_nic_evq_ack(nic, q->instance,
			   (EFHW_EVENT_OFFSET(q, q, 0) / sizeof(efhw_event_t)),
			   false);
	return count;
}
