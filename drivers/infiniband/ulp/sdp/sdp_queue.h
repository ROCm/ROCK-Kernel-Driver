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

  $Id: sdp_queue.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_SDP_QUEUE_H
#define _TS_SDP_QUEUE_H
/*
 * topspin specific includes.
 */
#include "sdp_types.h"
/*
 * defines for object types.
 */

typedef enum {
  TS_SDP_GENERIC_TYPE_UNKOWN = 0x00,
  TS_SDP_GENERIC_TYPE_BUFF   = 0x01,
  TS_SDP_GENERIC_TYPE_IOCB   = 0x02,
  TS_SDP_GENERIC_TYPE_ADVT   = 0x03,
  TS_SDP_GENERIC_TYPE_NONE
} tSDP_GENERIC_TYPE;
/*
 * object destruction callback type
 */
typedef tINT32 (* tSDP_GENERIC_DESTRUCT_FUNC) (tSDP_GENERIC element);

typedef tINT32 (* tSDP_GENERIC_LOOKUP_FUNC)  (tSDP_GENERIC element,
					      tPTR     arg);
/* ----------------------------------------------------------------------- */
/* SDP generic queue for multiple object types.                            */
/* ----------------------------------------------------------------------- */

struct tSDP_GENERIC_STRUCT {
  tSDP_GENERIC               next;   /* next structure in table */
  tSDP_GENERIC               prev;   /* previous structure in table */
  tUINT32                    type;   /* element type. (for generic queue) */
  tSDP_GENERIC_TABLE         table;  /* table to which this object belongs */
  tSDP_GENERIC_DESTRUCT_FUNC release; /* release the object */
}; /* tSDP_GENERIC_STRUCT */
/*
 * table for holding SDP advertisments.
 */
struct tSDP_GENERIC_TABLE_STRUCT {
  tSDP_GENERIC head;  /* double linked list of advertisments */
  tINT32       size;  /* current number of advertisments in table */
  tUINT16      count[TS_SDP_GENERIC_TYPE_NONE]; /* object specific counter */
}; /* tSDP_GENERIC_TABLE_STRUCT */
/* ----------------------------------------------------------------------- */
/*                                                                         */
/* SDP generic queue inline functions.                                     */
/*                                                                         */
/* ----------------------------------------------------------------------- */

/* ========================================================================= */
/*..__tsSdpGenericTableSize - return the number of elements in the table */
static __inline__ tINT32 __tsSdpGenericTableSize
(
 tSDP_GENERIC_TABLE table
)
{
  TS_CHECK_NULL(table, -EINVAL);

  return table->size;
} /* __tsSdpGenericTableSize */

/* ========================================================================= */
/*..__tsSdpGenericTableMember - return non-zero if element is in a table */
static __inline__ tINT32 __tsSdpGenericTableMember
(
 tSDP_GENERIC element
)
{
  TS_CHECK_NULL(element, -EINVAL);

  return ((NULL == element->table) ? 0 : 1);
} /* __tsSdpGenericTableMember */

#define tsSdpGenericTableSize(x)   __tsSdpGenericTableSize(x)
#define tsSdpGenericTableMember(x) __tsSdpGenericTableMember(x)

#endif /* _TS_SDP_QUEUE_H */
