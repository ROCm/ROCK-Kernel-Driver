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

#include "tcqm.h"
#if defined(USE_STD_MEMORY)
# include <memory.h>
#endif
#include <mtl_common.h>
#include <epool.h>
#include <tlog2.h>
#include <cmdif.h>
#include <thh_hob.h>
#include <hh_common.h>
#include <tmrwm.h>

#include <cr_types.h>
#include <MT23108_PRM.h>


#define logIfErr(f) \
  if (rc != HH_OK) { MTL_ERROR1("%s: rc=%s\n", f, HH_strerror_sym(rc)); }

/* macro for translating cmd_rc return codes for non-destroy procs */
#define CMDRC2HH_ND(cmd_rc) ((cmd_rc == THH_CMD_STAT_OK) ? HH_OK : \
                          (cmd_rc == THH_CMD_STAT_EINTR) ? HH_EINTR : HH_EFATAL)

#define CMDRC2HH_BUSY(cmd_rc) ((cmd_rc == THH_CMD_STAT_OK) ? HH_OK :   \
                          (cmd_rc == THH_CMD_STAT_EINTR) ? HH_EINTR :  \
                          ((cmd_rc == THH_CMD_STAT_RESOURCE_BUSY) ||   \
                            (cmd_rc == THH_CMD_STAT_REG_BOUND)) ? HH_EBUSY : HH_EFATAL)
                            
#define TCQM_CQN(cqm,cqc_index) \
          ( ( ((cqm)->entries[cqc_index].cqn_prefix) << (cqm)->log2_max_cq ) | (cqc_index) )

enum
{
  CQE_size      = sizeof(struct tavorprm_completion_queue_entry_st)/8, /* 32 */
  CQE_size_log2 = 5,
  CQE_size_mask = (1ul << CQE_size_log2) - 1
};

typedef struct Completion_Queue_Context  Completion_Queue_Context_t;

typedef struct
{
   unsigned long  prev;
   unsigned long  next;
} _free_offs_t;


/* Completion Queue Context Manager - entry info */
typedef struct CQCM_entry_s
{
  /* THH_cq_props_t  props; / * May be optimzed out, using CmdIf output */
  unsigned long   n_cq;  /* With buf_sz, may just recompute and save needed */
  VAPI_lkey_t     lkey;
  MOSAL_protection_ctx_t  user_protection_context; /*Save protection context to be used on resize*/
#if defined(MT_SUSPEND_QP)
  MT_bool         is_suspended;
#endif
} CQCM_entry_t;


/* Completion Queue Context Manager - entry */
typedef struct
{
  union
  {
    CQCM_entry_t  used;
    _free_offs_t  freelist;
  } u;
  unsigned char cqn_prefix:7; /* CQ number - avoid ghost CQ events (FM issue #15134) */
  unsigned char in_use    :1;
} CQCM_entry_ut;

static const EPool_meta_t  fl_meta =
  {
    sizeof(CQCM_entry_ut),
    (unsigned int)(MT_ulong_ptr_t)(&(((CQCM_entry_ut*)(0))->u.freelist.prev)),
    (unsigned int)(MT_ulong_ptr_t)(&(((CQCM_entry_ut*)(0))->u.freelist.next))
  };


/* The main CQ-manager structure */
typedef struct THH_cqm_st
{
  THH_hob_t       hob;
  u_int8_t        log2_max_cq;
  u_int32_t       max_cq;
  CQCM_entry_ut*  entries;
  EPool_t         flist;

  /* convenient handle saving  */
  THH_cmd_t       cmd_if;
  THH_mrwm_t      mrwm_internal;

  MT_bool cq_resize_fixed; /* FW fix for FM issue #16966/#17002: comp. events during resize */
} TCQM_t;


/************************************************************************/
/************************************************************************/
/*                         private functions                            */
/************************************************************************/

#if MAX_TRACE >= 1
static char*  ulr_print(char* buf, const THH_cq_ul_resources_t* p)
{
  sprintf(buf, "{CQulRes: buf="VIRT_ADDR_FMT", sz="SIZE_T_FMT", uar=%d}",
          p->cqe_buf, p->cqe_buf_sz, p->uar_index);
  return buf;
} /* ulr_print */
#endif


/************************************************************************/
static inline MT_bool  in_use_cq(TCQM_t* cqm, HH_cq_hndl_t  cq)
{
  u_int32_t cqc_index= cq & MASK32(cqm->log2_max_cq);
  MT_bool  in_use = (cqc_index < cqm->max_cq) && cqm->entries[cqc_index].in_use;
  if (!in_use) { MTL_ERROR2(MT_FLFMT("unused cq=0x%x"), cq); }
  return in_use;
} /* in_use_cq */


