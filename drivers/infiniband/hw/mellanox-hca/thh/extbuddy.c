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

#include "extbuddy.h"
#include <vip_hashp.h>
#include <tlog2.h>

typedef struct Extbuddy_st
{
  u_int8_t       log2_min;
  u_int8_t       log2_max;
  VIP_hashp_p_t  freelist[32];
  u_int32_t      available_size;
} Extbuddy_t;

typedef struct
{
  unsigned int  curr_size;
  unsigned int  ptrs_buff_sz;
  u_int32_t*    ptrs_buff;
} Query_Filler;


typedef struct
{
  u_int32_t     boundary_begin;
  u_int32_t     boundary_end;
  u_int32_t     target_size;
  u_int32_t*    pp;           /* address of desired pointer to return*/
  u_int32_t     chunk_start;
  u_int8_t      chunk_log2sz;
  u_int32_t     chunk_size;   /* 1ul << chunk_log2sz  --- cahced */
} Bound_Ptr;


/* The following structure serves the reserve routines callbacks.
 * It is designed for simple cached values rather than compactness.
 */
typedef struct
{
  Extbuddy_t*    xbdy;
  u_int32_t      res_begin;
  u_int32_t      res_size;
  u_int32_t      res_end;          /* = begin + size */
  u_int32_t      total_intersection;   /* should reach size */
  unsigned long  n_segments[32];
  unsigned long  n_segments_total;
  u_int32_t*     currseg;
  u_int32_t*     currseg_end;
  int8_t         curr_log2sz;
} Reserve_Segments;



/************************************************************************/
/************************************************************************/
/*                         private functions                            */


/************************************************************************/
/*   $$ [a_0,a_1) \cap [b_0,b_1) $$    %% (TeX format)                  */
/*   Note the half open segments:  $ [m,n) = [m, n-1] $                 */
static MT_bool  intersects(
  u_int32_t a0, u_int32_t a1,
  u_int32_t b0, u_int32_t b1
)
{
  return ((b0 < a1) && (a0 < b1));
} /* intersects */


/************************************************************************/
static u_int32_t  intersection_size(
  u_int32_t a0, u_int32_t a1,
  u_int32_t b0, u_int32_t b1
)
{
  u_int32_t  x = (intersects(a0, a1, b0, b1)
    ? (a1 < b1 ? a1 : b1) - (a0 < b0 ? b0 : a0)  /* min(right) - max(left) */
    : 0);
  return x;
} /* intersection_size */


/************************************************************************/
/* Fix given log2_sz to minimal size                                    */
static  u_int8_t  fix_log2(Extbuddy_t* xbdy, u_int8_t* log2_sz_p)
{
  if (*log2_sz_p < xbdy->log2_min)
  {
    *log2_sz_p = xbdy->log2_min;
  }
  return *log2_sz_p;
} /* fix_log2 */


/************************************************************************/
static MT_bool  create_lists(Extbuddy_t* xbdy)
{
  MT_bool  ok = TRUE;
  int  clg2;
  for (clg2 = xbdy->log2_min;  ok && (clg2 <= xbdy->log2_max);  ++clg2)
  {
    if (VIP_hashp_create(0, &(xbdy->freelist[clg2])) != VIP_OK)
    {
      ok = FALSE;
    }
  }
  return ok;
} /* create_lists */


/************************************************************************/
static void  destroy_lists(Extbuddy_t* xbdy)
{
  int  clg2;
  for (clg2 = xbdy->log2_min;  clg2 <= xbdy->log2_max;  ++clg2)
  {
    VIP_hashp_p_t  ph = xbdy->freelist[clg2];
    if (ph)
    {
      (void)VIP_hashp_destroy(ph, 0, 0);
       xbdy->freelist[clg2] = NULL;
    }
  }
} /* destroy_lists */


