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


#include <mtl_types.h>
#include <mtl_common.h>

#include <mmu_description.h>
#include <mmu.h>
#include <mosal.h>
#include <vip_hash.h>
#include <vip_array.h>
#include <hh.h>
#include <hobkl.h>
#include <vapi_common.h>
#include <mt_bufpool.h>

#define MM_ROOT_ID 0

#define MAX_FMR_UNMAP_LKEY_AT_ONCE 255/* Maximum FMRs to unmap in one call to HH (lkey_arr size) */
#define FMR_UNMAP_CONCURRENCY 16   /* Number of buffers in lkey_arr buffer pool */


/*
static void MM_FREE_FUNC(void* ptr)
{
  FREE(ptr);
}
*/



#ifdef MM_DEBUG_UL
  #include <sys/mman.h>
#else


#endif /*MM_DEBUG_UL*/

#ifdef MM_DEBUG_KEY_POOL
u_int32_t key_pool;
#endif /*DEBUG*/

/* Add more checks */
#define BAD_MMU_HNDL(hndl) (hndl==NULL)

/*end of dummy section*/

#define MMU_BUSY_ON(mm_p,t)     MOSAL_spinlock_irq_lock(&(mm_p)->t##_lock);    \
                                if ((mm_p)->t##_busy) {                        \
                                    MOSAL_spinlock_unlock(&(mm_p)->t##_lock);  \
                                    ret= VIP_EAGAIN;                           \
                                 }else {                                       \
                                   (mm_p)->t##_busy = TRUE;                    \
                                   ret= VIP_OK;                                \
                                 }                                             \
                                 MOSAL_spinlock_unlock(&(mm_p)->t##_lock); 
                                   
#define MMU_BUSY_OFF(mm_p,t)    MOSAL_spinlock_irq_lock(&(mm_p)->t##_lock);    \
                                (mm_p)->t##_busy = FALSE;                      \
                                MOSAL_spinlock_unlock(&(mm_p)->t##_lock);
                                

static inline MOSAL_mem_perm_t vip2mosal_perm(VAPI_mrw_acl_t acl)
{
  MOSAL_mem_perm_t mosal_perm;
  mosal_perm = MOSAL_PERM_READ | ((acl&VAPI_EN_LOCAL_WRITE) ? MOSAL_PERM_WRITE : 0);
  MTL_TRACE8(MT_FLFMT("%s: vip acl=0x%02x, mosal_perm=0x%08x"), __func__, acl, mosal_perm);
  return mosal_perm;
}


static void VIP_free_mr(void* p)
{
    if (((MM_mro*)p)->pub_props.type != VAPI_MPR) {
            MOSAL_iobuf_deregister(((MM_mro*)p)->iobuf);
    }
    FREE(p); 
    MTL_ERROR1(MT_FLFMT("MM delete:found unreleased mr"));        
}


static void VIP_free_mw(void* p)
{
    MTL_ERROR1(MT_FLFMT("MM delete:found unreleased mw"));        
    FREE(p); 
}


static void VIP_free_fmr(void* p)
{
    MTL_ERROR1(MT_FLFMT("MM delete:found unreleased fmr"));        
    FREE(p); 
}



/************************Static functions declaration *************/
/*******************************************************************
 * FUNCTION:
 *         MM_bld_hh_mr
 * DESCRIPTION: 
 *        locks & translates
 * ARGUMENTS: 
 *         prot_ctx
 *         start: 
 *         sz 
 *         hh_mr_p(OUT): pointer to hh_struct
 *         iobuf_p(out) pointer to returned iobuf object
 *         acl(in) access control list for the region
 *          
 * RETURNS: 
 *         VIP_OK 
 *         VIP_EAGAIN No resourses
 *         VIP_EINVAL_ADDR: illegal virtual addresses range
 *        
 *******************************************************************/ 
static VIP_ret_t MM_bld_hh_mr(MOSAL_prot_ctx_t prot_ctx,VAPI_virt_addr_t start,
                              VAPI_size_t sz,HH_mr_t *hh_mr_p, MOSAL_iobuf_t *iobuf_p, VAPI_mrw_acl_t acl);
/*******************************************************************
 * FUNCTION:
 *         MM_bld_hh_pmr
 * DESCRIPTION: 
 *        fills the props of hh_mr struct for physicalMR registration: translates the pages to physical
 * ARGUMENTS: 
 *         mm_hdnl(IN):   Memory Managment handle. 
 *         mm_mro_p(IN/OUT):  Memory Region object
 *         hh_mr_p(OUT): pointer to hh_struct
 *          
 * RETURNS: 
 *         VIP_OK 
 *         VIP_EAGAIN No resourses
 *        
 *******************************************************************/ 
static VIP_ret_t MM_bld_hh_pmr(MM_hndl_t  mm_hndl,VAPI_mrw_t* mrw_p,HH_mr_t *hh_mr_p);

/*******************************************************************
 * FUNCTION:
 *         MM_mr_get_keys
 * DESCRIPTION: 
 *        Calls HAL and retrieves Local and Remote Keys
 * ARGUMENTS: 
 *         mm_hdnl(IN):   Memory Managment handle. 
 *         mm_mro_p(IN/OUT):  Memory Region object
 *         mr_props_p: pointer to hh struct
 *          
 * RETURNS: 
 *         VIP_OK 
 *        
 *******************************************************************/ 
static VIP_ret_t MM_mr_get_keys(MM_hndl_t  mm_hndl, MM_mro *mm_mro_p,HH_mr_t* mr_props_p);
/*******************************************************************
 * FUNCTION:
 *         MM_smr_get_keys
 * DESCRIPTION: 
 *        Calls HAL and retrieves Local and Remote Keys
 * ARGUMENTS: 
 *         mm_hdnl(IN):   Memory Managment handle. 
 *         mm_mro_p(IN/OUT):  Memory Region object
 *         smr_props_p: pointer to hh struct
 *          
 * RETURNS: 
 *         VIP_OK 
 *        
 *******************************************************************/ 
 static VIP_ret_t MM_smr_get_keys(MM_hndl_t  mm_hndl, MM_mro *mm_mro_p,HH_smr_t* smr_props_p);

/*Free the key got from HCAHAL*/
#define MM_mr_free_keys(hh_hndl, l_key)    HH_deregister_mr( (hh_hndl), (l_key) )

/*******************************************************************
 * FUNCTION:
 *         MM_update_mr_tables
 * DESCRIPTION: 
 *        Updates tables of MMU  and registers MR at Protection Domain Maneger
 * ARGUMENTS: 
 *         mm_hdnl(IN):   Memory Managment handle. 
 *         mm_mro_p(IN):  Memory Region object
 *                  
 * RETURNS: 
 *         VIP_OK 
 *         VIP_EAGAIN No resourses
 *         VIP_EINVAL_PD_HNDL: invalid PD handle.
 *         VIP_EPERM: has no permission to control this PD.
 *         VIP_BUSY: Object already has PD associated with it
 *        
 *******************************************************************/
static VIP_ret_t MM_update_mr_tables(VIP_RSCT_t usr_ctx,MM_hndl_t mm_hndl, MM_mro *mm_mro_p, MM_mrw_hndl_t* mr_hndl_p); 

static VIP_ret_t validate_same_pages(MM_mro* ori_p,MM_mro* new_p);

static inline void MM_fill_mr_props(MM_mro* mm_mro_p,MM_VAPI_mro_t* mr_prop_p);
/*******************************************************************
 * FUNCTION:
 *         MM_local_unmap_fmr
 * DESCRIPTION: 
 *        unmaps a given mr
 * ARGUMENTS: 
 *         mm_hdnl(IN):   Memory Managment handle. 
 *         mm_fmr_p(IN):  Memory Region object
 *         have_fatal(IN): System is in midst of coping with a fatal error
 *                  
 * RETURNS: 
 *         VIP_OK 
 *         VIP_EINVAL_ADDR 
 *******************************************************************/
VIP_ret_t MM_local_unmap_fmr(MM_hndl_t mm_hndl ,EVAPI_fmr_hndl_t fmr_hndl, MT_bool have_fatal);    

static inline VIP_ret_t make_iobuf(MOSAL_prot_ctx_t prot_ctx,VAPI_virt_addr_t start,
                                    VAPI_size_t sz, VAPI_mrw_acl_t acl, MOSAL_iobuf_t *iobuf_p)
{
    call_result_t rr = MT_OK;
    MOSAL_mem_perm_t mosal_perm;

    mosal_perm = vip2mosal_perm(acl);
  rr = MOSAL_iobuf_register(start, sz, prot_ctx, mosal_perm, iobuf_p);
    if ( rr != MT_OK ) {
      MTL_ERROR1(MT_FLFMT("%s: MOSAL_iobuf_register failed: va="U64_FMT "size="U64_FMT),__func__, start, sz);
      switch (rr) {
          case MT_ENOMEM:
            return VIP_ENOMEM; /* Passed mlock limit (half of physical memory per process) or invalid address */
          case MT_EPERM:
            return VIP_EINVAL_ACL;
          case MT_EINVAL:
          default:
            return VIP_EAGAIN;
      }
    }
    MTL_DEBUG3("Lock is ok\n");
    return VIP_OK;

}
/************************ end of static functions declaration *****************/

/******************************************************************************/

/*                         Global Functions Implementation */
/*
 *   MM_new
 */
VIP_ret_t MM_new(HOBKL_hndl_t hob_hndl, VIP_delay_unlock_t delay_unlocks, MM_hndl_t *mm_hndl_p)
{
  VIP_ret_t rc;
  call_result_t mt_rc;
  VAPI_hca_cap_t hca_cap;
  MM_hndl_t mm_hndl;

  MTL_DEBUG1("inside MM_new \n");
  MTL_DEBUG3("CALLED " "%s \n", __func__);
  
  /* get HCA capabilities */
  rc = HOBKL_query_cap(hob_hndl, &hca_cap);
  if ( rc != VIP_OK ) return rc;

  /* allocate MMU handle */
  mm_hndl = (MM_hndl_t)MM_ALLOC(sizeof(struct MMU_t));
  if ( !mm_hndl ) return VIP_EAGAIN;
  memset(mm_hndl, 0, sizeof(struct MMU_t));
  *mm_hndl_p = mm_hndl;

  /* save HOB handle    */
  mm_hndl->hob_hndl = hob_hndl; 

  /* save delay_unlock handle */
  mm_hndl->delay_unlocks = delay_unlocks;

  mm_hndl->max_map_per_fmr = hca_cap.max_num_map_per_fmr;
    
  /* Create tables */
  /*rc = VIP_hash_create((hca_cap.max_num_mr>>2)>>CREATE_SHIFT, &mm_hndl->mr_by_lkey);
  if ( rc != VIP_OK ) goto mr_l_key_err;
  
  rc = VIP_hash_create((hca_cap.max_num_mr>>2)>>CREATE_SHIFT, &mm_hndl->mr_by_rkey);
  if ( rc != VIP_OK ) goto mr_r_key_err;
  */
  rc = VIP_hash_create_maxsize((hca_cap.max_mw_num>>2)>>CREATE_SHIFT, hca_cap.max_mw_num, &mm_hndl->mw_by_rkey);
  if ( rc != VIP_OK ) goto mw_r_key_err;

  rc = VIP_array_create_maxsize(hca_cap.max_num_mr>>CREATE_SHIFT, hca_cap.max_num_mr, &(mm_hndl->mr_by_hndl));
  if ( rc != VIP_OK ) goto mr_arr_err;

  rc = VIP_array_create_maxsize(hca_cap.max_mw_num>>CREATE_SHIFT, hca_cap.max_mw_num, &(mm_hndl->mw_by_hndl));
  if ( rc != VIP_OK ) goto mw_arr_err;

  rc = VIP_array_create_maxsize(hca_cap.max_num_fmr>>CREATE_SHIFT, hca_cap.max_num_fmr,&(mm_hndl->fmr_by_hndl));
  if ( rc != VIP_OK ) goto fmr_arr_err;

  mt_rc = MT_bufpool_create(sizeof(VAPI_lkey_t) * MAX_FMR_UNMAP_LKEY_AT_ONCE , 2, 
                            FMR_UNMAP_CONCURRENCY, 0, &(mm_hndl->lkey_array_bufpool));
  if ( mt_rc != MT_OK ) {
    MTL_ERROR2(MT_FLFMT("%s: Failed MT_bufpool_create (%s)"), __func__, mtl_strerror_sym(mt_rc));
    rc= VIP_EAGAIN;
    goto bufpool_err;
  }


#ifdef MM_DEBUG_KEY_POOL
  key_pool = 0;
#endif /*DEBUG*/

  return VIP_OK;


  /*error handling*/
  bufpool_err: VIP_array_destroy(mm_hndl->fmr_by_hndl,NULL);
  fmr_arr_err: VIP_array_destroy(mm_hndl->mw_by_hndl,NULL);
  mw_arr_err: VIP_array_destroy(mm_hndl->mr_by_hndl,NULL);
  mr_arr_err: VIP_hash_destroy(mm_hndl->mw_by_rkey);
  mw_r_key_err:	/*VIP_hash_destroy(mm_hndl->mr_by_rkey);*/
  /*mr_r_key_err: VIP_hash_destroy(mm_hndl->mr_by_lkey);*/
  /*mr_l_key_err: */
    MM_FREE(mm_hndl);
    return rc;
}


/*
 *    MM_delete
 */
VIP_ret_t MM_delete(MM_hndl_t mm)
{
 // VIP_array_handle_t hndl;
 // MM_mro* mro_p;
 // MM_mw*  mw_p;   
 // MM_fmr* fmr_p;
//  VIP_ret_t ret;

  if ( BAD_MMU_HNDL(mm) ) return VIP_EINVAL_MMU_HNDL;

  MT_bufpool_destroy(mm->lkey_array_bufpool);
  VIP_array_destroy(mm->mr_by_hndl,VIP_free_mr);
  VIP_array_destroy(mm->fmr_by_hndl,VIP_free_fmr);
  VIP_array_destroy(mm->mw_by_hndl,VIP_free_mw);

  /* Destroy hashes */
 /* VIP_hash_destroy(mm_hndl->mr_by_lkey);
  VIP_hash_destroy(mm_hndl->mr_by_rkey);*/
  VIP_hash_destroy(mm->mw_by_rkey);

/* Destroy MMU instance */
  MM_FREE(mm);
  return VIP_OK;
}




/*                              Memory Region related functions */


/*************************************************
*	MM_create_mw								 *
*************************************************/

VIP_ret_t MM_create_mw(VIP_RSCT_t usr_ctx,MM_hndl_t mm_h,PDM_pd_hndl_t pdm_pd_h,IB_rkey_t *r_key_p)
{
	HH_ret_t 		hhrc;
	VIP_ret_t 		rc, rc1;
	MM_mrw_hndl_t	mw_h;
	PDM_hndl_t		pdm_h = NULL;
	HH_pd_hndl_t	hh_pd_h;
	MM_mw 			*mm_mw_p;
	VIP_RSCT_rschndl_t r_h;
	
		
	/* if invalid MMU handle */
	if ( BAD_MMU_HNDL(mm_h) ) { 
		rc = VIP_EINVAL_MMU_HNDL;
		MTL_ERROR1("bad mmu handle \n");
    goto create_mw_exit;
	}
		
	/* TD: check return code of this call*/
	pdm_h = HOBKL_get_pdm(mm_h->hob_hndl);
	if( pdm_h == NULL ) {
		rc = VIP_EINVAL_PDM_HNDL;
		MTL_ERROR1("bad pdm handle \n");
    goto create_mw_exit;
	}

	/* register on PDM */
	rc = PDM_add_object_to_pd(usr_ctx, pdm_h, pdm_pd_h, NULL, &hh_pd_h);
	if ( rc != VIP_OK ) {
		MTL_ERROR1(MT_FLFMT("%s: PDM_add_object_to_pd failed - %s"), __func__, VAPI_strerror_sym(rc)); 
		goto create_mw_exit;
	}
	
	hhrc = HH_alloc_mw(HOBKL_get_hh_hndl(mm_h->hob_hndl),hh_pd_h,r_key_p);
	if (hhrc != HH_OK)
	{
		rc = (hhrc < VAPI_ERROR_MAX ? hhrc : VAPI_EGEN);
		MTL_ERROR1(MT_FLFMT("%s: HH_alloc_mw() failed (%d:%s)"),__func__, rc, VAPI_strerror_sym(rc));
        goto create_mw_get_pd_id_err;
	}
	
    MTL_DEBUG2(MT_FLFMT("after HH alloc mw \n"));
	/* allocate the instance of memory region object */
	mm_mw_p = (MM_mw *)MM_ALLOC(sizeof(MM_mw));
	if ( mm_mw_p == NULL ) 
	{
		rc = VIP_EAGAIN;
		goto create_mw_alloc_err;
	}
	
	mm_mw_p->init_key = *r_key_p;
	mm_mw_p->pd_h = pdm_pd_h;

	MTL_DEBUG1("before VIP array insert \n");
	
	rc = VIP_array_insert(mm_h->mw_by_hndl,mm_mw_p,&mw_h);
	if( rc != VIP_OK ) 
	{
		MTL_ERROR1(MT_FLFMT("%s: failed inserting mwh to array (%d:%s)"),__func__,rc,VIP_common_strerror_sym(rc));
        goto create_mw_array_err;
	}
	
	mm_mw_p->mm_mw_h = mw_h;

	rc = VIP_hash_insert(mm_h->mw_by_rkey,mm_mw_p->init_key,mw_h);
	if ( rc != VIP_OK ) {
		MTL_ERROR1(MT_FLFMT("%s: ERROR: VIP_hash_insert() (%d:%s)"),__func__,rc,VIP_common_strerror_sym(rc)); 
		goto create_mw_hash_err;
	}
	
	r_h.rsc_mw_hndl = *r_key_p;
    VIP_RSCT_register_rsc(usr_ctx,&mm_mw_p->rsc_ctx,VIP_RSCT_MW,r_h);
    MT_RETURN(VIP_OK);

  create_mw_hash_err:
	VIP_array_erase(mm_h->mw_by_hndl,mw_h,NULL);

create_mw_array_err:
	// TD: check return code for this call:
	MM_FREE(mm_mw_p);

create_mw_alloc_err:
	hhrc = HH_free_mw(HOBKL_get_hh_hndl(mm_h->hob_hndl),*r_key_p);
	if (hhrc != HH_OK)
	{
		MTL_ERROR1(MT_FLFMT("%s: HH_free_mw failed (%d:%s)"),__func__, rc, HH_strerror_sym(rc));
	}

create_mw_get_pd_id_err:
  if ( (rc1=PDM_rm_object_from_pd(pdm_h, pdm_pd_h)) != VIP_OK ) {
    MTL_ERROR1(MT_FLFMT("%s: PDM_rm_object_from_pd failed (%d:%s)"), __func__, rc1,VIP_common_strerror_sym(rc1));
  }

create_mw_exit:	
    MTL_ERROR1(MT_FLFMT("%s: MM_create_mw failed (%d:%s)"),__func__, rc, VAPI_strerror_sym(rc));
	MT_RETURN(rc);
}

/*
 *    MM_create_mr
 */
VIP_ret_t MM_create_mr(VIP_RSCT_t usr_ctx,MM_hndl_t mm_hndl,
                       VAPI_mrw_t *mrw_prop_p, PDM_pd_hndl_t pd_hndl,
                       MM_mrw_hndl_t *mr_hndl_p, MM_VAPI_mro_t *mr_prop_p)
{
  VIP_ret_t rc = VIP_OK, rc1;
  MM_mro *mm_mro_p;
  HH_hca_hndl_t hh_hndl;
  HH_mr_t hh_mr;
  VIP_RSCT_rschndl_t r_h;
  MOSAL_prot_ctx_t prot_ctx;
  HH_pd_hndl_t  pd_id;
  PDM_hndl_t pdm = HOBKL_get_pdm(mm_hndl->hob_hndl);
 
  MOSAL_iobuf_t iobuf;

  if ( BAD_MMU_HNDL(mm_hndl) ) return VIP_EINVAL_MMU_HNDL;
  hh_hndl = HOBKL_get_hh_hndl(mm_hndl->hob_hndl);

  MTL_DEBUG1("[create mr]: acl:0x%x type:%d start:"U64_FMT" size:"U64_FMT" pd hndl:0x%x\n",
             mrw_prop_p->acl,mrw_prop_p->type,
             mrw_prop_p->start,mrw_prop_p->size,pd_hndl);


  if ( (rc=CHECK_ACL(mrw_prop_p->acl)) != VIP_OK ) return rc;

  if ( mrw_prop_p->size == 0 ) return VIP_EINVAL_SIZE;

  if ( (mrw_prop_p->type!=VAPI_MR) && (mrw_prop_p->type!=VAPI_MPR) ) return VIP_EINVAL_MR_TYPE;


  /* Register on PDM */
  rc = PDM_add_object_to_pd(usr_ctx, pdm, pd_hndl, &prot_ctx, &pd_id);
  if ( rc != VIP_OK ) return rc;

  /* Allocate the instance of Memory Region Object */
  mm_mro_p = (MM_mro *)MM_ALLOC(sizeof(MM_mro));
  if ( mm_mro_p == NULL ) {
    if ( (rc1=PDM_rm_object_from_pd(pdm, pd_hndl)) != VIP_OK ) {
      MTL_ERROR1(MT_FLFMT("%s: PDM_rm_object_from_pd failed (%d:%s)"), __func__, rc1,VAPI_strerror_sym(rc1));
    }
    return VIP_EAGAIN;
  }

  /*TOBD:
  Implement requesting of HAL about real start and real end addresses of the region
*/
  mm_mro_p->pub_props.re_local_end = mrw_prop_p->start + mrw_prop_p->size - 1;
  mm_mro_p->pub_props.re_local_start = mrw_prop_p->start;
  mm_mro_p->pub_props.re_remote_end = mm_mro_p->pub_props.re_local_end;
  mm_mro_p->pub_props.re_remote_start = mm_mro_p->pub_props.re_local_start;
  mm_mro_p->pub_props.acl = mrw_prop_p->acl;
  mm_mro_p->pub_props.pd_hndl = pd_hndl;
  mm_mro_p->pub_props.type = mrw_prop_p->type;
  mm_mro_p->start = mm_mro_p->pub_props.re_local_start;
  mm_mro_p->size = mrw_prop_p->size;
    /*init */
  MOSAL_spinlock_init(&mm_mro_p->mr_lock);
  mm_mro_p->mr_busy = FALSE;

  
  
  /* build HH */
  hh_mr.tpt.tpt.buf_lst.phys_buf_lst = NULL;
  hh_mr.tpt.tpt.page_lst.phys_page_lst = NULL;
  hh_mr.acl = mm_mro_p->pub_props.acl;
  hh_mr.size  = mrw_prop_p->size;
  hh_mr.start = mm_mro_p->pub_props.re_local_start;
  hh_mr.pd = pd_id;
    
  switch (mrw_prop_p->type) {
    case VAPI_MR:
      rc = MM_bld_hh_mr(prot_ctx,mrw_prop_p->start,mrw_prop_p->size,&hh_mr, &iobuf, mrw_prop_p->acl);
      if ( rc != VIP_OK ) {
        MTL_ERROR1("[create_mr] MM_bld_hh_mr failed (%d:%s)\n", rc, VAPI_strerror_sym(rc));
        goto lock_err;
      }
      mm_mro_p->iobuf = iobuf;
      break;

    case VAPI_MPR:
      rc = MM_bld_hh_pmr(mm_hndl,mrw_prop_p,&hh_mr);
      if (rc != VIP_OK) {
        MTL_ERROR1("[create_mr] MM_bld_hh_pmr failed (%d:%s)\n", rc, VAPI_strerror_sym(rc));
        goto lock_err;
      }
      mm_mro_p->iobuf = NULL;
      break;

      /* shouldn't be hapenning.. */
    default:
      rc = VIP_EINVAL_MR_TYPE; 
      goto lock_err;
  }
  
  /* Get Rkey and Lkey  - call to thh_mrwm functions */
  rc = MM_mr_get_keys(mm_hndl, mm_mro_p, &hh_mr);
  if ( rc != VIP_OK ) {
    MTL_ERROR1("[MM_create_mr]:MM_mr_get_keys failed\n");
    goto get_keys_err;
  }
  MTL_DEBUG3("[create_mr]:Keys are received. R_key:0x%x L_key:0x%x\n", mm_mro_p->pub_props.r_key, mm_mro_p->pub_props.l_key);

  /* Update tables of MMU  and register on Protection Domain */
  rc = MM_update_mr_tables(usr_ctx,mm_hndl, mm_mro_p, mr_hndl_p);
  if ( rc != VIP_OK ) {
    goto update_tables_err;
  }

  if (mrw_prop_p->type == VAPI_MPR) { 
    MM_FREE(hh_mr.tpt.tpt.buf_lst.phys_buf_lst);
    MM_FREE(hh_mr.tpt.tpt.buf_lst.buf_sz_lst);
  }

  r_h.rsc_mr_hndl = *mr_hndl_p;
  VIP_RSCT_register_rsc(usr_ctx,&mm_mro_p->rsc_ctx,VIP_RSCT_MR,r_h);
  
  MM_fill_mr_props(mm_mro_p,mr_prop_p);
  MTL_DEBUG1(MT_FLFMT("%s (pid="MT_PID_FMT"): start="U64_FMT", size="U64_FMT", mr handle=0x%x"),
              __func__, MOSAL_getpid(), mm_mro_p->start, mm_mro_p->size, *mr_hndl_p);
  MT_RETURN(VIP_OK);

/*error handling*/
update_tables_err:
  MTL_DEBUG3("[MM_create_mr]:Was an error in updating VIP local tables (%d:%s)\n",
             rc, VAPI_strerror_sym(rc)); 
  MM_mr_free_keys(hh_hndl, mm_mro_p->pub_props.l_key);

get_keys_err:  
  if (mrw_prop_p->type == VAPI_MR) {
    MOSAL_iobuf_deregister(mm_mro_p->iobuf);
  }else {
      MM_FREE(hh_mr.tpt.tpt.buf_lst.phys_buf_lst);
      MM_FREE(hh_mr.tpt.tpt.buf_lst.buf_sz_lst);
  }

lock_err:
  if ( (rc1=PDM_rm_object_from_pd(pdm, pd_hndl)) != VIP_OK ) {
    MTL_ERROR1(MT_FLFMT("%s: PDM_rm_object_from_pd failed (%d:%s)"), __func__, rc1,VAPI_strerror_sym(rc1));
  }
  MM_FREE(mm_mro_p);
  MT_RETURN(rc);
}


/*
 *    MM_create_smr
 */
VIP_ret_t MM_create_smr(VIP_RSCT_t usr_ctx,MM_hndl_t mm_hndl, MM_mrw_hndl_t mr_orig_hndl, VAPI_mrw_t* mrw_prop_p, 
                        PDM_pd_hndl_t pd_hndl,MM_mrw_hndl_t* mr_hndl_p, MM_VAPI_mro_t* mr_prop_p)
{
  VIP_ret_t rs, rs1;
  MM_mro *mm_mro_p,*ori_p;
  HH_hca_hndl_t hh_hndl;
  HH_smr_t hh_smr;
  VIP_array_obj_t val;
  VIP_RSCT_rschndl_t r_h;
  MOSAL_prot_ctx_t prot_ctx;
  MOSAL_iobuf_props_t iobuf_props;
  call_result_t rc;
  HH_pd_hndl_t pd_id;
  PDM_hndl_t pdm;
  
  MTL_DEBUG1("[create_smr]: acl:0x%x type:%d start:"U64_FMT" pd hndl:0x%x\n",
             mrw_prop_p->acl,mrw_prop_p->type,mrw_prop_p->start,pd_hndl);

  /* if invalid MMU handle */
  if ( BAD_MMU_HNDL(mm_hndl) ) {
      MTL_ERROR1(MT_FLFMT("%s: Invalid MMU handle 0x%p"), __func__, mm_hndl);
      return VIP_EINVAL_MMU_HNDL;
  }
  hh_hndl = HOBKL_get_hh_hndl(mm_hndl->hob_hndl);
  pdm = HOBKL_get_pdm(mm_hndl->hob_hndl);

  /* Check access control list */
  if ( (rs=CHECK_ACL(mrw_prop_p->acl) ) != VIP_OK ) {
      MTL_ERROR1(MT_FLFMT("%s: Invalid ACL 0x%x"), __func__,mrw_prop_p->acl);
      return rs;
  }

  /* Register on PDM */
  rs = PDM_add_object_to_pd(usr_ctx, pdm, pd_hndl, &prot_ctx, &pd_id);
  if ( rs != VIP_OK ) {
      MTL_ERROR1(MT_FLFMT("%s: PDM_add_object_to_pd failed (%d:%s)"),
                  __func__,rs,VAPI_strerror_sym(rs));
      return rs;
  }

  /* Allocate the instance of Memory Region Object */
  mm_mro_p = (MM_mro*)MM_ALLOC(sizeof(MM_mro));
  if ( mm_mro_p == NULL ) {
    MTL_ERROR1(MT_FLFMT("%s: MALLOC failure"), __func__);
    rs = VIP_EAGAIN;
    goto create_smr_alloc_err;
  }


  /* query the original mr */ 
  MTL_DEBUG1("smr: before query mr \n");
  rs = VIP_array_find_hold(mm_hndl->mr_by_hndl,mr_orig_hndl,&val);
  if ( rs != VIP_OK ) {
    MTL_ERROR1(MT_FLFMT("%s: could not find mr handle 0x%x"), __func__, mr_orig_hndl);
    rs = VIP_EINVAL_MR_HNDL;
    goto create_smr_find_err;
  }
  ori_p = (MM_mro *)val;
  
  if ( ori_p->pub_props.type == VAPI_MPR ) {
    MTL_ERROR1(MT_FLFMT("%s: Original MR (0x%x) is physical MR"), __func__, mr_orig_hndl);
    rs = VIP_EINVAL_MR_HNDL;
    goto lock_err;
  }

  
  mm_mro_p->start = mrw_prop_p->start;
  mm_mro_p->size = ori_p->pub_props.re_local_end - ori_p->pub_props.re_local_start + 1;
  mm_mro_p->pub_props.re_local_start = mrw_prop_p->start;
  mm_mro_p->pub_props.re_local_end = mm_mro_p->start+ mm_mro_p->size-1;
  mm_mro_p->pub_props.re_remote_start = mm_mro_p->pub_props.re_local_start;
  mm_mro_p->pub_props.re_remote_end = mm_mro_p->pub_props.re_local_end;
  
  mm_mro_p->pub_props.acl = mrw_prop_p->acl;
  mm_mro_p->pub_props.pd_hndl = pd_hndl;
  mm_mro_p->pub_props.type = VAPI_MSHAR;
    
  rs= make_iobuf(prot_ctx,mm_mro_p->start,mm_mro_p->size,mm_mro_p->pub_props.acl,&mm_mro_p->iobuf);
  if (rs != VIP_OK) {
    MTL_ERROR1(MT_FLFMT("%s: make_iobuf failed (%d:%s)"), __func__, rs,VAPI_strerror_sym(rs));
    goto lock_err;
  }
  

  rc = MOSAL_iobuf_get_props(ori_p->iobuf, &iobuf_props);
  if ( rc != MT_OK ) {
    MTL_ERROR1(MT_FLFMT("%s: MOSAL_iobuf_get_props failed (%d:%s)"), __func__, rs,VAPI_strerror_sym(rs));
    rs = VIP_EFATAL;
    goto get_keys_err;
  }
  else {
    VAPI_virt_addr_t mask = ((VAPI_virt_addr_t)1<<iobuf_props.page_shift)-1;
    /* we compare the offset of the start address from the first page while
       assuming the both buffer have the same page size. If that is not the case
       the call to MOSAL_iobuf_cmp_tpt will fail (called from validate_same_pages) */
    if ( (mask&ori_p->start) != (mask&mm_mro_p->start) ) {
      MTL_ERROR1(MT_FLFMT("%s: start address offsets not equal"), __func__);
      rs = VIP_EINVAL_ADDR;
      goto get_keys_err;
    }
  }
  rs = validate_same_pages(ori_p,mm_mro_p);
  if (rs != VIP_OK) {
     goto get_keys_err;
  }

  MTL_DEBUG3("[MM_create_smr]Lock is ok\n");
  hh_smr.acl =  mm_mro_p->pub_props.acl;
  hh_smr.lkey = ori_p->pub_props.l_key;
  hh_smr.start = mm_mro_p->pub_props.re_local_start;
  hh_smr.pd = pd_id;

 /* Get Rkey and Lkey  - call to thh_mrwm functions */
  rs = MM_smr_get_keys(mm_hndl, mm_mro_p,&hh_smr);
  if ( rs != VIP_OK ) {
    MTL_ERROR1("[MM_create_smr]:MM_smr_get_keys failed (%d:%s)\n", rs,VAPI_strerror_sym(rs));
    goto get_keys_err;
  }
  MTL_DEBUG3("[MM_create_smr]:Keys are received. R_key:%d L_key:%d\n", mm_mro_p->pub_props.r_key, mm_mro_p->pub_props.l_key);

  /* Update tables of MMU  and register on Protection Domain */
  rs = MM_update_mr_tables(usr_ctx,mm_hndl, mm_mro_p, mr_hndl_p);
  if ( rs != VIP_OK ) {
      MTL_ERROR1("[MM_create_smr]:MM_update_mr_tables failed (%d:%s)\n", rs,VAPI_strerror_sym(rs));
      goto update_tables_err;
  }

  MTL_DEBUG3("[MM_create_smr] smr handle = 0x%x\n", *mr_hndl_p);

  VIP_array_find_release(mm_hndl->mr_by_hndl,mr_orig_hndl);
  
  r_h.rsc_mr_hndl = *mr_hndl_p;
  VIP_RSCT_register_rsc(usr_ctx,&mm_mro_p->rsc_ctx,VIP_RSCT_MR,r_h);
  MM_fill_mr_props(mm_mro_p,mr_prop_p);
  
  return VIP_OK;

/*error handling*/

update_tables_err:
  MM_mr_free_keys(hh_hndl, mm_mro_p->pub_props.l_key);
get_keys_err:  
  MOSAL_iobuf_deregister(mm_mro_p->iobuf);
lock_err:
  VIP_array_find_release(mm_hndl->mr_by_hndl,mr_orig_hndl);  
create_smr_find_err:
  MM_FREE(mm_mro_p);
create_smr_alloc_err:
  if ( (rs1=PDM_rm_object_from_pd(pdm, pd_hndl)) != VIP_OK ) {
    MTL_ERROR1(MT_FLFMT("%s: PDM_rm_object_from_pd failed (%d:%s)"), __func__, rs1,VAPI_strerror_sym(rs1));
  }
  MT_RETURN(rs);
}

/*
 *    MM_alloc_fmr
 */
VIP_ret_t MM_alloc_fmr(VIP_RSCT_t usr_ctx,MM_hndl_t mm_hndl, EVAPI_fmr_t *fmr_prop_p, PDM_pd_hndl_t pd_hndl, 
                       MM_mrw_hndl_t *fmr_hndl_p)
{
  VIP_ret_t rc = VIP_OK, rc1;
  MM_fmr *mm_fmr_p;
  HH_hca_hndl_t hh_hndl = HOBKL_get_hh_hndl(mm_hndl->hob_hndl);
  PDM_hndl_t pdm_hndl = HOBKL_get_pdm(mm_hndl->hob_hndl);
  VIP_array_handle_t vip_hndl;
  HH_pd_hndl_t pd_num;
  HH_ret_t ret = HH_OK;
  VIP_RSCT_rschndl_t r_h;
  

  if ( BAD_MMU_HNDL(mm_hndl) ) return VIP_EINVAL_MMU_HNDL;

  
  
  /* Check access control list */
  if ( (rc=CHECK_ACL(fmr_prop_p->acl)) != VIP_OK ) return rc;
  if (fmr_prop_p->acl & VAPI_EN_MEMREG_BIND) {
    return VIP_EINVAL_ACL;
  }

  
  if ( fmr_prop_p->max_pages == 0 ) return VIP_EINVAL_SIZE;
  if (fmr_prop_p->max_outstanding_maps == 0) return VIP_EINVAL_SIZE;
  if (fmr_prop_p->max_outstanding_maps > mm_hndl->max_map_per_fmr)
      {
        MTL_ERROR1(MT_FLFMT("%s: given max outstanding maps exceeds device limit"),__func__); 
        return VIP_EINVAL_SIZE;
      }
  
  if ( fmr_prop_p->log2_page_sz < MOSAL_SYS_PAGE_SHIFT ) {
    MTL_ERROR1(MT_FLFMT("%s: given page shift is smaller than system page shift (size)"),__func__);
    return VIP_EINVAL_PARAM;
  }

  rc = PDM_add_object_to_pd(usr_ctx, pdm_hndl, pd_hndl, NULL, &pd_num);
  if ( rc != VIP_OK ) {
    return VAPI_EINVAL_PD_HNDL;
  }
  
  /* Allocate the instance of Memory Region Object */
  mm_fmr_p = (MM_fmr *)MM_ALLOC(sizeof(MM_fmr));
  if ( mm_fmr_p == NULL ) {
    rc = VIP_EAGAIN;
    goto pd_remove;
  }

  mm_fmr_p->max_outstanding_maps = fmr_prop_p->max_outstanding_maps;
  mm_fmr_p->acl = fmr_prop_p->acl;
  mm_fmr_p->pd_hndl = pd_hndl;
  mm_fmr_p->max_page_num = fmr_prop_p->max_pages;
  mm_fmr_p->log2_page_sz = fmr_prop_p->log2_page_sz; 
  
  /*init */
  MOSAL_spinlock_init(&mm_fmr_p->fmr_lock);

  MTL_DEBUG1(MT_FLFMT("after init fmr object"));
  
  ret = HH_alloc_fmr(hh_hndl,pd_num,mm_fmr_p->acl,mm_fmr_p->max_page_num,mm_fmr_p->log2_page_sz,&mm_fmr_p->lkey); 
  if (ret!= HH_OK) {
      MTL_ERROR1(MT_FLFMT("%s: HH_alloc_fmr failed, got %s "),__func__,HH_strerror_sym(ret));
      rc = ret;
      goto bad;
  }
  
  /* Update tables of MMU  and register on Protection Domain */
  rc = VIP_array_insert(mm_hndl->fmr_by_hndl, mm_fmr_p, &vip_hndl);
  if (rc != VIP_OK) {
      MTL_ERROR1(MT_FLFMT("%s: failed VIP_array_insert"), __func__);
      HH_free_fmr(hh_hndl,mm_fmr_p->lkey);
      goto bad;
  }
  *fmr_hndl_p= (MM_mrw_hndl_t)vip_hndl;

  r_h.rsc_mr_hndl = *fmr_hndl_p;
  VIP_RSCT_register_rsc(usr_ctx,&mm_fmr_p->rsc_ctx,VIP_RSCT_FMR,r_h);

  MT_RETURN(VIP_OK);

bad:
    MM_FREE(mm_fmr_p);
pd_remove:
    if ( (rc1=PDM_rm_object_from_pd(pdm_hndl, pd_hndl)!=VIP_OK) ) {
      MTL_ERROR1(MT_FLFMT("%s: PDM_rm_object_from_pd failed - %s"), __func__, VAPI_strerror_sym(rc1));
    }
    MT_RETURN(rc); 
}

/*
 *    MM_reregister_mr
 */
VIP_ret_t MM_reregister_mr(VIP_RSCT_t usr_ctx,MM_hndl_t mm_hndl,
                           VAPI_mr_hndl_t ori_mr_hndl,VAPI_mr_change_t change_type,
                           VAPI_mrw_t *mrw_prop_p,PDM_pd_hndl_t pd_hndl,
                           MM_mrw_hndl_t *mr_hndl_p,MM_VAPI_mro_t *mr_prop_p)
{
  VIP_ret_t rc = VIP_OK;
  VIP_ret_t rc2;
  VIP_array_obj_t val;
  MM_mro *mm_mro_p;
  PDM_hndl_t pdm;
  HH_mr_t hh_mr;
  HH_ret_t  rc_hh;  
  HH_hca_hndl_t hh_hndl = HOBKL_get_hh_hndl(mm_hndl->hob_hndl);
  MOSAL_prot_ctx_t prot_ctx;
  VAPI_lkey_t new_lkey;
  VAPI_rkey_t new_rkey;
  MOSAL_iobuf_t new_iobuf = NULL;


  FUNC_IN;

  /* if invalid MMU handle */
  if ( BAD_MMU_HNDL(mm_hndl) ) return VIP_EINVAL_MMU_HNDL;

  MTL_DEBUG3(MT_FLFMT("%s: original mr hndl: 0x%x"),__func__,ori_mr_hndl); 
  rc = VIP_array_erase_prepare(mm_hndl->mr_by_hndl,ori_mr_hndl, &val);
  if (rc != VIP_OK) {
      MTL_ERROR1(MT_FLFMT("%s: Bad MR handle: 0x%x"),__func__, ori_mr_hndl);
      MT_RETURN(VIP_EINVAL_MR_HNDL);
  }

  mm_mro_p = (MM_mro *)val;
    
  rc=VIP_RSCT_check_usr_ctx(usr_ctx,&mm_mro_p->rsc_ctx);
  if (rc != VIP_OK) 
  {
      VIP_array_erase_undo(mm_hndl->mr_by_hndl,ori_mr_hndl);
      MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. original mr hndl: 0x%x (%s)"),__func__,
                 ori_mr_hndl,VAPI_strerror_sym(rc));
      MT_RETURN(rc);
  }

  if (change_type == 0) {
    goto end;
  }
  
  pdm = HOBKL_get_pdm(mm_hndl->hob_hndl);
  if (change_type & VAPI_MR_CHANGE_PD) {
      MTL_DEBUG1(MT_FLFMT("%s: changed pd"),__func__);
      rc = PDM_add_object_to_pd(usr_ctx, pdm,pd_hndl, &prot_ctx, &hh_mr.pd);
      if (rc != VIP_OK)
      {
               MTL_ERROR1(MT_FLFMT("%s: invalid pd hndl: 0x%x (%d:%s)"),__func__, pd_hndl,
                          rc,VAPI_strerror_sym(rc)); 
               rc = VIP_EINVAL_PD_HNDL;
               VIP_array_erase_undo(mm_hndl->mr_by_hndl,ori_mr_hndl);    
               goto just_inval_lbl;
      }
        
  }
  else {
    rc = PDM_get_prot_ctx(pdm, mm_mro_p->pub_props.pd_hndl, &prot_ctx);
    if ( rc != VIP_OK ) {
        MTL_ERROR1(MT_FLFMT("%s: cannot get prot ctx for current pd handle 0x%x (%d:%s)"),__func__, 
                   mm_mro_p->pub_props.pd_hndl,rc,VAPI_strerror_sym(rc)); 
        rc = VIP_EFATAL;
        VIP_array_erase_undo(mm_hndl->mr_by_hndl,ori_mr_hndl);    
        goto just_inval_lbl;
    }
  }

  
  if (change_type & VAPI_MR_CHANGE_TRANS) {
      VAPI_mrw_acl_t cur_acl;
      
      if (mrw_prop_p->size == 0) {
            MTL_ERROR1(MT_FLFMT("%s: invalid  mr len: "U64_FMT),__func__,mrw_prop_p->size);  
            rc= VIP_EINVAL_SIZE;
            goto inval_lbl; 
      }
      
      MTL_DEBUG1(MT_FLFMT("%s: changed trans"),__func__);
      hh_mr.start = mrw_prop_p->start;
      hh_mr.size =  mrw_prop_p->size;
    
      switch (mrw_prop_p->type) {
        case VAPI_MR:
            if (change_type & VAPI_MR_CHANGE_ACL) {
                    cur_acl = mrw_prop_p->acl;
            }else cur_acl = mm_mro_p->pub_props.acl; 
        
            rc = MM_bld_hh_mr(prot_ctx,mrw_prop_p->start,mrw_prop_p->size,&hh_mr, &new_iobuf,cur_acl);
            if (rc != VIP_OK) {
                goto inval_lbl;
            }
            break;
        case VAPI_MPR:
            rc = MM_bld_hh_pmr(mm_hndl,mrw_prop_p,&hh_mr);
            if (rc == VIP_OK ) {
                break;
            }
        default:
            MTL_ERROR1(MT_FLFMT("%s: ERROR - requested change translation with invalid target region type 0x%x"),
                       __func__, mrw_prop_p->type);
            rc = VIP_EINVAL_PARAM;
            goto inval_lbl;
       }
  }
  
  if (change_type & (VAPI_MR_CHANGE_ACL)) {
    rc=CHECK_ACL(mrw_prop_p->acl);
    if (rc != VIP_OK) 
    {
            MTL_ERROR1(MT_FLFMT("%s: invalid acl: 0x%x"),__func__, mrw_prop_p->acl);
            goto dereg_and_inval;
    }
    if (new_iobuf == NULL) {
    /* we haven't registered iobuf till now (no translation change) so we must do it here to validate the acl*/
        rc= make_iobuf(prot_ctx,mm_mro_p->start,mm_mro_p->size,mrw_prop_p->acl,&new_iobuf);
        if (rc != VIP_OK) {
            MTL_ERROR1(MT_FLFMT("%s: make iobuf failed: (%d:%s)"),__func__,rc,VAPI_strerror_sym(rc));
            goto inval_lbl;
        }
    }
    MTL_DEBUG1(MT_FLFMT("changed acl \n"));
    hh_mr.acl = mrw_prop_p->acl;
  } 
  
  
  MTL_DEBUG1(MT_FLFMT("%s: orig pd: 0x%x"),__func__,mm_mro_p->pub_props.pd_hndl);
  MTL_DEBUG1(MT_FLFMT("%s: orig acl: 0x%x"),__func__,mm_mro_p->pub_props.acl);
    
    /* Register the region on the driver  */
  rc_hh = HH_reregister_mr(hh_hndl,mm_mro_p->pub_props.l_key,change_type,&hh_mr,
                           &new_lkey,&new_rkey); 
  MTL_DEBUG3("[reregister_mr]:Keys received. R_key:0x%x L_key:0x%x\n", 
             mm_mro_p->pub_props.r_key, mm_mro_p->pub_props.l_key);
  if (rc_hh != HH_OK) {
      MTL_ERROR1(MT_FLFMT("%s: CALLED HH_reregister_mr. HH-return=(%d:%s)"),__func__,
                 rc_hh, HH_strerror_sym(rc_hh));
      if (rc_hh == HH_EBUSY) {
        rc= VIP_EBUSY;  /* bounded window */
        goto dereg_and_inval;
      } 
      else {
        rc = (VIP_ret_t) rc_hh; /* designate MR is invalidated (IB-spec. C11-20) */
        goto inval_vip_lbl;
      }
  } else rc = VIP_OK;
  
  /* change attributes locally */
  mm_mro_p->pub_props.l_key = new_lkey;
  mm_mro_p->pub_props.r_key = new_rkey;                           
  

  if (change_type & VAPI_MR_CHANGE_ACL) {
    MTL_DEBUG1(MT_FLFMT("%s: changed acl"),__func__);
    mm_mro_p->pub_props.acl = mrw_prop_p->acl;
  }
  
  if (change_type & VAPI_MR_CHANGE_PD) {
      MTL_DEBUG1(MT_FLFMT("%s: changed pd"),__func__);
      /* erase connection of old pd to mr */
      rc = PDM_rm_object_from_pd(pdm, mm_mro_p->pub_props.pd_hndl); 
      if (rc != VIP_OK) {
        MTL_ERROR1(MT_FLFMT(
          "%s: UNEXPECTED ERROR: Failed PDM_rm_object_from_pd (reboot recommended...)"),__func__);
      }
      mm_mro_p->pub_props.pd_hndl = pd_hndl;      
  }
  
  if (change_type & VAPI_MR_CHANGE_TRANS) {
      MTL_DEBUG1(MT_FLFMT("%s: changed translation"), __func__);
      mm_mro_p->start = mrw_prop_p->start;
      mm_mro_p->size = mrw_prop_p->size;
      mm_mro_p->pub_props.re_local_end = mrw_prop_p->start + mm_mro_p->size - 1;
      mm_mro_p->pub_props.re_local_start = mrw_prop_p->start;
      mm_mro_p->pub_props.re_remote_end = mm_mro_p->pub_props.re_local_end;
      mm_mro_p->pub_props.re_remote_start = mm_mro_p->pub_props.re_local_start;
      if (mrw_prop_p->type == VAPI_MPR) {
            MM_FREE(hh_mr.tpt.tpt.buf_lst.phys_buf_lst);
            MM_FREE(hh_mr.tpt.tpt.buf_lst.buf_sz_lst);
      }   
      mm_mro_p->pub_props.type = mrw_prop_p->type;
   }

  if ((new_iobuf != NULL) || 
      ((change_type & VAPI_MR_CHANGE_TRANS) && mrw_prop_p->type == VAPI_MPR)){
     
      if (mm_mro_p->iobuf != NULL) {
            MOSAL_iobuf_deregister(mm_mro_p->iobuf);
      }
      
      mm_mro_p->iobuf = new_iobuf;
  }
  
  
    
end:
  *mr_hndl_p = ori_mr_hndl;
  MM_fill_mr_props(mm_mro_p,mr_prop_p);
  MTL_DEBUG1(MT_FLFMT("%s: cur pd : 0x%x, cur_acl=0x%x, new mr handle =0x%x"),
             __func__, mr_prop_p->pd_hndl, mr_prop_p->acl, *mr_hndl_p);
  VIP_array_erase_undo(mm_hndl->mr_by_hndl,ori_mr_hndl);
  MT_RETURN(VIP_OK);


dereg_and_inval:  
   if (new_iobuf != NULL) {
         MOSAL_iobuf_deregister(new_iobuf);  
   }
   if ((change_type & VAPI_MR_CHANGE_TRANS) && (mrw_prop_p->type == VAPI_MPR)) {
        MM_FREE(hh_mr.tpt.tpt.buf_lst.phys_buf_lst);
        MM_FREE(hh_mr.tpt.tpt.buf_lst.buf_sz_lst);
   }
   
inval_lbl:
   VIP_array_erase_undo(mm_hndl->mr_by_hndl,ori_mr_hndl);    
   if (change_type & VAPI_MR_CHANGE_PD) {
       /* remove binding with new pd handle*/
      if (PDM_rm_object_from_pd(pdm, pd_hndl) != VIP_OK)
         {
            MTL_ERROR1(MT_FLFMT(
                    "UNEXPECTED ERROR: Failed PDM_rm_object_from_pd (reboot recommended...)\n"));
         }
   }
   
just_inval_lbl:
    
    if (rc == VIP_EBUSY) {
        MT_RETURN(VIP_EBUSY);
    }
    rc2= MM_destroy_mr(usr_ctx,mm_hndl,ori_mr_hndl);
    MT_RETURN(rc2 == VIP_OK ? rc : rc2);
    
inval_vip_lbl:
    
  if (change_type & VAPI_MR_CHANGE_PD) {
       /* remove binding with new pd handle*/
      if (PDM_rm_object_from_pd(pdm, pd_hndl) != VIP_OK)
         {
            MTL_ERROR1(MT_FLFMT(
                    "%s: UNEXPECTED ERROR: Failed PDM_rm_object_from_pd (reboot recommended...)"),__func__);
         }
   }    
      /* remove binding with old pd handle */
   if (PDM_rm_object_from_pd(pdm, mm_mro_p->pub_props.pd_hndl) != VIP_OK)
   {
        MTL_ERROR1(MT_FLFMT(
                "%s: UNEXPECTED ERROR: Failed PDM_rm_object_from_pd (reboot recommended...)"),__func__);
   }

  if (mm_mro_p->iobuf != NULL) {
        MOSAL_iobuf_deregister(mm_mro_p->iobuf);
  }
  if (new_iobuf != NULL) {
      MOSAL_iobuf_deregister(new_iobuf);  
  }
  if ((change_type & VAPI_MR_CHANGE_TRANS) && (mrw_prop_p->type == VAPI_MPR)) {
     MM_FREE(hh_mr.tpt.tpt.buf_lst.phys_buf_lst);
     MM_FREE(hh_mr.tpt.tpt.buf_lst.buf_sz_lst);
  }
  rc = VIP_RSCT_deregister_rsc(usr_ctx,&mm_mro_p->rsc_ctx,VIP_RSCT_MR);
  VIP_array_erase_done(mm_hndl->mr_by_hndl,ori_mr_hndl,NULL);    
  MM_FREE(mm_mro_p);
  MT_RETURN(VIP_EAGAIN);

}




/*
 *    MM_map_fmr
 */
VIP_ret_t MM_map_fmr(VIP_RSCT_t usr_ctx,MM_hndl_t mm_hndl,EVAPI_fmr_hndl_t fmr_hndl,EVAPI_fmr_map_t* map_p,
                     VAPI_lkey_t *lkey_p,VAPI_rkey_t *rkey_p)
{
  VIP_ret_t rc;
  HH_ret_t ret = HH_OK;
  MM_fmr *mm_fmr_p = NULL;
  HH_hca_hndl_t hh_hndl = HOBKL_get_hh_hndl(mm_hndl->hob_hndl);

  FUNC_IN;

 // MTL_ERROR1(MT_FLFMT("%s: start="U64_FMT" size="U64_FMT" page_array_len="SIZE_T_DFMT
 //                     " page_array=%p"), __func__,
 //            map_p->start, map_p->size, map_p->page_array_len, map_p->page_array);
  /* if invalid MMU handle */
  if ( BAD_MMU_HNDL(mm_hndl) ) return VIP_EINVAL_MMU_HNDL;

  if (map_p->size == 0) return VIP_EINVAL_SIZE;

  //if (map_p->start == VA_NULL) return VIP_EINVAL_ADDR;
  if (map_p->page_array_len == 0) {
    MTL_ERROR1(MT_FLFMT("%s: Given page_array_len=0"), __func__);
    return VIP_EINVAL_SIZE;
  }

  rc = VIP_array_find_hold(mm_hndl->fmr_by_hndl, fmr_hndl, (VIP_array_obj_t *)&mm_fmr_p);
  if ( rc != VIP_OK ) return VIP_EINVAL_MR_HNDL;
  if ((rc=VIP_RSCT_check_usr_ctx(usr_ctx,&mm_fmr_p->rsc_ctx)) != VIP_OK) {
    MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. fmr hndl: 0x%x (%s)"),__func__,
               fmr_hndl,VAPI_strerror_sym(rc));
    VIP_array_find_release(mm_hndl->fmr_by_hndl, fmr_hndl);
    MT_RETURN(rc);
  }

  MTL_DEBUG1(MT_FLFMT("%s: lkey:0x%X  start: "U64_FMT"  size:"U64_FMT"  page_array_len:"
                      SIZE_T_XFMT), __func__,
             mm_fmr_p->lkey, map_p->start, map_p->size, map_p->page_array_len); 
  
  ret = HH_map_fmr(hh_hndl,mm_fmr_p->lkey,map_p,&mm_fmr_p->lkey,&mm_fmr_p->rkey);
  if (ret != HH_OK) {
    MTL_ERROR1(MT_FLFMT("HH_map_fmr failed got:%s"),HH_strerror_sym(ret));
    if (ret == HH_EINVAL )
      rc = VIP_EINVAL_PARAM;
    else rc = VIP_EAGAIN;
    VIP_array_find_release(mm_hndl->fmr_by_hndl, fmr_hndl);
    MT_RETURN(rc);
  }

  MTL_DEBUG2(MT_FLFMT("%s: got lkey: 0x%x rkey: 0x%x"),__func__,mm_fmr_p->lkey,mm_fmr_p->rkey);
  *lkey_p = mm_fmr_p->lkey;
  *rkey_p = mm_fmr_p->rkey;

  VIP_array_find_release(mm_hndl->fmr_by_hndl, fmr_hndl);
  MT_RETURN(VIP_OK);
}

/*
 *      MMU_unmap_fmr
 */
VIP_ret_t MM_unmap_fmr(VIP_RSCT_t usr_ctx,MM_hndl_t mm_hndl,MT_size_t size,EVAPI_fmr_hndl_t* mr_hndl_arr)
{
    VIP_ret_t rc = VIP_OK;
    VIP_ret_t tmp_rc;
    MT_size_t i, unmap_chunk_size;
    HH_hca_hndl_t hh_hndl = HOBKL_get_hh_hndl(mm_hndl->hob_hndl);
    HH_ret_t ret;
    VAPI_lkey_t* lkey_arr;
    MM_fmr* mm_fmr_p = NULL;
    VIP_array_obj_t val1;
    MT_bool have_fatal = FALSE;
    
        
    FUNC_IN;

    if ( BAD_MMU_HNDL(mm_hndl) ) {
        MT_RETURN(VIP_EINVAL_MMU_HNDL);
    }

    lkey_arr= MT_bufpool_alloc(mm_hndl->lkey_array_bufpool);
    if (lkey_arr == NULL) {
      MTL_ERROR4(MT_FLFMT("%s: Failed lkey_arr allocation"), __func__);
      MT_RETURN(VIP_EAGAIN);    
    }

    MTL_DEBUG4(MT_FLFMT("%s: Before while(). lkey_arr=%p"),__func__, lkey_arr);

    while ((size > 0) && (rc == VIP_OK)) { /* loop for each chunk of l-keys */
      for (i=0; (i < MAX_FMR_UNMAP_LKEY_AT_ONCE) && (i < size); i++) {
          /* Find FMR objects and take L-keys (for HH_unmap_fmr) */
          rc = VIP_array_find_hold(mm_hndl->fmr_by_hndl,mr_hndl_arr[i],&val1);
          if ( rc != VIP_OK )
          {
                  MTL_ERROR1(MT_FLFMT("%s: bad fmr hndl (0x%X)"), __func__, mr_hndl_arr[i]); 
                  rc = VIP_EINVAL_MR_HNDL;
                  break;
          }
          mm_fmr_p = (MM_fmr*)val1;
          if ((rc=VIP_RSCT_check_usr_ctx(usr_ctx,&mm_fmr_p->rsc_ctx)) != VIP_OK)
          {
                  MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. fmr hndl: 0x%x (%s)"),__func__,
                             mr_hndl_arr[i],VAPI_strerror_sym(rc));
                  break;
          }

          if (rc != VIP_OK)  {
            tmp_rc= VIP_array_find_release(mm_hndl->fmr_by_hndl, mr_hndl_arr[i]);
            if (tmp_rc != VIP_OK) {
              MTL_ERROR1(MT_FLFMT("%s: Unexpected error on VIP_array_find (%s)"), __func__,
                         VIP_common_strerror_sym(tmp_rc));
            }
            break;
          }

          lkey_arr[i]= mm_fmr_p->lkey;
      }
      
      unmap_chunk_size= i;
      
      MTL_DEBUG4(MT_FLFMT(
        "%s: Before HH_unmap_fmr (size="SIZE_T_FMT", mr_hndl_arr=%p, unmap_chunk_size="SIZE_T_FMT),
                 __func__, size, mr_hndl_arr, unmap_chunk_size);

      /* Now unmap current chunk */
      if (rc == VIP_OK) {
        ret= HH_unmap_fmr(hh_hndl, (u_int32_t)i, lkey_arr);
        have_fatal = (ret == HH_EFATAL) ? TRUE : FALSE;
        if ((ret != HH_OK) && (ret != HH_EFATAL)) {
            MTL_ERROR1(MT_FLFMT("failed HH unmap fmr got:%s\n"),HH_strerror_sym(ret));
             if (ret == HH_EINVAL) rc = VIP_EINVAL_PARAM;
                 else if (ret == HH_EAGAIN) rc = VIP_EAGAIN;
                      else rc = VIP_EFATAL; /* unexpected error */
        }
      }
      
      /* Release FMRs of this iteration */
      for (i= 0; i < unmap_chunk_size; i++) {
        tmp_rc= VIP_array_find_release(mm_hndl->fmr_by_hndl, mr_hndl_arr[i]);
        if (tmp_rc != VIP_OK) {
          MTL_ERROR1(MT_FLFMT("%s: Unexpected error on VIP_array_find_release (%s)"), __func__,
                     VIP_common_strerror_sym(tmp_rc));
          rc= (rc == VIP_OK) ? tmp_rc : rc; /* retain original error */
          continue; /* Try to release the other FMRs */
        }
      } /* for i to release */

      /* If more entries than chunk size, move to next chunk */
      mr_hndl_arr+= unmap_chunk_size;
      size-= unmap_chunk_size;
    } /* while (size>0) */
    

   MT_bufpool_free(mm_hndl->lkey_array_bufpool, lkey_arr);
   MT_RETURN(rc);
}

