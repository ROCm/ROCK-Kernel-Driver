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

#include "qpm.h"
#include "qp_xition.h"
#include <cqm.h>
#include <srqm.h>
#include <mmu.h>
#include <vapi_common.h>
#include <vip_hash.h>
#include <vip_common.h>


/* macros *         *               *               */

/* sync on modify qp */
#define QP_MODIFY_BUSY_ON(qp_p)     MOSAL_spinlock_irq_lock(&qp_p->mod_lock);    \
                                    if (qp_p->mod_busy) {   \
                                        MOSAL_spinlock_unlock(&qp_p->mod_lock); \
                                        ret= VIP_EAGAIN;    \
                                        goto fail_busy; \
                                    }else qp_p->mod_busy = TRUE;    \
                                    MOSAL_spinlock_unlock(&qp_p->mod_lock); 
                                   
#define QP_MODIFY_BUSY_OFF(qp_p)    MOSAL_spinlock_irq_lock(&qp_p->mod_lock);    \
                                    qp_p->mod_busy = FALSE;    \
                                    MOSAL_spinlock_unlock(&qp_p->mod_lock);

#define QPM_MCG_BUSY_ON(qp_p)       MOSAL_spinlock_irq_lock(&qp_p->mcg_lock);    \
                                    if (qp_p->mcg_busy) {   \
                                        MOSAL_spinlock_unlock(&qp_p->mcg_lock); \
                                        ret= VIP_EAGAIN;    \
                                        goto retn_nobusy; \
                                    }else qp_p->mcg_busy = TRUE;    \
                                    MOSAL_spinlock_unlock(&qp_p->mcg_lock);

#define QPM_MCG_BUSY_OFF(qp_p)      MOSAL_spinlock_irq_lock(&qp_p->mcg_lock);    \
                                    qp_p->mcg_busy = FALSE;    \
                                    MOSAL_spinlock_unlock(&qp_p->mcg_lock);


#define CHECK_VRET(rfunc,vret)  if (vret != VIP_OK) {                                  \
                                  MTL_ERROR1(MT_FLFMT("%s: %s returned with error: %d = %s"),  \
                                  __func__,rfunc, vret, VIP_common_strerror_sym(vret));      \
                                }
                                
#define PVRET(rfunc,vret)   {MTL_ERROR1(MT_FLFMT("%s: %s returned with error: %d = %s"),        \
                                  __func__,rfunc, vret, VIP_common_strerror_sym(vret));  }
	
typedef QPM_qp_hndl_t special_qp_map_t[QPM_NUM_SPECIAL_QP_TYPE];

typedef struct QPM_static_port_info_t {
	IB_mtu_t	max_mtu;
	u_int16_t	pkey_table_len;
} QPM_static_port_info_t;

typedef struct QPM_t {
  HOBKL_hndl_t hca_hndl;
  HH_hca_hndl_t hh_hndl;
  VAPI_hca_hndl_t vapi_hca;
  VAPI_hca_cap_t  hca_caps;     /* HCA capabilities */
  PDM_hndl_t pdm_hndl;
  CQM_hndl_t cqm_hndl;
  SRQM_hndl_t srqm_hndl;
  MM_hndl_t mm_hndl;
  VIP_hash_p_t  qp_num2hndl;  /* HASH for retrieving a QP handle (QPM_qp_hndl_t) from it number */

  /* Array of QP handles array - 
   * QPM_LAST_QP_TYPE (one for each QP type) for each port. Entry 0 is not used.
   */ 
  special_qp_map_t *special_qp2hndl;
  /* 
  when modifying a QP, some port related attributes must be validated 
  against port info from the HCA. we query once(per port) in QPM_new() 
  & save the relevant static value fields in order to save extra qeuries. 
  */
  QPM_static_port_info_t	*stat_port_info;
  
  VIP_array_p_t qpm_array;       /* the array which holds the maps the qp handles to qp structs */
} QPM_t;


struct QPM_mcg_gid_list_st {
  IB_gid_t gid;
  struct QPM_mcg_gid_list_st *next;
} ;
typedef struct QPM_mcg_gid_list_st* QPM_mcg_gid_list_t;

/* Queue Pair Attributes hold by QPM  */
typedef struct {
  IB_wqpn_t       qp_num;
  VAPI_qp_hndl_t  vapi_qp_hndl;
  QPM_qp_type_t   qp_type;
  IB_port_t       port;        /* Port Number of the QP */
  VAPI_ts_type_t  ts_type;         /* Transport Services Type */
  VAPI_qp_state_t curr_state;	

  VAPI_qp_attr_mask_t mask;
  
  VAPI_sig_type_t   sq_sig_type;   /* Completion Queue Signal Type (ALL/UD)*/
  VAPI_sig_type_t   rq_sig_type;   /* Completion Queue Signal Type (ALL/UD)*/
                                                        
  SRQM_srq_hndl_t srq_hndl;
  CQM_cq_hndl_t  sq_cq_hndl;      /* CQ handle for the SQ */
  CQM_cq_hndl_t  rq_cq_hndl;      /* CQ handle for the RQ */
  PDM_pd_hndl_t   pd_hndl;         /* Protection domain handle */


  // TBD - Rkey
  VAPI_qp_cap_t      cap;          /* The actual capabilities of the QP*/
  
  /* mcg stuff */
  QPM_mcg_gid_list_t mcg_list;     /* the list of mcg gids that this QP belog to */
  MOSAL_spinlock_t mcg_lock;
  MOSAL_spinlock_t mod_lock;
  
  MT_bool mcg_busy;
  MT_bool mod_busy;
    
  VIP_RSCT_rscinfo_t rsc_ctx;
  EM_async_ctx_hndl_t async_hndl_ctx; /* point to the asynch handler of this QP */
  EVAPI_destroy_qp_cbk_t destroy_cbk; /* To be invoked on QP destruction */
  void *destroy_cbk_private_data;
} QPM_qp_prop_t; 


static void VIP_free(void* p)
{
    QPM_qp_prop_t* qp_p = (QPM_qp_prop_t*)p;
    QPM_mcg_gid_list_t mcg_p1;

    while (qp_p->mcg_list != NULL) {
        mcg_p1 = qp_p->mcg_list;
        qp_p->mcg_list = mcg_p1->next;
        FREE(mcg_p1);
    }

    FREE(qp_p); 
    MTL_ERROR1(MT_FLFMT("QPM delete: found unreleased qp in array \n"));
}



static VIP_ret_t create_qp_hh(VIP_RSCT_t usr_ctx,
                              QPM_hndl_t qpm_p, 
                              QPM_qp_init_attr_t *qp_init_attr_p,
                              void *qp_ul_resources_p, 
                              IB_wqpn_t *qp_num_p,
                              VAPI_special_qp_t qp_type, 
                              IB_port_t port);
static VIP_ret_t create_qp_vip(VIP_RSCT_t usr_ctx,
                               QPM_hndl_t qpm_p, 
                               EM_async_ctx_hndl_t async_hndl_ctx,
                               QPM_qp_init_attr_t *qp_init_attr_p,
                               IB_wqpn_t qp_numm, 
                               QPM_qp_prop_t **qp_prop_pp, 
                               QPM_qp_hndl_t *qpm_qp_hndl,
                               VAPI_qp_hndl_t vapi_qp_hndl, 
                               VAPI_special_qp_t qp_type, 
                               IB_port_t port);




/*
MT_bool QPM_check_constants()
{
	return TRUE;	
}
*/

VIP_ret_t QPM_new(HOBKL_hndl_t hca_hndl, QPM_hndl_t *qpm_hndl)
{
  QPM_t 			*new_qpm;
  VIP_ret_t 		ret;
  char 				*error_cause = "";
  VAPI_hca_cap_t 	hca_caps;
  IB_port_t 		port;
  VAPI_hca_port_t 	hob_port_prop_st;
  int 				qpt;  /* qp type (index)*/

  /* get info about the HCA capabilities from hobkl */
  ret = HOBKL_query_cap(hca_hndl, &hca_caps);
  if ( ret != VIP_OK ) {
    MTL_ERROR2("%s: failed HOBKL_query_cap (%s).\n", __func__,VAPI_strerror_sym(ret));
    return VIP_EINVAL_HOB_HNDL;
  }

  /* sanity check - protect from buggy HCA drivers... */
  if ( (hca_caps.phys_port_num<1) || (hca_caps.max_num_qp < 1) )  {
    MTL_ERROR1("%s: invalid HCA caps (phys_port_num=%d , max_qp_num=%d).\n", __func__,
               hca_caps.phys_port_num,hca_caps.max_num_qp);
    return VIP_EINVAL_HOB_HNDL;
  }

  /* create qps data structure */
  new_qpm = TMALLOC(QPM_t);
  if ( !new_qpm ) {
    ret= VIP_EAGAIN;
    error_cause = "MALLOC";
    goto malloc_fail;
  }
  memset(new_qpm, 0, sizeof(QPM_t));
  memcpy(&new_qpm->hca_caps, &hca_caps, sizeof(VAPI_hca_cap_t));

  /* create qps array.  hca_caps.max_num_qps is ONLY for regular QPs. Need to 
   * add number of special qps to the max array size.
   */
  ret = VIP_array_create_maxsize(hca_caps.max_num_qp>>CREATE_SHIFT,
            hca_caps.max_num_qp+
            (hca_caps.phys_port_num * (2 + hca_caps.max_raw_ethy_qp + hca_caps.max_raw_ipv6_qp)), 
            &(new_qpm->qpm_array));
  if ( ret != VIP_OK ) {
    error_cause = "VIP_array_create";
    goto array_fail;  
  }

  /* create hash qp_num -> qp_hndl. Do not need to do anything with special qps here:
   * max ONLY means that the number of buckets does not grow -- the number of entries
   * may still grow -- more entries are put in each bucket.  For the 4-8 QPs special QPs
   * its not worth incrementing the max (number of buckets). */
  ret = VIP_hash_create_maxsize((hca_caps.max_num_qp)>>CREATE_SHIFT, hca_caps.max_num_qp,  &new_qpm->qp_num2hndl);
  if ( ret != VIP_OK ) {
    error_cause = "VIP_hash_create";
    goto hash_fail;  
  }
  /* size shall hold the number of QPs created till now */
  new_qpm->hca_hndl = hca_hndl;
  /* get handle to be used to call HH functions */
  new_qpm->hh_hndl = HOBKL_get_hh_hndl(hca_hndl);
  
  /* get the other VIP Managers handles */
  new_qpm->mm_hndl = HOBKL_get_mm(hca_hndl);
  new_qpm->pdm_hndl = HOBKL_get_pdm(hca_hndl);
  new_qpm->cqm_hndl = HOBKL_get_cqm(hca_hndl);
  new_qpm->srqm_hndl = HOBKL_get_srqm(hca_hndl);

  new_qpm->special_qp2hndl = TNMALLOC(special_qp_map_t, new_qpm->hca_caps.phys_port_num); /* alloc. map per port */
  if ( !new_qpm->special_qp2hndl ) {
    ret = VIP_EAGAIN;
    error_cause = "MALLOC(special_qp2hndl)";
    goto special_fail;
  }

  new_qpm->stat_port_info = TNMALLOC(QPM_static_port_info_t,new_qpm->hca_caps.phys_port_num);
  if( new_qpm->stat_port_info == NULL ) {
	ret= VIP_EAGAIN;
	error_cause = "MALLOC";
	goto special_fail;
  }
  
  for(port=1;port <= new_qpm->hca_caps.phys_port_num;port++) {
    for(qpt=0;qpt < QPM_NUM_SPECIAL_QP_TYPE;qpt++) {
      new_qpm->special_qp2hndl[port-1][qpt]= QPM_INVAL_QP_HNDL; /* init to invalid qp handle */
    }
    
	ret = HOBKL_query_port_prop(new_qpm->hca_hndl,port,&hob_port_prop_st);

	if( ret != VIP_OK ) {
	  PVRET("HOBKL_query_port_prop",ret);
	  goto port_fail;
	}

	new_qpm->stat_port_info[port-1].max_mtu = hob_port_prop_st.max_mtu;
	new_qpm->stat_port_info[port-1].pkey_table_len = hob_port_prop_st.pkey_tbl_len;
	MTL_DEBUG5("%s: initializing stat_port_info[%d] - max_mtu:0x%x, stat_port_info:0x%x", __func__,
			   port-1,new_qpm->stat_port_info[port-1].max_mtu,
			   new_qpm->stat_port_info[port-1].pkey_table_len);
  }
  
  *qpm_hndl = new_qpm;

  return VIP_OK;

  port_fail:
	  FREE(new_qpm->stat_port_info);
  special_fail:
    VIP_hash_destroy(new_qpm->qp_num2hndl);
  hash_fail:
    VIP_array_destroy(new_qpm->qpm_array,0);
  array_fail:
    FREE(new_qpm);
  malloc_fail:
    PVRET(error_cause,ret);
    return ret;
}



