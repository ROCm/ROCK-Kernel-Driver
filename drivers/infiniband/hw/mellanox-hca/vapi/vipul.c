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

#include <mtl_common.h>
#include <mosal.h>
#include <vapi.h>
#include <evapi.h>
#include <vapi_common.h>
#include <vipkl.h>
#include <hobul.h>
#include <VIP_rsct.h>
#include <qpm.h>


/* Init user-space HCA handles table */
static HOBUL_hndl_t hca_tbl[VIP_MAX_HCA] = {NULL};
static u_int32_t hca_tbl_ref_cnt[VIP_MAX_HCA] = {0};
static MOSAL_mutex_t hca_tbl_lock;  /* Use when adding/removing/updating-ref-count from the table */
static MT_bool init_done= FALSE;  /* Flag to indicate library was initialized */

#define INVALID_HCA_HNDL(hca_hndl) ((hca_hndl >= VIP_MAX_HCA) || (hca_tbl[hca_hndl] == NULL))
#define CHECK_HCA_HNDL(hca_hndl) \
  if ((hca_hndl >= VIP_MAX_HCA) || (hca_tbl_ref_cnt[hca_hndl] == 0)) \
    return VAPI_EINVAL_HCA_HNDL;

#define DATAPATH_CHECK_HCA_HNDL(hca_hndl) CHECK_HCA_HNDL(hca_hndl)

#if !defined(MT_KERNEL) 

extern void VIPKL_wrap_user_init(void);

#endif /* MT_KERNEL */

/*********************************************************************
 *
 * Function: vipul_init
 *
 * Arguments: (none)
 *
 * RETURN: (void)
 *
 * Description:
 *  This function initializes the VIPUL module for a processing context.
 *  The function is going to be invoked automaticlly for user space (by library loader).
 *  For kernel space, init_module should invoke this function (mod_vapi).
 *
 *********************************************************************/
void vipul_init(void)
{
    int i;
#if !defined(MT_KERNEL)
    static MOSAL_spinlock_t spl = MOSAL_UL_SPINLOCK_STATIC_INIT; /*depends on compiler setting static struct */

    MOSAL_spinlock_lock(&spl);
#endif
    if (init_done)  goto init_exit; /* partial work-around for statically linked libraries */
    
    MTL_DEBUG1(MT_FLFMT("Initializing VIPUL module"));
    for (i= 0; i < VIP_MAX_HCA; i++) hca_tbl_ref_cnt[i]= 0;
    MOSAL_mutex_init(&hca_tbl_lock);

#if !defined(MT_KERNEL)
    MOSAL_user_lib_init();
    VIPKL_wrap_user_init();
#endif
    
    init_done= TRUE;

 init_exit:
#if !defined(MT_KERNEL)
    MOSAL_spinlock_unlock(&spl);   
#else
    ;
#endif
   
}

/* Automatic initialization */
#if defined(MT_KERNEL) || defined(__WIN__)
static void vipul_cleanup(void)
{
    int i;
    call_result_t mt_rc;
    
    MTL_DEBUG1(MT_FLFMT("* * * * inside vipul_cleanup\n"));
    if (MOSAL_mutex_acq(&hca_tbl_lock,FALSE) != MT_OK) return;
    for (i= 0; i < VIP_MAX_HCA; i++)
        if (hca_tbl[i] != NULL)
            HOBUL_delete_force(hca_tbl[i]);
    MOSAL_mutex_rel(&hca_tbl_lock);
    mt_rc = MOSAL_mutex_free(&hca_tbl_lock);
    if (mt_rc != MT_OK) {
      MTL_ERROR2(MT_FLFMT("Failed MOSAL_mutex_free (%s)"),mtl_strerror_sym(mt_rc));
    }
}

int init_module(void)
{
    vipul_init();
    return 0;
}

void cleanup_module(void)
{
     vipul_cleanup();
}

#endif

/* TBD: no solution for static linking with archive libraries */
/* For static linking one must assure that the first invocation to EVAPI_get_hca_hndl is 
   invoked alone (i.e. do not invoke more than once simultaneously)
 */

VAPI_ret_t MT_API EVAPI_get_hca_hndl(
                             VAPI_hca_id_t          hca_id,
                             VAPI_hca_hndl_t       *hca_hndl_p
                             )
{
VAPI_ret_t rc;
#ifndef MT_KERNEL
  vipul_init(); /* partial work-around for statically linked libraries */
#endif
  MTL_DEBUG5("%s: Entering ...\n", __func__);
  rc = VIPKL_get_hca_hndl(hca_id, hca_hndl_p, NULL);
  if (rc == VAPI_OK) {
    /* allocate user resources for this process */
    if (MOSAL_mutex_acq(&hca_tbl_lock,TRUE) != MT_OK)  return VAPI_EINTR;
    if (hca_tbl_ref_cnt[*hca_hndl_p] == 0) { /* First use of this HCA in this context */
      rc= HOBUL_new(*hca_hndl_p,&(hca_tbl[*hca_hndl_p]));
    }
    if (rc == VAPI_OK)  {
        hca_tbl_ref_cnt[*hca_hndl_p]++;
        MTL_DEBUG5("%s: New ref count for HCA handle %d = %d\n", __func__,
                   *hca_hndl_p, hca_tbl_ref_cnt[*hca_hndl_p]);
    }
    MOSAL_mutex_rel(&hca_tbl_lock);
  }
  return rc;
}


VAPI_ret_t MT_API EVAPI_release_hca_hndl(   
                                    /*IN*/ VAPI_hca_hndl_t       hca_hndl
                                 )
{
VAPI_ret_t rc= VAPI_OK;

  if (MOSAL_mutex_acq(&hca_tbl_lock,TRUE) != MT_OK)  return VAPI_EINTR;

  MTL_DEBUG5("%s: Entering (hca handle=%d) ...\n", __func__, hca_hndl);
  if ((hca_hndl >= VIP_MAX_HCA) || (hca_tbl_ref_cnt[hca_hndl] == 0)) {
    MOSAL_mutex_rel(&hca_tbl_lock);
    return VAPI_EINVAL_HCA_HNDL;
  }

  hca_tbl_ref_cnt[hca_hndl]--;
  MTL_DEBUG5("%s:     new ref count for HCA handle %d = %d\n", __func__,
             hca_hndl, hca_tbl_ref_cnt[hca_hndl]);
  if (hca_tbl_ref_cnt[hca_hndl] == 0) {
    rc = HOBUL_delete(hca_tbl[hca_hndl]);
    if (rc == VIP_OK){
        hca_tbl[hca_hndl] = NULL;
    } else {
      hca_tbl_ref_cnt[hca_hndl]++;
      MTL_ERROR2("%s HOBUL_delete failed return: %s\n", __func__, VAPI_strerror(rc));
    }
  }
  MOSAL_mutex_rel(&hca_tbl_lock);
  return rc;
}

VAPI_ret_t MT_API EVAPI_alloc_pd(
                        /*IN*/      VAPI_hca_hndl_t      hca_hndl,
                        /*IN*/      u_int32_t            max_num_avs, 
                        /*OUT*/     VAPI_pd_hndl_t       *pd_hndl_p
                        )
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_alloc_pd(hca_tbl[hca_hndl], max_num_avs, FALSE, pd_hndl_p);
}

VAPI_ret_t MT_API EVAPI_alloc_pd_sqp(
                        /*IN*/      VAPI_hca_hndl_t       hca_hndl,
                        /*IN*/      u_int32_t             max_num_avs, 
                        /*OUT*/     VAPI_pd_hndl_t       *pd_hndl_p
                        )
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_alloc_pd(hca_tbl[hca_hndl], max_num_avs, TRUE, pd_hndl_p);
}


/*******************************************
 * 11.2.1 HCA
 *
 *******************************************/
VAPI_ret_t MT_API  VAPI_open_hca(
                         /*IN*/      VAPI_hca_id_t          hca_id,
                         /*OUT*/     VAPI_hca_hndl_t       *hca_hndl_p
                         )
{
#ifndef MT_KERNEL
    vipul_init(); /* partial work-around for statically linked libraries */
#endif
    return VIPKL_open_hca(hca_id,NULL, NULL, hca_hndl_p);
}