/*
 *      MM_destroy_mr
 */
VIP_ret_t MM_destroy_mr(VIP_RSCT_t usr_ctx,MM_hndl_t mm_hndl,MM_mrw_hndl_t mr_hndl)
{
    MM_mro *mm_mro_p;
    VIP_ret_t rc;
    HH_ret_t  rc_hh;
    PDM_hndl_t pdm_hndl = HOBKL_get_pdm(mm_hndl->hob_hndl);
    HH_hca_hndl_t hh_hndl = HOBKL_get_hh_hndl(mm_hndl->hob_hndl);
    VIP_array_obj_t val;
    MT_bool  have_fatal = FALSE;

    FUNC_IN;

    /* if invalid MMU handle */
    if (BAD_MMU_HNDL(mm_hndl)) return VIP_EINVAL_MMU_HNDL;
    
    rc=VIP_array_find_hold(mm_hndl->mr_by_hndl, mr_hndl, &val);
    if ( rc != VIP_OK ) 
    {
            MTL_ERROR1(MT_FLFMT("%s: invalid mr hndl: 0x%x (%d:%s)"),
                       __func__,mr_hndl,rc,VIP_common_strerror_sym(rc));
            MT_RETURN(VIP_EINVAL_MR_HNDL);
    }
    mm_mro_p = (MM_mro*)val;    
    rc = VIP_RSCT_check_usr_ctx(usr_ctx,&mm_mro_p->rsc_ctx);
    if (rc != VIP_OK) {
      VIP_array_find_release(mm_hndl->mr_by_hndl, mr_hndl);
      MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. mr hndl: 0x%x (%s)"),
                 __func__,mr_hndl,VAPI_strerror_sym(rc));
      MT_RETURN(rc);
    }
    
    rc= VIP_array_find_release_erase_prepare(mm_hndl->mr_by_hndl, mr_hndl, &val);
    if ( rc != VIP_OK )
    {
      if (rc == VIP_EINVAL_HNDL) { /* only EBUSY is expected */
        MTL_ERROR1(MT_FLFMT("%s: Unexpected error - invalid handle (0x%x) on find_release_erase_prepare"),
                   __func__,mr_hndl);
      } else {
          MTL_DEBUG1(MT_FLFMT("%s:  VIP_array_find_release_erase_prepare failed. mr handle=0x%x (%d:%s)"),
                     __func__,mr_hndl,rc,VIP_common_strerror_sym(rc));
      }
      return rc;
    }
    mm_mro_p = (MM_mro*)val;    
    
    MTL_DEBUG2(MT_FLFMT("%s: before HH_deregister_mr"),__func__);
    /* Unregister the region at driver */
    rc_hh = HH_deregister_mr(hh_hndl, mm_mro_p->pub_props.l_key);
    have_fatal = (rc_hh == HH_EFATAL) ? TRUE : FALSE;
    if ((rc_hh != HH_OK) && (rc_hh != HH_EFATAL)) {
      if (rc_hh == HH_ENODEV )
        rc_hh = HH_EINVAL_HCA_HNDL;
      else if (rc_hh > HH_ERROR_MIN) {
        rc_hh = HH_EBUSY;
      }
      MTL_ERROR1(MT_FLFMT("%s: HH_deregister_mr failed for lkey 0x%x (%d:%s)"), __func__, 
                 mm_mro_p->pub_props.l_key,rc_hh,HH_strerror_sym(rc_hh));
      rc = (VIP_ret_t)rc_hh;  /* now, map HH errors to VIP errors */
      VIP_array_erase_undo(mm_hndl->mr_by_hndl, mr_hndl);
      MT_RETURN(rc);
    }

    VIP_array_erase_done(mm_hndl->mr_by_hndl, mr_hndl,NULL);
        /* Unregister the Region from Protection Domain */
    PDM_rm_object_from_pd(pdm_hndl, mm_mro_p->pub_props.pd_hndl);
    
    /* Update Lkey VIP_hash */
    /*VIP_hash_erase(mm_hndl->mr_by_lkey, mm_mro_p->pub_props.l_key, &val);*/
    
        /* Update Lkey VIP_hash */
    /*VIP_hash_erase(mm_hndl->mr_by_rkey, mm_mro_p->pub_props.r_key, &val);*/
    
    if (mm_mro_p->pub_props.type != VAPI_MPR) {
       if (have_fatal == TRUE) {
           VIP_delay_unlock_insert(mm_hndl->delay_unlocks,mm_mro_p->iobuf);
       } else {
           MOSAL_iobuf_deregister(mm_mro_p->iobuf);
       }
    }
    
    rc = VIP_RSCT_deregister_rsc(usr_ctx,&mm_mro_p->rsc_ctx,VIP_RSCT_MR);
    /* free the Memory Region object */
    if (rc != VIP_OK) { 
      MTL_DEBUG1(MT_FLFMT("%s: VIP_RSCT_deregister_rsc failed (%d:%s)"),
                 __func__,rc,VAPI_strerror_sym(rc));
    }
    MM_FREE(mm_mro_p);
    MT_RETURN(rc);
}