/************************************************************************/
static MT_bool  init_lists(Extbuddy_t* xbdy)
{
  MT_bool  ok = create_lists(xbdy);
  MTL_DEBUG4(MT_FLFMT("ok=%d"), ok);
  if (ok)
  {
    int        chunk = xbdy->log2_max;
    u_int32_t  chunk_size = 1ul << chunk;
    u_int32_t  offset = 0, next = offset + chunk_size;
    while (ok && (next <= xbdy->available_size) && (chunk > xbdy->log2_min))
    {
      //MTL_DEBUG4(MT_FLFMT("chunk=%d, offset=0x%x"), chunk, offset);
      ok = (VIP_hashp_insert(xbdy->freelist[chunk], offset, 0) == VIP_OK);
      offset = next;
      do
      {
        --chunk;
        chunk_size >>= 1;
        next = offset + chunk_size;
      } while ((next > xbdy->available_size) && (chunk > xbdy->log2_min));
    }
    /* sub divide the rest to the segments of minimum chunks */
    while (ok && (next <= xbdy->available_size))
    {
      offset = next;
      //MTL_DEBUG4(MT_FLFMT("chunk=%d, offset=0x%x"), chunk, offset);
      ok = (VIP_hashp_insert(xbdy->freelist[chunk], offset, 0) == VIP_OK);
      next += chunk_size;
    }
  }
  if (!ok)
  {
    destroy_lists(xbdy);
  }
  return ok;
} /* init_lists */


/************************************************************************/
static int  get1p(VIP_hashp_key_t key, VIP_hashp_value_t val, void* vp)
{
  u_int32_t   p = key;
  u_int32_t*  pp = (u_int32_t*)vp;
  *pp = p;
  return 0; /* just one call, and stop traverse */
} /* get1p */


/************************************************************************/
static int  get1p_bound(VIP_hashp_key_t key, VIP_hashp_value_t val, void* vp)
{
  u_int32_t   p = key;
  Bound_Ptr*  bp = (Bound_Ptr*)vp;
  u_int32_t   xsize = intersection_size(bp->boundary_begin, bp->boundary_end,
                                        p, p + bp->chunk_size);
  int         found = (bp->target_size <= xsize);
  if (found)
  {
    *bp->pp = (p < bp->boundary_begin ? bp->boundary_begin : p);
    bp->chunk_start = p;
  }
  return !found;
} /* get1p_bound */


/************************************************************************/
static int  query_fill(VIP_hashp_key_t key, VIP_hashp_value_t val, void* vp)
{
  u_int32_t     p = key;
  Query_Filler* filler_p = (Query_Filler*)vp;
  int           go_on = (filler_p->curr_size < filler_p->ptrs_buff_sz);
  if (go_on)
  {
    filler_p->ptrs_buff[filler_p->curr_size++] = p;
    /* go_on = (filler_p->curr_size < filler_p->ptrs_buff_sz);, no real need */
  }
  return go_on;
} /* query_fill */


/************************************************************************/
/* Reserve related functions                                            */


/************************************************************************/
static int  reserve_pass1(VIP_hashp_key_t key, VIP_hashp_value_t val, void* vp)
{
  MT_bool            more;
  u_int32_t          p = key;
  Reserve_Segments*  rs = (Reserve_Segments*)vp;
  u_int8_t           log2sz = rs->curr_log2sz;
  u_int32_t          xsize = intersection_size(rs->res_begin, rs->res_end,
                                               p, p + (1ul << log2sz));
  if (xsize != 0)
  {
    rs->total_intersection += xsize;
    ++rs->n_segments_total;
    ++rs->n_segments[log2sz];
  }
  more = (rs->total_intersection < rs->res_size);
  return more;
} /* reserve_pass1 */


/************************************************************************/
static int  reserve_pass2(VIP_hashp_key_t key, VIP_hashp_value_t val, void* vp)
{
  MT_bool            more;
  u_int32_t          p = key;
  Reserve_Segments*  rs = (Reserve_Segments*)vp;
  u_int8_t           log2sz = rs->curr_log2sz;
  if (intersects(rs->res_begin, rs->res_end, p, p + (1ul << log2sz)))
  {
    *rs->currseg++ = p;
  }
  more = (rs->currseg != rs->currseg_end);
  return more;
} /* reserve_pass2 */


