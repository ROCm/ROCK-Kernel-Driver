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

#include "thhul_pdm_priv.h"

#define MT_FORK_SUPPORT

HH_ret_t THHUL_pdm_create (
   THHUL_hob_t hob, 
   MT_bool priv_ud_av, 
   THHUL_pdm_t *pdm_p )
{
    THHUL_pdm_t    new_pdm_obj;
    HH_ret_t       ret;
    VIP_common_ret_t  vret;
    /* create new pdm object */
    new_pdm_obj = TMALLOC(struct THHUL_pdm_st);
    if (!new_pdm_obj) {
        return HH_EINVAL;
    }
    
    memset(new_pdm_obj, 0, sizeof(struct THHUL_pdm_st));

    ret =  THHUL_hob_query_version( hob, &(new_pdm_obj->version)); 
    if (ret != HH_OK) {
        MTL_ERROR1("THHUL_pdm_create: ERROR (%d) : could not get version info\n", ret);
        FREE(new_pdm_obj);
        return HH_EINVAL;
    }

    new_pdm_obj->priv_ud_av = priv_ud_av;
    vret = VIP_array_create(THHUL_PDM_INITIAL_NUM_PDS,&(new_pdm_obj->pd_array));
    if (vret != VIP_OK) {
        MTL_ERROR1("THHUL_pdm_create: ERROR (%d) : could not create PD array\n", vret);
        FREE(new_pdm_obj);
        return HH_EAGAIN;
    }

    *pdm_p = new_pdm_obj;
    return HH_OK;
}

HH_ret_t THHUL_pdm_destroy ( THHUL_pdm_t pdm)
{
    THHUL_pd_t   *ul_pd;
    HH_ret_t     ret;
    THH_udavm_t  udavm;
    VIP_common_ret_t vret;
    VIP_array_handle_t pd_h;


    VIP_ARRAY_FOREACH(pdm->pd_array,vret,pd_h,(VIP_array_obj_t*)&ul_pd)
    {
        udavm = ul_pd->udavm;
        /* For non-privileged UDAVs, destroy the UDAV user-level object allocated for this PD */
        if (!(pdm->priv_ud_av)) {
            /*udavm can legally be null if THHUL_pdm_alloc_pd_done was not called, due to 
             * failure of THH_pdm_alloc_pd 
             */
            if (udavm != NULL) {
                ret = THH_udavm_destroy(udavm);
                if (ret != HH_OK) {
                    MTL_ERROR1("THHUL_pdm_free_pd_done: ERROR (%d) : Could not destroy associated UDAV object\n", ret);
                    /* continue, to free up the ul_pd anyway., and report successful 'free' */
                }
            }
    
            /* If udav was not allocated in DDR, free the allocated memory here */
            if (ul_pd->udav_nonddr_table != (MT_virt_addr_t) 0) {
                MOSAL_pci_virt_free_consistent((void *)ul_pd->udav_nonddr_table, ul_pd->uadv_nonddr_table_alloc_size);
                ul_pd->udav_nonddr_table = (MT_virt_addr_t) 0;
                ul_pd->udav_nonddr_table_aligned = (MT_virt_addr_t) 0;
                ul_pd->uadv_nonddr_table_alloc_size = 0;
            }
        }
        ul_pd->valid = FALSE;  /* just in case OS does not detect heap errors, and does not zero entries */
        FREE(ul_pd);
    }
    
    VIP_array_destroy(pdm->pd_array,NULL);
    FREE(pdm);
    return HH_OK;
}

