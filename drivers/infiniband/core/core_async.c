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

  $Id: core_async.c 32 2004-04-09 03:57:42Z roland $
*/

#include "core_priv.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/slab.h>

struct ib_async_event_handler {
	TS_IB_DECLARE_MAGIC
	struct ib_async_event_record        record;
	tTS_IB_ASYNC_EVENT_HANDLER_FUNCTION function;
	void                               *arg;
	struct list_head                    list;
	spinlock_t                         *list_lock;
};

struct ib_async_event_list {
	struct ib_async_event_record record;
	struct list_head             list;
};

/* Table of modifiers for async events */
static struct {
	enum {
		QP,
		EEC,
		CQ,
		PORT,
		NONE
	}     mod;
	char *desc;
} event_table[] = {
	[TS_IB_QP_PATH_MIGRATED]                = { QP,   "QP Path Migrated"                },
	[TS_IB_EEC_PATH_MIGRATED]               = { EEC,  "EEC Path Migrated"               },
	[TS_IB_QP_COMMUNICATION_ESTABLISHED]    = { QP,   "QP Communication Established"    },
	[TS_IB_EEC_COMMUNICATION_ESTABLISHED]   = { EEC,  "EEC Communication Established"   },
	[TS_IB_SEND_QUEUE_DRAINED]              = { QP,   "Send Queue Drained"              },
	[TS_IB_CQ_ERROR]                        = { CQ,   "CQ Error"                        },
	[TS_IB_LOCAL_WQ_INVALID_REQUEST_ERROR]  = { QP,   "Local WQ Invalid Request Error"  },
	[TS_IB_LOCAL_WQ_ACCESS_VIOLATION_ERROR] = { QP,   "Local WQ Access Violation Error" },
	[TS_IB_LOCAL_WQ_CATASTROPHIC_ERROR]     = { QP,   "Local WQ Catastrophic Error"     },
	[TS_IB_PATH_MIGRATION_ERROR]            = { QP,   "Path Migration Error"            },
	[TS_IB_LOCAL_EEC_CATASTROPHIC_ERROR]    = { EEC,  "Local EEC Catastrophic Error"    },
	[TS_IB_LOCAL_CATASTROPHIC_ERROR]        = { NONE, "Local Catastrophic Error"        },
	[TS_IB_PORT_ERROR]                      = { PORT, "Port Error"                      },
	[TS_IB_PORT_ACTIVE]                     = { PORT, "Port Active"                     },
	[TS_IB_LID_CHANGE]                      = { PORT, "LID Change"                      },
	[TS_IB_PKEY_CHANGE]                     = { PORT, "P_Key Change"                    }
};

int ib_async_event_handler_register(struct ib_async_event_record       *record,
				    tTS_IB_ASYNC_EVENT_HANDLER_FUNCTION function,
				    void                               *arg,
				    tTS_IB_ASYNC_EVENT_HANDLER_HANDLE  *handle)
{
	struct ib_async_event_handler *handler;
	int ret;
	unsigned long flags;

	TS_IB_CHECK_MAGIC(record->device, DEVICE);

	if (record->event < 0 || record->event >= ARRAY_SIZE(event_table)) {
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "Attempt to register handler for invalid async event %d",
			       record->event);
		return -EINVAL;
	}

	handler = kmalloc(sizeof *handler, GFP_KERNEL);
	if (!handler) {
		return -ENOMEM;
	}

	handler->record   = *record;
	handler->function = function;
	handler->arg      = arg;

	switch (event_table[record->event].mod) {
	case QP:
	{
		struct ib_qp *qp = record->modifier.qp;

		if (!TS_IB_TEST_MAGIC(qp, QP)) {
			TS_REPORT_WARN(MOD_KERNEL_IB, "Bad magic 0x%lx at %p for QP",
				       TS_IB_GET_MAGIC(qp), qp);
			ret = -EINVAL;
			goto error;
		}

		if (qp->device != record->device) {
			ret = -EINVAL;
			goto error;
		}

		spin_lock_irqsave(&qp->async_handler_lock, flags);
		handler->list_lock = &qp->async_handler_lock;
		list_add_tail(&handler->list, &qp->async_handler_list);
		spin_unlock_irqrestore(&qp->async_handler_lock, flags);
	}
	break;

	case CQ:
	{
		struct ib_cq *cq = record->modifier.cq;

		if (!TS_IB_TEST_MAGIC(cq, CQ)) {
			TS_REPORT_WARN(MOD_KERNEL_IB, "Bad magic 0x%lx at %p for CQ",
				       TS_IB_GET_MAGIC(cq), cq);
			ret = -EINVAL;
			goto error;
		}

		if (cq->device != record->device) {
			ret = -EINVAL;
			goto error;
		}

		spin_lock_irqsave(&cq->async_handler_lock, flags);
		handler->list_lock = &cq->async_handler_lock;
		list_add_tail(&handler->list, &cq->async_handler_list);
		spin_unlock_irqrestore(&cq->async_handler_lock, flags);
	}
	break;

	case EEC:
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "Async events for EECs not supported yet");
		ret = -EINVAL;
		goto error;

	case PORT:
	case NONE:
	{
		struct ib_device_private *priv = ((struct ib_device *) record->device)->core;

		spin_lock_irqsave(&priv->async_handler_lock, flags);
		handler->list_lock = &priv->async_handler_lock;
		list_add_tail(&handler->list, &priv->async_handler_list);
		spin_unlock_irqrestore(&priv->async_handler_lock, flags);
	}
	break;
	}

	TS_IB_SET_MAGIC(handler, ASYNC);
	*handle = handler;
	return 0;

 error:
	kfree(handler);
	return ret;
}