VAPI_ret_t MT_API VAPI_query_hca_cap(
                             /*IN*/      VAPI_hca_hndl_t      hca_hndl,
                             /*OUT*/     VAPI_hca_vendor_t   *hca_vendor_p,
                             /*OUT*/     VAPI_hca_cap_t      *hca_cap_p
                             )
{
    VAPI_ret_t rc;
    CHECK_HCA_HNDL(hca_hndl);

    rc= HOBUL_get_vendor_info(hca_tbl[hca_hndl],hca_vendor_p);
    if (rc != VAPI_OK) {
        MTL_DEBUG1(MT_FLFMT("Failed HOBUL_get_vendor_info (%s)"),VAPI_strerror_sym(rc));
        return rc;
    }
    return VIPKL_query_hca_cap(hca_hndl,hca_cap_p);
}

VAPI_ret_t MT_API VAPI_query_hca_port_prop(
                                   /*IN*/      VAPI_hca_hndl_t      hca_hndl,
                                   /*IN*/      IB_port_t            port_num,
                                   /*OUT*/     VAPI_hca_port_t     *hca_port_p  /* set to NULL if not interested */
                                   )
{
    CHECK_HCA_HNDL(hca_hndl);
    return VIPKL_query_port_prop(hca_hndl,port_num,hca_port_p);
}

VAPI_ret_t MT_API VAPI_query_hca_gid_tbl(
                                 /*IN*/  VAPI_hca_hndl_t           hca_hndl,
                                 /*IN*/  IB_port_t                 port_num,
                                 /*IN*/  u_int16_t                 tbl_len_in,
                                 /*OUT*/ u_int16_t                *tbl_len_out,
                                 /*OUT*/ IB_gid_t                 *gid_tbl_p    
                                 )
{
    CHECK_HCA_HNDL(hca_hndl);
    return VIPKL_query_port_gid_tbl(hca_hndl,port_num,tbl_len_in,tbl_len_out,gid_tbl_p);
}

VAPI_ret_t MT_API VAPI_query_hca_pkey_tbl(
                                  /*IN*/  VAPI_hca_hndl_t           hca_hndl,
                                  /*IN*/  IB_port_t                 port_num,
                                  /*IN*/  u_int16_t                 tbl_len_in,
                                  /*OUT*/ u_int16_t                *tbl_len_out,
                                  /*OUT*/ VAPI_pkey_t              *pkey_tbl_p
                                  )
{
    CHECK_HCA_HNDL(hca_hndl);
    return VIPKL_query_port_pkey_tbl(hca_hndl,port_num,tbl_len_in,tbl_len_out,pkey_tbl_p);
}


VAPI_ret_t MT_API VAPI_modify_hca_attr(
                               /*IN*/  VAPI_hca_hndl_t          hca_hndl,
                               /*IN*/  IB_port_t                port_num,
                               /*IN*/  VAPI_hca_attr_t         *hca_attr_p,
                               /*IN*/  VAPI_hca_attr_mask_t    *hca_attr_mask_p
                               )
{
    CHECK_HCA_HNDL(hca_hndl);
    return VIPKL_modify_hca_attr(hca_hndl,port_num,hca_attr_p,hca_attr_mask_p);
}


VAPI_ret_t MT_API VAPI_close_hca(
                         /*IN*/      VAPI_hca_hndl_t       hca_hndl
                         )
{
    return VIPKL_close_hca(hca_hndl);
}


/* Protection Domain Verbs */
VAPI_ret_t MT_API VAPI_alloc_pd(
                        /*IN*/      VAPI_hca_hndl_t       hca_hndl,
                        /*OUT*/     VAPI_pd_hndl_t       *pd_hndl_p
                        )
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_alloc_pd(hca_tbl[hca_hndl], EVAPI_DEFAULT_AVS_PER_PD, FALSE, pd_hndl_p);
}


VAPI_ret_t MT_API VAPI_dealloc_pd(
                          /*IN*/      VAPI_hca_hndl_t       hca_hndl,
                          /*IN*/      VAPI_pd_hndl_t        pd_hndl
                          )
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_destroy_pd(hca_tbl[hca_hndl],pd_hndl);
}




/*******************************************
 * 11.2.2 Address Management Verbs
 *
 *******************************************/
VAPI_ret_t MT_API VAPI_create_addr_hndl(
                                /*IN*/      VAPI_hca_hndl_t       hca_hndl,
                                /*IN*/      VAPI_pd_hndl_t        pd_hndl,
                                /*IN*/      VAPI_ud_av_t             *av_p,
                                /*OUT*/     VAPI_ud_av_hndl_t        *av_hndl_p
                                )
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_create_av(hca_tbl[hca_hndl], pd_hndl, av_p,av_hndl_p);
}


VAPI_ret_t MT_API VAPI_modify_addr_hndl(
                                /*IN*/      VAPI_hca_hndl_t       hca_hndl,
                                /*IN*/      VAPI_ud_av_hndl_t        av_hndl,
                                /*IN*/      VAPI_ud_av_t             *av_p
                                )
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_set_av_data(hca_tbl[hca_hndl],av_hndl,av_p);
}


VAPI_ret_t MT_API VAPI_query_addr_hndl(
                               /*IN*/      VAPI_hca_hndl_t       hca_hndl,
                               /*IN*/      VAPI_ud_av_hndl_t        av_hndl,
                               /*OUT*/     VAPI_ud_av_t             *av_p
                               )
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_get_av_data(hca_tbl[hca_hndl],av_hndl,av_p);
}


VAPI_ret_t MT_API VAPI_destroy_addr_hndl(
                                 /*IN*/      VAPI_hca_hndl_t       hca_hndl,
                                 /*IN*/      VAPI_ud_av_hndl_t        av_hndl
                                 )
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_destroy_av(hca_tbl[hca_hndl],av_hndl);
}


/*******************************************
 * 11.2.3 Queue Pair
 *
 *******************************************/

VAPI_ret_t MT_API VAPI_create_qp(
                         /*IN*/      VAPI_hca_hndl_t       hca_hndl,
                         /*IN*/      VAPI_qp_init_attr_t  *qp_init_attr_p,
                         /*OUT*/     VAPI_qp_hndl_t       *qp_hndl_p,
                         /*OUT*/     VAPI_qp_prop_t       *qp_prop_p
                         )
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_alloc_qp(hca_tbl[hca_hndl],VAPI_REGULAR_QP,0,qp_init_attr_p, NULL,
                          qp_hndl_p,&(qp_prop_p->qp_num),&(qp_prop_p->cap));
}

VAPI_ret_t MT_API VAPI_create_qp_ext(
                         IN      VAPI_hca_hndl_t       hca_hndl,
                         IN      VAPI_qp_init_attr_t  *qp_init_attr_p,
                         IN      VAPI_qp_init_attr_ext_t *qp_ext_attr_p,
                         OUT     VAPI_qp_hndl_t       *qp_hndl_p,
                         OUT     VAPI_qp_prop_t       *qp_prop_p
                         )
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_alloc_qp(hca_tbl[hca_hndl],VAPI_REGULAR_QP,0,qp_init_attr_p,qp_ext_attr_p,
                          qp_hndl_p,&(qp_prop_p->qp_num),&(qp_prop_p->cap));
}


VAPI_ret_t MT_API VAPI_modify_qp(
                         /*IN*/      VAPI_hca_hndl_t       hca_hndl,
                         /*IN*/      VAPI_qp_hndl_t        qp_hndl,
                         /*IN*/      VAPI_qp_attr_t       *qp_attr_p,
                         /*IN*/      VAPI_qp_attr_mask_t  *qp_attr_mask_p,
                         /*OUT*/     VAPI_qp_cap_t        *qp_cap_p
                         )
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_modify_qp(hca_tbl[hca_hndl],qp_hndl,qp_attr_p,qp_attr_mask_p,qp_cap_p);
}


