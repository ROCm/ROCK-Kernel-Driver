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

  Copyright (c) 2004 Topspin Communications.  All rights reserved.

  $Id: mad_mem_compat.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _MAD_MEM_COMPAT_H
#define _MAD_MEM_COMPAT_H

#include "ts_kernel_uintptr.h"

/* Need the definition of high_memory: */
#include <linux/mm.h>

static inline uintptr_t ib_mad_buffer_address(void *buf)
{
  return (uintptr_t) buf;
}

static inline int ib_mad_register_memory(tTS_IB_PD_HANDLE  pd,
                                         tTS_IB_MR_HANDLE *mr,
                                         tTS_IB_LKEY      *lkey)
{
    tTS_IB_RKEY               rkey;
    uint64_t                  iova = PAGE_OFFSET;
    struct ib_physical_buffer buffer_list;
    int                       result;

    buffer_list.address = 0;

    /* make our region have size the size of low memory rounded up to
       the next power of 2, so we use as few TPT entries as possible
       and don't confuse the verbs driver when lowmem has an odd size
       (cf bug 1921) */
    for (buffer_list.size = 1;
         buffer_list.size < (uintptr_t) high_memory - PAGE_OFFSET;
         buffer_list.size <<= 1) {
      /* nothing */
    }

    result = ib_memory_register_physical(pd,
                                         &buffer_list,
                                         1, /* list_len */
                                         &iova,
                                         buffer_list.size,
                                         0, /* iova_offset */
                                         TS_IB_ACCESS_LOCAL_WRITE | TS_IB_ACCESS_REMOTE_WRITE,
                                         mr,
                                         lkey,
                                         &rkey);
    if (result) {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "ib_memory_register_physical failed "
                     "size 0x%016" TS_U64_FMT "x, iova 0x%016" TS_U64_FMT "x"
                     " (return code %d)",
                     buffer_list.size, iova, result);
    }
    return result;
}

#endif /* _MAD_COMPAT_H */
