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

#if !defined(_TMRW__H)
#define _TMRW__H

#include <mtl_common.h>
#include <vapi_types.h>
#include <mosal.h>
#include <hh.h>
#include <cmdif.h>
#include <thh.h>
#include <thh_hob.h> /* just for THH_hob_t decl */

typedef u_int32_t            THH_pdm_t;

typedef struct
{
  u_int64_t  mtt_base;             /* Physical address of MTT */
  MT_phys_addr_t  mpt_base;        /* Physical address of MPT */
  u_int8_t   log2_mpt_sz;          /* Log2 of number of entries in MPT */
  u_int8_t   log2_mtt_sz;          /* Log2 of number of entries in the MTT */
  u_int8_t   log2_mtt_seg_sz;      /* Log2 of MTT segment size in entries */
  u_int8_t   log2_max_mtt_segs;    /* Log2 of maximum MTT segments possible */
  u_int8_t   log2_rsvd_mpts;       /* Log2 of number of MPTs reserved for firmware */
  u_int8_t   log2_rsvd_mtt_segs;   /* Log2 of number of MTT segments reserved for firmware */
  MT_size_t  max_mem_reg;          /* Max regions  in MPT for external */
  MT_size_t  max_mem_reg_internal; /* Max regions ... internal (WQEs & CQEs) */
  MT_size_t  max_mem_win;          /* Max memory windows in the MPT */
} THH_mrwm_props_t;


typedef struct
{
  IB_virt_addr_t   start;    /* Region start address in user virtual space */
  VAPI_size_t      size;     /* Region size */
  HH_pd_hndl_t     pd;       /* PD to associate with requested region */
  MOSAL_protection_ctx_t  vm_ctx; /* Virtual  context of given virt. address */
  MT_bool          force_memkey;  /* Allocate region with given memory key */
  VAPI_lkey_t      memkey;   /* Requested memory key (valid iff force_memkey) */

  /* Optional supplied physical buffers. Similar to HH_tpt_t.buf_lst */
  MT_size_t        num_bufs;      /*  != 0   iff   physical buffers supplied */
  VAPI_phy_addr_t*     phys_buf_lst;  /* size = num_bufs */
  VAPI_size_t*     buf_sz_lst;    /* [num_bufs], corresponds to phys_buf_lst */
} THH_internal_mr_t;

/************************************************************************
 *  Function: THH_mrwm_create
 *
 *  Arguments:
 *    hob
 *    mrwm_props - Tables sizes and allocation partioning
 *    mrwm_p -     The allocated THH_mrwm object
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters (MPT size given is smaller than
 *                total number of regions and windows, or NULL ptr.)
 *    HH_EAGAIN - Not enough resources in order to allocate object
 *
 *  Description:
 *    This function creates the THH_mrwm_t object instance in order to
 *    manage the MPT and MTT resources.
 */
extern HH_ret_t  THH_mrwm_create(
  THH_hob_t          hob,         /* IN  */
  THH_mrwm_props_t*  mrwm_props,  /* IN  */
  THH_mrwm_t*        mrwm_p       /* OUT */
);


/************************************************************************
 *  Function: THH_mrwm_destroy
 *
 *  Arguments:
 *    mrwm - Object to destroy
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Unknown object
 *
 *  Description:
 *    This function frees the THH_mrwm object resources.
 */
extern HH_ret_t  THH_mrwm_destroy(
  THH_mrwm_t  mrwm,        /* IN */
  MT_bool     hca_failure  /* IN */
);


/************************************************************************
 *  Function: THH_mrwm_register_mr
 *
 *  Arguments:
 *    mrwm
 *    mr_props - Memory region properties
 *    lkey_p   - L-Key allocated for region (to be used as region handle)
 *    rkey_p   - R-Key allocated for region (valid for remote access)
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters (properties or pointers)
 *    HH_EAGAIN - No free region resources available
 *
 *  Description:
 *    This function registers given memory region (virtual or physical -
 *    based on given mr_props_p).
 */