/************************************************************************/
/* Recursive. Maximal depth is by log2sz                                */
static MT_bool  reserve_breaks_insert(
  Reserve_Segments* rs, 
  u_int32_t         p, 
  u_int8_t          log2sz
)
{
  MT_bool  ok = TRUE;
  MTL_DEBUG4(MT_FLFMT("{breaks_insert: p=0x%x, log2sz=%d"), p, log2sz);
  if (log2sz > rs->xbdy->log2_min)
  {
    unsigned int  i2 = 0;
    u_int32_t     half = (1ul << --log2sz);
    for (i2 = 0;  i2 != 2;  ++i2, p += half)
    {
      u_int32_t  xsize = intersection_size(rs->res_begin, rs->res_end, 
                                           p, p + half);
     // MTL_DEBUG4(MT_FLFMT("p=0x%x, xsize=0x%x"), p, xsize);
      if (xsize == 0) /* we can use the whole half */
      {
        VIP_common_ret_t  
          vrc = VIP_hashp_insert(rs->xbdy->freelist[log2sz], p, 0);
        ok = (vrc == VIP_OK);
        if (ok)
        {
          rs->xbdy->available_size += 1ul << log2sz;
        }
      }
      else
      {
        if (xsize != half) /* we can use part of the half */
        {
          ok = reserve_breaks_insert(rs, p, log2sz) && ok; /* recursive */
          //MTL_DEBUG4(MT_FLFMT("ok=%d"), ok);
        }
      }
    }
  }  
  MTL_DEBUG4(MT_FLFMT("}breaks_insert ok=%d"), (int)ok);
  return ok;
} /* reserve_breaks_insert */


/************************************************************************/
static MT_bool  reserve_break(Reserve_Segments* rs)
{
  MT_bool    ok = TRUE;
  u_int32_t  p = *rs->currseg;
  u_int8_t   log2sz = rs->curr_log2sz;
  ok = (VIP_hashp_erase(rs->xbdy->freelist[log2sz], p, 0) == VIP_OK);
  if (ok)
  {
     rs->xbdy->available_size -= 1ul << log2sz;
     ok = reserve_breaks_insert(rs, p, log2sz);
  }
  return ok;
} /* reserve_break */


/************************************************************************/
/************************************************************************/
/*                         interface functions                          */


/************************************************************************/
Extbuddy_hndl  extbuddy_create(u_int32_t size,  u_int8_t log2_min_chunk)
{
  u_int8_t     log2_max = floor_log2(size);
  Extbuddy_t*  xbdy = NULL;
  if (log2_min_chunk <= log2_max)
  {
     xbdy = TMALLOC(Extbuddy_t);
  }
  if (xbdy)
  {
    u_int32_t  mask = (1ul << log2_min_chunk) - 1;
    memset(xbdy, 0, sizeof(Extbuddy_t)); /* in particular, null hash lists */
    xbdy->log2_min = log2_min_chunk;
    xbdy->log2_max = log2_max;
    xbdy->available_size = size & ~mask;
    MTL_DEBUG4(MT_FLFMT("log2_min=%u, log2_max=%u, avail_size=%u"),
               xbdy->log2_min,  xbdy->log2_max,xbdy->available_size);
    if (!init_lists(xbdy))
    {
      FREE(xbdy);
      xbdy = NULL;
    }
  }
  return  xbdy;
} /* extbuddy_create */


/************************************************************************/
void  extbuddy_destroy(Extbuddy_hndl xbdy)
{
  destroy_lists(xbdy);
  FREE(xbdy);
} /* extbuddy_destroy */


/************************************************************************/
u_int32_t  extbuddy_alloc(Extbuddy_hndl xbdy, u_int8_t log2_sz)
{
  u_int32_t  p = EXTBUDDY_NULL;
  u_int32_t  log2_max = xbdy->log2_max;
  u_int8_t   sz = fix_log2(xbdy, &log2_sz);
  for ( ;  (sz <= log2_max) && !extbuddy_chunks_available(xbdy, sz); ++sz);
  if (sz <= xbdy->log2_max) /* we found a sufficient chunk */
  {
    u_int8_t  split_sz = log2_sz;
    VIP_hashp_traverse(xbdy->freelist[sz], &get1p, &p);
    VIP_hashp_erase(xbdy->freelist[sz], p, 0); /* can't fail */

    /* If bigger than we need, we split chunk of 2^{split_sz} by halves,
     * inserting chunks to free lists.
     */
    while (split_sz != sz)
    {
      u_int32_t  split = p + (1ul << split_sz);
      if (VIP_hashp_insert(xbdy->freelist[split_sz], split, 0) != VIP_OK)
      {
        xbdy->available_size -= (1ul << split_sz);  /* :)bad:(. */
      }
      ++split_sz;
    }
    xbdy->available_size -= (1ul << log2_sz);
  }
  return p;
} /* extbuddy_alloc */