/*
 *      MM_destroy_mw
 */
VIP_ret_t MM_destroy_mw(VIP_RSCT_t usr_ctx,MM_hndl_t mm_h, IB_rkey_t init_r_key)
{  	
    VIP_ret_t			rc,rc1;
    VIP_hash_value_t	mw_h;
    HH_ret_t			hh_rc;
    VIP_array_obj_t		object_p;
    MM_mw				*mw_p = NULL;
    
    FUNC_IN;
    MTL_DEBUG1(MT_FLFMT("%s: rkey: 0x%x"),__func__,init_r_key); 

    /* if invalid MMU handle */
    if (BAD_MMU_HNDL(mm_h)) return VIP_EINVAL_MMU_HNDL;
    
    /* array must be freed first, having a ref count on each entry
       to provide thread sfety.*/
    rc= VIP_hash_find(mm_h->mw_by_rkey,init_r_key,&mw_h);
    if ( rc != VIP_OK ) {
        MTL_ERROR4(MT_FLFMT("%s: error: Unknown memory window's initial R-key (0x%x)"),__func__,init_r_key);
        return VIP_EINVAL_MW_HNDL;
    }
    
    rc=VIP_array_find_hold(mm_h->mw_by_hndl,mw_h,&object_p);
    if ( rc != VIP_OK )
    {
            MTL_ERROR4(MT_FLFMT("%s: error: Unknown memory window handle (0x%x)"),__func__,mw_h);
            return VIP_EINVAL_MW_HNDL;
    }
    mw_p = (MM_mw *)object_p;
    rc = VIP_RSCT_check_usr_ctx(usr_ctx,&mw_p->rsc_ctx);
    if (rc != VIP_OK) {
      VIP_array_find_release(mm_h->mw_by_hndl, mw_h);
      MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. mw hndl: 0x%x (%s)"),__func__,mw_h,VAPI_strerror_sym(rc));
      return rc;
    }

    rc = VIP_array_find_release_erase(mm_h->mw_by_hndl,mw_h,&object_p);
    if ( rc != VIP_OK )
    {
      if (rc == VIP_EINVAL_HNDL) { /* only EBUSY is expected */
        MTL_ERROR1(MT_FLFMT("%s: Unexpected error - invalid handle (0x%x) on find_release_erase_prepare"),
                   __func__, mw_h);
      } else {
          MTL_DEBUG1(MT_FLFMT("%s:  VIP_array_find_release_erase failed. mw hndl=0x%x (%d:%s)"),
                     __func__, mw_h,rc,VAPI_strerror_sym(rc));
      }
      return rc;
    }
    mw_p = (MM_mw *)object_p;

    /* Update r_key VIP_hash */
    VIP_hash_erase(mm_h->mw_by_rkey,mw_p->init_key,NULL);
    
    
    /* Unregister the region at driver */
    MTL_DEBUG1(MT_FLFMT("%s: before HH_free_mw"), __func__);
    hh_rc = HH_free_mw(HOBKL_get_hh_hndl(mm_h->hob_hndl),mw_p->init_key);
    if (hh_rc != HH_OK) {
      if (hh_rc == HH_ENODEV )  {
      MTL_ERROR1(MT_FLFMT("%s: HH_free_mw failed. init_key=0x%x : Invalid HH's HCA handle"),
                 __func__, mw_p->init_key);
         rc = VIP_EINVAL_HCA_HNDL;
    } else if (hh_rc > HH_ERROR_MIN) {
      MTL_ERROR1(MT_FLFMT("%s: HH_free_mw failed. init_key=0x%x (%d:%s)"),
                 __func__, mw_p->init_key,rc,HH_strerror_sym(hh_rc));
         rc = VIP_ERROR;
      } else {
      MTL_ERROR1(MT_FLFMT("%s: HH_free_mw failed. init_key=0x%x (%d:%s)")
                 ,__func__, mw_p->init_key,rc,HH_strerror_sym(hh_rc));
         rc= (VIP_ret_t)hh_rc;
      }
    }
    
    /* Unregister the Region from Protection Domain */
    rc1 = PDM_rm_object_from_pd(HOBKL_get_pdm(mm_h->hob_hndl), mw_p->pd_h);
    if (rc1 != VIP_OK) { 
      MTL_DEBUG1(MT_FLFMT("%s: PDM_rm_object_from_pd failed. init_rkey=0x%x (%d:%s)"),
                 __func__,init_r_key,rc1,VAPI_strerror_sym(rc1));
    }
    rc = VIP_RSCT_deregister_rsc(usr_ctx,&mw_p->rsc_ctx,VIP_RSCT_MW);
    /* free the Memory Region object */
    if (rc1 != VIP_OK) { 
      MTL_DEBUG1(MT_FLFMT("%s: VIP_RSCT_deregister_rsc failed. init_rkey=0x%x (%d:%s)"),
                 __func__,init_r_key, rc1,VAPI_strerror_sym(rc1));
    }
    MM_FREE(mw_p);
    MT_RETURN(rc);
}


