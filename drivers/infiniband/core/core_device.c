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

  $Id: core_device.c,v 1.15 2004/02/25 00:35:16 roland Exp $
*/

#include "core_priv.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/init.h>

static LIST_HEAD(device_list);

static int _tsIbDeviceCheckMandatory(
                                     tTS_IB_DEVICE device
                                     ) {
#define TS_MANDATORY_FUNC(x) { offsetof(tTS_IB_DEVICE_STRUCT, x), #x }
  static const struct {
    size_t offset;
    char  *name;
  } mandatory_table[] = {
    TS_MANDATORY_FUNC(device_query),
    TS_MANDATORY_FUNC(port_query),
    TS_MANDATORY_FUNC(pkey_query),
    TS_MANDATORY_FUNC(gid_query),
    TS_MANDATORY_FUNC(pd_create),
    TS_MANDATORY_FUNC(pd_destroy),
    TS_MANDATORY_FUNC(address_create),
    TS_MANDATORY_FUNC(address_destroy),
    TS_MANDATORY_FUNC(special_qp_create),
    TS_MANDATORY_FUNC(qp_modify),
    TS_MANDATORY_FUNC(qp_destroy),
    TS_MANDATORY_FUNC(send_post),
    TS_MANDATORY_FUNC(receive_post),
    TS_MANDATORY_FUNC(cq_create),
    TS_MANDATORY_FUNC(cq_destroy),
    TS_MANDATORY_FUNC(cq_poll),
    TS_MANDATORY_FUNC(cq_arm),
    TS_MANDATORY_FUNC(mr_register_physical),
    TS_MANDATORY_FUNC(mr_deregister)
  };
  int i;

  for (i = 0; i < sizeof mandatory_table / sizeof mandatory_table[0]; ++i) {
    if (!*(void **) ((void *) device + mandatory_table[i].offset)) {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "Device %s is missing mandatory function %s",
                     device->name, mandatory_table[i].name);
      return -EINVAL;
    }
  }

  return 0;
}

int tsIbDeviceRegister(
                       tTS_IB_DEVICE device
                       ) {
  tTS_IB_DEVICE_PRIVATE           priv;
  tTS_IB_DEVICE_PROPERTIES_STRUCT prop;
  int                             ret;
  int                             p;

  if (_tsIbDeviceCheckMandatory(device)) {
    return -EINVAL;
  }

  priv = kmalloc(sizeof *priv, GFP_KERNEL);
  if (!priv) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "Couldn't allocate private struct for %s",
                   device->name);
    return -ENOMEM;
  }

  *priv = (tTS_IB_DEVICE_PRIVATE_STRUCT) { 0 };

  ret = device->device_query(device, &prop);
  if (ret) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "device_query failed for %s",
                   device->name);
    return ret;
  }

  memcpy(priv->node_guid, prop.node_guid, sizeof (tTS_IB_GUID));

  if (prop.is_switch) {
    priv->start_port = priv->end_port = 0;

    if (!device->switch_ops) {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "Device %s reports itself as a switch but has no switch_ops",
                     device->name);
    }
  } else {
    priv->start_port = 1;
    priv->end_port   = prop.num_port;

    if (device->switch_ops) {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "Device %s reports itself as a CA but has switch_ops",
                     device->name);
    }
  }

  priv->port_data = kmalloc((priv->end_port + 1) * sizeof (tTS_IB_PORT_DATA_STRUCT),
                            GFP_KERNEL);
  if (!priv->port_data) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "Couldn't allocate port info for %s",
                   device->name);
    goto out_free;
  }

  for (p = priv->start_port; p <= priv->end_port; ++p) {
    spin_lock_init(&priv->port_data[p].port_cap_lock);
    memset(priv->port_data[p].port_cap_count, 0, TS_IB_PORT_CAP_NUM * sizeof (int));
  }

  device->core = priv;

  INIT_LIST_HEAD(&priv->async_handler_list);
  spin_lock_init(&priv->async_handler_lock);

  if (tsIbCacheSetup(device)) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "Couldn't create device info cache for %s",
                   device->name);
    goto out_free_port;
  }

  if (tsIbProcSetup(device, prop.is_switch)) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "Couldn't create /proc dir for %s",
                   device->name);
    goto out_free_cache;
  }

  if (tsKernelQueueThreadStart("ts_ib_completion",
                               tsIbCompletionThread,
                               device,
                               &priv->completion_thread)) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "Couldn't start completion thread for %s",
                   device->name);
    goto out_free_proc;
  }

  if (tsKernelQueueThreadStart("ts_ib_async",
                               tsIbAsyncThread,
                               device,
                               &priv->async_thread)) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "Couldn't start async thread for %s",
                   device->name);
    goto out_stop_comp;
  }

  TS_IB_SET_MAGIC(device, DEVICE);

  list_add_tail(&device->core_list, &device_list);

  return 0;

 out_stop_comp:
  tsKernelQueueThreadStop(priv->completion_thread);

 out_free_proc:
  tsIbProcCleanup(device);

 out_free_cache:
  tsIbCacheCleanup(device);

 out_free_port:
  kfree(priv->port_data);

 out_free:
  kfree(priv);
  return -ENOMEM;
}


