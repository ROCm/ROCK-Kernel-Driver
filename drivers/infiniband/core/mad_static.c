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

  $Id: mad_static.c 32 2004-04-09 03:57:42Z roland $
*/

#include "mad_priv.h"
#include "ts_ib_provider.h"
#include "smp_access.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>

MODULE_PARM     (lid_base, "i");
MODULE_PARM_DESC(lid_base, "base for static LID assignments "
                 "(if not set, base will be computed from IP address)");

static int lid_base;

void ib_mad_static_compute_base(void)
{
  struct net_device *dev;
  struct in_device *idev;

  read_lock(&dev_base_lock);
  rcu_read_lock();
  for (dev = dev_base; dev; dev = dev->next) {
    if (dev->flags & IFF_LOOPBACK) {
      continue;
    }

    idev = in_dev_get(dev);
    if (!idev) {
      continue;
    }

    read_lock(&idev->mc_list_lock);

    if (!idev->ifa_list) {
      read_unlock(&idev->mc_list_lock);
      in_dev_put(idev);
      continue;
    }

    if (IB_LIMIT_STATIC_LID_TO_1K) {
      /* TS SM only supports 1K LIDs, so limit ourselves to 10 bits */
      /* Use the 7 least significant bits of the IP address to make a
         "unique" static LID base.  We shift over 3 bits and add 1,
         which allows for 7 ports per host (really, 3 2-port HCAs). */
      lid_base = ((be32_to_cpu(idev->ifa_list->ifa_local) & 0x7f) << 3) | 1;
    } else {
      /* Use the 12 least significant bits of the IP address to make a
         "unique" static LID base.  We shift over 3 bits and add 1,
         which allows for 7 ports per host (really, 3 2-port HCAs). */
      lid_base = ((be32_to_cpu(idev->ifa_list->ifa_local) & 0xfff) << 3) | 1;
    }

    {
      uint8_t *i = (uint8_t *) &idev->ifa_list->ifa_local;
      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "Using %s (%d.%d.%d.%d) to get lid_base 0x%04x",
               dev->name, i[0], i[1], i[2], i[3], lid_base);
    }

    read_unlock(&idev->mc_list_lock);
    in_dev_put(idev);
    break;
  }
  rcu_read_unlock();
  read_unlock(&dev_base_lock);

  if (!lid_base) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "Couldn't find a suitable network device; setting lid_base to 1");
    lid_base = 1;
    return;
  }
}

void ib_mad_static_assign(tTS_IB_DEVICE device,
                          tTS_IB_PORT   port)
{
  struct ib_mad *mad_in, *mad_out;

  if (!device->mad_process) {
    return;
  }

  if (!lid_base) {
    ib_mad_static_compute_base();
  }

  mad_in = kmem_cache_alloc(mad_cache, GFP_KERNEL);
  if (!mad_in) {
    TS_REPORT_FATAL(MOD_KERNEL_IB,
                    "Couldn't allocate input buffer");
    return;
  }

  mad_out = kmem_cache_alloc(mad_cache, GFP_KERNEL);
  if (!mad_out) {
    TS_REPORT_FATAL(MOD_KERNEL_IB,
                    "Couldn't allocate output buffer");
    kmem_cache_free(mad_cache, mad_in);
    return;
  }

  memset(mad_in, 0, sizeof *mad_in);
  mad_in->format_version = 1;
  mad_in->mgmt_class     = TS_IB_MGMT_CLASS_SUBN_LID_ROUTED;
  mad_in->class_version  = 1;
  mad_in->r_method       = TS_IB_MGMT_METHOD_GET;
  mad_in->attribute_id   = cpu_to_be16(TS_IB_SMP_ATTRIB_PORT_INFO);
  mad_in->port           = port;
  mad_in->slid           = 0xffff;

  /* Request port info from the device */
  if ((device->mad_process(device, 1, mad_in, mad_out) &
       (TS_IB_MAD_RESULT_SUCCESS | TS_IB_MAD_RESULT_REPLY)) !=
      (TS_IB_MAD_RESULT_SUCCESS | TS_IB_MAD_RESULT_REPLY)) {
    TS_REPORT_FATAL(MOD_KERNEL_IB, "%s: mad_process failed for port %d",
                    device->name, port);
    return;
  }

  /* Edit the lid field in the returned port info. */
  tsIbSmpPortInfoLidSet(TS_IB_MAD_TO_SMP_DATA(mad_out), lid_base);
  ++lid_base;
  mad_out->r_method = TS_IB_MGMT_METHOD_SET;
  mad_out->port     = port;
  mad_out->slid     = 0xffff;

  /* Update the port info on the device */
  if (!(device->mad_process(device, 1, mad_out, mad_in) &
        TS_IB_MAD_RESULT_SUCCESS)) {
    TS_REPORT_FATAL(MOD_KERNEL_IB, "%s: mad_process failed for port %d",
                    device->name, port);
    return;
  }

  kmem_cache_free(mad_cache, mad_in);
  kmem_cache_free(mad_cache, mad_out);

  {
    /* Generate an artificial port error event so that cached info is
       updated for this port */
    struct ib_async_event_record record;

    record.device        = ib_device_to_handle(device);
    record.event         = TS_IB_PORT_ERROR;
    record.modifier.port = port;
    ib_async_event_dispatch(&record);
  }
}

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
