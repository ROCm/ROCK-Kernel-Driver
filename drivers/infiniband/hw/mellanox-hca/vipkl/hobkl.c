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

#include "hobkl.h"
#include <pdm.h>
#include <cqm.h>
#include <mmu.h>
#include <devmm.h>
#include <srqm.h>
#include <qpm.h>
#include <em.h>
#include <hh.h>

/*
struct HOBKL_t {
  VIP_hca_id_t id;
  PDM_hndl_t pdm;  
  CQM_hndl_t cqm;
  MM_hndl_t mm;
  QPM_hndl_t qpm;
  EM_hndl_t em;
  HH_hca_hndl_t hh;
  u_int32_t ref_count;
}; */

/*************************************************************************
 * Function: (static) HOBKL_find 
 *
 * Arguments:
 *  hca_id (IN) - HW HCA id
 *  hh_hndl_p (OUT) - Pointer to HH handle to return
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EINVAL_HCA_ID: invalid HCA ID
 *  VIP_EAGAIN: not enough resources
 *
 * Description:
 *   Finds the HH handle by the give ID
 *
 *************************************************************************/ 
static VIP_ret_t HOBKL_find ( /*IN*/ VIP_hca_id_t hca_id, /*OUT*/ HH_hca_hndl_t *hh_hndl_p)
{
  HH_hca_hndl_t   *hh_list, *hh_iterator;
  u_int32_t       num_entries;
  u_int32_t       i;

  /* get number of entries in the table */
  HH_list_hcas(0, &num_entries, NULL);
  if (num_entries == 0) {
      /* no devices !! */
      *hh_hndl_p = NULL;
      return VIP_EAGAIN;
  }
  hh_list = (HH_hca_hndl_t   *)MALLOC(num_entries * sizeof(HH_hca_hndl_t));
  if (hh_list == NULL) {
      /* no memory !! */
      *hh_hndl_p = NULL;
      return VIP_EAGAIN;
  }

  /* get list of HH handles of all available devices */
  HH_list_hcas(num_entries, &num_entries, hh_list);

  /* search for a device with the desired ID */
  for (i = 0, hh_iterator = hh_list; i < num_entries; i++) {
      if (strncmp((*hh_iterator)->dev_desc, hca_id, sizeof(VIP_hca_id_t)) == 0) 
         break;
      hh_iterator++;
  }

  if (i >= num_entries) {
      *hh_hndl_p = NULL;
      FREE(hh_list);
      return VIP_EINVAL_HCA_ID;
  }

  *hh_hndl_p = *hh_iterator;
  FREE(hh_list);
  return VIP_OK;
}

/*************************************************************************
 * Function: HOBKL_create
 *
 * Arguments:
 *  hca_id (IN) - HW HCA id
 *  hca_hndl (IN) - VAPI handle (for EM)
 *  hobkl_hndl_p (OUT) - Pointer to HOB KL handle to return
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EINVAL_HCA_ID: invalid HCA ID
 *  VIP_EAGAIN: not enough resources
 *
 * Description:
 *   Creates new PDM object for new 	HOB.
 *
 *************************************************************************/ 