/*
 *  MM_free_fmr
 */
VIP_ret_t MM_free_fmr(VIP_RSCT_t usr_ctx,MM_hndl_t mm_hndl,EVAPI_fmr_hndl_t fmr_hndl)
{
    MM_fmr *mm_fmr_p = NULL;
    VIP_ret_t rc;
    HH_ret_t  ret;
    PDM_hndl_t pdm_hndl = HOBKL_get_pdm(mm_hndl->hob_hndl);
    HH_hca_hndl_t hh_hndl = HOBKL_get_hh_hndl(mm_hndl->hob_hndl);
    VIP_array_obj_t val;
    MT_bool have_fatal = FALSE;

    FUNC_IN;
    if (BAD_MMU_HNDL(mm_hndl)) return VIP_EINVAL_MMU_HNDL;
    
    
    rc=VIP_array_find_hold(mm_hndl->fmr_by_hndl,fmr_hndl,&val);
    if ( rc != VIP_OK ) return VIP_EINVAL_MR_HNDL;
    mm_fmr_p = (MM_fmr *)val;
    rc = VIP_RSCT_check_usr_ctx(usr_ctx,&mm_fmr_p->rsc_ctx);
    if (rc != VIP_OK) {
      VIP_array_find_release(mm_hndl->fmr_by_hndl,fmr_hndl);
      MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. fmr hndl: 0x%x (%s)"),__func__,fmr_hndl,VAPI_strerror_sym(rc));
      return rc;
    }

    rc = VIP_array_find_release_erase_prepare(mm_hndl->fmr_by_hndl, fmr_hndl, &val);
    if ( rc != VIP_OK )
    {
      if (rc == VIP_EINVAL_HNDL) { /* only EBUSY is expected */
        MTL_ERROR1(MT_FLFMT("%s: Unexpected error - invalid handle on find_release_erase"),
                   __func__);
      }
      return rc;
    }
    mm_fmr_p = (MM_fmr*)val;
    
    ret = HH_free_fmr(hh_hndl,mm_fmr_p->lkey);
    have_fatal = (ret == HH_EFATAL) ? TRUE : FALSE;
    if ((ret != HH_OK) && (ret != HH_EFATAL)) {
        VIP_array_erase_undo(mm_hndl->fmr_by_hndl,fmr_hndl);
        MTL_ERROR1(MT_FLFMT("%s: failed HH_free_fmr (%s)"), __func__, HH_strerror_sym(ret));    
        return (VIP_ret_t)ret;
    }

    rc = PDM_rm_object_from_pd(pdm_hndl, mm_fmr_p->pd_hndl);
    if (rc != VIP_OK) {
        MTL_ERROR1(MT_FLFMT("%s: unexpected error while removing hndl from PDM (%s)\n"),
                   __func__,VAPI_strerror_sym(rc));
    }
    
    VIP_array_erase_done(mm_hndl->fmr_by_hndl,fmr_hndl,NULL);
    VIP_RSCT_deregister_rsc(usr_ctx,&mm_fmr_p->rsc_ctx,VIP_RSCT_FMR);

    MM_FREE(mm_fmr_p);
    MT_RETURN(rc);
}



