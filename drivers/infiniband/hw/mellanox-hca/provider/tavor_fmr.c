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

  $Id: tavor_fmr.c,v 1.7 2004/03/04 02:10:04 roland Exp $
*/

#include "tavor_priv.h"
#include "ts_ib_provider.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/slab.h>

int tsIbTavorFmrCreate(
                       tTS_IB_PD            pd,
                       tTS_IB_MEMORY_ACCESS access,
                       int                  max_pages,
                       int                  max_remaps,
                       tTS_IB_FMR           fmr
                       ) {
  tTS_IB_TAVOR_PRIVATE priv  = pd->device->private;
  EVAPI_fmr_t          props = { 0 };
  VAPI_ret_t           ret;

  props.pd_hndl              = *(VAPI_pd_hndl_t *) &pd->private;
  props.max_pages            = max_pages;
  props.log2_page_sz         = PAGE_SHIFT;
  props.max_outstanding_maps = max_remaps;
  props.acl                  = tsIbTavorAccessTranslate(access);

  ret = EVAPI_alloc_fmr(priv->vapi_handle, &props, (EVAPI_fmr_hndl_t *) &fmr->private);
  if (ret != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: EVAPI_alloc_fmr failed, return code = %d (%s)",
                   pd->device->name, ret, VAPI_strerror(ret));
    return -EINVAL;
  }

  return 0;
}

int tsIbTavorFmrDestroy(
                        tTS_IB_FMR fmr
                        ) {
  tTS_IB_TAVOR_PRIVATE priv = fmr->device->private;
  VAPI_ret_t           ret;

  ret = EVAPI_free_fmr(priv->vapi_handle, *(EVAPI_fmr_hndl_t *) &fmr->private);
  if (ret != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: EVAPI_free_fmr failed, return code = %d (%s)",
                   fmr->device->name, ret, VAPI_strerror(ret));
    return -EINVAL;
  }

  return 0;
}

int tsIbTavorFmrMap(
                    tTS_IB_FMR   fmr,
                    uint64_t    *page_list,
                    int          list_len,
                    uint64_t    *io_virtual_address,
                    uint64_t     iova_offset,
                    tTS_IB_LKEY *lkey,
                    tTS_IB_RKEY *rkey
                    ) {
  tTS_IB_TAVOR_PRIVATE priv = fmr->device->private;
  EVAPI_fmr_map_t      map;
  VAPI_ret_t           ret;

  map.start          = *io_virtual_address - iova_offset;
  map.size           = list_len * PAGE_SIZE;
  map.page_array_len = list_len;
  map.page_array     = (VAPI_phy_addr_t *) page_list;

  ret = EVAPI_map_fmr(priv->vapi_handle,
                      *(EVAPI_fmr_hndl_t *) &fmr->private,
                      &map,
                      lkey,
                      rkey);
  if (ret != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: EVAPI_map_fmr failed, return code = %d (%s)",
                   fmr->device->name, ret, VAPI_strerror(ret));
    return -EINVAL;
  }

  return 0;
}

int tsIbTavorFmrUnmap(
                      tTS_IB_DEVICE     device,
                      struct list_head *fmr_list
                      ) {
  tTS_IB_TAVOR_PRIVATE priv = device->private;
  int                  count = 0;
  struct list_head    *ptr;
  tTS_IB_FMR           fmr;
  VAPI_ret_t           ret = VAPI_OK;

  if (down_interruptible(&priv->fmr_unmap_mutex)) {
    return -ERESTARTSYS;
  }

  list_for_each(ptr, fmr_list) {
    fmr = list_entry(ptr, tTS_IB_FMR_STRUCT, list);
    if (fmr->device != device) {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "%s: Mismatch with FMR device %s",
                     device->name, fmr->device->name);
    }
    priv->fmr_unmap[count++] = *(EVAPI_fmr_hndl_t *) &fmr->private;
    if (count == TS_IB_TAVOR_FMR_UNMAP_CHUNK_SIZE) {
      ret = EVAPI_unmap_fmr(priv->vapi_handle,
                            count,
                            priv->fmr_unmap);
      if (ret != VAPI_OK) {
        TS_REPORT_WARN(MOD_KERNEL_IB,
                       "%s: EVAPI_unmap_fmr failed, return code = %d (%s)",
                       device->name, ret, VAPI_strerror(ret));
        goto out;
      }
      count = 0;
    }
  }

  if (count) {
    ret = EVAPI_unmap_fmr(priv->vapi_handle,
                          count,
                          priv->fmr_unmap);
    if (ret != VAPI_OK) {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "%s: EVAPI_unmap_fmr failed, return code = %d (%s)",
                     device->name, ret, VAPI_strerror(ret));
    }
  }

 out:
  up(&priv->fmr_unmap_mutex);
  return ret == VAPI_OK ? 0 : -EINVAL;
}
