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

#include <thh_uldm_priv.h>

/******************************************************************************
 *  Function:     THH_uldm_create
 *****************************************************************************/
HH_ret_t THH_uldm_create( /*IN*/  THH_hob_t    hob, 
                          /*IN*/  MT_phys_addr_t  uar_base, 
                          /*IN*/  u_int8_t     log2_max_uar, 
                          /*IN*/  u_int8_t     log2_uar_pg_sz,
                          /*IN*/  u_int32_t    max_pd, 
                          /*OUT*/ THH_uldm_t   *uldm_p )
{
    u_int32_t   max_uar;
    THH_uldm_t  new_uldm_obj;

    MTL_DEBUG4("ENTERING THH_uldm_create: uar_base = " PHYS_ADDR_FMT ", log2_max_uar = %d\nlog2_uar_pg_sz=%d, max_pd=%d\n",
               uar_base, log2_max_uar, log2_uar_pg_sz, max_pd);
    /* create new uldm object */
    new_uldm_obj = (THH_uldm_t) MALLOC(sizeof(THH_uldm_obj_t));
    if (!new_uldm_obj) {
        MTL_ERROR1("THH_uldm_create: MALLOC of new_uldm_obj failed\n");
        return HH_ENOMEM;
    }

    max_uar = (1 << log2_max_uar);
    new_uldm_obj->max_uar = max_uar;


    /* create UAR free pool */
    new_uldm_obj->uldm_uar_table = (THH_uldm_uar_entry_t *) VMALLOC(max_uar * sizeof(THH_uldm_uar_entry_t));
    if (!new_uldm_obj->uldm_uar_table) {
        FREE(new_uldm_obj);
        MTL_ERROR1("THH_uldm_create: VMALLOC of uldm_uar_table failed\n");
        return HH_ENOMEM;
    }
    MTL_DEBUG4("THH_uldm_create: allocated uar table: addr = %p, size=%d\n",
                 (void *) (new_uldm_obj->uldm_uar_table), max_uar);

    memset(new_uldm_obj->uldm_uar_table, 0, max_uar * sizeof(THH_uldm_uar_entry_t));

    MTL_TRACE4("THH_uldm_create: creating UAR pool\n");
    new_uldm_obj->uar_list.entries = new_uldm_obj->uldm_uar_table;
    new_uldm_obj->uar_list.size = max_uar ;  /* list size is number of ENTRIES */
    new_uldm_obj->uar_list.head = 0;
    new_uldm_obj->uar_list.meta = &(new_uldm_obj->uar_meta);

    new_uldm_obj->uar_meta.entry_size =  sizeof(THH_uldm_uar_entry_t);
    new_uldm_obj->uar_meta.prev_struct_offset = (MT_ulong_ptr_t) &(((THH_uldm_uar_entry_t *) (NULL))->u1.epool.prev);
    new_uldm_obj->uar_meta.next_struct_offset = (MT_ulong_ptr_t) &(((THH_uldm_uar_entry_t *) (NULL))->u1.epool.next);

    MTL_DEBUG4("THH_uldm_create: calling epool_init: entries = %p, size=%lu, head=%lu\nentry_size=%d, prev_offs=%d, next_offs=%d\n",
                new_uldm_obj->uar_list.entries, new_uldm_obj->uar_list.size, new_uldm_obj->uar_list.head,
                new_uldm_obj->uar_meta.entry_size, new_uldm_obj->uar_meta.prev_struct_offset,
                new_uldm_obj->uar_meta.next_struct_offset);
    epool_init(&(new_uldm_obj->uar_list));

    /* set uar's 0 and 1 to unavailable */
    MTL_TRACE4("THH_uldm_create: Reserving UARs 0 and 1\n");
    epool_reserve(&(new_uldm_obj->uar_list), 0, 2);

    /* create PD free pool */
    MTL_TRACE4("THH_uldm_create: creating PD pool\n");
    new_uldm_obj->uldm_pd_table = (THH_uldm_pd_entry_t *) VMALLOC(max_pd * sizeof(THH_uldm_pd_entry_t));
    if (!new_uldm_obj->uldm_pd_table) {
        VFREE(new_uldm_obj->uldm_uar_table);
        FREE(new_uldm_obj);
        return HH_ENOMEM;
    }
    memset(new_uldm_obj->uldm_pd_table, 0, max_pd * sizeof(THH_uldm_pd_entry_t));


    new_uldm_obj->pd_list.entries = new_uldm_obj->uldm_pd_table;
    new_uldm_obj->pd_list.size = max_pd;    /* list size is number of ENTRIES */
    new_uldm_obj->pd_list.head = 0;
    new_uldm_obj->pd_list.meta = &(new_uldm_obj->pd_meta);

    new_uldm_obj->pd_meta.entry_size =  sizeof(THH_uldm_pd_entry_t);
    new_uldm_obj->pd_meta.prev_struct_offset = (MT_ulong_ptr_t) &(((THH_uldm_pd_entry_t *) (NULL))->u1.epool.prev);
    new_uldm_obj->pd_meta.next_struct_offset = (MT_ulong_ptr_t) &(((THH_uldm_pd_entry_t *) (NULL))->u1.epool.next);

    MTL_DEBUG4("THH_uldm_create: calling epool_init: entries = %p, size=%lu, head=%lu\nentry_size=%d, prev_offs=%d, next_offs=%d\n",
                new_uldm_obj->pd_list.entries, new_uldm_obj->pd_list.size, new_uldm_obj->pd_list.head,
                new_uldm_obj->pd_meta.entry_size, new_uldm_obj->pd_meta.prev_struct_offset,
                new_uldm_obj->pd_meta.next_struct_offset);
    epool_init(&(new_uldm_obj->pd_list));

    /* set pd's 0 and 1 to unavailable */
    MTL_TRACE4("THH_uldm_create: Reserving PDs 0 and 1\n");
    epool_reserve(&(new_uldm_obj->pd_list), 0, THH_NUM_RSVD_PD);

    new_uldm_obj->uar_base = uar_base;
    new_uldm_obj->log2_max_uar = log2_max_uar;
    new_uldm_obj->log2_uar_pg_sz = log2_uar_pg_sz;
    new_uldm_obj->max_pd = max_pd;
  
    /* save HOB handle */
    new_uldm_obj->hob = hob;

    /* return the object handle */
    *uldm_p =  new_uldm_obj;

    MTL_DEBUG4("LEAVING THH_uldm_create - OK\n");

    return HH_OK;
}