VIP_ret_t HOBKL_create ( /*IN*/ VIP_hca_id_t hca_id, 
                         /*IN*/ EVAPI_hca_profile_t  *profile_p,
                         /*OUT*/ EVAPI_hca_profile_t  *sugg_profile_p,
                         /*OUT*/ HOBKL_hndl_t *hobkl_hndl_p)
{
#ifdef QPX_PRF
  u_int32_t		i,j;
#endif
  VIP_ret_t ret=VIP_OK;
  HH_ret_t hh_ret=HH_OK;
  struct HOBKL_t* hob;
  HH_hca_hndl_t hh;
  int int_ret;

  MTL_DEBUG3("CALLED HOBKL_create: name=%s\n", hca_id);
  /* TBD: dynamically assign HH  handle? */

  ret=HOBKL_find(hca_id, &hh);
  if (ret != VIP_OK) goto hh_lbl;

  hh_ret=HH_open_hca(hh, profile_p, sugg_profile_p);
  MTL_DEBUG3("Inside " "%s HH_open_hca returned %d\n", __func__, hh_ret);

  /* TBD: check for return code and return 
   * meaningful error?*/
  ret=(hh_ret == HH_OK)? VIP_OK:
      ((hh_ret < HH_ERROR_MIN)? (VIP_ret_t)hh_ret : VIP_EGEN);

  if (ret != VIP_OK) goto hh_lbl;
  
  hob=(struct HOBKL_t* )MALLOC(sizeof(struct HOBKL_t));
  if (hob == NULL) {
      ret = VIP_EAGAIN;
      goto hob_lbl;
  }

  memset(hob, 0, sizeof(struct HOBKL_t));
  hob->ref_count = 0;

  hob->hh=hh;

  /* TBD: Stub: test HCA ID? */
  strncpy(hob->id,hca_id,sizeof(VIP_hca_id_t));

  /*create the delay unlock object for catastrophic error use */
  MTL_DEBUG3("Inside " "%s Start creating delayed-unlock object\n", __func__);
  int_ret = VIP_delay_unlock_create(&hob->delay_unlocks);
  if (int_ret != 0) {
      MTL_ERROR1("%s: THH_hob_create: could create delay unlock object (err = %d)\n", 
                 __func__, int_ret);
      ret = VIP_EAGAIN;
      goto delay_unlock_create_err;
  }

  MTL_DEBUG3("Inside " "%s Start creating managers\n", __func__);
  ret=PDM_new(hob,&(hob->pdm));
  MTL_DEBUG3("Inside " "%s PDM_new returned %d:%p\n", __func__, ret, hob->pdm);
  if (ret != VIP_OK) goto pdm_lbl;

  ret=DEVMM_new(hob,&(hob->devmm));
  MTL_DEBUG3("Inside " "%s  DEVMM_new returned %d:%p\n", __func__, ret, hob->devmm);
  if (ret != VIP_OK) goto devmm_lbl;


  ret=MM_new(hob, hob->delay_unlocks, &(hob->mm));
  MTL_DEBUG3("Inside " "%s  MM_new returned %d:%p\n", __func__, ret, hob->mm);
  if (ret != VIP_OK) goto mm_lbl;

  ret=CQM_new(hob,&(hob->cqm));
  MTL_DEBUG3("Inside " "%s CQM_new returned %d:%p\n", __func__, ret, hob->cqm);
  if (ret != VIP_OK) goto cqm_lbl;

  ret=SRQM_new(hob,&(hob->srqm));
  MTL_DEBUG3("%s: SRQM_new returned %d:%p\n", __func__, ret, hob->cqm);
  if (ret != VIP_OK) goto srqm_lbl;
  
  ret=QPM_new(hob,&(hob->qpm));
  MTL_DEBUG3("Inside " "%s QPM_new returned %d:%p\n", __func__, ret, hob->qpm);
  if (ret != VIP_OK) goto qpm_lbl;

#ifdef QPX_PRF
  prf_counter_init((&hob->pc_qpm_mod_qp));
  for(i = 0;i < VAPI_ERR;i++) {
	  for(j = 0;j < VAPI_ERR;j++) {
		  prf_counter_init((&(hob->pc_qpm_mod_qp_array[i][j])));
	  }
  }
#endif

  ret=EM_new(hob,&(hob->em));
  MTL_DEBUG3("Inside " "%s EM_new returned %d:%p\n", __func__, ret, hob->em);
  if (ret != VIP_OK) goto em_lbl;

  MTL_DEBUG3("Inside " "%s HOB handle is %p\n", __func__, hob);
  if (hobkl_hndl_p) *hobkl_hndl_p=hob;
 
  return VIP_OK;
/*managers_lbl:*/
  /* Stub: remove all managers here */
  /* Never actually get here until I add
   * more managers */

  EM_delete(hob->em);
em_lbl:
  QPM_delete (hob->qpm);
qpm_lbl:
  SRQM_delete(hob->srqm);
srqm_lbl:
  CQM_delete(hob->cqm);
cqm_lbl:
  MM_delete(hob->mm);
mm_lbl:
  DEVMM_delete(hob->devmm);
devmm_lbl:
  PDM_delete(hob->pdm);
pdm_lbl:
  VIP_delay_unlock_destroy(hob->delay_unlocks);
delay_unlock_create_err:
  FREE(hob);
hob_lbl:
  HH_close_hca(hh);
hh_lbl:
  return ret;
}

VIP_ret_t HOBKL_set_vapi_hca_hndl(/*IN*/HOBKL_hndl_t hob_hndl, /*IN*/VAPI_hca_hndl_t hca_hndl)
{
  VIP_ret_t ret;
  ret= EM_set_vapi_hca_hndl(hob_hndl->em,hca_hndl);
  ret= (ret != VIP_OK) ? ret : CQM_set_vapi_hca_hndl(hob_hndl->cqm,hca_hndl);
  ret= (ret != VIP_OK) ? ret : QPM_set_vapi_hca_hndl(hob_hndl->qpm,hca_hndl);
  return ret;
}

