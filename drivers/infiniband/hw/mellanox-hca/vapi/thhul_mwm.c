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

#define C_THHUL_MWM_C

#include <hh.h>
#include <hhul.h>
#include <thhul_hob.h>
#include <thhul_qpm.h>
#include <thhul_mwm.h>

typedef struct mwm_ul_ctx_st {
	/* TD: change the name.*/
	IB_rkey_t				key;
	struct mwm_ul_ctx_st	*next_p;
	struct mwm_ul_ctx_st	*back_p;
} mwm_ul_ctx;

typedef struct THHUL_mwm_st {
	// TD: next member possibly redundant:
	u_int32_t		log2_mpt_size;
	MOSAL_mutex_t   mtx;
	mwm_ul_ctx		*head_p;
} tmwm_t;

HH_ret_t THHUL_mwm_create
( 
  /*IN*/ THHUL_hob_t 	hob, 
  /*IN*/ u_int32_t		log2_mpt_size,
  /*OUT*/ THHUL_mwm_t 	*mwm_p 
) 
{ 
	/*
	HH_ret_t				rc;
	HHUL_hca_hndl_t			hca;
	THH_hca_ul_resources_t	hca_ul_res;
	*/
	THHUL_mwm_t mwm;
	
	FUNC_IN;
	
	mwm = TMALLOC(tmwm_t);

	if( mwm == NULL )
	{		  
		MTL_ERROR1("%s mwm malloc failed\n", __func__);
		return HH_EAGAIN;
	}

	/* change to THHUL_hob_get_hca_ul_hob_handle()*/ 
	/*
	if( (rc = THHUL_hob_get_hca_ul_handle(hob,&hca)) != HH_OK )
	{
		MTL_ERROR1("%sTHHUL_hob_get_hca_ul_handle() failed, ret=%d\n", __func__,rc);		
		return rc;
	}
	
    if( (rc = THHUL_hob_get_hca_ul_res(hca,&hca_ul_res)) != HH_OK )
	{
		MTL_ERROR1("%sTHHUL_hob_get_hca_ul_res() failed, ret=%d\n", __func__,rc);		
		return rc;
	}
	*/
	mwm->log2_mpt_size = log2_mpt_size;
	mwm->head_p = NULL;
	MOSAL_mutex_init(&(mwm->mtx));
	
	*mwm_p = mwm;
	FUNC_OUT;
	
	return HH_OK;
}


HH_ret_t THHUL_mwm_destroy
( 
	/*IN*/ THHUL_mwm_t mwm 
) 
{ 
	mwm_ul_ctx *cur_mw_p;
    
    while (mwm->head_p) {  
        cur_mw_p= mwm->head_p;
        mwm->head_p = cur_mw_p->next_p;
        FREE(cur_mw_p);
    }

    MOSAL_mutex_free(&(mwm->mtx));
    FREE(mwm);
	MT_RETURN(HH_OK);   
}