/******************************************************************************
 *  Function:     THH_uldm_destroy
 *****************************************************************************/
HH_ret_t THH_uldm_destroy( /*IN*/ THH_uldm_t uldm )
{
    u_int32_t i;

    MTL_DEBUG4("==> THH_uldm_destroy\n");
    for (i=0; i< uldm->max_uar; i++) {
        if (uldm->uldm_uar_table[i].valid) {
            /* is uar valid check is within this function */
            THH_uldm_free_uar(uldm,i);
        }
    }
    VFREE(uldm->uldm_uar_table);
    VFREE(uldm->uldm_pd_table);

    FREE(uldm);
    MTL_DEBUG4("<== THH_uldm_destroy\n");
    return HH_OK;
}

/*************************************************************************
 * Function: THH_uldm_alloc_ul_res
 *************************************************************************/ 
HH_ret_t THH_uldm_alloc_ul_res( /*IN*/ THH_uldm_t              uldm, 
                                /*IN*/ MOSAL_protection_ctx_t  prot_ctx, 
                                /*OUT*/ THH_hca_ul_resources_t *hca_ul_resources_p )
{
  u_int32_t    uar_index;
  MT_virt_addr_t  uar_map;
  HH_ret_t     ret = HH_OK;

#ifndef __DARWIN__
  MTL_DEBUG4("==> THH_uldm_alloc_ul_res. prot_ctx = 0x%x\n", prot_ctx);
#else
  MTL_DEBUG4("==> THH_uldm_alloc_ul_res.\n");
#endif

  ret = THH_uldm_alloc_uar(uldm,prot_ctx, &uar_index, &uar_map);
  if (ret != HH_OK) {
      MTL_ERROR1("%s: failed allocating UAR. ERROR = %d\n", __func__, ret);
      goto uldm_err;
  }

  hca_ul_resources_p->uar_index = uar_index;
  hca_ul_resources_p->uar_map   = uar_map;

uldm_err:
  MTL_DEBUG4("<== THH_uldm_alloc_ul_res. ret = %d\n", ret);
  return ret;
}

/*************************************************************************
 * Function: THH_uldm_free_ul_res
 *************************************************************************/ 
HH_ret_t THH_uldm_free_ul_res( /*IN*/ THH_uldm_t             uldm, 
                               /*IN*/ THH_hca_ul_resources_t *hca_ul_resources_p)
{
    HH_ret_t     ret = HH_OK;

    MTL_DEBUG4("==> THH_uldm_free_ul_res. resources ptr = %p\n", (void *) hca_ul_resources_p);
    if (hca_ul_resources_p->uar_index > 1) {
        ret = THH_uldm_free_uar(uldm, hca_ul_resources_p->uar_index);
        if (ret != HH_OK) {
            MTL_ERROR1("%s: failed freeing UAR index %d. ERROR = %d\n", __func__, hca_ul_resources_p->uar_index, ret);
            goto uldm_err;
        }
    }

    hca_ul_resources_p->uar_index = 0;
    hca_ul_resources_p->uar_map   = 0;

uldm_err:
    MTL_DEBUG4("<== THH_uldm_free_ul_res. ret = %d\n", ret);
    return ret;
}