extern HH_ret_t  THH_mrwm_register_mr(
  THH_mrwm_t    mrwm,       /* IN  */
  HH_mr_t*      mr_props_p, /* IN  */
  VAPI_lkey_t*  lkey_p,     /* OUT */
  IB_rkey_t*    rkey_p      /* OUT */
);


/************************************************************************
 *  Function: THH_mrwm_register_internal
 *
 *  Arguments:
 *    mrwm
 *    mr_props_p - Requested internal memory region propetries
 *    memkey_p   - Memory key to use in order to access this region
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters
 *    HH_EAGAIN - No resources to allocate internal memory region
 *
 *  Description:
 *    For the WQEs and CQEs buffers internal memory registration is
 *    required in order enable access of the InifinHost to those
 *    buffers. This function performs a full memory registration operation
 *    in addition to the registration operation as done for
 *    THH_mrwm_register_mr(), i.e. it deals with locking the memory and
 *    getting physical pages table (which is done by the VIP layers for
 *    external memory registrations).
 */
extern HH_ret_t  THH_mrwm_register_internal(
  THH_mrwm_t          mrwm,        /* IN  */
  THH_internal_mr_t*  mr_props_p,  /* IN  */
  VAPI_lkey_t*        memkey_p     /* OUT */
);


/************************************************************************
 *  Function: THH_mrwm_reregister_mr
 *
 *  Arguments:
 *    mrwm
 *    lkey
 *    change_mask - Change request
 *    mr_props_p -  Updated memory region properties
 *    lkey_p -      
 *    rkey_p -      Returned R-key
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters
 *    HH_EAGAIN - Not enough resources to complete operation
 *
 *  Description:
 *    (see HH-API s HH_reregister_mr)
 */
extern HH_ret_t  THH_mrwm_reregister_mr(
  THH_mrwm_t        mrwm,         /* IN  */
  VAPI_lkey_t       lkey,
  VAPI_mr_change_t  change_mask,  /* IN  */
  HH_mr_t*          mr_props_p,   /* IN  */
  VAPI_lkey_t*       lkey_p,       /* OUT  */
  IB_rkey_t*        rkey_p        /* OUT */
);


/************************************************************************
 *  Function: THH_mrwm_register_smr
 *
 *  Arguments:
 *    mrwm
 *    smr_props_p - Shared memory region properties
 *    lkey_p -      Returned L-key
 *    rkey_p -      Returned R-key
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters (properties or pointers)
 *    HH_EAGAIN - No free region resources available
 *
 *  Description:
 *    This function uses the same physical pages (MTT) translation entries
 *    for a new region (new MPT entry).
 */
extern HH_ret_t  THH_mrwm_register_smr(
  THH_mrwm_t   mrwm,         /* IN  */
  HH_smr_t*    smr_props_p,  /* IN  */
  VAPI_lkey_t* lkey_p,       /* OUT */
  IB_rkey_t*   rkey_p        /* OUT */
);


/************************************************************************
 *  Function: THH_mrwm_query_mr
 *
 *  Arguments:
 *    mrwm
 *    lkey -      L-key of memory region as returned on registration
 *    mr_info_p - Returned memory region information
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters
 *
 *  Description:
 *    This function returns properties of registered memory region using
 *    region s L-key as a handle.
 */
extern HH_ret_t  THH_mrwm_query_mr(
  THH_mrwm_t    mrwm,      /* IN  */
  VAPI_lkey_t   lkey,      /* IN  */
  HH_mr_info_t* mr_info_p  /* OUT */
);


/************************************************************************
 *  Function: THH_mrwm_deregister_mr
 *
 *  Arguments:
 *    mrwm
 *    lkey - L-key of region to deregister
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Unknown region
 *    HH_EBUSY - Given region is still bounded to memory windows
 *
 *  Description:
 *    This function frees given memory region resources (unless memory
 *    windows are still bounded to it).
 */
