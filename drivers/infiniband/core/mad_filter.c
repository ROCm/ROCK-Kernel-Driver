/*
  This software is available to you under a choice of one of two
  licenses.  You may choose to be licensed under the terms of the GNU
  General Public License (GPL) Version 2, available at
  <http://www.fsf.org/copyleft/gpl.html>, or the OpenIB.org BSD
  license, available in the LICENSE.TXT file accompanying this
  software.  These details are also available at
  <http://openib.org/license.html>.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  Copyright (c) 2004 Topspin Communications.  All rights reserved.

  $Id: mad_filter.c 32 2004-04-09 03:57:42Z roland $
*/

#include "mad_priv.h"
#include "ts_ib_mad_types.h"
#include "ts_ib_mad_smi_types.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/slab.h>

static LIST_HEAD(filter_list);
static DECLARE_MUTEX(filter_sem);

static int ib_mad_filter_match(struct ib_mad        *mad,
                               tTS_IB_MAD_DIRECTION  direction,
                               struct ib_mad_filter *filter)
{
	tTS_IB_QPN qpn =
		direction == TS_IB_MAD_DIRECTION_IN ? mad->dqpn : mad->sqpn;

	return
		(!(filter->mask & TS_IB_MAD_FILTER_DEVICE)       ||
		 filter->device       == mad->device)                       &&

		(!(filter->mask & TS_IB_MAD_FILTER_PORT)         ||
		 filter->port         == mad->port)                         &&

		(!(filter->mask & TS_IB_MAD_FILTER_QPN)          ||
		 filter->qpn          == qpn)                               &&

		(!(filter->mask & TS_IB_MAD_FILTER_MGMT_CLASS)   ||
		 filter->mgmt_class   == mad->mgmt_class)                   &&

		(!(filter->mask & TS_IB_MAD_FILTER_R_METHOD)     ||
		 filter->r_method     == mad->r_method)                     &&

		(!(filter->mask & TS_IB_MAD_FILTER_ATTRIBUTE_ID) ||
		 filter->attribute_id == be16_to_cpu(mad->attribute_id))    &&

		(!(filter->mask & TS_IB_MAD_FILTER_DIRECTION)    ||
		 filter->direction    == direction);
}

void ib_mad_invoke_filters(struct ib_mad        *mad,
			   tTS_IB_MAD_DIRECTION  direction)
{
	struct list_head          *ptr;
	struct ib_mad_filter_list *filter;

	if (down_interruptible(&filter_sem)) {
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "signal while getting filter semaphore");
		return;
	}

	list_for_each(ptr, &filter_list) {
		filter = list_entry(ptr, struct ib_mad_filter_list, list);
		if (ib_mad_filter_match(mad, direction, &filter->filter)) {
			tTS_IB_MAD_DISPATCH_FUNCTION function = filter->function;
			void                        *arg      = filter->arg;

			++filter->matches;
			filter->in_callback = 1;

			up(&filter_sem);
			function(mad, arg);
			if (down_interruptible(&filter_sem)) {
				TS_REPORT_WARN(MOD_KERNEL_IB,
					       "signal while getting filter semaphore");
				return;
			}

			filter->in_callback = 0;
		}
	}

	up(&filter_sem);
}

/*
 * ib_mad_validate_dr_smp
 *
 * Do SMI checks on incoming Directed Route SMP to see if we should drop
 * it or pass it to the device provider.  Also, do any Directed Route
 * fixups required by SMI.
 */
int ib_mad_validate_dr_smp(struct ib_mad    *mad,
			   struct ib_device *device)
{
	uint8_t hop_pointer, hop_count;

	hop_pointer = mad->route.directed.hop_pointer;
	hop_count   = mad->route.directed.hop_count;