HH_ret_t THHUL_mwm_alloc_mw
(
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*IN*/ IB_rkey_t initial_rkey,
  /*OUT*/ HHUL_mw_hndl_t*  mw_p
) 
{ 
	THHUL_mwm_t		mwm;
#ifdef THHUL_MWM_DEBUG_LIST
  mwm_ul_ctx *cur_mw_p;
#endif

	FUNC_IN;
	
	if( ( THHUL_hob_get_mwm(hca,&mwm) != HH_OK ) || ( mwm == NULL ) )
	{
		MTL_ERROR1(MT_FLFMT("Error while retrieving mwm handle.\n"));
		return HH_EINVAL;
	}

	*mw_p = MALLOC(sizeof(mwm_ul_ctx));
	if( mw_p == NULL ) {
		MTL_ERROR1("%sallocation failed.\n", __func__);
		return HH_EAGAIN;
	}

	if (MOSAL_mutex_acq(&mwm->mtx,TRUE) != MT_OK)  {
    FREE(mw_p);
    return HH_EINTR;
  }
	
	if( mwm->head_p )
		mwm->head_p->back_p = (struct mwm_ul_ctx_st *)*mw_p;
	((mwm_ul_ctx *) *(mw_p))->next_p 	= mwm->head_p;
	((mwm_ul_ctx *) *(mw_p))->back_p	= NULL;
	((mwm_ul_ctx *) *(mw_p))->key		= initial_rkey;

	mwm->head_p = (struct mwm_ul_ctx_st *)*mw_p;
	
#ifdef THHUL_MWM_DEBUG_LIST
  MTL_DEBUG5(MT_FLFMT("List check/dump:"));
  cur_mw_p= mwm->head_p;
  while (cur_mw_p) {  /* Verify list consistancy */
    MTL_DEBUG5(MT_FLFMT("Rkey=0x%X"),cur_mw_p->key);
    /* verify next point back to current */
    if ((cur_mw_p->next_p) && (cur_mw_p->next_p->back_p != cur_mw_p)) { 
      MTL_ERROR1(MT_FLFMT("Linked list is found to be inconsistant"));
      MOSAL_mutex_rel(&mwm->mtx);
      return HH_EINVAL;
    }
    cur_mw_p= cur_mw_p->next_p;
  }
  cur_mw_p= mwm->head_p;
  while (cur_mw_p) {  /* Scan list to assure given handle is in the list */
    if (cur_mw_p == *mw_p)  break;
    cur_mw_p= cur_mw_p->next_p;
  }
  if (cur_mw_p == NULL) {
    MTL_ERROR1(MT_FLFMT("New memory windows is not found in the list)"));
    MOSAL_mutex_rel(&mwm->mtx);
		return HH_EINVAL;
  }
#endif
	
  MOSAL_mutex_rel(&mwm->mtx);

	FUNC_OUT;	
	
	return HH_OK;; 
}

HH_ret_t THHUL_mwm_bind_mw
(
  /*IN*/ HHUL_hca_hndl_t   hhul_hndl,
  /*IN*/ HHUL_mw_hndl_t    mw,
  /*IN*/ HHUL_mw_bind_t*   bind_prop_p,
  /*OUT*/ IB_rkey_t*        bind_rkey_p
) 
{ 
	u_int32_t	rc,new_key;
	THHUL_mwm_t	mwm;

	FUNC_IN;

	MTL_DEBUG1("%s - dump of bind req:\n", __func__);
	MTL_DEBUG1("{\n");
	MTL_DEBUG1("init r_key: 0x%x.\n",((mwm_ul_ctx *) mw)->key);
	MTL_DEBUG1("acl: 0x%x.\n",bind_prop_p->acl);
	MTL_DEBUG1("comp_type: 0x%x.\n",bind_prop_p->comp_type);
	MTL_DEBUG1("id: 0x%x.\n",(u_int32_t) bind_prop_p->id);
	MTL_DEBUG1("lkey: 0x%x.\n",bind_prop_p->mr_lkey);
	MTL_DEBUG1("qp: " MT_ULONG_PTR_FMT ".\n",(MT_ulong_ptr_t) bind_prop_p->qp);
	MTL_DEBUG1("start: 0x%x:%x.\n",(u_int32_t) (bind_prop_p->start >> 32),(u_int32_t) bind_prop_p->start);
	MTL_DEBUG1("size: 0x%x:%x.\n",(u_int32_t) (bind_prop_p->size >> 32),(u_int32_t) bind_prop_p->size);
	MTL_DEBUG1("}\n");

	if( ( THHUL_hob_get_mwm(hhul_hndl,&mwm) != HH_OK ) || ( mwm == NULL ) )
	{
		MTL_ERROR1(MT_FLFMT("Error while retrieving mwm handle.\n"));
		return HH_EINVAL;
	}

	// req to bind a window to zero len is in fact an unbind req.
	// if unbunding, window e_key remains the same.
	// if binding, new r_key tag is the previous tag incremented by 1:
	new_key = ((mwm_ul_ctx *) mw)->key;
	/* TD: conventions */
	if( bind_prop_p->size > 0 ) { 
		new_key += (1 << mwm->log2_mpt_size);
	}
	
	if( (rc = THHUL_qpm_post_bind_req(bind_prop_p,new_key)) != HH_OK ) {
		MTL_ERROR1("%s failed to post bind descriptor.\n", __func__);
		return rc;
	}

	((mwm_ul_ctx *) mw)->key = new_key;
	*bind_rkey_p =  new_key;
	
	FUNC_OUT;
	
	return HH_OK; 
}


