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

  $Id: tavor_main.c,v 1.9 2004/02/25 01:03:34 roland Exp $
*/

#include "tavor_priv.h"
#include "ts_ib_provider.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/slab.h>

MODULE_AUTHOR("Roland Dreier");
MODULE_DESCRIPTION("IB API Tavor provider");
MODULE_LICENSE("Dual BSD/GPL");

static tTS_IB_DEVICE tavor_array[TS_IB_MAX_TAVOR];

static int _tsIbTavorInitOne(
                             VAPI_hca_id_t name,
                             int           i
                             ) {
  VAPI_ret_t           result;
  tTS_IB_DEVICE        device;
  tTS_IB_TAVOR_PRIVATE priv;

  device = kmalloc(sizeof *device, GFP_KERNEL);
  if (!device) {
    TS_REPORT_FATAL(MOD_KERNEL_IB,
                    "%s: Failed to allocate device structure",
                    name);
    return -ENOMEM;
  }

  *device = (tTS_IB_DEVICE_STRUCT) { 0 };

  priv = kmalloc(sizeof *priv, GFP_KERNEL);
  if (!priv) {
    TS_REPORT_FATAL(MOD_KERNEL_IB,
                    "%s: Failed to allocate private structure",
                    name);
    kfree(device);
    return -ENOMEM;
  }

  sema_init(&priv->fmr_unmap_mutex, 1);

  device->owner   = THIS_MODULE;
  device->private = priv;

  result = VAPI_open_hca(name, &priv->vapi_handle);
  switch (result) {
  case VAPI_OK:
  case VAPI_EBUSY:
    break;

  default:
    TS_REPORT_FATAL(MOD_KERNEL_IB,
                    "%s: VAPI_open_hca failed, status %d (%s)",
                    name, result, VAPI_strerror(result));
    goto error;
  }

  result = EVAPI_get_hca_hndl(name, &priv->vapi_handle);
  if (result != VAPI_OK) {
    TS_REPORT_FATAL(MOD_KERNEL_IB,
                    "%s: EVAPI_get_hca_hndl failed, status %d (%s)",
                    name, result, VAPI_strerror(result));
    goto error;
  }

  result = VAPI_set_async_event_handler(priv->vapi_handle,
                                        tsIbTavorAsyncHandler,
                                        device);
  if (result) {
    TS_REPORT_FATAL(MOD_KERNEL_IB,
                    "%s: VAPI_set_async_event_handler failed, status %d (%s)",
                    name, result, VAPI_strerror(result));
    goto error_release;
  }

  strncpy(device->name, name, TS_IB_DEVICE_NAME_MAX);
  device->provider             = "tavor";
  device->device_query         = tsIbTavorDeviceQuery;
  device->port_query           = tsIbTavorPortQuery;
  device->port_modify          = tsIbTavorPortModify;
  device->pkey_query           = tsIbTavorPkeyQuery;
  device->gid_query            = tsIbTavorGidQuery;
  device->pd_create            = tsIbTavorPdCreate;
  device->pd_destroy           = tsIbTavorPdDestroy;
  device->address_create       = tsIbTavorAddressCreate;
  device->address_destroy      = tsIbTavorAddressDestroy;
  device->qp_create            = tsIbTavorQpCreate;
  device->special_qp_create    = tsIbTavorSpecialQpCreate;
  device->qp_modify            = tsIbTavorQpModify;
  device->qp_query             = tsIbTavorQpQuery;
  device->qp_destroy           = tsIbTavorQpDestroy;
  device->send_post            = tsIbTavorSendPost;
  device->receive_post         = tsIbTavorReceivePost;
  device->cq_create            = tsIbTavorCqCreate;
  device->cq_destroy           = tsIbTavorCqDestroy;
  device->cq_resize            = tsIbTavorCqResize;
  device->cq_poll              = tsIbTavorCqPoll;
  device->cq_arm               = tsIbTavorCqArm;
  device->mr_register          = tsIbTavorMemoryRegister;
  device->mr_register_physical = tsIbTavorMemoryRegisterPhysical;
  device->mr_deregister        = tsIbTavorMemoryDeregister;
  device->fmr_create           = tsIbTavorFmrCreate;
  device->fmr_destroy          = tsIbTavorFmrDestroy;
  device->fmr_map              = tsIbTavorFmrMap;
  device->fmr_unmap            = tsIbTavorFmrUnmap;
  device->multicast_attach     = tsIbTavorMulticastAttach;
  device->multicast_detach     = tsIbTavorMulticastDetach;
  device->mad_process          = tsIbTavorMadProcess;

  tsIbTavorSetNodeDesc(priv, i);

  if (tsIbDeviceRegister(device)) {
    goto error_release;
  }

  tavor_array[i] = device;

  return 0;

 error_release:
  VAPI_set_async_event_handler(priv->vapi_handle, NULL, NULL);
  EVAPI_release_hca_hndl(priv->vapi_handle);

 error:
  kfree(device->private);
  kfree(device);

  return -ENODEV;
}

static void _tsIbTavorCleanupOne(
                                 tTS_IB_DEVICE device
                                 ) {
  tTS_IB_TAVOR_PRIVATE priv;

  if (!device) {
    return;
  }

  priv = device->private;

  VAPI_set_async_event_handler(priv->vapi_handle, NULL, NULL);
  EVAPI_release_hca_hndl(priv->vapi_handle);

  tsIbDeviceDeregister(device);

  kfree(device->private);
  kfree(device);
}

static int __init _tsIbTavorInitModule(
                                       void
                                       ) {
  VAPI_hca_id_t *name;
  u_int32_t     num_hcas;
  VAPI_ret_t    result;
  int           i;
  int           ret;

  TS_REPORT_INIT(MOD_KERNEL_IB,
                 "Initializing IB API Tavor provider");

  name = kmalloc(sizeof (VAPI_hca_id_t) * TS_IB_MAX_TAVOR, GFP_KERNEL);
  if (!name) {
    TS_REPORT_FATAL(MOD_KERNEL_IB, "Failed to allocate name array");
    return -ENOMEM;
  }

  result = EVAPI_list_hcas(TS_IB_MAX_TAVOR, &num_hcas, name);

  if (result != VAPI_OK) {
    TS_REPORT_FATAL(MOD_KERNEL_IB, "EVAPI_list_hcas failed, status %d (%s)",
                    result, VAPI_strerror(result));
    kfree(name);
    return -EINVAL;
  }

  for (i = 0; i < num_hcas; ++i) {
    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "Found %s", name[i]);

    ret = _tsIbTavorInitOne(name[i], i);

    if (ret) {
      goto error;
    }
  }

  TS_REPORT_INIT(MOD_KERNEL_IB,
                 "IB API Tavor provider initialized");

  kfree(name);
  return 0;

 error:
  for (; i >= 0; --i) {
    _tsIbTavorCleanupOne(tavor_array[i]);
  }

  kfree(name);

  return ret;
}

static void __exit _tsIbTavorCleanupModule(
                                          void
                                          ) {
  int i;

  TS_REPORT_CLEANUP(MOD_KERNEL_IB,
                    "Unloading IB API Tavor provider");

  for (i = 0; i < TS_IB_MAX_TAVOR; ++i) {
    _tsIbTavorCleanupOne(tavor_array[i]);
  }

  TS_REPORT_CLEANUP(MOD_KERNEL_IB,
                    "IB API Tavor provider unloaded");
}

module_init(_tsIbTavorInitModule);
module_exit(_tsIbTavorCleanupModule);