HH_ret_t THHUL_pdm_alloc_pd_avs_prep (
   HHUL_hca_hndl_t hca, 
   u_int32_t max_num_avs,
   HH_pdm_pd_flags_t pd_flags,
   HHUL_pd_hndl_t *pd_p, 
   void *pd_ul_resources_p )
{
    HH_ret_t              ret;
    THHUL_pdm_t           pdm;
    THHUL_pd_t            *new_pd_p;
    THH_pd_ul_resources_t *pd_ul_res = (THH_pd_ul_resources_t *)pd_ul_resources_p;
    MT_size_t             ud_av_table_sz = 0;
    VIP_common_ret_t      vret;
    VIP_array_handle_t    local_pd_hndl;

    ret = THHUL_hob_get_pdm(hca, &pdm);
    if (ret != HH_OK) {
        MTL_ERROR1("THHUL_pdm_alloc_pd_avs_prep: ERROR (%d) : PDM object has not yet been created\n", ret);
        return HH_EINVAL;
    }

    new_pd_p = TMALLOC(THHUL_pd_t);
    memset(new_pd_p, 0, sizeof(THHUL_pd_t));
    memset(pd_ul_res, 0, sizeof(THH_pd_ul_resources_t));

    /* first, see if need to create a UDAV table (non-priv mode) */
    if (!pdm->priv_ud_av) {

        if (max_num_avs == 0) {
            MTL_ERROR1("THHUL_pdm_alloc_pd_avs_prep: max_num_avs requested cannot be zero.\n");	
            FREE(new_pd_p);
            return HH_EINVAL;
        }
        
        if (max_num_avs == EVAPI_DEFAULT_AVS_PER_PD) {
            max_num_avs = THHUL_PDM_MAX_UL_UDAV_PER_PD;
            MTL_DEBUG4("THHUL_pdm_alloc_pd_avs_prep: using default AVs per PD (=%u)\n", max_num_avs);	
        }
    	/* guarantee that table size is a multiple of page size.   */
        ud_av_table_sz = max_num_avs * (sizeof(struct tavorprm_ud_address_vector_st) / 8);
#if !defined(__KERNEL__) && defined(MT_FORK_SUPPORT)
        /* Add 1 page for page alignment + one page to cover last page */
        new_pd_p->uadv_nonddr_table_alloc_size = 
          MT_UP_ALIGNX_SIZE(ud_av_table_sz, MOSAL_SYS_PAGE_SHIFT) + MOSAL_SYS_PAGE_SIZE - 1;
#else        
      /* malloc an extra udav entry to use for table-start alignment purposes */
        new_pd_p->uadv_nonddr_table_alloc_size = ud_av_table_sz + 
                                 (1<<ceil_log2(sizeof(struct tavorprm_ud_address_vector_st) / 8));
#endif
        
        MTL_DEBUG4("THHUL_pdm_alloc_pd_avs_prep: ud_av_table_sz = "SIZE_T_FMT", pd_flags=0x%x\n", 
                   ud_av_table_sz, pd_flags);	
        new_pd_p->udav_nonddr_table = 
            (MT_virt_addr_t) MOSAL_pci_virt_alloc_consistent(new_pd_p->uadv_nonddr_table_alloc_size,
                                                             ceil_log2(sizeof(struct tavorprm_ud_address_vector_st) / 8) );

        if (new_pd_p->udav_nonddr_table == VA_NULL ) {
            MTL_ERROR1("THHUL_pdm_alloc_pd_avs_prep: ERROR : Could not Vmalloc UDAV table\n");
            ret = HH_ENOMEM;
            goto thh_pdm_udavm_create_err;
        }
        memset((void *)new_pd_p->udav_nonddr_table, 0, new_pd_p->uadv_nonddr_table_alloc_size);
        
#if !defined(__KERNEL__) && defined(MT_FORK_SUPPORT)
        new_pd_p->udav_nonddr_table_aligned   = /* Align to page start */
          MT_UP_ALIGNX_VIRT((new_pd_p->udav_nonddr_table), MOSAL_SYS_PAGE_SHIFT);
#else
        /* now, align the buffer to the entry size */
        new_pd_p->udav_nonddr_table_aligned   = 
                                MT_UP_ALIGNX_VIRT((new_pd_p->udav_nonddr_table), 
                                ceil_log2((sizeof(struct tavorprm_ud_address_vector_st) / 8)));
#endif
    }

    /* add to array */
    if ((vret=VIP_array_insert(pdm->pd_array, (VIP_array_obj_t)new_pd_p, &local_pd_hndl)) != VIP_OK) {
        MTL_ERROR1("THHUL_pdm_alloc_pd_avs_prep: ERROR (%d) : Insertion failure.\n", vret);
        if (!pdm->priv_ud_av) {
            MOSAL_pci_virt_free_consistent((void *)new_pd_p->udav_nonddr_table, new_pd_p->uadv_nonddr_table_alloc_size);
        }
        FREE(new_pd_p);
        return HH_EAGAIN;
    }

    pd_ul_res->udavm_buf = new_pd_p->udav_nonddr_table_aligned;
    pd_ul_res->udavm_buf_sz = ud_av_table_sz;
    pd_ul_res->pd_flags = pd_flags;
    *pd_p = new_pd_p->hhul_pd_hndl = (HHUL_pd_hndl_t)local_pd_hndl;  /* return allocated PD handle */
    return HH_OK;

thh_pdm_udavm_create_err:
    FREE(new_pd_p);
    return ret;

}