VIP_ret_t QPM_delete(QPM_hndl_t qpm_p)
{
  VIP_ret_t ret;
//  QPM_qp_hndl_t qp_hndl;
 // QPM_qp_prop_t* qp_p;
 // QPM_mcg_gid_list_t mcg_p1;

  /* check attributes */
  if ( qpm_p == NULL || qpm_p->qpm_array == NULL) {
    return VIP_EINVAL_QPM_HNDL;
  }
  
  MTL_DEBUG3("Inside " "%s qpm_hndl not null\n", __func__);
  
  /* free static port info array */
  FREE(qpm_p->stat_port_info);

  /* destroy the qp array */
  ret = VIP_array_destroy(qpm_p->qpm_array,VIP_free);
  CHECK_VRET("VIP_array_destroy", ret);
  MTL_DEBUG3("Inside " "%s QPs array destroyed successfully...\n", __func__);
   
  /* destroy the hash table for tranlating qp number to qp handle */
  VIP_hash_destroy(qpm_p->qp_num2hndl);

  /* destroy the special qp map table*/
  FREE(qpm_p->special_qp2hndl);

  FREE(qpm_p);
  MTL_DEBUG3("Inside " "%s finished successfully\n", __func__);
  return VIP_OK;
}

VIP_ret_t  QPM_set_vapi_hca_hndl(/*IN*/ QPM_hndl_t qpm, /*IN*/VAPI_hca_hndl_t hca_hndl)
{
  if (qpm == NULL)  return VIP_EINVAL_QPM_HNDL;
  qpm->vapi_hca = hca_hndl;
  return VIP_OK;
}



VIP_ret_t QPM_create_qp (VIP_RSCT_t usr_ctx,QPM_hndl_t          qpm_hndl, /* handle to the QPM object */
                         VAPI_qp_hndl_t      vapi_qp_hndl, /* VAPI handle to the QP */
                         EM_async_ctx_hndl_t async_hndl_ctx,
                         void                *qp_ul_resources_p,
                         QPM_qp_init_attr_t *qp_init_attr_p,
                         QPM_qp_hndl_t      *qp_hndl_p,
                         IB_wqpn_t          *qp_num_p )
{
  VIP_ret_t rc, rc1;
  IB_wqpn_t qp_num;
  QPM_qp_prop_t *qp_prop_p;


  rc = PDM_add_object_to_pd(usr_ctx, qpm_hndl->pdm_hndl, qp_init_attr_p->pd_hndl, NULL, NULL);
  if ( rc != VIP_OK ) return rc;

  /* enforcement of max num of QPs which may be created is performed in the HH (THH) layer */
  rc = create_qp_hh(usr_ctx,qpm_hndl, qp_init_attr_p, qp_ul_resources_p, &qp_num, QPM_NORMAL_QP, 0);
  if ( rc != VIP_OK ) {
    MTL_ERROR1(MT_FLFMT("%s: HH failed to create qp (err=%d=%s)"), __func__, 
               rc, VAPI_strerror_sym(rc));
    if ( (rc1=PDM_rm_object_from_pd(qpm_hndl->pdm_hndl, qp_init_attr_p->pd_hndl)) != VIP_OK ) {
      MTL_ERROR1(MT_FLFMT("%s: PDM_rm_object_from_pd failed - %s"), __func__, VAPI_strerror(rc1));
    }
    return rc;
  }
  
  rc = create_qp_vip(usr_ctx,qpm_hndl, async_hndl_ctx, qp_init_attr_p, qp_num, 
                     &qp_prop_p, qp_hndl_p, vapi_qp_hndl, 
                      QPM_NORMAL_QP,0/* port field is initialized to 0/invalid for normal QPs */);
  if ( rc != VIP_OK ) {
    MTL_ERROR1(MT_FLFMT("vip failed to create qp"));
    if ( (rc1=PDM_rm_object_from_pd(qpm_hndl->pdm_hndl, qp_init_attr_p->pd_hndl)) != VIP_OK ) {
      MTL_ERROR1(MT_FLFMT("%s: PDM_rm_object_from_pd failed - %s"), __func__, VAPI_strerror(rc1));
    }
    if ( HH_destroy_qp(qpm_hndl->hh_hndl, qp_num) != HH_OK ) {
      MTL_ERROR1(MT_FLFMT("HH failed to destroy qp"));
    }
    return rc;
  }
  *qp_num_p = qp_num;
  
  return VIP_OK;
}


static VIP_ret_t create_qp_vip(VIP_RSCT_t usr_ctx,
                               QPM_hndl_t qpm_p, 
                               EM_async_ctx_hndl_t async_hndl_ctx,
                               QPM_qp_init_attr_t *qp_init_attr_p, 
                               IB_wqpn_t qp_num,
                               QPM_qp_prop_t **qp_prop_pp, 
                               QPM_qp_hndl_t *qpm_qp_hndl_p,
                               VAPI_qp_hndl_t vapi_qp_hndl, 
                               VAPI_special_qp_t qp_type, 
                               IB_port_t port /* for special QPs */) 
{
  QPM_qp_prop_t *qp_prop_p;
  VIP_ret_t rc;
  QPM_qp_hndl_t qpm_qp_hndl;
  VIP_RSCT_rschndl_t r_h;

  qp_prop_p = TNMALLOC(QPM_qp_prop_t, 1);
  if ( !qp_prop_p ) {
      MTL_ERROR1(MT_FLFMT("%s: Could not malloc QPM_qp_prop_t struct"),__func__);
      return VIP_EAGAIN;
  }
  memset (qp_prop_p, 0, sizeof(QPM_qp_prop_t));
  qp_prop_p->qp_type = qp_type;
  /* save the handle for asynch handler context - to be used in case of async errors */
  qp_prop_p->async_hndl_ctx = async_hndl_ctx;
  
  qp_prop_p->curr_state = VAPI_RESET;
  
  QP_ATTR_MASK_CLR_ALL(qp_prop_p->mask); 
  qp_prop_p->mask = (QP_ATTR_QP_STATE | QP_ATTR_QP_NUM | QP_ATTR_CAP);
  qp_prop_p->port = port;
  qp_prop_p->ts_type =      qp_init_attr_p->ts_type;    
  qp_prop_p->sq_sig_type =  qp_init_attr_p->sq_sig_type;
  qp_prop_p->rq_sig_type =  qp_init_attr_p->rq_sig_type;
  qp_prop_p->pd_hndl = qp_init_attr_p->pd_hndl;
  qp_prop_p->rq_cq_hndl = qp_init_attr_p->rq_cq_hndl;
  qp_prop_p->sq_cq_hndl = qp_init_attr_p->sq_cq_hndl;
  qp_prop_p->srq_hndl= qp_init_attr_p->srq_hndl;
  qp_prop_p->qp_num = qp_num;
  qp_prop_p->vapi_qp_hndl = vapi_qp_hndl;
  memcpy( &(qp_prop_p->cap), &(qp_init_attr_p->qp_cap), sizeof(VAPI_qp_cap_t));

  MOSAL_spinlock_init(&qp_prop_p->mcg_lock);
  MOSAL_spinlock_init(&qp_prop_p->mod_lock);

  qp_prop_p->mcg_busy = FALSE;
  qp_prop_p->mod_busy = FALSE;

  rc = VIP_array_insert (qpm_p->qpm_array, qp_prop_p, (VIP_array_handle_t *) &qpm_qp_hndl);
  if ( rc != VIP_OK ) {
    FREE(qp_prop_p);                 
    PVRET("VIP_array_insert",rc);
    return VIP_EAGAIN;
  }
  MTL_DEBUG3("Inside " "%s New qp added to array qp_hndl=%u\n", __func__, qpm_qp_hndl);

  rc = VIP_hash_insert(qpm_p->qp_num2hndl, qp_num, qpm_qp_hndl);
  if ( rc != VIP_OK ) {
    VIP_array_erase(qpm_p->qpm_array, qpm_qp_hndl, NULL);
    FREE(qp_prop_p);                 
    PVRET("VIP_hash_insert",rc);
    return VIP_EAGAIN;
  }

  /* bind the qp to the CQs */
  rc = CQM_bind_qp_to_cq(usr_ctx,qpm_p->cqm_hndl, qp_init_attr_p->sq_cq_hndl, qpm_qp_hndl, NULL);
  if ( rc != VIP_OK ) {
    VIP_hash_erase(qpm_p->qp_num2hndl, qp_num, NULL);
    VIP_array_erase(qpm_p->qpm_array, qpm_qp_hndl, (VIP_array_obj_t*)&qp_prop_p);
    FREE(qp_prop_p);
    PVRET("CQM_bind_qp_to_cq", rc);
    return rc;
  }

  if ( qp_init_attr_p->rq_cq_hndl != qp_init_attr_p->sq_cq_hndl ) {
    rc = CQM_bind_qp_to_cq(usr_ctx,qpm_p->cqm_hndl, qp_init_attr_p->rq_cq_hndl, qpm_qp_hndl, NULL);
    if ( rc != VIP_OK ) {
      CQM_unbind_qp_from_cq (qpm_p->cqm_hndl, qp_init_attr_p->sq_cq_hndl, qpm_qp_hndl);
      VIP_hash_erase(qpm_p->qp_num2hndl, qp_num, NULL);
      VIP_array_erase(qpm_p->qpm_array, qpm_qp_hndl, (VIP_array_obj_t*)&qp_prop_p);
      FREE(qp_prop_p);
      PVRET("CQM_bind_qp_to_cq", rc);
      return rc;
    }
  }
  if ( qp_prop_pp ) *qp_prop_pp = qp_prop_p;
  
  *qpm_qp_hndl_p = qpm_qp_hndl;
  r_h.rsc_qp_hndl = qpm_qp_hndl;
  
  MTL_DEBUG1("before VIP_RSCT_register_rsc  \n");
  VIP_RSCT_register_rsc(usr_ctx,&qp_prop_p->rsc_ctx,VIP_RSCT_QP,r_h);

  return VIP_OK;
}

