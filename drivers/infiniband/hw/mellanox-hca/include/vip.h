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

#ifndef H_VIP_H
#define H_VIP_H

#include <mtl_common.h>
#include <vapi.h>
#include <vip_common.h>
#include <hh.h>


#define VIP_VMALLOC_THRESHOLD (2*MOSAL_SYS_PAGE_SIZE)

#ifdef __LINUX__
#define VIP_SMART_MALLOC(size) ({                                          \
                                  void *p;                                 \
                                  if ( (size) > VIP_VMALLOC_THRESHOLD ) {  \
                                    p = VMALLOC(size);                  \
                                  }                                     \
                                  else {                                \
                                    p = MALLOC(size);                   \
                                  }                                     \
                                  p;                                    \
                               })




#define VIP_SMART_FREE(ptr,size) do {                                      \
                                   if ( (size) > VIP_VMALLOC_THRESHOLD )   {  \
                                     VFREE(ptr);                           \
                                   }                                       \
                                   else {                                  \
                                     FREE(ptr);                            \
                                   }                                       \
                                 }                                         \
                                 while(0)
#else
#define VIP_SMART_MALLOC(size)  VMALLOC(size)
#define VIP_SMART_FREE(ptr,size) VFREE(ptr)
#endif

typedef u_int32_t VIP_hca_hndl_t;
enum {
  VIP_MAX_HCA=MAX_HCA_DEV_NUM /* Total number HCA devices in one system - taken from hh.h */
};


/* VIP object type */
typedef enum {
  VIP_QPC,
  VIP_MW,
  VIP_MR,
  VIP_FMR,
  VIP_AV,
  VIP_MAX_OBJECT_TYPE /*Used to get max type */
} VIP_obj_type_t;

/* All KL manager objects */
/* Put here as managers need to talk to each other
 * */

/* All please use this type for HOBKL */
typedef struct HOBKL_t* HOBKL_hndl_t;

typedef struct DEVMM_t* DEVMM_hndl_t;
typedef struct PDM_t* PDM_hndl_t;  
typedef struct CQM_t* CQM_hndl_t;
typedef struct MMU_t* MM_hndl_t;
typedef struct SRQM_t* SRQM_hndl_t;
typedef struct QPM_t* QPM_hndl_t;
typedef struct EM_t*  EM_hndl_t;
typedef struct VIP_RSCT_proc_hca_ctx_t* VIP_RSCT_t;

typedef u_int32_t QPM_qp_hndl_t;
typedef u_int32_t SRQM_srq_hndl_t;
#define SRQM_INVAL_SRQ_HNDL 0xFFFFFFFF
typedef u_int32_t CQM_cq_hndl_t;
typedef u_int32_t PDM_pd_hndl_t;  
typedef u_int32_t MM_mrw_hndl_t;
typedef u_int32_t DEVMM_dm_hndl_t;
typedef u_int32_t EM_async_ctx_hndl_t;




enum {
  /* Remap VAPI return codes */

#if 0
  /* Codes used by collections: moved to vip_common.h */
  /* Non error return values */
  VIP_OK                    = VAPI_OK,

  /* General errors */
  VIP_EAGAIN                = VAPI_EAGAIN,  /* Not enough resources (try again later...) */
  VIP_EBUSY                 = VAPI_EBUSY,  /* Resource is in use */

  VIP_ENOMEM                = VAPI_ENOMEM,  /* Invalid address of exhausted physical memory quota */
  VIP_EPERM                 = VAPI_EPERM,  /* Not enough permissions */
  VIP_ENOSYS                = VAPI_ENOSYS,/* Operation/option not supported */
  VIP_EINVAL_PARAM          = VAPI_EINVAL_PARAM, /* invalid parameter*/
#endif

  VIP_EINVAL_HCA_HNDL       = VAPI_EINVAL_HCA_HNDL,

  /* HOB specific errors */
  VIP_EINVAL_HCA_ID         = VAPI_EINVAL_HCA_ID,

  /* PDM specific errors */
  VIP_EINVAL_PD_HNDL        = VAPI_EINVAL_PD_HNDL,