/*************************************************************************
 * Function: THH_uldm_alloc_uar
 *************************************************************************/
HH_ret_t THH_uldm_alloc_uar( /*IN*/ THH_uldm_t              uldm, 
                             /*IN*/ MOSAL_protection_ctx_t  prot_ctx, 
                             /*OUT*/ u_int32_t              *uar_index, 
                             /*OUT*/ MT_virt_addr_t            *uar_map )
{
    unsigned long i;
    HH_ret_t     ret = HH_OK;
    
#ifndef __DARWIN__
    MTL_DEBUG4("==> THH_uldm_alloc_uar. prot_ctx = 0x%x\n", prot_ctx);
#else
    MTL_DEBUG4("==> THH_uldm_alloc_uar.\n");
#endif
    
    if (uldm == (THH_uldm_t)THH_INVALID_HNDL) {
        MTL_ERROR1("THH_uldm_alloc_uar: ERROR : HCA device has not yet been opened\n");
        ret = HH_EINVAL;
        goto uldm_err;
    }

    i = epool_alloc(&(uldm->uar_list)) ;
    if (i == EPOOL_NULL) {
        MTL_ERROR1("THH_uldm_alloc_uar: ERROR : Could not allocate a UAR from pool\n");
        ret = HH_EINVAL;
        goto uldm_err;
    } else {
        *uar_index = i;
        uldm->uldm_uar_table[i].u1.uar.prot_ctx = prot_ctx;
        uldm->uldm_uar_table[i].valid = TRUE;
        *uar_map =  (MT_virt_addr_t) MOSAL_map_phys_addr(/* phys addr */ uldm->uar_base + (i * (1 << uldm->log2_uar_pg_sz)),
                                                        (1 << uldm->log2_uar_pg_sz),
                                                        MOSAL_MEM_FLAGS_NO_CACHE | MOSAL_MEM_FLAGS_PERM_WRITE, 
                                                        prot_ctx);
        if (*uar_map == (MT_virt_addr_t) NULL) {
#ifndef __DARWIN__
            MTL_ERROR1("THH_uldm_alloc_uar: MOSAL_map_phys_addr failed for prot ctx %d, addr " PHYS_ADDR_FMT ", size %d\n",
                       prot_ctx,
                       (MT_phys_addr_t) (uldm->uar_base + (i * (1 << uldm->log2_uar_pg_sz))),
                       (1U << uldm->log2_uar_pg_sz));
#else
            MTL_ERROR1("THH_uldm_alloc_uar: MOSAL_map_phys_addr failed, addr " PHYS_ADDR_FMT ", size %d\n",
                       (MT_phys_addr_t) (uldm->uar_base + (i * (1 << uldm->log2_uar_pg_sz))),
                       (1U << uldm->log2_uar_pg_sz));
#endif
            return HH_EINVAL;
        }
        uldm->uldm_uar_table[i].u1.uar.virt_addr = *uar_map;
        MTL_DEBUG4("THH_uldm_alloc_uar: index = %d, addr = " VIRT_ADDR_FMT "\n", *uar_index, *uar_map);
        ret = HH_OK;
    }

uldm_err:
    MTL_DEBUG4("<== THH_uldm_alloc_uar. ret = %d\n", ret);
    return ret;
}

/******************************************************************************
 *  Function:     THH_uldm_free_uar
 *****************************************************************************/