/*
 *  MM_query_mr
 */
VIP_ret_t MM_query_mr( VIP_RSCT_t usr_ctx, MM_hndl_t mm_hndl, MM_mrw_hndl_t mr_hndl,MM_VAPI_mro_t *mr_prop_p)
{
  VIP_ret_t rc = VIP_OK;
  MM_mro* mm_mro_p;
  VIP_array_obj_t val;

  FUNC_IN;
  
  
  /* if invalid MMU handle */
  if ( BAD_MMU_HNDL(mm_hndl) )
    return(VIP_EINVAL_MMU_HNDL);

  rc = VIP_array_find_hold(mm_hndl->mr_by_hndl, mr_hndl, &val);
  if ( rc != VIP_OK )
      {
        MTL_ERROR1(MT_FLFMT("%s: could not find mr handle"),__func__);
        return VIP_EINVAL_MR_HNDL;
       }
  mm_mro_p = (MM_mro*)val;
  rc = VIP_RSCT_check_usr_ctx(usr_ctx,&mm_mro_p->rsc_ctx);
  if (rc == VIP_OK)
  {
      MM_fill_mr_props(mm_mro_p,mr_prop_p);
  } else {
      MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. mr hndl: 0x%x (%s)"),__func__,mr_hndl,VAPI_strerror_sym(rc));
  }
  
  VIP_array_find_release(mm_hndl->mr_by_hndl, mr_hndl);
  MT_RETURN(rc);
}



