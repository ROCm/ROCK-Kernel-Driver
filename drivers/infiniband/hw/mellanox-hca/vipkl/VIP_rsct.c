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

#include "VIP_rsct.h"
#include <vipkl.h>

#define VIP_RSCT_DELAY_TIME 100000 
#define VIP_RSCT_MAX_ITER   3000

VIP_ret_t VIP_RSCT_create(VIP_RSCT_t* obj_p,
                          VIP_hca_hndl_t hca_hndl,
                          MOSAL_protection_ctx_t prot,
                          MT_size_t hca_ul_resources_sz,
                          void* hca_ul_resources_p,
                          EM_async_ctx_hndl_t *async_hndl_ctx_p)
{
    int i;
    VIP_ret_t ret = VIP_OK;

    ret = VIPKL_alloc_ul_resources(hca_hndl,prot,hca_ul_resources_sz,
                                   hca_ul_resources_p,async_hndl_ctx_p);
    if (ret != VIP_OK) {
       return VIP_EAGAIN; 
    }
    
    *obj_p = (struct VIP_RSCT_proc_hca_ctx_t*)MALLOC(sizeof(struct VIP_RSCT_proc_hca_ctx_t));
    if ((*obj_p) == NULL) {
        ret = VIPKL_free_ul_resources(hca_hndl,hca_ul_resources_sz,hca_ul_resources_p,
                                      *async_hndl_ctx_p);
        return VIP_EAGAIN;
    }
    (*obj_p)->hca_hndl = hca_hndl;
    (*obj_p)->async_ctx_hndl = *async_hndl_ctx_p;
    (*obj_p)->hca_ul_resources_sz = hca_ul_resources_sz;
    (*obj_p)->hca_ul_resources_p = MALLOC(hca_ul_resources_sz);
    if (!(*obj_p)->hca_ul_resources_p) {
        MTL_ERROR1("failed allocating ul res buf \n");
        ret = VIPKL_free_ul_resources(hca_hndl,hca_ul_resources_sz,hca_ul_resources_p,
                                      *async_hndl_ctx_p);
        FREE(*obj_p);
        return VIP_EAGAIN;
    }
    memcpy((*obj_p)->hca_ul_resources_p,hca_ul_resources_p,hca_ul_resources_sz);

//    MTL_DEBUG1("hca hndl: %d \n",(*obj_p)->hca_hndl);
  //  MTL_DEBUG1("ul res: %p \n",(*obj_p)->hca_ul_resources_p);
  //  MTL_DEBUG1("ul res sz: %d \n",(*obj_p)->hca_ul_resources_sz);

    for (i=0; i< VIP_RSCT_MAX_RSC; i++) {
        (*obj_p)->rsc_array[i] = NULL;
    }
    for (i=0; i< VIP_RSCT_MAX_RSC; i++) {
        MOSAL_spinlock_init(&((*obj_p)->rsc_lock_array[i]));
    }

    MTL_DEBUG1("VIP_RSCT_create \n");
    return VIP_OK;
}