HH_ret_t THH_uldm_free_uar( /*IN*/ THH_uldm_t    uldm, 
                            /*IN*/ u_int32_t     uar_index )
{
    call_result_t  rc;
    HH_ret_t       ret = HH_OK;

    /* check that index is valid */
    MTL_DEBUG4("==> THH_uldm_free_uar. uar_index = %d\n", uar_index);
    if (uldm == (THH_uldm_t)THH_INVALID_HNDL) {
        MTL_ERROR1("THH_uldm_free_uar: ERROR : HCA device has not yet been opened\n");
        ret = HH_EINVAL;
        goto err;
    }

    if (uar_index > uldm->max_uar ) {
        MTL_ERROR1("THH_uldm_free_uar: uar_index out of range (%d)\n", uar_index);
        ret = HH_EINVAL;
        goto err;
    }

    if (!(uldm->uldm_uar_table[uar_index].valid)) {
        MTL_ERROR1("THH_uldm_free_uar: uar_index was not allocated (%d)\n", uar_index);
        ret = HH_EINVAL;
        goto err;
    }
    /* Unmap previously mapped physical (will be page aligned, of course) */

    rc = MOSAL_unmap_phys_addr(uldm->uldm_uar_table[uar_index].u1.uar.prot_ctx, 
                               (MT_virt_addr_t) /* (void *) <ER/> */ uldm->uldm_uar_table[uar_index].u1.uar.virt_addr, 
                               (1 << uldm->log2_uar_pg_sz));

    if (rc != MT_OK) {
#ifndef __DARWIN__
        MTL_ERROR1("THH_uldm_free_uar: MOSAL_unmap_phys_addr failed for prot ctx %d, addr %p, size %d\n",
                   uldm->uldm_uar_table[uar_index].u1.uar.prot_ctx,
                   (void *)uldm->uldm_uar_table[uar_index].u1.uar.virt_addr,
                   (1 << uldm->log2_uar_pg_sz));
#else
        MTL_ERROR1("THH_uldm_free_uar: MOSAL_unmap_phys_addr failed, addr %p, size %d\n",
                   (void *)uldm->uldm_uar_table[uar_index].u1.uar.virt_addr,
                   (1 << uldm->log2_uar_pg_sz));
#endif
        ret = HH_EINVAL;
        goto err;
    }
    
    uldm->uldm_uar_table[uar_index].valid = FALSE;
    epool_free(&(uldm->uar_list), uar_index);
    
err:
    MTL_DEBUG4("<== THH_uldm_free_uar. ret = %d\n", ret);
    return ret;

}

/******************************************************************************
 *  Function:     THH_uldm_alloc_pd
 *****************************************************************************/