extern HH_ret_t  THH_mrwm_deregister_mr(
  THH_mrwm_t   mrwm, /* IN  */
  VAPI_lkey_t  lkey  /* IN  */
);


/************************************************************************
 *  Function: THH_mrwm_alloc_mw
 *
 *  Arguments:
 *    mrwm
 *    pd -             The protection domain of the allocated window
 *    initial_rkey_p - R-Key to be used for first binding request
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters (unknown PD or NULL ptr.)
 *    HH_EAGAIN - No available MPT resources
 *
 *  Description:
 *    Allocate MPT entry for a memory window.
 */
extern HH_ret_t  THH_mrwm_alloc_mw(
  THH_mrwm_t    mrwm,          /* IN  */
  HH_pd_hndl_t  pd,            /* IN  */
  IB_rkey_t*    initial_rkey_p /* OUT */
);


/************************************************************************
 *  Function: THH_mrwm_query_mw
 *
 *  Arguments:
 *    mrwm
 *    initial_rkey -   R-Key received on window allocation
 *    current_rkey_p - The current R-Key associated with this window
 *    pd_p -           The protection domain of this window
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters (unknown window or NULL ptr.)
 *
 *  Description:
 *    Return properties of given memory window (initial R-Key used as a handle).
 */
extern HH_ret_t  THH_mrwm_query_mw(
  THH_mrwm_t    mrwm,            /* IN  */
  IB_rkey_t     initial_rkey,    /* IN  */
  IB_rkey_t*    current_rkey_p,  /* OUT */
  HH_pd_hndl_t* pd_p             /* OUT */
);


/************************************************************************
 *  Function: THH_mrwm_free_mw
 *
 *  Arguments:
 *    mrwm
 *    initial_rkey - R-Key received on window allocation
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters (initial_rkey does not match
 *                any memory window)
 *
 *  Description:
 *    Free the MPT resources associated with given memory window.
 */
extern HH_ret_t  THH_mrwm_free_mw(
  THH_mrwm_t  mrwm,         /* IN  */
  IB_rkey_t   initial_rkey  /* IN  */
);



/************************************************************************
 * Fast memory region
 ************************************************************************/

HH_ret_t  THH_mrwm_alloc_fmr(THH_mrwm_t  mrwm,           /*IN*/
                             HH_pd_hndl_t   pd,          /*IN*/
                             VAPI_mrw_acl_t acl,         /*IN*/
                             MT_size_t      max_pages,   /*IN*/   
                             u_int8_t       log2_page_sz,/*IN*/
                             VAPI_lkey_t*   last_lkey_p);/*OUT*/

HH_ret_t  THH_mrwm_map_fmr(THH_mrwm_t  mrwm,             /*IN*/
                           VAPI_lkey_t      last_lkey,   /*IN*/
                           EVAPI_fmr_map_t* map_p,       /*IN*/
                           VAPI_lkey_t*     lkey_p,      /*OUT*/
                           IB_rkey_t*       rkey_p);     /*OUT*/

HH_ret_t  THH_mrwm_unmap_fmr(THH_mrwm_t  mrwm,                  /*IN*/
                             u_int32_t     num_of_fmrs_to_unmap,/*IN*/
                             VAPI_lkey_t*  last_lkeys_array);   /*IN*/

HH_ret_t  THH_mrwm_free_fmr(THH_mrwm_t  mrwm,           /*IN*/
                            VAPI_lkey_t    last_lkey);  /*IN*/

/* debug info */
HH_ret_t THH_mrwm_get_num_objs(THH_mrwm_t mrwm,u_int32_t *num_mr_int_p, 
                                u_int32_t *num_mr_ext_p,u_int32_t *num_mws_p );


#if defined(MT_SUSPEND_QP)
HH_ret_t  THH_mrwm_suspend_internal(
  THH_mrwm_t    mrwm,         /* IN */
  VAPI_lkey_t   lkey,         /* IN */
  MT_bool       suspend_flag  /* IN */
  );
#endif
#endif /* _TMRW__H */