HH_ret_t THHUL_mwm_free_mw
(
  /*IN*/ HHUL_hca_hndl_t  hhul_hndl,
  /*IN*/ HHUL_mw_hndl_t   mw
) 
{ 
	THHUL_mwm_t				mwm;
#ifdef THHUL_MWM_DEBUG_LIST
  mwm_ul_ctx *cur_mw_p;
#endif

	FUNC_IN;
	
	if( ( THHUL_hob_get_mwm(hhul_hndl,&mwm) != HH_OK ) || ( mwm == NULL ) )
	{
		MTL_ERROR1(MT_FLFMT("Error while retrieving mwm handle.\n"));
		return HH_EINVAL;
	}
	
	MOSAL_mutex_acq_ui(&mwm->mtx);
	
#ifdef THHUL_MWM_DEBUG_LIST
  MTL_DEBUG5(MT_FLFMT("List check/dump (removal of Rkey=0x%X):"),((mwm_ul_ctx*)mw)->key);
  cur_mw_p= mwm->head_p;
  while (cur_mw_p) {  /* Verify list consistancy */
    MTL_DEBUG5(MT_FLFMT("Rkey=0x%X"),cur_mw_p->key);
    /* verify next point back to current */
    if ((cur_mw_p->next_p) && (cur_mw_p->next_p->back_p != cur_mw_p)) { 
      MTL_ERROR1(MT_FLFMT("Linked list is found to be inconsistant"));
      MOSAL_mutex_rel(&mwm->mtx);
      return HH_EINVAL;
    }
    cur_mw_p= cur_mw_p->next_p;
  }
  cur_mw_p= mwm->head_p;
  while (cur_mw_p) {  /* Scan list to assure given handle is in the list */
    if (cur_mw_p == mw)  break;
    cur_mw_p= cur_mw_p->next_p;
  }
  if (cur_mw_p == NULL) {
    MTL_ERROR1(MT_FLFMT("Given memory window handle %p is unknown (not in list)"),
      (mwm_ul_ctx *) mw);
    MOSAL_mutex_rel(&mwm->mtx);
		return HH_EINVAL;
  }
#endif
	// window list is empty:
	if( mwm->head_p == NULL )
	{
    MOSAL_mutex_rel(&mwm->mtx);
		return HH_EINVAL;
	}

	// single window in the list:
	if( mwm->head_p->next_p == NULL )
	{
		mwm->head_p = NULL;
		goto	mwm_free_mw_exit;
	}

	// unlink from previous entry:
	if( ((mwm_ul_ctx *) mw)->back_p )
	{
		((mwm_ul_ctx *) mw)->back_p->next_p = ((mwm_ul_ctx *) mw)->next_p;
	} else {  /* Removing first - Make next (if any) the first */
    mwm->head_p= ((mwm_ul_ctx *) mw)->next_p;
  }

	// unlink from next entry:
	if( ((mwm_ul_ctx *) mw)->next_p )
	{
		((mwm_ul_ctx *) mw)->next_p->back_p = ((mwm_ul_ctx *) mw)->back_p;
	}

mwm_free_mw_exit:
	MOSAL_mutex_rel(&mwm->mtx);
	FREE(mw);
	
	FUNC_OUT;
	
	return HH_OK; 
}