VAPI_ret_t MT_API VAPI_query_qp(
                        /*IN*/      VAPI_hca_hndl_t       hca_hndl,
                        /*IN*/      VAPI_qp_hndl_t        qp_hndl,
                        /*OUT*/     VAPI_qp_attr_t       *qp_attr_p,
                        /*OUT*/     VAPI_qp_attr_mask_t  *qp_attr_mask_p,
                        /*OUT*/     VAPI_qp_init_attr_t  *qp_init_attr_p
                        )
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_query_qp(hca_tbl[hca_hndl],qp_hndl,qp_attr_p,qp_attr_mask_p,qp_init_attr_p,NULL);
}

VAPI_ret_t MT_API VAPI_query_qp_ext(                        
                        IN      VAPI_hca_hndl_t       hca_hndl,
                        IN      VAPI_qp_hndl_t        qp_hndl,
                        OUT     VAPI_qp_attr_t       *qp_attr_p,
                        OUT     VAPI_qp_attr_mask_t  *qp_attr_mask_p,
                        OUT     VAPI_qp_init_attr_t  *qp_init_attr_p,
                        OUT     VAPI_qp_init_attr_ext_t *qp_init_attr_ext_p
                        )
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_query_qp(hca_tbl[hca_hndl],qp_hndl,qp_attr_p,qp_attr_mask_p,qp_init_attr_p,
                          qp_init_attr_ext_p);
}

VAPI_ret_t MT_API VAPI_destroy_qp(
                          /*IN*/      VAPI_hca_hndl_t       hca_hndl,
                          /*IN*/      VAPI_qp_hndl_t        qp_hndl
                          )
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_destroy_qp(hca_tbl[hca_hndl],qp_hndl);
}


VAPI_ret_t MT_API VAPI_get_special_qp(
                              /*IN*/      VAPI_hca_hndl_t      hca_hndl,
                              /*IN*/      IB_port_t            port,
                              /*IN*/      VAPI_special_qp_t    qp,
                              /*IN*/      VAPI_qp_init_attr_t *qp_init_attr_p,
                              /*OUT*/     VAPI_qp_hndl_t      *qp_hndl_p,
                              /*OUT*/     VAPI_qp_cap_t       *qp_cap_p
                              )
{
    VAPI_qp_num_t qpn;
    CHECK_HCA_HNDL(hca_hndl);
    switch(qp) {
    case VAPI_SMI_QP:
    case VAPI_GSI_QP:
        return HOBUL_alloc_qp(hca_tbl[hca_hndl],qp,port,qp_init_attr_p, NULL,
                              qp_hndl_p,&qpn,qp_cap_p);
    case VAPI_REGULAR_QP:
        return VAPI_EINVAL_PARAM;
    case VAPI_RAW_IPV6_QP:
    case VAPI_RAW_ETY_QP:
    default:
        return VAPI_ENOSYS;
    }
}


VAPI_ret_t MT_API EVAPI_k_get_qp_hndl(
                              /*IN*/     VAPI_hca_hndl_t         hca_hndl,
                              /*IN*/     VAPI_qp_hndl_t          qp_ul_hndl,
                              /*OUT*/    VAPI_k_qp_hndl_t        *qp_kl_hndl
                              )
{
    CHECK_HCA_HNDL(hca_hndl);
    MTL_TRACE1("inside EVAPI_k_get_qp_hndl().\n");
    return HOBUL_vapi2vipkl_qp(hca_tbl[hca_hndl],qp_ul_hndl,qp_kl_hndl);
}

#ifdef MT_KERNEL
VAPI_ret_t MT_API EVAPI_k_modify_qp(
                            /*IN*/     VAPI_hca_hndl_t         hca_hndl,
                            /*IN*/     VAPI_k_qp_hndl_t        qp_kl_hndl,
                            /*IN*/     VAPI_qp_attr_t          *qp_attr_p,
                            /*IN*/     VAPI_qp_attr_mask_t     *qp_attr_mask_p
                            )
{
    CHECK_HCA_HNDL(hca_hndl);
    return VIPKL_modify_qp(VIP_RSCT_IGNORE_CTX,hca_hndl,qp_kl_hndl,qp_attr_mask_p,qp_attr_p);
}

VAPI_ret_t MT_API EVAPI_k_set_destroy_qp_cbk(
  IN   VAPI_hca_hndl_t                  k_hca_hndl,
  IN   VAPI_k_qp_hndl_t                 k_qp_hndl,
  IN   EVAPI_destroy_qp_cbk_t           cbk_func,
  IN   void*                            private_data
)
{
  CHECK_HCA_HNDL(k_hca_hndl);
  return VIPKL_set_destroy_qp_cbk(k_hca_hndl,k_qp_hndl,cbk_func,private_data);
}
 
VAPI_ret_t MT_API EVAPI_k_clear_destroy_qp_cbk(
  IN   VAPI_hca_hndl_t                  k_hca_hndl,
  IN   VAPI_k_qp_hndl_t                 k_qp_hndl
)
{
  CHECK_HCA_HNDL(k_hca_hndl);
  return VIPKL_clear_destroy_qp_cbk(k_hca_hndl,k_qp_hndl);
}

 
#endif

VAPI_ret_t MT_API EVAPI_k_sync_qp_state(
                                 IN      VAPI_hca_hndl_t     hca_hndl,
                                 IN      VAPI_qp_hndl_t      qp_ul_hndl,
                                 IN      VAPI_qp_state_t     curr_state
                                )
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_k_sync_qp_state(hca_tbl[hca_hndl],qp_ul_hndl,curr_state);
}


/*******************************************
 *  SRQs
 *
 *******************************************/

VAPI_ret_t MT_API VAPI_create_srq(
                         IN      VAPI_hca_hndl_t    hca_hndl,
                         IN      VAPI_srq_attr_t  *srq_props_p,
                         OUT     VAPI_srq_hndl_t  *srq_hndl_p,
                         OUT     VAPI_srq_attr_t  *actual_srq_props_p
                         )
{
  CHECK_HCA_HNDL(hca_hndl);
  return HOBUL_create_srq(hca_tbl[hca_hndl], srq_props_p, srq_hndl_p, actual_srq_props_p);
}
                         

VAPI_ret_t MT_API VAPI_query_srq(
                         IN      VAPI_hca_hndl_t   hca_hndl,
                         IN      VAPI_srq_hndl_t   srq_hndl,
                         OUT     VAPI_srq_attr_t   *srq_attr_p
			 )
{
  CHECK_HCA_HNDL(hca_hndl);
  return HOBUL_query_srq(hca_tbl[hca_hndl], srq_hndl, srq_attr_p);
}
			 

VAPI_ret_t MT_API VAPI_modify_srq(
                         IN      VAPI_hca_hndl_t    hca_hndl,
                         IN      VAPI_srq_hndl_t   srq_hndl,
                         IN      VAPI_srq_attr_t  *srq_attr_p,
                         IN      VAPI_srq_attr_mask_t srq_attr_mask,
                         OUT     u_int32_t        *max_outs_wr_p 
			 )
{
  CHECK_HCA_HNDL(hca_hndl);
  return VAPI_ENOSYS;
}



VAPI_ret_t MT_API VAPI_destroy_srq(
                         IN      VAPI_hca_hndl_t    hca_hndl,
                         IN      VAPI_srq_hndl_t   srq_hndl
			 )
{
  CHECK_HCA_HNDL(hca_hndl);
  return HOBUL_destroy_srq(hca_tbl[hca_hndl], srq_hndl);
}

VAPI_ret_t MT_API VAPI_post_srq(
                       IN VAPI_hca_hndl_t       hca_hndl,
                       IN VAPI_srq_hndl_t       srq_hndl,
                       IN u_int32_t             rwqe_num,
                       IN VAPI_rr_desc_t       *rwqe_array,
                       OUT u_int32_t           *rwqe_posted_p)
{
  DATAPATH_CHECK_HCA_HNDL(hca_hndl);
  return HOBUL_post_srq(hca_tbl[hca_hndl],srq_hndl,rwqe_num, rwqe_array, rwqe_posted_p);
}