HH_ret_t THH_uldm_alloc_pd( /*IN*/ THH_uldm_t                   uldm, 
                            /*IN*/ MOSAL_protection_ctx_t       prot_ctx, 
                            /*IN-OUT*/ THH_pd_ul_resources_t    *pd_ul_resources_p, 
                            /*OUT*/ HH_pd_hndl_t                *pd_p )
{   
    unsigned long      i;
    HH_ret_t           ret = HH_OK;
    THH_internal_mr_t  mr_data;
    VAPI_lkey_t        lkey;
    THH_mrwm_t         mrwm;
    THH_ddrmm_t        ddrmm;
    THH_udavm_t        udavm;
    MT_size_t          adj_ud_av_table_sz = 0;
    MT_virt_addr_t        udav_virt_addr = 0;
    MT_phys_addr_t        udav_phys_addr = 0;
    VAPI_size_t        udav_phys_buf_size = 0;
    MT_bool            use_priv_udav, av_in_host_mem, hide_ddr;
    u_int32_t          pd_index;
    u_int32_t          max_ah_num = 0, max_requested_avs = 0;
    unsigned int page_size;
    call_result_t rc;

#ifndef __DARWIN__
    MTL_DEBUG4("==> THH_uldm_alloc_pd. uldm = %p, prot_ctx = 0x%x, resources_p = %p\n", (void *) uldm, prot_ctx, (void *) pd_ul_resources_p);
#else
    MTL_DEBUG4("==> THH_uldm_alloc_pd. uldm = %p, resources_p = %p\n", (void *) uldm, (void *) pd_ul_resources_p);
#endif
    
    if (uldm == (THH_uldm_t)THH_INVALID_HNDL) {
        MTL_ERROR1("<==THH_uldm_alloc_pd: ERROR : HCA device has not yet been opened\n");
        return HH_EINVAL;
    }


    ret = THH_hob_get_udavm_info (uldm->hob, &udavm, &use_priv_udav, &av_in_host_mem, 
                                  &lkey, &max_ah_num, &hide_ddr );
    if (ret != HH_OK) {
        MTL_ERROR1("<==THH_uldm_alloc_pd: ERROR:  could not acquire udavm information (%d)\n", ret);
        return ret;
    }

    rc = MOSAL_get_page_size(prot_ctx, pd_ul_resources_p->udavm_buf, &page_size);
    if ( rc != MT_OK ) {
      MTL_ERROR1(MT_FLFMT("%s: could not obtain page size of address="VIRT_ADDR_FMT), __func__,
                           pd_ul_resources_p->udavm_buf);
      return HH_ENOMEM;
    }
    
    if (use_priv_udav && (((pd_ul_resources_p->udavm_buf_sz) & (page_size - 1)) != 0)){
        MTL_ERROR1("<==THH_uldm_alloc_pd: ERROR : udavm_buf_size ("SIZE_T_FMT") is not a multiple of the page size\n",
			pd_ul_resources_p->udavm_buf_sz);
        return HH_EINVAL;
     }

    if (!use_priv_udav) {
        /* check to see that we are not trying to allocate space for more AVs than max reported in HCA capabilities */
        max_requested_avs = (u_int32_t)((pd_ul_resources_p->udavm_buf_sz) / (sizeof(struct tavorprm_ud_address_vector_st) / 8));
        if (max_requested_avs > max_ah_num) {
            MTL_ERROR1("<==THH_uldm_alloc_pd: max AVs too large: requested %d, max available=%d\n",
                         max_requested_avs, max_ah_num);
            return HH_EINVAL;
        }
    }

    i = epool_alloc(&uldm->pd_list) ;
    if (i == EPOOL_NULL) {
        MTL_ERROR1("<==THH_uldm_alloc_pd: could not allocate a PD from pool\n");
        return HH_EAGAIN;
    } else {
        pd_index = (u_int32_t) i;
        uldm->uldm_pd_table[pd_index].u1.pd.prot_ctx = prot_ctx;
        *pd_p = (HH_pd_hndl_t) i;

        if (use_priv_udav) {
            pd_ul_resources_p->udavm_buf_memkey = lkey;
        } else {
            ret = THH_hob_get_mrwm(uldm->hob,&mrwm);
            if (ret != HH_OK) {
                MTL_ERROR1("THH_uldm_alloc_pd: could not acquire MRWM handle (%d)\n", ret);
                goto udavm_get_mwrm_err;
            }
            /* Use DDR resources if appropriate flag set at module initialization,
               or if Tavor firmware indicates that should not hide DDR memory, or if not a PD for a SQP
             */
            if ((hide_ddr == FALSE) && (av_in_host_mem == FALSE) && (pd_ul_resources_p->pd_flags != PD_FOR_SQP)) {
                ret = THH_hob_get_ddrmm(uldm->hob,&ddrmm);
                if (ret != HH_OK) {
                    MTL_ERROR1("THH_uldm_alloc_pd: could not acquire DDRMM handle (%d)\n", ret);
                } else {
    
                    /* get memory in DDR. If cannot, then use regular memory allocated in  */
                    /* call to THHUL_uldm_alloc_pd_prep */
                    adj_ud_av_table_sz =  MT_UP_ALIGNX_SIZE((pd_ul_resources_p->udavm_buf_sz), MOSAL_SYS_PAGE_SHIFT);
                    udav_phys_buf_size = adj_ud_av_table_sz;
                    ret = THH_ddrmm_alloc(ddrmm, 
                                          adj_ud_av_table_sz, 
                                          MOSAL_SYS_PAGE_SHIFT,
                                          &udav_phys_addr);
                    if (ret != HH_OK) {
                       MTL_DEBUG1("THH_uldm_alloc_pd: could not allocate protected udavm area in DDR(err = %d)\n", ret);
                       udav_phys_addr = (MT_phys_addr_t) 0;
                    } else {
#ifndef __DARWIN__
                       MTL_DEBUG4("THH_uldm_alloc_pd. prot_ctx = 0x%x, phys_addr = " PHYS_ADDR_FMT 
                                  ", buf_size="SIZE_T_FMT", adj_size="SIZE_T_FMT"\n",
                                   prot_ctx,
                                   udav_phys_addr, pd_ul_resources_p->udavm_buf_sz,
                                   adj_ud_av_table_sz);
#else
                       MTL_DEBUG4("THH_uldm_alloc_pd. phys_addr = " PHYS_ADDR_FMT 
                                  ", buf_size="SIZE_T_FMT", adj_size="SIZE_T_FMT"\n",
                                  udav_phys_addr, pd_ul_resources_p->udavm_buf_sz,
                                  adj_ud_av_table_sz);
#endif
                       udav_virt_addr = (MT_virt_addr_t) MOSAL_map_phys_addr( udav_phys_addr , 
                                                                           udav_phys_buf_size,
                                                                           MOSAL_MEM_FLAGS_NO_CACHE | 
                                                                              MOSAL_MEM_FLAGS_PERM_WRITE | 
                                                                              MOSAL_MEM_FLAGS_PERM_READ , 
                                                                           prot_ctx);
                       MTL_DEBUG4("THH_uldm_alloc_pd. udav virt_addr = " VIRT_ADDR_FMT "\n", udav_virt_addr);
                       if (udav_virt_addr == (MT_virt_addr_t) NULL) {
                           MTL_ERROR1("THH_uldm_alloc_pd: could not map physical address " PHYS_ADDR_FMT " to virtual\n", 
                                      udav_phys_addr);
                           ret = THH_ddrmm_free(ddrmm,udav_phys_addr,udav_phys_buf_size);
                           if (ret != HH_OK) {
                              MTL_ERROR1("THH_uldm_alloc_pd: could not free protected udavm area in DDR(err = %d)\n", ret);
                           }
                           udav_phys_addr = (MT_phys_addr_t) 0;
                       } else {
                           /* substitute DDR-allocated region for the one passed by pd_ul_resources_p */
                           pd_ul_resources_p->udavm_buf = udav_virt_addr;
                       }
                   }
                }
            }
            /* check if have a buffer allocated for udav table */
            if (pd_ul_resources_p->udavm_buf == (MT_virt_addr_t) NULL) {
                MTL_ERROR1("THH_uldm_alloc_pd: no udavm area allocated.\n");
                /*value of ret was set above by some failure */
                goto no_udavm_area;
            }

            memset(&mr_data, 0, sizeof(mr_data));
            mr_data.force_memkey = FALSE;
            mr_data.memkey       = 0;
            mr_data.pd           = (HH_pd_hndl_t) pd_index;
            mr_data.size         = pd_ul_resources_p->udavm_buf_sz;
            mr_data.vm_ctx       = prot_ctx;
            if (udav_phys_addr) {
                VAPI_phy_addr_t udav_phy = udav_phys_addr;
                mr_data.num_bufs = 1;      /*  != 0   iff   physical buffesrs supplied */
                mr_data.phys_buf_lst = &udav_phy;  /* size = num_bufs */
                mr_data.buf_sz_lst = &udav_phys_buf_size;    /* [num_bufs], corresponds to phys_buf_lst */
                mr_data.start        = pd_ul_resources_p->udavm_buf;
            } else {
                /* using user-level buffer: check that buffer address is aligned  to entry size; */
                if (pd_ul_resources_p->udavm_buf != 
                     (MT_UP_ALIGNX_VIRT((pd_ul_resources_p->udavm_buf), 
                                  ceil_log2((sizeof(struct tavorprm_ud_address_vector_st) / 8))))) {
                    MTL_ERROR1("THH_uldm_alloc_pd: provided HOST MEM buffer not properly aligned.\n");
                    /*value of ret was set above by some failure */
                    ret = HH_EINVAL;
                    goto no_udavm_area;
                }
                mr_data.start = (IB_virt_addr_t)pd_ul_resources_p->udavm_buf;
                MTL_DEBUG1(MT_FLFMT("%s: User level UDAV tbl = " U64_FMT), __func__, mr_data.start);
            }
            uldm->uldm_pd_table[pd_index].valid = TRUE; /* set to valid here, so that THH_uldm_get_protection_ctx will work */
            ret = THH_mrwm_register_internal(mrwm, &mr_data, &lkey);
            if (ret != HH_OK) {
                MTL_ERROR1("THH_uldm_alloc_pd: could not register udavm table (%d)\n", ret);
                uldm->uldm_pd_table[pd_index].valid = FALSE;
                goto udavm_table_register_err;
            }
            pd_ul_resources_p->udavm_buf_memkey = lkey;
            uldm->uldm_pd_table[pd_index].u1.pd.lkey   = lkey;
            uldm->uldm_pd_table[pd_index].u1.pd.udav_table_ddr_phys_addr   = udav_phys_addr;
            uldm->uldm_pd_table[pd_index].u1.pd.udav_table_ddr_virt_addr   = udav_virt_addr;
            uldm->uldm_pd_table[pd_index].u1.pd.udav_table_size   = (udav_phys_addr ? udav_phys_buf_size : 0);
            MTL_DEBUG4("THH_uldm_alloc_pd: PD %u: saving phys addr = " PHYS_ADDR_FMT ", virt addr = " VIRT_ADDR_FMT ", size="SIZE_T_FMT"\n",
                       pd_index, udav_phys_addr, udav_virt_addr, uldm->uldm_pd_table[pd_index].u1.pd.udav_table_size );
        }
        /* set VALID flag only if successful */
        uldm->uldm_pd_table[pd_index].valid = TRUE;
        ret =  HH_OK;
        MTL_DEBUG4("THH_uldm_alloc_pd. PD = %u\n", pd_index);
        goto ok_retn;
    }

no_udavm_area:
udavm_get_mwrm_err:
udavm_table_register_err:
    epool_free(&(uldm->pd_list),i);
ok_retn:
    MTL_DEBUG4("<== THH_uldm_alloc_pd. ret = %d\n", ret);
    return ret;

}