/*************************************************************************
 * Function: HOBKL_destroy
 *
 * Arguments:
 *  hobkl_hndl (IN) - HOB KL object to destroy
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EINVAL_hobkl_hndl
 *  VIP_EPERM
 *
 * Description:
 *   Cleanup resources of given HOB KL object
 *
 *************************************************************************/ 
VIP_ret_t HOBKL_destroy ( /*IN*/ HOBKL_hndl_t hca_hndl )
{
#ifdef QPX_PRF
  u_int32_t		i,j;
#endif
  HH_hca_hndl_t hh;
  MTL_DEBUG3("CALLED " "%s(%p);\n", __func__, hca_hndl);
  hh=hca_hndl->hh;

  /* EM must be destroyed first, in order to stop performing reverse lookups due to events */
  MTL_DEBUG3("Inside " "%s: calling EM_delete:%p\n", __func__, hca_hndl->em);
  EM_delete (hca_hndl->em);
  MTL_DEBUG3("Inside " "%s: calling QPM_delete:%p\n", __func__, hca_hndl->qpm);
  QPM_delete (hca_hndl->qpm);
  MTL_DEBUG3("%s: calling SRQM_delete:%p\n", __func__, hca_hndl->srqm);
  SRQM_delete(hca_hndl->srqm);
  MTL_DEBUG3("Inside " "%s: calling CQM_delete:%p\n", __func__, hca_hndl->cqm);
  CQM_delete(hca_hndl->cqm);
  MTL_DEBUG3("Inside " "%s: calling  MM_delete:%p\n", __func__, hca_hndl->mm);
  MM_delete(hca_hndl->mm);
  MTL_DEBUG3("Inside " "%s: calling  DEVMM_delete:%p\n", __func__, hca_hndl->devmm);
  DEVMM_delete(hca_hndl->devmm);
  MTL_DEBUG3("Inside " "%s: calling PDM_delete:%p\n", __func__, hca_hndl->pdm);
  PDM_delete(hca_hndl->pdm);
  
#ifdef QPX_PRF
  for(i = 0;i < VAPI_ERR;i++) {
	  for(j = 0;j < VAPI_ERR;j++) {
		MTL_ERROR1("%s: qpx_prf: modify_qp from %s to %s counter reports delta/count ratio of %d/%d\n", __func__,
				   VAPI_qp_state_sym(i),
				   VAPI_qp_state_sym(j),
				   (u_int32_t) hca_hndl->pc_qpm_mod_qp_array[i][j].delta,
				   (u_int32_t) hca_hndl->pc_qpm_mod_qp_array[i][j].test_count);
		MTL_ERROR1("%s: at %lluhz clock.\n", __func__,
				   hca_hndl->pc_qpm_mod_qp_array[i][j].cpu_CLCKS_PER_SECS);
	  }
  }
#endif


  MTL_DEBUG3("Inside " "%s: calling HH_close_hca:%p\n", __func__, (void *) hh);
  HH_close_hca(hh);

  /* destroy the delayed-unlock object.  MUST be following close_HCA, in case
   * a catastrophic-error reset is performed in the hh_close_hca.
    */
  VIP_delay_unlock_destroy(hca_hndl->delay_unlocks);

  FREE(hca_hndl);
  return VIP_OK;
}


/*************************************************************************
 * Function: HOBKL_query_cap
 *
 * Arguments:
 *  hobkl_hndl (IN)
 *  caps (OUT) - fill HCA capabilities here
 *
 * Returns:	
 *  VIP_OK: Operation completed successfully.
 *  VIP_EINVAL_hobkl_hndl: Invalid HOB KL handle.
 *
 * Description:
 *   Query HCA associated with this HOB.
 *   Only number of ports is returned.
 *   Use additional APIs to query specific ports.
 *
 *************************************************************************/ 
VIP_ret_t HOBKL_query_cap(HOBKL_hndl_t hobkl_hndl, VAPI_hca_cap_t* caps)
{
  HH_ret_t ret=HH_query_hca(hobkl_hndl->hh, caps);
  return ret;
  /* TBD: fix return codes */
  /*if (ret != HH_OK) return VIP_EINVAL_HCA_HNDL;
  return VIP_OK;*/
}

/*************************************************************************
 * Function: HOBKL_query_port_prop
 *
 * Arguments:
 *  hobkl_hndl (IN)
 *  port_num (IN) number of the port to query
 *  hobkl_port_p (OUT) - fill HCA capabilities here
 *
 * Returns:	
 *  VIP_OK: Operation completed successfully.
 *  VIP_EINVAL_hobkl_hndl: Invalid HOB KL handle.
 *  VIP_EINVAL_PORT_NUM: Invalid port number
 *
 * Description:
 *   Query HCA port associated with this HOB.
 *   Only number of GIDs/ PKs is returned.
 *
 *************************************************************************/ 
