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

  Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
*/

#define C_VIPKL_POLL_BLK_C
#include <mosal.h>
#include <vapi.h>
#include <vapi_common.h>
#include <vipkl.h>
#include "vipkl_cqblk.h"

typedef struct {
  MOSAL_syncobj_t blkobj;
  VIP_hca_hndl_t vipkl_hca;
  CQM_cq_hndl_t vipkl_cq;
  VIP_RSCT_rscinfo_t rsc_ctx;
} VIPKL_cqblk_ctx_t;

static VIP_array_p_t blk_ctx_array;

static void VIPKL_evapi_comp_eventh_stub(
                                            /*IN*/ VAPI_hca_hndl_t hca_hndl,
                                            /*IN*/ VAPI_cq_hndl_t cq_hndl,
                                            /*IN*/ void* private_data)
{
  VIPKL_cqblk_ctx_t *blk_ctx= (VIPKL_cqblk_ctx_t *)private_data;
  MOSAL_syncobj_signal(&(blk_ctx->blkobj));
}

/* Initialization for this module - return false if failed */
MT_bool VIPKL_cqblk_init(void)
{
  VIP_common_ret_t rc;
  rc= VIP_array_create(1024,&blk_ctx_array);
  if (rc != VIP_OK) return FALSE;
  return TRUE;
}

void VIPKL_cqblk_cleanup(void)
{
  VIP_common_ret_t ret;
  VIP_array_handle_t hdl;
  VIP_array_obj_t obj;

  VIP_ARRAY_FOREACH(blk_ctx_array,ret,hdl,&obj)  FREE(obj);
  VIP_array_destroy(blk_ctx_array,NULL);
}

VIP_ret_t VIPKL_cqblk_alloc_ctx(VIP_RSCT_t usr_ctx,
  VIP_hca_hndl_t hca_hndl,
  /*IN*/CQM_cq_hndl_t    vipkl_cq,
  /*OUT*/VIPKL_cqblk_hndl_t *cqblk_hndl_p
)
{
  VIP_common_ret_t rc;
  call_result_t mt_rc;
  VIPKL_cqblk_ctx_t *new_blk_ctx;
  VIP_array_handle_t vip_array_hndl;
  VIP_RSCT_rschndl_t r_h;

  new_blk_ctx= TMALLOC(VIPKL_cqblk_ctx_t);
  if (new_blk_ctx == NULL) {
    MTL_ERROR2(MT_FLFMT("Failed to allocate new_blk_ctx"));
    return VIP_EAGAIN;
  }
  new_blk_ctx->vipkl_hca= hca_hndl;
  new_blk_ctx->vipkl_cq= vipkl_cq;
  mt_rc= MOSAL_syncobj_init(&(new_blk_ctx->blkobj));
  if (mt_rc != MT_OK) {
    MTL_ERROR2(MT_FLFMT("Failed initializing MOSAL_syncobj_t (%s)"),mtl_strerror_sym(mt_rc));
    return VIP_ERROR;
  }

  rc= VIPKL_bind_evapi_completion_event_handler(hca_hndl,vipkl_cq,
                                                VIPKL_evapi_comp_eventh_stub,new_blk_ctx);
  if (rc != VIP_OK) {
    MTL_ERROR2(MT_FLFMT("Failed binding event handler (%s)"),VAPI_strerror_sym(rc));
    goto bind_failed;
  }

  rc= VIP_array_insert(blk_ctx_array,(VIP_array_obj_t)new_blk_ctx,&vip_array_hndl);
  if (rc != VIP_OK) {
    MTL_ERROR2(MT_FLFMT("Failed allocating VIP_array handle (%s)"),VAPI_strerror_sym(rc));
    goto array_failed;
  }
  
  *cqblk_hndl_p= vip_array_hndl;
  
  r_h.rsc_cqblk_hndl = vip_array_hndl;
  VIP_RSCT_register_rsc(usr_ctx,&new_blk_ctx->rsc_ctx,VIP_RSCT_CQBLK,r_h);

  return VIP_OK;

  array_failed:
    VIPKL_bind_evapi_completion_event_handler(hca_hndl,vipkl_cq,NULL,NULL);  /* unbind */
  bind_failed:
    mt_rc= MOSAL_syncobj_free(&(new_blk_ctx->blkobj));
    if (mt_rc != MT_OK) {
      MTL_ERROR2(MT_FLFMT("Failed MOSAL_syncobj_free (%s)"),mtl_strerror_sym(mt_rc));
    }
    FREE(new_blk_ctx);
    return rc;
}