static VIP_ret_t create_qp_hh(VIP_RSCT_t usr_ctx,QPM_hndl_t qpm_p, QPM_qp_init_attr_t *qp_init_attr_p, void *qp_ul_resources_p,
                              IB_wqpn_t *qp_num_p, VAPI_special_qp_t qp_type, IB_port_t port)
{
  HH_qp_init_attr_t hh_qp_init_attr_st;
  IB_wqpn_t qp_num;
  VIP_ret_t rc;
  HH_ret_t hrc;
  HH_srq_hndl_t hh_srq;
  HH_cq_hndl_t hh_sq_hndl, hh_rq_hndl;
  HH_pd_hndl_t pd_num;

  if ( !qpm_p ) {
    return VIP_EINVAL_QPM_HNDL;
  }

  if (qp_init_attr_p->srq_hndl != SRQM_INVAL_SRQ_HNDL) {
    rc= SRQM_bind_qp_to_srq(usr_ctx, qpm_p->srqm_hndl, qp_init_attr_p->srq_hndl, &hh_srq);
    if ( rc != VIP_OK ) {
      MTL_ERROR1(MT_FLFMT("SRQM_bind_qp_to_srq failed (%d=%s"), rc, VAPI_strerror_sym(rc));
      return rc;
    }
  } else {
    hh_srq= HH_INVAL_SRQ_HNDL;
  }
  
  rc = CQM_get_cq_props (usr_ctx,qpm_p->cqm_hndl, qp_init_attr_p->rq_cq_hndl, &hh_rq_hndl, NULL);
  if ( rc != VIP_OK ) {
    MTL_ERROR1(MT_FLFMT("failed to get rq cq properties"));
    goto failure_cleanup;
  }

  rc = CQM_get_cq_props (usr_ctx,qpm_p->cqm_hndl, qp_init_attr_p->sq_cq_hndl, &hh_sq_hndl, NULL);
  if ( rc != VIP_OK ) {
    MTL_ERROR1(MT_FLFMT("failed to get sq cq properties"));
    goto failure_cleanup;
  }

  rc = PDM_get_pd_id (qpm_p->pdm_hndl, qp_init_attr_p->pd_hndl, &pd_num);
  if ( rc != VIP_OK ) {
    MTL_ERROR1(MT_FLFMT("failed to get pd number"));
    goto failure_cleanup;
  }

  hh_qp_init_attr_st.pd = pd_num;
  hh_qp_init_attr_st.ts_type = qp_init_attr_p->ts_type;
  hh_qp_init_attr_st.rq_cq = hh_rq_hndl;
  hh_qp_init_attr_st.sq_cq = hh_sq_hndl;
  hh_qp_init_attr_st.rq_sig_type = qp_init_attr_p->rq_sig_type;
  hh_qp_init_attr_st.sq_sig_type = qp_init_attr_p->sq_sig_type;
  hh_qp_init_attr_st.srq = hh_srq; 
  memcpy(&(hh_qp_init_attr_st.qp_cap), &(qp_init_attr_p->qp_cap), sizeof(VAPI_qp_cap_t));

  if ( qp_type == QPM_NORMAL_QP ) {
    hrc = HH_create_qp (qpm_p->hh_hndl, &hh_qp_init_attr_st, qp_ul_resources_p, &qp_num);
  } else {  /* Special QP */
    hrc = HH_get_special_qp(qpm_p->hh_hndl, qp_type, port, &hh_qp_init_attr_st, qp_ul_resources_p, &qp_num);
  }
  if ( hrc != HH_OK ) {
    MTL_ERROR2(MT_FLFMT("%s: Failed %s : %s") , __func__, 
               ( qp_type == QPM_NORMAL_QP ) ? "HH_create_qp":"HH_get_special_qp", 
               HH_strerror_sym(hrc));
    rc= (hrc >= VIP_COMMON_ERROR_MIN ? VIP_EGEN : hrc);
    goto failure_cleanup;
  }
  *qp_num_p = qp_num;

  return VIP_OK;

  failure_cleanup:
    if (qp_init_attr_p->srq_hndl != SRQM_INVAL_SRQ_HNDL) {
      SRQM_unbind_qp_from_srq(qpm_p->srqm_hndl, qp_init_attr_p->srq_hndl);
    }
    return rc;
}



VIP_ret_t QPM_get_special_qp (VIP_RSCT_t usr_ctx,
                              QPM_hndl_t          qpm_hndl,
                              VAPI_qp_hndl_t      vapi_qp_hndl, /* VAPI handle to the QP */
                              EM_async_ctx_hndl_t async_hndl_ctx,
                              void               *qp_ul_resources_p, 
                              VAPI_special_qp_t   qp_type,
                              IB_port_t           port,
                              QPM_qp_init_attr_t *qp_init_attr_p, 
                              QPM_qp_hndl_t      *qp_hndl_p, 
                              IB_wqpn_t          *qp_num_p)
{
  VIP_ret_t rc, rc1;
  u_int32_t sqp_index=qp_type-VAPI_SMI_QP;  /* Index of special QP in special_qp2hndl array */
  IB_wqpn_t qp_num;

  MTL_DEBUG1("inside get special qp \n");
  
  if ( !qpm_hndl ) return VIP_EINVAL_QPM_HNDL;
  if ( (port<1) || (port>qpm_hndl->hca_caps.phys_port_num) ) return VIP_EINVAL_PORT;
  if ( (qp_type<VAPI_SMI_QP) || (qp_type>VAPI_RAW_ETY_QP) ) return VIP_ENOSYS;

  if ( qpm_hndl->special_qp2hndl[port-1][sqp_index] != QPM_INVAL_QP_HNDL ) return VIP_EBUSY; /* QP already opened */

  rc = PDM_add_object_to_pd(usr_ctx, qpm_hndl->pdm_hndl, qp_init_attr_p->pd_hndl, NULL, NULL);
  if ( rc != VIP_OK ) return rc;

  MTL_DEBUG1("inside get special qp : before hh\n");
  rc = create_qp_hh(usr_ctx,qpm_hndl, qp_init_attr_p, qp_ul_resources_p, &qp_num, qp_type, port);
  if ( rc != VIP_OK ) {
    if ( (rc1=PDM_rm_object_from_pd(qpm_hndl->pdm_hndl, qp_init_attr_p->pd_hndl)) != VIP_OK ) {
      MTL_ERROR1(MT_FLFMT("%s: PDM_rm_object_from_pd failed - %s"), __func__, VAPI_strerror(rc1));
    }
    MTL_ERROR1(MT_FLFMT("HH failed to create special qp of type=%d"), qp_type);
    return rc;
  }

  rc = create_qp_vip(usr_ctx,qpm_hndl, async_hndl_ctx, qp_init_attr_p, qp_num, NULL, qp_hndl_p,vapi_qp_hndl, 
    qp_type, port); 
  if ( rc != VIP_OK ) {
    MTL_ERROR1(MT_FLFMT("vip failed to create qp"));
    if ( HH_destroy_qp(qpm_hndl->hh_hndl, qp_num) != HH_OK ) {
      MTL_ERROR1(MT_FLFMT("HH failed to destroy qp"));
    }
    if ( (rc1=PDM_rm_object_from_pd(qpm_hndl->pdm_hndl, qp_init_attr_p->pd_hndl)) != VIP_OK ) {
      MTL_ERROR1(MT_FLFMT("%s: PDM_rm_object_from_pd failed - %s"), __func__, VIP_common_strerror_sym(rc1));
    }
    return rc;
  }

  *qp_num_p = qp_num;
  qpm_hndl->special_qp2hndl[port-1][sqp_index]= *qp_hndl_p;

  return VIP_OK;
}

