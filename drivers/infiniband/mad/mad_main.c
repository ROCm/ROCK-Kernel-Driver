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

  $Id: mad_main.c,v 1.11 2004/02/25 00:56:10 roland Exp $
*/

#include "mad_priv.h"
#include "mad_mem_compat.h"

#include "ts_ib_tavor_provider.h"

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

static int _tsIbMadQpCreate(
                            tTS_IB_DEVICE device,
                            tTS_IB_PORT   port,
                            tTS_IB_QPN    qpn
                            ) {
  tTS_IB_MAD_PRIVATE         priv = device->mad;
  tTS_IB_QP_ATTRIBUTE_STRUCT qp_attr;
  int                        ret;

  TS_TRACE(MOD_KERNEL_IB, T_VERY_VERBOSE, TRACE_KERNEL_IB_GEN,
           "Creating port %d QPN %d for device %s",
           port, qpn, device->name);

  {
    tTS_IB_QP_CREATE_PARAM_STRUCT param = { { 0 } };

    param.limit.max_outstanding_send_request    = TS_IB_MAD_SENDS_PER_QP;
    param.limit.max_outstanding_receive_request = TS_IB_MAD_RECEIVES_PER_QP;
    param.limit.max_send_gather_element         = 1;
    param.limit.max_receive_scatter_element     = 1;

    param.pd             = priv->pd;
    param.send_queue     = priv->cq;
    param.receive_queue  = priv->cq;
    param.send_policy    = TS_IB_WQ_SIGNAL_ALL;
    param.receive_policy = TS_IB_WQ_SIGNAL_ALL;
    param.transport      = TS_IB_TRANSPORT_UD;

    ret = tsIbSpecialQpCreate(&param,
                              port,
                              qpn == 0 ? TS_IB_SMI_QP : TS_IB_GSI_QP,
                              &priv->qp[port][qpn]);
    if (ret) {
      TS_REPORT_FATAL(MOD_KERNEL_IB,
                      "tsIbSpecialQpCreate failed for %s port %d QPN %d (%d)",
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

  ret = tsIbQpModify(priv->qp[port][qpn], &qp_attr);
  if (ret) {
    TS_REPORT_FATAL(MOD_KERNEL_IB,
                    "tsIbQpModify -> INIT failed for %s port %d QPN %d (%d)",
                    device->name, port, qpn, ret);
    return ret;
  }

  qp_attr.state        = TS_IB_QP_STATE_RTR;
  qp_attr.valid_fields = TS_IB_QP_ATTRIBUTE_STATE;
  ret = tsIbQpModify(priv->qp[port][qpn], &qp_attr);
  if (ret) {
    TS_REPORT_FATAL(MOD_KERNEL_IB,
                    "tsIbQpModify -> RTR failed for %s port %d QPN %d (%d)",
                    device->name, port, qpn, ret);
    return ret;
  }

  qp_attr.state    = TS_IB_QP_STATE_RTS;
  qp_attr.send_psn = 0;
  qp_attr.valid_fields =
    TS_IB_QP_ATTRIBUTE_STATE |
    TS_IB_QP_ATTRIBUTE_SEND_PSN;
  ret = tsIbQpModify(priv->qp[port][qpn], &qp_attr);
  if (ret) {
    TS_REPORT_FATAL(MOD_KERNEL_IB,
                    "tsIbQpModify -> RTS failed for %s port %d QPN %d (%d)",
                    device->name, port, qpn, ret);
    return ret;
  }

  return 0;
}

static int _tsIbMadInitOne(
                           tTS_IB_DEVICE_HANDLE device_handle
                           ) {
  tTS_IB_DEVICE                   device = device_handle;
  tTS_IB_MAD_PRIVATE              priv;
  tTS_IB_DEVICE_PROPERTIES_STRUCT prop;
  int                             ret;

  ret = tsIbDevicePropertiesGet(device, &prop);
  if (ret) {
    return ret;
  }

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

  /* First special case: Tavor provider wants to know that our PD will
     be used for special QPs. */
  {
    tTS_IB_TAVOR_PD_PARAM_STRUCT pd_param = { 0 };

    pd_param.special_qp = 1;

    if (!strcmp(prop.provider, "tavor")) {
      ret = tsIbPdCreate(device_handle, &pd_param, &priv->pd);
    } else {
      ret = tsIbPdCreate(device_handle, NULL, &priv->pd);
    }

    if (ret) {
      TS_REPORT_FATAL(MOD_KERNEL_IB,
                      "Failed to allocate PD for %s",
                      prop.name);
      goto error;
    }
  }

  /* Second special case: If we are dealing with an Anafa 2, we
     shouldn't increment the hop pointer when sending DR SMPs. */
#if TS_IB_ANAFA2_HOP_COUNT_WORKAROUND
  priv->is_anafa2 = !strcmp(prop.provider, "anafa2");
#endif

  {
    int entries = (TS_IB_MAD_RECEIVES_PER_QP + TS_IB_MAD_SENDS_PER_QP) * priv->num_port;
    tTS_IB_CQ_CALLBACK_STRUCT callback;

    callback.context        = TS_IB_CQ_CALLBACK_PROCESS;
    callback.policy         = TS_IB_CQ_PROVIDER_REARM;
    callback.function.entry = tsIbMadCompletion;
    callback.arg            = device;

    ret = tsIbCqCreate(device_handle, &entries, &callback, NULL, &priv->cq);
    if (ret) {
      TS_REPORT_FATAL(MOD_KERNEL_IB,
                      "Failed to allocate CQ for %s",
                      prop.name);
      goto error_free_pd;
    }
  }

  if (tsIbMadRegisterMemory(priv->pd, &priv->mr, &priv->lkey)) {
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

    for (p = 0; p <= TS_IB_MAD_MAX_PORTS_PER_DEVICE; ++p) {
      for (q = 0; q <= 1; ++q) {
        priv->qp[p][q]        = TS_IB_HANDLE_INVALID;
        priv->send_free[p][q] = TS_IB_MAD_SENDS_PER_QP;
        priv->send_next[p][q] = 0;

        INIT_LIST_HEAD(&priv->send_list[p][q]);
        for (i = 0; i < TS_IB_MAD_RECEIVES_PER_QP; ++i) {
          priv->receive_buf[p][q][i] = NULL;
        }
      }
    }

    for (p = start_port; p <= end_port; ++p) {
      if (TS_IB_ASSIGN_STATIC_LID) {
        tsIbMadStaticAssign(device, p);
      }

      for (q = 0; q <= 1; ++q) {
        ret = _tsIbMadQpCreate(device, p, q);
        if (ret) {
          goto error_free_qp;
        }

        for (i = 0; i < TS_IB_MAD_RECEIVES_PER_QP; ++i) {
          ret = tsIbMadPostReceive(device, p, q, i);
          if (ret) {
            goto error_free_qp;
          }
        }
      }
    }
  }

  ret = tsKernelQueueThreadStart("ts_ib_mad",
                                 tsIbMadWorkThread,
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

    for (p = 0; p <= TS_IB_MAD_MAX_PORTS_PER_DEVICE; ++p) {
      for (q = 0; q <= 1; ++q) {
        if (priv->qp[p][q] != TS_IB_HANDLE_INVALID) {
          tsIbQpDestroy(priv->qp[p][q]);
          for (i = 0; i < TS_IB_MAD_RECEIVES_PER_QP; ++i) {
            kfree(priv->receive_buf[p][q][i]);
          }
        }
      }
    }
  }

  tsIbMemoryDeregister(priv->mr);

 error_free_cq:
  tsIbCqDestroy(priv->cq);

 error_free_pd:
  tsIbPdDestroy(priv->pd);

 error:
  kfree(priv);
  return ret;
}

static void _tsIbMadCleanupOne(
                               tTS_IB_DEVICE_HANDLE device_handle
                               ) {
  tTS_IB_DEVICE device = device_handle;

  if (device->mad) {
    tTS_IB_MAD_PRIVATE priv = device->mad;

    {
      int p, q, i;

      for (p = 0; p <= TS_IB_MAD_MAX_PORTS_PER_DEVICE; ++p) {
        for (q = 0; q <= 1; ++q) {
          if (priv->qp[p][q] != TS_IB_HANDLE_INVALID) {
            tsIbQpDestroy(priv->qp[p][q]);

            for (i = 0; i < TS_IB_MAD_RECEIVES_PER_QP; ++i) {
              kfree(priv->receive_buf[p][q][i]);
            }
          }
        }
      }
    }
    tsIbMemoryDeregister(priv->mr);
    tsIbCqDestroy(priv->cq);
    tsIbPdDestroy(priv->pd);

    kfree(priv);
    device->mad = NULL;
  }
}

static int __init _tsIbMadInitModule(
                                     void
                                     ) {
  int i = 0;
  int ret;
  tTS_IB_DEVICE_HANDLE device;

  TS_REPORT_INIT(MOD_KERNEL_IB,
                 "Initializing IB MAD layer");

#define TS_WORD_ALIGN(x) (((x) + sizeof (void *) - 1) & ~(sizeof (void *) - 1))
  mad_cache = kmem_cache_create("ib_mad",
                                TS_WORD_ALIGN(sizeof (tTS_IB_MAD_STRUCT)),
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

  while ((device = tsIbDeviceGetByIndex(i)) != TS_IB_HANDLE_INVALID) {
    _tsIbMadInitOne(device);
    ++i;
  }

  ret = tsIbMadProcSetup();
  if (ret) {
    i = 0;

    while ((device = tsIbDeviceGetByIndex(i)) != TS_IB_HANDLE_INVALID) {
      _tsIbMadCleanupOne(device);
      ++i;
    }
  }

  TS_REPORT_INIT(MOD_KERNEL_IB,
                 "IB MAD layer initialized");

  return 0;
}

static void __exit _tsIbMadCleanupModule(
                                          void
                                          ) {
  int i = 0;
  tTS_IB_DEVICE_HANDLE device;

  TS_REPORT_CLEANUP(MOD_KERNEL_IB,
                    "Unloading IB MAD layer");

  tsIbMadProcCleanup();

  while ((device = tsIbDeviceGetByIndex(i)) != TS_IB_HANDLE_INVALID) {
    _tsIbMadCleanupOne(device);
    ++i;
  }

  if (kmem_cache_destroy(mad_cache)) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "Failed to destroy MAD slab cache (memory leak?)");
  }

  TS_REPORT_CLEANUP(MOD_KERNEL_IB,
                    "IB MAD layer unloaded");
}

module_init(_tsIbMadInitModule);
module_exit(_tsIbMadCleanupModule);
