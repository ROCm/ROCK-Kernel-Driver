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

  $Id: sdp_buff.h 35 2004-04-09 05:34:32Z roland $
*/

#ifndef _TS_SDP_BUFF_H
#define _TS_SDP_BUFF_H
/*
 * topspin specific headers.
 */
#include <ib_legacy_types.h>
#include "sdp_types.h"
#include "sdp_queue.h"
/*
 * structures
 */
struct tSDP_POOL_STRUCT {
  tSDP_BUFF  head;  /* double linked list of buffers */
  tUINT32    size;  /* current number of buffers allocated to the pool */
#ifdef _TS_SDP_DEBUG_POOL_NAME
  tSTR       name;  /* pointer to pools name */
#endif
}; /* tSDP_POOL_STRUCT */

struct tSDP_BUFF_STRUCT {
  tSDP_BUFF                  next;
  tSDP_BUFF                  prev;
  tUINT32                    type;  /* element type. (for generic queue) */
  tSDP_POOL                  pool;  /* pool currently holding this buffer. */
  tSDP_GENERIC_DESTRUCT_FUNC release; /* release the object */
  /*
   * primary generic data pointers
   */
  tPTR              head;  /* first byte of data buffer */
  tPTR              data;  /* first byte of valid data in buffer */
  tPTR              tail;  /* last byte of valid data in buffer */
  tPTR              end;   /* last byte of data buffer */
  /*
   * Experimental
   */
  tUINT32           flags; /* Buffer flags */
  tUINT32           u_id;     /* unique buffer ID, used for tracking */
  /*
   * Protocol specific data
   */
  tSDP_MSG_BSDH bsdh_hdr;  /* SDP header (BSDH) */
  tUINT32       data_size; /* size of just data in the buffer */
  tUINT32       ib_wrid;   /* IB work request ID */
  /*
   * IB specific data (The main buffer pool sets the lkey when it is created)
   */
  tUINT64   real;      /* component of scather/gather list (address) */
  tUINT32   size;      /* component of scather/gather list (lenght)  */
  tUINT32   lkey;      /* component of scather/gather list (key) */
}; /* tSDP_BUFF_STRUCT */

/*
 * buffer flag defintions
 */
#define TS_SDP_BUFF_F_UNSIG    0x0001  /* unsignalled buffer */
#define TS_SDP_BUFF_F_SE       0x0002  /* buffer is an IB solicited event */
#define TS_SDP_BUFF_F_OOB_PEND 0x0004  /* urgent byte in flight (OOB) */
#define TS_SDP_BUFF_F_OOB_PRES 0x0008  /* urgent byte in buffer (OOB) */
#define TS_SDP_BUFF_F_QUEUED   0x0010  /* buffer is queued for transmission */

#define TS_SDP_BUFF_F_GET_SE(buff) ((buff)->flags & TS_SDP_BUFF_F_SE)
#define TS_SDP_BUFF_F_SET_SE(buff) ((buff)->flags |= TS_SDP_BUFF_F_SE)
#define TS_SDP_BUFF_F_CLR_SE(buff) ((buff)->flags &= (~TS_SDP_BUFF_F_SE))
#define TS_SDP_BUFF_F_GET_UNSIG(buff) ((buff)->flags & TS_SDP_BUFF_F_UNSIG)
#define TS_SDP_BUFF_F_SET_UNSIG(buff) ((buff)->flags |= TS_SDP_BUFF_F_UNSIG)
#define TS_SDP_BUFF_F_CLR_UNSIG(buff) ((buff)->flags &= (~TS_SDP_BUFF_F_UNSIG))
/*
 * data accessors.
 */
#define TS_SDP_BUFF_GAT_SCAT(buff)                                  \
   ({ (buff)->real = (0x00000000FFFFFFFFULL &              \
		      (tUINT32)virt_to_phys((buff)->data)); \
      (buff)->size = (buff)->tail - (buff)->data;                   \
      (tTS_IB_GATHER_SCATTER)(&(buff)->real); })
/*
 * function prototypes used in certain functions.
 */
typedef tINT32 (* tSDP_BUFF_TEST_FUNC)  (tSDP_BUFF buff,
					 tPTR     arg);
typedef tINT32 (* tSDP_BUFF_TRAV_FUNC)  (tSDP_BUFF buff,
					 tPTR     arg);
/*
 * pool size
 */
#define tsSdpBuffPoolSize(pool) ((pool)->size)

#endif /* _TS_SDP_BUFF_H */