#if 0
VIP_ret_t QPM_get_special_qp (QPM_hndl_t          qpm_hndl,
                              void               *qp_ul_resources_p, 
                              VAPI_special_qp_t   qp_type,
                              IB_port_t           port,
                              QPM_qp_init_attr_t *qp_init_attr_p, 
                              QPM_qp_hndl_t      *qp_hndl_p, 
                              IB_wqpn_t          *sqp_hndl)
{
  VIP_ret_t ret;
  QPM_qp_prop_t *qp_prop_p;
  QPM_t *qpm_p;
  int sqp_index= qp_type-VAPI_SMI_QP;  /* Index of special QP in special_qp2hndl array */

  /* check attributes */
  if (qpm_hndl == NULL || qpm_hndl->qpm_array == NULL) {
    return VIP_EINVAL_QPM_HNDL;
  }
  qpm_p = qpm_hndl;
  MTL_TRACE2("%s: qpm_p=%p\n", __func__,qpm_p);

  if ((port == 0) || (port > qpm_p->hca_caps.phys_port_num )) {
    return VIP_EINVAL_PORT;
  }
  
  if ((qp_type < VAPI_SMI_QP) || (qp_type > VAPI_RAW_ETY_QP))
    return VIP_EINVAL_SPECIAL_QP;
  if ((qp_type != VAPI_SMI_QP) && (qp_type != VAPI_GSI_QP)) {
    return VIP_ENOSYS;  /* not support here for raw types */
  }

  if (qpm_p->special_qp2hndl[port-1][sqp_index] != QPM_INVAL_QP_HNDL) {
    return VIP_EBUSY; /* QP already opened */
  }

  if (qp_hndl_p == NULL) {
    return VIP_EINVAL_QP_HNDL;
  }

  ret= QPM_init_vip_qp_resources(qpm_p,qp_init_attr_p,qp_hndl_p,qp_type,port,&qp_prop_p);
  /* qp_type of VAPI maps directly to QPM's type, so no need to remap */
  if (ret != VIP_OK) {
    MTL_DEBUG2("%s: Failed QPM_init_qp_resources() - err=%d\n", __func__,ret);
    return ret;
  }

  ret= QPM_init_hh_qp_resources(qpm_p,qp_ul_resources_p, qp_init_attr_p,*qp_hndl_p,qp_prop_p,qp_type,port);
  /* qp_type of VAPI maps directly to QPM's type, so no need to remap */
  if (ret != VIP_OK) {
    MTL_DEBUG2("%s: Failed QPM_init_hh_qp_resources() - err=%d\n", __func__,ret);
    return ret;
  }
  
  MTL_DEBUG2("%s: AFTER QPM_init_hh_qp_resources()\n", __func__);
  /* return the qp number (special qp handle) back to upper layer*/
  *sqp_hndl = qp_prop_p->qp_num;
  qpm_p->special_qp2hndl[port-1][sqp_index]= *qp_hndl_p;
  return VIP_OK;
}
#endif


VIP_ret_t QPM_destroy_qp(VIP_RSCT_t usr_ctx,QPM_hndl_t qpm_hndl, QPM_qp_hndl_t qp_hndl,MT_bool in_rsct_cleanup)
{
  VIP_ret_t ret;
  QPM_qp_prop_t *qp_prop_p;
  QPM_t *qpm_p=qpm_hndl;
  QPM_mcg_gid_list_t mcg_p1;

  /* check arguments & get the qp structure */
  if (qpm_hndl == NULL || qpm_hndl->qpm_array == NULL) {
    return VIP_EINVAL_QPM_HNDL;
  }
  qpm_p = qpm_hndl;

  MTL_DEBUG3("Inside " "%s qpm_hndl checked\n", __func__);
 
  ret = VIP_array_find_hold(qpm_hndl->qpm_array,qp_hndl,(VIP_array_obj_t *)&qp_prop_p);
  if (ret != VIP_OK ) {
    return VIP_EINVAL_QP_HNDL;
  }
  ret = VIP_RSCT_check_usr_ctx(usr_ctx,&qp_prop_p->rsc_ctx);
  if (ret != VIP_OK) {
    VIP_array_find_release(qpm_hndl->qpm_array,qp_hndl);
    MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. qp handle=0x%x (%s)"),__func__,qp_hndl,VAPI_strerror_sym(ret));
    return ret;
  } 

  if ((in_rsct_cleanup == FALSE) && (qp_prop_p->mcg_list != NULL)) {
      /* not in cleanup, and qp still attached to multicast group */
      MTL_ERROR1(MT_FLFMT("Could not destroy qp 0x%x --  still attached to a multicast group"),
                 qp_prop_p->qp_num);
      VIP_array_find_release(qpm_hndl->qpm_array,qp_hndl);
      return VIP_EBUSY;
  }
 
  ret = VIP_array_find_release_erase_prepare(qpm_p->qpm_array, qp_hndl, (VIP_array_obj_t *)&qp_prop_p);
  if ( ret != VIP_OK ) {
    MTL_ERROR1(MT_FLFMT("failed VIP_array_erase_prep: got %s handle=%d"),
               VIP_common_strerror_sym(ret),qp_hndl);
    return ret;
  }
  if (qp_prop_p->destroy_cbk) {
    /* Invoke "destroy callback" before this QP handle is put back to free-pool by erase_done */
    qp_prop_p->destroy_cbk(qpm_p->vapi_hca, qp_hndl, qp_prop_p->destroy_cbk_private_data);
  }

  ret = VIP_array_erase_done(qpm_p->qpm_array, qp_hndl, (VIP_array_obj_t *)&qp_prop_p);
  if ( ret != VIP_OK ) {
    MTL_ERROR1(MT_FLFMT("failed VIP_array_erase_done: got %s handle=%d"),
               VIP_common_strerror_sym(ret),qp_hndl);
    return ret;
  }
  

  ret = VIP_hash_erase(qpm_p->qp_num2hndl, qp_prop_p->qp_num, NULL);
  if (ret != VIP_OK ) {
    PVRET("VIP_hash_erase",ret);
  }
  if ( qp_prop_p->qp_type != QPM_NORMAL_QP ) { /* special QP */
    qpm_p->special_qp2hndl[qp_prop_p->port - 1][qp_prop_p->qp_type - QPM_SMI_QP]= QPM_INVAL_QP_HNDL;
  }

  if (qp_prop_p->srq_hndl != SRQM_INVAL_SRQ_HNDL) {
    ret= SRQM_unbind_qp_from_srq(qpm_p->srqm_hndl, qp_prop_p->srq_hndl);
    if (ret != VIP_OK ) {
      PVRET("SRQM_unbind_qp_from_srq",ret);
    }
  }

  ret = CQM_unbind_qp_from_cq(qpm_p->cqm_hndl, qp_prop_p->rq_cq_hndl, qp_hndl);
  if (ret != VIP_OK ) {
    PVRET("CQM_unbind_qp_from_cq",ret);
    /*return ret;*/
  }


  if ( qp_prop_p->rq_cq_hndl != qp_prop_p->sq_cq_hndl) {
    ret = CQM_unbind_qp_from_cq(qpm_p->cqm_hndl, (CQM_cq_hndl_t)qp_prop_p->sq_cq_hndl, qp_hndl);
    if (ret != VIP_OK ) {
      PVRET("CQM_unbind_qp_from_cq",ret);
    }
  }

  /* this QP attached to MCG - need to detach it */
  if (in_rsct_cleanup == TRUE) {
      while (qp_prop_p->mcg_list != NULL) {
        MTL_DEBUG1(MT_FLFMT("%s QP 0x%x was attached to a MCG"), __func__, qp_prop_p->qp_num);
        ret = HH_detach_from_multicast(qpm_hndl->hh_hndl, qp_prop_p->qp_num, qp_prop_p->mcg_list->gid);
        mcg_p1 = qp_prop_p->mcg_list;
        qp_prop_p->mcg_list = mcg_p1->next;
        FREE(mcg_p1);
      }
  }

  /* MTL_DEBUG3("Inside " "%s qp detached from MCG successfully\n", __func__); */
  
  ret = HH_destroy_qp(qpm_p->hh_hndl, qp_prop_p->qp_num);
  if (ret != HH_OK) {
    MTL_ERROR1(MT_FLFMT("HH_destroy_qp returned error for QPN=0x%06x"), qp_prop_p->qp_num);
  }


  ret = PDM_rm_object_from_pd (qpm_p->pdm_hndl, qp_prop_p->pd_hndl);
  if (ret != VIP_OK ) {
    MTL_ERROR1(MT_FLFMT("%s: PDM_rm_object_from_pd failed pd_hndl=%d - %s"), __func__, qp_prop_p->pd_hndl, VAPI_strerror(VAPI_EINVAL_PD_HNDL));
    /* Unexpected error... */
  }
  MTL_DEBUG3("Inside " "%s qp removed from pd  successfully\n", __func__);

  VIP_RSCT_deregister_rsc(usr_ctx,&qp_prop_p->rsc_ctx,VIP_RSCT_QP);
  FREE(qp_prop_p);

  MTL_DEBUG3("Inside " "%s finished successfully\n", __func__);
  return VIP_OK;
}


