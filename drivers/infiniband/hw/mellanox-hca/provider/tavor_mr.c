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

  $Id: tavor_mr.c,v 1.7 2004/03/10 02:38:23 roland Exp $
*/

#include "tavor_priv.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"
#include "ts_kernel_uintptr.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>

static inline int _tsIbTavorMemoryRegisterCommon(
                                                 tTS_IB_PD            pd,
                                                 tTS_IB_MEMORY_ACCESS access,
                                                 tTS_IB_MR            mr,
                                                 VAPI_mrw_t          *request_attr
                                                 ) {
  VAPI_ret_t           ret;
  VAPI_mrw_t           response_attr;
  tTS_IB_TAVOR_PRIVATE priv = pd->device->private;

  request_attr->pd_hndl = *(VAPI_pd_hndl_t *) &pd->private;
  request_attr->acl     = tsIbTavorAccessTranslate(access);

  ret = VAPI_register_mr(priv->vapi_handle,
                         request_attr,
                         (VAPI_mr_hndl_t *) &mr->private,
                         &response_attr);
  if (ret != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: VAPI_register_mr failed, return code = %d (%s)",
                   pd->device->name, ret, VAPI_strerror(ret));
    return -EINVAL;
  }

  mr->lkey = response_attr.l_key;
  mr->rkey = response_attr.r_key;

  return 0;
}

int tsIbTavorMemoryRegister(
                            tTS_IB_PD            pd,
                            void                *start_address,
                            uint64_t             buffer_size,
                            tTS_IB_MEMORY_ACCESS access,
                            tTS_IB_MR            mr
                            ) {
  VAPI_mrw_t            request_attr;

  request_attr.type  = VAPI_MR;
  request_attr.start = (VAPI_virt_addr_t) (uintptr_t) start_address;
  request_attr.size  = buffer_size;

  return _tsIbTavorMemoryRegisterCommon(pd, access, mr, &request_attr);
}

int tsIbTavorMemoryRegisterPhysical(
                                    tTS_IB_PD              pd,
                                    tTS_IB_PHYSICAL_BUFFER buffer_list,
                                    int                    list_len,
                                    uint64_t              *io_virtual_address,
                                    uint64_t               buffer_size,
                                    uint64_t               iova_offset,
                                    tTS_IB_MEMORY_ACCESS   access,
                                    tTS_IB_MR              mr
                                    ) {
  VAPI_mrw_t request_attr;

  /* We make sure our structure layouts match.  The compiler will
     not generate any code for this unless something is wrong. */
  if (sizeof ((tTS_IB_PHYSICAL_BUFFER) 0)->address     != sizeof ((VAPI_phy_buf_t *) 0)->start ||
      offsetof(tTS_IB_PHYSICAL_BUFFER_STRUCT, address) != offsetof(VAPI_phy_buf_t, start)      ||
      sizeof ((tTS_IB_PHYSICAL_BUFFER) 0)->size        != sizeof ((VAPI_phy_buf_t *) 0)->size  ||
      offsetof(tTS_IB_PHYSICAL_BUFFER_STRUCT, size)    != offsetof(VAPI_phy_buf_t, size)) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "Struct layout of tTS_IB_PHYSICAL_BUFFER_STRUCT doesn't match VAPI_phy_buf_t");
    return -EINVAL;
  }

  request_attr.type          = VAPI_MPR;
  request_attr.start         = *io_virtual_address;
  request_attr.pbuf_list_len = list_len;
  request_attr.pbuf_list_p   = (VAPI_phy_buf_t *) buffer_list;
  request_attr.size          = buffer_size;
  request_attr.iova_offset   = iova_offset;

  return _tsIbTavorMemoryRegisterCommon(pd, access, mr, &request_attr);
}

int tsIbTavorMemoryDeregister(
                              tTS_IB_MR mr
                              ) {
  VAPI_ret_t           ret;
  tTS_IB_TAVOR_PRIVATE priv = mr->device->private;

  ret = VAPI_deregister_mr(priv->vapi_handle, *(VAPI_mr_hndl_t *) &mr->private);

  if (ret != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: VAPI_deregister_mr failed, return code = %d (%s)",
                   mr->device->name, ret, VAPI_strerror(ret));
    return -EINVAL;
  }

  return 0;
}