HH_ret_t THHUL_pdm_alloc_pd_prep (
   HHUL_hca_hndl_t hca, 
   HHUL_pd_hndl_t *pd_p, 
   void *pd_ul_resources_p )
{
  return THHUL_pdm_alloc_pd_avs_prep(hca, 256, PD_NO_FLAGS, pd_p, pd_ul_resources_p);
}


HH_ret_t THHUL_pdm_alloc_pd_done (
   HHUL_hca_hndl_t hca, 
   HHUL_pd_hndl_t hhul_pd, 
   HH_pd_hndl_t hh_pd, 
   void  *pd_ul_resources_p )
{
    HH_ret_t     ret;
    THHUL_pdm_t  pdm;
    THH_pd_ul_resources_t *pd_ul_res = (THH_pd_ul_resources_t *)pd_ul_resources_p;
    THHUL_pd_t   *ul_pd;
    VIP_common_ret_t vret;

    MTL_DEBUG3("==> THHUL_pdm_alloc_pd_done\n");

    ret = THHUL_hob_get_pdm(hca, &pdm);
    if (ret != HH_OK) {
        MTL_ERROR1("THHUL_pdm_alloc_pd_done: ERROR (%d) : PDM object has not yet been created\n", ret);
        return HH_EINVAL;
    }

    if ((vret=VIP_array_find_hold(pdm->pd_array,(VIP_array_handle_t)hhul_pd,
                                  (VIP_array_obj_t *)&ul_pd)) != VIP_OK) {
        if (vret == VIP_EBUSY) { 
            MTL_DEBUG4("THHUL_pdm_alloc_pd_done:  PD object is busy\n");
            return HH_EBUSY;
        } else {
            MTL_ERROR1("THHUL_pdm_alloc_pd_done: ERROR (%d) : Could not find PD object\n", vret);
            return HH_EINVAL_PD_HNDL;
        }
    }
    ul_pd->hh_pd_hndl = hh_pd;

    if (!pdm->priv_ud_av) {
        if (pd_ul_res->udavm_buf != ul_pd->udav_nonddr_table_aligned) {
            MTL_DEBUG3("THHUL_pdm_alloc_pd_done. USING DDR MEMORY.udavm_buf="
                       VIRT_ADDR_FMT", nonddr_table="VIRT_ADDR_FMT", extra=%d\n",
                       pd_ul_res->udavm_buf, ul_pd->udav_nonddr_table,
                       (int)(sizeof(struct tavorprm_ud_address_vector_st) / 8) );
            MOSAL_pci_virt_free_consistent((void *)ul_pd->udav_nonddr_table, ul_pd->uadv_nonddr_table_alloc_size);
            ul_pd->udav_nonddr_table = (MT_virt_addr_t) 0;
            ul_pd->udav_nonddr_table_aligned = (MT_virt_addr_t) 0;
            ul_pd->uadv_nonddr_table_alloc_size = 0;
        } else {
            MTL_DEBUG3("THHUL_pdm_alloc_pd_done. USING HOST MEMORY.udavm_buf (aligned) ="
                       VIRT_ADDR_FMT", non-aligned nonddr_table="VIRT_ADDR_FMT"\n",
                       pd_ul_res->udavm_buf, ul_pd->udav_nonddr_table);
        }
        ret = THH_udavm_create( &(pdm->version), pd_ul_res->udavm_buf_memkey,
                                pd_ul_res->udavm_buf,
                                pd_ul_res->udavm_buf_sz,
                                &(ul_pd->udavm));
        if (ret != HH_OK) {
            MTL_ERROR1("THHUL_pdm_alloc_pd_done: ERROR (%d) : Could not create UDAV manager object\n", ret);
            MTL_DEBUG4("<== THHUL_pdm_alloc_pd_done. ERROR\n");
            VIP_array_find_release(pdm->pd_array,hhul_pd);
            return ret;
        }
    }

    /* save the memory key in all cases, for use by THHUL_qpm */
    ul_pd->lkey = pd_ul_res->udavm_buf_memkey;
    ul_pd->valid = TRUE;
    VIP_array_find_release(pdm->pd_array,hhul_pd);
    MTL_DEBUG3("<== THHUL_pdm_alloc_pd_done\n");
    return HH_OK;
}