/************************************************************************/
static HH_ret_t  sw2hw_cq
(
  THH_cmd_t               cmd_if,
  THH_cq_ul_resources_t*  user_prot_ctx_p,
  u_int32_t               cqn,   
  unsigned long           n_cq_entries,
  VAPI_lkey_t             lkey,
  THH_eqn_t               comp_eqn,
  THH_eqn_t               error_eqn
)
{
  THH_cqc_t          cqc;
  THH_cmd_status_t   cmd_rc;

  memset(&cqc, 0, sizeof(THH_cqc_t));
  cqc.st           = 0; /* disarmed */
#ifdef NO_CQ_CI_DBELL
  /* Use this option carefully - CQ overrun may cause unexpected behavior */
  /* It is recommended to set CQ size to be the total of max. outstanding */
  /* WQEs of all attached work queues.                                    */
  cqc.oi            = 1;/*CQ's consumer index update DBells are not used - must ignore CQ overrun*/
#else
  cqc.oi            = 0;/* Enforce CQ overrun detection based on consumer index doorbells updates*/
#endif
  cqc.tr            = 1;
  cqc.status        = 0;
  cqc.start_address = user_prot_ctx_p->cqe_buf;
  cqc.usr_page      = user_prot_ctx_p->uar_index;
  cqc.log_cq_size   = floor_log2(n_cq_entries);
  cqc.e_eqn         = error_eqn;
  cqc.c_eqn         = comp_eqn;
  cqc.pd            = THH_RESERVED_PD;
  cqc.l_key         = lkey;
  cqc.cqn           = cqn;

  cmd_rc = THH_cmd_SW2HW_CQ(cmd_if, cqn, &cqc);
  MTL_DEBUG4(MT_FLFMT("cmd_rc=%d=%s"), cmd_rc, str_THH_cmd_status_t(cmd_rc));
  return (CMDRC2HH_ND(cmd_rc));
} /* sw2hw_cq */


/************************************************************************/
static HH_ret_t  hw2sw_cq
(
  THH_cmd_t        cmd_if,
  u_int32_t        cqn
)
{
  THH_cmd_status_t     cmd_rc;
  cmd_rc = THH_cmd_HW2SW_CQ(cmd_if, cqn, NULL);
  return (CMDRC2HH_ND(cmd_rc));
} /* hw2sw_cq */


/************************************************************************/
/************************************************************************/
/*                         interface functions                            */


/************************************************************************/
HH_ret_t  THH_cqm_create(
  THH_hob_t   hob,          /* IN  */
  u_int8_t    log2_max_cq,  /* IN  */
  u_int8_t    log2_rsvd_cqs,  /* IN  */
  THH_cqm_t*  cqm_p         /* OUT */
)
{
  THH_cmd_t       cmd_if;
  THH_ver_info_t version;
  HH_ret_t        rc = THH_hob_get_cmd_if(hob, &cmd_if);
  TCQM_t*         cqm = 0;
  CQCM_entry_ut*  entries = 0;
  unsigned long   ncq = 1ul << log2_max_cq;
  unsigned long   tavor_num_reserved_cqs = 1ul << log2_rsvd_cqs;
  MTL_TRACE1("{THH_cqm_create: hob=%p, log2_max_cq=%d, rsvd_cqs=%lu\n", 
             hob, log2_max_cq, tavor_num_reserved_cqs);

#ifdef NO_CQ_CI_DBELL
  MTL_ERROR4(MT_FLFMT("WARNING: HCA driver is in CQ-Overrun-Ignore mode !"));
#endif
  
  if (rc == HH_OK) {
    rc= THH_hob_get_ver_info(hob, &version);
  }
  if (rc == HH_OK)
  {
    cqm = TMALLOC(TCQM_t);
    entries = ((log2_max_cq < 24) && (ncq > tavor_num_reserved_cqs)
               ? TNVMALLOC(CQCM_entry_ut, ncq)
               : NULL);
    if (!(cqm && entries))
    {
      rc = HH_EAGAIN;    MTL_ERROR2(MT_FLFMT(""));
    }
    else
    {
      HH_ret_t  hob_rc;
      /* clearing is needed, but for the sake of consistency */
      memset(cqm, 0, sizeof(TCQM_t));
      memset(entries, 0, ncq * sizeof(CQCM_entry_ut));
      hob_rc = THH_hob_get_mrwm(hob, &cqm->mrwm_internal);
      if (hob_rc != HH_OK)
      {
        rc = HH_EAGAIN;  MTL_ERROR2(MT_FLFMT(""));
      }
    }
  }
  if (rc == HH_OK)
  {
    cqm->hob           = hob;
    cqm->cmd_if        = cmd_if;
    cqm->log2_max_cq   = log2_max_cq;
    cqm->max_cq        = ncq;
    cqm->entries       = entries;
    cqm->flist.entries = entries;
    cqm->flist.size    = ncq;
    cqm->flist.meta    = &fl_meta;
    epool_init(&cqm->flist);
    /* reserve is simpler than using an offset */
    epool_reserve(&cqm->flist, 0, tavor_num_reserved_cqs); 

    cqm->cq_resize_fixed= (version.fw_ver_major >= 3);
    rc = HH_OK;
    *cqm_p = cqm;
  }
  else
  {
    if (entries) {VFREE(entries);}
    if (cqm) {FREE(cqm);}
  }
  MTL_TRACE1("}THH_cqm_create: cqm=%p\n", cqm);
  logIfErr("THH_cqm_create");
  return  rc;
} /* THH_cqm_create */


