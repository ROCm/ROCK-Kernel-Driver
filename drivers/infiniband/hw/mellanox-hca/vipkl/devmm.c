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

#include "devmm.h"
//#include <mosal.h>
#include <vip_array.h>
#include <hh.h>
#include <hobkl.h>
#include <vapi_common.h>

#define DEVMM_NUM_MEM_TYPES 1

struct DEVMM_t {
    HOBKL_hndl_t hob_hndl;   
    VIP_array_p_t mems;  
}; 

typedef struct DEVMM_dm_t{
    VAPI_size_t  bsize;
    VAPI_phy_addr_t buf;
    MT_virt_addr_t  virt_buf;
    EVAPI_devmem_type_t mem_type;
    MT_bool is_kernel_prot_ctx;
    VIP_RSCT_rscinfo_t rsc_ctx;
}DEVMM_dm_t;

static void VIP_free(void* p)
{
    DEVMM_dm_t* chunk_p = (DEVMM_dm_t*)p;
    MOSAL_prot_ctx_t prot_ctx = (chunk_p->is_kernel_prot_ctx == FALSE) ? 
                                    MOSAL_get_current_prot_ctx() : MOSAL_get_kernel_prot_ctx();
    MTL_ERROR1(MT_FLFMT("DEVMM delete:found unreleased devmm object"));        
    if (chunk_p->virt_buf) 
    {
        call_result_t cr= MOSAL_unmap_phys_addr(prot_ctx,chunk_p->virt_buf,chunk_p->bsize);
        if (cr != MT_OK) {MTL_ERROR1(MT_FLFMT("VIP_free: error: unmap_phys failed \n")); }
    }
    FREE(p); 
}

/****************************************************************************************
 *
 *  DEMM_new
 *
 ****************************************************************************************/
VIP_ret_t DEVMM_new( HOBKL_hndl_t hob_hndl, DEVMM_hndl_t *devmm_p)
{
   VIP_ret_t rc = VIP_OK;
   VAPI_hca_cap_t hca_cap;
   DEVMM_hndl_t devmm_hndl;
      
   MTL_DEBUG1("inside DEVMM_new \n");
      
   /* get HCA capabilities */
   rc = HOBKL_query_cap(hob_hndl, &hca_cap);
   if ( rc != VIP_OK ) return rc;

   /* allocate DEVMM handle */
   devmm_hndl = (DEVMM_hndl_t)MALLOC(sizeof(struct DEVMM_t));
   if ( !devmm_hndl ) return VIP_EAGAIN;
   memset(devmm_hndl, 0, sizeof(struct DEVMM_t));
   *devmm_p = devmm_hndl;

   devmm_hndl->hob_hndl = hob_hndl; 

   rc = VIP_array_create(0,&devmm_hndl->mems);
   if (rc != VIP_OK) {
        FREE(devmm_hndl);
   }
   
   return rc;
}

/****************************************************************************************
 *
 *  DEVMM_delete
 *
 ****************************************************************************************/
VIP_ret_t DEVMM_delete(DEVMM_hndl_t devmm)
{
   //VIP_array_handle_t hndl;
   //DEVMM_dm_t* chunk_p;
   //VIP_ret_t rc;

   if (devmm == NULL ) return VIP_EINVAL_DEVMM_HNDL;

   VIP_array_destroy(devmm->mems,VIP_free);
   FREE(devmm);
   return VIP_OK;
}
 
/****************************************************************************************
 *
 *  DEVMM_alloc_devmem
 *
 ****************************************************************************************/