/************************************************************************/
u_int32_t  extbuddy_alloc_bound(
  Extbuddy_t*   xbdy,
  u_int8_t      log2_sz,
  u_int32_t     area_start,
  u_int32_t     area_size
)
{
  u_int32_t  p = EXTBUDDY_NULL;
  u_int32_t  log2_max = xbdy->log2_max;
  u_int8_t   sz = fix_log2(xbdy, &log2_sz);
  u_int32_t  target_size = (1ul << log2_sz);
  Bound_Ptr  bp;

  // Align the boundary beginning to the target size */
  bp.boundary_begin = area_start & ~(target_size - 1);
  if (bp.boundary_begin != area_start) { bp.boundary_begin += target_size; }
  bp.boundary_end   = area_start + area_size;
  bp.target_size    = 1ul << log2_sz;
  bp.pp    = &p;

  for (;  (p == EXTBUDDY_NULL) && (sz <= log2_max);  ++sz)
  {
    if (extbuddy_chunks_available(xbdy, sz) != 0)
    {
      bp.chunk_log2sz = sz;
      bp.chunk_size   = 1ul << sz;
      VIP_hashp_traverse(xbdy->freelist[sz], &get1p_bound, &bp);
      if (p != EXTBUDDY_NULL)
      {
        u_int32_t  buddy = p;
        u_int8_t   split_sz = log2_sz;
        VIP_hashp_erase(xbdy->freelist[sz], bp.chunk_start, 0); /* can't fail */

        /* If bigger than we need, we split chunk of 2^{split_sz} by halves,
         * inserting chunks to free lists. This is more complicated than
         * the above (non bound) extbuddy_alloc(...), since we keep
         * the returned p, which is not necessarily chunk_start.
         * So we use the buddy trick again.
         */
        while (split_sz != sz)
        {
          u_int32_t  buddy_bit = 1ul << split_sz;
          buddy ^= buddy_bit;
          if (VIP_hashp_insert(xbdy->freelist[split_sz], buddy, 0) != VIP_OK)
          {
            xbdy->available_size -= (1ul << split_sz);  /* :)bad:(. */
          }
          buddy &= ~buddy_bit;
          ++split_sz;
        }
        xbdy->available_size -= (1ul << log2_sz);
      }
    }
  }
  return p;
} /* extbuddy_alloc_bound */


/************************************************************************/
/* p is assumed to be alligned to 1<<log2sz                            */
MT_bool  extbuddy_free(Extbuddy_t* xbdy, u_int32_t p, u_int8_t log2sz)
{
  MT_bool    ok = TRUE;
  u_int8_t   slot = fix_log2(xbdy, &log2sz);
  u_int32_t  buddy = p ^ (1ul << slot); /* it is buddy system after all */
  if (slot > xbdy->log2_max) {
      MTL_ERROR1(MT_FLFMT("extbuddy_free: slot too large: %d (max is %d"), slot, xbdy->log2_max);
      return FALSE;
  }
  while ((slot <= xbdy->log2_max) &&
         VIP_hashp_erase(xbdy->freelist[slot], buddy, 0) == VIP_OK)
  {
    p &= ~(1ul << slot); /* unite with buddy */
    ++slot;
    buddy = p ^ (1ul << slot);
  }
  ok = (VIP_hashp_insert(xbdy->freelist[slot], p, 0) == VIP_OK);
  if (ok)
  {
    xbdy->available_size += (1ul << log2sz);
  }
  return ok;
} /* extbuddy_free */


/************************************************************************/
unsigned int  extbuddy_chunks_available(Extbuddy_t* xbdy, u_int8_t log2sz)
{
  unsigned int  n = 0;
  if (xbdy->log2_min <= log2sz && log2sz <= xbdy->log2_max)
  {
    n = VIP_hashp_get_num_of_objects(xbdy->freelist[log2sz]);
  }
  return n;
} /* extbuddy_chunks_available */