  /* QPM specific errors */
  VIP_EINVAL_QP_HNDL        = VAPI_EINVAL_QP_HNDL,
  VIP_EINVAL_QP_STATE       = VAPI_EINVAL_QP_STATE,
  VIP_EINVAL_PORT           = VAPI_EINVAL_PORT, 
  VIP_EINVAL_SPECIAL_QP     = VAPI_EINVAL_QP_TYPE, /* MST: right ? */
  VIP_EINVAL_SERVICE_TYPE   = VAPI_EINVAL_SERVICE_TYPE,
  VIP_ENOSYS_ATTR           = VAPI_ENOSYS_ATTR,
  VIP_EINVAL_ATTR           = VAPI_EINVAL_ATTR,
  VIP_ENOSYS_ATOMIC         = VAPI_ENOSYS_ATOMIC,
  VIP_EINVAL_PKEY_IX        = VAPI_EINVAL_PKEY_IX,
  VIP_EINVAL_PKEY_TBL_ENTRY = VAPI_EINVAL_PKEY_TBL_ENTRY,
  VIP_EINVAL_RNR_NAK_TIMER  = VAPI_EINVAL_RNR_NAK_TIMER,
  VIP_EINVAL_LOCAL_ACK_TIMEOUT  = VAPI_EINVAL_LOCAL_ACK_TIMEOUT,
  VIP_E2BIG_WR_NUM          = VAPI_E2BIG_WR_NUM,
  VIP_E2BIG_SG_NUM          = VAPI_E2BIG_SG_NUM,
  VIP_EINVAL_MTU            = VAPI_EINVAL_MTU,
  /*multicast */
  VIP_EINVAL_MCG_GID        = VAPI_EINVAL_MCG_GID,
  /* AVDB specific errors */
  VIP_EINVAL_AV_HNDL        = VAPI_EINVAL_AV_HNDL,

  /* SRQM errors */
  VIP_EINVAL_SRQ_HNDL       = VAPI_EINVAL_SRQ_HNDL,
  VIP_ESRQ                  = VAPI_ESRQ,

  /* MMU specific errors */
  VIP_EINVAL_SIZE           = VAPI_EINVAL_LEN,
  VIP_EINVAL_ADDR           = VAPI_EINVAL_VA,
  VIP_EINVAL_MEMACL         = VAPI_EINVAL_ACL,
  VIP_EINVAL_MR_HNDL       	= VAPI_EINVAL_MR_HNDL,
  VIP_EINVAL_MW_HNDL       	= VAPI_EINVAL_MW_HNDL,

  /* EM specific errors */

  /* CQM specific errors */
  
  VIP_EINVAL_CQ_HNDL        = VAPI_EINVAL_CQ_HNDL,
  VIP_EINVAL_ACL            = VAPI_EINVAL_ACL,
  VIP_E2BIG_CQ_NUM          = VAPI_E2BIG_CQ_NUM,
  VIP_EOL                   = VAPI_EOL, /* end of list - no more items */

  /*******************************************************/
  /* VIP specific errors */

  VIP_ERROR_MIN             = VIP_COMMON_ERROR_MAX,  /* Dummy error code: put this VIP error code first */

  /* General errors */
  VIP_ERROR,                /* Generic error code       */

  /* HOB specific errors */
  VIP_EINVAL_HOB_HNDL,

  /* PDM specific errors */
  VIP_EINVAL_PDM_HNDL,
  VIP_EINVAL_OBJ_HNDL,

  /* QPM specific errors */
  VIP_EINVAL_QPM_HNDL,
  VIP_E2SMALL_BUF,

  /* SRQM */
  VIP_EINVAL_SRQM_HNDL,

  /* AVDB specific errors */
  VIP_EINVAL_AVDB_HNDL,

  /* MMU specific errors */
  VIP_EINVAL_MMU_HNDL,
  VIP_EINVAL_MR_TYPE,

  /* EM specific errors */
  VIP_EINVAL_EM_HNDL,

  /* CQM specific errors */
  VIP_EINVAL_CQM_HNDL,
  
  /* DEVMM specific errors */
  VIP_EINVAL_DEVMM_HNDL,
  

  VIP_ERROR_MAX             /* Dummy max error code : put all error codes before this */

};

typedef int32_t VIP_ret_t;

/* used to return parameters from CQM_get_cq_by_id() */
typedef struct {
  CQM_cq_hndl_t cqm_cq_hndl;
  VAPI_cq_hndl_t vapi_cq_hndl;
  VAPI_completion_event_handler_t completion_handler;
  void *private_data;
  struct CQM_cq_t *cq_p;
  CQM_hndl_t cqm_hndl;
  EM_async_ctx_hndl_t async_hndl_ctx;
}
cq_params_t;

#define CREATE_SHIFT 4 /* shift left count to sizes in array and hash create */

#if defined(MT_KERNEL)
typedef struct {
  u_int32_t allocated_hca_handles; 	/* Based on EM's async handler contexts */
  u_int32_t allocated_pd;	  	/* From PDM */
  u_int32_t allocated_cq;		/* From CQM */
  u_int32_t allocated_qp;		/* From QPM */
  u_int32_t allocated_mr;		/* From MMU */
  u_int32_t allocated_fmr;		/* From MMU */
  u_int32_t allocated_mw;		/* From MMU */
  u_int32_t allocated_devmm_chunks;	/* From DEVMM */
} VIPKL_debug_info_t;


struct qp_data_st;
typedef struct qp_item_st {
  struct qp_data_st *qp_list;
  unsigned int count;
}
qp_item_t;
typedef struct {
  qp_item_t qp;
}
VIPKL_rsrc_info_t;
#endif

#endif
