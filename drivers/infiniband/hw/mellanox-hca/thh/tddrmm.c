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

#include <tddrmm.h>
#include <mtl_common.h>
#include <tlog2.h>
#include <extbuddy.h>
#include <mosal.h>


#define ELSE_ACQ_ERROR(f) else { MTL_ERROR1("%s MOSAL_mutex_acq failed\n", f); }
#define logIfErr(f) \
  if (rc != HH_OK) { MTL_ERROR1("%s: rc=%s\n", f, HH_strerror_sym(rc)); }


typedef struct THH_ddrmm_st
{
  MT_phys_addr_t    mem_base;
  MT_size_t      mem_sz;
  Extbuddy_hndl  xb;
  MOSAL_mutex_t  mtx; /* protect xb */
} DDRMM_t;


/************************************************************************/
/************************************************************************/
/*                         private functions                            */

/************************************************************************/
static int  well_alligned(MT_phys_addr_t mem_base, MT_size_t  mem_sz)
{
  unsigned int  lg2sz = ceil_log2(mem_sz);
  MT_phys_addr_t   mask = (1ul << lg2sz) - 1;
  MT_phys_addr_t   residue = mem_base & mask;
  return (residue == 0);
} /* well_alligned  */


/************************************************************************/
static void  lookup_sort(
  MT_size_t         n,
  const MT_size_t*  sizes,
  MT_size_t*        lut
)
{
  unsigned int   i, i1, j;
  for (i = 0;  i != n;   lut[i] = i, ++i); /* identity init */
  for (i = 0, i1 = 1; i1 != n;  i = i1++)  /* small n, so Bubble sort O(n^2) */
  {
    MT_size_t  luiMax = i;
    MT_size_t  iMax   = sizes[ lut[i] ];
    for (j = i1;  j != n;  ++j)
    {
      MT_size_t v = sizes[ lut[j] ];
      if (iMax < v)
      {
        iMax = v;
        luiMax = j;
      }
    }
    j = (unsigned int)lut[i];  lut[i] = lut[luiMax];  lut[luiMax] = j; /* swap */
  }
} /* lookup_sort */


/************************************************************************/
/************************************************************************/
/*                         interface functions                          */


/************************************************************************/
HH_ret_t  THH_ddrmm_create(
  MT_phys_addr_t  mem_base,  /* IN  */
  MT_size_t    mem_sz,    /* IN  */
  THH_ddrmm_t* ddrmm_p    /* OUT */
)
{
  HH_ret_t       rc = HH_OK;
  Extbuddy_hndl  xb = NULL;
  DDRMM_t*       mm = NULL;

  MTL_TRACE1("{THH_ddrmm_create: base="U64_FMT", sz="SIZE_T_FMT"\n", 
             (u_int64_t)mem_base, mem_sz);
  if (!well_alligned(mem_base, mem_sz))
  {
    rc = HH_EINVAL;
  }
  else
  {
    xb = extbuddy_create((u_int32_t) mem_sz, 0);
    mm = (xb ? TMALLOC(DDRMM_t) : NULL);
  }
  if (mm == NULL)
  {
    rc = HH_EAGAIN;
  }
  else
  {
    mm->mem_base = mem_base;
    mm->mem_sz   = mem_sz;
    mm->xb       = xb;
    MOSAL_mutex_init(&mm->mtx);
    *ddrmm_p = mm;
  }
  MTL_TRACE1("}THH_ddrmm_create: ddrmm=%p\n", *ddrmm_p);
//  logIfErr("THH_ddrmm_create")
  return rc;
} /* THH_ddrmm_create */


/************************************************************************/
HH_ret_t  THH_ddrmm_destroy(THH_ddrmm_t ddrmm)
{
  HH_ret_t  rc = HH_OK;
  MTL_TRACE1("{THH_ddrmm_destroy: ddrmm=%p\n", ddrmm);
  extbuddy_destroy(ddrmm->xb);
  MOSAL_mutex_free(&ddrmm->mtx);  
  FREE(ddrmm);
  MTL_TRACE1("}THH_ddrmm_destroy\n");
  logIfErr("THH_ddrmm_destroy")
  return rc;
} /* THH_ddrmm_destroy */



