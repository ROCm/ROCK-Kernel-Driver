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

  $Id: useraccess_ioctl.c 32 2004-04-09 03:57:42Z roland $
*/

#include "useraccess.h"

#include "ts_ib_core.h"
#include "ts_ib_mad.h"

/*
  We include ts_ib_provider_types.h so that we can access the
  mad_process member of a device struct.  This is sort of an ugly
  violation of our layering (since the useraccess module should
  probably only use devices through device handles) but seems like the
  least bad solution.
*/
#include "ts_ib_provider_types.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/ioctl.h>
#include <linux/slab.h>
#include <asm/uaccess.h>

static int _tsIbUserIoctlMadFiltAdd(
                                    tTS_IB_USERACCESS_PRIVATE priv,
                                    unsigned long             arg
                                    ) {
  tTS_IB_USER_MAD_FILTER_STRUCT filter;
  int ret;

  if (copy_from_user(&filter,
                     (tTS_IB_USER_MAD_FILTER) arg,
                     sizeof filter)) {
    return -EFAULT;
  }

  ret = tsIbUserFilterAdd(priv, &filter);
  if (ret) {
    return ret;
  }

  return put_user(filter.handle,
                  &((tTS_IB_USER_MAD_FILTER) arg)->handle);
}

static int _tsIbUserIoctlMadFiltDel(
                                    tTS_IB_USERACCESS_PRIVATE priv,
                                    unsigned long             arg
                                    ) {
  tTS_IB_USER_MAD_FILTER_HANDLE handle;
  int ret;

  ret = get_user(handle, (tTS_IB_USER_MAD_FILTER_HANDLE *) arg);
  if (ret) {
    return ret;
  }

  return tsIbUserFilterDel(priv, handle);
}

static int _tsIbUserIoctlGetPortInfo(
                                     tTS_IB_USERACCESS_PRIVATE priv,
                                     unsigned long             arg
                                     ) {
  tTS_IB_GET_PORT_INFO_IOCTL_STRUCT get_port_info_ioctl;
  int ret;

  if (copy_from_user(&get_port_info_ioctl,
                     (tTS_IB_GET_PORT_INFO_IOCTL) arg,
                     sizeof get_port_info_ioctl)) {
    return -EFAULT;
  }

  ret = tsIbPortPropertiesGet(priv->device->ib_device,
                              get_port_info_ioctl.port,
                              &get_port_info_ioctl.port_info);

  if (ret) {
    return -EFAULT;
  }

  return copy_to_user((tTS_IB_GET_PORT_INFO_IOCTL) arg,
                      &get_port_info_ioctl,
                      sizeof get_port_info_ioctl) ? -EFAULT : 0;
}