int tsIbDeviceDeregister(
                         tTS_IB_DEVICE device
                         ) {
  tTS_IB_DEVICE_PRIVATE priv;

  TS_IB_CHECK_MAGIC(device, DEVICE);

  priv = device->core;

  if (tsKernelQueueThreadStop(priv->completion_thread)) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "tsKernelThreadStop failed for %s completion thread",
                   device->name);
  }

  if (tsKernelQueueThreadStop(priv->async_thread)) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "tsKernelThreadStop failed for %s completion thread",
                   device->name);
  }

  tsIbProcCleanup(device);
  tsIbCacheCleanup(device);

  list_del(&device->core_list);

  TS_IB_CLEAR_MAGIC(device);
  kfree(priv->port_data);
  kfree(priv);

  return 0;
}

tTS_IB_DEVICE_HANDLE tsIbDeviceGetByName(
                                         const char *name
                                         ) {
  struct list_head *ptr;
  tTS_IB_DEVICE device;

  list_for_each(ptr, &device_list) {
    device = list_entry(ptr, tTS_IB_DEVICE_STRUCT, core_list);
    if (!strcmp(device->name, name)) {
      return device;
    }
  }

  return TS_IB_HANDLE_INVALID;
}

tTS_IB_DEVICE_HANDLE tsIbDeviceGetByIndex(
                                          int index
                                          ) {
  struct list_head *ptr;
  tTS_IB_DEVICE device;

  if (index < 0) {
    return TS_IB_HANDLE_INVALID;
  }

  list_for_each(ptr, &device_list) {
    device = list_entry(ptr, tTS_IB_DEVICE_STRUCT, core_list);
    if (!index) {
      return device;
    }
    --index;
  }

  return TS_IB_HANDLE_INVALID;
}

int tsIbDevicePropertiesGet(
                            tTS_IB_DEVICE_HANDLE     device_handle,
                            tTS_IB_DEVICE_PROPERTIES properties
                            ) {
  tTS_IB_DEVICE device = device_handle;

  TS_IB_CHECK_MAGIC(device, DEVICE);

  return device->device_query ? device->device_query(device, properties) : -ENOSYS;
}

int tsIbDevicePropertiesSet(
                            tTS_IB_DEVICE_HANDLE         device_handle,
                            tTS_IB_DEVICE_PROPERTIES_SET properties
                            ) {
  tTS_IB_DEVICE device = device_handle;

  TS_IB_CHECK_MAGIC(device, DEVICE);

  return device->device_modify ? device->device_modify(device, properties) : -ENOSYS;
}

int tsIbPortPropertiesGet(
                          tTS_IB_DEVICE_HANDLE     device_handle,
                          tTS_IB_PORT              port,
                          tTS_IB_PORT_PROPERTIES   properties
                          ) {
  tTS_IB_DEVICE device = device_handle;

  TS_IB_CHECK_MAGIC(device, DEVICE);

  return device->port_query ? device->port_query(device, port, properties) : -ENOSYS;
}