/************************************************************************/
HH_ret_t  THH_cqm_destroy(
  THH_cqm_t  cqm,         /* IN */
  MT_bool    hca_failure  /* IN */
)
{
  HH_ret_t        rc = HH_OK;
  MTL_TRACE1("{THH_cqm_destroy: cqm=%p, hca_failure=%d\n", cqm, hca_failure);
  if (!hca_failure)
  {
    CQCM_entry_ut*  e = cqm->entries;
    CQCM_entry_ut*  e_end = e + cqm->max_cq;
    THH_mrwm_t      mrwm_internal = cqm->mrwm_internal;
    int             any_busy = 0;
    for (;  (e != e_end) && (rc == HH_OK);  ++e)
    {
      if (e->in_use)
      {
        HH_ret_t  mrrc = THH_mrwm_deregister_mr(mrwm_internal, e->u.used.lkey);
        switch (mrrc)
        {
          case HH_OK:
            break;
          case HH_EINVAL:
            rc = HH_EINVAL; MTL_ERROR2(MT_FLFMT(""));
            break;
          case HH_EBUSY:
            any_busy = 1;   MTL_ERROR2(MT_FLFMT(""));  /* Cannot happen! */
            break;
          default:          MTL_ERROR2(MT_FLFMT(""));
        }
      }
    }
    if (any_busy) { rc = HH_EINVAL; } /* again... should not happen */
  }
  VFREE(cqm->entries);
  FREE(cqm);
  MTL_TRACE1("}THH_cqm_destroy\n");
  logIfErr("THH_cqm_destroy");
  return  rc;
} /* THH_cqm_destroy */