static int _tsIbUserIoctlSetPortInfo(
                                     tTS_IB_USERACCESS_PRIVATE priv,
                                     unsigned long             arg
                                     ) {
  tTS_IB_SET_PORT_INFO_IOCTL_STRUCT set_port_info_ioctl;
  int port;

  if (copy_from_user(&set_port_info_ioctl,
                     (tTS_IB_SET_PORT_INFO_IOCTL) arg,
                     sizeof set_port_info_ioctl)) {
    return -EFAULT;
  }

  port = set_port_info_ioctl.port;

  if (port < 0 || port > TS_USERACCESS_MAX_PORTS_PER_DEVICE) {
    return -EINVAL;
  }

  if (set_port_info_ioctl.port_info.valid_fields &
      TS_IB_PORT_IS_SM) {
    if (set_port_info_ioctl.port_info.is_sm) {
      if (priv->port_cap_count[port][TS_IB_PORT_CAP_SM]++) {
        /* already set, don't set it again */
        set_port_info_ioctl.port_info.valid_fields &=
          ~TS_IB_PORT_IS_SM;
      }
    } else {
      if (!priv->port_cap_count[port][TS_IB_PORT_CAP_SM]) {
        /* can't decrement count below 0 */
        return -EINVAL;
      } else if (--priv->port_cap_count[port][TS_IB_PORT_CAP_SM]) {
        /* still set, don't clear it yet */
        set_port_info_ioctl.port_info.valid_fields &=
          ~TS_IB_PORT_IS_SM;
      }
    }
  }

  if (set_port_info_ioctl.port_info.valid_fields &
      TS_IB_PORT_IS_SNMP_TUNNELING_SUPPORTED) {
    if (set_port_info_ioctl.port_info.is_snmp_tunneling_supported) {
      if (priv->port_cap_count[port][TS_IB_PORT_CAP_SNMP_TUN]++) {
        /* already set, don't set it again */
        set_port_info_ioctl.port_info.valid_fields &=
          ~TS_IB_PORT_IS_SNMP_TUNNELING_SUPPORTED;
      }
    } else {
      if (!priv->port_cap_count[port][TS_IB_PORT_CAP_SNMP_TUN]) {
        /* can't decrement count below 0 */
        return -EINVAL;
      } else if (--priv->port_cap_count[port][TS_IB_PORT_CAP_SNMP_TUN]) {
        /* still set, don't clear it yet */
        set_port_info_ioctl.port_info.valid_fields &=
          ~TS_IB_PORT_IS_SNMP_TUNNELING_SUPPORTED;
      }
    }
  }

  if (set_port_info_ioctl.port_info.valid_fields &
      TS_IB_PORT_IS_DEVICE_MANAGEMENT_SUPPORTED) {
    if (set_port_info_ioctl.port_info.is_device_management_supported) {
      if (priv->port_cap_count[port][TS_IB_PORT_CAP_DEV_MGMT]++) {
        /* already set, don't set it again */
        set_port_info_ioctl.port_info.valid_fields &=
          ~TS_IB_PORT_IS_DEVICE_MANAGEMENT_SUPPORTED;
      }
    } else {
      if (!priv->port_cap_count[port][TS_IB_PORT_CAP_DEV_MGMT]) {
        /* can't decrement count below 0 */
        return -EINVAL;
      } else if (--priv->port_cap_count[port][TS_IB_PORT_CAP_DEV_MGMT]) {
        /* still set, don't clear it yet */
        set_port_info_ioctl.port_info.valid_fields &=
          ~TS_IB_PORT_IS_DEVICE_MANAGEMENT_SUPPORTED;
      }
    }
  }

  if (set_port_info_ioctl.port_info.valid_fields &
      TS_IB_PORT_IS_VENDOR_CLASS_SUPPORTED) {
    if (set_port_info_ioctl.port_info.is_vendor_class_supported) {
      if (priv->port_cap_count[port][TS_IB_PORT_CAP_VEND_CLASS]++) {
        /* already set, don't set it again */
        set_port_info_ioctl.port_info.valid_fields &=
          ~TS_IB_PORT_IS_VENDOR_CLASS_SUPPORTED;
      }
    } else {
      if (!priv->port_cap_count[port][TS_IB_PORT_CAP_VEND_CLASS]) {
        /* can't decrement count below 0 */
        return -EINVAL;
      } else if (--priv->port_cap_count[port][TS_IB_PORT_CAP_VEND_CLASS]) {
        /* still set, don't clear it yet */
        set_port_info_ioctl.port_info.valid_fields &=
          ~TS_IB_PORT_IS_VENDOR_CLASS_SUPPORTED;
      }
    }
  }

  return tsIbPortPropertiesSet(priv->device->ib_device,
                               set_port_info_ioctl.port,
                               &set_port_info_ioctl.port_info);
}

static int _tsIbUserIoctlGetReceiveQueueLength(
                                               tTS_IB_USERACCESS_PRIVATE priv,
                                               unsigned long             arg
                                               ) {
  return put_user(priv->max_mad_queue_length, (int *)arg);
}

static int _tsIbUserIoctlSetReceiveQueueLength(
                                               tTS_IB_USERACCESS_PRIVATE priv,
                                               unsigned long             arg
                                               ) {
  int max_rcv_queue_length;
  int ret;

  ret = get_user(max_rcv_queue_length, (int *)arg);

  if (ret) {
    return ret;
  }

  priv->max_mad_queue_length = max_rcv_queue_length;

  return 0;
}

static int _tsIbUserIoctlGetGidEntry(
                                     tTS_IB_USERACCESS_PRIVATE priv,
                                     unsigned long             arg
                                    ) {
  tTS_IB_GID_ENTRY_IOCTL_STRUCT gid_ioctl;
  int ret;

  if (copy_from_user(&gid_ioctl, (void *) arg, sizeof (gid_ioctl))) {
    return -EFAULT;
  }

  ret = tsIbGidEntryGet(priv->device->ib_device, gid_ioctl.port,
                        gid_ioctl.index, gid_ioctl.gid_entry);

  if (ret) {
    return ret;
  }

  if (copy_to_user((void *) arg, &gid_ioctl, sizeof (gid_ioctl))) {
    return -EFAULT;
  }

  return 0;
}