int tsIbPortPropertiesSet(
                          tTS_IB_DEVICE_HANDLE       device_handle,
                          tTS_IB_PORT                port,
                          tTS_IB_PORT_PROPERTIES_SET properties
                          ) {
  tTS_IB_DEVICE device = device_handle;
  tTS_IB_DEVICE_PRIVATE priv;
  tTS_IB_PORT_PROPERTIES_SET_STRUCT prop_set;
  unsigned long flags;

  TS_IB_CHECK_MAGIC(device, DEVICE);

  priv = device->core;

  if (port < priv->start_port || port > priv->end_port) {
    return -EINVAL;
  }

  prop_set = *properties;

  spin_lock_irqsave(&priv->port_data[port].port_cap_lock, flags);

  if (properties->valid_fields & TS_IB_PORT_IS_SM) {
    priv->port_data[port].port_cap_count[TS_IB_PORT_CAP_SM] +=
      2 * !!properties->is_sm - 1;
    if (priv->port_data[port].port_cap_count[TS_IB_PORT_CAP_SM] < 0) {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "'is SM' cap count decremented below 0");
      priv->port_data[port].port_cap_count[TS_IB_PORT_CAP_SM] = 0;
    }
    prop_set.is_sm =
      !!priv->port_data[port].port_cap_count[TS_IB_PORT_CAP_SM];
  }

  if (properties->valid_fields & TS_IB_PORT_IS_SNMP_TUNNELING_SUPPORTED) {
    priv->port_data[port].port_cap_count[TS_IB_PORT_CAP_SNMP_TUN] +=
      2 * !!properties->is_snmp_tunneling_supported - 1;
    if (priv->port_data[port].port_cap_count[TS_IB_PORT_CAP_SNMP_TUN] < 0) {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "'is SNMP tunneling supported' cap count decremented below 0");
      priv->port_data[port].port_cap_count[TS_IB_PORT_CAP_SNMP_TUN] = 0;
    }
    prop_set.is_snmp_tunneling_supported =
      !!priv->port_data[port].port_cap_count[TS_IB_PORT_CAP_SNMP_TUN];
  }

  if (properties->valid_fields & TS_IB_PORT_IS_DEVICE_MANAGEMENT_SUPPORTED) {
    priv->port_data[port].port_cap_count[TS_IB_PORT_CAP_DEV_MGMT] +=
      2 * !!properties->is_device_management_supported - 1;
    if (priv->port_data[port].port_cap_count[TS_IB_PORT_CAP_DEV_MGMT] < 0) {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "'is device management supported' cap count decremented below 0");
      priv->port_data[port].port_cap_count[TS_IB_PORT_CAP_DEV_MGMT] = 0;
    }
    prop_set.is_device_management_supported =
      !!priv->port_data[port].port_cap_count[TS_IB_PORT_CAP_DEV_MGMT];
  }

  if (properties->valid_fields & TS_IB_PORT_IS_VENDOR_CLASS_SUPPORTED) {
    priv->port_data[port].port_cap_count[TS_IB_PORT_CAP_VEND_CLASS] +=
      2 * !!properties->is_vendor_class_supported - 1;
    if (priv->port_data[port].port_cap_count[TS_IB_PORT_CAP_VEND_CLASS] < 0) {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "'is vendor class supported' cap count decremented below 0");
      priv->port_data[port].port_cap_count[TS_IB_PORT_CAP_VEND_CLASS] = 0;
    }
    prop_set.is_vendor_class_supported =
      !!priv->port_data[port].port_cap_count[TS_IB_PORT_CAP_VEND_CLASS];
  }

  spin_unlock_irqrestore(&priv->port_data[port].port_cap_lock, flags);

  return device->port_modify ? device->port_modify(device, port, &prop_set) : -ENOSYS;
}

int tsIbPkeyEntryGet(
                     tTS_IB_DEVICE_HANDLE device_handle,
                     tTS_IB_PORT          port,
                     int                  index,
                     tTS_IB_PKEY         *pkey
                     ) {
  tTS_IB_DEVICE device = device_handle;

  TS_IB_CHECK_MAGIC(device, DEVICE);

  return device->pkey_query ? device->pkey_query(device, port, index, pkey) : -ENOSYS;
}

int tsIbGidEntryGet(
                    tTS_IB_DEVICE_HANDLE device_handle,
                    tTS_IB_PORT          port,
                    int                  index,
                    tTS_IB_GID           gid
                    ) {
  tTS_IB_DEVICE device = device_handle;

  TS_IB_CHECK_MAGIC(device, DEVICE);

  return device->gid_query ? device->gid_query(device, port, index, gid) : -ENOSYS;
}

int __init tsIbDeviceInitModule(
                                void
                                ) {
  return 0;
}
