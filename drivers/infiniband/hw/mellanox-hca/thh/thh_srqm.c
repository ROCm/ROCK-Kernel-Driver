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

#define C_THH_SRQM_C

#include <vapi_common.h>
#include <vip_common.h>
#include <vip_array.h>
#include <thh_hob.h>
#include <cmdif.h>
#include <tmrwm.h>
#include <thh_uldm.h>
#include "thh_srqm.h"

struct THH_srqm_st {
  VIP_array_p_t srq_array;
  THH_hob_t              hob;
  THH_cmd_t              cmdif;
  THH_mrwm_t             mrwm;
  THH_uldm_t             uldm;
  u_int32_t              max_srq;  /* Excluding reserved */
  u_int32_t              rsvd_srq; /* Offset of first user SRQ from SRQC table base */
};

typedef struct THH_srq_st {
  VAPI_lkey_t     lkey;
  /* No need for anything more for SRQ destruction. Still, VIP_array requires an "object" */
} *THH_srq_t;

/************************************************************************/
/*                      Private functions                               */
/************************************************************************/

void free_srq_context(void* srq_context)
{
  MTL_ERROR1(MT_FLFMT("THH_srqm_destroy: Garbage collection: Releasing SRQ #%u"),
             ((THH_srq_t)srq_context)->lkey);
  /* Internal memory regions are cleaned-up on THH_mrwm_destroy */
  FREE(srq_context);
}


/************************************************************************/
/*                      Public functions                                */
/************************************************************************/

HH_ret_t  THH_srqm_create(
  THH_hob_t              hob,           /* IN  */
  u_int8_t               log2_max_srq,  /* IN  */
  u_int8_t               log2_rsvd_srq, /* IN  */
  THH_srqm_t*            srqm_p         /* OUT */
)
{
  HH_ret_t ret;
  VIP_common_ret_t vret;
  u_int32_t rsvd_srq= 1 << log2_rsvd_srq;
  u_int32_t max_srq= (1 << log2_max_srq) - rsvd_srq;
  u_int32_t initial_array_sz= max_srq > 1024 ? 1024 : max_srq;
  MTL_DEBUG1(MT_FLFMT("%s: Invoked with log2_max_srq=0x%u log2_rsrv_srq=0x%u srqm_p=0x%p"), 
             __func__, log2_max_srq, log2_rsvd_srq, srqm_p);
  
  *srqm_p= MALLOC(sizeof(struct THH_srqm_st));
  if (*srqm_p == NULL)  return HH_EAGAIN;

  vret= VIP_array_create_maxsize(initial_array_sz, max_srq, &(*srqm_p)->srq_array);
  if (vret != VIP_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Failed VIP_array_create_maxsize (%u-%s)"), __func__, 
               vret, VAPI_strerror_sym(vret));
    ret= HH_EAGAIN;
    goto vip_array_create_failed;
  }

  ret= THH_hob_get_cmd_if(hob, &(*srqm_p)->cmdif);
  if (ret != HH_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Failed THH_hob_get_cmd_if (%s)"), __func__, HH_strerror_sym(ret));
    goto get_failed;
  }

  ret= THH_hob_get_mrwm(hob, &(*srqm_p)->mrwm);
  if (ret != HH_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Failed THH_hob_get_mrwm (%s)"), __func__, HH_strerror_sym(ret));
    goto get_failed;
  }

  ret= THH_hob_get_uldm(hob, &(*srqm_p)->uldm);
  if (ret != HH_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Failed THH_hob_get_uldm (%s)"), __func__, HH_strerror_sym(ret));
    goto get_failed;
  }

  (*srqm_p)->hob= hob;
  (*srqm_p)->max_srq= max_srq;
  (*srqm_p)->rsvd_srq= rsvd_srq;
  
  return HH_OK;

  get_failed:
    VIP_array_destroy((*srqm_p)->srq_array, NULL);
  vip_array_create_failed:
    FREE(*srqm_p);
    return ret;
}


