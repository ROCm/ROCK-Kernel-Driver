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



#ifndef __MMU_DESCRIPTION
#define __MMU_DESCRIPTION

#include <mtl_common.h>
#include <vip_hash.h>
#include <vip_array.h>
#include <vip_hashp.h>
#include <hobkl.h>
#include <pdm.h>
#include <vip.h>  
#include <mosal.h>
#include <mmu.h>
#include <mt_bufpool.h>

 
#define MM_GOOD_ALLOC(sz)    VIP_SMART_MALLOC(sz)
#define MM_GOOD_FREE(ptr,sz)  VIP_SMART_FREE(ptr,sz)

#define MM_ALLOC(size) MALLOC(size)
/* malloc(size) */
#define MM_FREE(obj)   FREE(obj)

/* this struct is for MMU internal use */
typedef struct  {
    MM_VAPI_mro_t pub_props;          /* Region's public properties (see MM_VAPI_mro_t type) */
    VAPI_virt_addr_t start;
    VAPI_size_t size;
    VIP_RSCT_rscinfo_t rsc_ctx;
    MOSAL_iobuf_t iobuf; /* iobuf object */
    MOSAL_spinlock_t mr_lock;
    MT_bool mr_busy;
} MM_mro;

typedef struct {
	VAPI_rkey_t		init_key;
	PDM_pd_hndl_t	pd_h;
	MM_mrw_hndl_t	mm_mw_h;
    VIP_RSCT_rscinfo_t rsc_ctx;
} MM_mw;

typedef struct fmr_lock_info
{
    VAPI_virt_addr_t va;
    VAPI_size_t sz;
    MOSAL_iobuf_t iobuf;
}fmr_lock_info_t;

typedef struct {
    MM_key_t lkey, rkey;                     /* Local (Lkey) and Remote Protection Key (Rkey) */
    u_int32_t   max_page_num;                     /*region supports up to max_page_num mtt's*/
    u_int8_t    log2_page_sz;
    
    u_int32_t       max_outstanding_maps;
    
    PDM_pd_hndl_t pd_hndl;                     /* Protection Domain handle */
    MM_mr_acl_t acl;       /* Region access control list */
    VIP_RSCT_rscinfo_t rsc_ctx;

    MOSAL_spinlock_t fmr_lock;
} MM_fmr;


/* MMU instance handle  */
struct MMU_t {
    HOBKL_hndl_t hob_hndl;     /* Handle of HOB */
    VIP_hash_p_t mw_by_rkey;       /* Hash by Rkey */
    VIP_array_p_t mr_by_hndl;   /* Array by Memory Region handle  */
    VIP_array_p_t mw_by_hndl;   /* Array by Memory Region handle  */
    VIP_array_p_t fmr_by_hndl;   /* Array by Memory Region handle  */
    MT_bufpool_t    lkey_array_bufpool; /* Buffer pool for unmap_fmr */
    u_int32_t     max_map_per_fmr;
    VIP_delay_unlock_t   delay_unlocks;
}; 


#endif /*__MMU_DESCRIPTION */
