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
 
/* First to validate "tmrwm.h" is a legal header */
#include <tmrwm.h>
#if defined(USE_STD_MEMORY)
# include <memory.h>
#endif
#include <mtl_common.h>
#include <mosal.h>

#include <cmdif.h>
#include <epool.h>
#include <thh_uldm.h>
#include <thh.h>
#include <tlog2.h>
#include <tavor_if_defs.h>
#include <vip_common.h>
#include <vip_array.h>
#include <vapi_common.h>

#include "extbuddy.h"

#include <mosal.h>
#ifndef MTL_TRACK_ALLOC
#define EXTBUDDY_ALLOC_MTT(index,log2_n_segs) 
#define EXTBUDDY_FREE_MTT(index,log2_n_segs) 
#else
#define EXTBUDDY_ALLOC_MTT(index,log2_n_segs) \
  memtrack_alloc(MEMTRACK_MTT_SEG,(unsigned long) index,(unsigned long)(1<<log2_n_segs),__FILE__,__LINE__)
#define EXTBUDDY_FREE_MTT(index,log2_n_segs) memtrack_free(MEMTRACK_MTT_SEG,(unsigned long)index,__FILE__,__LINE__)
#endif

enum { TAVOR_LOG_MPT_PG_SZ_SHIFT = 12 };  /* log2(4K) */
#define LOG2_MTT_ENTRY_SZ 3

#define IFFREE(p)  if (p) { FREE(p); }
#define logIfErr(f) \
  if (rc != HH_OK) { MTL_ERROR1("%s: rc=%s\n", f, HH_strerror_sym(rc)); }

#define CURRENT_MEMKEY(mrwm,mpt_seg,mpt_index) \
  ( ((mrwm)->key_prefix[mpt_seg][mpt_index-(mrwm)->offset[mpt_seg]] << (mrwm)->props.log2_mpt_sz) | (mpt_index))

/* macro for translating cmd_rc return codes for non-destroy procs */
#define CMDRC2HH_ND(cmd_rc) ((cmd_rc == THH_CMD_STAT_OK) ? HH_OK : \
                          (cmd_rc == THH_CMD_STAT_EINTR) ? HH_EINTR : HH_EFATAL)
                          
/* Reg_Segs_t phys_pages array allocation/free */

#define SMART_MALLOC(size) THH_SMART_MALLOC(size)
#define SMART_FREE(ptr,size) THH_SMART_FREE(ptr,size)

#define ALLOC_PHYS_PAGES_ARRAY(reg_segs_p) \
  (reg_segs_p)->phys_pages= (VAPI_phy_addr_t*)SMART_MALLOC(sizeof(VAPI_phy_addr_t)*(reg_segs_p)->n_pages)

/* Free memory allocated in buf_lst_2_pages() into phys_pages of reg_segs */
#define FREE_PHYS_PAGES_ARRAY(reg_segs_p)  \
  SMART_FREE((reg_segs_p)->phys_pages,sizeof(VAPI_phy_addr_t)*(reg_segs_p)->n_pages); \
  (reg_segs_p)->phys_pages= NULL

static u_int8_t  native_page_shift;




#define DHERE  {MTL_DEBUG4(MT_FLFMT(""));}

enum
{
  /* These are the tunebale parameters */
  LOG2_MIN_SEG_SIZE  = 3,
  /* LOG2_MAX_SEGS      = 20, 1M limit [REMOVED! 2002/November/18] */
  MTT_WRITE_MAX      = 64,

  /* Better be given explicitly on creation (by firmware), hard code for now */
  MTT_LOG_MTT_ENTRY_SIZE = 3

};

typedef struct
{
   unsigned long  prev;
   unsigned long  next;
} _free_offs_t;

/*keeps all the data shared between shared mr's */
typedef struct
{
  VAPI_size_t     size;
  u_int32_t       seg_start;   /* segment index - given by EPool */
  u_int8_t        log2_segs;
  u_int8_t        page_shift;
  u_int32_t       ref_count; 
  MOSAL_spinlock_t ref_lock; /* May be removed when atomic is available */
} Shared_data_t;

/* Since remote=local for {key,start,size}
 * we can do with less than the whole HH_mr_info_t.
 * On the other hand, we do need to save some internal data.
 */
typedef struct
{
  VAPI_lkey_t     key;
  IB_virt_addr_t  start;
  HH_pd_hndl_t    pd;
  
  Shared_data_t* shared_p; 
  MOSAL_iobuf_t iobuf;    /* for internal regions only */

  /* the small fields, in the end */
  VAPI_mrw_acl_t  acl;

#if defined(MT_SUSPEND_QP)
  MT_bool         is_suspended;  /* cannot use mpt_entry field below, since mpt_entry
                                  * is set before iobuf_dereg when suspending
                                  */
  THH_mpt_entry_t *mpt_entry;    /* saved at suspend, for unsuspend */
  MT_virt_addr_t va;             /* saved at suspend, for unsuspend */
  MT_size_t size;                /* saved at suspend, for unsuspend */
  MOSAL_prot_ctx_t prot_ctx;     /* saved at suspend, for unsuspend */
#endif
  
} Mr_sw_t;

typedef struct {
  u_int32_t       seg_start;   /* segment index - given by EPool */
  u_int8_t        log2_segs;
  u_int8_t        log2_page_sz;
  MT_virt_addr_t  mtt_entries;             /* Mapped MTT entries */
  MT_virt_addr_t  mpt_entry;               /* Mapped MPT entry */
  u_int32_t last_free_key;        /* Last memory key that was explicitly freed 
                                   * (for key wrap around detection)          */
} FMR_sw_info_t;


typedef enum {
  MPT_int,
  MPT_ext,
  MPT_EOR, /* end of regions */
  MPT_win = MPT_EOR,
  MPT_N,
  MPT_reserved
} mpt_segment_t;

/* The MRWM main structure */
typedef struct THH_mrwm_st
{
  THH_hob_t         hob;
  THH_mrwm_props_t  props;
  u_int32_t usage_cnt[MPT_N]; /* current number of rgn_int,rgn_ext,win */
  VIP_array_p_t     mpt[MPT_N];   /* MPT array for each of the entities */
  u_int8_t*         is_fmr_bits;
  u_int32_t         max_mpt[MPT_N];/* limit size of each mpt array */
  u_int32_t         offset[MPT_N]; /* [MPT_int=0] = 0,  [MPT_EOR] = #regions */
  u_int16_t        *key_prefix[MPT_N]; /* persistant key prefix storage */
  MOSAL_spinlock_t  key_prefix_lock;/* protect key_prefix updates (2be changed to atomic_inc)*/
  /* u_int8_t          log2_seg_size; */
  Extbuddy_hndl     xbuddy_tpt;
  u_int32_t         surplus_segs;  /* # segs we can give over 1/region */
  MOSAL_spinlock_t  reserve_lock;  /* protect MPT (usage_cnt) and MTT (surplus_segs) reservations*/
  MOSAL_mutex_t     extbuddy_lock; /* protect extbuddy calls */

  /* convenient handle saving  */
  THH_cmd_t         cmd_if;
  THH_uldm_t        uldm;
} TMRWM_t;

/* place holder for parameters during registartion */
typedef struct
{
  THH_mrwm_t       mrwm;
  THH_mpt_entry_t  mpt_entry;
  u_int32_t        mpt_index; /* == lower_bits(mpt_entry.lkey) */
  VAPI_phy_addr_t*     phys_pages;
  MT_size_t        n_pages;
  u_int8_t         log2_page_size;
  u_int32_t        seg_start;
  u_int32_t        key;
  VAPI_mrw_acl_t   acl;
  MOSAL_iobuf_t iobuf; /* for internal regions */
} Reg_Segs_t;


/************************************************************************/
/*                         private functions                            */


static inline mpt_segment_t get_mpt_seg(THH_mrwm_t mrwm,u_int32_t mpt_index)
{
  if (mpt_index < mrwm->offset[MPT_int]) { /* internal region */
    return MPT_reserved;
  } else if (mpt_index < mrwm->offset[MPT_ext]) {
    return MPT_int;
  } else if (mpt_index < mrwm->offset[MPT_win]) {
    return MPT_ext;
  } /* else... */
  return MPT_win;
}

/********** MPT entries and MTT segments reservations *************/

      
/* reserve an MPT entry in given segment (to limit VIP_array's size) */
static inline HH_ret_t reserve_mpt_entry(THH_mrwm_t mrwm, mpt_segment_t mpt_seg)
{
  MOSAL_spinlock_lock(&mrwm->reserve_lock);
  if (mrwm->usage_cnt[mpt_seg] >= mrwm->max_mpt[mpt_seg]) {
    MOSAL_spinlock_unlock(&mrwm->reserve_lock);
    return HH_EAGAIN; /* reached limit for given segment */
  }
  MTL_DEBUG5(MT_FLFMT("%s: usage_cnt[%d]=%d -> %d"),__func__,
             mpt_seg,mrwm->usage_cnt[mpt_seg],mrwm->usage_cnt[mpt_seg]+1);
  mrwm->usage_cnt[mpt_seg]++;
  MOSAL_spinlock_unlock(&mrwm->reserve_lock);
  return HH_OK;
}

/* opposite of reserve_mpt */
static inline void release_mpt_entry(THH_mrwm_t mrwm, mpt_segment_t mpt_seg)
{
  MOSAL_spinlock_lock(&mrwm->reserve_lock);
#ifdef MAX_DEBUG
  if (mrwm->usage_cnt[mpt_seg] == 0) {
    MTL_ERROR1(MT_FLFMT("%s: Invoked while usage_cnt==0"),__func__);
  }
#endif
  MTL_DEBUG5(MT_FLFMT("%s: usage_cnt[%d]=%d -> %d"),__func__,
             mpt_seg,mrwm->usage_cnt[mpt_seg],mrwm->usage_cnt[mpt_seg]-1);
  mrwm->usage_cnt[mpt_seg]--;
  MOSAL_spinlock_unlock(&mrwm->reserve_lock);
}

/* reserve  MTT segments (to assure allocation in "extbuddy" structure) */
static inline HH_ret_t reserve_mtt_segs(THH_mrwm_t mrwm, u_int32_t surplus2reserve)
{
  MOSAL_spinlock_lock(&mrwm->reserve_lock);
  if (surplus2reserve > mrwm->surplus_segs) {
    MTL_ERROR4(MT_FLFMT("%s: Cannot reserve %d MTT segments (%d dynamic MTT segments left)"),
               __func__,surplus2reserve,mrwm->surplus_segs);
    MOSAL_spinlock_unlock(&mrwm->reserve_lock);
    return HH_EAGAIN; /* reached limit */
  }
  MTL_DEBUG5(MT_FLFMT("%s: MTT segments %d -> %d"),__func__,
             mrwm->surplus_segs,mrwm->surplus_segs-surplus2reserve);
  mrwm->surplus_segs -= surplus2reserve;
  MOSAL_spinlock_unlock(&mrwm->reserve_lock);
  return HH_OK;
}

static inline void release_mtt_segs(THH_mrwm_t mrwm, u_int32_t surplus2reserve)
{
  MOSAL_spinlock_lock(&mrwm->reserve_lock);
  MTL_DEBUG5(MT_FLFMT("%s: MTT segments %d -> %d"),__func__,
             mrwm->surplus_segs,mrwm->surplus_segs+surplus2reserve);
  mrwm->surplus_segs += surplus2reserve;
  MOSAL_spinlock_unlock(&mrwm->reserve_lock);
}

/************************************************************************/
static HH_ret_t  buf_lst_2_pages
(
  const VAPI_phy_addr_t*  phys_buf_lst,
  const VAPI_size_t*  buf_sz_lst,
  MT_size_t           n,
  IB_virt_addr_t      start,
  VAPI_phy_addr_t     iova_offset,
  Reg_Segs_t*         reg_segs
);


static inline HH_ret_t  tpt_buf_lst_2_pages(const HH_mr_t* mr, Reg_Segs_t* rs)
{
  const HH_tpt_t*   t = &mr->tpt;
  MTL_DEBUG4(MT_FLFMT("tpt_buf_lst_2_pages"));
  return buf_lst_2_pages(t->tpt.buf_lst.phys_buf_lst, t->tpt.buf_lst.buf_sz_lst,
                         t->num_entries, 
                         mr->start, t->tpt.buf_lst.iova_offset,
                         rs);
} /* tpt_buf_lst_2_pages */

/******************************************************/

static void release_shared_mtts(THH_mrwm_t mrwm, Mr_sw_t *mrsw_p)
{
  MOSAL_spinlock_lock(&mrsw_p->shared_p->ref_lock);
  if (mrsw_p->shared_p->ref_count > 1) {
    mrsw_p->shared_p->ref_count--;    
    MOSAL_spinlock_unlock(&mrsw_p->shared_p->ref_lock);
  }else{    
    MOSAL_spinlock_unlock(&mrsw_p->shared_p->ref_lock);
    /* MTT segment 0 is reserved, so if seg_start is 0 it is a physical addressing region (no MTTs)*/
    if (mrsw_p->shared_p->seg_start != 0) { 
      MOSAL_mutex_acq_ui(&mrwm->extbuddy_lock);
      extbuddy_free(mrwm->xbuddy_tpt, mrsw_p->shared_p->seg_start, mrsw_p->shared_p->log2_segs);
      EXTBUDDY_FREE_MTT(mrsw_p->shared_p->seg_start, mrsw_p->shared_p->log2_segs);
      MOSAL_mutex_rel(&mrwm->extbuddy_lock);
      release_mtt_segs(mrwm,(1 << mrsw_p->shared_p->log2_segs) - 1);
    }
    FREE(mrsw_p->shared_p); 
  }
  mrsw_p->shared_p = NULL;
}