HH_ret_t THHUL_pdm_free_pd_prep (
   HHUL_hca_hndl_t hca, 
   HHUL_pd_hndl_t hhul_pd,
   MT_bool        undo_flag )
{
    HH_ret_t     ret;
    THHUL_pdm_t  pdm;
    VIP_common_ret_t vret;

    ret = THHUL_hob_get_pdm(hca, &pdm);
    if (ret != HH_OK) {
        MTL_ERROR1("THHUL_pdm_free_pd_prep: ERROR (%d) : PDM object has not yet been created\n", ret);
        return HH_EINVAL;
    }

    /* undoing a previous prep */
    if (undo_flag == TRUE) {
        if ((vret=VIP_array_erase_undo(pdm->pd_array,(VIP_array_handle_t)hhul_pd)) != VIP_OK){
                MTL_ERROR1("THHUL_pdm_free_pd_prep: ERROR (%d) : invalid handle\n", vret);
                return HH_EINVAL_PD_HNDL;
        }
        return HH_OK;
    }

    /* preparing a PD FREE */
    /* need to find pd table entry in pd list */
    /* and signal it as prepared for erase.  Purpose here is to see if still have outstanding AVs */
    /* on this PD, in which case erase_prepare will return busy. */
    if ((vret=VIP_array_erase_prepare(pdm->pd_array,(VIP_array_handle_t)hhul_pd, NULL)) != VIP_OK){
        if (vret == VIP_EBUSY) { 
            MTL_DEBUG4("THHUL_pdm_free_pd_prep:  PD object is busy\n");
            return HH_EBUSY;
        } else {
            MTL_ERROR1("THHUL_pdm_free_pd_prep: ERROR (%d) : Could not find PD object\n", vret);
            return HH_EINVAL_PD_HNDL;
        }
    }
    return HH_OK;
}

HH_ret_t THHUL_pdm_free_pd_done (
   HHUL_hca_hndl_t hca, 
   HHUL_pd_hndl_t hhul_pd )
{
    HH_ret_t     ret;
    THHUL_pdm_t  pdm;
    THHUL_pd_t   *ul_pd;
    THH_udavm_t  udavm;
    VIP_common_ret_t vret;

    ret = THHUL_hob_get_pdm(hca, &pdm);
    if (ret != HH_OK) {
        MTL_ERROR1("THHUL_pdm_free_pd_done: ERROR (%d) : PDM object has not yet been created\n", ret);
        return HH_EINVAL;
    }

    /* need to find pd table entry in pd list */
    /* and destroy udavm if needed, and delete the entry from list */
    if ((vret=VIP_array_erase_done(pdm->pd_array,(VIP_array_handle_t)hhul_pd,
                              (VIP_array_obj_t*) &ul_pd)) != VIP_OK){
            MTL_ERROR1("THHUL_pdm_free_pd_done: ERROR (%d) : Could not find PD object\n", vret);
            return HH_EINVAL_PD_HNDL;
    }
    udavm = ul_pd->udavm;


    /* For non-privileged UDAVs, destroy the UDAV user-level object allocated for this PD */
    if (!(pdm->priv_ud_av)) {
        /*udavm can legally be null if THHUL_pdm_alloc_pd_done was not called, due to 
         * failure of THH_pdm_alloc_pd 
         */
        if (udavm != NULL) {
            ret = THH_udavm_destroy(udavm);
            if (ret != HH_OK) {
                MTL_ERROR1("THHUL_pdm_free_pd_done: ERROR (%d) : Could not destroy associated UDAV object\n", ret);
                /* continue, to free up the ul_pd anyway., and report successful 'free' */
            }
        }

        /* If udav was not allocated in DDR, free the allocated memory here */
        if (ul_pd->udav_nonddr_table != (MT_virt_addr_t) 0) {
            MOSAL_pci_virt_free_consistent((void *)ul_pd->udav_nonddr_table, ul_pd->uadv_nonddr_table_alloc_size);
            ul_pd->udav_nonddr_table = (MT_virt_addr_t) 0;
            ul_pd->udav_nonddr_table_aligned = (MT_virt_addr_t) 0;
            ul_pd->uadv_nonddr_table_alloc_size = 0;
        }
    }
    ul_pd->valid = FALSE;  /* just in case OS does not detect heap errors, and does not zero entries */
    FREE(ul_pd);
    return HH_OK;
}


