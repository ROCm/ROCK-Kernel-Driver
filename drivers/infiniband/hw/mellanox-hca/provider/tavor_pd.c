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

  $Id: tavor_pd.c,v 1.4 2004/03/04 02:10:04 roland Exp $
*/

#include "tavor_priv.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>

int tsIbTavorPdCreate(
                      tTS_IB_DEVICE device,
                      void         *device_specific,
                      tTS_IB_PD     pd
                      ) {
  tTS_IB_TAVOR_PRIVATE  priv = device->private;
  tTS_IB_TAVOR_PD_PARAM pd_param = device_specific;
  VAPI_ret_t            ret;

  if (pd_param && pd_param->special_qp) {
    ret = EVAPI_alloc_pd_sqp(priv->vapi_handle,
                             EVAPI_DEFAULT_AVS_PER_PD,
                             (VAPI_pd_hndl_t *) &pd->private);
    if (ret != VAPI_OK) {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "%s: EVAPI_alloc_pd_sqp failed, return code = %d (%s)",
                     device->name, ret, VAPI_strerror(ret));
    }
  } else {
    ret = VAPI_alloc_pd(priv->vapi_handle,
                        (VAPI_pd_hndl_t *) &pd->private);
    if (ret != VAPI_OK) {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "%s: VAPI_alloc_pd failed, return code = %d (%s)",
                     device->name, ret, VAPI_strerror(ret));
    }
  }

  return (ret == VAPI_OK) ? 0 : -EINVAL;
}

int tsIbTavorPdDestroy(
                       tTS_IB_PD pd
                       ) {
  VAPI_ret_t            ret;
  tTS_IB_TAVOR_PRIVATE  priv = pd->device->private;

  ret = VAPI_dealloc_pd(priv->vapi_handle, *(VAPI_pd_hndl_t *) &pd->private);
  if (ret != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: VAPI_dealloc_pd failed, return code = %d (%s)",
                   pd->device->name, ret, VAPI_strerror(ret));
    return -EINVAL;
  }

  return 0;
}