static int _tsIbUserIoctlMadProcess(
                                    tTS_IB_USERACCESS_PRIVATE priv,
                                    unsigned long             arg
                                    ) {
  tTS_IB_MAD mad;
  tTS_IB_MAD_RESULT result;
  int ret;

  /* Here's the ugly layering violation mentioned above: */
  tTS_IB_DEVICE device = priv->device->ib_device;

  if (!device->mad_process) {
    return -ENOSYS;
  }

  mad = kmalloc(2 * sizeof *mad, GFP_KERNEL);
  if (!mad) {
    return -ENOMEM;
  }

  if (copy_from_user(mad, (void *) arg, TS_USER_MAD_SIZE)) {
    ret = -EFAULT;
    goto out;
  }
  mad->device = priv->device->ib_device;

  result = device->mad_process(device, 1, mad, mad + 1);

  if (copy_to_user((void *) arg + TS_USER_MAD_SIZE, &result, sizeof result)) {
    ret = -EFAULT;
    goto out;
  }

  if (!(result & TS_IB_MAD_RESULT_SUCCESS)) {
    ret = -EINVAL;
    goto out;
  }

  if (result & TS_IB_MAD_RESULT_REPLY) {
    if (copy_to_user((void *) arg, mad + 1, TS_USER_MAD_SIZE)) {
      ret = -EFAULT;
      goto out;
    }
  }

  ret = 0;

 out:
  kfree(mad);
  return ret;
}

static const struct {
  int cmd;
  int (*function)(tTS_IB_USERACCESS_PRIVATE, unsigned long);
  char *name;
} ioctl_table[] = {
  {
    .cmd      = TS_IB_IOCSMADFILTADD,
    .function = _tsIbUserIoctlMadFiltAdd,
    .name     = "add MAD filter"
  },
  {
    .cmd      = TS_IB_IOCSMADFILTDEL,
    .function = _tsIbUserIoctlMadFiltDel,
    .name     = "delete MAD filter"
  },
  {
    .cmd      = TS_IB_IOCGPORTINFO,
    .function = _tsIbUserIoctlGetPortInfo,
    .name     = "get port info"
  },
  {
    .cmd      = TS_IB_IOCSPORTINFO,
    .function = _tsIbUserIoctlSetPortInfo,
    .name     = "set port info"
  },
  {
    .cmd      = TS_IB_IOCGRCVQUEUELENGTH,
    .function = _tsIbUserIoctlGetReceiveQueueLength,
    .name     = "get receive queue length"
  },
  {
    .cmd      = TS_IB_IOCSRCVQUEUELENGTH,
    .function = _tsIbUserIoctlSetReceiveQueueLength,
    .name     = "set receive queue length"
  },
  {
    .cmd      = TS_IB_IOCGGIDENTRY,
    .function = _tsIbUserIoctlGetGidEntry,
    .name     = "get GID entry"
  },
  {
    .cmd      = TS_IB_IOCMADPROCESS,
    .function = _tsIbUserIoctlMadProcess,
    .name     = "process MAD"
  }
};

static const int num_ioctl = sizeof ioctl_table / sizeof ioctl_table[0];

/* =============================================================== */
/*..tsIbUserIoctl -                                                */
int tsIbUserIoctl(
                  TS_FILE_OP_PARAMS(inode, filp),
                  unsigned int cmd,
                  unsigned long arg
                  )
{
  tTS_IB_USERACCESS_PRIVATE priv = TS_IB_USER_PRIV_FROM_FILE(filp);
  int i;

  for (i = 0; i < num_ioctl; ++i) {
    if (cmd == ioctl_table[i].cmd) {
      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "%s ioctl", ioctl_table[i].name);
      return ioctl_table[i].function(priv, arg);
    }
  }

  TS_REPORT_WARN(MOD_KERNEL_IB,
                 "Unimplemented ioctl %d",
                 cmd);
  return -ENOIOCTLCMD;
}