/**********************Static functions implementation*************************/

/*
 *  MM_bld_hh_mr    
 */
static VIP_ret_t MM_bld_hh_mr(MOSAL_prot_ctx_t prot_ctx, VAPI_virt_addr_t start,
                              VAPI_size_t size,HH_mr_t *hh_mr_p, MOSAL_iobuf_t *iobuf_p, VAPI_mrw_acl_t acl)
{
  MOSAL_iobuf_props_t props;
  VIP_ret_t ret;

  ret= make_iobuf(prot_ctx,start,size,acl,iobuf_p);
  if (ret!= VIP_OK) {
    return ret;
  }
  MOSAL_iobuf_get_props(*iobuf_p, &props);
  
  hh_mr_p->tpt.tpt_type = HH_TPT_IOBUF;
  hh_mr_p->tpt.num_entries = (VAPI_size_t)props.nr_pages;
  hh_mr_p->tpt.tpt.iobuf= *iobuf_p;

  return VIP_OK;
}


/*
 *  MM_bld_hh_pmr    
 */
static VIP_ret_t MM_bld_hh_pmr(MM_hndl_t  mm_hndl,VAPI_mrw_t* mrw_p,HH_mr_t *hh_mr_p)
{
  VAPI_phy_addr_t* buf_lst;
  VAPI_size_t* sz_lst;
  MT_size_t i;
  u_int8_t page_shift = MOSAL_SYS_PAGE_SHIFT;
  VAPI_size_t total_sz = 0;

  /* check if the bufs sz aligned to page size & aligned to page start */
  MTL_DEBUG1("buf list: \n");
  for (i=0; i<mrw_p->pbuf_list_len; i++ ) {
    MTL_DEBUG4(" buf: "U64_FMT" size:"U64_FMT" \n",mrw_p->pbuf_list_p[i].start,mrw_p->pbuf_list_p[i].size); 
    
    if (mrw_p->pbuf_list_p[i].size != 
        MM_DOWN_ALIGNX_PHYS(mrw_p->pbuf_list_p[i].size,page_shift) ) {
            MTL_ERROR1("phys buf size is not page aligned \n");
            return VIP_EINVAL_PARAM;
    }

    total_sz +=  mrw_p->pbuf_list_p[i].size;

    if (mrw_p->pbuf_list_p[i].start != 
        MM_DOWN_ALIGNX_PHYS(mrw_p->pbuf_list_p[i].start,page_shift) ) {
            MTL_ERROR1("phys buf start adrs is not aligned to page start \n");
            return VIP_EINVAL_PARAM;
    }

  }
  if (total_sz < mrw_p->size + mrw_p->iova_offset) {
    MTL_ERROR2("bld pmr: mr size(0x"U64_FMT")+iova offset(0x"U64_FMT") ",mrw_p->size,mrw_p->iova_offset); 
    MTL_ERROR2("bigger than total phys buffs size(0x"U64_FMT") \n",total_sz);
    return VIP_EINVAL_SIZE;
  }
  if (mrw_p->iova_offset > mrw_p->pbuf_list_p[0].size) {
      MTL_ERROR2("bld pmr: iova offset(0x"U64_FMT") bigger than 1st phy buf sz(0x"U64_FMT")\n",
                                                  mrw_p->iova_offset,mrw_p->pbuf_list_p[0].size);
      return VIP_EINVAL_SIZE;
  }

  buf_lst = (VAPI_phy_addr_t*)MM_ALLOC(sizeof(VAPI_phy_addr_t)*(mrw_p->pbuf_list_len));
  if ( buf_lst == NULL ) {
    MTL_ERROR1("[MM_bld_hh_pmr]: error allocating resources \n");
    return(VIP_EAGAIN);
  }
  sz_lst = (VAPI_size_t*)MM_ALLOC(sizeof(VAPI_size_t)*(mrw_p->pbuf_list_len));
  if ( sz_lst == NULL ) {
    MTL_ERROR1("[MM_bld_hh_pmr]: error allocating resources \n");
    MM_FREE(buf_lst);
    return(VIP_EAGAIN);
  }

  /* fill MR struct */
  hh_mr_p->tpt.tpt_type = HH_TPT_BUF;
  MTL_DEBUG4(MT_FLFMT("[MM_bld_hh_pmr]: len is "SIZE_T_DFMT), mrw_p->pbuf_list_len);

  for (i=0; i<mrw_p->pbuf_list_len; i++ ) {
    buf_lst[i] = mrw_p->pbuf_list_p[i].start;
    sz_lst[i] = mrw_p->pbuf_list_p[i].size;
  }

  hh_mr_p->tpt.tpt.buf_lst.phys_buf_lst = buf_lst; 
  hh_mr_p->tpt.tpt.buf_lst.buf_sz_lst = sz_lst;
  hh_mr_p->tpt.tpt.buf_lst.iova_offset = mrw_p->iova_offset;
  hh_mr_p->tpt.num_entries = mrw_p->pbuf_list_len;

  MTL_DEBUG1(MT_FLFMT("len is "SIZE_T_DFMT" \n"),hh_mr_p->tpt.num_entries);
  return VIP_OK;
}
  