void reset_qp_mod_attr(VAPI_qp_attr_t *qp_mod_attr_p,QPM_qp_prop_t *qp_prop_p,VAPI_qp_attr_mask_t *qp_mod_mask_p)
{
    qp_mod_attr_p->qp_state = VAPI_RESET; 
    memcpy(&(qp_mod_attr_p->cap), &(qp_prop_p->cap), sizeof(VAPI_qp_cap_t));
    qp_mod_attr_p->dest_qp_num = 0;
    qp_mod_attr_p->min_rnr_timer = IB_RNR_NAK_TIMER_655_36;
    qp_mod_attr_p->ous_dst_rd_atom = 0;
    qp_mod_attr_p->pkey_ix =0;
    qp_mod_attr_p->port = 1;
    qp_mod_attr_p->qkey = 0;
    qp_mod_attr_p->qp_ous_rd_atom =0;
    qp_mod_attr_p->remote_atomic_flags =0;
    qp_mod_attr_p->retry_count =0;
    qp_mod_attr_p->rnr_retry =0;
    qp_mod_attr_p->rq_psn =0;
    qp_mod_attr_p->sq_psn =0;
    qp_mod_attr_p->timeout =0;
    qp_mod_attr_p->path_mtu = MTU256;
    qp_mod_attr_p->path_mig_state = VAPI_MIGRATED;
    //qp_mod_attr_p->av.dgid = 0;
    qp_mod_attr_p->av.dlid = 0;
    qp_mod_attr_p->av.flow_label =0;
    qp_mod_attr_p->av.hop_limit =0;
    qp_mod_attr_p->av.sgid_index =0;
    qp_mod_attr_p->av.sl =0;
    qp_mod_attr_p->av.src_path_bits =0;
    qp_mod_attr_p->av.static_rate =0;
    qp_mod_attr_p->av.traffic_class =0;
    qp_mod_attr_p->av.grh_flag = FALSE;
    //qp_mod_attr_p->alt_av.dgid = 0;
    qp_mod_attr_p->alt_av.dlid = 0;
    qp_mod_attr_p->alt_av.flow_label =0;
    qp_mod_attr_p->alt_av.hop_limit =0;
    qp_mod_attr_p->alt_av.sgid_index =0;
    qp_mod_attr_p->alt_av.sl =0;
    qp_mod_attr_p->alt_av.src_path_bits =0;
    qp_mod_attr_p->alt_av.static_rate =0;
    qp_mod_attr_p->alt_av.traffic_class =0;
    qp_mod_attr_p->alt_av.grh_flag = FALSE;
    qp_mod_attr_p->en_sqd_asyn_notif = FALSE;
    QP_ATTR_MASK_SET_ALL(*qp_mod_mask_p);
    *qp_mod_mask_p = (*qp_mod_mask_p & ALL_SUPPORTED_MASK);
}


VIP_ret_t
QPM_modify_qp(VIP_RSCT_t usr_ctx,QPM_hndl_t qpm_hndl, QPM_qp_hndl_t qp_hndl, 
              VAPI_qp_attr_mask_t *qp_mod_mask_p,VAPI_qp_attr_t *qp_mod_attr_p)
{
  VIP_ret_t ret= VIP_OK;
  VAPI_qp_state_t curr_state;
  VAPI_qp_state_t new_state;
  VAPI_qp_attr_mask_t must_mask;     /* all the required attributes for the transition */
  VAPI_qp_attr_mask_t allowed_mask;     /* all the required and the optional attributes for the transition */
  //VAPI_qp_attr_mask_t supported_mask;   /* the modifiers which are currently supported */
  VAPI_qp_attr_mask_t ts_mask;         /* the modifiers which are appliable for the QP transition service type */
  QPM_qp_prop_t *qp_prop_p;
  IB_port_t		port;
  /*VAPI_hca_port_t hob_port_prop_st;*/
  /*MT_bool port_prop_valid = FALSE;*/
  QPM_t *qpm_p;
  
#ifdef QPX_PRF
  //VAPI_qp_attr_t qp_attr;
#endif
  
  MTL_DEBUG3("---------------------------------------------------------------------\n");
  
  MTL_TRACE1("Inside " "%s\n", __func__);

  if ( qpm_hndl == NULL ) {
    return VIP_EINVAL_QPM_HNDL;
  }

  if ( qpm_hndl->qpm_array == NULL ) {
    return VIP_EINVAL_QPM_HNDL;
  }
  ret = VIP_array_find_hold(qpm_hndl->qpm_array, qp_hndl, (VIP_array_obj_t *)&qp_prop_p);
  MTL_DEBUG3(MT_FLFMT("ret=%d"), ret);
  if ( ret!=VIP_OK ) {
    return VIP_EINVAL_QP_HNDL;
  }

  ret = VIP_RSCT_check_usr_ctx(usr_ctx,&qp_prop_p->rsc_ctx);
  if (ret != VIP_OK) {
    VIP_array_find_release(qpm_hndl->qpm_array, qp_hndl);
    MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. qp handle=0x%x (%s)"),__func__,qp_hndl,VAPI_strerror_sym(ret));
    return ret;
  } 
    
  QP_MODIFY_BUSY_ON(qp_prop_p)

  port = (*qp_mod_mask_p & QP_ATTR_PORT ) ? qp_mod_attr_p->port : qp_prop_p->port;
  
  qpm_p = qpm_hndl;

  /* first check if the state transition is valid 
   * if no state transition check that the current state is valid
   */
  /* get the current state from the hh */
  /*
  MTL_DEBUG3(MT_FLFMT("calling HH_query_qp(%p,0x%x,%p)"),
      qpm_hndl->hh_hndl, qp_prop_p->qp_num, &qp_attr);
  
  ret = HH_query_qp(qpm_hndl->hh_hndl, qp_prop_p->qp_num, &qp_attr);
  
  MTL_DEBUG3(MT_FLFMT("ret=%d"), ret);
  if (ret != HH_OK) {
    VIP_array_find_release(qpm_hndl->qpm_array, qp_hndl);
    PVRET("HH_query_qp",ret);
    return ret;
  }
  */
  
  curr_state = qp_prop_p->curr_state;
  
  MTL_DEBUG3(MT_FLFMT("qpn=0x%x"), qp_prop_p->qp_num);
  MTL_DEBUG3(MT_FLFMT("curr_state=%d=%s"), curr_state, VAPI_qp_state_sym(curr_state));
  if (QP_ATTR_IS_SET(*qp_mod_mask_p, QP_ATTR_QP_STATE) == FALSE) {
    if( (curr_state != VAPI_RTS) && (curr_state != VAPI_INIT) && (curr_state != VAPI_RESET) /*&& (curr_state != VAPI_ERR)*/ ) {
        ret= VIP_EINVAL_ATTR;
        goto end;
    }
    qp_mod_attr_p->qp_state = curr_state;
    //QP_ATTR_MASK_SET(*qp_mod_mask_p, QP_ATTR_QP_STATE);
  }
  new_state = qp_mod_attr_p->qp_state;
  MTL_DEBUG3(MT_FLFMT("new_state=%d=%s"), 
             new_state, VAPI_qp_state_sym(new_state));
  
  /*-----------------------------------------------------------------------------------------------------------*/
  
  if( (new_state < 0) || (new_state >= QPM_NUM_QP_STATE) ) {
      ret= VIP_EINVAL_QP_STATE;
      goto end;
  }

  if( qp_xition_table[curr_state][new_state].valid == FALSE ) {
      MTL_DEBUG3(MT_FLFMT("ERROR: ilegal QP state xition. curr_state:0x%x, new_state:0x%x"),curr_state,new_state);
      ret= VIP_EINVAL_QP_STATE;
      goto end;
  }

  must_mask 	= qp_xition_table[curr_state][new_state].must_mask;
  allowed_mask	= qp_xition_table[curr_state][new_state].allowed_mask;
  
  /* second mask filter according to the transport service type */
  if( (qp_prop_p->ts_type < 0) || (qp_prop_p->ts_type >= QPM_NUM_TS) ) {
	  ts_mask = 0;
  }

  /* TD: leave (ts_mask = 0) to fail later (in THH?), or return here for ilegal TS?*/
  else {
	  ts_mask = qp_ts_attr_support[qp_prop_p->ts_type];
  }
  
  MTL_DEBUG3(MT_FLFMT("usr_mask             =0x%x"), *qp_mod_mask_p);
  MTL_DEBUG3(MT_FLFMT("supported_mask       =0x%x"), ALL_SUPPORTED_MASK);
  MTL_DEBUG3(MT_FLFMT("ts_mask              =0x%x"), ts_mask);
  
  must_mask = must_mask & ts_mask;
  allowed_mask = allowed_mask & ts_mask;
  if ((must_mask & *qp_mod_mask_p) != must_mask ) {
            MTL_ERROR1(MT_FLFMT("%s=>%s: required mask bits missing for this transition: 0x%X\n"), 
               VAPI_qp_state_sym(curr_state), VAPI_qp_state_sym(new_state),((must_mask & *qp_mod_mask_p) ^ must_mask));
            ret= VIP_EINVAL_ATTR;  // required attributes missing
            goto end;
  }
  
  allowed_mask = allowed_mask & ALL_SUPPORTED_MASK;
  if ((allowed_mask | *qp_mod_mask_p) != allowed_mask) {
    MTL_ERROR1(MT_FLFMT("%s=>%s, must_mask: 0x%x, allowed_mask: 0x%x"),
              VAPI_qp_state_sym(curr_state), VAPI_qp_state_sym(new_state), must_mask,allowed_mask);
	MTL_ERROR1(MT_FLFMT("The following bits are not supported or allowed in this transition: 0x%X\n"), 
               (allowed_mask | *qp_mod_mask_p) ^ allowed_mask);
    ret= VIP_ENOSYS_ATTR;  // required attributes missing
    goto end;
  }

  /* check if the modifiers values are valid */
  /* checking if atomic opertion supported */
  if (qpm_p->hca_caps.atomic_cap == VAPI_ATOMIC_CAP_NONE) {
    if ( (*qp_mod_mask_p & QP_ATTR_REMOTE_ATOMIC_FLAGS) && 
         (qp_mod_attr_p->remote_atomic_flags & VAPI_EN_REM_ATOMIC_OP) ) {
      ret= VIP_ENOSYS_ATOMIC;
      goto end;
    }
  }
  
  if (*qp_mod_mask_p & QP_ATTR_PORT ) {
    MTL_DEBUG3("Inside " "%s hca_caps.phys_port_num = %d\n", __func__, qpm_p->hca_caps.phys_port_num);
    if ((qp_mod_attr_p->port == 0) || (qp_mod_attr_p->port > qpm_p->hca_caps.phys_port_num )) {
		MTL_DEBUG3("Inside " "%s qp_mod_attr_p->port = 0x%x\n", __func__,qp_mod_attr_p->port);
      ret=VIP_EINVAL_PORT;
      goto end;
    }
  }

  /* checking if pkey index is out of range or points to empty table entry */
  if (*qp_mod_mask_p & QP_ATTR_PKEY_IX) {
    if (qp_mod_attr_p->pkey_ix >= qpm_p->hca_caps.max_pkeys) {
      MTL_DEBUG3("Inside " "%s fail: qpm_p->hca_caps.max_pkeys = %d\n", __func__, qpm_p->hca_caps.max_pkeys);
      ret=VIP_EINVAL_PKEY_IX;
      goto end;
    }
    // check if pkey points to empty table entry -- TBD
	/*
	if( port_prop_valid == FALSE ) {	
	  ret=HOBKL_query_port_prop (qpm_p->hca_hndl, 
	      // if port is to be updated use new port per Pkey-tbl. - otherwise use current
		  (*qp_mod_mask_p & QP_ATTR_PORT ) ? qp_mod_attr_p->port : qp_prop_p->port,
		  &hob_port_prop_st);
	  if (ret != VIP_OK) {
	    PVRET("HOBKL_query_port_prop",ret);
	    VIP_array_find_release(qpm_hndl->qpm_array, qp_hndl);
	    return ret;
	  }

      MTL_DEBUG3("Inside " "%s note: hob_port_prop_st.pkey_tbl_len = %d.\n", __func__,hob_port_prop_st.pkey_tbl_len);
	  port_prop_valid = TRUE;
	}
	*/
    MTL_DEBUG3("Inside " "%s note: port number will be %d.\n", __func__,port);
    if( qp_mod_attr_p->pkey_ix >= qpm_p->stat_port_info[port-1].pkey_table_len ) {
      MTL_DEBUG3("%s: fail: hob_port_prop_st.pkey_tbl_len = %d, req index: %d\n", __func__,qpm_p->stat_port_info[port].pkey_table_len,qp_mod_attr_p->pkey_ix /*hob_port_prop_st.pkey_tbl_len*/);
      ret= VIP_EINVAL_PKEY_IX;
      goto end;
    }
  }

  /* checking if alternative pkey index is out of range or points to empty table entry */
  if( *qp_mod_mask_p & QP_ATTR_ALT_PATH ) {
    MTL_DEBUG3("Inside " "%s alternative path values are being modified\n", __func__);
    if (qp_mod_attr_p->alt_pkey_ix >= qpm_p->hca_caps.max_pkeys) {
      ret= VIP_EINVAL_PKEY_IX;
      goto end;
    }
    
    /* check that provided alt timeout value has no more than 5 bits */
    if ((qp_mod_attr_p->alt_timeout >> IB_LOCAL_ACK_TIMEOUT_NUM_BITS) != 0) {
        MTL_DEBUG3("%s: fail: alt path local ACK timeout value too large (val=%d)\n", 
                   __func__,qp_mod_attr_p->alt_timeout);
      ret=VIP_EINVAL_LOCAL_ACK_TIMEOUT;
      goto end;
    }
    // check if pkey points to empty table entry -- TBD
    /*
	if( port_prop_valid == FALSE ) {
	  ret=HOBKL_query_port_prop (qpm_p->hca_hndl, qp_prop_p->port, &hob_port_prop_st);
      if (ret != VIP_OK) {
        PVRET("HOBKL_query_port_prop",ret);
        VIP_array_find_release(qpm_hndl->qpm_array, qp_hndl);
        return ret;
      }
	}
	*/
    if (qp_mod_attr_p->alt_pkey_ix >= qpm_p->stat_port_info[port-1].pkey_table_len) {
      ret= VIP_EINVAL_PKEY_IX;
      goto end;
    }
  }

  if (*qp_mod_mask_p & QP_ATTR_PATH_MTU) {
    /*
	if( port_prop_valid == FALSE ) {
	  ret=HOBKL_query_port_prop (qpm_p->hca_hndl, qp_prop_p->port, &hob_port_prop_st);
      if (ret != VIP_OK) {
        PVRET("HOBKL_query_port_prop",ret);
        VIP_array_find_release(qpm_hndl->qpm_array, qp_hndl);
        return ret;
	  }
    }
    */
	if ( (qp_mod_attr_p->path_mtu == 0) || (qp_mod_attr_p->path_mtu > qpm_p->stat_port_info[port-1].max_mtu) ) {
      MTL_DEBUG3("Inside " "%s fail: requested mtu=%d, hob_port_prop_st.max_mtu = %d\n", __func__, qp_mod_attr_p->path_mtu, qpm_p->stat_port_info[port-1].max_mtu);
      ret= VIP_EINVAL_MTU;
      goto end;
    }
  }

  if (*qp_mod_mask_p & QP_ATTR_MIN_RNR_TIMER ) {
      MTL_DEBUG3("%s: min rnr timer being modified (val=%d)\n", __func__, qp_mod_attr_p->min_rnr_timer);
      if ((qp_mod_attr_p->min_rnr_timer>> IB_RNR_NAK_TIMER_NUM_BITS) != 0) {
          MTL_DEBUG3("Inside " "%s fail: min rnr nak timer value too large (val=%d)\n", 
                     __func__,qp_mod_attr_p->min_rnr_timer);
        ret=VIP_EINVAL_RNR_NAK_TIMER;
        goto end;
      }
  }

  if (*qp_mod_mask_p & QP_ATTR_TIMEOUT ) {
      MTL_DEBUG3("%s: local ACK timeout being modified (val=%d)\n", __func__, qp_mod_attr_p->timeout);
      if ((qp_mod_attr_p->timeout >> IB_LOCAL_ACK_TIMEOUT_NUM_BITS) != 0) {
          MTL_DEBUG3("Inside " "%s fail: local ACK timeout value too large (val=%d)\n", 
                     __func__,qp_mod_attr_p->timeout);
        ret=VIP_EINVAL_LOCAL_ACK_TIMEOUT;
        goto end;
      }
  }
  /*
   * in changing state to RESET attributes should be changed to the same values
   *  after the QP was created
   */

  if( new_state == VAPI_RESET ) {
	reset_qp_mod_attr(qp_mod_attr_p,qp_prop_p,qp_mod_mask_p);
  }
  
  MTL_DEBUG3("Inside " "%s: finished testing QP_ATTR flags\n", __func__);
  MTL_DEBUG3("Inside " "%s: qp_mod_attr_p->qp_ous_rd_atom = 0x%x\n", __func__,qp_mod_attr_p->qp_ous_rd_atom);
  MTL_DEBUG3("Inside " "%s: calling HH_modify_qp(%p,%d, %x, %p, %p\n", __func__,
              qpm_p->hh_hndl, qp_prop_p->qp_num, curr_state, qp_mod_attr_p, qp_mod_mask_p
      );

  /* call hh function with the struct */
  
#ifdef QPX_PRF
  prf_counter_pause((&qpm_p->hca_hndl->pc_qpm_mod_qp));
#endif
  
  ret = HH_modify_qp (qpm_p->hh_hndl, qp_prop_p->qp_num, curr_state, qp_mod_attr_p, qp_mod_mask_p);
  
#ifdef QPX_PRF
  prf_counter_resume((&qpm_p->hca_hndl->pc_qpm_mod_qp));
#endif
  MTL_DEBUG3("HH_modify_qp for qpn: 0x%x returned 0x%x.\n",
			 qp_prop_p->qp_num,
			 ret);
  
  if (ret != HH_OK) {
    //PVRET("HH_modify_qp",ret);
    goto end;
  }

  qp_prop_p->curr_state = new_state;
#ifdef QPX_PRF
  qp_mod_attr_p->prev_state = curr_state;
#endif
  /* get the port number (should be in RESET to INIT or any state to RESET) */
  if ((*qp_mod_mask_p & QP_ATTR_PORT ) && 
      (qp_prop_p->qp_type == QPM_NORMAL_QP)) { /* Cannot modify port of special QP */
    qp_prop_p->port = qp_mod_attr_p->port;
  }

  MTL_DEBUG1(MT_FLFMT("curr mask: 0x%x \n"),qp_prop_p->mask);
  if ((new_state != VAPI_RESET) && (new_state != VAPI_ERR))
  {
      MTL_DEBUG1(MT_FLFMT("given mask: 0x%x \n"),*qp_mod_mask_p);
      qp_prop_p->mask|=(*qp_mod_mask_p); 
      MTL_DEBUG1(MT_FLFMT("modified mask: 0x%x \n"),qp_prop_p->mask);
  }else {
      /* "reset" the attributes mask */
      QP_ATTR_MASK_CLR_ALL(qp_prop_p->mask); 
      qp_prop_p->mask = (QP_ATTR_QP_STATE | QP_ATTR_QP_NUM | QP_ATTR_CAP);
  }

end:
    QP_MODIFY_BUSY_OFF(qp_prop_p)
fail_busy:
    VIP_array_find_release(qpm_hndl->qpm_array, qp_hndl);
    return ret;
}