int ib_async_event_handler_deregister(tTS_IB_ASYNC_EVENT_HANDLER_HANDLE handle)
{
	struct ib_async_event_handler *handler = handle;
	unsigned long flags;

	TS_IB_CHECK_MAGIC(handle, ASYNC);

	spin_lock_irqsave(handler->list_lock, flags);
	list_del(&handler->list);
	spin_unlock_irqrestore(handler->list_lock, flags);

	TS_IB_CLEAR_MAGIC(handle);
	kfree(handle);
	return 0;
}

void ib_async_event_dispatch(struct ib_async_event_record *event_record)
{
	struct ib_async_event_list *event;
	struct ib_device_private   *priv = ((struct ib_device *) event_record->device)->core;

	event = kmalloc(sizeof *event, GFP_ATOMIC);
	if (!event) {
		return;
	}

	event->record = *event_record;

	tsKernelQueueThreadAdd(priv->async_thread, &event->list);
}

void ib_async_thread(struct list_head *entry,
                     void *device_ptr)
{
	struct ib_async_event_list         *event;
	struct ib_device_private           *priv;
	char                                mod_buf[32];
	struct list_head                   *handler_list = NULL;
	spinlock_t                         *handler_lock = NULL;
	struct list_head                   *pos;
	struct list_head                   *n;
	struct ib_async_event_handler      *handler;
	tTS_IB_ASYNC_EVENT_HANDLER_FUNCTION function;
	void                               *arg;

	event = list_entry(entry, struct ib_async_event_list, list);
	priv  = ((struct ib_device *) event->record.device)->core;

	switch (event_table[event->record.event].mod) {
	case QP:
		sprintf(mod_buf, " (QP %p)", event->record.modifier.qp);
		handler_list = &((struct ib_qp *) event->record.modifier.qp)->async_handler_list;
		handler_lock = &((struct ib_qp *) event->record.modifier.qp)->async_handler_lock;
		break;

	case CQ:
		sprintf(mod_buf, " (CQ %p)", event->record.modifier.cq);
		handler_list = &((struct ib_cq *) event->record.modifier.cq)->async_handler_list;
		handler_lock = &((struct ib_cq *) event->record.modifier.cq)->async_handler_lock;
		break;

	case EEC:
		sprintf(mod_buf, " (EEC %p)", event->record.modifier.eec);
		break;

	case PORT:
		sprintf(mod_buf, " (port %d)", event->record.modifier.port);
		handler_list = &priv->async_handler_list;
		handler_lock = &priv->async_handler_lock;

		/* Update cached port info */
		ib_cache_update(event->record.device, event->record.modifier.port);
		break;

	case NONE:
		mod_buf[0] = '\0';
		handler_list = &priv->async_handler_list;
		handler_lock = &priv->async_handler_lock;
		break;
	}

	TS_TRACE(MOD_KERNEL_IB, T_VERY_VERBOSE, TRACE_KERNEL_IB_GEN,
		 "Received %s event for %s%s",
		 event_table[event->record.event].desc,
		 ((struct ib_device *) event->record.device)->name,
		 mod_buf);

	if (!handler_list) {
		return;
	}

	spin_lock_irq(handler_lock);
	list_for_each_safe(pos, n, handler_list) {
		handler = list_entry(pos, struct ib_async_event_handler, list);
		if (handler->record.event == event->record.event) {
			function = handler->function;
			arg      = handler->arg;

			spin_unlock_irq(handler_lock);
			function(&event->record, arg);
			spin_lock_irq(handler_lock);
		}
	}
	spin_unlock_irq(handler_lock);

	kfree(event);
}

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