/*
 *    MM_mr_get_keys
 */
static VIP_ret_t MM_mr_get_keys(MM_hndl_t  mm_hndl, MM_mro *mm_mro_p,HH_mr_t* mr_props_p)
{
  VIP_ret_t rs = VIP_OK;
  HH_ret_t  rs_hh;
  HH_hca_hndl_t hh_hndl = HOBKL_get_hh_hndl(mm_hndl->hob_hndl);

  /* Register the region on the driver  */
  rs_hh = HH_register_mr(hh_hndl,mr_props_p, &(mm_mro_p->pub_props.l_key), &(mm_mro_p->pub_props.r_key));
  MTL_DEBUG3(MT_FLFMT("HH register mr returned: %s\n"), HH_strerror_sym(rs_hh));
  if (rs_hh != HH_OK) {
    if (rs_hh == HH_ENODEV )
      rs_hh = HH_EINVAL_HCA_HNDL;
    rs = rs_hh;
  }
  
  MTL_DEBUG3("[get_keys]:HH_register has returned: Rkey: %d Lkey: %d\n",mm_mro_p->pub_props.r_key, mm_mro_p->pub_props.l_key );

  return(rs);
}

/*
 *    MM_smr_get_keys
 */
static VIP_ret_t MM_smr_get_keys(MM_hndl_t  mm_hndl, MM_mro *mro_p,HH_smr_t* smr_props_p)
{
  VIP_ret_t rs = VIP_OK;
  HH_ret_t  rs_hh;
  HH_hca_hndl_t hh_hndl = HOBKL_get_hh_hndl(mm_hndl->hob_hndl);
  VAPI_lkey_t lkey;
  VAPI_rkey_t rkey;

  /* Register the region on the driver  */
  rs_hh = HH_register_smr(hh_hndl,smr_props_p,&lkey,&rkey);
  MTL_DEBUG3(MT_FLFMT("HH register smr returned: %s\n"), HH_strerror_sym(rs_hh));
  if (rs_hh != HH_OK) {
    if (rs_hh == HH_ENODEV )
      rs_hh = HH_EINVAL_HCA_HNDL;
    rs = rs_hh;
  }else{
     mro_p->pub_props.l_key = lkey;
     mro_p->pub_props.r_key = rkey;
  }
   
  MTL_DEBUG3("[get_keys]:HH_register has returned: Rkey: %d Lkey: %d\n",mro_p->pub_props.r_key, mro_p->pub_props.l_key );

  return(rs);
}