HH_ret_t THHUL_pdm_create_ud_av ( 
   HHUL_hca_hndl_t hca, 
   HHUL_pd_hndl_t hhul_pd, 
   VAPI_ud_av_t *av_p, 
   HHUL_ud_av_hndl_t *ah_p )
{
    HH_ret_t     ret;
    THHUL_pdm_t  pdm;
    THHUL_pd_t   *ul_pd ;
    VIP_common_ret_t vret;

    /* pre-allocation checks */
    if (av_p == NULL) {
      MTL_ERROR4("THHUL_pdm_create_ud_av: av_p is NULL.\n");
      MT_RETURN(HH_EINVAL_PARAM);
    }
    
    if (av_p->port == 0 || av_p->port > THHUL_TAVOR_NUM_PORTS) {
        MTL_ERROR1("THHUL_pdm_create_ud_av: ERROR: invalid port number specified (%d)\n"
                   ,av_p->port);
        return HH_EINVAL_PORT;
    }

    ret = THHUL_hob_get_pdm(hca, &pdm);
    if (ret != HH_OK) {
        MTL_ERROR1("THHUL_pdm_create_ud_av: ERROR (%d) : PDM object has not yet been created\n", ret);
        return HH_EINVAL;
    }

    if (pdm->priv_ud_av) {
        MTL_ERROR1("THHUL_pdm_create_ud_av: non_privileged UDAVs not configured\n");
        return HH_EINVAL;
    }
    
    /* need to find pd table entry in pd array */
    if ((vret=VIP_array_find_hold(pdm->pd_array,(VIP_array_handle_t)hhul_pd,
                                  (VIP_array_obj_t*) &ul_pd)) != VIP_OK) {
            MTL_ERROR1("THHUL_pdm_create_ud_av: ERROR (%d) : Could not find PD object\n", vret);
            return HH_EINVAL_PD_HNDL;
    } 
    
    if (ul_pd->valid == FALSE) {
        MTL_ERROR1("THHUL_pdm_create_ud_av: ERROR: This PD is not allocated\n");
        ret = HH_EINVAL_PD_HNDL;
        goto err;
    }

    if (ul_pd->udavm == NULL) {
        MTL_ERROR1("THHUL_pdm_create_ud_av: ERROR: UDAVM object not allocated\n");
        ret = HH_EINVAL;
        goto err;
    }

    /* now, do it */
    ret = THH_udavm_create_av(ul_pd->udavm, ul_pd->hh_pd_hndl, av_p, (HH_ud_av_hndl_t *)ah_p);
    if (ret != HH_OK) {
        MTL_ERROR1("THHUL_pdm_create_ud_av: ERROR (%d) : Could not create address vector\n", ret);
        goto err;
    }
    return HH_OK;

err:
    VIP_array_find_release(pdm->pd_array,(VIP_array_handle_t)hhul_pd);
    return ret;
}