VIP_ret_t VIP_RSCT_destroy(VIP_RSCT_t rsct)
{
    int i,j=0;
    VIP_ret_t ret = VIP_OK;
    VIP_RSCT_rscinfo_t   *it,*next_it;   /* iterators */
    MT_bool had_busy = TRUE;
        
    FUNC_IN;
    
    
    while (had_busy && (j<VIP_RSCT_MAX_ITER) ) {
        had_busy = FALSE;
	    for (i=0; i< VIP_RSCT_MAX_RSC; i++) {
            switch (i) {
              case VIP_RSCT_EQ:
                  next_it = rsct->rsc_array[VIP_RSCT_EQ];
                  while (next_it != NULL) {
                      it = next_it;
                      next_it = it->next;
                      MTL_DEBUG1("VIP_RSCT_destroy : destroy eq \n");
                      ret = VIPKL_EQ_del(rsct,rsct->hca_hndl,it->rsc_hndl.rsc_eq_hndl);
                      if (ret == VIP_EBUSY) {
                        MTL_DEBUG1(MT_FLFMT("VIPKL_EQ_del returned BUSY"));
    		                had_busy = TRUE;
                      }
                  }
                  break;
              case VIP_RSCT_CQBLK:
                  next_it= rsct->rsc_array[VIP_RSCT_CQBLK];
                  while (next_it != NULL) {
                      it = next_it;
                      next_it = it->next;
                      MTL_DEBUG1("VIP_RSCT_destroy : destroy cqblk \n");
                      ret = VIPKL_cqblk_free_ctx(rsct,rsct->hca_hndl,it->rsc_hndl.rsc_cqblk_hndl);
                      if (ret == VIP_EBUSY) {
    		            had_busy = TRUE;
                    MTL_DEBUG1(MT_FLFMT("VIPKL_cqblk_free_ctx returned BUSY"));
                      }
                  }
                  break;
            case VIP_RSCT_MW:
                next_it= rsct->rsc_array[VIP_RSCT_MW];
                while (next_it != NULL) {
                    it = next_it;
                    next_it = it->next;
                    MTL_DEBUG1("VIP_RSCT_destroy : destroy mw \n");
                    ret = VIPKL_destroy_mw(rsct,rsct->hca_hndl,it->rsc_hndl.rsc_mw_hndl);
                    if (ret == VIP_EBUSY) {
    		            had_busy = TRUE;
                    MTL_DEBUG1(MT_FLFMT("VIPKL_destroy_mw returned BUSY"));
                    }
                    
                }
                break;
            case VIP_RSCT_QP:
                next_it= rsct->rsc_array[VIP_RSCT_QP];
                while (next_it != NULL) {
                    it = next_it;
                    next_it = it->next;
                    MTL_DEBUG1("VIP_RSCT_destroy : destroy qp \n");
                    ret = VIPKL_destroy_qp(rsct,rsct->hca_hndl,it->rsc_hndl.rsc_qp_hndl,TRUE);
                    if (ret == VIP_EBUSY) {
    		            had_busy = TRUE;
                    MTL_DEBUG1(MT_FLFMT("VIPKL_destroy_qp returned BUSY"));
                    }
                    
                }
                break;
              case VIP_RSCT_SRQ:
                  next_it= rsct->rsc_array[VIP_RSCT_SRQ];
                  while (next_it != NULL) {
                      it = next_it;
                      next_it = it->next;
                      MTL_DEBUG1("VIP_RSCT_destroy : destroy srq \n");
                      ret = VIPKL_destroy_srq(rsct,rsct->hca_hndl,it->rsc_hndl.rsc_srq_hndl);
                      if (ret == VIP_EBUSY) {
                        had_busy = TRUE;
                        MTL_DEBUG1(MT_FLFMT("VIPKL_destroy_srq returned BUSY"));
                      }

                  }
                  break;
            case VIP_RSCT_CQ:
                next_it= rsct->rsc_array[VIP_RSCT_CQ];
                while (next_it != NULL) {
                    it = next_it;
                    next_it = it->next;
                    MTL_DEBUG1("VIP_RSCT_destroy : destroy cq \n");
                    ret = VIPKL_destroy_cq(rsct,rsct->hca_hndl,it->rsc_hndl.rsc_cq_hndl,TRUE);
                    if (ret == VIP_EBUSY) {
    		            had_busy = TRUE;
                        MTL_DEBUG1(MT_FLFMT("VIPKL_destroy_cq returned BUSY"));
                    }
                }
                break;
            case VIP_RSCT_MR:
                next_it= rsct->rsc_array[VIP_RSCT_MR];
                while (next_it != NULL) {
                    it = next_it;
                    next_it = it->next;
                    MTL_DEBUG1("VIP_RSCT_destroy : destroy mr \n");
                    ret = VIPKL_destroy_mr(rsct,rsct->hca_hndl,it->rsc_hndl.rsc_mr_hndl);
                    if (ret == VIP_EBUSY) {
    		            had_busy = TRUE;
                        MTL_DEBUG1(MT_FLFMT("VIPKL_destroy_mr returned BUSY"));
                    }
                   
                }
                break;
            case VIP_RSCT_FMR:    
                next_it= rsct->rsc_array[VIP_RSCT_FMR];
                while (next_it != NULL) {
                    it = next_it;
                    next_it = it->next;
                    MTL_DEBUG1("VIP_RSCT_destroy : destroy fmr \n");
                    ret = VIPKL_free_fmr(rsct,rsct->hca_hndl,it->rsc_hndl.rsc_mr_hndl);
                    if (ret == VIP_EBUSY) {
    		            had_busy = TRUE;
                        MTL_DEBUG1(MT_FLFMT("VIPKL_free_fmr returned BUSY"));
                    }
                }
                break;
            case VIP_RSCT_PD:  
                next_it= rsct->rsc_array[VIP_RSCT_PD];
                while (next_it != NULL) {
                    MTL_DEBUG1("VIP_RSCT_destroy : destroy pd \n");
                    it = next_it;
                    next_it = it->next;
                    ret = VIPKL_destroy_pd(rsct,rsct->hca_hndl,it->rsc_hndl.rsc_pd_hndl);
                    if (ret == VIP_EBUSY) {
    		            had_busy = TRUE;
                    MTL_DEBUG1(MT_FLFMT("VIPKL_destroy_pd returned BUSY"));
                    }
                }
                break;
            case VIP_RSCT_DEVMEM:  
                next_it= rsct->rsc_array[VIP_RSCT_DEVMEM];
                while (next_it != NULL) {
                    MTL_DEBUG1("VIP_RSCT_destroy : destroy pd \n");
                    it = next_it;
                    next_it = it->next;
                    ret = VIPKL_free_unmap_devmem(rsct,rsct->hca_hndl,it->rsc_hndl.rsc_devmem_hndl);
                    if (ret == VIP_EBUSY) {
    		            had_busy = TRUE;
                    MTL_DEBUG1(MT_FLFMT("VIPKL_free_unmap_devmem returned BUSY"));
                    }
                }
                break;
            default: ;
    
        }
      }/* for (i,..)  */
        
      if (had_busy) MOSAL_delay_execution(VIP_RSCT_DELAY_TIME);
      j++;
    } /* while had_busy.. */
    
    if ( j== VIP_RSCT_MAX_ITER && had_busy) {
        MTL_ERROR1(MT_FLFMT("quitting after %d iterations: there are still busy resources in HCA \n"),
                   VIP_RSCT_MAX_ITER);
    }
    
    MTL_DEBUG1("before free ul resources rsct->async_ctx_hndl=%d\n", rsct->async_ctx_hndl);
    VIPKL_free_ul_resources(rsct->hca_hndl,rsct->hca_ul_resources_sz,
                                  rsct->hca_ul_resources_p, rsct->async_ctx_hndl);
    
    FREE(rsct->hca_ul_resources_p);
    //MTL_DEBUG1("after free rsct ul resources \n");
    FREE(rsct);
    
    MT_RETURN(ret);
}

