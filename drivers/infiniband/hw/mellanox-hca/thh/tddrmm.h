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

#if !defined(_TDDRM__H)
#define _TDDRM__H

#include <mtl_common.h>
#include <vapi_types.h>
#include <mosal.h>
#include <hh.h>
#include <thh.h>


#define THH_DDRMM_INVALID_HANDLE  ((THH_ddrmm_t)0)
#define THH_DDRMM_INVALID_SZ ((MT_size_t)-1)
#define THH_DDRMM_INVALID_PHYS_ADDR ((MT_phys_addr_t)-1)


/************************************************************************
 *  Function: THH_ddrmm_create
 *
 *  Arguments:
 *    mem_base - Physical address base for DDR memory
 *    mem_sz -   Size in bytes of DDR memory 1
 *    ddrmm_p -  Created object handle
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameter
 *    HH_EAGAIN - Not enough resources for creating this object
 *
 *  Description:
 *    Create DDR memory management object context.
 */
extern HH_ret_t  THH_ddrmm_create(
  MT_phys_addr_t  mem_base,  /* IN  */
  MT_size_t    mem_sz,    /* IN  */
  THH_ddrmm_t* ddrmm_p    /* OUT */
);


/************************************************************************
 *  Function: THH_ddrmm_destroy
 *
 *  Arguments:
 *   ddrmm - The handle of the object to destroy
 *
 *  Returns:
 *   HH_OK HH_EINVAL - No such object
 *
 *  Description:
 *   Free given object resources.
 */
extern HH_ret_t  THH_ddrmm_destroy(THH_ddrmm_t ddrmm /* IN */);


/************************************************************************
 *  Function: THH_ddrmm_reserve
 *
 *  Arguments:
 *    ddrmm
 *    addr - Physical address of reserved area
 *    size - Byte size of the reserved area
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters (Given area is beyond DDR memory space)
 *                or called after some allocation done.
 *
 *  Description:
 *    Can be used only before any THH_ddrmm_alloc...() calls.
 *    Some areas in the attached DDR memory are reserved and may not be
 *    allocated by HCA driver or an application (e.g. firmware reserved
 *    areas). This function allows the HCA driver to explicitly define a
 *    memory space to exclude of the THH_ddrmm dynamic allocation.
 */
extern HH_ret_t  THH_ddrmm_reserve (
  THH_ddrmm_t  ddrmm,    /* IN  */
  MT_phys_addr_t  addr,     /* IN  */
  MT_size_t    size      /* IN  */
);


/************************************************************************
 *  Function: THH_ddrmm_alloc_sz_aligned
 *
 *  Arguments:
 *    ddrmm - THH_ddrmm context
 *    num_o_chunks - The number of chunks to allocate (size of arrays below)
 *    chunks_sizes - An array of num_o_chunks sizes (log2) array.
 *                   One for each chunk.
 *    chunks_addrs - An array of num_o_chunks addresses allocated
 *                   for the chunks based on given size/alignment
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters (NULL...)
 *    HH_EAGAIN - No resources available to match all chunks requirements
 *
 *  Description:
 *    HCA resources which are allocated during HCA initialization are all
 *    required to be size aligned (context tables etc.). By providing all
 *    those memory requirement at once this function allows the THH_ddrmm
 *    to efficiently allocate the attached DDR memory resources under the
 *    alignment constrains.
 */
extern HH_ret_t  THH_ddrmm_alloc_sz_aligned(
  THH_ddrmm_t   ddrmm,             /* IN  */
  MT_size_t     num_o_chunks,      /* IN  */
  MT_size_t*    chunks_log2_sizes, /* IN  */
  MT_phys_addr_t*  chunks_addrs       /* OUT */
);


/************************************************************************
 *  Function: THH_ddrmm_alloc
 *
 *  Arguments:
 *    ddrmm -       The object handle
 *    size -        Size in bytes of memory chunk required
 *    align_shift - Alignment shift of chunk (log2 of alignment value)
 *    buf_p -       The returned allocated buffer physical address
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalud object handle
 *    HH_EAGAIN - Not enough resources for required allocation
 *
 *  Description:
 *    Allocate a physically contiguous memory chunk in DDR memory which
 *    follows requirement of size and alignment.
 */
extern HH_ret_t  THH_ddrmm_alloc(
  THH_ddrmm_t   ddrmm,        /* IN  */
  MT_size_t     size,         /* IN  */
  u_int8_t      align_shift,  /* IN  */
  MT_phys_addr_t*  buf_p         /* OUT */
);


/************************************************************************
 *  Function: THH_ddrmm_alloc_bound
 *
 *  Arguments:
 *    ddrmm -       The object handle
 *    size -        Size in bytes of memory chunk required
 *    align_shift - Alignment shift of chunk (log2 of alignment value)
 *    area_start -  Start of area where allocation is restricted to.
 *    area_size -   Size of area where allocation is restricted to.
 *    buf_p -       The returned allocated buffer physical address
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalud object handle
 *    HH_EAGAIN - Not enough resources for required allocation
 *
 *  Description:
 *    Allocate a physically contiguous memory chunk in DDR memory which
 *    follows requirement of size and alignment.
 */
extern HH_ret_t  THH_ddrmm_alloc_bound(
  THH_ddrmm_t   ddrmm,        /* IN  */
  MT_size_t     size,         /* IN  */
  u_int8_t      align_shift,  /* IN  */
  MT_phys_addr_t   area_start,   /* IN  */
  MT_phys_addr_t   area_size,    /* IN  */
  MT_phys_addr_t*  buf_p         /* OUT */
);


/************************************************************************
 *  Function: THH_ddrmm_free
 *
 *  Arguments:
 *    ddrmm - The object handle
 *    buf -   Exact address of buffer as given in allocation
 *    size -  Original size (in bytes)of buffer as given in allocation
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Given handle in unknown or given address is not
 *                an address returned by THH_ddrmm_alloc
 *                (or THH_ddrmm_alloc_sz_aligned)
 *
 *  Description:
 *    Free a memory chunk allocated by THH_ddrmm_alloc.
 */
extern HH_ret_t  THH_ddrmm_free(
  THH_ddrmm_t  ddrmm,       /* IN  */
  MT_phys_addr_t  buf,         /* IN */
  MT_size_t    size         /* IN  */
);


/************************************************************************
 *  Function: THH_ddrmm_query
 *
 *  Arguments:
 *    ddrmm -               The object handle
 *    align_shift -         Alignment requirements for returned  largest_chunk
 *    total_mem -           Total byte count of memory managed by this object
 *    free_mem -            Total byte count of free memory
 *    largest_chunk -       Largest chunk possible to allocate with given
 *                          align_shift  requirements
 *    largest_free_addr_p - Address of the refered largest chunk
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid handle
 *
 *  Description:
 *    Query ddrmm object for allocation capabilities with given alignment
 *    contrains (use 0 if alignment is not needed). This is useful in case
 *    one wishes to get a hint from the object on the amount to request.
 */
extern HH_ret_t  THH_ddrmm_query(
  THH_ddrmm_t   ddrmm,              /* IN  */
  u_int8_t      align_shift,        /* IN  */
  VAPI_size_t*    total_mem,          /* OUT */
  VAPI_size_t*    free_mem,           /* OUT */
  VAPI_size_t*    largest_chunk,      /* OUT */
  VAPI_phy_addr_t*  largest_free_addr_p /* OUT */
);


#endif /* _TDDRM__H */