HH_ret_t THHUL_pdm_modify_ud_av (
   HHUL_hca_hndl_t hca, 
   HHUL_ud_av_hndl_t ah, 
   VAPI_ud_av_t *av_p )
{
    HH_ret_t              ret;
    THHUL_pdm_t           pdm;
    THHUL_pd_t            *ul_pd;
    VIP_common_ret_t      vret;
    VIP_array_handle_t    local_pd_hndl;
    MT_bool               found = FALSE;
    
    /* error checks */
    if (av_p->port == 0 || av_p->port > THHUL_TAVOR_NUM_PORTS) {
        MTL_ERROR1("THHUL_pdm_modify_ud_av: ERROR: invalid port number specified (%d)\n"
                   ,av_p->port);
        return HH_EINVAL_PORT;
    }

    ret = THHUL_hob_get_pdm(hca, &pdm);
    if (ret != HH_OK) {
        MTL_ERROR1("THHUL_pdm_modify_ud_av: ERROR (%d) : PDM object has not yet been created\n", ret);
        return HH_EINVAL;
    }
    
    if (pdm->priv_ud_av) {
        MTL_ERROR1("THHUL_pdm_modify_ud_av: non_privileged UDAVs not configured\n");
        return HH_EINVAL;
    }

    /* find the associated PD handle for this AV */
    VIP_ARRAY_FOREACH_HOLD(pdm->pd_array, vret, local_pd_hndl, (VIP_array_obj_t *)&ul_pd, TRUE) {
        if ((vret != VIP_OK) && (vret != VIP_EAGAIN)) {
                MTL_ERROR1("THHUL_pdm_modify_ud_av: ERROR (%d) : Could not find PD object\n", vret);
                /* return invalid AV handle, because a PD error return is not acceptable here  */
                /* IB Spec demands that destroy PD shall fail if it has any outstanding resources. */
                /* Essentially, then, we have allowed destroy PD anyway, so that the AV handle is */
                /* no loger valid. */
                return HH_EINVAL_AV_HNDL;
        }
        if (ul_pd->valid == FALSE) {
            /* ignore if PD is in process of being created */
            if (vret == VIP_OK)  {VIP_array_find_release(pdm->pd_array,local_pd_hndl);}
            continue;
        }
        /* try to modify the UDAVM  */
        ret = THH_udavm_modify_av(ul_pd->udavm,ah,av_p);
        if (ret == HH_OK) {
            if (vret == VIP_OK)  {VIP_array_find_release(pdm->pd_array,local_pd_hndl);}
            found = TRUE;
            break;
        } else if (ret != HH_EINVAL_AV_HNDL) {
            MTL_ERROR1("THHUL_pdm_modify_ud_av: ERROR (%d) : invalid parameter\n", ret);
            if (vret == VIP_OK)  {VIP_array_find_release(pdm->pd_array,local_pd_hndl);}
            return ret;
        } 
        if (vret == VIP_OK)  {VIP_array_find_release(pdm->pd_array,local_pd_hndl);}
    }
    
    if (found == FALSE)
    {
        return HH_EINVAL_AV_HNDL;
    }

    return HH_OK;
}


HH_ret_t THHUL_pdm_query_ud_av (
   HHUL_hca_hndl_t hca, 
   HHUL_ud_av_hndl_t ah, 
   VAPI_ud_av_t *av_p )
{
    HH_ret_t              ret;
    THHUL_pdm_t           pdm;
    THHUL_pd_t            *ul_pd;
    VIP_common_ret_t      vret;
    VIP_array_handle_t    local_pd_hndl;
    MT_bool               found = FALSE;
    
    /* error checks */
    ret = THHUL_hob_get_pdm(hca, &pdm);
    if (ret != HH_OK) {
        MTL_ERROR1("THHUL_pdm_query_ud_av: ERROR (%d) : PDM object has not yet been created\n", ret);
        return HH_EINVAL;
    }
    
    if (pdm->priv_ud_av) {
        MTL_ERROR1("THHUL_pdm_query_ud_av: non_privileged UDAVs not configured\n");
        return HH_EINVAL;
    }

    /* find the associated PD handle for this AV */
    VIP_ARRAY_FOREACH_HOLD(pdm->pd_array, vret, local_pd_hndl, (VIP_array_obj_t *)&ul_pd, TRUE) {
        if ((vret != VIP_OK) && (vret != VIP_EAGAIN)) {
                MTL_ERROR1("THHUL_pdm_query_ud_av: ERROR (%d) : Could not find PD object\n", vret);
                /* return invalid AV handle, because a PD error return is not acceptable here  */
                /* IB Spec demands that destroy PD shall fail if it has any outstanding resources. */
                /* Essentially, then, we have allowed destroy PD anyway, so that the AV handle is */
                /* no loger valid. */
                return HH_EINVAL_AV_HNDL;
        }
        if (ul_pd->valid == FALSE) {
            /* ignore if PD is in process of being created */
            if (vret == VIP_OK)  {VIP_array_find_release(pdm->pd_array,local_pd_hndl);}
            continue;
        }
        /* try to modify the UDAVM  */
        ret = THH_udavm_query_av(ul_pd->udavm,ah,av_p);
        if (ret == HH_OK) {
            if (vret == VIP_OK)  {VIP_array_find_release(pdm->pd_array,local_pd_hndl);}
            found = TRUE;
            break;
        } else if (ret != HH_EINVAL_AV_HNDL) {
            MTL_ERROR1("THHUL_pdm_query_ud_av: ERROR (%d) : invalid parameter\n", ret);
            if (vret == VIP_OK)  {VIP_array_find_release(pdm->pd_array,local_pd_hndl);}
            return ret;
        } 
        if (vret == VIP_OK)  {VIP_array_find_release(pdm->pd_array,local_pd_hndl);}
    }
    
    if (found == FALSE)
    {
        return HH_EINVAL_AV_HNDL;
    }
    return HH_OK;
}