VIP_ret_t DEVMM_alloc_map_devmem(VIP_RSCT_t usr_ctx,DEVMM_hndl_t devmm, EVAPI_devmem_type_t mem_type, 
                             VAPI_size_t  bsize,u_int8_t align_shift,VAPI_phy_addr_t* buf_p,
                             void** virt_addr_p,DEVMM_dm_hndl_t* hndl_p)
{
    VIP_ret_t rc = VIP_OK;
    DEVMM_dm_t* chunk_p;
    HH_ret_t rc_hh = HH_OK;
    VIP_RSCT_rschndl_t r_h;
    MOSAL_prot_ctx_t prot_ctx = usr_ctx ? MOSAL_get_current_prot_ctx() : MOSAL_get_kernel_prot_ctx();
    VAPI_size_t fixed_bsize;   
    u_int8_t fixed_align_shift;
    unsigned int pg_shift = MOSAL_SYS_PAGE_SHIFT;

    FUNC_IN;

    if (devmm == NULL) {
        MT_RETURN(VIP_EINVAL_DEVMM_HNDL);
    }

    if (bsize == 0) {
        MT_RETURN(VIP_EINVAL_SIZE);
    }

    MTL_DEBUG1("DEVMM_alloc_map_devmm: bsize:"U64_FMT" align_shift:%d  \n",bsize,align_shift);
    chunk_p = (DEVMM_dm_t*)MALLOC(sizeof(DEVMM_dm_t));
    if (!chunk_p) {
        MT_RETURN(VIP_EAGAIN);
    }

    chunk_p->is_kernel_prot_ctx = (usr_ctx ? FALSE : TRUE);
    switch (mem_type) {
    case EVAPI_DEVMEM_EXT_DRAM: fixed_bsize = MT_UP_ALIGNX_U64(bsize,pg_shift);
                                if ((sizeof(MT_size_t) == 4) && (fixed_bsize > 0xFFFFFFFF)) {
                                    rc= VIP_EAGAIN;
                                    goto clean;
                                }
                                /* ceil align_shift to a multiple of page_shift */
                                fixed_align_shift= (align_shift >= pg_shift? align_shift : pg_shift);
                                MTL_DEBUG1(MT_FLFMT("before HH_ddrmm_alloc \n"));
                                MTL_DEBUG1(MT_FLFMT("warning: using bsize:"U64_FMT" - aligned to page size\n"),fixed_bsize);
                                MTL_DEBUG1(MT_FLFMT("warning: using align_shift:%d - mult of page size\n"),fixed_align_shift);

                                rc_hh = HH_ddrmm_alloc(HOBKL_get_hh_hndl(devmm->hob_hndl),fixed_bsize,
                                                       fixed_align_shift,buf_p);
                                MTL_DEBUG1(MT_FLFMT("HH_ddrmm_alloc returned %s \n"),VAPI_strerror_sym(rc_hh));                            
                                MTL_DEBUG1(MT_FLFMT("returned phys adrs: "U64_FMT" \n"),*buf_p);
                                if (rc_hh != HH_OK){
                                        rc = (rc_hh < VAPI_ERROR_MAX ? rc_hh : VAPI_EGEN);
                                        goto clean;
                                }
                                else break;
    
      default: rc= VIP_EINVAL_PARAM;
               MTL_ERROR1(MT_FLFMT("%s: Invalid memory type (%d)"), __func__, mem_type);
             goto clean;
    }

    if (virt_addr_p)
    {
             chunk_p->virt_buf = MOSAL_map_phys_addr(*buf_p,fixed_bsize,MOSAL_MEM_FLAGS_NO_CACHE |
                                                     MOSAL_MEM_FLAGS_PERM_READ | MOSAL_MEM_FLAGS_PERM_WRITE,
                                                     prot_ctx);
             if (chunk_p->virt_buf == (MT_virt_addr_t)0)
             {
                 MTL_ERROR1(MT_FLFMT("MOSAL_map_phys_addr failed \n"));
                 rc=VIP_EINVAL_PARAM; 
                 goto revert;
             }
             *virt_addr_p = (void*)chunk_p->virt_buf;
    }else chunk_p->virt_buf = (MT_virt_addr_t)0;
    chunk_p->buf = *buf_p;
    chunk_p->bsize = fixed_bsize;
    chunk_p->mem_type = mem_type;
    
    
    rc = VIP_array_insert(devmm->mems,chunk_p,hndl_p);
    if (rc != VIP_OK) {
        if (chunk_p->virt_buf) 
        {
                MOSAL_unmap_phys_addr(prot_ctx,chunk_p->virt_buf,chunk_p->bsize);
        }
        goto revert;
    }
    MTL_DEBUG1(MT_FLFMT("hndl: 0x%x \n"),*hndl_p);
    r_h.rsc_devmem_hndl = *hndl_p;
    VIP_RSCT_register_rsc(usr_ctx,&chunk_p->rsc_ctx,VIP_RSCT_DEVMEM,r_h);
    MT_RETURN(VIP_OK);

revert: 
    HH_ddrmm_free(HOBKL_get_hh_hndl(devmm->hob_hndl),*buf_p,fixed_bsize);
clean:    
    FREE(chunk_p);
    MT_RETURN(rc);    
}

/****************************************************************************************
 *
 *  DEMM_query_devmem
 *
 ****************************************************************************************/
VIP_ret_t DEVMM_query_devmem(VIP_RSCT_t usr_ctx,DEVMM_hndl_t devmm, EVAPI_devmem_type_t mem_type,
                             u_int8_t align_shift , EVAPI_devmem_info_t*  devmem_info_p)
{
    VIP_ret_t rc = VIP_OK;
    HH_ret_t  rc_hh = VIP_OK;
    VAPI_phy_addr_t tmp;
    u_int8_t fixed_align_shift = 0;
    unsigned int pg_shift = MOSAL_SYS_PAGE_SHIFT;

    FUNC_IN;

    if (devmm == NULL) {
        return VIP_EINVAL_DEVMM_HNDL;
    }

    MTL_DEBUG1(MT_FLFMT("got align_shift:%d \n"),align_shift);
    switch (mem_type) {
    case EVAPI_DEVMEM_EXT_DRAM:  /* ceil align_shift to a multiple of page_shift */
                                fixed_align_shift= (align_shift >= pg_shift? align_shift : pg_shift);
                                MTL_DEBUG1(MT_FLFMT("warning: using align_shift:%d - mult of page size\n"),fixed_align_shift);

                                rc_hh = HH_ddrmm_query(HOBKL_get_hh_hndl(devmm->hob_hndl),fixed_align_shift,         
                                                       &devmem_info_p->total_mem,
                                                       &devmem_info_p->free_mem,
                                                       &devmem_info_p->largest_chunk,
                                                       &tmp
                                                       ); 
                                break;
    default: 
      MTL_ERROR1(MT_FLFMT("%s: Invalid memory type (%d)"), __func__, mem_type);
      return VIP_EINVAL_PARAM;
    }

    if (rc_hh != HH_OK) {
      MTL_ERROR1("%s:CALLED HH_ddrmm_free. HH-specific return=%d \n", __func__, rc_hh);
      rc = (VIP_ret_t)rc_hh; 
    }
    
    MT_RETURN(rc);
}