static HH_ret_t change_translation(Reg_Segs_t* rs_p,HH_mr_t* mr_props_p, Mr_sw_t *mrsw_p,
                                   u_int8_t *log2_segs_p)
{
    HH_ret_t ret= HH_EAGAIN;
    u_int32_t  n_segs_o = (1 << mrsw_p->shared_p->log2_segs); /* >= 1  !!  */
    THH_mrwm_t mrwm = rs_p->mrwm;
    
    /*calc num of segs of new mr*/
    u_int8_t page_shift;
    u_int8_t    log2_mtt_seg_sz = mrwm->props.log2_mtt_seg_sz;
    /* avoid explict u_int64_t division ! */
    u_int32_t   n_segs;
    u_int8_t    log2_segs;
    MT_size_t seg_comp;
    MOSAL_iobuf_props_t iobuf_props;

    FUNC_IN;

    switch (mr_props_p->tpt.tpt_type) {
      case HH_TPT_PAGE: 
        /*calc num of segs of new mr*/
        page_shift = mr_props_p->tpt.tpt.page_lst.page_shift;
        rs_p->n_pages = ((mr_props_p->start+mr_props_p->size - 1)>> page_shift)-(mr_props_p->start>> page_shift)+ 1;
        /* avoid explict u_int64_t division ! */
        if (rs_p->n_pages != mr_props_p->tpt.num_entries) {
          MTL_ERROR1(MT_FLFMT("%s: Given "SIZE_T_DFMT" pages of %uKB is smaller than given region size ("U64_FMT" KB)"),
                     __func__,mr_props_p->tpt.num_entries,(1<<(page_shift-10)),mr_props_p->size>>10);
          return HH_EINVAL;
        }
        rs_p->phys_pages = (VAPI_phy_addr_t*)mr_props_p->tpt.tpt.page_lst.phys_page_lst;
        rs_p->log2_page_size= mr_props_p->tpt.tpt.page_lst.page_shift;
        break; 
      
      case HH_TPT_BUF:  
        if ((mr_props_p->tpt.num_entries == 1) 
              && (mr_props_p->tpt.tpt.buf_lst.phys_buf_lst[0] == mr_props_p->start))
          {
            /* no translation needed */
              rs_p->mpt_entry.pa = TRUE;
              rs_p->n_pages = 1;
              rs_p->seg_start=  0;
              rs_p->log2_page_size= 0;
              ret= HH_OK;
          }else {
              ret = tpt_buf_lst_2_pages(mr_props_p, rs_p);
          }
        if (ret != HH_OK) return ret;
        break;
      
      case HH_TPT_IOBUF:
        rs_p->iobuf= mr_props_p->tpt.tpt.iobuf;
        if (MOSAL_iobuf_get_props(rs_p->iobuf,&iobuf_props) != MT_OK) {
          MTL_ERROR4(MT_FLFMT("Failed MOSAL_iobuf_get_props."));
          return HH_EINVAL;
        }
        rs_p->n_pages= iobuf_props.nr_pages;
        rs_p->phys_pages= NULL;
        rs_p->log2_page_size = iobuf_props.page_shift;
        break;
      
      default:    
        MTL_ERROR2(MT_FLFMT("%s: Invalid tpt type (%d)\n"),__func__,mr_props_p->tpt.tpt_type); 
        return HH_EINVAL;
    }
    seg_comp= rs_p->n_pages >> log2_mtt_seg_sz;
    seg_comp= ((seg_comp << log2_mtt_seg_sz) != rs_p->n_pages) ? seg_comp + 1 : seg_comp;
    /*check that n_segs will not overflow 32 bits */
    log2_segs = ceil_log2(seg_comp);
    if (log2_segs >= (8*sizeof(n_segs)))  return HH_EINVAL_PARAM;
    n_segs = 1 << log2_segs;

    //MTL_DEBUG4(MT_FLFMT("start=0x%Lx, size=0x%Lx, shift=%d, ne=%d, np=%d"),(u_int64_t)start,
      //         (u_int64_t)mr_props_p->size, page_shift,(int)mr_props_p->tpt.num_entries, (int)n_pages);
    
    MTL_DEBUG3(MT_FLFMT("log2 mtt sz=%d \n"),log2_mtt_seg_sz);
    MTL_DEBUG3(MT_FLFMT("n_segs_o=%d     n_segs=%d \n"),n_segs_o,n_segs);
    
    if ( (n_segs != n_segs_o) || (mrsw_p->shared_p->ref_count > 1) || (rs_p->mpt_entry.pa)) {
      /* replace MTTs (or just free if new "translation" is physical with no translation) */
        u_int32_t   seg_start = EXTBUDDY_NULL;
        /* we are not using spinlock on the ref_cnt on the above check to simplify code*/
        /* if we are "lucky" enough we may get here even we can keep use the same MTTs */
        /* (when 2 sharing regions did the change_translation simultaneously)          */
        /* but... this is not a bug. Just an overkill (reallocating MTTs)              */
        /* ("shared memory" regions are rarely reregistered)                           */
        release_shared_mtts(mrwm,mrsw_p);

        MTL_DEBUG1(MT_FLFMT("alloc new MTT's \n"));
        /* 2. alloc new MTT's  */ 
        if (!rs_p->mpt_entry.pa) { /* If translation needed */
          if (reserve_mtt_segs(mrwm, n_segs-1) != HH_OK) { /* surplus segments reservation */
            MTL_ERROR1(MT_FLFMT("Out of MTT segments"));
            return HH_EAGAIN;
          }
          if (MOSAL_mutex_acq(&mrwm->extbuddy_lock,TRUE) != MT_OK) {
            release_mtt_segs(mrwm,n_segs-1);
            return HH_EINTR;
          }
          seg_start = extbuddy_alloc(mrwm->xbuddy_tpt, log2_segs); 
          if (seg_start != EXTBUDDY_NULL) {EXTBUDDY_ALLOC_MTT(seg_start,log2_segs);}
          MOSAL_mutex_rel(&mrwm->extbuddy_lock);

          if (seg_start != EXTBUDDY_NULL) {
              rs_p->seg_start = seg_start;
              rs_p->mpt_entry.mtt_seg_adr = mrwm->props.mtt_base |
                                      (seg_start << (log2_mtt_seg_sz+ MTT_LOG_MTT_ENTRY_SIZE));;
          }else{ /* reregister (translation) failed */
            MTL_ERROR1(MT_FLFMT("Failed allocating MTT segments (unexpected error !!)"));
            release_mtt_segs(mrwm,n_segs-1);
            return HH_EAGAIN;
          }
        }
    }else{  /* else, using the same MTT's of the original region */
        rs_p->seg_start= mrsw_p->shared_p->seg_start;
        rs_p->log2_page_size = mrsw_p->shared_p->page_shift;
    }
    
    *log2_segs_p= log2_segs;
    return HH_OK;
}


/************************************************************************/
static void  determine_ctx(
  THH_mrwm_t               mrwm,
  THH_internal_mr_t*       mr_props,
  MOSAL_protection_ctx_t*  ctx_p
)
{
  HH_ret_t  rc = THH_uldm_get_protection_ctx(mrwm->uldm, mr_props->pd, ctx_p);
  if (rc != HH_OK)
  {
#ifndef __DARWIN__
    MTL_DEBUG4(MT_FLFMT("THH_uldm_get_protection_ctx failed, use ctx=0x%x"),
               mr_props->vm_ctx);
#else
    MTL_DEBUG4(MT_FLFMT("THH_uldm_get_protection_ctx failed"));
#endif
    *ctx_p = mr_props->vm_ctx;
  }
} /* determine_ctx */



/************************************************************************
 *  Copies and check props into mrwm handle.
 *  Here are the restrictions:
 *     #-regions + #-windows <= #MPT-entries.
 *     segment_size = 2^n >= 2^3 = 8
 *     #-regions <= #-segments <= 1M = 2^20
 */
static MT_bool  check_props(const THH_mrwm_props_t* props, THH_mrwm_t mrwm)
{
  MT_bool    ok = FALSE;
  u_int8_t   log2_mtt_sz = props->log2_mtt_sz;
  u_int32_t n_log_reserved_mtt_segs= props->log2_rsvd_mtt_segs + props->log2_mtt_seg_sz;
  u_int32_t      tavor_num_reserved_mtts = (u_int32_t) (1ul << props->log2_rsvd_mtt_segs);
  u_int32_t      tavor_num_reserved_mpts = (u_int32_t) (1ul << props->log2_rsvd_mpts);
  u_int32_t  n_req_mpts = (u_int32_t)(tavor_num_reserved_mpts + props->max_mem_reg_internal + 
                        props->max_mem_reg + props->max_mem_win);
  
  MTL_DEBUG4(MT_FLFMT("base="U64_FMT", log2_mpt_sz=%d, log2_mtt_sz=%d, n_req_mpts=0x%x"),
                      props->mtt_base, props->log2_mpt_sz, log2_mtt_sz, n_req_mpts);
  if ((n_req_mpts <= (1ul << props->log2_mpt_sz)) &&
      (log2_mtt_sz > n_log_reserved_mtt_segs) &&
      (log2_mtt_sz >= LOG2_MIN_SEG_SIZE)) /* funny check, but... */
  {
    u_int32_t  n_segs;
    u_int32_t n_rgns= (u_int32_t)(props->max_mem_reg_internal + props->max_mem_reg);
    u_int8_t   log2_n_segs = props->log2_mtt_sz - props->log2_mtt_seg_sz;
    mrwm->props = *props; /* But we may fix some values */
    MTL_DEBUG4(MT_FLFMT("log2_n_segs=%d, max=%d"), 
                        log2_n_segs, props->log2_max_mtt_segs);
    if (log2_n_segs >  props->log2_max_mtt_segs)
    {
      /* Waste of MTT memory, but we cannot use more than 1M */
      mrwm->props.log2_mtt_sz = props->log2_mtt_seg_sz + 
                                props->log2_max_mtt_segs;
      log2_n_segs = props->log2_max_mtt_segs;
      MTL_DEBUG4(MT_FLFMT("Enlarge: log2_n_segs=%d"), log2_n_segs);
    }
    n_segs = (1ul << log2_n_segs);
    ok = (n_rgns <= n_segs);
    if (!ok)
    {
      MTL_ERROR1(MT_FLFMT("n_rgns=0x%x > n_segs=0x%x"), n_rgns, n_segs);
      return ok;
    }
    mrwm->surplus_segs = (n_segs - tavor_num_reserved_mtts) - n_rgns;
  }
  MTL_DEBUG4(MT_FLFMT("ok=%d"), ok);
  return ok;
} /* check_props */


/************************************************************************/
static void  mpt_entry_init(THH_mpt_entry_t* e)
{
  memset(e, 0, sizeof(THH_mpt_entry_t));
  e->ver            = 0;
  /* e->ce             = 1; */
  e->lr             = TRUE;
  e->pw             = 0;
  e->m_io           = TRUE;
  /* e->vl             = 0; */
  /* e->owner          = 0;  */
  e->status         = 0;
  e->win_cnt        = 0;
  e->win_cnt_limit  = 0; /* 0 means no limit */
} /* mpt_entry_init */


/************************************************************************/
static void  props2mpt_entry(const HH_mr_t* props, THH_mpt_entry_t* e)
{
  VAPI_mrw_acl_t  acl = props->acl;

  mpt_entry_init(e);
  e->lw             = (acl & VAPI_EN_LOCAL_WRITE ? TRUE : FALSE);
  e->rr             = (acl & VAPI_EN_REMOTE_READ ? TRUE : FALSE);
  e->rw             = (acl & VAPI_EN_REMOTE_WRITE ? TRUE : FALSE);
  e->a              = (acl & VAPI_EN_REMOTE_ATOM ? TRUE : FALSE);
  e->eb             = (acl & VAPI_EN_MEMREG_BIND ? TRUE : FALSE);
  e->pd             = props->pd;
  e->start_address  = props->start;
  e->reg_wnd_len    = props->size;
  e->pa = FALSE;
} /* props2mpt_entry */

/************************************************************************/
static HH_ret_t  smr_props2mpt_entry(THH_mrwm_t   mrwm,const HH_smr_t* props, THH_mpt_entry_t* e)
{
  VAPI_mrw_acl_t  acl = props->acl;
  HH_mr_info_t      orig_mr;  
  HH_ret_t ret;

  mpt_entry_init(e);
  e->lw             = (acl & VAPI_EN_LOCAL_WRITE ? TRUE : FALSE);
  e->rr             = (acl & VAPI_EN_REMOTE_READ ? TRUE : FALSE);
  e->rw             = (acl & VAPI_EN_REMOTE_WRITE ? TRUE : FALSE);
  e->a              = (acl & VAPI_EN_REMOTE_ATOM ? TRUE : FALSE);
  e->eb             = (acl & VAPI_EN_MEMREG_BIND ? TRUE : FALSE);
  e->pd             = props->pd;
  e->start_address  = props->start;
  
  /* size is not given, so we must query it according to lkey of the original mr*/
  ret = THH_mrwm_query_mr(mrwm,props->lkey,&orig_mr);
  if (ret != HH_OK) {
      MTL_ERROR1("failed quering the original mr \n");
      return ret;
  }

  MTL_DEBUG1("end SMR props2mpt \n");
  /*TBD: what about remote ?? */
  e->reg_wnd_len    = orig_mr.local_size;
  return HH_OK;
} /* props2mpt_entry */

static void init_fmr_mpt_entry(THH_mpt_entry_t* mpt_entry_p, 
                               HH_pd_hndl_t pd, 
                               VAPI_mrw_acl_t acl,
                               u_int32_t init_memkey,
                               u_int8_t log2_page_sz,
                               u_int64_t mtt_seg_adr)
{
  memset(mpt_entry_p, 0, sizeof(THH_mpt_entry_t));
  mpt_entry_p->pd          = pd;
  mpt_entry_p->lr          = TRUE;
  mpt_entry_p->m_io        = TRUE;
  mpt_entry_p->r_w         = TRUE;    /* Region */
  mpt_entry_p->pa          = FALSE;
  mpt_entry_p->page_size   = log2_page_sz - TAVOR_LOG_MPT_PG_SZ_SHIFT;
  mpt_entry_p->mem_key     = init_memkey; /* Initial (invalid) key */
  mpt_entry_p->mtt_seg_adr = mtt_seg_adr;
  mpt_entry_p->lw          = (acl & VAPI_EN_LOCAL_WRITE ? TRUE : FALSE);
  mpt_entry_p->rr          = (acl & VAPI_EN_REMOTE_READ ? TRUE : FALSE);
  mpt_entry_p->rw          = (acl & VAPI_EN_REMOTE_WRITE ? TRUE : FALSE);
  mpt_entry_p->a           = (acl & VAPI_EN_REMOTE_ATOM ? TRUE : FALSE);
  mpt_entry_p->eb          = FALSE;  /* No memory bind allowed over FMRs */
  mpt_entry_p->reg_wnd_len= 0;       /* prevent access via this MPT */
}

/************************************************************************/
static void  internal_props2mpt_entry(
  const THH_internal_mr_t* props,
  THH_mpt_entry_t*         e
)
{
  mpt_entry_init(e);
  e->lw             = TRUE;
  e->rr             = FALSE;
  e->rw             = FALSE;
  e->a              = FALSE;
  e->eb             = FALSE;
  e->pd             = props->pd;
  e->start_address  = props->start;
  e->reg_wnd_len    = props->size;
} /* internal_props2mpt_entry */


/************************************************************************/
inline static u_int32_t  make_key(THH_mrwm_t  mrwm,  mpt_segment_t mpt_seg, u_int32_t  mpt_index)
{
  if ((mpt_index < mrwm->offset[mpt_seg]) || 
      (mpt_index >= mrwm->offset[mpt_seg]+mrwm->max_mpt[mpt_seg])) {
    MTL_ERROR4(MT_FLFMT("%s: Given MPT index (0x%X) is not in given mpt segment"),__func__,
               mpt_index);
    return 0;
  }
  MOSAL_spinlock_irq_lock(&mrwm->key_prefix_lock);  /* TBD: change to atomic_inc */
  mrwm->key_prefix[mpt_seg][mpt_index-mrwm->offset[mpt_seg]]++;
  MOSAL_spinlock_unlock(&mrwm->key_prefix_lock);
  return CURRENT_MEMKEY(mrwm,mpt_seg,mpt_index);
} /* make_key */


/************************************************************************/
/*  Go thru the two synced arrays of buffers addresses and sizes.
 *  For each get the alignment - that is lowest bit.
 *  Return the total_size and the minimal lowest bit.
 *  The latter is minimized with initial page_shift provided value.
 *  Note that this lowest bit can be used to test native page alignment.
 */
static u_int8_t  buf_lst_2_page_shift
(
  const VAPI_phy_addr_t*  phys_buf_lst,
  const VAPI_size_t*  buf_sz_lst,
  MT_size_t           n,
  u_int8_t            page_shift,
  VAPI_size_t*        total_p
)
{
  MT_size_t          bi;
  VAPI_phy_addr_t    bs[2], *bs_begin = &bs[0], *bs_end = bs_begin + 2, *pbs;
  VAPI_size_t  total_size = 0;

  MTL_DEBUG4(MT_FLFMT("n="SIZE_T_FMT", page_shift=%d"), n, page_shift);
  /*  Find gcd of address+size that is a power of 2. 
   *  Actually minimizing lowest bit on.
   */
  for (bi = n;  bi--;  )
  { /* 'arraying' the address + size values, to allow for loop */
    bs[0] = phys_buf_lst[bi];  /* must be native page aligned */
    bs[1] = buf_sz_lst[bi];
    MTL_DEBUG4(MT_FLFMT("buf="U64_FMT", sz="U64_FMT), bs[0], bs[1]);
    total_size += buf_sz_lst[bi];
    for (pbs = bs_begin;  pbs != bs_end;  ++pbs)
    {
      VAPI_phy_addr_t  u32 = *pbs;
      if (u32) 
      {
        u_int8_t  l = lowest_bit(u32);
        MTL_DEBUG4(MT_FLFMT("lowest_bit("U64_FMT")=%d"), u32, l);
        if (l < page_shift)
        {
          page_shift = l;
        }
      }
    }
  }
  *total_p = total_size;
  MTL_DEBUG4(MT_FLFMT("page_shift=%d"), page_shift);
  return page_shift;
} /* buf_lst_2_page_shift */