HH_ret_t  THH_srqm_destroy(
  THH_srqm_t  srqm        /* IN */
)
{
  VIP_common_ret_t vret;
  
  if (srqm == (THH_srqm_t)THH_INVALID_HNDL)  {
    MTL_ERROR1(MT_FLFMT("%s: Invoked for THH_INVALID_HNDL"), __func__);
    return HH_EINVAL;
  }
  MTL_DEBUG1(MT_FLFMT("%s: Releasing SRQM handle 0x%p"), __func__, srqm);

  /* In case of abnormal HCA termination we may still have unreleased SRQ resources */
  vret= VIP_array_destroy(srqm->srq_array, free_srq_context);
  if (vret != VIP_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Failed VIP_array_destroy (%u-%s) - completing SRQM destroy anyway"),
               __func__, vret, VAPI_strerror_sym(vret));
  }

  FREE(srqm);

  return HH_OK;
}


HH_ret_t  THH_srqm_create_srq(
  THH_srqm_t         srqm,                    /* IN */
  HH_pd_hndl_t       pd,                      /* IN */
  THH_srq_ul_resources_t *srq_ul_resources_p, /* IO  */
  HH_srq_hndl_t     *srq_p                    /* OUT */
)
{
  HH_ret_t ret;
  THH_cmd_status_t cmd_ret;
  VIP_common_ret_t vret;
  VIP_array_handle_t vip_hndl;
  THH_srq_t srq;
  THH_internal_mr_t mr_props;
  MOSAL_prot_ctx_t vm_ctx;
  u_int32_t srqn;
  THH_srq_context_t thh_srqc;

  if (srq_ul_resources_p->wqes_buf == 0) {
    MTL_ERROR1(MT_FLFMT("%s: Got wqes_buf=NULL. WQEs in DDR-mem are not supported, yet."), 
               __func__);
    return HH_ENOSYS;
  }

  ret = THH_uldm_get_protection_ctx(srqm->uldm, pd, &vm_ctx);
  if (ret != HH_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Failed THH_uldm_get_protection_ctx (%s)"), __func__, 
               mtl_strerror_sym(ret));
    return ret;
  }

  srq= MALLOC(sizeof(struct THH_srq_st));
  if (srq == NULL) {
    return HH_EAGAIN;
  }

  vret= VIP_array_insert(srqm->srq_array, srq, &vip_hndl);
  if (vret != VIP_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Failed VIP_array_insert (%u)"), __func__, vret);
    ret= HH_EAGAIN;
    goto vip_array_insert_failed;
  }

  memset(&mr_props, 0, sizeof(mr_props));
  mr_props.start= srq_ul_resources_p->wqes_buf;
  mr_props.size= srq_ul_resources_p->wqes_buf_sz;
  mr_props.pd= pd;
  mr_props.vm_ctx= vm_ctx;
  mr_props.force_memkey = FALSE;
  ret= THH_mrwm_register_internal(srqm->mrwm, &mr_props, &srq->lkey);
  if (ret != HH_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Failed THH_mrwm_register_internal (%s)"), __func__, 
               HH_strerror_sym(ret));
    goto register_internal_failed;
  }

  srqn= vip_hndl + srqm->rsvd_srq;

  thh_srqc.pd= pd;
  thh_srqc.l_key= srq->lkey;
  thh_srqc.wqe_addr_h= /* Upper 32b of WQEs buffer */
    (u_int32_t)((sizeof(MT_virt_addr_t) > 4) ? (srq_ul_resources_p->wqes_buf >> 32) : 0);
  thh_srqc.ds= (u_int32_t)(srq_ul_resources_p->wqe_sz >> 4); /* 16B chunks */
  if (thh_srqc.ds > 0x3F)  
    thh_srqc.ds=0x3F; /* Stride may be 1024, but max WQE size is 1008 (ds is 6bit) */
  thh_srqc.uar= srq_ul_resources_p->uar_index;
  cmd_ret= THH_cmd_SW2HW_SRQ(srqm->cmdif, srqn, &thh_srqc);
  if (cmd_ret != THH_CMD_STAT_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Failed THH_cmd_SW2HW_SRQ for srqn=0x%X (%s)"), __func__, 
               srqn, str_THH_cmd_status_t(cmd_ret));
    ret= HH_EFATAL; /* Unexpected error */
    goto sw2hw_failed;
  }

  *srq_p= srqn;
  MTL_DEBUG4(MT_FLFMT("%s: Allocated SRQn=0x%X"), __func__, srqn); 
  return HH_OK;

  sw2hw_failed:
    THH_mrwm_deregister_mr(srqm->mrwm, srq->lkey);
  register_internal_failed:
    VIP_array_erase(srqm->srq_array, vip_hndl, NULL);
  vip_array_insert_failed:
    FREE(srq);
    return ret;
}