/******************************************************************************
 *  Function:     THH_uldm_free_pd 
 *****************************************************************************/
HH_ret_t THH_uldm_free_pd( /*IN*/ THH_uldm_t    uldm, 
                           /*IN*/ HH_pd_hndl_t  pd )
{

    HH_ret_t           ret = HH_OK;
    VAPI_lkey_t        lkey;
    THH_mrwm_t         mrwm;
    THH_udavm_t        udavm;
    THH_ddrmm_t        ddrmm;
    MT_virt_addr_t        udav_virt_addr = 0;
    MT_phys_addr_t        udav_phys_addr = 0;
    MT_size_t          udav_size = 0;
    MT_bool            use_priv_udav, av_in_host_mem, hide_ddr;
    call_result_t      res;
    u_int32_t          max_ah_num = 0;
    
    MTL_DEBUG4("==> THH_uldm_free_pd\n");
    if (uldm == (THH_uldm_t)THH_INVALID_HNDL) {
        MTL_ERROR1("THH_uldm_free_pd: ERROR : HCA device has not yet been opened\n");
        ret = HH_EINVAL;
        goto err_retn;
    }
    
    if (pd > uldm->max_pd ) {
        MTL_ERROR1("THH_uldm_free_pd: pd out of range (%d)\n", pd);
        ret = HH_EINVAL;
        goto err_retn;
    }

    if (!(uldm->uldm_pd_table[pd].valid)) {
        MTL_ERROR1("THH_uldm_free_pd: pd was not allocated (%d)\n", pd);
        ret = HH_EINVAL;
        goto err_retn;
    }

    ret = THH_hob_get_udavm_info (uldm->hob, &udavm, &use_priv_udav, &av_in_host_mem, 
                                  &lkey, &max_ah_num, &hide_ddr );
    if (ret != HH_OK) {
        MTL_ERROR1("THH_uldm_free_pd: could not acquire udavm information (%d)\n", ret);
        goto get_udavm_info_err;
    }
    MTL_DEBUG4("THH_uldm_free_pd: udavm=%p, use_priv_udav = %s, lkey = 0x%x\n", 
               udavm, (use_priv_udav ? "TRUE" : "FALSE"), lkey);
    if (!use_priv_udav) {

        ret = THH_hob_get_mrwm(uldm->hob,&mrwm);
        if (ret != HH_OK) {
            MTL_ERROR1("THH_uldm_free_pd: could not acquire MRWM handle (%d)\n", ret);
            ret = HH_OK;
            goto udavm_get_mwrm_err;
        }
        
        lkey =  uldm->uldm_pd_table[pd].u1.pd.lkey; /* get lkey saved with this PD */
        MTL_DEBUG4("THH_uldm_free_pd: DEREGISTERING mem region with lkey = 0x%x\n", lkey); 
        ret = THH_mrwm_deregister_mr(mrwm, lkey);
        if (ret != HH_OK) {
            MTL_ERROR1("THH_uldm_free_pd: THH_mrwm_deregister_mr error (%d)\n", ret);
            ret = HH_OK;
            goto  mrwm_unregister_err;
        }
        
        udav_virt_addr = uldm->uldm_pd_table[pd].u1.pd.udav_table_ddr_virt_addr;
        udav_phys_addr = uldm->uldm_pd_table[pd].u1.pd.udav_table_ddr_phys_addr;
        udav_size      = uldm->uldm_pd_table[pd].u1.pd.udav_table_size;
        
        MTL_DEBUG4("THH_uldm_free_pd: PD %d: udav_phys_addr =" PHYS_ADDR_FMT ", udav_virt_addr = " VIRT_ADDR_FMT  ", udav_size = "SIZE_T_FMT"\n", 
                   pd, udav_phys_addr, udav_virt_addr, udav_size);
        /* If UDAV was allocated in DDR, free up the DDR memory here */
        if (udav_phys_addr != (MT_phys_addr_t) 0) {
            ret = THH_hob_get_ddrmm(uldm->hob,&ddrmm);
            if (ret != HH_OK) {
                MTL_ERROR1("THH_uldm_free_pd: could not acquire DDRMM handle (%d)\n", ret);
                ret = HH_OK;
                goto udavm_get_ddrmm_err;
            }
    
            if ((res = MOSAL_unmap_phys_addr( uldm->uldm_pd_table[pd].u1.pd.prot_ctx, 
                                        (MT_virt_addr_t) udav_virt_addr, 
                                        udav_size )) != MT_OK) {
                MTL_ERROR1("THH_uldm_free_pd: MOSAL_unmap_phys_addr error for udavm:%d\n", res);
                ret = HH_OK;
                goto unmap_phys_addr_err;
            }
            ret = THH_ddrmm_free(ddrmm, udav_phys_addr, udav_size);
            if (ret != HH_OK) {
                MTL_ERROR1("THH_uldm_free_pd: THH_ddrmm_free error (%d)\n", ret);
                ret = HH_OK;
                goto  ddrmm_free_err;
            }
        }
    }
    ret = HH_OK;
    goto ok_retn;

ddrmm_free_err:
  unmap_phys_addr_err:
udavm_get_ddrmm_err:
mrwm_unregister_err:
udavm_get_mwrm_err:
ok_retn:
    /* make resource inaccessible */
    uldm->uldm_pd_table[pd].valid = FALSE;
    epool_free(&(uldm->pd_list), pd);

get_udavm_info_err:
err_retn:
    MTL_DEBUG4("<== THH_uldm_free_pd. ret = %d\n", ret);
    return ret;
}