/************************************************************************/
static HH_ret_t  buf_lst_2_pages
(
  const VAPI_phy_addr_t*  phys_buf_lst,
  const VAPI_size_t*  buf_sz_lst,
  MT_size_t           n,
  IB_virt_addr_t      start,
  VAPI_phy_addr_t     iova_offset,
  Reg_Segs_t*         reg_segs
)
{
  VAPI_size_t  total_size;
  VAPI_phy_addr_t   initial_pages_skip;
  VAPI_phy_addr_t*  currp;
  VAPI_size_t       page_size;
  MT_size_t  bi;
  IB_virt_addr_t    start_unoffset = start - iova_offset;
  u_int8_t          page_shift = lowest_bit(start_unoffset);
  VAPI_size_t tmp;
  
  MTL_DEBUG4(MT_FLFMT("start="U64_FMT", offset="U64_FMT", unoffset="U64_FMT", shift=%d"),
                      start, iova_offset, start_unoffset, page_shift);
  
  /* calc page shift */
  page_shift = buf_lst_2_page_shift(phys_buf_lst, buf_sz_lst, n, page_shift, 
                                    &total_size);
  
  if (page_shift > TAVOR_IF_MAX_MPT_PAGE_SIZE) {
      MTL_ERROR1("page shift calculated :%d , due to MPT restrictions page shift will be 31 \n",page_shift);  
      page_shift = TAVOR_IF_MAX_MPT_PAGE_SIZE;
  }
  MTL_DEBUG4(MT_FLFMT("n="SIZE_T_DFMT", page_shift=%d"), n, page_shift);
  if (page_shift < native_page_shift)
  {
    MTL_ERROR1(MT_FLFMT("page shift below system page size"));
    return HH_EINVAL;
  }
  
    initial_pages_skip = /* page_size * (iova_offset/page_size) */
                    iova_offset & ~(((VAPI_phy_addr_t)1 << page_shift) - 1);
    MTL_DEBUG4(MT_FLFMT("total_size="U64_FMT", initial_pages_skip="U64_FMT),total_size, initial_pages_skip);
    
    total_size -= initial_pages_skip;
    reg_segs->n_pages = total_size >> page_shift;
    reg_segs->log2_page_size = page_shift;
    MTL_DEBUG4(MT_FLFMT("total_size="U64_FMT", page_shift=%d, n_pages="SIZE_T_DFMT),
               total_size, page_shift, reg_segs->n_pages);
    tmp= sizeof(VAPI_phy_addr_t)*reg_segs->n_pages;
    if (tmp > ((MT_phys_addr_t) MAKE_ULONGLONG(0xFFFFFFFFFFFFFFFF))) {
        MTL_ERROR1(MT_FLFMT("total bufs size exceeds max size available on this machine"));
        return HH_EINVAL;
    }
    ALLOC_PHYS_PAGES_ARRAY(reg_segs);
    if (reg_segs->phys_pages == NULL)
    {
      MTL_ERROR1(MT_FLFMT("alloc of "SIZE_T_DFMT" phys_pages failed"),reg_segs->n_pages);
      return HH_EAGAIN;
    }
          
    page_size = (VAPI_size_t)1<< page_shift;
    currp = reg_segs->phys_pages;
    MTL_DEBUG4(MT_FLFMT("page_size="U64_FMT", phys_buf_lst=%p, currp=%p"),page_size, phys_buf_lst, currp);
    for (bi = 0;  bi != n;  ++bi)
      {
        VAPI_phy_addr_t  buf_page = phys_buf_lst[bi] + initial_pages_skip;
        VAPI_phy_addr_t  buf_page_end = buf_page + buf_sz_lst[bi];
        initial_pages_skip = 0; /* skip only in 1st buffer */
        MTL_DEBUG4(MT_FLFMT("bi="SIZE_T_FMT", currp=%p, bp="U64_FMT", bp_end="U64_FMT), 
                     bi, currp, buf_page, buf_page_end);
        for ( ;  buf_page != buf_page_end;  buf_page += page_size, ++currp)
        {
          MTL_DEBUG4(MT_FLFMT("currp=%p, b="U64_FMT), currp, buf_page);
          *currp = buf_page;
        }
      }
      
    
    return HH_OK;
} /* buf_lst_2_pages */




/************************************************************************/
/* For the sake of MicroCode(?) efficiency,
 * we ensure writing an even number of MTTs.
 */
static  HH_ret_t  mtt_writes(
  THH_cmd_t         cmd_if,
  VAPI_phy_addr_t*  phys_page_lst,
  VAPI_phy_addr_t   mtt_pa,
  VAPI_size_t       n_pages
)
{
  static const MT_size_t MTT_WRITE_MAX_SIZE = 
                              MTT_WRITE_MAX * (1ul << MTT_LOG_MTT_ENTRY_SIZE);
  THH_mtt_entry_t          *e0,*e_end;
  THH_cmd_status_t          cmd_rc = THH_CMD_STAT_OK;
  VAPI_size_t               n_entries = MTT_WRITE_MAX, n_w_entries;

  MTL_DEBUG4(MT_FLFMT("mtt_writes: mtt_pa="U64_FMT", n="U64_FMT), mtt_pa, n_pages);
  e0 = (THH_mtt_entry_t*)MALLOC((MTT_WRITE_MAX + 1) * sizeof(THH_mtt_entry_t));
  if (!e0) {
    MTL_ERROR1(MT_FLFMT("kmalloc of "SIZE_T_FMT" bytes failed"),
               (MTT_WRITE_MAX + 1) * sizeof(THH_mtt_entry_t));
    return HH_EAGAIN;
  }

  e_end = e0 + MTT_WRITE_MAX;


  while (n_pages)
  {
    THH_mtt_entry_t*  e = e0;
    if (n_pages < MTT_WRITE_MAX)
    {
      n_entries = n_pages;
      e_end = e0 + n_pages;
    }
    for (;  e != e_end;  ++e)
    {
      e->ptag = *phys_page_lst++;
      e->p    = TRUE;
      /* MTL_DEBUG4(MT_FLFMT("e=0x%p, e->ptag=0x%Lx"), e, e->ptag); */
    }
    /* dummy extra, to ensure even number of MTTs */
    e->ptag = 0;
    e->p    = FALSE;
    n_w_entries = (n_entries + 1) & ~1ul;  /* even upper bound */
    MTL_DEBUG4(MT_FLFMT("mtt_pa="U64_FMT", ne="U64_FMT), mtt_pa, n_w_entries);
    cmd_rc = THH_cmd_WRITE_MTT(cmd_if, mtt_pa, n_w_entries, e0);
    n_pages -= n_entries;
    MTL_DEBUG4(MT_FLFMT("cmd_rc=%d, n_pages="U64_FMT), cmd_rc, n_pages);
    if (cmd_rc != THH_CMD_STAT_OK)
    {
      n_pages = 0;
    }
    else if (n_pages) /* may save 64-bit addition */
    {
      mtt_pa += MTT_WRITE_MAX_SIZE;
    }
  } DHERE;
  if (cmd_rc != THH_CMD_STAT_OK) {
    MTL_ERROR1(MT_FLFMT("mtt writes failed got %d \n"),cmd_rc);
  }
  FREE(e0);
  return (CMDRC2HH_ND(cmd_rc));
} /* mtt_writes */

static  HH_ret_t  mtt_writes_iobuf(
  THH_cmd_t         cmd_if,
  MOSAL_iobuf_t     iobuf,
  VAPI_phy_addr_t   mtt_pa,
  VAPI_size_t       n_pages
)
{
  static const MT_size_t MTT_WRITE_MAX_SIZE = 
                              MTT_WRITE_MAX * (1ul << MTT_LOG_MTT_ENTRY_SIZE);
  THH_mtt_entry_t          *e0,*e;
  THH_cmd_status_t          cmd_rc = THH_CMD_STAT_OK;
  MT_size_t                 n_entries = MTT_WRITE_MAX, n_w_entries;
  MT_size_t n_entries_out, cur_entry;
  MOSAL_iobuf_iter_t iobuf_iter;
  MT_phys_addr_t  *mt_pages_p;
  call_result_t mt_rc;
  HH_ret_t rc= HH_EAGAIN;

  MTL_DEBUG4(MT_FLFMT("mtt_writes_iobuf: mtt_pa="U64_FMT", n="U64_FMT", iobuf=0x%p, n_pages="U64_FMT),
             mtt_pa, n_pages,iobuf,n_pages);

  e0 = (THH_mtt_entry_t*)MALLOC((MTT_WRITE_MAX + 1) * sizeof(THH_mtt_entry_t));
  if (!e0) {
    MTL_ERROR2(MT_FLFMT("kmalloc of "SIZE_T_FMT" bytes failed"),
               (MTT_WRITE_MAX + 1) * sizeof(THH_mtt_entry_t));
    return HH_EAGAIN;
  }
  mt_pages_p= (MT_phys_addr_t*)MALLOC(MTT_WRITE_MAX * sizeof(MT_phys_addr_t)); /* for MOSAL_iobuf_get_tpt_seg */
  if (!mt_pages_p) {
    MTL_ERROR2(MT_FLFMT("kmalloc of "SIZE_T_FMT" bytes failed"),
               MTT_WRITE_MAX * sizeof(MT_phys_addr_t));
    goto fail_mt_pages;
  }
  (void)MOSAL_iobuf_iter_init(iobuf,&iobuf_iter);

  while (n_pages)
  {
    if (n_pages < MTT_WRITE_MAX)  {
      n_entries = n_pages;
    }

    MTL_DEBUG5(MT_FLFMT("%s: n_pages="U64_FMT"  n_entries="SIZE_T_FMT"  mtt_pa="U64_FMT),
               __func__, n_pages, n_entries, mtt_pa); 

    /* get next segment of the page table */
    mt_rc= MOSAL_iobuf_get_tpt_seg(iobuf, &iobuf_iter, n_entries, &n_entries_out, mt_pages_p);
    if (mt_rc != MT_OK) {
      MTL_ERROR2(MT_FLFMT("Failed MOSAL_iobuf_get_tpt_seg (%s)"),mtl_strerror_sym(mt_rc));
      rc= HH_EFATAL;
      goto fail_get_tpt;
    }
    if (n_entries_out != n_entries) { /* sanity check */
      MTL_ERROR2(MT_FLFMT(
        "Number of pages returned from MOSAL_iobuf_get_tpt_seg ("SIZE_T_DFMT
        ") is different from expected ("SIZE_T_DFMT")"), n_entries_out, n_entries);
      rc= HH_EFATAL;
      goto fail_get_tpt;
    }
    for (e= e0, cur_entry= 0;  cur_entry < n_entries;  ++e, ++cur_entry)  {
      e->ptag = mt_pages_p[cur_entry];
      e->p    = TRUE;
      /* MTL_DEBUG4(MT_FLFMT("e=0x%p, e->ptag=0x%Lx"), e, e->ptag); */
    }
    /* dummy extra, to ensure even number of MTTs */
    e->ptag = 0;
    e->p    = FALSE;
    n_w_entries = (n_entries + 1) & ~1ul;  /* even upper bound */
    cmd_rc = THH_cmd_WRITE_MTT(cmd_if, mtt_pa, n_w_entries, e0);
    if (cmd_rc != THH_CMD_STAT_OK)  {
      MTL_ERROR1(MT_FLFMT("THH_cmd_WRITE_MTT failed (err=%d)"),cmd_rc);
      rc= HH_EFATAL;
      goto fail_cmd;
    }
    n_pages -= n_entries;
    mtt_pa += MTT_WRITE_MAX_SIZE;
  }
  FREE(mt_pages_p);
  FREE(e0);
  MTL_DEBUG5(MT_FLFMT("mtt_writes_iobuf - DONE"));
  return HH_OK;

  fail_cmd:
  fail_get_tpt:
    FREE(mt_pages_p);
  fail_mt_pages:
    FREE(e0);
    return rc;
} /* mtt_writes */



/************************************************************************/
static HH_ret_t  register_pages(Reg_Segs_t*  rs_p,mpt_segment_t  mpt_seg,VAPI_mrw_type_t mr_type)
{
  HH_ret_t          rc = HH_EAGAIN;
  THH_mrwm_t        mrwm = rs_p->mrwm;
  THH_mpt_entry_t*  mpt_entry_p = &rs_p->mpt_entry;
  THH_cmd_status_t  cmd_rc;
  u_int8_t          log2_seg_sz = mrwm->props.log2_mtt_seg_sz;
  VAPI_phy_addr_t         mtt_seg_adr;

  MTL_DEBUG4(MT_FLFMT("register_pages: seg_start=0x%x, log2_seg_sz=%u iobuf=0x%p n_pages="SIZE_T_DFMT), 
                      rs_p->seg_start, log2_seg_sz, rs_p->iobuf, rs_p->n_pages);

  mtt_seg_adr = mrwm->props.mtt_base |
                (rs_p->seg_start << (log2_seg_sz + MTT_LOG_MTT_ENTRY_SIZE));
  rs_p->key = make_key(mrwm, mpt_seg,rs_p->mpt_index);

  mpt_entry_p->r_w         = TRUE;
  mpt_entry_p->page_size   = rs_p->log2_page_size - TAVOR_LOG_MPT_PG_SZ_SHIFT;
  mpt_entry_p->mem_key     = rs_p->key;
  mpt_entry_p->mtt_seg_adr = mtt_seg_adr;

  if ( (!mpt_entry_p->pa) &&
        ((mr_type == VAPI_MR) || (mr_type == VAPI_MPR)) )
  {
    if (rs_p->iobuf != NULL) { /* use MOSAL_iobuf to write MTT entries */
      rc= mtt_writes_iobuf(mrwm->cmd_if, rs_p->iobuf, mtt_seg_adr, rs_p->n_pages);
    } else { /* page tables is given in rs_p->phys_pages */
      rc= mtt_writes(mrwm->cmd_if, rs_p->phys_pages, mtt_seg_adr, rs_p->n_pages);
    }
  }
  else rc = HH_OK; /* no need to write mtt with SHARED (or no-translation) mr */
  
  if (rc == HH_OK)
  {
    cmd_rc = THH_cmd_SW2HW_MPT(mrwm->cmd_if, rs_p->mpt_index, &rs_p->mpt_entry);
    MTL_DEBUG4(MT_FLFMT("SW2HW_MPT: cmd_rc=%d"), cmd_rc);
    if (cmd_rc != THH_CMD_STAT_OK) {
            MTL_ERROR1(MT_FLFMT("register pages failed got %d \n"),cmd_rc);
            rc = (CMDRC2HH_ND(cmd_rc));
    }
  }
  return rc;
} /* register_pages */


/************************************************************************/
static HH_ret_t  save_sw_context(const Reg_Segs_t* rs_p, u_int8_t log2_segs, VAPI_mrw_type_t mr_type,
                                 Mr_sw_t* mrsw)
{
  mrsw->key        = rs_p->key;
  mrsw->start      = rs_p->mpt_entry.start_address;
  mrsw->pd         = rs_p->mpt_entry.pd;
  mrsw->acl        = rs_p->acl;
  if (mr_type != VAPI_MSHAR) {
      mrsw->shared_p = (Shared_data_t*)MALLOC(sizeof(Shared_data_t));
      if (mrsw->shared_p == NULL) {
          MTL_ERROR1(MT_FLFMT("save_sw_ctxt: failed allocating memory \n"));
          return HH_EAGAIN; 
      }
      mrsw->shared_p->size       = rs_p->mpt_entry.reg_wnd_len;
      mrsw->shared_p->seg_start  = rs_p->seg_start;
      mrsw->shared_p->log2_segs  = log2_segs;
      mrsw->shared_p->page_shift = rs_p->log2_page_size;
      mrsw->shared_p->ref_count = 1;
      MOSAL_spinlock_init(&mrsw->shared_p->ref_lock);
  }else {
    MOSAL_spinlock_lock(&mrsw->shared_p->ref_lock);
    mrsw->shared_p->ref_count ++;
    MOSAL_spinlock_unlock(&mrsw->shared_p->ref_lock);
  }
  mrsw->iobuf = rs_p->iobuf;
  return HH_OK;
} /* save_sw_context */


static HH_ret_t init_fmr_context(THH_mrwm_t     mrwm, 
                                 FMR_sw_info_t*    fmr_info_p,
                                 u_int32_t      mpt_index, 
                                 u_int32_t      seg_start, 
                                 u_int8_t       log2_segs,
                                 u_int8_t       log2_page_sz)
{
  MT_phys_addr_t mtt_seg_adr= 
    mrwm->props.mtt_base | (seg_start << (mrwm->props.log2_mtt_seg_sz + MTT_LOG_MTT_ENTRY_SIZE));
  MT_phys_addr_t mpt_entry_adr= mrwm->props.mpt_base | (mpt_index << TAVOR_IF_STRIDE_MPT_BIT);

  fmr_info_p->mtt_entries= MOSAL_io_remap(mtt_seg_adr,1 << (log2_segs + mrwm->props.log2_mtt_seg_sz + MTT_LOG_MTT_ENTRY_SIZE));
  if (fmr_info_p->mtt_entries == 0) {
    MTL_ERROR2(MT_FLFMT("%s: MOSAL_io_remap("PHYS_ADDR_FMT", 0x%x) failed"),
                __func__, mtt_seg_adr,
                1 << (log2_segs + mrwm->props.log2_mtt_seg_sz + MTT_LOG_MTT_ENTRY_SIZE));
    return HH_EAGAIN;
  }
  
  fmr_info_p->mpt_entry= MOSAL_io_remap(mpt_entry_adr, TAVOR_IF_STRIDE_MPT);
  if (fmr_info_p->mpt_entry == 0) {
    MTL_ERROR2(MT_FLFMT("%s: MOSAL_io_remap("PHYS_ADDR_FMT", 0x%x) failed"), __func__, 
               mpt_entry_adr, TAVOR_IF_STRIDE_MPT);
    MOSAL_io_unmap((MT_virt_addr_t)fmr_info_p->mtt_entries);
    return HH_EAGAIN;
  }
  
  fmr_info_p->last_free_key= CURRENT_MEMKEY(mrwm,MPT_ext,mpt_index);
  fmr_info_p->seg_start= seg_start;
  fmr_info_p->log2_segs= log2_segs;
  fmr_info_p->log2_page_sz= log2_page_sz;

  return HH_OK;
}