HH_ret_t  THH_srqm_destroy_srq(
  THH_srqm_t         srqm,                    /* IN */
  HH_srq_hndl_t      srqn                     /* IN */
)
{
  HH_ret_t ret;
  THH_cmd_status_t cmd_ret;
  VIP_common_ret_t vret;
  VIP_array_obj_t vip_obj;
  THH_srq_t srq;
  MT_bool have_fatal= FALSE;

  vret= VIP_array_erase_prepare(srqm->srq_array, srqn - srqm->rsvd_srq, &vip_obj);
  if (vret != HH_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Failed VIP_array_erase_prepare (%u)"), __func__, vret);
    return HH_EINVAL;
  }
  srq= (THH_srq_t)vip_obj;

  cmd_ret= THH_cmd_HW2SW_SRQ(srqm->cmdif, srqn, NULL);
  if (cmd_ret != THH_CMD_STAT_OK) {
    MTL_ERROR1(MT_FLFMT("%s: Failed THH_cmd_SW2HW_SRQ for srqn=0x%X (%s)"), __func__, 
               srqn, str_THH_cmd_status_t(cmd_ret));
    have_fatal= TRUE; /* Unexpected error */
  } else {
    ret= THH_mrwm_deregister_mr(srqm->mrwm, srq->lkey);
    if (ret != HH_OK) {
      MTL_ERROR2(MT_FLFMT("%s: Failed THH_mrwm_deregister_mr (%s)"), __func__,
                 HH_strerror_sym(ret));
      have_fatal= TRUE;
    }
  }

  if (!have_fatal) {
    vret= VIP_array_erase_done(srqm->srq_array, srqn - srqm->rsvd_srq, NULL);
    if (vret != HH_OK) {
      MTL_ERROR2(MT_FLFMT("%s: Failed VIP_array_erase_done (%u)"), __func__, vret);
      have_fatal= TRUE;
    } else {
      FREE(srq);
    }

  } else {
    VIP_array_erase_undo(srqm->srq_array, srqn - srqm->rsvd_srq); 
    /* Leave for srqm_destroy cleanup */
  }

  return HH_OK; /* resource cleanup is always OK - even if fatal */
}


HH_ret_t  THH_srqm_query_srq(
  THH_srqm_t         srqm,                    /* IN */
  HH_srq_hndl_t      srq,                     /* IN */
  u_int32_t          *limit_p                 /* OUT */
)
{
  VIP_common_ret_t vret;
  
  vret= VIP_array_find_hold(srqm->srq_array, srq - srqm->rsvd_srq, NULL);
  if (vret != HH_OK) {
    return HH_EINVAL_SRQ_HNDL;
  }

  *limit_p= 0; /* Tavor does not support SRQ limit, so the limit event is disarmed */

  VIP_array_find_release(srqm->srq_array, srq - srqm->rsvd_srq);
  return HH_OK;
}

HH_ret_t  THH_srqm_modify_srq(
  THH_srqm_t         srqm,                    /* IN */
  HH_srq_hndl_t      srq,                     /* IN */
  THH_srq_ul_resources_t *srq_ul_resources_p  /* IO */
)
{
  return HH_ENOSYS;
}