/******************************************************************************
 *  Function:     THH_uldm_get_protection_ctx 
 *****************************************************************************/
HH_ret_t THH_uldm_get_protection_ctx( /*IN*/ THH_uldm_t                 uldm, 
                                      /*IN*/ HH_pd_hndl_t               pd, 
                                      /*OUT*/ MOSAL_protection_ctx_t    *prot_ctx_p )
{
    HH_ret_t  ret = HH_OK;

    MTL_DEBUG4("==> THH_uldm_get_protection_ctx. uldm = %p, pd = %d, prot_ctx_p = %p\n", 
               (void *) uldm, pd, (void *)prot_ctx_p);
    if (uldm == (THH_uldm_t)THH_INVALID_HNDL) {
        MTL_ERROR1("THH_uldm_get_protection_ctx: ERROR : HCA device has not yet been opened\n");
        ret = HH_EINVAL;
        goto err_retn;
    }
    
    /* protect against bad calls */
    if (uldm == (THH_uldm_t)0) {
        MTL_ERROR1("THH_uldm_get_protection_ctx: ERROR : uldm handle is ZERO\n");
        ret = HH_EINVAL;
    goto err_retn;
}

    if (pd > uldm->max_pd ) {
        MTL_ERROR1("THH_uldm_get_protection_ctx: pd out of range (%d)\n", pd);
        ret = HH_EINVAL;
        goto err_retn;
    }

    if (!(uldm->uldm_pd_table[pd].valid)) {
        if (pd != THH_RESERVED_PD) {
             MTL_ERROR1("THH_uldm_get_protection_ctx: pd was not allocated (%d)\n", pd);
        }
        ret = HH_EINVAL;
        goto err_retn;
    }

    *prot_ctx_p = uldm->uldm_pd_table[pd].u1.pd.prot_ctx;
#ifndef __DARWIN__
    MTL_DEBUG4("THH_uldm_get_protection_ctx. ctx  = %d\n", *prot_ctx_p);
#else
    MTL_DEBUG4("THH_uldm_get_protection_ctx\n");
#endif

err_retn:
    MTL_DEBUG4("<== THH_uldm_get_protection_ctx. ret = %d\n", ret);
    return ret;
}