VIP_ret_t VIPKL_cqblk_free_ctx(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl,/*IN*/VIPKL_cqblk_hndl_t cqblk_hndl)
{
  VIP_array_obj_t vip_array_obj;
  VIPKL_cqblk_ctx_t *blk_ctx;
  VIP_ret_t rc;
  call_result_t mt_rc;

  rc = VIP_array_find_hold(blk_ctx_array,cqblk_hndl,(VIP_array_obj_t *)&vip_array_obj);
  if (rc != VIP_OK ) {
    return VIPKL_CQBLK_INVAL_HNDL;
  }
  blk_ctx= (VIPKL_cqblk_ctx_t*)vip_array_obj;
  rc = VIP_RSCT_check_usr_ctx(usr_ctx,&blk_ctx->rsc_ctx);
  VIP_array_find_release(blk_ctx_array,cqblk_hndl);
  if (rc != VIP_OK) {
    MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. cqblk handle=0x%x (%s)"),__func__,cqblk_hndl,VAPI_strerror_sym(rc));
    return rc;
  } 

  
  rc= VIP_array_erase(blk_ctx_array,cqblk_hndl,&vip_array_obj);
  if (rc != VIP_OK) {
    MTL_ERROR1(MT_FLFMT("Failed erasing cqblk_hndl %d (%s)"),cqblk_hndl,VAPI_strerror_sym(rc));
    return rc;
  }
  blk_ctx= (VIPKL_cqblk_ctx_t*)vip_array_obj;
  
  VIPKL_bind_evapi_completion_event_handler(blk_ctx->vipkl_hca,blk_ctx->vipkl_cq,NULL,NULL);
  VIP_RSCT_deregister_rsc(usr_ctx,&blk_ctx->rsc_ctx,VIP_RSCT_CQBLK);

  mt_rc= MOSAL_syncobj_free(&(blk_ctx->blkobj));
  if (mt_rc != MT_OK) {
    MTL_ERROR2(MT_FLFMT("Failed MOSAL_syncobj_free (%s)"),mtl_strerror_sym(mt_rc));
  }
  FREE(blk_ctx);
  return VIP_OK;
}


VIP_ret_t VIPKL_cqblk_wait(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl,
  /*IN*/VIPKL_cqblk_hndl_t cqblk_hndl,
  /*IN*/MT_size_t timeout_usec)
{
  VIP_array_obj_t vip_array_obj;
  VIPKL_cqblk_ctx_t *blk_ctx;
  VIP_ret_t rc;
  call_result_t mt_rc;

  rc= VIP_array_find_hold(blk_ctx_array,cqblk_hndl,&vip_array_obj);
  if (rc != VIP_OK) {
    MTL_ERROR1(MT_FLFMT("Failed finding cqblk_hndl %d (%s)"),cqblk_hndl,VAPI_strerror_sym(rc));
    return rc;
  }

  blk_ctx= (VIPKL_cqblk_ctx_t*)vip_array_obj;
  rc = VIP_RSCT_check_usr_ctx(usr_ctx,&blk_ctx->rsc_ctx);
  if (rc != VIP_OK) {
      VIP_array_find_release(blk_ctx_array,cqblk_hndl);
      MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. cqblk handle=0x%x (%s)"),__func__,cqblk_hndl,VAPI_strerror_sym(rc));
      return rc;
  } 


  mt_rc= MOSAL_syncobj_waiton(&(blk_ctx->blkobj),(timeout_usec > 0) ? timeout_usec : MOSAL_SYNC_TIMEOUT_INFINITE);
  if (mt_rc == MT_OK) {
    MOSAL_syncobj_clear(&(blk_ctx->blkobj)); /* Rearm object for next event */
  } else {
    MTL_DEBUG4(MT_FLFMT("%s: wait on syncobj terminated (%s)"),__func__,mtl_strerror_sym(mt_rc));
    rc= (mt_rc == MT_ETIMEDOUT) ? VIP_ETIMEDOUT : VIP_EINTR;
  }

  VIP_array_find_release(blk_ctx_array,cqblk_hndl);
  return rc;
}

VIP_ret_t VIPKL_cqblk_signal(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl,/*IN*/VIPKL_cqblk_hndl_t cqblk_hndl)
{
  VIP_array_obj_t vip_array_obj;
  VIPKL_cqblk_ctx_t *blk_ctx;
  VIP_ret_t rc;

  rc= VIP_array_find_hold(blk_ctx_array,cqblk_hndl,&vip_array_obj);
  if (rc != VIP_OK) {
    MTL_ERROR1(MT_FLFMT("Failed finding cqblk_hndl %d (%s)"),cqblk_hndl,VAPI_strerror_sym(rc));
    return rc;
  }

  blk_ctx= (VIPKL_cqblk_ctx_t*)vip_array_obj;
  rc = VIP_RSCT_check_usr_ctx(usr_ctx,&blk_ctx->rsc_ctx);
  if (rc != VIP_OK) {
      VIP_array_find_release(blk_ctx_array,cqblk_hndl);
      MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. cqblk handle=0x%x (%s)"),__func__,cqblk_hndl,VAPI_strerror_sym(rc));
      return rc;
  } 


  MOSAL_syncobj_signal(&(blk_ctx->blkobj)); 

  VIP_array_find_release(blk_ctx_array,cqblk_hndl);
  return VIP_OK;
}