/************************************************************************/
HH_ret_t  THH_cqm_create_cq(
  THH_cqm_t               cqm,                     /* IN  */
  MOSAL_protection_ctx_t  user_protection_context, /* IN  */
  THH_eqn_t               comp_eqn,                /* IN  */
  THH_eqn_t               error_eqn,               /* IN  */
  THH_cq_ul_resources_t*  cq_ul_resources_p,       /* IO  */
  HH_cq_hndl_t*           cq_p                     /* OUT */
)
{
  MT_virt_addr_t    cqe_buf = cq_ul_resources_p->cqe_buf;
  MT_virt_addr_t    unalligned_bits = cqe_buf & CQE_size_mask;
  MT_size_t  buf_sz = cq_ul_resources_p->cqe_buf_sz;
  MT_size_t  residue = buf_sz % CQE_size;
  HH_ret_t       rc = ((unalligned_bits == 0) && (residue == 0)
                       ? HH_OK : HH_EINVAL);
  u_int32_t new_cqn= 0xFFFFFFFF; /* Initialize to invalid CQN */
#if MAX_TRACE >= 1
  char  ulr_tbuf[256], *ulr_buf = &ulr_tbuf[0];
#ifndef __DARWIN__
  MTL_TRACE1("{THH_cqm_create_cq: cqm=%p, ctx=0x%x, Ceqn=0x%x, Eeqn=0x%x\n"
             "    %s\n", cqm, user_protection_context, comp_eqn, error_eqn,
             ulr_print(ulr_buf, cq_ul_resources_p));
#else
  MTL_TRACE1("{THH_cqm_create_cq: cqm=%p, Ceqn=0x%x, Eeqn=0x%x\n"
             "    %s\n", cqm, comp_eqn, error_eqn,
             ulr_print(ulr_buf, cq_ul_resources_p));
#endif
#endif
  if (rc == HH_OK)
  {
    VAPI_lkey_t    lkey;
    HH_ret_t       mr_rc = HH_ERR;
    unsigned long  n_cq_entries = (unsigned long)(buf_sz / CQE_size);
    u_int32_t      cqc_index = epool_alloc(&cqm->flist);
    rc = HH_ENOSYS; /* pessimistic */
    if (cqc_index != EPOOL_NULL)
    {
      THH_internal_mr_t  mr_internal;
      memset(&mr_internal, 0, sizeof(mr_internal));
      mr_internal.start        = cq_ul_resources_p->cqe_buf;
      mr_internal.size         = buf_sz;
      mr_internal.pd           = THH_RESERVED_PD;
      mr_internal.vm_ctx       = user_protection_context;
      mr_internal.force_memkey = FALSE;
      mr_internal.memkey       = (VAPI_lkey_t)0;

      mr_rc = THH_mrwm_register_internal(
                cqm->mrwm_internal, &mr_internal, &lkey);
      new_cqn= ( ++(cqm->entries[cqc_index].cqn_prefix) << cqm->log2_max_cq ) | cqc_index;
      rc = ((mr_rc == HH_OK)
            ? sw2hw_cq(cqm->cmd_if, cq_ul_resources_p, new_cqn, n_cq_entries,
                       lkey, comp_eqn, error_eqn)
            : mr_rc);
      MTL_DEBUG4(MT_FLFMT("mr=%d=%s, rc=%d=%s"), 
                 mr_rc, HH_strerror_sym(mr_rc), rc, HH_strerror_sym(rc));
    }

	else {
		MTL_ERROR2(MT_FLFMT("CQ pool is drained.\n"));
		rc = HH_EAGAIN;
	}

    if (rc == HH_OK)
    {
      /* cqm->entries[cq].u.used.props = *cq_props_p; */
      cqm->entries[cqc_index].u.used.n_cq = n_cq_entries;
      cqm->entries[cqc_index].u.used.lkey = lkey;
      /* Save protection context for CQ-resize */
      cqm->entries[cqc_index].u.used.user_protection_context= user_protection_context;
      cqm->entries[cqc_index].in_use = 1;
#if defined(MT_SUSPEND_QP)
      cqm->entries[cqc_index].u.used.is_suspended = FALSE;
#endif
      *cq_p = new_cqn;
    }
    else /* clean */
    {
      MTL_ERROR2(MT_FLFMT("fail, now clean"));
      if (mr_rc == HH_OK)
      {
         (void)THH_mrwm_deregister_mr(cqm->mrwm_internal, lkey);
      }
      if (cqc_index != EPOOL_NULL)
      {
         epool_free(&cqm->flist, cqc_index);
      }
    }
  }
  MTL_TRACE1("}THH_cqm_create_cq, cq=0x%x\n", *cq_p);
  logIfErr("THH_cqm_create_cq");
  return  rc;
} /* THH_cqm_create_cq */