HH_ret_t THH_uldm_get_num_objs( /*IN*/ THH_uldm_t uldm, u_int32_t *num_alloc_us_res_p,
                                           u_int32_t  *num_alloc_pds_p)
{
    u_int32_t i;
    u_int32_t  alloc_res = 0;

    MTL_DEBUG4("==> THH_uldm_get_num_objs\n");
    if ((uldm == (THH_uldm_t)THH_INVALID_HNDL) ||
         (uldm == (THH_uldm_t)0) ||
        (!num_alloc_us_res_p && !num_alloc_pds_p)){
        MTL_ERROR1("THH_uldm_get_num_objs: Invalid uldm handle, or all return params are NULL\n");
        return HH_EINVAL;
    }
    
    for (i=0; i< uldm->max_uar; i++) {
        if (uldm->uldm_uar_table[i].valid) {
            /* is uar valid check is within this function */
            alloc_res++;
        }
    }
    if (num_alloc_us_res_p) {
        *num_alloc_us_res_p=alloc_res;
    }

    alloc_res = 0;
    for (i=0; i< uldm->max_pd; i++) {
        if (uldm->uldm_pd_table[i].valid) {
            /* is uar valid check is within this function */
            alloc_res++;
        }
    }
    if (num_alloc_pds_p) {
        *num_alloc_pds_p=alloc_res;
    }
    
    return HH_OK;
}