/*******************************************
 * 11.2.5 Compeletion Queue
 *
 *******************************************/

VAPI_ret_t MT_API VAPI_create_cq(
                         /*IN*/  VAPI_hca_hndl_t         hca_hndl,   
                         /*IN*/  VAPI_cqe_num_t          cqe_num,
                         /*OUT*/ VAPI_cq_hndl_t          *cq_hndl_p,
                         /*OUT*/ VAPI_cqe_num_t          *num_of_entries_p
                         )
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_create_cq(hca_tbl[hca_hndl],cqe_num,cq_hndl_p,num_of_entries_p);
}


VAPI_ret_t MT_API VAPI_query_cq(
                        /*IN*/  VAPI_hca_hndl_t          hca_hndl,
                        /*IN*/  VAPI_cq_hndl_t           cq_hndl,
                        /*OUT*/ VAPI_cqe_num_t          *num_of_entries_p
                        )
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_get_cq_props(hca_tbl[hca_hndl],cq_hndl,num_of_entries_p);
}


VAPI_ret_t MT_API VAPI_resize_cq(
                         /*IN*/  VAPI_hca_hndl_t         hca_hndl,
                         /*IN*/  VAPI_cq_hndl_t          cq_hndl,
                         /*IN*/  VAPI_cqe_num_t          cqe_num,
                         /*OUT*/ VAPI_cqe_num_t          *num_of_entries_p
                         )
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_resize_cq(hca_tbl[hca_hndl],cq_hndl,cqe_num,num_of_entries_p);
}


VAPI_ret_t MT_API VAPI_destroy_cq(
                          /*IN*/  VAPI_hca_hndl_t         hca_hndl,
                          /*IN*/  VAPI_cq_hndl_t          cq_hndl
                          )
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_destroy_cq(hca_tbl[hca_hndl],cq_hndl);
}


/*******************************************
 * 11.2.7 Memory Managemnet
 *
 *******************************************/
VAPI_ret_t MT_API VAPI_register_mr(
                           /*IN*/  VAPI_hca_hndl_t  hca_hndl,
                           /*IN*/  VAPI_mrw_t      *req_mrw_p,
                           /*OUT*/ VAPI_mr_hndl_t  *mr_hndl_p,
                           /*OUT*/ VAPI_mrw_t      *rep_mrw_p
                           )
{
    MM_VAPI_mro_t mr_prop;
    PDM_pd_hndl_t vipkl_pd;
    VIP_ret_t rc;
    u_int32_t i;


    CHECK_HCA_HNDL(hca_hndl);
    if ((req_mrw_p == NULL) ||
        ((req_mrw_p->type != VAPI_MR)&& (req_mrw_p->type != VAPI_MPR)))
        return VAPI_EINVAL_PARAM;
    
    rc= HOBUL_inc_ref_cnt(hca_tbl[hca_hndl]);
    if (rc != VIP_OK)  return rc;

    rc= HOBUL_vapi2vipkl_pd(hca_tbl[hca_hndl],req_mrw_p->pd_hndl,&vipkl_pd);
    if (rc != VIP_OK)  {HOBUL_dec_ref_cnt(hca_tbl[hca_hndl]); return rc;}

    
    if (req_mrw_p->type == VAPI_MPR) {
        MTL_DEBUG1("%s: buf list: \n", __func__);
        for (i=0; i<req_mrw_p->pbuf_list_len; i++ ) {
          MTL_DEBUG1("%s: buf: "U64_FMT" size:"U64_FMT" \n", __func__,
                     req_mrw_p->pbuf_list_p[i].start,req_mrw_p->pbuf_list_p[i].size); 
        }
    }
    

    rc= VIPKL_create_mr(VIP_RSCT_NULL_USR_CTX, hca_hndl,
                        req_mrw_p,vipkl_pd,mr_hndl_p,&mr_prop);
    if (rc != VIP_OK) {
      HOBUL_dec_ref_cnt(hca_tbl[hca_hndl]);  
      MTL_ERROR1(MT_FLFMT("%s failed: %s"), __func__, VAPI_strerror(rc));
      return rc;
    }
    

    rep_mrw_p->l_key= mr_prop.l_key;
    rep_mrw_p->r_key= mr_prop.r_key;
    rep_mrw_p->start= mr_prop.re_local_start;
    rep_mrw_p->size= mr_prop.re_local_end - mr_prop.re_local_start + 1;
    rep_mrw_p->pd_hndl= req_mrw_p->pd_hndl;
    rep_mrw_p->acl= mr_prop.acl;
    rep_mrw_p->type= mr_prop.type;

    /* not returning the buf list - if given!!*/
    rep_mrw_p->pbuf_list_p = NULL;
    rep_mrw_p->pbuf_list_len = 0;
    rep_mrw_p->iova_offset = 0;

    return VAPI_OK;
}


VAPI_ret_t MT_API VAPI_query_mr(
                        /*IN*/  VAPI_hca_hndl_t      hca_hndl,
                        /*IN*/  VAPI_mr_hndl_t       mr_hndl,
                        /*OUT*/ VAPI_mrw_t          *rep_mrw_p,
                        /*OUT*/ VAPI_virt_addr_t    *remote_start_p,
                        /*OUT*/ VAPI_virt_addr_t    *remote_size_p
                        )
{
    MM_VAPI_mro_t mr_prop;
    VIP_ret_t rc;

    CHECK_HCA_HNDL(hca_hndl);
    rc= VIPKL_query_mr(VIP_RSCT_NULL_USR_CTX,hca_hndl,mr_hndl,&mr_prop);
    if (rc == VIP_OK) {
        rc= HOBUL_vipkl2vapi_pd(hca_tbl[hca_hndl],mr_prop.pd_hndl,&(rep_mrw_p->pd_hndl));
        if (rc != VIP_OK)  return rc;
        rep_mrw_p->l_key= mr_prop.l_key;
        rep_mrw_p->r_key= mr_prop.r_key;
        rep_mrw_p->start= mr_prop.re_local_start;
        rep_mrw_p->size= mr_prop.re_local_end - mr_prop.re_local_start + 1;
        rep_mrw_p->acl= mr_prop.acl;
        rep_mrw_p->type= mr_prop.type;
        *remote_start_p= mr_prop.re_remote_start;
        *remote_size_p= mr_prop.re_remote_end - mr_prop.re_remote_start + 1;
    }
    return rc;
}


VAPI_ret_t MT_API VAPI_deregister_mr(
                             /*IN*/ VAPI_hca_hndl_t      hca_hndl,
                             /*IN*/ VAPI_mr_hndl_t       mr_hndl
                             )
{
  VAPI_ret_t ret;  
  CHECK_HCA_HNDL(hca_hndl);
  ret= VIPKL_destroy_mr(VIP_RSCT_NULL_USR_CTX,hca_hndl,mr_hndl);
  if (ret == VAPI_OK)  HOBUL_dec_ref_cnt(hca_tbl[hca_hndl]);
  return ret;
}