	/*
	 * Outgoing MAD processing.  "Outgoing" means from initiator to responder.
	 * Section 14.2.2.2, Vol 1 IB spec
	 */
	if (TS_IB_MAD_DR_OUTGOING(mad)) {
		/* C14-9:1 */
		if (hop_pointer == 0 && hop_count != 0) {
			/* We should never see this case, since this would be at the sender. */
			TS_REPORT_WARN(MOD_KERNEL_IB, "Got DR SMP with hop_pointer == 0.");
			return 0;
		}

		/* C14-9:2 */
		if (hop_pointer != 0 && hop_pointer < hop_count) {
			if (!TS_IB_DEVICE_IS_SWITCH(device)) {
				return 0;   // Drop intermediate hop on non-switch.
			} else {
				/* XXX switch */
				TS_REPORT_WARN(MOD_KERNEL_IB, "Need to handle DrMad on switch");
				return 0;
			}
			/* NOTREACHED */
		}

		/* C14-9:3 -- We're at the end of the DR segment of path */
		if (hop_pointer == hop_count) {
			if (hop_count != 0)
				(TS_IB_MAD_SMP_DR_PAYLOAD(mad))->return_path[hop_pointer] = mad->port;
			++mad->route.directed.hop_pointer;

			if (TS_IB_DEVICE_IS_SWITCH(device)) {
				/* XXX switch */
				TS_REPORT_WARN(MOD_KERNEL_IB, "Need to handle DrMad on switch");
				return 0;
			} else {
				/* Must be permissive LID on CA. */
				if (TS_IB_MAD_SMP_DR_PAYLOAD(mad)->dr_dlid != cpu_to_be16(0xffff))
					return 0;
			}

			/* Probably a NOP for us, but spec says to modify LRH:DLID. */
			mad->dlid = TS_IB_MAD_SMP_DR_PAYLOAD(mad)->dr_dlid;

			/* If DLID is permissive, we're at end point. */
			if (mad->dlid == cpu_to_be16(0xffff)) {
				return 1;
			} else {
				/* XXX switch */
				/* Really, this should only happen in switch case. */
				TS_REPORT_WARN(MOD_KERNEL_IB, "Need to handle DrMad on switch");
				return 0;
			}
			/* NOTREACHED */
		}

		/* C14-9:4 -- Hop Pointer = Hop Count + 1 => give to SMA/SM. */
		if (hop_pointer == hop_count + 1)
			return 1;

		/* Check for unreasonable hop pointer.  (C14-9:5) */
		if (hop_pointer > hop_count + 1)
			return 0;

		/* There should be no way of getting here, since one of the if statements
		 * above should have matched, and should have returned a value.
		 */
		TS_REPORT_WARN(MOD_KERNEL_IB, "Unhandled Outgoing DR MAD case.");
		return 0;
	} else {  // Returning MAD (From responder to initiator)

		/* C14-13:1 */
		if (hop_count != 0 && hop_pointer == hop_count + 1) {
			/* We should never see this case, since this would be at the sender. */
			TS_REPORT_WARN(MOD_KERNEL_IB,
				       "Got sender's returning DR SMP at receiver.");
			return 0;
		}

		/* C14-13:2 */
		if (hop_count != 0 && 2 <= hop_pointer && hop_pointer <= hop_count) {
			if (!TS_IB_DEVICE_IS_SWITCH(device)) {
				return 0;   // Drop intermediate hop on non-switch.
			} else {
				/* XXX switch */
				TS_REPORT_WARN(MOD_KERNEL_IB, "Need to handle DrMad on switch");
				return 0;
			}
			/* NOTREACHED */
		}

		/* C14-13:3 -- We're at the end of the DR segment of path */
		if (hop_pointer == 1) {
			--mad->route.directed.hop_pointer;

			if (TS_IB_DEVICE_IS_SWITCH(device)) {
				/* XXX switch */
				TS_REPORT_WARN(MOD_KERNEL_IB, "Need to handle DrMad on switch");
				return 0;
			} else {
				/* Must be permissive LID on CA. */
				if (TS_IB_MAD_SMP_DR_PAYLOAD(mad)->dr_slid != cpu_to_be16(0xffff))
					return 0;
			}

			/* Probably a NOP for us, but spec says to modify LRH:DLID. */
			mad->dlid = TS_IB_MAD_SMP_DR_PAYLOAD(mad)->dr_slid;

			/* If DLID is permissive, we're at end point. */
			if (mad->dlid == cpu_to_be16(0xffff)) {
				return 1;
			} else {
				/* XXX switch */
				/* Really, this should only happen in switch case. */
				TS_REPORT_WARN(MOD_KERNEL_IB, "Need to handle DrMad on switch");
				return 0;
			}
			/* NOTREACHED */
		}

		/* C14-13:4 -- Hop Pointer = 0 => give to SM. */
		if (hop_pointer == 0)
			return 1;

		/* Check for unreasonable hop pointer.  (C14-13:5) */
		if (mad->route.directed.hop_pointer > mad->route.directed.hop_count + 1)
			return 0;
	}
	return 1;
}