VIP_ret_t QPM_query_qp (VIP_RSCT_t usr_ctx,QPM_hndl_t qpm_hndl, QPM_qp_hndl_t qp_hndl, QPM_qp_query_attr_t *qp_query_prop_p,
                        VAPI_qp_attr_mask_t *qp_mod_mask_p)
{
  VIP_ret_t ret;
  QPM_qp_prop_t *qp_prop_p;
  QPM_t *qpm_p;

  if (qpm_hndl == NULL || qpm_hndl->qpm_array == NULL) {
    return VIP_EINVAL_QPM_HNDL;
  }
  qpm_p = qpm_hndl;


  if (qp_query_prop_p == NULL  || qp_mod_mask_p == NULL) {
    return VIP_EINVAL_PARAM;
  }

  ret = VIP_array_find_hold(qpm_hndl->qpm_array, qp_hndl, (VIP_array_obj_t *)&qp_prop_p);
  if ( ret!=VIP_OK ) {
    PVRET("VIP_array_find",ret);
    return VIP_EINVAL_QP_HNDL;
  }
  
  ret = VIP_RSCT_check_usr_ctx(usr_ctx,&qp_prop_p->rsc_ctx);
  if (ret != VIP_OK) {
    VIP_array_find_release(qpm_hndl->qpm_array, qp_hndl);
    MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. qp handle=0x%x (%s)"),__func__,qp_hndl,VAPI_strerror_sym(ret));
    return ret;
  } 
  

  MTL_DEBUG2("%s: calling HH_query_qp with qp_num=%d \n", __func__,qp_prop_p->qp_num);
  ret = HH_query_qp (qpm_p->hh_hndl, qp_prop_p->qp_num, &(qp_query_prop_p->qp_mod_attr));
  if (ret != HH_OK) {
    VIP_array_find_release(qpm_hndl->qpm_array, qp_hndl);
    PVRET("HH_query_qp",ret);
    return (ret >= VIP_COMMON_ERROR_MIN ? VIP_EGEN : ret);
  }

  /* update curr_state in QPM data base - in case QP has xitioned into (ERR || SQE) */
  qp_prop_p->curr_state = qp_query_prop_p->qp_mod_attr.qp_state;

  /* fill the cap (hold by QPM) */
  memcpy(&(qp_query_prop_p->qp_mod_attr.cap), &(qp_prop_p->cap), sizeof (VAPI_qp_cap_t));

  /* fill the intializing values */
  qp_query_prop_p->pd_hndl = qp_prop_p->pd_hndl;
  qp_query_prop_p->rq_cq_hndl = (CQM_cq_hndl_t)qp_prop_p->rq_cq_hndl;
  qp_query_prop_p->sq_cq_hndl = (CQM_cq_hndl_t)qp_prop_p->sq_cq_hndl;
  qp_query_prop_p->rq_sig_type = qp_prop_p->rq_sig_type;
  qp_query_prop_p->sq_sig_type = qp_prop_p->sq_sig_type;
  qp_query_prop_p->ts_type = qp_prop_p->ts_type;
  
  /*
   * fill the mask - deciding what should be valid and what not
   */
  *qp_mod_mask_p = 0;
  
  /* first add the attributes that always should be valid */
  *qp_mod_mask_p = (*qp_mod_mask_p | QP_ATTR_QP_STATE | QP_ATTR_QP_NUM | QP_ATTR_CAP);

  /* add the attributes which are not valid in RESET state */
  if (qp_query_prop_p->qp_mod_attr.qp_state != VAPI_RESET) {
    (*qp_mod_mask_p) |= qp_prop_p->mask;   
  }

  VIP_array_find_release(qpm_hndl->qpm_array, qp_hndl);
  return VIP_OK;
}