HH_ret_t THHUL_pdm_destroy_ud_av (
   HHUL_hca_hndl_t hca, 
   HHUL_ud_av_hndl_t ah )
{
    HH_ret_t              ret;
    THHUL_pdm_t           pdm;
    THHUL_pd_t            *ul_pd;
    VIP_common_ret_t      vret;
    VIP_array_handle_t    local_pd_hndl;
    MT_bool               found = FALSE;
    
    /* error checks */
    ret = THHUL_hob_get_pdm(hca, &pdm);
    if (ret != HH_OK) {
        MTL_ERROR1("THHUL_pdm_destroy_ud_av: ERROR (%d) : PDM object has not yet been created\n", ret);
        return HH_EINVAL;
    }
    
    if (pdm->priv_ud_av) {
        MTL_ERROR1("THHUL_pdm_destroy_ud_av: non_privileged UDAVs not configured\n");
        return HH_EINVAL;
    }

    /* find the associated PD handle for this AV */
    VIP_ARRAY_FOREACH_HOLD(pdm->pd_array, vret, local_pd_hndl, (VIP_array_obj_t *)&ul_pd, TRUE) {
        if ((vret != VIP_OK) && (vret != VIP_EAGAIN)) {
                MTL_ERROR1("THHUL_pdm_destroy_ud_av: ERROR (%d) : Could not find PD object\n", vret);
                /* We were unable to find a PD to which this udav handle was registered. */
                /* Return invalid AV handle, because a PD error return is not acceptable here  */
                return HH_EINVAL_AV_HNDL;
        }
        /* try to modify the UDAVM  */
        if (ul_pd->valid == FALSE) {
            /* ignore if PD is in process of being created */
            if (vret == VIP_OK)  {VIP_array_find_release(pdm->pd_array,local_pd_hndl);}
            continue;
        }
        ret = THH_udavm_destroy_av(ul_pd->udavm,ah);
        if (ret == HH_OK) {
            if (vret == VIP_OK)  {VIP_array_find_release(pdm->pd_array,local_pd_hndl);}
            found = TRUE;
            break;
        } else if (ret != HH_EINVAL_AV_HNDL) {
            if (vret == VIP_OK)  {VIP_array_find_release(pdm->pd_array,local_pd_hndl);}
            MTL_ERROR1("THHUL_pdm_destroy_ud_av: ERROR (%d) : invalid parameter\n", ret);
            return ret;
        } 
        if (vret == VIP_OK)  {VIP_array_find_release(pdm->pd_array,local_pd_hndl);}
    }
    
    if (found == TRUE){
        /* decrement udav reference count for the PD */
        VIP_array_find_release(pdm->pd_array,local_pd_hndl);
        return HH_OK;
    } else {
        return HH_EINVAL_AV_HNDL;
    }
}

HH_ret_t THHUL_pdm_get_ud_av_memkey_sqp_ok(
  /*IN*/ THHUL_pdm_t  pdm,
  /*IN*/ HHUL_pd_hndl_t hhul_pd,
  /*OUT*/MT_bool *ok_for_sqp,
  /*OUT*/ VAPI_lkey_t *ud_av_memkey_p
)
{
    /* sanity check */
    THHUL_pd_t   *ul_pd;
    VIP_common_ret_t vret;

/* need to find pd table entry in pd array */
    if ((vret=VIP_array_find_hold(pdm->pd_array,(VIP_array_handle_t)hhul_pd,
                                  (VIP_array_obj_t*) &ul_pd)) != VIP_OK) {
        if (vret == VIP_EBUSY) { 
            MTL_DEBUG4("THHUL_pdm_get_ud_av_memkey:  PD object is busy\n");
            return HH_EBUSY;
        } else {
            MTL_ERROR1("THHUL_pdm_get_ud_av_memkey: ERROR (%d) : Could not find PD object\n", vret);
            return HH_EINVAL_PD_HNDL;
        }
    } 
    *ud_av_memkey_p = ((THHUL_pd_t *)(ul_pd))->lkey;
    /* is OK for special QP iff udav table is located in host memory */
    *ok_for_sqp =  ((THHUL_pd_t *)(ul_pd))->uadv_nonddr_table_alloc_size == 0 ? FALSE : TRUE;
    VIP_array_find_release(pdm->pd_array,(VIP_array_handle_t)hhul_pd);
    return HH_OK;
    
} /* THHUL_pdm_get_ud_av_memkey */