/************************************************************************/
HH_ret_t  THH_ddrmm_reserve (
  THH_ddrmm_t  ddrmm,    /* IN  */
  MT_phys_addr_t  addr,     /* IN  */
  MT_size_t    size      /* IN  */
)
{
  MT_bool   ok = FALSE;
  HH_ret_t  rc;
  
  MTL_TRACE1("{THH_ddrmm_reserve: ddrmm=%p, addr="U64_FMT", size="SIZE_T_FMT"\n",
             ddrmm, (u_int64_t)addr, size);
  MOSAL_mutex_acq_ui(&ddrmm->mtx);
  MTL_DEBUG4(MT_FLFMT("rel="U64_FMT", size="SIZE_T_FMT""), 
               (u_int64_t)(addr - ddrmm->mem_base), size);
  ok = extbuddy_reserve(ddrmm->xb, (u_int32_t)(addr - ddrmm->mem_base), (u_int32_t)size);
  MOSAL_mutex_rel(&ddrmm->mtx);
  rc = (ok ? HH_OK : HH_EINVAL);
  MTL_TRACE1("}THH_ddrmm_reserve\n");
  logIfErr("THH_ddrmm_reserve")
  return rc;
} /* THH_ddrmm_reserve */


/************************************************************************/
/* Note: For chunks in the array of size THH_DDRMM_INVALID_SZ, no allocation is made */
HH_ret_t  THH_ddrmm_alloc_sz_aligned(
  THH_ddrmm_t  ddrmm,             /* IN  */
  MT_size_t    num_o_chunks,      /* IN  */
  MT_size_t*   chunks_log2_sizes, /* IN  */
  MT_phys_addr_t* chunks_addrs       /* OUT */
)
{
  HH_ret_t    rc = HH_EAGAIN;
  MT_size_t*  slut; /* Sorted Look-Up Table - (:politely incorrect:) */

  MTL_TRACE1("{THH_ddrmm_alloc_sz_aligned: ddrmm=%p, n="SIZE_T_FMT"\n", 
             ddrmm, num_o_chunks);
  slut  = TNMALLOC(MT_size_t, num_o_chunks); /* small, so not VMALLOC */
  if (slut)
  {
    MT_size_t  i;
    rc = HH_OK;
    lookup_sort(num_o_chunks, chunks_log2_sizes, slut);
    for (i = 0;  (i != num_o_chunks) && (rc == HH_OK);  ++i)
    {
      MT_size_t  si = slut[i];
      u_int8_t   log2sz = (u_int8_t)chunks_log2_sizes[si];
      if (log2sz != (u_int8_t)THH_DDRMM_INVALID_SZ)  {
        rc = THH_ddrmm_alloc(ddrmm, 1ul << log2sz, log2sz, &chunks_addrs[si]);
      } else {
        chunks_addrs[si]= THH_DDRMM_INVALID_PHYS_ADDR;
        /* No allocation if given log size is zero (workaround struct design in ddr_alloc_size_vec)*/
      }
    }
    if (rc != HH_OK)
    { /* Backwards. Note that we avoid (MT_size_t)-1 > 0 */
      while (i-- > 0)
      { /* Now i >= 0 */
         MT_size_t  si = slut[i];
         u_int8_t   log2sz = (u_int8_t)chunks_log2_sizes[si];
         if (log2sz != (u_int8_t)THH_DDRMM_INVALID_SZ) 
           THH_ddrmm_free(ddrmm, chunks_addrs[si], 1ul << log2sz);
      }
    }
    FREE(slut);
  }
  MTL_TRACE1("}THH_ddrmm_alloc_sz_aligned\n");
  logIfErr("THH_ddrmm_alloc_sz_aligned")
  return rc;
} /* THH_ddrmm_alloc_sz_aligned */



/************************************************************************/
HH_ret_t  THH_ddrmm_alloc(
  THH_ddrmm_t  ddrmm,        /* IN  */
  MT_size_t    size,         /* IN  */
  u_int8_t     align_shift,  /* IN  */
  MT_phys_addr_t* buf_p         /* OUT */
)
{
  HH_ret_t     rc = HH_EINVAL;
  /* internally (extbuddy) we need power-2 size */
  MT_size_t  log2sz = ceil_log2(size);
  MTL_TRACE1("{THH_ddrmm_alloc: ddrmm=%p, sz="SIZE_T_FMT", lg2=%d, shift=%d\n", 
             ddrmm, size, (u_int32_t)log2sz, align_shift);
  if (log2sz >= align_shift)
  {
    u_int32_t  p = EXTBUDDY_NULL;
    MOSAL_mutex_acq_ui(&ddrmm->mtx);
    p = extbuddy_alloc(ddrmm->xb, (u_int8_t)log2sz);
    MOSAL_mutex_rel(&ddrmm->mtx);
    MTL_DEBUG7(MT_FLFMT("log2sz="SIZE_T_FMT", p=0x%x"), log2sz, p);
    rc = HH_EAGAIN;
    if (p != EXTBUDDY_NULL)
    {
      *buf_p = ddrmm->mem_base + p;
      rc     = HH_OK;
    }
  }
  MTL_TRACE1("}THH_ddrmm_alloc: buf="U64_FMT"\n", (u_int64_t)*buf_p);
  if ( rc != HH_OK ) {
     if (rc == HH_EAGAIN) {
         MTL_DEBUG1("%s: rc=%s\n",__func__, HH_strerror_sym(rc));
     } else {
         MTL_ERROR1("%s: rc=%s\n",__func__, HH_strerror_sym(rc));
     }
  }
  return rc;
} /* THH_ddrmm_alloc */