/*
 *   MM_update_tables
 */
static VIP_ret_t MM_update_mr_tables(VIP_RSCT_t usr_ctx,MM_hndl_t mm_hndl, MM_mro *mm_mro_p, MM_mrw_hndl_t* mr_hndl_p)
{
  VIP_ret_t rc;
  MM_mrw_hndl_t hndl; 

  /* Get the handle */
  rc = VIP_array_insert(mm_hndl->mr_by_hndl, mm_mro_p, &hndl);
  if ( rc != VIP_OK ) {
    return rc;
  }

  *mr_hndl_p = hndl;
  return VIP_OK;
}


/*
 *
 */
static inline void MM_fill_mr_props(MM_mro* mm_mro_p,MM_VAPI_mro_t* mr_prop_p)
{
  mr_prop_p->acl = mm_mro_p->pub_props.acl;
  mr_prop_p->l_key = mm_mro_p->pub_props.l_key;
  mr_prop_p->r_key = mm_mro_p->pub_props.r_key;
  mr_prop_p->pd_hndl = mm_mro_p->pub_props.pd_hndl;
  mr_prop_p->re_local_end = mm_mro_p->pub_props.re_local_end;
  mr_prop_p->re_local_start = mm_mro_p->pub_props.re_local_start;
  mr_prop_p->re_remote_end = mm_mro_p->pub_props.re_remote_end;
  mr_prop_p->re_remote_start = mm_mro_p->pub_props.re_remote_start;
  mr_prop_p->type = mm_mro_p->pub_props.type;
}

/*
 *  validate_same_pages
 */
VIP_ret_t validate_same_pages(MM_mro* ori_p,MM_mro* new_p)
{
  if (MOSAL_iobuf_cmp_tpt(ori_p->iobuf,new_p->iobuf) != 0) {
    MTL_ERROR1(MT_FLFMT("%s: MOSAL_iobuf_cmp_tpt failed"), __func__);
    return VIP_EINVAL_ADDR;
  }
      
  return VIP_OK;
}
      


VIP_ret_t MMU_get_num_objs(MM_hndl_t mm_hndl,u_int32_t *num_mrs, u_int32_t *num_fmrs,u_int32_t *num_mws )
{
  /* check attributes */
  if ( mm_hndl == NULL || mm_hndl->mr_by_hndl == NULL ||
       mm_hndl->mw_by_hndl == NULL || mm_hndl->fmr_by_hndl == NULL) {
    return VIP_EINVAL_MMU_HNDL;
  }
  
  if (num_mrs == NULL && num_mws == NULL && num_fmrs == NULL) {
      return VIP_EINVAL_PARAM;
  }

  if (num_mrs) {
      *num_mrs = VIP_array_get_num_of_objects(mm_hndl->mr_by_hndl);
  }
  if (num_fmrs) {
      *num_fmrs = VIP_array_get_num_of_objects(mm_hndl->fmr_by_hndl);
  }
  if (num_fmrs) {
      *num_mws = VIP_array_get_num_of_objects(mm_hndl->mw_by_hndl);
  }
  return VIP_OK;
}