/************************************************************************/
HH_ret_t  THH_cqm_resize_cq(
  THH_cqm_t               cqm,                     /* IN */
  HH_cq_hndl_t            cq,                      /* IN */
  THH_cq_ul_resources_t*  cq_ul_resources_p        /* IO */
)
{
  MT_virt_addr_t    cqe_buf = cq_ul_resources_p->cqe_buf;
  MT_virt_addr_t    unalligned_bits = cqe_buf & CQE_size_mask;
  MT_size_t  buf_sz = cq_ul_resources_p->cqe_buf_sz;
  MT_size_t  residue = buf_sz % CQE_size;
  VAPI_lkey_t    lkey;
  unsigned long  n_cq_entries = (unsigned long)(buf_sz / CQE_size);
  CQCM_entry_ut*  sw_cqc_p;
  THH_internal_mr_t  mr_internal;
  THH_cmd_status_t   cmd_rc;
  HH_ret_t           rc;
  
  /* Validate parameters */
  if (!in_use_cq(cqm, cq)) {
    MTL_ERROR1(MT_FLFMT("Invalid CQ handle (0x%X)"),cq);
    return HH_EINVAL_CQ_HNDL;
  }
  sw_cqc_p = &cqm->entries[cq & MASK32(cqm->log2_max_cq)];

  if ((unalligned_bits != 0) || (residue != 0) || (buf_sz == 0)) {
    MTL_ERROR1(MT_FLFMT("%s: Invalid CQEs buffer (va="VIRT_ADDR_FMT" , size="SIZE_T_FMT")"),
               __func__,cqe_buf,buf_sz);
    return HH_EINVAL;
  }

  /* Register new CQEs buffer */
  memset(&mr_internal, 0, sizeof(mr_internal));
  mr_internal.start        = cq_ul_resources_p->cqe_buf;
  mr_internal.size         = buf_sz;
  mr_internal.pd           = THH_RESERVED_PD;
  mr_internal.vm_ctx       = sw_cqc_p->u.used.user_protection_context;
  mr_internal.force_memkey = FALSE;
  mr_internal.memkey       = (VAPI_lkey_t)0;

  rc= THH_mrwm_register_internal(cqm->mrwm_internal, &mr_internal, &lkey);
  if (rc != HH_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Failed registering new CQEs buffer (%s)"),
               __func__,HH_strerror_sym(rc));
    return rc;
  }
  
  cmd_rc= THH_cmd_RESIZE_CQ(cqm->cmd_if, cq, cqe_buf, lkey, floor_log2(n_cq_entries),
                            cqm->cq_resize_fixed ? NULL : &cq_ul_resources_p->new_producer_index);
  if (cmd_rc != THH_CMD_STAT_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Failed command RESIZE_CQ (%s)"),
               __func__,str_THH_cmd_status_t(cmd_rc));
    switch (cmd_rc) {
      case THH_CMD_STAT_BAD_SIZE:
        rc= HH_E2BIG_CQE_NUM;  /* Retry after polling some CQEs */
        break;
      case THH_CMD_STAT_BAD_RES_STATE:  /* CQ in error state or does not exist anymore */
      case THH_CMD_STAT_BAD_INDEX:      /* Wrong CQ number */
        rc= HH_EINVAL_CQ_HNDL;  
        break;
      case THH_CMD_STAT_BAD_OP:
        rc= HH_ENOSYS;  /* Probably old firmware */
        break;
      case  THH_CMD_STAT_EINTR:
        rc = HH_EINTR;
        break;
      default:
        rc= HH_EFATAL; /* Unexpected error */
        break;
    }
    (void)THH_mrwm_deregister_mr(cqm->mrwm_internal, lkey); /* deregister new buffer */
    return rc;
  }
   
  rc= THH_mrwm_deregister_mr(cqm->mrwm_internal, sw_cqc_p->u.used.lkey);
  if (rc != HH_OK) {
    MTL_ERROR1(MT_FLFMT("%s: Failed deregistration of old CQEs buffer (%s) !!"),
               __func__,HH_strerror_sym(rc));
    /* Nothing we can do about old CQEs region but anyway nobody uses it for any other resource */
  }
  /* Save new parameters of the CQ */
  sw_cqc_p->u.used.n_cq = n_cq_entries;
  sw_cqc_p->u.used.lkey = lkey;
  
  return  HH_OK;
}

/************************************************************************/
HH_ret_t  THH_cqm_destroy_cq(
  THH_cqm_t     cqm /* IN */,
  HH_cq_hndl_t  cq  /* IN */
)
{
  u_int32_t cqc_index= cq & MASK32(cqm->log2_max_cq);
  HH_ret_t  rc = HH_EINVAL_CQ_HNDL;
  MTL_TRACE1("{THH_cqm_destroy_cq, cqm=%p, cq=0x%x\n", cqm, cq);
  if (in_use_cq(cqm, cq))
  {
    rc = hw2sw_cq(cqm->cmd_if, cq);
    if ((rc == HH_OK) || (rc == HH_EFATAL))
    {
      CQCM_entry_ut*  e = &cqm->entries[cqc_index];
      THH_cmd_status_t  mrrc = THH_mrwm_deregister_mr(cqm->mrwm_internal,
                                                    e->u.used.lkey);
      if (mrrc != THH_CMD_STAT_OK)
      {
        MTL_ERROR1(MT_FLFMT("%s: Failed deregistration of CQEs buffer (%d = %s) !!"),
                   __func__,mrrc,str_THH_cmd_status_t(mrrc));
        rc = CMDRC2HH_BUSY(mrrc);
      }
      else
      {
         /* If we are in a fatal error, return OK for destruction */
        if (rc == HH_EFATAL){
            MTL_DEBUG1(MT_FLFMT("%s: in fatal error"), __func__);
            rc = HH_OK;
        }
        e->in_use = 0;
        epool_free(&cqm->flist, cqc_index);
      }
    }
  }
  MTL_TRACE1("}THH_cqm_destroy_cq\n");
  logIfErr("THH_cqm_destroy_cq");
  return  rc;
} /* THH_cqm_destroy_cq */