VIP_ret_t HOBKL_query_port_prop(
    /*IN*/      HOBKL_hndl_t      hobkl_hndl,
    /*IN*/      IB_port_t           port_num,
    /*OUT*/     VAPI_hca_port_t     *hobkl_port_p
)
{
  return HH_query_port_prop(hobkl_hndl->hh, port_num, hobkl_port_p);
}

/*************************************************************************
 * Function: HOBKL_query_port_pkey_tbl
 *
 * Arguments:
 *  hobkl_hndl (IN)
 *  port_num (IN) - number of the port to query
 *  tbl_len_in (IN) - size of table allocated
 *  tbl_len_out_p (OUT) return actual number of entries filled here
 *  pkey_tbl_p (OUT) - fill this PK table
 *
 * Returns:	
 *  VIP_OK: Operation completed successfully.
 *  VIP_EINVAL_HOB_HNDL: Invalid HOB KL handle.
 *  VIP_EINVAL_PORT_NUM: Invalid port number
 *  VIP_EAGAIN:  tbl_len_out > tbl_len_in
 *
 * Description:
 *   Query HCA port associated with this HOB.
 *   Sufficient memory must be allocated by the user
 *   beforehand. It is possible to use HOBKL_query_port_prop
 *   to find out the amount of memory that is necessary.
 *
 *************************************************************************/ 
VIP_ret_t HOBKL_query_port_pkey_tbl(
    /*IN*/  HOBKL_hndl_t     hobkl_hndl,
    /*IN*/  IB_port_t        port_num,
    /*IN*/  u_int16_t        tbl_len_in,
    /*OUT*/ u_int16_t        *tbl_len_out_p,
    /*OUT*/ VAPI_pkey_t      *pkey_tbl_p
)
{
  
  HH_ret_t ret=HH_get_pkey_tbl(hobkl_hndl->hh, port_num, tbl_len_in, tbl_len_out_p,pkey_tbl_p);
  return ret;
}

/*************************************************************************
 * Function: HOBKL_query_port_gid_tbl
 *
 * Arguments:
 *  hobkl_hndl (IN)
 *  port_num (IN) - number of the port to query
 *  tbl_len_in (IN) - size of table allocated
 *  tbl_len_out_p (OUT) return actual number of entries filled here
 *  gid_tbl_p (OUT) - fill this PK table
 *
 * Returns:	
 *  VIP_OK: Operation completed successfully.
 *  VIP_EINVAL_HOB_HNDL: Invalid HOB KL handle.
 *  VIP_EINVAL_PORT_NUM: Invalid port number
 *  VIP_EAGAIN:  tbl_len_out > tbl_len_in
 *
 * Description:
 *   Query HCA port associated with this HOB.
 *   Sufficient memory must be allocated by the user
 *   beforehand. It is possible to use HOBKL_query_port_prop
 *   to find out the amount of memory that is necessary.
 *
 *************************************************************************/ 
VIP_ret_t HOBKL_query_port_gid_tbl(
    /*IN*/  HOBKL_hndl_t     hobkl_hndl,
    /*IN*/  IB_port_t        port_num,
    /*IN*/  u_int16_t        tbl_len_in,
    /*OUT*/ u_int16_t        *tbl_len_out_p,
    /*OUT*/ VAPI_gid_t      *gid_tbl_p
)
{
  HH_ret_t ret = HH_get_gid_tbl(hobkl_hndl->hh, port_num, tbl_len_in, tbl_len_out_p, gid_tbl_p);
  return ret;
}
/*************************************************************************
 * Function: HOBKL_process_local_mad
 *
 * Arguments:
 *  hobkl_hndl (IN)
 *  port_num (IN) - number of the port to which to submit MAD packet
 *  slid : Source LID of incoming MAD. Required Mkey violation trap genenration.
 *         (this parameter is ignored if EVAPI_MAD_IGNORE_MKEY flag is set)
 *  proc_mad_opts (IN) -- non-default processing options
 *  mad_in_p (IN) - mad input packet
 *  mad_out_p (OUT)  - mad packet returned by adapter
 *
 * Returns:	
 *  VIP_OK: Operation completed successfully.
 *  VIP_EINVAL_HOB_HNDL: Invalid HOB KL handle.
 *  VIP_EINVAL_PORT_NUM: Invalid port number
 *  VIP_EAGAIN:  
 *
 * Description:
 *   Submits a MAD packet to the channel adapter for processing,
 *   and returns a mad packet for replying to source host when needed.
 *************************************************************************/ 
