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

  $Id: sdp_advt.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_SDP_ADVT_H
#define _TS_SDP_ADVT_H
/*
 * topspin specific includes.
 */
#include "sdp_types.h"
#include "sdp_queue.h"
/*
 * IOCB flags.
 */
#define TS_SDP_ADVT_F_READ 0x00000001 /* ADVT has an active read operation */
/* ----------------------------------------------------------------------- */
/* SDP read/write advertisments                                            */
/* ----------------------------------------------------------------------- */

struct tSDP_ADVT_STRUCT {
  tSDP_ADVT                  next;   /* next structure in table */
  tSDP_ADVT                  prev;   /* previous structure in table */
  tUINT32                    type;   /* element type. (for generic queue) */
  tSDP_ADVT_TABLE            table;  /* table to which this object belongs */
  tSDP_GENERIC_DESTRUCT_FUNC release; /* release the object */
  /*
   * advertisment specific
   */
  tTS_IB_RKEY        rkey; /* advertised buffer remote key */
  tINT32             size; /* advertised buffer size */
  tINT32             post; /* running total of data moved for this advert. */
  tUINT32            wrid; /* work request completing this advertisment */
  tUINT32            flag; /* advertisment flags. */
  tUINT64            addr; /* advertised buffer virtual address */
}; /* tSDP_ADVT_STRUCT */
/*
 * table for holding SDP advertisments.
 */
struct tSDP_ADVT_TABLE_STRUCT {
  tSDP_ADVT head;  /* double linked list of advertisments */
  tINT32    size;  /* current number of advertisments in table */
}; /* tSDP_ADVT_TABLE_STRUCT */
/*
 * make size a macro.
 */
#define tsSdpConnAdvtTableSize(table) ((table)->size)

#endif /* _TS_SDP_ADVT_H */