/************************************************************************/
HH_ret_t  THH_ddrmm_alloc_bound(
  THH_ddrmm_t   ddrmm,        /* IN  */
  MT_size_t     size,         /* IN  */
  u_int8_t      align_shift,  /* IN  */
  MT_phys_addr_t   area_start,   /* IN  */
  MT_phys_addr_t   area_size,    /* IN  */
  MT_phys_addr_t*  buf_p         /* OUT */
)
{
  HH_ret_t     rc = HH_EINVAL;
  /* internally (extbuddy) we need power-2 size */
  MT_size_t  log2sz = ceil_log2(size);
  MTL_TRACE1("{THH_ddrmm_alloc_bound: ddrmm=%p, sz="SIZE_T_FMT", shift=%d, "
         "area:{start="U64_FMT", size="U64_FMT"\n", 
         ddrmm, size, align_shift, (u_int64_t)area_start, (u_int64_t)area_size);
  if (log2sz >= align_shift)
  {
    u_int32_t  p = EXTBUDDY_NULL;
    MOSAL_mutex_acq_ui(&ddrmm->mtx);
    p = extbuddy_alloc_bound(ddrmm->xb, (u_int8_t)log2sz, (u_int32_t)area_start, (u_int32_t)area_size);
    MOSAL_mutex_rel(&ddrmm->mtx);
    rc = HH_EAGAIN;
    if (p != EXTBUDDY_NULL)
    {
      *buf_p = ddrmm->mem_base + p;
      rc     = HH_OK;
    }
  }
  MTL_TRACE1("}THH_ddrmm_alloc_bound: buf="U64_FMT"\n", (u_int64_t)*buf_p);
  logIfErr("THH_ddrmm_alloc_bound")
  return rc;
} /* THH_ddrmm_alloc_bound */


/************************************************************************/
HH_ret_t  THH_ddrmm_free(
  THH_ddrmm_t  ddrmm,   /* IN  */
  MT_phys_addr_t  buf,     /* IN */
  MT_size_t    size     /* IN  */
)
{
  HH_ret_t   rc = HH_OK; 
  MT_size_t  log2sz = ceil_log2(size);
  MTL_TRACE1("{THH_ddrmm_free: ddrmm=%p, buf="U64_FMT", sz="SIZE_T_FMT"\n",
             ddrmm, (u_int64_t)buf, size);
  MOSAL_mutex_acq_ui(&ddrmm->mtx);
  extbuddy_free(ddrmm->xb, (u_int32_t)(buf - ddrmm->mem_base), (u_int8_t)log2sz);
  MOSAL_mutex_rel(&ddrmm->mtx);
  MTL_TRACE1("}THH_ddrmm_free\n");
  logIfErr("THH_ddrmm_free")
  return rc;
} /* THH_ddrmm_free */


/************************************************************************/
HH_ret_t  THH_ddrmm_query(
  THH_ddrmm_t   ddrmm,              /* IN  */
  u_int8_t      align_shift,        /* IN  */
  VAPI_size_t*    total_mem,          /* OUT */
  VAPI_size_t*    free_mem,           /* OUT */
  VAPI_size_t*    largest_chunk,      /* OUT */
  VAPI_phy_addr_t*  largest_free_addr_p /* OUT */
)
{
  HH_ret_t   rc = HH_OK;
  int        log2_sz;
  MTL_TRACE1("{THH_ddrmm_query: ddrmm=%p, shift=%d\n", ddrmm, align_shift);
  *total_mem     = ddrmm->mem_sz;
  *free_mem      = extbuddy_total_available(ddrmm->xb);
  log2_sz        = extbuddy_log2_max_available(ddrmm->xb);
  *largest_chunk = 0;
  *largest_free_addr_p = ddrmm->mem_base + ddrmm->mem_sz; /* like null */
  if (log2_sz >= align_shift)
  {
    u_int32_t  p;
    *largest_chunk = 1ul << log2_sz;
    extbuddy_query_chunks(ddrmm->xb, log2_sz, 1, &p);
    *largest_free_addr_p = ddrmm->mem_base + p;
  }
  MTL_TRACE1("}THH_ddrmm_query: total="U64_FMT", free="U64_FMT", lc="U64_FMT", p="U64_FMT"\n",
             *total_mem, *free_mem,         
             *largest_chunk, (VAPI_phy_addr_t)*largest_free_addr_p);
  logIfErr("THH_ddrmm_query")
  return rc;
} /* THH_ddrmm_query */