VAPI_ret_t MT_API VAPI_reregister_mr(
                             /*IN*/  VAPI_hca_hndl_t       hca_hndl,
                             /*IN*/  VAPI_mr_hndl_t        mr_hndl,
                             /*IN*/  VAPI_mr_change_t      change_type,
                             /*IN*/  VAPI_mrw_t           *req_mrw_p,
                             /*OUT*/ VAPI_mr_hndl_t       *rep_mr_hndl_p,
                             /*OUT*/ VAPI_mrw_t           *rep_mrw_p
                             )
{
    MM_VAPI_mro_t mr_prop;
    PDM_pd_hndl_t vipkl_pd = 0;
    VIP_ret_t rc,rc_d;


    CHECK_HCA_HNDL(hca_hndl);
    if (req_mrw_p == NULL) return VAPI_EINVAL_PARAM;

    if (change_type & VAPI_MR_CHANGE_PD){
        rc= HOBUL_vapi2vipkl_pd(hca_tbl[hca_hndl],req_mrw_p->pd_hndl,&vipkl_pd);
        if (rc != VIP_OK)  {
            rc_d= VIPKL_destroy_mr(VIP_RSCT_NULL_USR_CTX,hca_hndl,mr_hndl);
            if (rc_d == VIP_OK) {
               HOBUL_dec_ref_cnt(hca_tbl[hca_hndl]); 
            }
            return rc;
        }
    }

        
    rc= VIPKL_reregister_mr(VIP_RSCT_NULL_USR_CTX,hca_hndl,mr_hndl,change_type,req_mrw_p,vipkl_pd,
                            rep_mr_hndl_p,&mr_prop);
    
    if (rc != VIP_OK) {
      if ((rc == VIP_EAGAIN) || (rc == VIP_ENOMEM) || (rc == VIP_EINVAL_ACL) || 
          (rc == VIP_EINVAL_SIZE) || (rc == VIP_EINVAL_PARAM)) { /* region invalidated due to failure */
              HOBUL_dec_ref_cnt(hca_tbl[hca_hndl]);
      }
      return rc;
    }
    
    MTL_DEBUG1(MT_FLFMT("--now pd : 0x%x \n"),mr_prop.pd_hndl);
    MTL_DEBUG1(MT_FLFMT("--now acl: 0x%x \n"),mr_prop.acl);

    rep_mrw_p->l_key= mr_prop.l_key;
    rep_mrw_p->r_key= mr_prop.r_key;
    rep_mrw_p->start= mr_prop.re_local_start;
    rep_mrw_p->size= mr_prop.re_local_end - mr_prop.re_local_start + 1;
    rc = HOBUL_vipkl2vapi_pd(hca_tbl[hca_hndl],mr_prop.pd_hndl,&rep_mrw_p->pd_hndl);
    rep_mrw_p->acl= mr_prop.acl;
    rep_mrw_p->type= mr_prop.type;

    /* not returning the buf list - if given!!*/
    rep_mrw_p->pbuf_list_p = NULL;
    rep_mrw_p->pbuf_list_len = 0;
    rep_mrw_p->iova_offset = 0;
    return VAPI_OK;
}


VAPI_ret_t MT_API VAPI_register_smr(
                            /*IN*/  VAPI_hca_hndl_t      hca_hndl,
                            /*IN*/  VAPI_mr_hndl_t       orig_mr_hndl,
                            /*IN*/  VAPI_mr_t           *req_mrw_p,
                            /*OUT*/ VAPI_mr_hndl_t      *mr_hndl_p,
                            /*OUT*/ VAPI_mr_t           *rep_mrw_p
                            )
{
    MM_VAPI_mro_t mr_prop;
    PDM_pd_hndl_t vipkl_pd;
    VIP_ret_t rc;


    CHECK_HCA_HNDL(hca_hndl);
    if (req_mrw_p == NULL)
        return VAPI_EINVAL_PARAM;

    rc= HOBUL_inc_ref_cnt(hca_tbl[hca_hndl]);
    if (rc != VIP_OK)  return rc;
    
    rc= HOBUL_vapi2vipkl_pd(hca_tbl[hca_hndl],req_mrw_p->pd_hndl,&vipkl_pd);
    if (rc != VIP_OK)  {HOBUL_dec_ref_cnt(hca_tbl[hca_hndl]); return rc;}

    req_mrw_p->type = VAPI_MSHAR;  
  MTL_DEBUG1("[VAPI_register_smr]: acl:0x%x type:%d start:"U64_FMT" size:"U64_FMT" pd hndl:0x%x\n",
             req_mrw_p->acl,req_mrw_p->type,
             req_mrw_p->start,req_mrw_p->size,vipkl_pd);

    rc= VIPKL_create_smr(VIP_RSCT_NULL_USR_CTX,hca_hndl,orig_mr_hndl,req_mrw_p,vipkl_pd,mr_hndl_p,&mr_prop);
    if (rc != VIP_OK) {
      HOBUL_dec_ref_cnt(hca_tbl[hca_hndl]);  
      MTL_ERROR1("ERROR: VIPKL_create_smr failed !\n");
      return rc;
    }

    rep_mrw_p->l_key= mr_prop.l_key;
    rep_mrw_p->r_key= mr_prop.r_key;
    rep_mrw_p->start= mr_prop.re_local_start;
    rep_mrw_p->size= mr_prop.re_local_end - mr_prop.re_local_start + 1;
    rep_mrw_p->pd_hndl= req_mrw_p->pd_hndl;
    rep_mrw_p->acl= mr_prop.acl;
    rep_mrw_p->type= VAPI_MSHAR;

    rep_mrw_p->pbuf_list_p = NULL;
    rep_mrw_p->pbuf_list_len = 0;
    rep_mrw_p->iova_offset = 0;

    return VAPI_OK;
}


#ifdef __KERNEL__

VAPI_ret_t MT_API EVAPI_alloc_fmr(
                          /*IN*/   VAPI_hca_hndl_t      hca_hndl,
                          /*IN*/   EVAPI_fmr_t          *fmr_props_p,
                          /*OUT*/  VAPI_mr_hndl_t       *mr_hndl_p
                          )
{
    PDM_pd_hndl_t vipkl_pd;
    VIP_ret_t rc = VAPI_OK;


    CHECK_HCA_HNDL(hca_hndl);
    if (fmr_props_p == NULL)
        return VAPI_EINVAL_PARAM;

    rc= HOBUL_inc_ref_cnt(hca_tbl[hca_hndl]);
    if (rc != VIP_OK)  return rc;
    rc= HOBUL_vapi2vipkl_pd(hca_tbl[hca_hndl],fmr_props_p->pd_hndl,&vipkl_pd);
    if (rc != VIP_OK)  {HOBUL_dec_ref_cnt(hca_tbl[hca_hndl]); return rc;}

    rc = VIPKL_alloc_fmr(VIP_RSCT_NULL_USR_CTX,hca_hndl,fmr_props_p,vipkl_pd,mr_hndl_p);
    if (rc != VIP_OK)  HOBUL_dec_ref_cnt(hca_tbl[hca_hndl]);
    return rc;
}



VAPI_ret_t MT_API EVAPI_map_fmr(
                        /*IN*/   VAPI_hca_hndl_t      hca_hndl,
                        /*IN*/   EVAPI_fmr_hndl_t     fmr_hndl,
                        /*IN*/   EVAPI_fmr_map_t      *map_p,
                        /*OUT*/   VAPI_lkey_t*          l_key_p,
                        /*OUT*/  VAPI_rkey_t*          r_key_p
                        )
{
    VIP_ret_t rc = VAPI_OK;

    CHECK_HCA_HNDL(hca_hndl);
    if (map_p == NULL) {
        return VAPI_EINVAL_PARAM;
    }

    rc = VIPKL_map_fmr(VIP_RSCT_NULL_USR_CTX,hca_hndl,fmr_hndl,map_p,l_key_p,r_key_p);
    return rc;
}


VAPI_ret_t MT_API EVAPI_unmap_fmr(
                          /*IN*/   VAPI_hca_hndl_t      hca_hndl,
                          /*IN*/   MT_size_t            num_of_mr_to_unmap,
                          /*IN*/   VAPI_mr_hndl_t       *mr_hndl_array
                          )
{
    VIP_ret_t rc = VAPI_OK;

    CHECK_HCA_HNDL(hca_hndl);

    rc = VIPKL_unmap_fmr(VIP_RSCT_NULL_USR_CTX,hca_hndl,num_of_mr_to_unmap,mr_hndl_array);
    return rc;
}

VAPI_ret_t MT_API EVAPI_free_fmr(
                         /*IN*/   VAPI_hca_hndl_t      hca_hndl,
                         /*IN*/   VAPI_mr_hndl_t       mr_hndl
                         )
{
    VIP_ret_t rc = VAPI_OK;

    CHECK_HCA_HNDL(hca_hndl);

    rc = VIPKL_free_fmr(VIP_RSCT_NULL_USR_CTX,hca_hndl,mr_hndl);
    if (rc == VIP_OK)  HOBUL_dec_ref_cnt(hca_tbl[hca_hndl]);
    return rc;
}

