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

  $Id: tavor_ah.c,v 1.4 2004/03/04 02:10:03 roland Exp $
*/

#include "tavor_priv.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>

int tsIbTavorAddressCreate(
                           tTS_IB_PD             pd,
                           tTS_IB_ADDRESS_VECTOR address_vector,
                           tTS_IB_ADDRESS        address
                           ) {
  tTS_IB_TAVOR_PRIVATE priv = pd->device->private;
  VAPI_ud_av_t         av;
  VAPI_ret_t           ret;

  av.sl              = address_vector->service_level;
  av.dlid            = address_vector->dlid;
  av.src_path_bits   = address_vector->source_path_bits;
  av.static_rate     = address_vector->static_rate;
  av.port            = address_vector->port;
  av.grh_flag        = address_vector->use_grh;
  if (address_vector->use_grh) {
    av.traffic_class = address_vector->traffic_class;
    av.flow_label    = address_vector->flow_label;
    av.hop_limit     = address_vector->hop_limit;
    av.sgid_index    = address_vector->source_gid_index;
    memcpy(av.dgid, address_vector->dgid, sizeof av.dgid);
  }

  ret = VAPI_create_addr_hndl(priv->vapi_handle,
                              *(VAPI_pd_hndl_t *) &pd->private,
                              &av,
                              (VAPI_ud_av_hndl_t *) &address->private);
  if (ret != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: VAPI_create_addr_hndl failed, return code = %d (%s)",
                   pd->device->name, ret, VAPI_strerror(ret));
    return -EINVAL;
  }

  return 0;
}

int tsIbTavorAddressDestroy(
                            tTS_IB_ADDRESS address
                            ) {
  tTS_IB_TAVOR_PRIVATE priv = address->device->private;
  VAPI_ret_t           ret;

  ret = VAPI_destroy_addr_hndl(priv->vapi_handle,
                               *(VAPI_ud_av_hndl_t *) &address->private);
  if (ret != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: VAPI_destroy_addr_hndl failed, return code = %d (%s)",
                   address->device->name, ret, VAPI_strerror(ret));
    return -EINVAL;
  }

  return 0;
}