/************************************************************************/
u_int32_t  extbuddy_total_available(Extbuddy_t* xbdy)
{
  return xbdy->available_size;
} /* extbuddy_total_available */


/************************************************************************/
int  extbuddy_log2_max_available(Extbuddy_t* xbdy)
{
  int       log2max = -1;
  int  chunk = xbdy->log2_max + 1;
  while ((log2max == -1) && (--chunk >= (int)xbdy->log2_min))
  {
    if (VIP_hashp_get_num_of_objects(xbdy->freelist[chunk]) > 0)
    {
      log2max = chunk;
    }
  }
  return log2max;
} /* extbuddy_log2_max_available */


/************************************************************************/
void  extbuddy_query_chunks(
  Extbuddy_t*   xbdy,
  u_int8_t      log2sz,
  unsigned int  ptrs_buff_sz,
  u_int32_t*    ptrs_buff
)
{
  Query_Filler  filler;
  filler.curr_size    = 0;
  filler.ptrs_buff_sz = ptrs_buff_sz;
  filler.ptrs_buff    = ptrs_buff;
  VIP_hashp_traverse(xbdy->freelist[log2sz], &query_fill, &filler);
} /* extbuddy_query_chunks */


/************************************************************************/
/*  We go through the free lists, break free segments that intersect the
 *  reserved area and insert the non reserved sub-segments into smaller
 *  chunk lists.
 *
 *  Three passes:
 *  1. See if reservation is possible and count the number of
 *     free segments involved.
 *  2. Collect the involved segments.
 *  3. Go thru the involved segments.
 *     Break each segment by calling a recursive function.
 *
 *  Note that the first two passes traverse the underlying hash tables.
 *  These tables are not modified until the third pass.
 */
MT_bool  extbuddy_reserve(Extbuddy_t* xbdy, u_int32_t p, u_int32_t size)
{
  MT_bool           ok = TRUE;
  Reserve_Segments  rs;
  u_int32_t*        segs = NULL;
  u_int8_t          log2_min_used = xbdy->log2_max;

  memset(&rs, 0, sizeof(rs)); /* clumsy but simple initialization */
  rs.xbdy      = xbdy;
  rs.res_begin = p;
  rs.res_size  = size;
  rs.res_end   = p + size;

  /* 1st pass */
  MTL_DEBUG4(MT_FLFMT("1st pass"));
  for (rs.curr_log2sz = xbdy->log2_max;
       (rs.curr_log2sz >= xbdy->log2_min) && (rs.total_intersection < size);
       --rs.curr_log2sz)
  {
    VIP_hashp_traverse(xbdy->freelist[rs.curr_log2sz], &reserve_pass1, &rs);
    if (rs.n_segments[rs.curr_log2sz])
    {
      log2_min_used = rs.curr_log2sz;
    }
  }


  ok = (rs.total_intersection == size);
  segs = (ok ? TNVMALLOC(u_int32_t, rs.n_segments_total) : NULL);
  ok = (segs != NULL);

  if (ok)
  {
    /* 2nd pass */
    MTL_DEBUG4(MT_FLFMT("2nd pass"));
    rs.currseg = segs;
    for (rs.curr_log2sz = xbdy->log2_max; rs.curr_log2sz >= log2_min_used;
         --rs.curr_log2sz)
    {
      if (rs.n_segments[rs.curr_log2sz])
      {
        rs.currseg_end = rs.currseg + rs.n_segments[rs.curr_log2sz];
        VIP_hashp_traverse(xbdy->freelist[rs.curr_log2sz], &reserve_pass2, &rs);
      }
    }

    /* 3rd pass */
    MTL_DEBUG4(MT_FLFMT("3rd pass"));
    rs.currseg = segs;
    for (rs.curr_log2sz = xbdy->log2_max; rs.curr_log2sz >= log2_min_used;
         --rs.curr_log2sz)
    {
      //MTL_DEBUG4(MT_FLFMT("curr_log2sz=%d"), rs.curr_log2sz);
      rs.currseg_end = rs.currseg + rs.n_segments[rs.curr_log2sz];
      for (;  rs.currseg != rs.currseg_end;  ++rs.currseg)
      {
        ok = reserve_break(&rs) && ok;
      }
    }
  }

  if (segs) { VFREE(segs); }
  return ok;
} /* extbuddy_reserve */