VIP_ret_t QPM_get_vapiqp_by_qp_num(QPM_hndl_t qpm_hndl, 
                                   VAPI_qp_num_t qp_num, 
                                   VAPI_qp_hndl_t *vapi_qp_hndl_p,
                                   EM_async_ctx_hndl_t *async_hndl_ctx)
{
  VIP_ret_t ret= VIP_OK;
  QPM_qp_prop_t *qp_prop_p;
  QPM_qp_hndl_t qp_hndl;

  if (qpm_hndl == NULL || qpm_hndl->qpm_array == NULL) {
    return VIP_EINVAL_QPM_HNDL;
  }

  ret = VIP_hash_find(qpm_hndl->qp_num2hndl,qp_num, &qp_hndl);
  if (ret != VIP_OK) return VIP_EINVAL_QP_HNDL;

  /* will be release after the handler is called */
  ret = VIP_array_find_hold(qpm_hndl->qpm_array, qp_hndl, (VIP_array_obj_t *)(&qp_prop_p));
  if ( ret == VIP_OK ) {
    *vapi_qp_hndl_p = qp_prop_p->vapi_qp_hndl;
    *async_hndl_ctx = qp_prop_p->async_hndl_ctx;
    VIP_array_find_release(qpm_hndl->qpm_array, qp_hndl);
  }
  
  return ret;
}


VIP_ret_t QPM_release_qp_handle(QPM_hndl_t qpm_hndl, QPM_qp_hndl_t qp_hndl) 
{
  return VIP_array_find_release(qpm_hndl->qpm_array, qp_hndl);
}


VIP_ret_t MCG_attach_to_multicast(VIP_RSCT_t usr_ctx,QPM_hndl_t    qpm_hndl, 
                                  IB_gid_t      mcg_dgid,
                                  QPM_qp_hndl_t qp_hndl)
{
  VIP_ret_t ret;
  QPM_qp_prop_t *qp_prop_p;
  QPM_mcg_gid_list_t mcg_p;

  FUNC_IN;

  if (qpm_hndl == NULL || qpm_hndl->qpm_array == NULL) {
    MT_RETURN(VIP_EINVAL_QPM_HNDL);
  }

  MTL_DEBUG3(MT_FLFMT(" got MCG_DGID = %d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d\n"), 
             mcg_dgid[0],mcg_dgid[1],mcg_dgid[2],mcg_dgid[3],mcg_dgid[4],mcg_dgid[5],
             mcg_dgid[6],mcg_dgid[7],mcg_dgid[8],mcg_dgid[9],mcg_dgid[10],mcg_dgid[11],
             mcg_dgid[12],mcg_dgid[13],mcg_dgid[14],mcg_dgid[15]);


  if (!IB_VALID_MULTICAST_GID(mcg_dgid)) {
    MTL_ERROR1("%s: mcg_dgid is not valid mulitcat gid (byte 0 != 0xFF)\n", __func__);
    MT_RETURN(VAPI_EINVAL_MCG_GID);
  }

  ret = VIP_array_find_hold(qpm_hndl->qpm_array, qp_hndl, (VIP_array_obj_t *)&qp_prop_p);
  if ( ret!=VIP_OK ) {
    PVRET("No QP was found",ret);
    MT_RETURN(VIP_EINVAL_QP_HNDL);
  }

  ret = VIP_RSCT_check_usr_ctx(usr_ctx,&qp_prop_p->rsc_ctx);
  if (ret != VIP_OK) {
      MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. qp handle=0x%x (%s)"),__func__,qp_hndl,VAPI_strerror_sym(ret));
      goto retn_nobusy;  
  }
    
  
  /* check that this is UD QP */
  if ((int)(qp_prop_p->ts_type) != VAPI_TS_UD) {
    ret = VAPI_EINVAL_SERVICE_TYPE;
    PVRET("QP transport type is not UD",ret);
    goto retn_nobusy;
  }
  MTL_DEBUG3("%s: QP_num =%d \n", __func__,qp_prop_p->qp_num);
  
  QPM_MCG_BUSY_ON(qp_prop_p)
  
  /* search for the gid in the list */
  mcg_p = qp_prop_p->mcg_list;
  while (mcg_p != NULL) {
    /*MTL_DEBUG3("%s: in gids list. GID = %d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d\n", __func__, 
               mcg_p->gid[0],mcg_p->gid[1],mcg_p->gid[2],mcg_p->gid[3],mcg_p->gid[4],mcg_p->gid[5],
               mcg_p->gid[6],mcg_p->gid[7],mcg_p->gid[8],mcg_p->gid[9],mcg_p->gid[10],mcg_p->gid[11],
               mcg_p->gid[12],mcg_p->gid[13],mcg_p->gid[14],mcg_p->gid[15]);                         */
    
    if (memcmp(mcg_dgid, mcg_p->gid, sizeof(IB_gid_t)) != 0) {
      /* not this gid */
      mcg_p = mcg_p->next;
    }
    else {
      MTL_DEBUG3("%s: gid found, no need to attach this qp again..\n", __func__);
      goto retn;
    }
  }
  
  MTL_DEBUG2(MT_FLFMT("gid is not in qp gids list. attaching the gid \n"));
  ret = HH_attach_to_multicast(qpm_hndl->hh_hndl, qp_prop_p->qp_num, mcg_dgid);
  if (ret != VIP_OK) {
    PVRET("Attach to MCG failed",ret);
    goto retn;
  }
  
  /* need to add gid since it is not in list */
  
    mcg_p = (QPM_mcg_gid_list_t)MALLOC(sizeof(struct QPM_mcg_gid_list_st));
    if (mcg_p == NULL) {
      ret =  VIP_EAGAIN;
      goto inval;
    }
    memcpy(mcg_p->gid, mcg_dgid, sizeof(IB_gid_t));
    mcg_p->next = qp_prop_p->mcg_list;
    qp_prop_p->mcg_list = mcg_p;
  
  
inval:
    if (ret != VIP_OK) {
        HH_detach_from_multicast(qpm_hndl->hh_hndl,qp_prop_p->qp_num,mcg_dgid);
    }
retn:
  QPM_MCG_BUSY_OFF(qp_prop_p)
retn_nobusy:  
  VIP_array_find_release(qpm_hndl->qpm_array, qp_hndl);
  MT_RETURN(ret);
}



VIP_ret_t MCG_detach_from_multicast(VIP_RSCT_t usr_ctx,QPM_hndl_t    qpm_hndl, 
                                    IB_gid_t      mcg_dgid,
                                    QPM_qp_hndl_t qp_hndl)