/************************************************************************/
static HH_ret_t  alloc_reg_pages(
  Reg_Segs_t*  rs_p,
  mpt_segment_t  tpt_group,
  VAPI_lkey_t* forced_key
)
{
  HH_ret_t    rc = HH_EAGAIN;
  VIP_common_ret_t   vip_array_rc=VIP_EAGAIN;
  Mr_sw_t* mrsw_p= TMALLOC(Mr_sw_t);
  VIP_array_handle_t mpt_index= 0xFFFFFFFF;
  THH_mrwm_t  mrwm = rs_p->mrwm;
  u_int32_t   seg_start = EXTBUDDY_NULL;
  u_int8_t    log2_mtt_seg_sz = mrwm->props.log2_mtt_seg_sz;
              /* avoid explict u_int64_t division ! */
  u_int32_t   n_segs;
  MT_size_t   seg_comp;
  u_int8_t log2_segs;
  
  seg_comp= rs_p->n_pages >> log2_mtt_seg_sz;
  seg_comp= ((seg_comp << log2_mtt_seg_sz) != rs_p->n_pages) ? seg_comp + 1 : seg_comp;
  /*check that n_segs will not overflow 32 bits */
  log2_segs = ceil_log2(seg_comp);
  if (log2_segs >= (8*sizeof(n_segs)))  return HH_EINVAL_PARAM;
  n_segs = 1 << log2_segs;

  MTL_DEBUG4(MT_FLFMT("%s: n_pages="SIZE_T_DFMT" n_segs=%u"), __func__,
             rs_p->n_pages, n_segs); 
  
  if (!mrsw_p) {
    MTL_ERROR3(MT_FLFMT("%s: Failed allocating MR_sw_t for new memory region"),__func__);
    return HH_EAGAIN;
  }
  memset(mrsw_p,0,sizeof(Mr_sw_t));
  MTL_DEBUG4(MT_FLFMT("log2_mtt_seg_sz=%d"), log2_mtt_seg_sz);
  MTL_DEBUG4(MT_FLFMT("alloc_reg_pages: #pg="SIZE_T_FMT", #segs=0x%x, surp=0x%x, g=%d"),
                      rs_p->n_pages, n_segs, mrwm->surplus_segs, tpt_group);

  if (reserve_mpt_entry(mrwm,tpt_group) != HH_OK) {
    MTL_ERROR4(MT_FLFMT("%s: Out of MPT entries"),__func__);
    goto failed_mpt_reserve;
  }
  
  if (!rs_p->mpt_entry.pa) {
    if ((rc= reserve_mtt_segs(mrwm,n_segs-1)) != HH_OK) {
        MTL_ERROR4(MT_FLFMT("%s: Out of MTT entries"),__func__);
        goto failed_mtt_reserve;
    }
  }
  
  if (forced_key) /* must be Internal! */
  {
    mpt_index= (*forced_key & MASK32(mrwm->props.log2_mpt_sz));
    if ((mpt_index >= mrwm->offset[MPT_int]) && 
        (mpt_index < mrwm->offset[MPT_ext])) {
      vip_array_rc= 
        VIP_array_insert2hndl(mrwm->mpt[tpt_group],mrsw_p,mpt_index-mrwm->offset[MPT_int]);
    } else {
      vip_array_rc= VIP_EINVAL_PARAM; /* given key is not available */
    }
  }
  else /* !forced_key */
  {
    vip_array_rc= VIP_array_insert(mrwm->mpt[tpt_group],mrsw_p,&mpt_index);
    mpt_index+= mrwm->offset[tpt_group];
  }
  
  if (vip_array_rc != VIP_OK)  {
    MTL_ERROR3(MT_FLFMT("%s: Failed MPT entry allocation (%s)"),__func__,
               VAPI_strerror_sym(vip_array_rc));
    goto failed_vip_array;
  }
    
  
  if (!rs_p->mpt_entry.pa) {
    if (MOSAL_mutex_acq(&mrwm->extbuddy_lock, TRUE) != MT_OK)  {
      rc= VIP_EINTR;
      goto failed_mutex;
    }
    seg_start = extbuddy_alloc(mrwm->xbuddy_tpt, log2_segs);
    if (seg_start != EXTBUDDY_NULL) {EXTBUDDY_ALLOC_MTT(seg_start,log2_segs);}
    MOSAL_mutex_rel(&mrwm->extbuddy_lock);
    if (seg_start == EXTBUDDY_NULL) {
      MTL_ERROR3(MT_FLFMT("%s: Failed allocation of %d MTT segment/s allocation"),__func__,
                 log2_segs);
      rc= HH_EAGAIN;
      goto failed_extbuddy;
    }
    rs_p->seg_start = seg_start;
  }
  
    
  rs_p->mpt_index = mpt_index;
  /*in PMR, it acts the same, as long as it's not SHARED */
  rc = register_pages(rs_p,tpt_group,VAPI_MR); 
  if (rc != HH_OK) {
    goto failed_register_pages;
  }
    
  /*saving the new MPT entry */
  /*in PMR, it acts the same, as long as it's not SHARED */
  rc = save_sw_context(rs_p, log2_segs,VAPI_MR,mrsw_p); 
  if (rc != HH_OK) {
    MTL_ERROR1("failed save_sw_ctxt \n");
    goto failed_save_ctx;
  }
    
  return HH_OK;

  failed_save_ctx:
    THH_cmd_HW2SW_MPT(mrwm->cmd_if, mpt_index, NULL); /* reclaim MPT entry from HW */
  failed_register_pages:
    MOSAL_mutex_acq_ui(&mrwm->extbuddy_lock);
    extbuddy_free(mrwm->xbuddy_tpt, seg_start, log2_segs);
    EXTBUDDY_FREE_MTT(seg_start, log2_segs);
    MOSAL_mutex_rel(&mrwm->extbuddy_lock);
  failed_extbuddy:
  failed_mutex:
    VIP_array_erase(mrwm->mpt[tpt_group],mpt_index-mrwm->offset[tpt_group],NULL);
  failed_vip_array:
      if (!rs_p->mpt_entry.pa) {
        release_mtt_segs(mrwm,n_segs-1);
      }
  failed_mtt_reserve:
    release_mpt_entry(mrwm,tpt_group);
  failed_mpt_reserve:
    FREE(mrsw_p);
    return rc;
} /* alloc_reg_pages */


/************************************************************************/
static void  swinfo2mrinfo(const Mr_sw_t* swmr, HH_mr_info_t* hmr)
{
  hmr->lkey         = swmr->key;
  hmr->rkey         = swmr->key;
  hmr->local_start  = swmr->start;
  hmr->remote_start = swmr->start;
  hmr->local_size   = swmr->shared_p->size;
  hmr->remote_size  = swmr->shared_p->size;
  hmr->pd           = swmr->pd;
  hmr->acl          = swmr->acl;
} /* swinfo2mrinfo */



/************************************************************************/
/* Handling  THH_mrwm_register_internal(...) case 
 * where physical buffers are given
 */
static HH_ret_t  bufs_register_internal(
  THH_internal_mr_t*  mr_p,
  Reg_Segs_t*         rs_p,
  VAPI_lkey_t*        forced_key
)
{
  HH_ret_t  rc;
  MTL_DEBUG4(MT_FLFMT("bufs_register_internal"));
  rc = buf_lst_2_pages(mr_p->phys_buf_lst, mr_p->buf_sz_lst, (unsigned int)mr_p->num_bufs, 
                       mr_p->start, 0 /* no offset */,rs_p);
  MTL_DEBUG4(MT_FLFMT("rc=%d"), rc);
  if (rc == HH_OK)
  {
    rs_p->acl = VAPI_EN_LOCAL_WRITE;
    rc = alloc_reg_pages(rs_p, MPT_int, forced_key);
  }
  if (rs_p->phys_pages) FREE_PHYS_PAGES_ARRAY(rs_p);
  return rc;
} /* bufs_register_internal */


/************************************************************************/
/* Handling  THH_mrwm_register_internal(...) case 
 * where physical buffers are not supplied and pages need to be locked
 */
static HH_ret_t  lock_register_internal(
  THH_internal_mr_t*  mr_props_p,
  Reg_Segs_t*         rs_p,
  VAPI_lkey_t*        forced_key
)
{
  MOSAL_iobuf_props_t iobuf_props;
  MOSAL_protection_ctx_t  ctx;
  HH_ret_t            rc = HH_ENOSYS;
  call_result_t       mosal_rc;

  MTL_DEBUG4(MT_FLFMT("%s: start="U64_FMT" size="U64_FMT), __func__ ,
             mr_props_p->start, mr_props_p->size);  

  /* Arrange for pages locking */
  determine_ctx(rs_p->mrwm, mr_props_p, &ctx);

  mosal_rc = MOSAL_iobuf_register(mr_props_p->start, mr_props_p->size, ctx, 
                                  MOSAL_PERM_READ | MOSAL_PERM_WRITE, &rs_p->iobuf);
  if (mosal_rc != MT_OK) {
    MTL_ERROR4(MT_FLFMT("MOSAL_iobuf_register: rc=%s"), mtl_strerror_sym(mosal_rc));
    return mosal_rc == MT_EAGAIN ? HH_EAGAIN : HH_EINVAL_VA;
  }

  if (MOSAL_iobuf_get_props(rs_p->iobuf,&iobuf_props) != MT_OK) {
    MTL_ERROR4(MT_FLFMT("Failed MOSAL_iobuf_get_props."));
    rc= HH_EINVAL;
  } else {
    rs_p->n_pages= iobuf_props.nr_pages;
    rs_p->log2_page_size = iobuf_props.page_shift;
    rs_p->acl = VAPI_EN_LOCAL_WRITE;
    rc = alloc_reg_pages(rs_p, MPT_int, forced_key);
  }

  if ( rc != HH_OK ) {
    MOSAL_iobuf_deregister(rs_p->iobuf);
    rs_p->iobuf= NULL;
  }
  return rc;
} /* lock_register_internal */


/************************************************************************/
static void  internal_unlock(VIP_delay_unlock_t delay_unlock_obj, MOSAL_iobuf_t iobuf, MT_bool have_fatal)
{
  MTL_DEBUG4(MT_FLFMT("%s: iobuf=0x%p have_fatal=%d"),__func__,iobuf,have_fatal);
  if ( iobuf ) {
    if (have_fatal) {
        VIP_delay_unlock_insert(delay_unlock_obj, iobuf);
    }
    else {
      MOSAL_iobuf_deregister(iobuf);
    }
  }
} /* internal_unlock */



/************************************************************************/
/************************************************************************/
/*                         interface functions                          */


/************************************************************************/
HH_ret_t  THH_mrwm_create(
  THH_hob_t          hob,        /* IN  */
  THH_mrwm_props_t*  mrwm_props, /* IN  */
  THH_mrwm_t*        mrwm_p      /* OUT */
)
{
  HH_ret_t       rc = HH_EAGAIN;
  TMRWM_t*       mrwm = TMALLOC(TMRWM_t);
  u_int32_t      mtt_sz =  1ul << mrwm_props->log2_mtt_sz;
  int i;
  MT_bool        ok = TRUE;

  MTL_TRACE1("{THH_mrwm_create: hob=%p\n", hob);

  if (mrwm) memset(mrwm,0,sizeof(TMRWM_t));
  ok = (mrwm && check_props(mrwm_props, mrwm) &&
        ((rc=THH_hob_get_cmd_if(hob, &mrwm->cmd_if)) == HH_OK) &&
        ((rc=THH_hob_get_uldm(hob, &mrwm->uldm)) == HH_OK));

  /* Key prefix arrays (for "persistant" storage) */
  if (ok) {rc = HH_EAGAIN;} /*reinitialize return code */
  ok= ok && ((mrwm->key_prefix[MPT_int]= 
              TNVMALLOC(u_int16_t,mrwm->props.max_mem_reg_internal)) != NULL);
  ok= ok && ((mrwm->key_prefix[MPT_ext]= 
              TNVMALLOC(u_int16_t,mrwm->props.max_mem_reg)) != NULL);
  ok= ok && ((mrwm->key_prefix[MPT_win]= 
              TNVMALLOC(u_int16_t,mrwm->props.max_mem_win)) != NULL);

  /* we allocate mtt segmenst, each of 2^LOG2_SEG_SIZE entries */
  ok = ok && ((mrwm->xbuddy_tpt =
          extbuddy_create(mtt_sz >> mrwm->props.log2_mtt_seg_sz, 0)) != NULL);
  ok = ok && extbuddy_reserve(mrwm->xbuddy_tpt, 0, (1ul << mrwm->props.log2_rsvd_mtt_segs));

  ok = ok && 
    ((rc= VIP_array_create_maxsize((u_int32_t)(mrwm->props.max_mem_reg_internal>>10),(u_int32_t)mrwm->props.max_mem_reg_internal,
                                   &mrwm->mpt[MPT_int])) == HH_OK);
  ok = ok && 
    ((rc= VIP_array_create_maxsize((u_int32_t)(mrwm->props.max_mem_reg>>10),(u_int32_t)mrwm->props.max_mem_reg,
                                   &mrwm->mpt[MPT_ext])) == HH_OK);
  ok = ok && 
    ((rc= VIP_array_create_maxsize((u_int32_t)(mrwm->props.max_mem_win>>10),(u_int32_t)mrwm->props.max_mem_win,
                                   &mrwm->mpt[MPT_win])) == HH_OK);
  
  if (ok) {
    mrwm->is_fmr_bits= TNMALLOC(u_int8_t,mrwm->props.max_mem_reg>>3);
    if (mrwm->is_fmr_bits == NULL) {
        rc = HH_EAGAIN;
        goto cleanup;
    }
    memset(mrwm->is_fmr_bits,0,sizeof(u_int8_t)*mrwm->props.max_mem_reg>>3);
    mrwm->hob             = hob;
    /* divide MPT to 3 sections: 1) Internal region. 2) External region. 3) mem. windows. */
    mrwm->offset[MPT_int] = (1<<mrwm->props.log2_rsvd_mpts);   
    mrwm->offset[MPT_ext] = (u_int32_t)(mrwm->offset[MPT_int]+mrwm_props->max_mem_reg_internal);
    mrwm->offset[MPT_win] = (u_int32_t)(mrwm->offset[MPT_ext]+mrwm_props->max_mem_reg);
    mrwm->max_mpt[MPT_int]= (u_int32_t)mrwm_props->max_mem_reg_internal;
    mrwm->max_mpt[MPT_ext]= (u_int32_t)mrwm_props->max_mem_reg;
    mrwm->max_mpt[MPT_win]= (u_int32_t)mrwm_props->max_mem_win;
    for (i= 0; i < MPT_N ; i++)  {
      mrwm->usage_cnt[i]= 0;
      memset(mrwm->key_prefix[i],0,sizeof(u_int16_t)*mrwm->max_mpt[i]);
    }
    MOSAL_mutex_init(&mrwm->extbuddy_lock);
    MOSAL_spinlock_init(&mrwm->reserve_lock);
    MOSAL_spinlock_init(&mrwm->key_prefix_lock);
    *mrwm_p = mrwm;
    MTL_TRACE1("}THH_mrwm_create: mrwm=%p,rc=OK\n", *mrwm_p);
    return HH_OK;
  } 

cleanup:
    for (i= 0; i < MPT_N ; i++) {
        if (mrwm->mpt[i])  VIP_array_destroy(mrwm->mpt[i],NULL);
        if (mrwm->key_prefix[i])  VFREE(mrwm->key_prefix[i]);
    }
    if (mrwm->xbuddy_tpt)  extbuddy_destroy(mrwm->xbuddy_tpt);
    IFFREE(mrwm);
    MTL_TRACE1("}THH_mrwm_create: mrwm=%p\n", *mrwm_p);
    logIfErr("THH_mrwm_create")
    return rc;
} /* THH_mrwm_create */


/************************************************************************/
static void VIP_free_mw(void* p)
{
    MTL_ERROR1(MT_FLFMT("found unreleased mw!!!!\n"));
}


