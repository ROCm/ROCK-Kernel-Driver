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

  $Id: mad_main.c 58 2004-04-16 02:09:40Z roland $
*/

#include "mad_priv.h"
#include "mad_mem_compat.h"

#ifdef CONFIG_INFINIBAND_MELLANOX_HCA
#include "ts_ib_tavor_provider.h"
#endif

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/init.h>
#include <linux/errno.h>

#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

MODULE_AUTHOR("Roland Dreier");
MODULE_DESCRIPTION("kernel IB MAD API");
MODULE_LICENSE("Dual BSD/GPL");

kmem_cache_t *mad_cache;

static int ib_mad_qp_create(struct ib_device *device,
                            tTS_IB_PORT       port,
                            tTS_IB_QPN        qpn)
{
	struct ib_mad_private *priv = device->mad;
	struct ib_qp_attribute qp_attr;
	int                    ret;

	TS_TRACE(MOD_KERNEL_IB, T_VERY_VERBOSE, TRACE_KERNEL_IB_GEN,
		 "Creating port %d QPN %d for device %s",
		 port, qpn, device->name);

	{
		struct ib_qp_create_param param = { { 0 } };

		param.limit.max_outstanding_send_request    = IB_MAD_SENDS_PER_QP;
		param.limit.max_outstanding_receive_request = IB_MAD_RECEIVES_PER_QP;
		param.limit.max_send_gather_element         = 1;
		param.limit.max_receive_scatter_element     = 1;

		param.pd             = priv->pd;
		param.send_queue     = priv->cq;
		param.receive_queue  = priv->cq;
		param.send_policy    = TS_IB_WQ_SIGNAL_ALL;
		param.receive_policy = TS_IB_WQ_SIGNAL_ALL;
		param.transport      = TS_IB_TRANSPORT_UD;

		ret = ib_special_qp_create(&param,
					   port,
					   qpn == 0 ? TS_IB_SMI_QP : TS_IB_GSI_QP,
					   &priv->qp[port][qpn]);
		if (ret) {
			TS_REPORT_FATAL(MOD_KERNEL_IB,
					"ib_special_qp_create failed for %s port %d QPN %d (%d)",
					device->name, port, qpn, ret);
			return ret;
		}
	}

	qp_attr.state = TS_IB_QP_STATE_INIT;
	qp_attr.qkey  = qpn == 0 ? 0 : TS_IB_GSI_WELL_KNOWN_QKEY;
	/* P_Key index is really irrelevant for QP0/QP1, but we have to set
	   some value for RESET->INIT transition. */
	qp_attr.pkey_index = 0;
	qp_attr.valid_fields =
		TS_IB_QP_ATTRIBUTE_STATE |
		TS_IB_QP_ATTRIBUTE_QKEY  |
		TS_IB_QP_ATTRIBUTE_PKEY_INDEX;
	
	/* This is not required, according to the IB spec, but do it until
	   the Tavor driver is fixed: */
	qp_attr.port          = port;
	qp_attr.valid_fields |= TS_IB_QP_ATTRIBUTE_PORT;

	ret = ib_qp_modify(priv->qp[port][qpn], &qp_attr);
	if (ret) {
		TS_REPORT_FATAL(MOD_KERNEL_IB,
				"ib_qp_modify -> INIT failed for %s port %d QPN %d (%d)",
				device->name, port, qpn, ret);
		return ret;
	}

	qp_attr.state        = TS_IB_QP_STATE_RTR;
	qp_attr.valid_fields = TS_IB_QP_ATTRIBUTE_STATE;
	ret = ib_qp_modify(priv->qp[port][qpn], &qp_attr);
	if (ret) {
		TS_REPORT_FATAL(MOD_KERNEL_IB,
				"ib_qp_modify -> RTR failed for %s port %d QPN %d (%d)",
				device->name, port, qpn, ret);
		return ret;
	}

	qp_attr.state    = TS_IB_QP_STATE_RTS;
	qp_attr.send_psn = 0;
	qp_attr.valid_fields =
		TS_IB_QP_ATTRIBUTE_STATE |
		TS_IB_QP_ATTRIBUTE_SEND_PSN;
	ret = ib_qp_modify(priv->qp[port][qpn], &qp_attr);
	if (ret) {
		TS_REPORT_FATAL(MOD_KERNEL_IB,
				"ib_qp_modify -> RTS failed for %s port %d QPN %d (%d)",
				device->name, port, qpn, ret);
		return ret;
	}

	return 0;
}