{
  VIP_ret_t ret;
  QPM_qp_prop_t *qp_prop_p;
  QPM_mcg_gid_list_t mcg_p1, mcg_p2;

  FUNC_IN;

  if (qpm_hndl == NULL || qpm_hndl->qpm_array == NULL) {
    MT_RETURN(VIP_EINVAL_QPM_HNDL);
  }

  MTL_DEBUG3(MT_FLFMT(" got MCG_DGID = %d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d\n"), 
             mcg_dgid[0],mcg_dgid[1],mcg_dgid[2],mcg_dgid[3],mcg_dgid[4],mcg_dgid[5],
             mcg_dgid[6],mcg_dgid[7],mcg_dgid[8],mcg_dgid[9],mcg_dgid[10],mcg_dgid[11],
             mcg_dgid[12],mcg_dgid[13],mcg_dgid[14],mcg_dgid[15]);


  if (!IB_VALID_MULTICAST_GID(mcg_dgid)) {
    MTL_ERROR1("%s mcg_dgid is not valid mulitcat gid (byte[0] != 0xFF)\n", __func__);
    MT_RETURN(VAPI_EINVAL_MCG_GID);
  }

  ret = VIP_array_find_hold(qpm_hndl->qpm_array, qp_hndl, (VIP_array_obj_t *)&qp_prop_p);
  if ( ret!=VIP_OK ) {
    PVRET("No QP was found",ret);
    MT_RETURN(VIP_EINVAL_QP_HNDL);
  }

   ret = VIP_RSCT_check_usr_ctx(usr_ctx,&qp_prop_p->rsc_ctx);
   if (ret != VIP_OK) {
       MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. qp handle=0x%x (%s)"),__func__,qp_hndl,VAPI_strerror_sym(ret));
       goto retn_nobusy;  
   }

  
  /* check that this is UD QP */
  if ((int)(qp_prop_p->ts_type) != VAPI_TS_UD) {
      ret = VAPI_EINVAL_SERVICE_TYPE;
      PVRET("QP transport type is not UD",ret);
      goto retn_nobusy;
  }
  
  QPM_MCG_BUSY_ON(qp_prop_p)

  /* no MCG for this QP */
  if ( qp_prop_p->mcg_list == NULL ) {
      ret =  VIP_EINVAL_MCG_GID;
      PVRET("No MCG for this QP\n",ret);
      goto retn;
  }
  
  MTL_DEBUG3("%s QP_num =%d related to some MC groups\n", __func__, qp_prop_p->qp_num);
  
  /* search the gid */
  mcg_p1 = qp_prop_p->mcg_list;
  mcg_p2 = NULL;
  while (mcg_p1 != NULL) {
/*    MTL_DEBUG3("%s in gids list. GID = %d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d\n", __func__, 
               mcg_p1->gid[0],mcg_p1->gid[1],mcg_p1->gid[2],mcg_p1->gid[3],mcg_p1->gid[4],mcg_p1->gid[5],
               mcg_p1->gid[6],mcg_p1->gid[7],mcg_p1->gid[8],mcg_p1->gid[9],mcg_p1->gid[10],mcg_p1->gid[11],
               mcg_p1->gid[12],mcg_p1->gid[13],mcg_p1->gid[14],mcg_p1->gid[15]);*/
    if (memcmp(mcg_dgid, mcg_p1->gid, sizeof(IB_gid_t)) != 0) {
      /* not this gid */
      mcg_p2 = mcg_p1;
      mcg_p1 = mcg_p1->next;
    }else
        {
        MTL_DEBUG3(MT_FLFMT("found gid \n")); 
        break;
    }
  }
  
  /* gid doesn't appear in qp gid list */
  if (mcg_p1 == NULL) {
     ret =  VIP_EINVAL_MCG_GID;
    PVRET("No MCG for this QP\n",ret);
    goto retn;
  }
    
  MTL_DEBUG2(MT_FLFMT("removing gid from qp \n"));
  ret = HH_detach_from_multicast(qpm_hndl->hh_hndl, qp_prop_p->qp_num, mcg_dgid);
  if (ret != VIP_OK) {
    PVRET("Detach from MCG failed",ret);
    goto retn;  
  }

  /* remove this gid from the mcg_list */
   
  if (mcg_p2 != NULL){
  /* not the first element in the list */
    MTL_DEBUG3("%s NOT first gid in list \n", __func__);
    mcg_p2->next = mcg_p1->next;
  }
  else {
        /* need to move head of the list */
     MTL_DEBUG3("%s FIRST gid in list \n", __func__);
     qp_prop_p->mcg_list = mcg_p1->next;
     MTL_DEBUG3("%s After next change \n", __func__);
  }
  FREE(mcg_p1);
  MTL_DEBUG3("%s After FREE \n", __func__);
  
  MTL_DEBUG3("%s completed succesfuly. There was MCG for this QP\n", __func__);
  
retn:
  QPM_MCG_BUSY_OFF(qp_prop_p)
retn_nobusy:
  VIP_array_find_release(qpm_hndl->qpm_array, qp_hndl);
  MT_RETURN(ret);
}

VIP_ret_t QPM_set_destroy_qp_cbk(
  /*IN*/ QPM_hndl_t               qpm_hndl,
  /*IN*/ QPM_qp_hndl_t            qp_hndl,
  /*IN*/ EVAPI_destroy_qp_cbk_t   cbk_func,
  /*IN*/ void*                    private_data
)
{
  QPM_qp_prop_t *qp_p;
  VIP_ret_t ret;

  ret = VIP_array_find_hold(qpm_hndl->qpm_array, qp_hndl, (VIP_array_obj_t *)&qp_p);
  if (ret == VIP_EINVAL_HNDL) {
    return VIP_EINVAL_QP_HNDL;
  }

  /* Under API assumptions - we do not protect against a race of simultaneous set for the same QP */
  /* (It is a trusted kernel code that invokes this - at most it will harm itself) */
  if (qp_p->destroy_cbk != NULL) {
    VIP_array_find_release(qpm_hndl->qpm_array, qp_hndl);
    return VIP_EBUSY;
  }
  qp_p->destroy_cbk = cbk_func;
  qp_p->destroy_cbk_private_data = private_data;
  
  VIP_array_find_release(qpm_hndl->qpm_array, qp_hndl);
  return VIP_OK;
}
 
VIP_ret_t QPM_clear_destroy_qp_cbk(
  /*IN*/ QPM_hndl_t               qpm_hndl,
  /*IN*/ QPM_qp_hndl_t            qp_hndl
)
{
  QPM_qp_prop_t *qp_p;
  VIP_ret_t ret;

  ret = VIP_array_find_hold(qpm_hndl->qpm_array, qp_hndl, (VIP_array_obj_t *)&qp_p);
  if (ret == VIP_EINVAL_HNDL) {
    return VIP_EINVAL_QP_HNDL;
  }
  
  qp_p->destroy_cbk = NULL;
  qp_p->destroy_cbk_private_data = NULL;
  
  VIP_array_find_release(qpm_hndl->qpm_array, qp_hndl);
  return VIP_OK;
}

VIP_ret_t QPM_get_num_qps(QPM_hndl_t qpm_hndl, u_int32_t *num_qps)
{
  /* check attributes */
  if ( qpm_hndl == NULL || qpm_hndl->qpm_array == NULL) {
    return VIP_EINVAL_QPM_HNDL;
  }
  
  if (num_qps == NULL) {
      return VIP_EINVAL_PARAM;
  }
  *num_qps = VIP_array_get_num_of_objects(qpm_hndl->qpm_array);
  return VIP_OK;
}


VIP_ret_t QPM_get_qp_list(QPM_hndl_t qpm, struct qp_item_st *qp_item_p)
{
  VIP_common_ret_t rc;
  QPM_qp_hndl_t qp_hndl;
  QPM_qp_prop_t *qp_prop_p;
  qp_data_t *qpd, *head;
  unsigned int count;


  count = 0;
  head = NULL;
  for (
        rc = VIP_array_get_first_handle(qpm->qpm_array, &qp_hndl, (VIP_array_obj_t *)&qp_prop_p);
        rc == VIP_OK;
        rc = VIP_array_get_next_handle(qpm->qpm_array, &qp_hndl, (VIP_array_obj_t *)&qp_prop_p)
      ) {
    rc = VIP_array_find_hold(qpm->qpm_array, qp_hndl, (VIP_array_obj_t *)&qp_prop_p);
    if ( rc != VIP_OK ) {
      return VIP_ERROR;
    }
    qpd = TMALLOC(qp_data_t);
    if ( !qpd ) {
      VIP_array_find_release(qpm->qpm_array, qp_hndl);
      while ( head ) {
        qpd = head->next;
        FREE(head);
        head = qpd;
      }
      return VIP_EAGAIN;
    }

    qpd->qpn = qp_prop_p->qp_num;
    qpd->qp_state = qp_prop_p->curr_state;

    rc = CQM_get_hh_cq_num(qpm->cqm_hndl, (CQM_cq_hndl_t)qp_prop_p->rq_cq_hndl, &qpd->rq_cq_id);
    if ( rc != VIP_OK ) {
      MTL_ERROR1(MT_FLFMT("%s: cq handle not found in CQ vip array"), __func__);
    }
    rc = CQM_get_hh_cq_num(qpm->cqm_hndl, (CQM_cq_hndl_t)qp_prop_p->sq_cq_hndl, &qpd->sq_cq_id);
    if ( rc != VIP_OK ) {
      MTL_ERROR1(MT_FLFMT("%s: cq handle not found in CQ vip array"), __func__);
    }
    qpd->port = qp_prop_p->port;
    qpd->ts_type = qp_prop_p->ts_type;

    qpd->next = head;
    head = qpd;
    count++;
    VIP_array_find_release(qpm->qpm_array, qp_hndl);
  }

  qp_item_p->count = count;
  qp_item_p->qp_list = head;
  return VIP_OK;
}

#if defined(MT_SUSPEND_QP)
VIP_ret_t
QPM_suspend_qp (VIP_RSCT_t usr_ctx,QPM_hndl_t qpm_hndl, QPM_qp_hndl_t qp_hndl, MT_bool suspend_flag)
{
    VIP_ret_t ret;
    QPM_qp_prop_t *qp_prop_p;
    QPM_t *qpm_p;

    if (qpm_hndl == NULL || qpm_hndl->qpm_array == NULL) {
      return VIP_EINVAL_QP_HNDL;
    }
    qpm_p = qpm_hndl;

    ret = VIP_array_find_hold(qpm_hndl->qpm_array, qp_hndl, (VIP_array_obj_t *)&qp_prop_p);
    if ( ret!=VIP_OK ) {
      PVRET("VIP_array_find_hold",ret);
      return VIP_EINVAL_QP_HNDL;
    }

    ret = VIP_RSCT_check_usr_ctx(usr_ctx,&qp_prop_p->rsc_ctx);
    if (ret != VIP_OK) {
      VIP_array_find_release(qpm_hndl->qpm_array, qp_hndl);
      MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. qp handle=0x%x (%s)"),__func__,qp_hndl,VAPI_strerror_sym(ret));
      return ret;
    } 


    if (suspend_flag == TRUE) { QP_MODIFY_BUSY_ON(qp_prop_p) }

    ret = HH_suspend_qp (qpm_p->hh_hndl, qp_prop_p->qp_num, suspend_flag);
    if (ret != HH_OK) {
      if (suspend_flag == TRUE) { QP_MODIFY_BUSY_OFF(qp_prop_p) }
      VIP_array_find_release(qpm_hndl->qpm_array, qp_hndl);
      PVRET("HH_suspend_qp",ret);
      return (ret >= VIP_COMMON_ERROR_MIN ? VIP_EGEN : ret);
    }

    if (suspend_flag == FALSE) { QP_MODIFY_BUSY_OFF(qp_prop_p) }
    VIP_array_find_release(qpm_hndl->qpm_array, qp_hndl);
    return VIP_OK;
fail_busy:
    MTL_DEBUG1(MT_FLFMT("%s: Busy flag already on. suspend_flag=%s, qp_num=0x%x"),
               __func__, (suspend_flag==TRUE)?"TRUE":"FALSE", qp_prop_p->qp_num);
    VIP_array_find_release(qpm_hndl->qpm_array, qp_hndl);
    return ret;

}
#endif