HH_ret_t  THH_mrwm_destroy(
  THH_mrwm_t  mrwm,        /* IN */
  MT_bool     hca_failure  /* IN  */
)
{
  int i;
  VIP_common_ret_t ret=VIP_OK;
  VIP_array_handle_t hdl;
  VIP_array_obj_t obj;

  MTL_TRACE1("THH_mrwm_destroy{\n");
  for (i= 0; i < MPT_N ; i++) {
    if (i==MPT_int) {
        ret= VIP_array_get_first_handle(mrwm->mpt[i],&hdl,&obj);
        while (ret == VIP_OK) {
            MTL_ERROR1(MT_FLFMT("found unreleased internal mr!!!!\n"));
            if (!hca_failure)
            {
                THH_mrwm_deregister_mr(mrwm,((Mr_sw_t*)obj)->key);
            }else {
                internal_unlock(THH_hob_get_delay_unlock(mrwm->hob),((Mr_sw_t*)obj)->iobuf, TRUE);
            }
            ret= VIP_array_get_next_handle(mrwm->mpt[i],&hdl,&obj);
        }
    }
    
    if (i==MPT_ext) {
        ret= VIP_array_get_first_handle(mrwm->mpt[i],&hdl,&obj);
        while (ret == VIP_OK) {
            /* check if it's fmr or mr */
            u_int8_t  offset_in_cell = hdl & 0x7;
            if ((mrwm->is_fmr_bits[hdl>>3] >> offset_in_cell) & 0x1) 
            {
                MTL_ERROR1(MT_FLFMT("found unreleased fmr!!!!\n"));
                if (!hca_failure) {
                    THH_mrwm_free_fmr(mrwm,((FMR_sw_info_t*)obj)->last_free_key);
                }
                
            }else 
            {
                MTL_ERROR1(MT_FLFMT("found unreleased mr!!!!\n"));
                if (!hca_failure) {
                    THH_mrwm_deregister_mr(mrwm,((Mr_sw_t*)obj)->key);
                }else{
                    if (((Mr_sw_t*)obj)->shared_p->ref_count > 1) {
                      
                        ((Mr_sw_t*)obj)->shared_p->ref_count--;
                    
                    }else {
                      /*free shared data structures */
                      FREE(((Mr_sw_t*)obj)->shared_p);
                    }
                }
                
            }
            ret= VIP_array_get_next_handle(mrwm->mpt[i],&hdl,&obj);
        }
    }
    
    if (i==MPT_win) {
        VIP_array_destroy(mrwm->mpt[i],VIP_free_mw); 
    }else {
        VIP_array_destroy(mrwm->mpt[i],NULL);
    }
    VFREE(mrwm->key_prefix[i]);
   }
  FREE(mrwm->is_fmr_bits);
  MOSAL_mutex_free(&mrwm->extbuddy_lock);
  extbuddy_destroy(mrwm->xbuddy_tpt);
  FREE(mrwm);

  MTL_TRACE1("}THH_mrwm_destroy\n");
  return HH_OK;
} /* THH_mrwm_destroy */


/************************************************************************/
HH_ret_t  THH_mrwm_register_mr(
  THH_mrwm_t    mrwm,       /* IN  */
  HH_mr_t*      mr_props_p, /* IN  */
  VAPI_lkey_t*  lkey_p,     /* OUT */
  IB_rkey_t*    rkey_p      /* OUT */
)
{
  HH_ret_t        rc = HH_EINVAL;
  HH_tpt_t*       tpt = &mr_props_p->tpt;    /* just a shorthand */
  IB_virt_addr_t  start = mr_props_p->start;
  u_int8_t        page_shift;
  Reg_Segs_t      reg_segs;
  MOSAL_iobuf_props_t iobuf_props;

  MTL_TRACE1("{THH_mrwm_register_mr: mrwm=%p\n", mrwm);
  reg_segs.mrwm = mrwm;
  reg_segs.acl  = mr_props_p->acl;
  reg_segs.iobuf= NULL;
  props2mpt_entry(mr_props_p, &reg_segs.mpt_entry);
  switch (tpt->tpt_type)
  {
    case HH_TPT_PAGE:
      page_shift = tpt->tpt.page_lst.page_shift;
      reg_segs.n_pages = ((start + mr_props_p->size - 1) >> page_shift) -
                          (start                         >> page_shift) + 1;
      MTL_DEBUG4(MT_FLFMT("start="U64_FMT", size="U64_FMT", shift=%d, ne="
                          SIZE_T_DFMT", np="SIZE_T_DFMT),
        (u_int64_t)start, (u_int64_t)mr_props_p->size, page_shift,
         tpt->num_entries, reg_segs.n_pages);
      if (tpt->num_entries != reg_segs.n_pages)
      {
        MTL_ERROR1(MT_FLFMT("mismatch: num_entries="SIZE_T_DFMT" != n_pages="SIZE_T_DFMT),
                   tpt->num_entries, reg_segs.n_pages);
      }
      if (tpt->num_entries >= reg_segs.n_pages)
      {
        if (tpt->num_entries > reg_segs.n_pages)
        {
          MTL_ERROR1(MT_FLFMT("Warning: Extra tpt entries will be ignored"));
        }
        reg_segs.phys_pages     = tpt->tpt.page_lst.phys_page_lst;
        reg_segs.log2_page_size = page_shift;
        rc = alloc_reg_pages(&reg_segs, MPT_ext, NULL);
      }
      break;
    
    case HH_TPT_BUF:
      if ((mr_props_p->tpt.num_entries == 1) 
            && (mr_props_p->tpt.tpt.buf_lst.phys_buf_lst[0] == mr_props_p->start))
        {
          /* no translation needed */
            reg_segs.mpt_entry.pa = TRUE;
            reg_segs.n_pages = 1;
            reg_segs.seg_start=  0;
            reg_segs.phys_pages= NULL;
            rc= HH_OK;
        }else {
            rc = tpt_buf_lst_2_pages(mr_props_p, &reg_segs);
        }
        if (rc == HH_OK)
        {
                rc = alloc_reg_pages(&reg_segs, MPT_ext, NULL);
                if (reg_segs.phys_pages != NULL) FREE_PHYS_PAGES_ARRAY(&reg_segs);
        }
      break;
    
    case HH_TPT_IOBUF:
      reg_segs.iobuf= tpt->tpt.iobuf;
      if (MOSAL_iobuf_get_props(reg_segs.iobuf,&iobuf_props) != MT_OK) {
        MTL_ERROR4(MT_FLFMT("Failed MOSAL_iobuf_get_props."));
        rc= HH_EINVAL;
        break;
      }
      reg_segs.n_pages= iobuf_props.nr_pages;
      reg_segs.log2_page_size = iobuf_props.page_shift;
      rc = alloc_reg_pages(&reg_segs, MPT_ext, NULL);
      break;
    
    default:
      MTL_ERROR1(MT_FLFMT("bad tpt_type=%d"), tpt->tpt_type);
  }
  if (rc == HH_OK)
  {
    *lkey_p = reg_segs.key;
    *rkey_p = reg_segs.key;
  }
  MTL_TRACE1("}THH_mrwm_register_mr: lkey=0x%x\n", *lkey_p);
  logIfErr("THH_mrwm_register_mr")
  return rc;
} /* THH_mrwm_register_mr */


/************************************************************************/
HH_ret_t  THH_mrwm_register_internal(
  THH_mrwm_t          mrwm,        /* IN  */
  THH_internal_mr_t*  mr_props_p,  /* IN  */
  VAPI_lkey_t*        lkey_p       /* OUT */
)
{
  HH_ret_t                rc = HH_EAGAIN;
  Reg_Segs_t              reg_segs;
  VAPI_lkey_t*            forced_key;

  MTL_TRACE1("{THH_mrwm_register_internal: mrwm=%p, force=%d, nbufs="SIZE_T_FMT"\n",
             mrwm, mr_props_p->force_memkey, mr_props_p->num_bufs);
  reg_segs.mrwm = mrwm;
  internal_props2mpt_entry(mr_props_p, &reg_segs.mpt_entry);
  reg_segs.phys_pages = NULL;
  reg_segs.iobuf= NULL;
  forced_key = (mr_props_p->force_memkey ? &mr_props_p->memkey : NULL);
  if (mr_props_p->num_bufs != 0)
  { /* physical buffers supplied */ 
    rc = bufs_register_internal(mr_props_p, &reg_segs, forced_key);
  }
  else
  {
    rc = lock_register_internal(mr_props_p, &reg_segs, forced_key);
  }
  if (rc == HH_OK)
  {
    *lkey_p = reg_segs.key;
  }
  MTL_TRACE1("}THH_mrwm_register_internal: lkey=0x%x\n", *lkey_p);
  logIfErr("THH_mrwm_register_internal")
  return rc;
} /* THH_mrwm_register_internal */

/************************************************************************/
HH_ret_t  THH_mrwm_register_smr(
  THH_mrwm_t    mrwm,       /* IN  */
  HH_smr_t*     smr_props_p, /* IN  */
  VAPI_lkey_t*  lkey_p,     /* OUT */
  IB_rkey_t*    rkey_p      /* OUT */
)
{
    HH_ret_t            rc = HH_EINVAL;
    Reg_Segs_t          reg_segs;
    VIP_common_ret_t    vip_rc;
    VIP_array_obj_t     vip_obj;
    Mr_sw_t             *mrsw_p,*shared_mr_p;
    VIP_array_handle_t  mr_hndl;
    u_int32_t           shared_index; /*index of region shared with*/
 

    MTL_TRACE1("{THH_mrwm_register_smr: mrwm=%p\n", mrwm);
  /* prepare inputs */
  shared_index = smr_props_p->lkey & ((1ul << mrwm->props.log2_mpt_sz) - 1);
  if ((shared_index < mrwm->offset[MPT_ext]) || (shared_index >= mrwm->offset[MPT_win])) {
    MTL_ERROR4(MT_FLFMT("%s: Got Lkey (0x%X) invalid for (ext.) memory region"),__func__,
               smr_props_p->lkey);
    return HH_EINVAL;
  }
  shared_index-= mrwm->offset[MPT_ext];
  /* Hold shared MR while sharing in progress (retain properties until this function done) */
  vip_rc= VIP_array_find_hold(mrwm->mpt[MPT_ext],shared_index,&vip_obj);
  if (vip_rc != VIP_OK) {
    MTL_ERROR4(MT_FLFMT("%s: Failed to find (ext.) region with Lkey=0x%X"),__func__,
               smr_props_p->lkey);
    return HH_EINVAL;
  }
  shared_mr_p= (Mr_sw_t*)vip_obj;
  rc = smr_props2mpt_entry(mrwm,smr_props_p,&reg_segs.mpt_entry);
  if (rc != HH_OK) {
      MTL_ERROR1("failed smr_props2mpt_entry \n");
      goto failed_props2mpt;
  }


  mrsw_p= TMALLOC(Mr_sw_t);
  if (mrsw_p == NULL) {
    MTL_ERROR4(MT_FLFMT("%s: Failed allocation for MR SW context memory"),__func__);
    rc= HH_EAGAIN;
    goto failed_malloc;
  }
  memset(mrsw_p, 0, sizeof(Mr_sw_t));

  reg_segs.acl  = smr_props_p->acl;
  reg_segs.iobuf= NULL;
  /*allocate new mpt idx */
  if (reserve_mpt_entry(mrwm,MPT_ext) != HH_OK) {
    MTL_ERROR4(MT_FLFMT("%s: No more free MPT entry for external regions"),__func__);
    rc= HH_EAGAIN;
    goto failed_reserve_mpt;
  }
  vip_rc= VIP_array_insert(mrwm->mpt[MPT_ext],mrsw_p,&mr_hndl);
  if (vip_rc != VIP_OK)
  {
    MTL_ERROR1("register_smr: ERROR: failed allocating new mpt idx \n");   
    rc= HH_EAGAIN;
    goto failed_array_insert;
  }


  reg_segs.mpt_index =  mrwm->offset[MPT_ext] + mr_hndl;  
  

  /*taking MTT seg start from the mr we're sharing with*/
   reg_segs.seg_start =  shared_mr_p->shared_p->seg_start;
   reg_segs.log2_page_size =  shared_mr_p->shared_p->page_shift;
   reg_segs.mrwm = mrwm;


    rc = register_pages(&reg_segs,MPT_ext,VAPI_MSHAR);   
    if (rc != HH_OK) {
       MTL_ERROR1("register_smr: failed regsiter_pages \n");
       goto failed_register;
    }
    
    /*pointing to the original allocated struct */
    mrsw_p->shared_p = shared_mr_p->shared_p;
    /*saving the new MPT entry ctx*/
    rc = save_sw_context(&reg_segs,0,VAPI_MSHAR,mrsw_p);
    if (rc != HH_OK) {
       MTL_ERROR1("%s: unexpected error !!! failed save_sw_context \n",__func__);
       goto failed_save;
    }
    VIP_array_find_release(mrwm->mpt[MPT_ext],shared_index);
    
    *lkey_p = reg_segs.key;
    *rkey_p = reg_segs.key;
    return HH_OK;

  failed_save:
    THH_cmd_HW2SW_MPT(mrwm->cmd_if, reg_segs.mpt_index, NULL);
  failed_register:
    VIP_array_erase(mrwm->mpt[MPT_ext],mr_hndl,NULL);
  failed_array_insert:
    release_mpt_entry(mrwm,MPT_ext);
  failed_reserve_mpt:
    FREE(mrsw_p);
  failed_malloc:
  failed_props2mpt:
    VIP_array_find_release(mrwm->mpt[MPT_ext],shared_index);
  return rc;

} /* THH_mrwm_register_smr */