VIP_ret_t HOBKL_process_local_mad(
    /*IN*/  HOBKL_hndl_t         hobkl_hndl,
    /*IN*/  IB_port_t            port_num,
    /*IN*/  IB_lid_t              slid, /* ignored on EVAPI_MAD_IGNORE_MKEY */
    /*IN*/  EVAPI_proc_mad_opt_t proc_mad_opts,
    /*IN*/  void *               mad_in_p,
    /*OUT*/ void *               mad_out_p
)
{
  
  HH_ret_t ret=HH_process_local_mad(hobkl_hndl->hh, port_num, slid, proc_mad_opts, mad_in_p, mad_out_p);
  return ret;
}


VAPI_ret_t HOBKL_modify_hca_attr(
    /*IN*/  HOBKL_hndl_t            hobkl_hndl,
    /*IN*/  IB_port_t               port_num,
    /*IN*/  VAPI_hca_attr_t         *hca_attr_p,
    /*IN*/  VAPI_hca_attr_mask_t    *hca_attr_mask_p
)
{
  HH_ret_t ret=HH_modify_hca(hobkl_hndl->hh, port_num, hca_attr_p, hca_attr_mask_p);
  return ret;
}


VIP_ret_t HOBKL_get_hca_ul_info(HOBKL_hndl_t hca_hndl, HH_hca_dev_t *hca_ul_info_p)
{
  HH_hca_hndl_t hh_hndl;
  memset(hca_ul_info_p, 0, sizeof(HH_hca_dev_t));

  hh_hndl = hca_hndl->hh;

  hca_ul_info_p->vendor_id = hh_hndl->vendor_id;
  hca_ul_info_p->dev_id = hh_hndl->dev_id;
  hca_ul_info_p->hw_ver = hh_hndl->hw_ver;
  hca_ul_info_p->hca_ul_resources_sz = hh_hndl->hca_ul_resources_sz;
  hca_ul_info_p->cq_ul_resources_sz = hh_hndl->cq_ul_resources_sz;
  hca_ul_info_p->srq_ul_resources_sz = hh_hndl->srq_ul_resources_sz;
  hca_ul_info_p->qp_ul_resources_sz = hh_hndl->qp_ul_resources_sz;
  hca_ul_info_p->pd_ul_resources_sz = hh_hndl->pd_ul_resources_sz;
  hca_ul_info_p->status = hh_hndl->status;
  return VIP_OK;
}

VIP_ret_t HOBKL_get_hca_id(HOBKL_hndl_t hca_hndl, VIP_hca_id_t* hca_id_p)
{
  if (hca_id_p) memcpy(*hca_id_p,hca_hndl->id,sizeof(VIP_hca_id_t));
  return VIP_OK;
}

VIP_ret_t HOBKL_alloc_ul_resources(HOBKL_hndl_t hca_hndl, 
                                   MOSAL_protection_ctx_t usr_prot_ctx,
                                   void * hca_ul_resources_p,
                                   EM_async_ctx_hndl_t *async_hndl_ctx_p)
{
  VIP_ret_t ret = VIP_OK;
  HH_hca_hndl_t hh_hndl;
  hh_hndl = hca_hndl->hh;
  
  ret = EM_alloc_async_handler_ctx(hca_hndl->em, async_hndl_ctx_p);

  
  if (ret != VIP_OK) {
  MTL_ERROR1("%s: EM_alloc_async_handler_ctx failed: ret=%d\n",__func__, ret);
  }                  

  return (HH_alloc_ul_resources(hh_hndl, usr_prot_ctx, hca_ul_resources_p));
}

VIP_ret_t HOBKL_free_ul_resources(HOBKL_hndl_t hca_hndl, 
                                  void * hca_ul_resources_p,
                                  EM_async_ctx_hndl_t async_hndl_ctx)
{
  VIP_ret_t ret = VIP_OK;
  HH_hca_hndl_t hh_hndl;
  hh_hndl = hca_hndl->hh;
  
  ret = EM_dealloc_async_handler_ctx(hca_hndl->em, async_hndl_ctx);
  if (ret != VIP_OK) {
    MTL_ERROR1("%s: EM_dealloc_async_handler_ctx failed: ret=%d\n",__func__, ret);
  } 
  return (HH_free_ul_resources(hh_hndl, hca_ul_resources_p));
}       