/****************************************************************************************
 *
 *  DEMM_free_devmem
 *
 ****************************************************************************************/
VIP_ret_t DEVMM_free_unmap_devmem(VIP_RSCT_t usr_ctx,DEVMM_hndl_t devmm,DEVMM_dm_hndl_t dm_hndl)
{
    VIP_ret_t rc = VIP_OK;
    DEVMM_dm_t* chunk_p;
    HH_ret_t  rc_hh = HH_EINVAL;
    VIP_array_obj_t val;
    MOSAL_prot_ctx_t prot_ctx = usr_ctx ? MOSAL_get_current_prot_ctx() : MOSAL_get_kernel_prot_ctx();


    FUNC_IN;

    if (devmm == NULL) {
        return VIP_EINVAL_DEVMM_HNDL;
    }

    rc=VIP_array_find_hold(devmm->mems,dm_hndl,&val);
    if ( rc != VIP_OK ) 
    {
            MTL_ERROR1(MT_FLFMT("error: invalid dm hndl: 0x%x \n"),dm_hndl);
            MT_RETURN(VIP_EINVAL_PARAM);
    }
    chunk_p = (DEVMM_dm_t*)val;    
    rc = VIP_RSCT_check_usr_ctx(usr_ctx,&chunk_p->rsc_ctx);
    VIP_array_find_release(devmm->mems,dm_hndl);
    if (rc != VIP_OK) {
        MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. dm hndl=0x%x (%s)"),__func__,dm_hndl,VAPI_strerror_sym(rc));
        MT_RETURN(rc);
    }
    
    rc=VIP_array_erase(devmm->mems,dm_hndl,&val);
    if ( rc != VIP_OK )
    {
        MTL_ERROR1(MT_FLFMT("error: invalid devmm hndl: 0x%x \n"),dm_hndl);    
        MT_RETURN(VIP_EINVAL_PARAM);
    }
    chunk_p = (DEVMM_dm_t*)val;    
    
    if (chunk_p->virt_buf) 
    {
                call_result_t cr= MOSAL_unmap_phys_addr(prot_ctx,chunk_p->virt_buf,chunk_p->bsize);
                if (cr != MT_OK) {
                    MTL_ERROR1(MT_FLFMT("error: unmap_phys failed \n"));    
                    MT_RETURN(VIP_EINVAL_PARAM);

                }
    }

    switch (chunk_p->mem_type) {
    case EVAPI_DEVMEM_EXT_DRAM: rc_hh = HH_ddrmm_free(HOBKL_get_hh_hndl(devmm->hob_hndl),chunk_p->buf,chunk_p->bsize);
                                MTL_DEBUG1(MT_FLFMT("HH_ddrmm_free returned %s \n"),VAPI_strerror_sym(rc_hh));                            
                                if (rc_hh != HH_OK){
                                  MTL_ERROR1("%s:CALLED HH_ddrmm_free. HH-specific return=%d \n", __func__, rc_hh);
                                    rc = (rc_hh < VAPI_ERROR_MAX ? rc_hh : VAPI_EGEN);
                                }
                                break;
    default: 
      MTL_ERROR1(MT_FLFMT("%s: Invalid memory type (%d)"), __func__, chunk_p->mem_type);
      rc= VIP_EFATAL; /* unexpected error */
    }


    if (rc == VIP_OK) {
      rc = VIP_RSCT_deregister_rsc(usr_ctx,&chunk_p->rsc_ctx,VIP_RSCT_DEVMEM);
    } else { /* do not override rc */
      VIP_RSCT_deregister_rsc(usr_ctx,&chunk_p->rsc_ctx,VIP_RSCT_DEVMEM);
    }
    /* free the Memory Region object */
    FREE(chunk_p);
    MT_RETURN(rc);

}

VIP_ret_t DEVMM_get_num_alloc_chunks(DEVMM_hndl_t devmm, u_int32_t *num_alloc_chunks)
{
  /* check attributes */
  if ( devmm == NULL || devmm->mems == NULL) {
    return VIP_EINVAL_DEVMM_HNDL;
  }
  
  if (num_alloc_chunks == NULL) {
      return VIP_EINVAL_PARAM;
  }
  *num_alloc_chunks = VIP_array_get_num_of_objects(devmm->mems);
  return VIP_OK;
}