/************************************************************************/
HH_ret_t  THH_mrwm_reregister_mr(
                                THH_mrwm_t        mrwm,         /* IN  */
                                VAPI_lkey_t       lkey,
                                VAPI_mr_change_t  change_mask,  /* IN  */
                                HH_mr_t*          mr_props_p,   /* IN  */
                                VAPI_lkey_t*       lkey_p,       /* OUT  */
                                IB_rkey_t*        rkey_p        /* OUT */
                                )
{
    HH_ret_t   rc = HH_OK;
    u_int32_t  mpt_index = lkey & ((1ul << mrwm->props.log2_mpt_sz) - 1);
    VIP_common_ret_t vip_rc;
    THH_cmd_status_t cmd_rc;
    Reg_Segs_t reg_segs;
    u_int8_t log2_segs;
    mpt_segment_t mpt_seg;
    VIP_array_obj_t vip_array_obj;
    Mr_sw_t *mrsw_p;

    memset(&reg_segs,0,sizeof(Reg_Segs_t));
    reg_segs.mrwm = mrwm;
    reg_segs.iobuf= NULL; /* external */
    reg_segs.mpt_index = mpt_index;

    mpt_seg= get_mpt_seg(mrwm,mpt_index);
    if (mpt_seg != MPT_ext) {
      MTL_ERROR4(MT_FLFMT("%s: Invalid L-key (0x%X) for (external) memory region"),__func__,lkey);
      return HH_EINVAL;
    }
    /* hide entry while changing translation (i.e. no other operation allowed - mostly sharing) */
    vip_rc= VIP_array_erase_prepare(mrwm->mpt[mpt_seg],mpt_index-mrwm->offset[mpt_seg],&vip_array_obj);
    if (vip_rc != VIP_OK) {
      MTL_ERROR4(MT_FLFMT("%s: Failed removing L-key (0x%X) for memory region (%s)"),__func__,
                 lkey,VAPI_strerror_sym(vip_rc));
      return vip_rc == VIP_EINVAL_HNDL ? HH_EINVAL : HH_EBUSY;
    }
    mrsw_p= (Mr_sw_t*)vip_array_obj;

    MTL_DEBUG1(MT_FLFMT("before query orig mr \n"));
    /* query original mpt entry */
    cmd_rc = THH_cmd_HW2SW_MPT(mrwm->cmd_if, mpt_index,&reg_segs.mpt_entry);
    if (cmd_rc != THH_CMD_STAT_OK) {
        switch(cmd_rc)  {
        case THH_CMD_STAT_REG_BOUND:
            MTL_ERROR1(MT_FLFMT("There are mw bounded to this region \n"));
            VIP_array_erase_undo(mrwm->mpt[MPT_ext],mpt_index - mrwm->offset[MPT_ext]);
            return HH_EBUSY;
        default:
            rc = HH_EFATAL;
        }
        goto failure_release;
    }
    rc = HH_OK;
    
    /* make new mem key */
    /*reg_segs.mpt_entry.mem_key = make_key(mrwm,mpt_seg,reg_segs.mpt_index);*/
    /* CHANGE: We retain the same memory key - some ULPs don't like that we replace the Rkey */
    
    /* fill changed attributes in sw mpt entry */
    if (change_mask & VAPI_MR_CHANGE_ACL) {
        reg_segs.mpt_entry.lw             = (mr_props_p->acl & VAPI_EN_LOCAL_WRITE ? TRUE : FALSE);
        reg_segs.mpt_entry.rr             = (mr_props_p->acl & VAPI_EN_REMOTE_READ ? TRUE : FALSE);
        reg_segs.mpt_entry.rw             = (mr_props_p->acl & VAPI_EN_REMOTE_WRITE ? TRUE : FALSE);
        reg_segs.mpt_entry.a              = (mr_props_p->acl & VAPI_EN_REMOTE_ATOM ? TRUE : FALSE);
        reg_segs.mpt_entry.eb             = (mr_props_p->acl & VAPI_EN_MEMREG_BIND ? TRUE : FALSE);
        reg_segs.acl = mr_props_p->acl;
    } 

    if (change_mask & VAPI_MR_CHANGE_PD) {
        reg_segs.mpt_entry.pd  = mr_props_p->pd;
    }

    if (change_mask & VAPI_MR_CHANGE_TRANS) {

        MTL_DEBUG1(MT_FLFMT("changed translation \n"));
        rc = change_translation(&reg_segs,mr_props_p,mrsw_p,&log2_segs);
        if (rc != HH_OK) {
            MTL_ERROR1(MT_FLFMT("change translation failed \n"));
            goto failure_release;
        }
        reg_segs.mpt_entry.page_size   = reg_segs.log2_page_size - TAVOR_LOG_MPT_PG_SZ_SHIFT;
        reg_segs.mpt_entry.start_address = mr_props_p->start;
        reg_segs.mpt_entry.reg_wnd_len = mr_props_p->size;
        
        /* write the new MTT's */
        MTL_DEBUG3(MT_FLFMT("before mtt writes \n"));
        if (reg_segs.iobuf != NULL) {
          rc= mtt_writes_iobuf(mrwm->cmd_if, reg_segs.iobuf, reg_segs.mpt_entry.mtt_seg_adr, 
                               reg_segs.n_pages);
        } else {
          rc = mtt_writes(mrwm->cmd_if, reg_segs.phys_pages,reg_segs.mpt_entry.mtt_seg_adr,reg_segs.n_pages);
        }
        if (rc!= HH_OK) {
            MTL_ERROR1(MT_FLFMT("mtt_writes(_iobuf) failed (%s)\n"),HH_strerror_sym(rc));
            goto failure_release;
        }
        MTL_DEBUG4(MT_FLFMT("mtt_writes: rc=%d"), rc);
    }
    

    /* write new MPT to HW */
    cmd_rc = THH_cmd_SW2HW_MPT(reg_segs.mrwm->cmd_if, reg_segs.mpt_index, &reg_segs.mpt_entry);
    MTL_DEBUG4(MT_FLFMT("SW2HW_MPT: cmd_rc=%d"), cmd_rc);
    rc =  (CMDRC2HH_ND(cmd_rc));
    if (rc != HH_OK) {
       goto failure_release;
    }

    /* save the new MPT locally */
    reg_segs.key = reg_segs.mpt_entry.mem_key;
    
    /* save the new attributes locally */
    mrsw_p->key        = reg_segs.key;

    if (change_mask & VAPI_MR_CHANGE_ACL) {
      mrsw_p->acl = reg_segs.acl;  
    } 

    if (change_mask & VAPI_MR_CHANGE_PD) {
       mrsw_p->pd = reg_segs.mpt_entry.pd ;
    }

    if (change_mask & VAPI_MR_CHANGE_TRANS) {
        mrsw_p->start      = reg_segs.mpt_entry.start_address;
        if (mrsw_p->shared_p == NULL) {
            mrsw_p->shared_p = (Shared_data_t*)MALLOC(sizeof(Shared_data_t));
            if (mrsw_p->shared_p == NULL)  {
                        MTL_ERROR1(MT_FLFMT("failed allocating memory \n"));
                        rc=HH_EAGAIN; 
                        goto failure_release;
            }
       }
       mrsw_p->shared_p->size       = reg_segs.mpt_entry.reg_wnd_len;
       mrsw_p->shared_p->seg_start  = reg_segs.seg_start;
       mrsw_p->shared_p->log2_segs  = log2_segs;
       mrsw_p->shared_p->page_shift = reg_segs.log2_page_size;
       mrsw_p->shared_p->ref_count = 1;
       MOSAL_spinlock_init(&mrsw_p->shared_p->ref_lock);
       if ((mr_props_p->tpt.tpt_type == HH_TPT_BUF) && (reg_segs.phys_pages != NULL)) 
           FREE_PHYS_PAGES_ARRAY(&reg_segs);
    }
    
    VIP_array_erase_undo(mrwm->mpt[MPT_ext],mpt_index - mrwm->offset[MPT_ext]);
    *lkey_p = reg_segs.mpt_entry.mem_key;
    *rkey_p = reg_segs.mpt_entry.mem_key;
    logIfErr("THH_mrwm_reregister_mr")
    return rc;

    failure_release:
      /* Invalidate entry after failure - as requested by IB */
      if (mrsw_p->shared_p) { /* still has MTTs to release */
        release_shared_mtts(mrwm,mrsw_p);
      }
      if ((mr_props_p->tpt.tpt_type == HH_TPT_BUF) && (reg_segs.phys_pages != NULL)) 
        FREE_PHYS_PAGES_ARRAY(&reg_segs);
      VIP_array_erase_done(mrwm->mpt[MPT_ext],mpt_index - mrwm->offset[MPT_ext],NULL);
      release_mpt_entry(mrwm,MPT_ext);
      FREE(mrsw_p);
      return rc;
} /* THH_mrwm_reregister_mr */

/************************************************************************/
HH_ret_t  THH_mrwm_query_mr(
  THH_mrwm_t    mrwm,      /* IN  */
  VAPI_lkey_t   lkey,      /* IN  */
  HH_mr_info_t* mr_info_p  /* OUT */
)
{
  VIP_common_ret_t vip_rc;
  u_int32_t  mpt_index = lkey & ((1ul << mrwm->props.log2_mpt_sz) - 1);
  mpt_segment_t mpt_seg;
  VIP_array_obj_t vip_obj;
  Mr_sw_t *mrsw_p;

  mpt_seg= get_mpt_seg(mrwm,mpt_index);
  if ((mpt_seg != MPT_ext) && (mpt_seg != MPT_int)){
    MTL_ERROR4(MT_FLFMT("%s: Invalid L-key (0x%X) for memory region"),__func__,lkey);
    return HH_EINVAL;
  }
  vip_rc= VIP_array_find_hold(mrwm->mpt[mpt_seg],mpt_index-mrwm->offset[mpt_seg],&vip_obj);
  if (vip_rc != VIP_OK) {
    MTL_ERROR4(MT_FLFMT("%s: Failed finding a memory region with L-key 0x%X (%s)"),__func__,
               lkey,VAPI_strerror_sym(vip_rc));
    return (HH_ret_t)vip_rc;
  }
  mrsw_p= (Mr_sw_t*)vip_obj;
  swinfo2mrinfo(mrsw_p, mr_info_p);
  VIP_array_find_release(mrwm->mpt[mpt_seg],mpt_index-mrwm->offset[mpt_seg]);
  MTL_TRACE1("}THH_mrwm_query_mr\n");
  return HH_OK;
} /* THH_mrwm_query_mr */


/************************************************************************/
HH_ret_t  THH_mrwm_deregister_mr(
  THH_mrwm_t   mrwm, /* IN  */
  VAPI_lkey_t  lkey  /* IN  */
)
{
  HH_ret_t   rc = HH_EINVAL;
  VIP_common_ret_t vip_rc= VIP_EINVAL_PARAM;
  u_int32_t  mpt_index = lkey & ((1ul << mrwm->props.log2_mpt_sz) - 1);
  MT_bool    have_fatal = FALSE;
  VIP_array_obj_t vip_obj;
  Mr_sw_t *mrsw_p=NULL;
  THH_cmd_status_t cmd_rc = THH_CMD_STAT_OK;
  mpt_segment_t mpt_seg;

  MTL_TRACE1("{THH_mrwm_deregister_mr: mrwm=%p, lkey=0x%x, mi=0x%x\n",
             mrwm, lkey, mpt_index);
  mpt_seg= get_mpt_seg(mrwm,mpt_index);
  if ((mpt_seg != MPT_ext) && (mpt_seg != MPT_int)){
    MTL_ERROR4(MT_FLFMT("%s: Invalid L-key (0x%X) for memory region"),__func__,lkey);
    return HH_EINVAL;
  }
  vip_rc= VIP_array_erase_prepare(mrwm->mpt[mpt_seg],mpt_index-mrwm->offset[mpt_seg],&vip_obj);
  if (vip_rc != VIP_OK) {
    MTL_ERROR4(MT_FLFMT("%s: Failed removing L-key (0x%X) for memory region (%s)"),__func__,
               lkey,VAPI_strerror_sym(vip_rc));
    return (HH_ret_t)vip_rc;
  }
  mrsw_p= (Mr_sw_t*)vip_obj;

#if defined(MT_SUSPEND_QP)
  if ((mpt_seg != MPT_int) || (mrsw_p->is_suspended == FALSE)) {
      /* if region IS suspended, is is already in SW ownership */
      cmd_rc = THH_cmd_HW2SW_MPT(mrwm->cmd_if, mpt_index, NULL);
  }
#else
  cmd_rc = THH_cmd_HW2SW_MPT(mrwm->cmd_if, mpt_index, NULL);
#endif
  /* for memory regions only, anything that is not a 'legal' return code is considered fatal.
   * In all problem cases, the unlocking of memory is deferred until THH_hob_destroy.
   */
  MTL_DEBUG4(MT_FLFMT("cmd_rc=%d=%s"), cmd_rc, str_THH_cmd_status_t(cmd_rc));
  switch(cmd_rc) {
    case THH_CMD_STAT_RESOURCE_BUSY:
    case THH_CMD_STAT_REG_BOUND:
      VIP_array_erase_undo(mrwm->mpt[mpt_seg],mpt_index-mrwm->offset[mpt_seg]);
      rc = HH_EBUSY;
      break;
    case THH_CMD_STAT_EINTR:
      VIP_array_erase_undo(mrwm->mpt[mpt_seg],mpt_index-mrwm->offset[mpt_seg]);
      rc = HH_EINTR;
      break;
    default:  /* OK and all fatal errors*/
      {
        have_fatal = (cmd_rc != THH_CMD_STAT_OK) ? TRUE : FALSE;
        if (have_fatal && (cmd_rc != THH_CMD_STAT_EFATAL)) {
          MTL_ERROR1(MT_FLFMT("POSSIBLE FATAL ERROR:cmd_rc=%d=%s"), cmd_rc, str_THH_cmd_status_t(cmd_rc));
       }
       
       VIP_array_erase_done(mrwm->mpt[mpt_seg],mpt_index-mrwm->offset[mpt_seg],NULL);
       release_mpt_entry(mrwm,mpt_seg);
       if (mpt_index >= mrwm->offset[MPT_ext]) {   
           /* return fatal for external mem region if had fatal */
         release_shared_mtts(mrwm,mrsw_p);
         rc = (have_fatal == TRUE) ? HH_EFATAL : HH_OK ;
       } else  { /* internal region */
         /* return OK for internal mem region even if had fatal, because we are
          * handling deferred unlocking here.
          */
         internal_unlock(THH_hob_get_delay_unlock(mrwm->hob), mrsw_p->iobuf, have_fatal);
         release_shared_mtts(mrwm,mrsw_p);
         rc = HH_OK;
       }
       FREE(mrsw_p);
     }
  }
  
  MTL_TRACE1("}THH_mrwm_deregister_mr, rc=%d\n", rc);
  if (rc != HH_EFATAL) {
      logIfErr("THH_mrwm_deregister_mr")
  }
  return rc;
} /* THH_mrwm_deregister_mr */


/************************************************************************/
HH_ret_t  THH_mrwm_alloc_mw(
  THH_mrwm_t    mrwm,          /* IN  */
  HH_pd_hndl_t  pd,            /* IN  */
  IB_rkey_t*    initial_rkey_p /* OUT */
)
{
  HH_ret_t      rc = HH_EAGAIN;
  VIP_array_handle_t win_hndl;
  VIP_common_ret_t vip_rc;

  MTL_TRACE1("{THH_mrwm_alloc_mw: mrwm=%p, pd=%d\n", mrwm, pd);
  if (reserve_mpt_entry(mrwm,MPT_win) != HH_OK) {
    MTL_ERROR4(MT_FLFMT("%s: No more free MPT entries for memory windows"),__func__);
    return HH_EAGAIN;
  }
  vip_rc= VIP_array_insert(mrwm->mpt[MPT_win],NULL,&win_hndl);

  if (vip_rc == VIP_OK)
  {
    THH_mpt_entry_t   mpt_entry;
    THH_cmd_status_t  cmd_rc;
    u_int32_t         mpt_index = mrwm->offset[MPT_win] + win_hndl;
    
    mpt_entry_init(&mpt_entry);
    mpt_entry.pd  = pd;
    mpt_entry.r_w = FALSE;
    mpt_entry.mem_key   = CURRENT_MEMKEY(mrwm,MPT_win,mpt_index);
    cmd_rc = THH_cmd_SW2HW_MPT(mrwm->cmd_if, mpt_index, &mpt_entry);
    MTL_DEBUG4(MT_FLFMT("alloc_mw: cmd_rc=%d=%s"), 
               cmd_rc, str_THH_cmd_status_t(cmd_rc));
    rc = (CMDRC2HH_ND(cmd_rc));
    *initial_rkey_p = mpt_entry.mem_key;
  }
  if ((rc != HH_OK) && (vip_rc == VIP_OK)) {
    VIP_array_erase(mrwm->mpt[MPT_win],win_hndl,NULL);
    release_mpt_entry(mrwm,MPT_win);
  }
  MTL_TRACE1("}THH_mrwm_alloc_mw: ley=0x%x\n", *initial_rkey_p);
  logIfErr("THH_mrwm_alloc_mw")
  return rc;
} /* THH_mrwm_alloc_mw */


/************************************************************************/
HH_ret_t  THH_mrwm_query_mw(
  THH_mrwm_t     mrwm,            /* IN  */
  IB_rkey_t      initial_rkey,    /* IN  */
  IB_rkey_t*     current_rkey_p,  /* OUT */
  HH_pd_hndl_t*  pd_p             /* OUT */
)
{
  HH_ret_t   rc = HH_EINVAL;
  VIP_common_ret_t vip_rc;
  u_int32_t mpt_index= initial_rkey & MASK32(mrwm->props.log2_mpt_sz);
  u_int32_t  win_index= mpt_index-mrwm->offset[MPT_win];
  VIP_array_obj_t win_obj; 
  MTL_TRACE1("{THH_mrwm_query_mw: mrwm=%p, ini_key=0x%x\n", mrwm, initial_rkey);
  
  if (get_mpt_seg(mrwm,mpt_index) != MPT_win) {
    MTL_ERROR4(MT_FLFMT("%s: Invalid initial R-key for memory window (0x%X)"),__func__,
               initial_rkey);
    return HH_EINVAL_MW;
  }
  vip_rc= VIP_array_find_hold(mrwm->mpt[MPT_win],win_index,&win_obj);
  if (vip_rc != VIP_OK) {
    MTL_ERROR3(MT_FLFMT("%s: Invalid mem-window memkey (0x%X)"),__func__,initial_rkey);
  } else {
    THH_mpt_entry_t   mpt_entry;
    THH_cmd_status_t  cmd_rc;
    mpt_entry_init(&mpt_entry); /* not really needed */
    cmd_rc = THH_cmd_QUERY_MPT(mrwm->cmd_if, mpt_index, &mpt_entry);
    rc = (CMDRC2HH_ND(cmd_rc));
    if (cmd_rc == THH_CMD_STAT_OK)
    {
      *current_rkey_p = mpt_entry.mem_key;
      *pd_p           = mpt_entry.pd;
    }
  }
  
  VIP_array_find_release(mrwm->mpt[MPT_win],win_index);
  MTL_TRACE1("}THH_mrwm_query_mw: cur_key=0x%x, pd=%d\n",
             *current_rkey_p, *current_rkey_p);
  logIfErr("THH_mrwm_query_mw")
  return rc;
} /* THH_mrwm_query_mw */