#endif /* __KERNEL__ */

/*******************************************
 *   Memory Windows
 *******************************************/

VAPI_ret_t MT_API VAPI_alloc_mw(
                        /* IN */      VAPI_hca_hndl_t     hca_hndl,
                        /* IN */      VAPI_pd_hndl_t      pd,
                        /* OUT*/     VAPI_mw_hndl_t*     mw_hndl_p,
                        /* OUT*/     VAPI_rkey_t*        rkey_p
                        )
{
    VAPI_ret_t  rc;
    CHECK_HCA_HNDL(hca_hndl);

    rc = HOBUL_alloc_mw(hca_tbl[hca_hndl], pd, mw_hndl_p, rkey_p);
    return rc;
} /* VAPI_alloc_mw */


VAPI_ret_t MT_API VAPI_query_mw(
                        /* IN */      VAPI_hca_hndl_t     hca_hndl,
                        /* IN */      VAPI_mw_hndl_t      mw_hndl,
                        /* OUT*/     VAPI_rkey_t*        rkey_p,
                        /* OUT*/     VAPI_pd_hndl_t*     pd_p
                        )
{
    VAPI_ret_t  rc;
    CHECK_HCA_HNDL(hca_hndl);

    rc = HOBUL_query_mw(hca_tbl[hca_hndl], mw_hndl, rkey_p, pd_p);
    return rc;
} /* VAPI_query_mw */


VAPI_ret_t MT_API VAPI_bind_mw(
                       /* IN */      VAPI_hca_hndl_t         hca_hndl,
                       /* IN */      VAPI_mw_hndl_t          mw_hndl,
                       /* IN */      const VAPI_mw_bind_t*   bind_prop_p,
                       /* IN */      VAPI_qp_hndl_t          qp, 
                       /* IN */      VAPI_wr_id_t            id,
                       /* IN */      VAPI_comp_type_t        comp_type,
                       /* OUT*/      VAPI_rkey_t*            new_rkey
                       )
{
    VAPI_ret_t  rc;
    CHECK_HCA_HNDL(hca_hndl);

    MTL_DEBUG1("before hobul bind mw \n");
    rc = HOBUL_bind_mw(hca_tbl[hca_hndl], mw_hndl, bind_prop_p, qp, id, comp_type,new_rkey);
    return rc;
} /* VAPI_bind_mw */


VAPI_ret_t MT_API VAPI_dealloc_mw(
                          /* IN */      VAPI_hca_hndl_t     hca_hndl,
                          /* IN */      VAPI_mw_hndl_t      mw_hndl
                          )
{
    VAPI_ret_t  rc;
    CHECK_HCA_HNDL(hca_hndl);

    rc = HOBUL_dealloc_mw(hca_tbl[hca_hndl], mw_hndl);
    return rc;
} /* VAPI_dealloc_mw */


/*******************************************
 *   11.3 Multicast Group
 *******************************************/
VAPI_ret_t MT_API VAPI_attach_to_multicast(
                                   /* IN */     VAPI_hca_hndl_t     hca_hndl,
                                   /* IN */     IB_gid_t            mcg_dgid,
                                   /* IN */     VAPI_qp_hndl_t      qp_hndl,
                                   /* IN */     IB_lid_t            mcg_lid) /*currently ignored */
{
    QPM_qp_hndl_t vipkl_qp;
    VIP_ret_t rc;

    CHECK_HCA_HNDL(hca_hndl);
    rc= HOBUL_vapi2vipkl_qp(hca_tbl[hca_hndl],qp_hndl,&vipkl_qp);
    if (rc != VIP_OK)  return rc;

    return VIPKL_attach_to_multicast(VIP_RSCT_NULL_USR_CTX,hca_hndl, mcg_dgid, vipkl_qp);
}


VAPI_ret_t MT_API VAPI_detach_from_multicast(
                                     /* IN */     VAPI_hca_hndl_t     hca_hndl,
                                     /* IN */     IB_gid_t            mcg_dgid,
                                     /* IN */     VAPI_qp_hndl_t      qp_hndl,
                                     /* IN */     IB_lid_t            mcg_lid) /*currently ignored */
{
    QPM_qp_hndl_t vipkl_qp;
    VIP_ret_t rc;

    CHECK_HCA_HNDL(hca_hndl);
    rc= HOBUL_vapi2vipkl_qp(hca_tbl[hca_hndl],qp_hndl,&vipkl_qp);
    if (rc != VIP_OK) return VAPI_EINVAL_QP_HNDL;

    return VIPKL_detach_from_multicast(VIP_RSCT_NULL_USR_CTX,hca_hndl, mcg_dgid, vipkl_qp);
}



/*******************************************
 *  11.4 Work Request Processing
 *******************************************/

/* Queue Pair Operations */
VAPI_ret_t MT_API VAPI_post_sr(
                       /*IN*/ VAPI_hca_hndl_t       hca_hndl,
                       /*IN*/ VAPI_qp_hndl_t        qp_hndl,
                       /*IN*/ VAPI_sr_desc_t       *sr_desc_p
                       )
{
    DATAPATH_CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_post_sendq(hca_tbl[hca_hndl],qp_hndl,sr_desc_p);
}


VAPI_ret_t MT_API EVAPI_post_inline_sr(
                               /*IN*/ VAPI_hca_hndl_t       hca_hndl,
                               /*IN*/ VAPI_qp_hndl_t        qp_hndl,
                               /*IN*/ VAPI_sr_desc_t       *sr_desc_p
                               )
{
    DATAPATH_CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_post_inline_sendq(hca_tbl[hca_hndl],qp_hndl,sr_desc_p);
}

VAPI_ret_t MT_API EVAPI_post_sr_list(
                       /*IN*/ VAPI_hca_hndl_t       hca_hndl,
                       /*IN*/ VAPI_qp_hndl_t        qp_hndl,
                       /*IN*/ u_int32_t             num_of_requests,
                       /*IN*/ VAPI_sr_desc_t       *sr_desc_array
                       )
{
  DATAPATH_CHECK_HCA_HNDL(hca_hndl);
  return HOBUL_post_list_sendq(hca_tbl[hca_hndl],qp_hndl,num_of_requests,sr_desc_array);
}

VAPI_ret_t MT_API EVAPI_post_gsi_sr(
                       IN VAPI_hca_hndl_t       hca_hndl,
                       IN VAPI_qp_hndl_t        qp_hndl,
                       IN VAPI_sr_desc_t       *sr_desc_p,
                       IN VAPI_pkey_ix_t        pkey_index
                       )
{
    DATAPATH_CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_post_gsi_sendq(hca_tbl[hca_hndl],qp_hndl,sr_desc_p,pkey_index);
}


VAPI_ret_t MT_API VAPI_post_rr(
                       /*IN*/ VAPI_hca_hndl_t       hca_hndl,
                       /*IN*/ VAPI_qp_hndl_t        qp_hndl,
                       /*IN*/ VAPI_rr_desc_t       *rr_desc_p
                       )
{
    DATAPATH_CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_post_recieveq(hca_tbl[hca_hndl],qp_hndl,rr_desc_p);
}

VAPI_ret_t MT_API EVAPI_post_rr_list(
                       /*IN*/ VAPI_hca_hndl_t       hca_hndl,
                       /*IN*/ VAPI_qp_hndl_t        qp_hndl,
                       /*IN*/ u_int32_t             num_of_requests,
                       /*IN*/ VAPI_rr_desc_t       *rr_desc_array
                       )
{
  DATAPATH_CHECK_HCA_HNDL(hca_hndl);
  return HOBUL_post_list_recieveq(hca_tbl[hca_hndl],qp_hndl,num_of_requests,rr_desc_array);
}