void ib_mad_dispatch(struct ib_mad *mad)
{
	struct ib_device         *device = mad->device;
	struct ib_mad            *response;
	tTS_IB_MAD_RESULT         ret;

	response = kmem_cache_alloc(mad_cache, GFP_KERNEL);
	if (!response) {
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "No memory for response packet, dropping MAD");
		return;
	}

	response->device          = mad->device;
	response->port            = mad->port;
	response->sl              = mad->sl;
	response->dlid            = mad->slid;
	response->sqpn            = mad->dqpn;
	response->dqpn            = mad->sqpn;
	response->completion_func = NULL;

	/* If MAD is Directed Route, we need to validate it and fix it up. */
	if ((mad->mgmt_class == TS_IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE) &&
	    !ib_mad_validate_dr_smp(mad, device))
		ret = TS_IB_MAD_RESULT_SUCCESS; // As if device ignored packet.
	else
		ret = device->mad_process(device, 0, mad, response);

	if (!(ret & TS_IB_MAD_RESULT_SUCCESS))
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "mad_process failed (%x) for %s port %d QPN %d (class 0x%02x, aid 0x%04x)",
			       ret,
			       device->name,
			       mad->port,
			       mad->dqpn,
			       mad->mgmt_class,
			       be16_to_cpu(mad->attribute_id));

	/* If the packet was consumed, we don't want to let anyone else look at it.
	 * This is a special case for hardware (tavor) which uses the input queue
	 * to generate traps.
	 */
	if (ret & TS_IB_MAD_RESULT_CONSUMED)
		goto no_response;

	/* Look at incoming MADs to see if they match any filters.
	 * Outgoing MADs are checked in ib_mad_work_thread().
	 */
	ib_mad_invoke_filters(mad, TS_IB_MAD_DIRECTION_IN);

	/* Send a reply if one was generated. */
	if (ret & TS_IB_MAD_RESULT_REPLY) {
		ib_mad_send_no_copy(response);
		return;
	}

 no_response:
	kmem_cache_free(mad_cache, response);
}

int ib_mad_handler_register(struct ib_mad_filter        *filter_struct,
			    tTS_IB_MAD_DISPATCH_FUNCTION function,
			    void                        *arg,
			    tTS_IB_MAD_FILTER_HANDLE    *handle)
{
	struct ib_mad_filter_list *filter;

	filter = kmalloc(sizeof *filter, GFP_KERNEL);
	if (!filter)
		return -ENOMEM;

	filter->filter      = *filter_struct;
	filter->function    = function;
	filter->arg         = arg;
	filter->matches     = 0;
	filter->in_callback = 0;
	TS_IB_SET_MAGIC(filter, FILTER);

	if (down_interruptible(&filter_sem)) {
		kfree(filter);
		return -EINTR;
	}

	list_add_tail(&filter->list, &filter_list);

	up(&filter_sem);

	*handle = filter;
	return 0;
}

int ib_mad_handler_deregister(tTS_IB_MAD_FILTER_HANDLE handle)
{
	struct ib_mad_filter_list *filter = handle;

	TS_IB_CHECK_MAGIC(filter, FILTER);

	if (down_interruptible(&filter_sem))
		return -EINTR;

	while (filter->in_callback) {
		up(&filter_sem);
		set_current_state(TASK_RUNNING);
		schedule();
		if (down_interruptible(&filter_sem))
			return -EINTR;
	}

	list_del(&filter->list);

	up(&filter_sem);

	TS_IB_CLEAR_MAGIC(filter);
	kfree(filter);
	return 0;
}

int ib_mad_filter_get_by_index(int                        index,
			       struct ib_mad_filter_list *filter)
{
	int ret;
	struct list_head *ptr;

	if (down_interruptible(&filter_sem))
		return -EINTR;

	if (list_empty(&filter_list)) {
		ret = -EAGAIN;
		goto out;
	}

	for (ptr = filter_list.next; index > 0; ptr = ptr->next, --index) {
		if (ptr == &filter_list)
			break;
	}

	if (ptr == &filter_list) {
		ret = -EAGAIN;
		goto out;
	}

	if (filter)
		*filter = *list_entry(ptr, struct ib_mad_filter_list, list);

	ret = 0;

 out:
	up(&filter_sem);
	return ret;
}

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