/*
 *  VIP_RSCT_check_usr_ctx
 */
VIP_ret_t VIP_RSCT_check_usr_ctx(VIP_RSCT_t usr_ctx, VIP_RSCT_rscinfo_t *rsc_p)
{

    if (usr_ctx == VIP_RSCT_IGNORE_CTX) {
        return VIP_OK;
    }

    
    if (rsc_p == NULL) {
        MTL_ERROR1("VIP RSCT check: sanity check 1 failed \n");
        return VIP_EINVAL_PARAM;
    }
    
    //MTL_DEBUG1("got usr_ctx: %p saved usr_ctx:%p\n",usr_ctx,rsc_p->usr_ctx);
    
    if (rsc_p->usr_ctx != usr_ctx) {
        MTL_DEBUG1(MT_FLFMT("VIP_RSCT_check_usr_ctx: ERROR. usr_ctx(=%p) != rsc_p->usr_ctx(=%p)"),
                        usr_ctx,rsc_p->usr_ctx);
        return VIP_EPERM;
    }

    MTL_DEBUG2("VIP_RSCT: check ctx: OK \n");

    return VIP_OK;

}

VIP_ret_t VIP_RSCT_register_rsc(VIP_RSCT_t usr_ctx,VIP_RSCT_rscinfo_t* rsc_p, VIP_RSCT_rsctype_t rsc_type,
                                VIP_RSCT_rschndl_t rsc_h)
{


    if (rsc_type >= VIP_RSCT_MAX_RSC) {
        MTL_ERROR1("VIP RSCT reg: sanity check 2 failed \n");
        return VIP_EINVAL_PARAM;
    }

    FUNC_IN;

    MTL_DEBUG1("usr_ctx: %p \n",usr_ctx);
    MTL_DEBUG1("rsc type: %d \n",rsc_type);

    if (rsc_p == NULL) {
        MTL_ERROR1("VIP RSCT reg: sanity check 1 failed \n");
        return VIP_EINVAL_PARAM;
    }
    rsc_p->usr_ctx = usr_ctx;

    if (usr_ctx == VIP_RSCT_NULL_USR_CTX) {
        return VIP_OK;
    }

    switch (rsc_type) {
    case VIP_RSCT_PD:        rsc_p->rsc_hndl.rsc_pd_hndl = rsc_h.rsc_pd_hndl;
                             break;
    case VIP_RSCT_EQ:        rsc_p->rsc_hndl.rsc_eq_hndl = rsc_h.rsc_eq_hndl;
                             break;
    case VIP_RSCT_QP:        rsc_p->rsc_hndl.rsc_qp_hndl = rsc_h.rsc_qp_hndl;
                               break;
    case VIP_RSCT_SRQ:       rsc_p->rsc_hndl.rsc_srq_hndl = rsc_h.rsc_srq_hndl;
                             break;
    case VIP_RSCT_CQ:        rsc_p->rsc_hndl.rsc_cq_hndl = rsc_h.rsc_cq_hndl;
                             break;
    case VIP_RSCT_FMR:
    case VIP_RSCT_MR:        rsc_p->rsc_hndl.rsc_mr_hndl = rsc_h.rsc_mr_hndl;
                             break;
    case VIP_RSCT_MW:        rsc_p->rsc_hndl.rsc_mw_hndl = rsc_h.rsc_mw_hndl;
                             break;
    case VIP_RSCT_CQBLK:     rsc_p->rsc_hndl.rsc_cqblk_hndl = rsc_h.rsc_cqblk_hndl;
                             break;
    case VIP_RSCT_DEVMEM:     rsc_p->rsc_hndl.rsc_devmem_hndl = rsc_h.rsc_devmem_hndl;
                             break;

    default: MTL_ERROR1(MT_FLFMT("error: invalid rsct type (%d) !!"), rsc_type);
             return VIP_EINVAL_PARAM; 
    }
    rsc_p->prev = NULL;
    MOSAL_spinlock_lock(&usr_ctx->rsc_lock_array[rsc_type]);
    if ((rsc_type == VIP_RSCT_EQ) &&  (usr_ctx->rsc_array[rsc_type] != NULL) &&
        (usr_ctx->rsc_array[rsc_type]->next != NULL)) { 
        /* Limit to 2 EQs per process (completion+async.) */
        MOSAL_spinlock_unlock(&usr_ctx->rsc_lock_array[rsc_type]);
        MTL_ERROR1(MT_FLFMT("%s: Current process (pid="MT_ULONG_PTR_FMT") has already allocated 2 EQs \n"),__func__,
                   MOSAL_getpid());
        return VIP_EAGAIN;
    }
    rsc_p->next = usr_ctx->rsc_array[rsc_type];
    usr_ctx->rsc_array[rsc_type] = rsc_p;
    if (rsc_p->next)
        rsc_p->next->prev = rsc_p;
    MOSAL_spinlock_unlock(&usr_ctx->rsc_lock_array[rsc_type]);
    
    MT_RETURN(VIP_OK);
}