static int ib_mad_init_one(tTS_IB_DEVICE_HANDLE device_handle)
{
	struct ib_device           *device = device_handle;
	struct ib_mad_private      *priv;
	struct ib_device_properties prop;
	int                         ret;

	ret = ib_device_properties_get(device, &prop);
	if (ret)
		return ret;

	TS_TRACE(MOD_KERNEL_IB, T_VERY_VERBOSE, TRACE_KERNEL_IB_GEN,
		 "Setting up device %s (%s), %d ports",
		 prop.name, prop.provider, prop.num_port);

	priv = kmalloc(sizeof *priv, GFP_KERNEL);
	if (!priv) {
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "Couldn't allocate private structure for %s",
			       prop.name);
		return -ENOMEM;
	}

	device->mad    = priv;
	priv->num_port = (prop.is_switch) ? 1 : prop.num_port;

	/*
	  Handle device-specific special cases:
	*/

#ifdef CONFIG_INFINIBAND_MELLANOX_HCA
	/* First special case: Tavor provider wants to know that our PD will
	   be used for special QPs. */
	if (!strcmp(prop.provider, "tavor")) {
		tTS_IB_TAVOR_PD_PARAM_STRUCT pd_param = { 0 };

		pd_param.special_qp = 1;

		ret = tsIbPdCreate(device_handle, &pd_param, &priv->pd);
	} else
#endif
		ret = tsIbPdCreate(device_handle, NULL, &priv->pd);

	if (ret) {
		TS_REPORT_FATAL(MOD_KERNEL_IB,
				"Failed to allocate PD for %s",
				prop.name);
		goto error;
	}

	/* Second special case: If we are dealing with an Anafa 2, we
	   shouldn't increment the hop pointer when sending DR SMPs. */
#if IB_ANAFA2_HOP_COUNT_WORKAROUND
	priv->is_anafa2 = !strcmp(prop.provider, "anafa2");
#endif

	{
		int entries =
			(IB_MAD_RECEIVES_PER_QP + IB_MAD_SENDS_PER_QP) * priv->num_port;
		struct ib_cq_callback callback;

		callback.context        = TS_IB_CQ_CALLBACK_PROCESS;
		callback.policy         = TS_IB_CQ_PROVIDER_REARM;
		callback.function.entry = ib_mad_completion;
		callback.arg            = device;

		ret = ib_cq_create(device_handle, &entries, &callback, NULL, &priv->cq);
		if (ret) {
			TS_REPORT_FATAL(MOD_KERNEL_IB,
					"Failed to allocate CQ for %s",
					prop.name);
			goto error_free_pd;
		}
	}

	if (ib_mad_register_memory(priv->pd, &priv->mr, &priv->lkey)) {
		TS_REPORT_FATAL(MOD_KERNEL_IB,
				"Failed to allocate MR for %s",
				prop.name);
		goto error_free_cq;
	}

	{
		int start_port, end_port;
		int p, q, i;

		if (prop.is_switch) {
			start_port = end_port = 0;
		} else {
			start_port = 1;
			end_port   = prop.num_port;
		}

		for (p = 0; p <= IB_MAD_MAX_PORTS_PER_DEVICE; ++p) {
			for (q = 0; q <= 1; ++q) {
				priv->qp[p][q]        = TS_IB_HANDLE_INVALID;
				priv->send_free[p][q] = IB_MAD_SENDS_PER_QP;
				priv->send_next[p][q] = 0;

				INIT_LIST_HEAD(&priv->send_list[p][q]);
				for (i = 0; i < IB_MAD_RECEIVES_PER_QP; ++i) {
					priv->receive_buf[p][q][i] = NULL;
				}
			}
		}

		for (p = start_port; p <= end_port; ++p) {
			if (IB_ASSIGN_STATIC_LID)
				ib_mad_static_assign(device, p);

			for (q = 0; q <= 1; ++q) {
				ret = ib_mad_qp_create(device, p, q);
				if (ret)
					goto error_free_qp;

				for (i = 0; i < IB_MAD_RECEIVES_PER_QP; ++i) {
					ret = ib_mad_post_receive(device, p, q, i);
					if (ret)
						goto error_free_qp;
				}
			}
		}
	}

	ret = tsKernelQueueThreadStart("ts_ib_mad",
				       ib_mad_work_thread,
				       device,
				       &priv->work_thread);
	if (ret) {
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "Couldn't start completion thread for %s",
			       prop.name);
		goto error_free_qp;
	}

	return 0;

 error_free_qp:
	{
		int p, q, i;

		for (p = 0; p <= IB_MAD_MAX_PORTS_PER_DEVICE; ++p) {
			for (q = 0; q <= 1; ++q) {
				if (priv->qp[p][q] != TS_IB_HANDLE_INVALID) {
					ib_qp_destroy(priv->qp[p][q]);
					for (i = 0; i < IB_MAD_RECEIVES_PER_QP; ++i) {
						kfree(priv->receive_buf[p][q][i]);
					}
				}
			}
		}
	}

	ib_memory_deregister(priv->mr);

 error_free_cq:
	ib_cq_destroy(priv->cq);

 error_free_pd:
	ib_pd_destroy(priv->pd);

 error:
	kfree(priv);
	return ret;
}

