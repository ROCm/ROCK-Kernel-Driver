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

  $Id: sdp_buff_p.h 35 2004-04-09 05:34:32Z roland $
*/

#ifndef _TS_SDP_BUFF_P_H
#define _TS_SDP_BUFF_P_H
/*
 * linux types
 */
#include <asm/atomic.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
/*
 * topspin specific headers.
 */
#include <ib_legacy_types.h>
#include "sdp_types.h"
#include "sdp_buff.h"
/*
 * definitions
 */
#define TS_SDP_BUFFER_COUNT_MIN 1024
#define TS_SDP_BUFFER_COUNT_MAX 131072
#define TS_SDP_BUFFER_COUNT_INC 1024

#define TS_SDP_POOL_NAME_MAX   16     /* maximum size pool name */
#define TS_SDP_MAIN_POOL_NAME  "main"
#define TS_SDP_BUFF_OUT_LEN    33    /* size of buffer output line */
/*
 * types
 */
typedef struct tSDP_MAIN_POOL_STRUCT tSDP_MAIN_POOL_STRUCT, *tSDP_MAIN_POOL;
typedef struct tSDP_MEMORY_SEGMENT_STRUCT tSDP_MEMORY_SEGMENT_STRUCT,
              *tSDP_MEMORY_SEGMENT;
typedef struct tSDP_MEM_SEG_HEAD_STRUCT tSDP_MEM_SEG_HEAD_STRUCT,
              *tSDP_MEM_SEG_HEAD;
/*
 * structures
 */
struct tSDP_MAIN_POOL_STRUCT {
  /*
   * variant
   */
  tSDP_POOL_STRUCT      pool;  /* actual pool of buffers */
  spinlock_t            lock;  /* spin lock for pool access */
  /*
   * invariant
   */
  kmem_cache_t        *pool_cache; /* cache of pool objects */

  tUINT32              buff_min;
  tUINT32              buff_max;
  tUINT32              buff_cur;
  tUINT32              buff_size;  /* size of each buffer in the pool */

  tSDP_MEMORY_SEGMENT  segs;
}; /* tSDP_MAIN_POOL_STRUCT */

/*
 * Each memory segment is its own page.
 */
struct tSDP_MEM_SEG_HEAD_STRUCT {
  tSDP_MEMORY_SEGMENT  next;
  tSDP_MEMORY_SEGMENT  prev;
  tUINT32              size;
}; /* tSDP_MEM_SEG_HEAD_STRUCT */

#define TS_SDP_BUFF_COUNT ((PAGE_SIZE - sizeof(tSDP_MEM_SEG_HEAD_STRUCT))/ \
			   sizeof(tSDP_BUFF_STRUCT))

struct tSDP_MEMORY_SEGMENT_STRUCT {
  tSDP_MEM_SEG_HEAD_STRUCT head;
  tSDP_BUFF_STRUCT         list[TS_SDP_BUFF_COUNT];
}; /* tSDP_MEMORY_REGION_STRUCT */

#endif /* _TS_SDP_BUFF_P_H */
