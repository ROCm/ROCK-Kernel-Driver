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

  $Id: sdp_types.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_SDP_TYPES_H
#define _TS_SDP_TYPES_H
/*
 * defines
 */
#if 0
#define TS_EXPECT(mod, expr)                                      \
{                                                                 \
  if (!(expr)) {                                                  \
    TS_TRACE(mod, T_TERSE, TRACE_FLOW_WARN,                       \
             "EXCEPT: Internal error check <%s> failed.", #expr); \
  } /* if */                                                      \
} /* TS_EXPECT */

#define TS_CHECK_NULL(value, result) \
        if (NULL == (value)) return (result);
#define TS_CHECK_LT(value, bound, result) \
        if ((bound) > (value)) return(result);
#define TS_CHECK_GT(value, bound, result) \
        if ((bound) < (value)) return(result);
#define TS_CHECK_EQ(value, test, result) \
        if ((test) == (value)) return(result);
#define TS_CHECK_EXPR(expr, result) \
        if (!(expr)) return(result);
#else
#define TS_EXPECT(mod, expr)
#define TS_CHECK_NULL(value, result)
#define TS_CHECK_LT(value, bound, result)
#define TS_CHECK_GT(value, bound, result)
#define TS_CHECK_EQ(value, test, result)
#define TS_CHECK_EXPR(expr, result)
#endif

#define TS_SPIN_LOCK_INIT(x) spin_lock_init((x))
#if 0
#define TS_SPIN_LOCK(x, y)   spin_lock((x))
#define TS_SPIN_UNLOCK(x, y) spin_unlock((x))
#else
#define TS_SPIN_LOCK(x, y)   spin_lock_irqsave((x), (y))
#define TS_SPIN_UNLOCK(x, y) spin_unlock_irqrestore((x), (y))
#endif

/*
 * types
 */
typedef struct tSDP_POOL_STRUCT tSDP_POOL_STRUCT, *tSDP_POOL;
typedef struct tSDP_BUFF_STRUCT tSDP_BUFF_STRUCT, *tSDP_BUFF;
typedef struct tSDP_CONN_STRUCT tSDP_CONN_STRUCT, *tSDP_CONN;
typedef struct tSDP_ADVT_STRUCT tSDP_ADVT_STRUCT, *tSDP_ADVT;
typedef struct tSDP_IOCB_STRUCT tSDP_IOCB_STRUCT, *tSDP_IOCB;

typedef struct tSDP_ADVT_TABLE_STRUCT tSDP_ADVT_TABLE_STRUCT, *tSDP_ADVT_TABLE;
typedef struct tSDP_IOCB_TABLE_STRUCT tSDP_IOCB_TABLE_STRUCT, *tSDP_IOCB_TABLE;
typedef struct tSDP_DEV_ROOT_STRUCT tSDP_DEV_ROOT_STRUCT, *tSDP_DEV_ROOT;

typedef struct tSDP_DEV_EVENT_STRUCT tSDP_DEV_EVENT_STRUCT, *tSDP_DEV_EVENT;
typedef struct tSDP_GENERIC_STRUCT tSDP_GENERIC_STRUCT, *tSDP_GENERIC;
typedef struct tSDP_GENERIC_TABLE_STRUCT tSDP_GENERIC_TABLE_STRUCT, \
              *tSDP_GENERIC_TABLE;

typedef struct tSDP_CONN_LOCK_STRUCT tSDP_CONN_LOCK_STRUCT, *tSDP_CONN_LOCK;

typedef struct tSDP_DEV_PORT_STRUCT tSDP_DEV_PORT_STRUCT, *tSDP_DEV_PORT;
typedef struct tSDP_DEV_HCA_STRUCT tSDP_DEV_HCA_STRUCT, *tSDP_DEV_HCA;
/*
 * msg header types
 */
typedef struct tSDP_MSG_BSDH_STRUCT  tSDP_MSG_BSDH_STRUCT,  *tSDP_MSG_BSDH;
typedef struct tSDP_MSG_HH_STRUCT    tSDP_MSG_HH_STRUCT,    *tSDP_MSG_HH;
typedef struct tSDP_MSG_HAH_STRUCT   tSDP_MSG_HAH_STRUCT,   *tSDP_MSG_HAH;
typedef struct tSDP_MSG_SRCAH_STRUCT tSDP_MSG_SRCAH_STRUCT, *tSDP_MSG_SRCAH;
typedef struct tSDP_MSG_SNKAH_STRUCT tSDP_MSG_SNKAH_STRUCT, *tSDP_MSG_SNKAH;
typedef struct tSDP_MSG_RWCH_STRUCT  tSDP_MSG_RWCH_STRUCT,  *tSDP_MSG_RWCH;
typedef struct tSDP_MSG_RRCH_STRUCT  tSDP_MSG_RRCH_STRUCT,  *tSDP_MSG_RRCH;
typedef struct tSDP_MSG_MCH_STRUCT   tSDP_MSG_MCH_STRUCT,   *tSDP_MSG_MCH;
typedef struct tSDP_MSG_CRBH_STRUCT  tSDP_MSG_CRBH_STRUCT,  *tSDP_MSG_CRBH;
typedef struct tSDP_MSG_CRBAH_STRUCT tSDP_MSG_CRBAH_STRUCT, *tSDP_MSG_CRBAH;
typedef struct tSDP_MSG_SCH_STRUCT   tSDP_MSG_SCH_STRUCT,   *tSDP_MSG_SCH;

typedef struct tSDP_MSG_HELLO_STRUCT tSDP_MSG_HELLO_STRUCT, \
              *tSDP_MSG_HELLO;
typedef struct tSDP_MSG_HELLO_ACK_STRUCT tSDP_MSG_HELLO_ACK_STRUCT, \
              *tSDP_MSG_HELLO_ACK;
/*
 * end point resolution functions
 */
typedef struct tSDP_IPOIB_ADDR_STRUCT tSDP_IPOIB_ADDR_STRUCT, *tSDP_IPOIB_ADDR;
typedef struct tSDP_IPOIB_HDR_STRUCT tSDP_IPOIB_HDR_STRUCT, *tSDP_IPOIB_HDR;
typedef struct tSDP_IPOIB_ARP_STRUCT tSDP_IPOIB_ARP_STRUCT, *tSDP_IPOIB_ARP;
typedef struct tSDP_IPOIB_WAIT_STRUCT tSDP_IPOIB_WAIT_STRUCT, *tSDP_IPOIB_WAIT;

typedef struct tSDP_LINK_ROOT_STRUCT tSDP_LINK_ROOT_STRUCT, *tSDP_LINK_ROOT;
typedef struct tSDP_PATH_ELEMENT_STRUCT tSDP_PATH_ELEMENT_STRUCT, \
              *tSDP_PATH_ELEMENT;
/*
 * atomic ref count.
 */
typedef atomic_t tSDP_ATOMIC;


#define TS_SDP_ATOMIC_GET(x)      atomic_read(&(x))
#define TS_SDP_ATOMIC_INC(x)      atomic_inc(&(x))
#define TS_SDP_ATOMIC_SET(x, y)   atomic_set(&(x), (y))
#define TS_SDP_ATOMIC_DEC_TEST(x) atomic_dec_and_test(&(x))

#endif /* _TS_SDP_TYPES_H */