VIP_ret_t VIP_RSCT_deregister_rsc(VIP_RSCT_t usr_ctx,VIP_RSCT_rscinfo_t* rsc_p, VIP_RSCT_rsctype_t rsc_type)
{


    if (usr_ctx == VIP_RSCT_NULL_USR_CTX) {
        MTL_TRACE1(" NULL usr ctx \n");
        return VIP_OK;
    }

    if (usr_ctx == VIP_RSCT_IGNORE_CTX) {
        MTL_TRACE1(" failed SANITY CHECK: trying to close hca while resources are still allocated \n");
        return VIP_OK;
    }


    if (rsc_type >= VIP_RSCT_MAX_RSC) {
        MTL_ERROR1("VIP RSCT reg: sanity check 2 failed \n");
        return VIP_EINVAL_PARAM;
    }

    if (rsc_p == NULL) {
        MTL_ERROR1("VIP RSCT dereg: sanity check 1 failed \n");
        return VIP_EINVAL_PARAM;
    }

    FUNC_IN;
    
    MTL_DEBUG1("usr_ctx: %p \n",usr_ctx);

    MOSAL_spinlock_lock(&usr_ctx->rsc_lock_array[rsc_type]);
        if (rsc_p->prev) {
            rsc_p->prev->next = rsc_p->next;
        }else usr_ctx->rsc_array[rsc_type]  = rsc_p->next;
        if (rsc_p->next) {
            rsc_p->next->prev = rsc_p->prev;
        }
    MOSAL_spinlock_unlock(&usr_ctx->rsc_lock_array[rsc_type]);

    MT_RETURN(VIP_OK);
}