/************************************************************************/
HH_ret_t  THH_mrwm_free_mw(
  THH_mrwm_t  mrwm,         /* IN  */
  IB_rkey_t   initial_rkey  /* IN  */
)
{
  HH_ret_t   rc = HH_EINVAL;
  VIP_common_ret_t vip_rc;
  VIP_array_obj_t win_obj;
  u_int32_t mpt_index= initial_rkey & MASK32(mrwm->props.log2_mpt_sz);
  u_int32_t  win_index= mpt_index-mrwm->offset[MPT_win];
  MTL_TRACE1("{THH_mrwm_free_mw: mrwm=%p, ini_key=0x%x\n", mrwm, initial_rkey);
  
  if (get_mpt_seg(mrwm,mpt_index) != MPT_win) {
    MTL_ERROR4(MT_FLFMT("%s: Invalid initial R-key for memory window (0x%X)"),__func__,
               initial_rkey);
    return HH_EINVAL_MW;
  }
  vip_rc= VIP_array_erase_prepare(mrwm->mpt[MPT_win],win_index,&win_obj);
  if (vip_rc != VIP_OK) {
    MTL_ERROR3(MT_FLFMT("%s: Invalid mem-window memkey (0x%X)"),__func__,initial_rkey);
  } else {
    THH_mpt_entry_t   mpt_entry;
    THH_cmd_status_t  cmd_rc = THH_cmd_HW2SW_MPT(mrwm->cmd_if, mpt_index,
                                                 &mpt_entry);
    switch(cmd_rc) {
    case THH_CMD_STAT_OK:
        rc = HH_OK;
        break;
    case THH_CMD_STAT_RESOURCE_BUSY:
        rc = HH_EBUSY;
        break;
    case THH_CMD_STAT_EINTR:
        rc = HH_EINTR;
        break;
    default:
        rc = HH_EFATAL;
    }
    if (cmd_rc == THH_CMD_STAT_OK)
    {
      mrwm->key_prefix[MPT_win][win_index] = /* sync key prefix for next allocation */
        (mpt_entry.mem_key >> mrwm->props.log2_mpt_sz) + 1;
    }
    if ((rc == HH_OK) || (rc == HH_EFATAL)) {
      VIP_array_erase_done(mrwm->mpt[MPT_win],win_index,NULL);
      release_mpt_entry(mrwm,MPT_win);
    } else {
      VIP_array_erase_undo(mrwm->mpt[MPT_win],win_index);
    }
  }
  MTL_TRACE1("}THH_mrwm_free_mw\n");
  if (rc != HH_EFATAL) {
      logIfErr("THH_mrwm_free_mw")
  }
  return rc;
} /* THH_mrwm_free_mw */


HH_ret_t  THH_mrwm_alloc_fmr(THH_mrwm_t     mrwm,        /*IN*/
                             HH_pd_hndl_t   pd,          /*IN*/
                             VAPI_mrw_acl_t acl,         /*IN*/
                             MT_size_t      max_pages,   /*IN*/   
                             u_int8_t       log2_page_sz,/*IN*/
                             VAPI_lkey_t*   last_lkey_p) /*OUT*/
{
  HH_ret_t    rc = HH_EAGAIN;
  VIP_common_ret_t vip_rc;
  FMR_sw_info_t* new_fmr_p;
  VIP_array_handle_t fmr_hndl;
  u_int32_t   seg_start = EXTBUDDY_NULL;
  u_int8_t    log2_mtt_seg_sz = mrwm->props.log2_mtt_seg_sz;
  u_int32_t   n_segs;
  u_int8_t    log2_segs;
  THH_mpt_entry_t mpt_entry;
  THH_mpt_index_t mpt_index;
  THH_cmd_status_t cmd_rc;
  MT_size_t seg_comp = max_pages >> log2_mtt_seg_sz;
  

  /*compute n_segs: round it up to mtt seg size multiple    */
  seg_comp= ((seg_comp << log2_mtt_seg_sz) != max_pages) ? seg_comp + 1 : seg_comp;
  /*check that n_segs will not overflow 32 bits */
  log2_segs = ceil_log2(seg_comp);
  if (log2_segs >= (8*sizeof(n_segs)))  return HH_EINVAL_PARAM;
  n_segs = 1 << log2_segs;


  if (log2_page_sz < TAVOR_LOG_MPT_PG_SZ_SHIFT) {
    MTL_ERROR4(MT_FLFMT("Given log2_page_sz too small (%d)"),log2_page_sz);
    return HH_EINVAL_PARAM;
  }

  new_fmr_p= TMALLOC(FMR_sw_info_t);
  if (new_fmr_p == NULL) {
    MTL_ERROR4(MT_FLFMT("%s: Failed allocating memory for FMR context"),__func__);
    goto failed_malloc; /* HH_EAGAIN */
  }

  if (reserve_mpt_entry(mrwm,MPT_ext) != HH_OK) {
    MTL_ERROR4(MT_FLFMT("%s: No more free MPT entry for external regions"),__func__);
    goto failed_reserve_mpt;
  }
  vip_rc= VIP_array_insert(mrwm->mpt[MPT_ext],new_fmr_p,&fmr_hndl);
  if (vip_rc != VIP_OK)  {
    MTL_ERROR1(MT_FLFMT("Failed allocating MPT entry for FMR (%s)"),VAPI_strerror_sym(vip_rc));
    rc= HH_EAGAIN;
    goto failed_array_insert;
  }
  
  /* set the fmr_bit in the array */
  {
      u_int8_t  offset_in_cell = fmr_hndl & 0x7;
      mrwm->is_fmr_bits[fmr_hndl>>3]|= (((u_int8_t)1) << offset_in_cell);
  }

  /* we must ensure at least one segment for each region */
  if (reserve_mtt_segs(mrwm,n_segs-1) != HH_OK) {
    MTL_ERROR4(MT_FLFMT("Not enough available MTT segments for a new FMR of %d segments"),n_segs);
    rc= HH_EAGAIN;
    goto failed_out_of_mtt;
  }
  if (MOSAL_mutex_acq(&mrwm->extbuddy_lock, TRUE) != MT_OK)  {
    rc= HH_EINTR;  /* Operation interrupted */
    goto failed_mutex;
  }
  seg_start = extbuddy_alloc(mrwm->xbuddy_tpt, log2_segs);
  if (seg_start != EXTBUDDY_NULL) {EXTBUDDY_ALLOC_MTT(seg_start,log2_segs);}
  MOSAL_mutex_rel(&mrwm->extbuddy_lock);
  if (seg_start == EXTBUDDY_NULL) {
    MTL_ERROR1(MT_FLFMT("Failed allocating MTT segments for FMR"));
    rc= HH_EAGAIN;
    goto failed_extbd;
  }

  mpt_index = mrwm->offset[MPT_ext] + fmr_hndl;
  init_fmr_mpt_entry(&mpt_entry,pd,acl,make_key(mrwm, MPT_ext,(u_int32_t)mpt_index),log2_page_sz,
    mrwm->props.mtt_base | (seg_start << (mrwm->props.log2_mtt_seg_sz + MTT_LOG_MTT_ENTRY_SIZE)) );
  MTL_DEBUG4(MT_FLFMT("mtt_seg_adr="U64_FMT), mpt_entry.mtt_seg_adr);

  cmd_rc = THH_cmd_SW2HW_MPT(mrwm->cmd_if, mpt_index, &mpt_entry);
  if (cmd_rc != THH_CMD_STAT_OK) {
    MTL_ERROR1(MT_FLFMT("SW2HW_MPT failed - cmd_rc=%d"), cmd_rc);
    rc =  (cmd_rc == THH_CMD_STAT_EINTR) ? HH_EINTR : HH_EFATAL;  
    goto failed_sw2hw_mpt;
  }
  
  /*saving the new MPT entry */
  rc = init_fmr_context(mrwm,new_fmr_p ,(u_int32_t)mpt_index, seg_start, log2_segs, log2_page_sz); 
  if (rc != HH_OK) {
    MTL_ERROR1(MT_FLFMT("failed init_fmr_context() \n"));
    goto failed_sw_ctx;
  }

  *last_lkey_p = mpt_entry.mem_key;
  return rc;

  failed_sw_ctx:
    if (THH_cmd_HW2SW_MPT(mrwm->cmd_if, mpt_index, &mpt_entry) != THH_CMD_STAT_OK)  rc= HH_EFATAL;
  failed_sw2hw_mpt:
    MOSAL_mutex_acq_ui(&mrwm->extbuddy_lock);
    extbuddy_free(mrwm->xbuddy_tpt, seg_start,log2_segs);
    EXTBUDDY_FREE_MTT(seg_start, log2_segs);
    MOSAL_mutex_rel(&mrwm->extbuddy_lock);
  failed_extbd:
  failed_mutex:
    release_mtt_segs(mrwm,n_segs - 1);
  failed_out_of_mtt:
    VIP_array_erase(mrwm->mpt[MPT_ext],fmr_hndl,NULL);
  failed_array_insert:
    release_mpt_entry(mrwm,MPT_ext);
  failed_reserve_mpt:
    FREE(new_fmr_p);
  failed_malloc:
    return rc;
}