/* Completion Queue Operations */
VAPI_ret_t MT_API VAPI_poll_cq(
                       /*IN*/  VAPI_hca_hndl_t      hca_hndl,
                       /*IN*/  VAPI_cq_hndl_t       cq_hndl,
                       /*OUT*/ VAPI_wc_desc_t      *comp_desc_p
                       )
{
    DATAPATH_CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_poll_cq(hca_tbl[hca_hndl],cq_hndl,comp_desc_p);
}

VAPI_ret_t MT_API EVAPI_poll_cq_block(
                              /*IN*/  VAPI_hca_hndl_t      hca_hndl,
                              /*IN*/  VAPI_cq_hndl_t       cq_hndl,
                              /*IN*/  MT_size_t            timeout_usec,
                              /*OUT*/ VAPI_wc_desc_t      *comp_desc_p
                              )
{
    DATAPATH_CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_poll_cq_block(hca_tbl[hca_hndl],cq_hndl,timeout_usec,comp_desc_p);
}

VAPI_ret_t MT_API EVAPI_poll_cq_unblock(
                                /*IN*/  VAPI_hca_hndl_t      hca_hndl,
                                /*IN*/  VAPI_cq_hndl_t       cq_hndl
                                )
{
    DATAPATH_CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_poll_cq_unblock(hca_tbl[hca_hndl],cq_hndl);
}

VAPI_ret_t MT_API EVAPI_peek_cq(
  /*IN*/  VAPI_hca_hndl_t      hca_hndl,
  /*IN*/  VAPI_cq_hndl_t       cq_hndl,
  /*IN*/  VAPI_cqe_num_t       cqe_num
)
{
    DATAPATH_CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_peek_cq(hca_tbl[hca_hndl],cq_hndl,cqe_num);
}


VAPI_ret_t MT_API VAPI_req_comp_notif(
                              /*IN*/  VAPI_hca_hndl_t         hca_hndl,
                              /*IN*/  VAPI_cq_hndl_t          cq_hndl,
                              /*IN*/  VAPI_cq_notif_type_t    notif_type
                              )
{
    DATAPATH_CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_req_comp_notif(hca_tbl[hca_hndl],cq_hndl,notif_type);
}

VAPI_ret_t MT_API EVAPI_req_ncomp_notif(
                              /*IN*/  VAPI_hca_hndl_t         hca_hndl,
                              /*IN*/  VAPI_cq_hndl_t          cq_hndl,
                              /*IN*/  VAPI_cqe_num_t          cqe_num
                              )
{
    DATAPATH_CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_req_ncomp_notif(hca_tbl[hca_hndl],cq_hndl,cqe_num);
}

#if 0
VAPI_ret_t MT_API EVAPI_poll_cq_n(
  IN  VAPI_hca_hndl_t      hca_hndl,
  IN  VAPI_cq_hndl_t       cq_hndl,
  IN  VAPI_cqe_num_t       n,
  IN  MT_size_t            timeout_usec,
  OUT VAPI_wc_desc_t      *comp_desc_array,
  OUT VAPI_cqe_num_t       *cur_n_p)
{
    DATAPATH_CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_poll_cq_n(hca_tbl[hca_hndl],cq_hndl,n,timeout_usec,comp_desc_array,cur_n_p);
}
#endif


/*******************************************
 *  11.5 Event Handling
 *******************************************/
#ifdef MT_KERNEL
VAPI_ret_t MT_API VAPI_set_comp_event_handler(
                                      /*IN*/ VAPI_hca_hndl_t                  hca_hndl,
                                      /*IN*/ VAPI_completion_event_handler_t    handler,
                                      /*IN*/ void* private_data 
                                      )
{
    CHECK_HCA_HNDL(hca_hndl);
    return VIPKL_bind_completion_event_handler(hca_hndl, handler, private_data);
}
#endif

#ifdef MT_KERNEL
VAPI_ret_t MT_API VAPI_set_async_event_handler(
                                       /*IN*/ VAPI_hca_hndl_t                  hca_hndl,
                                       /*IN*/ VAPI_async_event_handler_t         handler,
                                       /*IN*/ void* private_data 
                                       )
{
    CHECK_HCA_HNDL(hca_hndl);
    return VIPKL_bind_async_error_handler(hca_hndl, handler, private_data);
}
#endif

VAPI_ret_t MT_API EVAPI_set_async_event_handler(
                                  /*IN*/  VAPI_hca_hndl_t                 hca_hndl,
                                  /*IN*/  VAPI_async_event_handler_t      handler,
                                  /*IN*/  void*                           private_data,
                                  /*OUT*/ EVAPI_async_handler_hndl_t     *async_handler_hndl_p
                                  )

{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_set_async_event_handler(hca_tbl[hca_hndl], handler, private_data, async_handler_hndl_p);
}


VAPI_ret_t MT_API EVAPI_clear_async_event_handler(
                               /*IN*/ VAPI_hca_hndl_t                  hca_hndl, 
                               /*IN*/ EVAPI_async_handler_hndl_t async_handler_hndl)

{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_clear_async_event_handler(hca_tbl[hca_hndl], async_handler_hndl);
}


VAPI_ret_t MT_API EVAPI_k_get_cq_hndl( /*IN*/  VAPI_hca_hndl_t hca_hndl,
                                /*IN*/  VAPI_cq_hndl_t  cq_hndl,
                                /*OUT*/ VAPI_k_cq_hndl_t *k_cq_hndl_p)
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_vapi2vipkl_cq(hca_tbl[hca_hndl], cq_hndl, (CQM_cq_hndl_t *)k_cq_hndl_p);
}


#ifdef MT_KERNEL
VAPI_ret_t MT_API EVAPI_k_set_comp_eventh(/*IN*/  VAPI_hca_hndl_t                  k_hca_hndl,
                                   /*IN*/  VAPI_k_cq_hndl_t                 k_cq_hndl,
                                   /*IN*/  VAPI_completion_event_handler_t  completion_handler,
                                   /*IN*/  void                            *private_data,
                                   /*OUT*/ EVAPI_compl_handler_hndl_t      *completion_handler_hndl)
{
    VIP_ret_t rc;

    CHECK_HCA_HNDL(k_hca_hndl);
    rc = VIPKL_bind_evapi_completion_event_handler(k_hca_hndl, (CQM_cq_hndl_t)k_cq_hndl, completion_handler, private_data);
    if ( rc != VIP_OK ) {
        return rc;
    }
    *completion_handler_hndl = (EVAPI_compl_handler_hndl_t)k_cq_hndl;
    return VAPI_OK;
}


VAPI_ret_t MT_API EVAPI_k_clear_comp_eventh(/*IN*/  VAPI_hca_hndl_t            k_hca_hndl,
                                     /*IN*/  EVAPI_compl_handler_hndl_t completion_handler_hndl)
{
    CHECK_HCA_HNDL(k_hca_hndl);
    return VIPKL_bind_evapi_completion_event_handler(k_hca_hndl, (CQM_cq_hndl_t)completion_handler_hndl, NULL, NULL);
}

VAPI_ret_t MT_API EVAPI_k_set_destroy_cq_cbk(
  IN   VAPI_hca_hndl_t                  k_hca_hndl,
  IN   VAPI_k_cq_hndl_t                 k_cq_hndl,
  IN   EVAPI_destroy_cq_cbk_t           cbk_func,
  IN   void*                            private_data
)
{
    CHECK_HCA_HNDL(k_hca_hndl);
    return VIPKL_set_destroy_cq_cbk(k_hca_hndl, k_cq_hndl, cbk_func, private_data);
}

 
VAPI_ret_t MT_API EVAPI_k_clear_destroy_cq_cbk(
  IN   VAPI_hca_hndl_t                  k_hca_hndl,
  IN   VAPI_k_cq_hndl_t                 k_cq_hndl
)
{
    CHECK_HCA_HNDL(k_hca_hndl);
    return VIPKL_clear_destroy_cq_cbk(k_hca_hndl, k_cq_hndl);
}
 

#endif /* MT_KERNEL */