static void ib_mad_remove_one(tTS_IB_DEVICE_HANDLE device_handle)
{
	struct ib_device *device = device_handle;

	if (device->mad) {
		struct ib_mad_private *priv = device->mad;

		tsKernelQueueThreadStop(priv->work_thread);

		{
			int p, q, i;

			for (p = 0; p <= IB_MAD_MAX_PORTS_PER_DEVICE; ++p) {
				for (q = 0; q <= 1; ++q) {
					if (priv->qp[p][q] != TS_IB_HANDLE_INVALID) {
						ib_qp_destroy(priv->qp[p][q]);

						for (i = 0; i < IB_MAD_RECEIVES_PER_QP; ++i) {
							kfree(priv->receive_buf[p][q][i]);
						}
					}
				}
			}
		}
		ib_memory_deregister(priv->mr);
		ib_cq_destroy(priv->cq);
		ib_pd_destroy(priv->pd);

		kfree(priv);
		device->mad = NULL;
	}
}

static void ib_mad_device_notifier(struct ib_device_notifier *self,
				   tTS_IB_DEVICE_HANDLE       device,
				   int                        event)
{
	switch (event) {
	case IB_DEVICE_NOTIFIER_ADD:
		if (ib_mad_init_one(device))
			TS_REPORT_WARN(MOD_KERNEL_IB,
				       "Failed to initialize device.");
		break;

	case IB_DEVICE_NOTIFIER_REMOVE:
		ib_mad_remove_one(device);
		break;

	default:
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "Unknown device notifier event %d.");
		break;
	}
}

static struct ib_device_notifier mad_notifier = {
	.notifier = ib_mad_device_notifier
};

static int __init ib_mad_init(void)
{
	int i = 0;
	int ret;
	tTS_IB_DEVICE_HANDLE device;

	TS_REPORT_INIT(MOD_KERNEL_IB,
		       "Initializing IB MAD layer");

#define WORD_ALIGN(x) (((x) + sizeof (void *) - 1) & ~(sizeof (void *) - 1))
	mad_cache = kmem_cache_create("ib_mad",
				      WORD_ALIGN(sizeof (tTS_IB_MAD_STRUCT)),
				      0,
				      SLAB_HWCACHE_ALIGN,
				      NULL,
				      NULL);
	if (!mad_cache) {
		TS_REPORT_FATAL(MOD_KERNEL_IB,
				"Couldn't create MAD slab cache");
		return -ENOMEM;
	}

#if defined(CONFIG_KMOD) && defined(TS_HOST_DRIVER)
	request_module("infiniband_hca");
#endif

	ib_device_notifier_register(&mad_notifier);

	ret = ib_mad_proc_setup();
	if (ret) {
		i = 0;

		while ((device = ib_device_get_by_index(i)) != TS_IB_HANDLE_INVALID) {
			ib_mad_remove_one(device);
			++i;
		}
	}

	TS_REPORT_INIT(MOD_KERNEL_IB,
		       "IB MAD layer initialized");

	return 0;
}

static void __exit ib_mad_cleanup(void)
{
	int i = 0;
	tTS_IB_DEVICE_HANDLE device;

	TS_REPORT_CLEANUP(MOD_KERNEL_IB,
			  "Unloading IB MAD layer");

	ib_device_notifier_deregister(&mad_notifier);
	ib_mad_proc_cleanup();

	while ((device = ib_device_get_by_index(i)) != TS_IB_HANDLE_INVALID) {
		ib_mad_remove_one(device);
		++i;
	}

	if (kmem_cache_destroy(mad_cache))
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "Failed to destroy MAD slab cache (memory leak?)");

	TS_REPORT_CLEANUP(MOD_KERNEL_IB,
			  "IB MAD layer unloaded");
}

module_init(ib_mad_init);
module_exit(ib_mad_cleanup);

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