HH_ret_t  THH_mrwm_map_fmr(THH_mrwm_t       mrwm,        /*IN*/
                           VAPI_lkey_t      last_lkey,   /*IN*/
                           EVAPI_fmr_map_t* map_p,       /*IN*/
                           VAPI_lkey_t*     lkey_p,      /*OUT*/
                           IB_rkey_t*       rkey_p)      /*OUT*/
{
  u_int32_t mpt_index= last_lkey & MASK32(mrwm->props.log2_mpt_sz);
  u_int32_t fmr_hndl;
  VIP_array_obj_t vip_obj;
  FMR_sw_info_t* fmr_info_p;
  MT_size_t max_pages,real_num_of_pages,i;
  u_int32_t cur_memkey,new_memkey;
#ifndef WRITE_QWORD_WORKAROUND
  volatile u_int64_t tmp_qword;
#endif
  MT_virt_addr_t cur_mtt_p;
  VIP_common_ret_t vip_rc;

  /* Validity checks */
  
  if (get_mpt_seg(mrwm,mpt_index) != MPT_ext){
    MTL_ERROR3(MT_FLFMT("%s: Invalid FMR lkey (0x%X)"),__func__,last_lkey);
    return HH_EINVAL;
  }
  fmr_hndl= mpt_index-mrwm->offset[MPT_ext];
  vip_rc= VIP_array_find_hold(mrwm->mpt[MPT_ext],fmr_hndl,&vip_obj);
  if (vip_rc != VIP_OK) {
    MTL_ERROR2(MT_FLFMT("THH_mrmw_map_fmr invoked for invalid MPT (last_lkey=0x%X)"),last_lkey);
    return HH_EINVAL;
  }
  fmr_info_p= (FMR_sw_info_t*)vip_obj;
  cur_memkey= CURRENT_MEMKEY(mrwm,MPT_ext,mpt_index);
  if (last_lkey != cur_memkey) {
    VIP_array_find_release(mrwm->mpt[MPT_ext],fmr_hndl);
    MTL_ERROR2(MT_FLFMT("THH_mrmw_map_fmr invoked with last_lkey=0x%X while current lkey=0x%X"),
               last_lkey,cur_memkey);
    return HH_EINVAL;
  }
  
  max_pages= 1<<(fmr_info_p->log2_segs + mrwm->props.log2_mtt_seg_sz);  
  /* TBD: possible optimization for line above: save max_pages on FMR allocation in FMR_sw_info_t */
  real_num_of_pages= ((map_p->start + map_p->size - 1) >> fmr_info_p->log2_page_sz) -   /* end_page - start_page + 1 */
                      (map_p->start >> fmr_info_p->log2_page_sz) + 1; 
  if ((map_p->page_array_len > max_pages) || (real_num_of_pages != map_p->page_array_len)) {
    VIP_array_find_release(mrwm->mpt[MPT_ext],fmr_hndl);
     MTL_ERROR2(MT_FLFMT("%s: illegal number of pages for mapping FMR at MPT index 0x%X: start="U64_FMT
                         " , size="U64_FMT" , log2_page_sz=%d, "
                        "real_num_of_pages="SIZE_T_DFMT" , page_array_len="SIZE_T_DFMT" , max_pages="SIZE_T_DFMT), __func__,mpt_index,map_p->start, map_p->size,fmr_info_p->log2_page_sz,  
               real_num_of_pages,map_p->page_array_len,max_pages);
    return HH_EINVAL;
  }
  
  /* Compute new memory key */
  MOSAL_spinlock_irq_lock(&mrwm->key_prefix_lock);
  ++(mrwm->key_prefix[MPT_ext][fmr_hndl]);
  new_memkey= CURRENT_MEMKEY(mrwm,MPT_ext,mpt_index);
  if (new_memkey == fmr_info_p->last_free_key) {
    mrwm->key_prefix[MPT_ext][fmr_hndl]--; /* Restore previous key */
    MOSAL_spinlock_unlock(&mrwm->key_prefix_lock);
    VIP_array_find_release(mrwm->mpt[MPT_ext],fmr_hndl);
    MTL_DEBUG4(MT_FLFMT("Wrap around of memory key detected for MPT index %d (last_free_key=0x%X)"),
               mpt_index,new_memkey);
    return HH_EAGAIN; /* Retry after unmapping */
  }
  MOSAL_spinlock_unlock(&mrwm->key_prefix_lock);

  if (cur_memkey != fmr_info_p->last_free_key) {  /* It's a "remap" - invalidate MPT before updating MTT andother MPT fields */
    MOSAL_MMAP_IO_WRITE_BYTE((fmr_info_p->mpt_entry + TAVOR_IF_MPT_HW_STATUS_OFFSET),0xf0);
  }

  for (i= 0, cur_mtt_p= fmr_info_p->mtt_entries; i < real_num_of_pages; i++) {  /* Write MTT entries */
#ifdef WRITE_QWORD_WORKAROUND
    MOSAL_MMAP_IO_WRITE_DWORD(cur_mtt_p,MOSAL_cpu_to_be32((u_int32_t)(map_p->page_array[i] >> 32)));
    MOSAL_MMAP_IO_WRITE_DWORD(cur_mtt_p+4,MOSAL_cpu_to_be32(((u_int32_t)(map_p->page_array[i] & 0xFFFFFFFF)) | 1);
#else
    ((volatile u_int32_t*)&tmp_qword)[0]= 
      MOSAL_cpu_to_be32((u_int32_t)(map_p->page_array[i] >> 32));              /* ptag_h */
    ((volatile u_int32_t*)&tmp_qword)[1]= 
      MOSAL_cpu_to_be32(((u_int32_t)(map_p->page_array[i] & 0xFFFFF000)) | 1); /* ptag_l | p */
    MOSAL_MMAP_IO_WRITE_QWORD(cur_mtt_p, tmp_qword );
#endif
    cur_mtt_p+= (1<<LOG2_MTT_ENTRY_SZ);
  }
#ifdef WRITE_QWORD_WORKAROUND
  MOSAL_MMAP_IO_WRITE_DWORD((fmr_info_p->mpt_entry + TAVOR_IF_MPT_HW_START_ADDR_OFFSET),
                            MOSAL_cpu_to_be32((u_int32_t)(map_p->start >> 32)));
  MOSAL_MMAP_IO_WRITE_DWORD((fmr_info_p->mpt_entry + TAVOR_IF_MPT_HW_START_ADDR_OFFSET + 4),
                            MOSAL_cpu_to_be32((u_int32_t)(map_p->start  & 0xFFFFFFFF)));
#else
  ((volatile u_int32_t*)&tmp_qword)[0]= 
    MOSAL_cpu_to_be32((u_int32_t)(map_p->start >> 32));         /* start_h */   
  ((volatile u_int32_t*)&tmp_qword)[1]= 
    MOSAL_cpu_to_be32((u_int32_t)(map_p->start  & 0xFFFFFFFF)); /* start_l */
  MOSAL_MMAP_IO_WRITE_QWORD((fmr_info_p->mpt_entry + TAVOR_IF_MPT_HW_START_ADDR_OFFSET),tmp_qword);
#endif
  /* MemKey+Lkey update */
  MOSAL_MMAP_IO_WRITE_DWORD((fmr_info_p->mpt_entry + TAVOR_IF_MPT_HW_MEMKEY_OFFSET),MOSAL_cpu_to_be32(new_memkey));
  MOSAL_MMAP_IO_WRITE_DWORD((fmr_info_p->mpt_entry + TAVOR_IF_MPT_HW_LKEY_OFFSET),MOSAL_cpu_to_be32(new_memkey));

#ifdef WRITE_QWORD_WORKAROUND
  MOSAL_MMAP_IO_WRITE_DWORD((fmr_info_p->mpt_entry + TAVOR_IF_MPT_HW_LEN_OFFSET),
                            MOSAL_cpu_to_be32((u_int32_t)(map_p->size >> 32)));
  MOSAL_MMAP_IO_WRITE_DWORD((fmr_info_p->mpt_entry + TAVOR_IF_MPT_HW_LEN_OFFSET + 4),
                            MOSAL_cpu_to_be32((u_int32_t)(map_p->size  & 0xFFFFFFFF)));
#else
  ((volatile u_int32_t*)&tmp_qword)[0]= 
    MOSAL_cpu_to_be32((u_int32_t)(map_p->size >> 32));         /* length_h */   
  ((volatile u_int32_t*)&tmp_qword)[1]= 
    MOSAL_cpu_to_be32((u_int32_t)(map_p->size  & 0xFFFFFFFF)); /* length_l */
  MOSAL_MMAP_IO_WRITE_QWORD((fmr_info_p->mpt_entry + TAVOR_IF_MPT_HW_LEN_OFFSET),tmp_qword); /* length change makes MPT valid again */
#endif
   /* revalidate this MPT */
  MOSAL_MMAP_IO_WRITE_BYTE((fmr_info_p->mpt_entry + TAVOR_IF_MPT_HW_STATUS_OFFSET),0);

  VIP_array_find_release(mrwm->mpt[MPT_ext],fmr_hndl);
  *lkey_p= new_memkey;
  *rkey_p= new_memkey;
  return HH_OK;
}

HH_ret_t  THH_mrwm_unmap_fmr(THH_mrwm_t       mrwm,             /*IN*/
                             u_int32_t     num_of_fmrs_to_unmap,/*IN*/
                             VAPI_lkey_t*  last_lkeys_array)    /*IN*/
{
  u_int32_t mpt_index,fmr_hndl;
  u_int32_t index_mask= MASK32(mrwm->props.log2_mpt_sz);
  VIP_array_obj_t vip_obj;
  FMR_sw_info_t* fmr_info_p;
  u_int32_t cur_memkey;
  u_int32_t i;
  THH_cmd_status_t cmd_rc;
  VIP_common_ret_t vip_rc;

  for (i= 0; i < num_of_fmrs_to_unmap; i++) {
    mpt_index= last_lkeys_array[i] & index_mask;
    if (get_mpt_seg(mrwm,mpt_index) != MPT_ext){
      MTL_ERROR3(MT_FLFMT("%s: Invalid FMR lkey (0x%X)"),__func__,last_lkeys_array[i]);
      continue;
    }
    fmr_hndl= mpt_index-mrwm->offset[MPT_ext];
    vip_rc= VIP_array_find_hold(mrwm->mpt[MPT_ext],fmr_hndl,&vip_obj);
    if (vip_rc != VIP_OK) {
      MTL_ERROR2(MT_FLFMT("THH_mrmw_map_fmr invoked for invalid MPT (last_lkey=0x%X)"),
                 last_lkeys_array[i]);
      continue;
    }
    fmr_info_p= (FMR_sw_info_t*)vip_obj;
    cur_memkey= CURRENT_MEMKEY(mrwm,MPT_ext,mpt_index);
    if (last_lkeys_array[i] != cur_memkey) {
      VIP_array_find_release(mrwm->mpt[MPT_ext],fmr_hndl);
      MTL_ERROR2(MT_FLFMT("THH_mrmw_map_fmr invoked with last_lkey=0x%X while current lkey=0x%X"),
                 last_lkeys_array[i],cur_memkey);
      continue; /* continue unmap for any region we can */
    }
    
    MOSAL_MMAP_IO_WRITE_BYTE((fmr_info_p->mpt_entry + TAVOR_IF_MPT_HW_STATUS_OFFSET),0xf0); /* invalidate mpt */
    fmr_info_p->last_free_key= cur_memkey;
    VIP_array_find_release(mrwm->mpt[MPT_ext],fmr_hndl);
  }

  cmd_rc = THH_cmd_SYNC_TPT(mrwm->cmd_if);
  if ((cmd_rc != THH_CMD_STAT_OK) && (cmd_rc != THH_CMD_STAT_EINTR)) {
    MTL_ERROR1(MT_FLFMT("Fatal error: Command SYNC_TPT failed"));
  }
  return (CMDRC2HH_ND(cmd_rc));
}

HH_ret_t  THH_mrwm_free_fmr(THH_mrwm_t       mrwm,      /*IN*/
                            VAPI_lkey_t    last_lkey)   /*IN*/

{
  u_int32_t mpt_index= last_lkey & MASK32(mrwm->props.log2_mpt_sz);
  VIP_array_obj_t vip_obj;
  VIP_array_handle_t fmr_hndl;
  FMR_sw_info_t* fmr_info_p;
  THH_cmd_status_t stat;
  VIP_common_ret_t vip_rc;

  /* Validity checks */
  if (get_mpt_seg(mrwm,mpt_index) != MPT_ext){
    MTL_ERROR3(MT_FLFMT("%s: Invalid FMR lkey (0x%X)"),__func__,last_lkey);
    return HH_EINVAL;
  }
  fmr_hndl= mpt_index-mrwm->offset[MPT_ext];
  vip_rc= VIP_array_erase_prepare(mrwm->mpt[MPT_ext],fmr_hndl,&vip_obj);
  if (vip_rc != VIP_OK) {
    MTL_ERROR2(MT_FLFMT("THH_mrmw_map_fmr invoked for invalid MPT (last_lkey=0x%X)"),last_lkey);
    return HH_EINVAL;
  }
  fmr_info_p= (FMR_sw_info_t*)vip_obj;
  
  if (last_lkey != CURRENT_MEMKEY(mrwm,MPT_ext,mpt_index)) {
    VIP_array_erase_undo(mrwm->mpt[MPT_ext],fmr_hndl);
    MTL_ERROR2(MT_FLFMT("THH_mrwm_free_fmr invoked with last_lkey=0x%X while current lkey=0x%X"),
               last_lkey,CURRENT_MEMKEY(mrwm,MPT_ext,mpt_index));
    return HH_EINVAL;
  }
  
  MOSAL_MMAP_IO_WRITE_BYTE((fmr_info_p->mpt_entry + TAVOR_IF_MPT_HW_STATUS_OFFSET),0xf0); /* invalidate mpt */

  stat = THH_cmd_SYNC_TPT(mrwm->cmd_if);
  if ((stat != THH_CMD_STAT_OK) && (stat != THH_CMD_STAT_EINTR)) {
      MTL_ERROR1(MT_FLFMT("Fatal error: Command SYNC_TPT failed"));
  }

    /* MOSAL_io_unmap for MTTs+MPT */
  MOSAL_io_unmap(fmr_info_p->mpt_entry);
  MOSAL_io_unmap(fmr_info_p->mtt_entries);
  
  /* Return MTTs to extbuddy and MPT to epool */
  MOSAL_mutex_acq_ui(&mrwm->extbuddy_lock);
  if (!extbuddy_free(mrwm->xbuddy_tpt, fmr_info_p->seg_start,fmr_info_p->log2_segs)) {
    MTL_ERROR4(MT_FLFMT(
      "extbuddy_free failed for %d MTT segments from segment %d - resource leak !"),
      fmr_info_p->seg_start,fmr_info_p->log2_segs);  /* continue anyway */
  	}
  EXTBUDDY_FREE_MTT(fmr_info_p->seg_start,fmr_info_p->log2_segs);
  MOSAL_mutex_rel(&mrwm->extbuddy_lock);
  release_mtt_segs(mrwm,(1 << fmr_info_p->log2_segs) - 1);
  
  VIP_array_erase_done(mrwm->mpt[MPT_ext],fmr_hndl,NULL);
  /* zero the fmr_bit in the array */
  {
      u_int8_t  offset_in_cell = fmr_hndl & 0x7;
      mrwm->is_fmr_bits[fmr_hndl>>3]&= ~(((u_int8_t)1) << offset_in_cell);
  }
  
  release_mpt_entry(mrwm,MPT_ext);
  FREE(fmr_info_p);
  return ((stat ==THH_CMD_STAT_OK) ? HH_OK : HH_EFATAL);
}

/************************************************************************/
/* Assumed to be the first called in this module, single thread.        */
void  THH_mrwm_init(void)
{
  native_page_shift    = MOSAL_SYS_PAGE_SHIFT;
  MTL_DEBUG4(MT_FLFMT("native_page: shift=%d"), native_page_shift);
} /* THH_mrwm_init */


HH_ret_t THH_mrwm_get_num_objs(THH_mrwm_t mrwm,u_int32_t *num_mr_int_p, 
                                u_int32_t *num_mr_ext_p,u_int32_t *num_mws_p )
{
  /* check attributes */
  if ( mrwm == NULL || mrwm->mpt[MPT_int] == NULL ||
       mrwm->mpt[MPT_ext] == NULL || mrwm->mpt[MPT_win] == NULL) {
    return HH_EINVAL;
  }
  
  if (num_mr_int_p == NULL && num_mr_ext_p == NULL && num_mws_p == NULL) {
      return HH_EINVAL;
  }

  if (num_mr_int_p) {
      *num_mr_int_p = VIP_array_get_num_of_objects(mrwm->mpt[MPT_int]);
  }
  if (num_mr_ext_p) {
      *num_mr_ext_p = VIP_array_get_num_of_objects(mrwm->mpt[MPT_ext]);
  }
  if (num_mws_p) {
      *num_mws_p = VIP_array_get_num_of_objects(mrwm->mpt[MPT_win]);
  }
  return HH_OK;
}

#if defined(MT_SUSPEND_QP)
HH_ret_t  THH_mrwm_suspend_internal(
  THH_mrwm_t    mrwm,         /* IN */
  VAPI_lkey_t   lkey,         /* IN */
  MT_bool       suspend_flag  /* IN */
)
{
  VIP_common_ret_t  vip_rc;
  u_int32_t         mpt_index = lkey & ((1ul << mrwm->props.log2_mpt_sz) - 1);
  mpt_segment_t     mpt_seg;
  VIP_array_obj_t   vip_obj;
  Mr_sw_t           *mrsw_p;
  THH_cmd_status_t  cmd_st;
  MOSAL_iobuf_props_t iobuf_props = {0};
  call_result_t     mosal_rc;
  HH_ret_t          rc = HH_OK;       

  MTL_TRACE1(MT_FLFMT("{%s: L_key=0x%x, suspend_flag=%s"),
              __func__, lkey, (suspend_flag==TRUE)?"TRUE":"FALSE");

  mpt_seg= get_mpt_seg(mrwm,mpt_index);
  if (mpt_seg != MPT_int){
    MTL_ERROR4(MT_FLFMT("%s: Invalid L-key (0x%X) for internal memory region"),__func__,lkey);
    return HH_EINVAL;
  }

  vip_rc= VIP_array_find_hold(mrwm->mpt[mpt_seg],mpt_index-mrwm->offset[mpt_seg],&vip_obj);
  if (vip_rc != VIP_OK) {
    MTL_ERROR4(MT_FLFMT("%s: Failed finding internal memory region with L-key 0x%X (%s)"),__func__,
               lkey,VAPI_strerror_sym(vip_rc));
    return (HH_ret_t)vip_rc;
  }
  mrsw_p= (Mr_sw_t*)vip_obj;

  if (suspend_flag == TRUE) {
      if (mrsw_p->is_suspended == TRUE) {
          MTL_DEBUG2(MT_FLFMT("%s: internal memory region with L-key 0x%x already suspended"),
                     __func__,lkey);
          rc = HH_EAGAIN;
          goto suspend_hold;
      }
      if (mrsw_p->iobuf == NULL) {
          MTL_ERROR1(MT_FLFMT("%s: suspending intl_reg with L-key 0x%X. IOBUF is NULL!!"),
                     __func__,lkey);
          rc = HH_ERR;
          goto suspend_hold;
      }

      mrsw_p->mpt_entry = TMALLOC(THH_mpt_entry_t);
      if (mrsw_p->mpt_entry == NULL) {
          MTL_ERROR1(MT_FLFMT("%s: Could not malloc mem for saving mpt_entry for internal reg L-key 0x%X"),
                     __func__,lkey);
          rc = HH_EAGAIN;
          goto suspend_hold;
      }
      /* change MPT entry to SW ownership to disable it, and save the mpt entry for restoring later */
      cmd_st = THH_cmd_HW2SW_MPT(mrwm->cmd_if, mpt_index, mrsw_p->mpt_entry);
      if (cmd_st != THH_CMD_STAT_OK) {
          MTL_ERROR1(MT_FLFMT("%s: THH_cmd_HW2SW_MPT returned %d for internal reg L-key 0x%X"),
                     __func__,cmd_st, lkey);
          rc = HH_ERR;
          goto suspend_malloc;
      }
      /* deregister the iobuf */
      MOSAL_iobuf_get_props(mrsw_p->iobuf, &iobuf_props);
      mrsw_p->prot_ctx = iobuf_props.prot_ctx;
      mrsw_p->va       = iobuf_props.va;
      mrsw_p->size     = iobuf_props.size;

      MOSAL_iobuf_deregister(mrsw_p->iobuf);
      mrsw_p->iobuf= NULL;
      mrsw_p->is_suspended = TRUE;
  } else {
      /* unsuspending */
      /* reregister the iobuf */
      if (mrsw_p->is_suspended == FALSE) {
          MTL_ERROR1(MT_FLFMT("%s: unsuspend request. internel region is not suspended"), __func__);
          rc = HH_ERR;
          goto unsuspend_hold;
      }
      mosal_rc = MOSAL_iobuf_register( mrsw_p->va, mrsw_p->size, mrsw_p->prot_ctx, 
                                      MOSAL_PERM_READ | MOSAL_PERM_WRITE, &mrsw_p->iobuf);
      if (mosal_rc != MT_OK) {
        MTL_ERROR1(MT_FLFMT("%s: unsuspend. MOSAL_iobuf_register: rc=%s"), __func__, mtl_strerror_sym(mosal_rc));
        rc = (mosal_rc == MT_EAGAIN) ? HH_EAGAIN : HH_EINVAL_VA;
        goto unsuspend_hold;
      }

      /* get properties of the iobuf just obtained, to get n_pages. */
      mosal_rc = MOSAL_iobuf_get_props(mrsw_p->iobuf, &iobuf_props);
      if (mosal_rc != MT_OK) {
        MTL_ERROR1(MT_FLFMT("%s: unsuspend. MOSAL_iobuf_get_props: rc=%s"), __func__, mtl_strerror_sym(mosal_rc));
        rc = HH_ERR;
        goto unsuspend_iobuf;
      }
      
      /* write the MTT entry with the page translation table*/
      rc = mtt_writes_iobuf(mrwm->cmd_if, 
                            mrsw_p->iobuf, 
                            (VAPI_phy_addr_t)mrsw_p->mpt_entry->mtt_seg_adr,
                            iobuf_props.nr_pages);
      if (rc != HH_OK) {
          MTL_ERROR1(MT_FLFMT("%s: unsuspend. mtt_writes_iobuf failed (%d: %s)"),__func__,
                     rc,HH_strerror_sym(rc));
          goto unsuspend_iobuf;
      }

      /* re-activate the MPT entry */
      cmd_st = THH_cmd_SW2HW_MPT(mrwm->cmd_if, mpt_index, mrsw_p->mpt_entry);
      if (cmd_st != THH_CMD_STAT_OK) {
          MTL_ERROR1(MT_FLFMT("%s: THH_cmd_SW2HW_MPT returned %d for internal reg L-key 0x%X"),
                     __func__,cmd_st, lkey);
          rc = HH_ERR;
          goto unsuspend_iobuf;
      }

      /* clean-up */
      FREE(mrsw_p->mpt_entry);
      mrsw_p->mpt_entry= NULL;
      mrsw_p->is_suspended = FALSE;
  }
  
  VIP_array_find_release(mrwm->mpt[mpt_seg],mpt_index-mrwm->offset[mpt_seg]);
  MTL_TRACE1("}THH_mrwm_suspend_mr\n");
  return HH_OK;

suspend_malloc:
  FREE(mrsw_p->mpt_entry);
  mrsw_p->mpt_entry = NULL;

suspend_hold:
  VIP_array_find_release(mrwm->mpt[mpt_seg],mpt_index-mrwm->offset[mpt_seg]);
  return rc;

unsuspend_iobuf:
      MOSAL_iobuf_deregister(mrsw_p->iobuf);
      mrsw_p->iobuf= NULL;
unsuspend_hold:
  VIP_array_find_release(mrwm->mpt[mpt_seg],mpt_index-mrwm->offset[mpt_seg]);
  return rc;
    

} /* THH_mrwm_query_mr */
#endif