/************************************************************************/
/* Note: we actually not validating that given 'cq' is indeed in use. */
HH_ret_t  THH_cqm_query_cq(
  THH_cqm_t        cqm,           /* IN  */
  HH_cq_hndl_t     cq,            /* IN  */
  VAPI_cqe_num_t*  num_o_cqes_p   /* IN  */
)
{
  u_int32_t cqc_index= cq & MASK32(cqm->log2_max_cq);
  HH_ret_t  rc = (in_use_cq(cqm, cq) ? HH_OK : HH_EINVAL_CQ_HNDL);
  MTL_TRACE1("{THH_cqm_query_cq: cqm=%p, cq=0x%x\n", cqm, cq);
  if (rc == HH_OK)  rc= (TCQM_CQN(cqm,cqc_index) == cq) ? HH_OK : HH_EINVAL_CQ_HNDL;
  if (rc == HH_OK)
  {
    *num_o_cqes_p = cqm->entries[cqc_index].u.used.n_cq;
  }
  MTL_TRACE1("}THH_cqm_query_cq\n");
  logIfErr("THH_cqm_query_cq");
  return  rc;
} /* THH_cqm_query_cq */



/************************************************************************/
/* Assumed to be the first called in this module, single thread.        */
void  THH_cqm_init(void)
{
  MTL_TRACE1("THH_cqm_init\n");
} /* THH_cqm_init */


/************************************************************************/
HH_ret_t  THH_cqm_get_num_cqs(
  THH_cqm_t  cqm,         /* IN */
  u_int32_t  *num_cqs_p   /* OUT*/
)
{
  CQCM_entry_ut*  e;
  CQCM_entry_ut*  e_end;
  int             num_alloc_cqs = 0;

  if (cqm == NULL) {
      return HH_EINVAL;
  }

  e = cqm->entries;
  e_end = e + cqm->max_cq;
  for (;  (e != e_end) ;  ++e){
      if (e->in_use){ num_alloc_cqs++; }
  }
  return  HH_OK;
} /* THH_cqm_destroy */

#if defined(MT_SUSPEND_QP)
HH_ret_t  THH_cqm_suspend_cq(
  THH_cqm_t        cqm,           /* IN  */
  HH_cq_hndl_t     cq,            /* IN  */ 
  MT_bool          do_suspend     /* IN  */)
{
  u_int32_t cqc_index= cq & MASK32(cqm->log2_max_cq);
  HH_ret_t  rc = HH_EINVAL_CQ_HNDL;
  MTL_TRACE1("{THH_cqm_suspend_cq, cqm=%p, cq=0x%x, do_suspend=%s\n",
             cqm, cq,(do_suspend==FALSE)?"FALSE":"TRUE");
  if (in_use_cq(cqm, cq))
  {
      CQCM_entry_ut*  e = &cqm->entries[cqc_index];
      MT_bool         is_suspended = e->u.used.is_suspended;
      if (do_suspend == is_suspended) {
          /* cq suspension already in desired state */
          MTL_DEBUG2(MT_FLFMT("%s: CQ 0x%x already %s"),
                     __func__,cq, (is_suspended == TRUE)?"suspended" : "unsuspended");
          return HH_OK;
      }
      rc = THH_mrwm_suspend_internal(cqm->mrwm_internal,e->u.used.lkey, do_suspend);
      if (rc != HH_OK)
      {
        MTL_ERROR1(MT_FLFMT("%s: Failed THH_mrwm_suspend_mr of CQEs buffer region(%d: %s) !!"),
                   __func__,rc, HH_strerror_sym(rc));
      }
      else
      {
          MTL_DEBUG2(MT_FLFMT("%s: CQ 0x%x is %s"),
                     __func__,cq, (do_suspend == TRUE)?"suspended" : "unsuspended");
          e->u.used.is_suspended = do_suspend;
      }
  }
  MTL_TRACE1("}THH_cqm_suspend_cq\n");
  logIfErr("THH_cqm_suspend_cq");
  return  rc;
} /* THH_cqm_suspend_cq */
#endif
