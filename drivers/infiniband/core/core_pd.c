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

  $Id: core_pd.c,v 1.3 2004/02/25 00:35:17 roland Exp $
*/

#include "core_priv.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/slab.h>

int tsIbPdCreate(
                 tTS_IB_DEVICE_HANDLE device_handle,
                 void                *device_specific,
                 tTS_IB_PD_HANDLE    *pd_handle
                 ) {
  tTS_IB_DEVICE device = device_handle;
  tTS_IB_PD     pd;
  int           ret;

  TS_IB_CHECK_MAGIC(device, DEVICE);
  if (!device->pd_create) {
    return -ENOSYS;
  }

  pd = kmalloc(sizeof *pd, GFP_KERNEL);
  if (!pd) {
    return -ENOMEM;
  }

  ret = device->pd_create(device, device_specific, pd);

  if (!ret) {
    TS_IB_SET_MAGIC(pd, PD);
    pd->device = device;
    *pd_handle = pd;
  } else {
    kfree(pd);
  }

  return ret;
}

int tsIbPdDestroy(
                  tTS_IB_PD_HANDLE pd_handle
                  ) {
  tTS_IB_PD pd = pd_handle;
  int       ret;

  TS_IB_CHECK_MAGIC(pd, PD);

  if (!pd->device->pd_destroy) {
    return -ENOSYS;
  }

  ret = pd->device->pd_destroy(pd);
  if (!ret) {
    TS_IB_CLEAR_MAGIC(pd);
    kfree(pd);
  }

  return ret;
}
