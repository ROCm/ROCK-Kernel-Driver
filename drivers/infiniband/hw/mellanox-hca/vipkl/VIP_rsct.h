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
 #ifndef H_VIP_RSCT_H
 #define H_VIP_RSCT_H

#include <vip.h>
#include <vipkl_eq.h>
#include <vipkl_cqblk.h>


#define VIP_RSCT_IGNORE_CTX ((VIP_RSCT_t)(MT_ulong_ptr_t)0xffffffff)              /* on vapi stop, shut down kernel modules */
#define VIP_RSCT_NULL_USR_CTX ((VIP_RSCT_t)(MT_ulong_ptr_t)0)    /* coming from kernel  */

#define VIP_RSCT_MAX_RSC    10


/* Order of resources in this enumeration is the order of resources destruction ! */
typedef enum {
  VIP_RSCT_EQ,
  VIP_RSCT_CQBLK,
  VIP_RSCT_MW,  /* Destroying MWs before QPs is good for type II MWs */
  VIP_RSCT_QP,
  VIP_RSCT_SRQ,
  VIP_RSCT_CQ,
  VIP_RSCT_MR,
  VIP_RSCT_FMR,
  VIP_RSCT_PD,
  VIP_RSCT_DEVMEM
  } VIP_RSCT_rsctype_t;

typedef union {
     DEVMM_dm_hndl_t    rsc_devmem_hndl;
     MM_mrw_hndl_t      rsc_mr_hndl;
     PDM_pd_hndl_t      rsc_pd_hndl;
     QPM_qp_hndl_t      rsc_qp_hndl;
     SRQM_srq_hndl_t    rsc_srq_hndl;
     CQM_cq_hndl_t      rsc_cq_hndl;
     IB_rkey_t          rsc_mw_hndl;
     VIPKL_EQ_hndl_t    rsc_eq_hndl;
     VIPKL_cqblk_hndl_t rsc_cqblk_hndl;
}VIP_RSCT_rschndl_t;
     
typedef struct VIP_RSCT_rscinfo{
     VIP_RSCT_t usr_ctx; 
     VIP_RSCT_rschndl_t rsc_hndl;
     struct VIP_RSCT_rscinfo* next;
     struct VIP_RSCT_rscinfo* prev;
}VIP_RSCT_rscinfo_t;
 
struct VIP_RSCT_proc_hca_ctx_t{
     VIP_hca_hndl_t hca_hndl;
     MT_size_t hca_ul_resources_sz;
     void *hca_ul_resources_p;
     VIP_RSCT_rscinfo_t* rsc_array[VIP_RSCT_MAX_RSC];
     MOSAL_spinlock_t    rsc_lock_array[VIP_RSCT_MAX_RSC]; /*extra, for the eq */
     EM_async_ctx_hndl_t async_ctx_hndl;
};

      

 VIP_ret_t VIP_RSCT_create(VIP_RSCT_t* obj_p,
                           VIP_hca_hndl_t hca_hndl,
                           MOSAL_protection_ctx_t prot, 
                           MT_size_t hca_ul_resources_sz,
                           void* hca_ul_resources_p,
                           EM_async_ctx_hndl_t *async_hndl_ctx_p);
 
 VIP_ret_t VIP_RSCT_destroy(VIP_RSCT_t rsct);
 
 VIP_ret_t VIP_RSCT_register_rsc(VIP_RSCT_t usr_ctx,VIP_RSCT_rscinfo_t* rsc_p, VIP_RSCT_rsctype_t rsc_type,
                       VIP_RSCT_rschndl_t rsc_h); 
 
 VIP_ret_t VIP_RSCT_deregister_rsc(VIP_RSCT_t usr_ctx,VIP_RSCT_rscinfo_t* rsc_p, VIP_RSCT_rsctype_t rsc_type); 
 
 VIP_ret_t VIP_RSCT_check_usr_ctx(VIP_RSCT_t usr_ctx, VIP_RSCT_rscinfo_t *rsc_p);


 #endif