VAPI_ret_t MT_API EVAPI_alloc_map_devmem(
					        VAPI_hca_hndl_t 	hca_hndl,
                            EVAPI_devmem_type_t mem_type,
                            VAPI_size_t           bsize,
                            u_int8_t            align_shift, 
                            VAPI_phy_addr_t*    buf_p,
                            void**              virt_addr_p,
                            VAPI_devmem_hndl_t* dm_hndl_p)
{
  VIP_ret_t ret;

  CHECK_HCA_HNDL(hca_hndl);
  if (buf_p == NULL) {
    return VAPI_EINVAL_PARAM;
  }
  ret= HOBUL_inc_ref_cnt(hca_tbl[hca_hndl]);
  if (ret != VIP_OK)  return ret;

  ret= VIPKL_alloc_map_devmem(VIP_RSCT_NULL_USR_CTX,hca_hndl,mem_type,bsize,align_shift,
                              buf_p,virt_addr_p,dm_hndl_p);
  if (ret != VIP_OK)  HOBUL_dec_ref_cnt(hca_tbl[hca_hndl]);
  return ret;
}

VAPI_ret_t MT_API EVAPI_query_devmem(
	   VAPI_hca_hndl_t      hca_hndl, 	   
	   EVAPI_devmem_type_t  mem_type, 	   
       u_int8_t             align_shift,
	   EVAPI_devmem_info_t  *devmem_info_p)
{
    CHECK_HCA_HNDL(hca_hndl);
    if (devmem_info_p == NULL) {
        return VAPI_EINVAL_PARAM;
    }
    return VIPKL_query_devmem(VIP_RSCT_NULL_USR_CTX,hca_hndl,mem_type,align_shift,devmem_info_p);
}


VAPI_ret_t MT_API EVAPI_free_unmap_devmem(
					        VAPI_hca_hndl_t 	hca_hndl,
                            VAPI_devmem_hndl_t dm_hndl
                            )
{
  VIP_ret_t ret;

  CHECK_HCA_HNDL(hca_hndl);
  ret= VIPKL_free_unmap_devmem(VIP_RSCT_NULL_USR_CTX,hca_hndl,dm_hndl);
  if (ret == VIP_OK)  HOBUL_dec_ref_cnt(hca_tbl[hca_hndl]);
  return ret;
}



VAPI_ret_t MT_API EVAPI_set_comp_eventh(
                                /*IN*/   VAPI_hca_hndl_t                  hca_hndl,
                                /*IN*/   VAPI_cq_hndl_t                   cq_hndl,
                                /*IN*/   VAPI_completion_event_handler_t  completion_handler,
                                /*IN*/   void *                           private_data,
                                /*OUT*/  EVAPI_compl_handler_hndl_t       *completion_handler_hndl )
{
    CHECK_HCA_HNDL(hca_hndl);
    *completion_handler_hndl =  cq_hndl;
    return HOBUL_evapi_set_comp_eventh(hca_tbl[hca_hndl],cq_hndl,completion_handler,private_data);
}

VAPI_ret_t MT_API EVAPI_clear_comp_eventh(
                                  /*IN*/  VAPI_hca_hndl_t                  hca_hndl,
                                  /*IN*/  EVAPI_compl_handler_hndl_t       completion_handler_hndl )
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_evapi_clear_comp_eventh(hca_tbl[hca_hndl],completion_handler_hndl);
}

VAPI_ret_t MT_API EVAPI_list_hcas(/* IN*/ u_int32_t         hca_id_buf_sz,
                           /*OUT*/ u_int32_t*       num_of_hcas_p,
                           /*OUT*/ VAPI_hca_id_t*   hca_id_buf_p)
{
#ifndef MT_KERNEL
    vipul_init(); /* partial work-around for statically linked libraries */
#endif
    return VIPKL_list_hcas(hca_id_buf_sz, num_of_hcas_p, hca_id_buf_p);
}

VAPI_ret_t MT_API EVAPI_process_local_mad(
                                  /* IN */  VAPI_hca_hndl_t       hca_hndl,
                                  /* IN */  IB_port_t             port,
                                  /* IN */  IB_lid_t              slid, /* ignored on EVAPI_MAD_IGNORE_MKEY */
                                  /* IN */  EVAPI_proc_mad_opt_t  proc_mad_opts,
                                  /* IN */  const void *          mad_in_p,
                                  /* OUT*/  void *                mad_out_p)
{
    CHECK_HCA_HNDL(hca_hndl);
    return VIPKL_process_local_mad(hca_hndl, port, slid, proc_mad_opts,(void *) mad_in_p, 
                                   mad_out_p);
}

VAPI_ret_t MT_API EVAPI_set_priv_context4qp(
                                    /* IN */   VAPI_hca_hndl_t      hca_hndl,
                                    /* IN */   VAPI_qp_hndl_t       qp,
                                    /* IN */   void *               priv_context)
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_set_priv_context4qp(hca_tbl[hca_hndl],qp,priv_context);
}

VAPI_ret_t MT_API EVAPI_get_priv_context4qp(
                                    /* IN */   VAPI_hca_hndl_t      hca_hndl,
                                    /* IN */   VAPI_qp_hndl_t       qp,
                                    /* OUT*/   void **             priv_context_p)
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_get_priv_context4qp(hca_tbl[hca_hndl],qp,priv_context_p);
}

VAPI_ret_t MT_API EVAPI_set_priv_context4cq(
                                    /* IN */   VAPI_hca_hndl_t      hca_hndl,
                                    /* IN */   VAPI_cq_hndl_t       cq,
                                    /* IN */   void *               priv_context)
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_set_priv_context4cq(hca_tbl[hca_hndl],cq,priv_context);
}

VAPI_ret_t MT_API EVAPI_get_priv_context4cq(
                                    /* IN */   VAPI_hca_hndl_t      hca_hndl,
                                    /* IN */   VAPI_cq_hndl_t       cq,
                                    /* OUT*/   void **             priv_context_p)
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_get_priv_context4cq(hca_tbl[hca_hndl],cq,priv_context_p);
}

VAPI_ret_t MT_API  EVAPI_open_hca(
                           /*IN*/      VAPI_hca_id_t          hca_id,
                           /*IN*/  EVAPI_hca_profile_t    *profile_p,
                           /*OUT*/ EVAPI_hca_profile_t    *sugg_profile_p
                         )
{
    VAPI_hca_hndl_t       hca_hndl;
#ifndef MT_KERNEL
    vipul_init(); /* partial work-around for statically linked libraries */
#endif
    if ((sugg_profile_p != NULL) && (profile_p != NULL)) {
        memcpy(sugg_profile_p, profile_p, sizeof(EVAPI_hca_profile_t));
    }
    return VIPKL_open_hca(hca_id, profile_p, sugg_profile_p, &hca_hndl);
}

VAPI_ret_t MT_API EVAPI_close_hca(
                           /*IN */ VAPI_hca_id_t          hca_id
                             )
{
  VAPI_ret_t rc;
  VAPI_hca_hndl_t       hca_hndl;

  MTL_DEBUG5("%s: Entering ...\n", __func__);
  rc = VIPKL_get_hca_hndl(hca_id, &hca_hndl, NULL);
  if (rc == VAPI_OK) {
    /* allocate user resources for this process */
      rc =  VIPKL_close_hca(hca_hndl);
  }
  return rc;
}


#if defined(MT_SUSPEND_QP)
VAPI_ret_t EVAPI_suspend_qp(
					/*IN*/	VAPI_hca_hndl_t  hca_hndl,
					/*IN*/	VAPI_qp_hndl_t 	 qp_ul_hndl,
                    /*IN*/  MT_bool          suspend_flag)
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_suspend_qp(hca_tbl[hca_hndl],qp_ul_hndl,suspend_flag);
}
VAPI_ret_t EVAPI_suspend_cq(
					/*IN*/	VAPI_hca_hndl_t  hca_hndl,
					/*IN*/	VAPI_cq_hndl_t 	 cq_ul_hndl,
                    /*IN*/  MT_bool          do_suspend)
{
    CHECK_HCA_HNDL(hca_hndl);
    return HOBUL_suspend_cq(hca_tbl[hca_hndl],cq_ul_hndl,do_suspend);
}

#endif


