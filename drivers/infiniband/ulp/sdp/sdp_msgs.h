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

  $Id: sdp_msgs.h,v 1.6 2004/02/24 23:48:57 roland Exp $
*/

#ifndef _TS_SDP_MSGS_H
#define _TS_SDP_MSGS_H

#include <ib_types.h>
/*
 * Message Identifier Opcodes for BSDH
 */
/*        Name                       Value    Extended Header     Payload   */
#define TS_SDP_MSG_MID_HELLO           0x00 /* tSDP_MSG_HH       <none>   */
#define TS_SDP_MSG_MID_HELLO_ACK       0x01 /* tSDP_MSG_HAH      <none>   */
#define TS_SDP_MSG_MID_DISCONNECT      0x02 /* <none>               <none>   */
#define TS_SDP_MSG_MID_ABORT_CONN      0x03 /* <none>               <none>   */
#define TS_SDP_MSG_MID_SEND_SM         0x04 /* <none>               <none>   */
#define TS_SDP_MSG_MID_RDMA_WR_COMP    0x05 /* tSDP_MSG_RWCH     <none>   */
#define TS_SDP_MSG_MID_RDMA_RD_COMP    0x06 /* tSDP_MSG_RRCH     <none>   */
#define TS_SDP_MSG_MID_MODE_CHANGE     0x07 /* tSDP_MSG_MCH      <none>   */
#define TS_SDP_MSG_MID_SRC_CANCEL      0x08 /* <none>               <none>   */
#define TS_SDP_MSG_MID_SNK_CANCEL      0x09 /* <none>               <none>   */
#define TS_SDP_MSG_MID_SNK_CANCEL_ACK  0x0A /* <none>               <none>   */
#define TS_SDP_MSG_MID_CH_RECV_BUF     0x0B /* tSDP_MSG_CRBH     <none>   */
#define TS_SDP_MSG_MID_CH_RECV_BUF_ACK 0x0C /* tSDP_MSG_CRBAH    <none>   */
#define TS_SDP_MSG_MID_SUSPEND         0x0D /* tSDP_MSG_SCH      <none>   */
#define TS_SDP_MSG_MID_SUSPEND_ACK     0x0E /* <none>               <none>   */
#define TS_SDP_MSG_MID_SNK_AVAIL       0xFD /* tSDP_MSG_SNKAH  <optional> */
#define TS_SDP_MSG_MID_SRC_AVAIL       0xFE /* tSDP_MSG_SRCAH  <optional> */
#define TS_SDP_MSG_MID_DATA            0xFF /* <none>             <optional> */
/*
 * shift number for BSDH Flags.
 */
#define TS_SDP_MSG_FLAG_NON_FLAG (0x0)       /* no flag present */
#define TS_SDP_MSG_FLAG_OOB_PRES  0    /* out-of-band data present */
#define TS_SDP_MSG_FLAG_OOB_PEND  1    /* out-of-band data pending */
#define TS_SDP_MSG_FLAG_REQ_PIPE  2    /* request change to pipelined  */
/*
 * message type masks
 */
#define TS_SDP_MSG_MID_CTRL(mid) ((0 < (0xF0 & mid)) ? 0 : 1)
/*
 * Base Sockets Direct Header (header for all SDP messages)
 */
struct tSDP_MSG_BSDH_STRUCT {
  tUINT8   mid;        /* message identifier opcode (TS_SDP_MSG_MID_*) */
  tUINT8   flags;      /* flags as defined by TS_SDP_MSG_FLAG_* */
  tUINT16  recv_bufs;  /* cuurent number of posted private receive buffers */
  tUINT32  size;       /* length of message, including header(s) and data */
  tUINT32  seq_num;    /* message sequence number */
  tUINT32  seq_ack;    /* last received message sequence number */
} __attribute__ ((packed)); /* tSDP_MSG_BSDH_STRUCT */
/*
 * Hello Header constants (two 8-bit constants, no conversion needed)
 */
#ifdef _TS_SDP_MS_APRIL_ERROR_COMPAT
#define TS_SDP_MSG_IPVER   0x04 /* (1: ipversion), (0: reserved) */
#else
#define TS_SDP_MSG_IPVER   0x40 /* (1: ipversion), (0: reserved) */
#endif
#define TS_SDP_MSG_VERSION 0x11 /* (1: major ), (0: minor ) */
/*
 * Hello Header (BSDH + HH are contained in private data of the CM REQ MAD
 */
struct tSDP_MSG_HH_STRUCT {
  tUINT8  version;    /* 0-3: minor version (current spec; 0x1)
			 4-7: major version (current spec; 0x1) */
  tUINT8  ip_ver;     /* 0-3: reserved
			 4-7: ip version (0x4 = ipv4, 0x6 = ipv6) */
  tUINT8  rsvd_1;     /* reserved */
  tUINT8  max_adv;    /* max outstanding Zcopy advertisments (>0) */
  tUINT32 r_rcv_size; /* requested size of each remote receive buffer */
  tUINT32 l_rcv_size; /* initial size of each local receive buffer */
  tUINT16 port;       /* local port */
  tUINT16 rsvd_2;     /* reserved */

  union {             /* source IP address. */
    struct {
      tUINT32 addr3;  /* ipv6 96-127 */
      tUINT32 addr2;  /* ipv6 64-95  */
      tUINT32 addr1;  /* ipv6 32-63  */
      tUINT32 addr0;  /* ipv6  0-31  */
    } ipv6;           /* 128bit IPv6 address */
    struct {
      tUINT32 none2;  /* unused 96-127 */
      tUINT32 none1;  /* unused 64-95  */
      tUINT32 none0;  /* unused 32-63  */
      tUINT32 addr;   /* ipv4    0-31  */
    } ipv4;           /* 32bit IPv4 address */
  } src;

  union {             /* destination IP address. */
    struct {
      tUINT32 addr3;  /* ipv6 96-127 */
      tUINT32 addr2;  /* ipv6 64-95  */
      tUINT32 addr1;  /* ipv6 32-63  */
      tUINT32 addr0;  /* ipv6  0-31  */
    } ipv6;           /* 128bit IPv6 address */
    struct {
      tUINT32 none2;  /* unused 96-127 */
      tUINT32 none1;  /* unused 64-95  */
      tUINT32 none0;  /* unused 32-63  */
      tUINT32 addr;   /* ipv4    0-31  */
    } ipv4;           /* 32bit IPv4 address */
  } dst;

  tUINT8 rsvd_3[28];  /* reserved for future use, and zero'd */
} __attribute__ ((packed)); /* tSDP_MSG_HH_STRUCT */
/*
 * Hello Acknowledgement Header (BSDH + HAH are contained in private data
 *                               of the CM REP MAD)
 */
struct tSDP_MSG_HAH_STRUCT {
  tUINT8  version;     /* 0-3: minor version (current spec; 0x1)
			  4-7: major version (current spec; 0x1) */
  tUINT16 rsvd_1;      /* reserved */
  tUINT8  max_adv;     /* max outstanding Zcopy advertisments (>0) */
  tUINT32 l_rcv_size;  /* initial size of each local receive buffer */
#if 0 /* There is a bug in the 1.1 spec. REP message grew by 8 bytes. */
  tUINT8  rsvd_2[180]; /* reserved for future use, and zero'd (big) */
#else
  tUINT8  rsvd_2[172]; /* reserved for future use, and zero'd (big) */
#endif
} __attribute__ ((packed)); /* tSDP_MSG_HAH_STRUCT */
/*
 * Source Available Header. Source notifies Sink that there are buffers
 * which can be moved, using RDMA Read, by the Sink. The message is flowing
 * in the same direction as the data it is advertising.
 */
struct tSDP_MSG_SRCAH_STRUCT {
  tUINT32  size;     /* size, in bytes, of buffer to be RDMA'd */
  tUINT32  r_key;    /* R_Key needed for sink to perform RMDA Read */
  tUINT64  addr;     /* virtual address of buffer */
#ifdef _TS_SDP_MS_APRIL_ERROR_COMPAT
  tUINT64  none;     /* garbage in their header. */
#endif
} __attribute__ ((packed)); /* tSDP_MSG_SRCAH_STRUCT */
/*
 * Sink Available Header. Sink notifies Source that there are buffers
 * into which the source, using RMDA write, can move data. The message
 * is flowing in the opposite direction as the data will be moving into
 * the buffer.
 */
struct tSDP_MSG_SNKAH_STRUCT {
  tUINT32  size;     /* size, in bytes, of buffer to be RDMA'd */
  tUINT32  r_key;    /* R_Key needed for sink to perform RMDA Read */
  tUINT64  addr;     /* virtual address of buffer */
  tUINT32  non_disc; /* SDP messages, containing data, not discarded */
} __attribute__ ((packed)); /* tSDP_MSG_SNKAH_STRUCT */
/*
 * RDMA Write Completion Header. Notifying the data sink, which sent a
 * SinkAvailable message, that the RDMA write, for the oldest outdtanding
 * SNKAH message has completed.
 */
struct tSDP_MSG_RWCH_STRUCT {
  tUINT32 size;   /* size of data RDMA'd */
} __attribute__ ((packed)); /* tSDP_MSG_RWCH_STRUCT */
/*
 * RDMA Read Completion Header. Notifiying the data source, which sent a
 * SourceAvailable message, that the RDMA Read. Sink must RDMA the
 * entire contents of the advertised buffer, minus the data sent as
 * immediate data in the SRCAH.
 */
struct tSDP_MSG_RRCH_STRUCT {
  tUINT32 size;   /* size of data actually RDMA'd */
} __attribute__ ((packed)); /* tSDP_MSG_RRCH_STRUCT */
/*
 * Mode Change Header constants. (low 4 bits are reserved, next 3 bits
 * are cast to integer and determine mode, highest bit determines send
 * or recv half of the receiving peers connection.)
 */
#define TS_SDP_MSG_MCH_BUFF_RECV 0x0
#define TS_SDP_MSG_MCH_COMB_RECV 0x1
#define TS_SDP_MSG_MCH_PIPE_RECV 0x2
#define TS_SDP_MSG_MCH_BUFF_SEND 0x8
#define TS_SDP_MSG_MCH_COMB_SEND 0x9
#define TS_SDP_MSG_MCH_PIPE_SEND 0xA
/*
 * Mode Change Header. Notification of a flowcontrol mode transition.
 * The receiver is required to change mode upon notification.
 */
struct tSDP_MSG_MCH_STRUCT {
  tUINT8  flags;        /* 0-3: reserved
			   4-6: flow control modes
			     7: send/recv flow control */
  tUINT8  reserved[3];  /* reserved for future use */
} __attribute__ ((packed)); /* tSDP_MSG_MCH_STRUCT */
/*
 * Change Receive Buffer size Header. Request for the peer to change the
 * size of it's private receive buffers.
 */
struct tSDP_MSG_CRBH_STRUCT {
  tUINT32  size; /* desired receive buffer size */
} __attribute__ ((packed)); /* tSDP_MSG_CRBH_STRUCT */
/*
 * Change Receive Buffer size Acknowkedgement Header. Response to the
 * peers request for a receive buffer size change, containing the
 * actual size size of the receive buffer.
 */
struct tSDP_MSG_CRBAH_STRUCT {
  tUINT32  size; /* actuall receive buffer size */
} __attribute__ ((packed)); /* tSDP_MSG_CRBAH_STRUCT */
/*
 * Suspend Communications Header. Request for the peer to suspend
 * communication in preperation for a socket duplication. The message
 * contains the new serviceID of the connection.
 */
struct tSDP_MSG_SCH_STRUCT {
  tUINT64 service_id; /* new service ID */
} __attribute__ ((packed)); /* tSDP_MSG_SCH_STRUCT */
/*
 * Header flags accessor functions
 */
#define TS_SDP_MSG_HDR_GET_FLAG(bsdh, flag) \
        (((bsdh)->flags & (0x1U << (flag))) >> (flag))
#define TS_SDP_MSG_HDR_SET_FLAG(bsdh, flag) \
        ((bsdh)->flags |= (0x1U << (flag)))
#define TS_SDP_MSG_HDR_CLR_FLAG(bsdh, flag) \
        ((bsdh)->flags &= ~(0x1U << (flag)))

#define TS_SDP_MSG_HDR_GET_OOB_PRES(bsdh) \
        TS_SDP_MSG_HDR_GET_FLAG(bsdh, TS_SDP_MSG_FLAG_OOB_PRES)
#define TS_SDP_MSG_HDR_SET_OOB_PRES(bsdh) \
        TS_SDP_MSG_HDR_SET_FLAG(bsdh, TS_SDP_MSG_FLAG_OOB_PRES)
#define TS_SDP_MSG_HDR_CLR_OOB_PRES(bsdh) \
        TS_SDP_MSG_HDR_CLR_FLAG(bsdh, TS_SDP_MSG_FLAG_OOB_PRES)
#define TS_SDP_MSG_HDR_GET_OOB_PEND(bsdh) \
        TS_SDP_MSG_HDR_GET_FLAG(bsdh, TS_SDP_MSG_FLAG_OOB_PEND)
#define TS_SDP_MSG_HDR_SET_OOB_PEND(bsdh) \
        TS_SDP_MSG_HDR_SET_FLAG(bsdh, TS_SDP_MSG_FLAG_OOB_PEND)
#define TS_SDP_MSG_HDR_CLR_OOB_PEND(bsdh) \
        TS_SDP_MSG_HDR_CLR_FLAG(bsdh, TS_SDP_MSG_FLAG_OOB_PEND)
#define TS_SDP_MSG_HDR_GET_REQ_PIPE(bsdh) \
        TS_SDP_MSG_HDR_GET_FLAG(bsdh, TS_SDP_MSG_FLAG_REQ_PIPE)
#define TS_SDP_MSG_HDR_SET_REQ_PIPE(bsdh) \
        TS_SDP_MSG_HDR_SET_FLAG(bsdh, TS_SDP_MSG_FLAG_REQ_PIPE)
#define TS_SDP_MSG_HDR_CLR_REQ_PIPE(bsdh) \
        TS_SDP_MSG_HDR_CLR_FLAG(bsdh, TS_SDP_MSG_FLAG_REQ_PIPE)

#define TS_SDP_MSG_MCH_GET_MODE(mch) (((mch)->flags & 0xF0) >> 4)
#define TS_SDP_MSG_MCH_SET_MODE(mch, value) \
        ((mch)->flags = (((mch)->flags & 0x0F) | (value << 4)))
/* ------------------------------------------------------------------------ */
/* Endian Conversions                                                       */
/* ------------------------------------------------------------------------ */

#ifdef __LITTLE_ENDIAN

#define TS_GW_SWAP_64(x) swab64((x))
#define TS_GW_SWAP_32(x) swab32((x))
#define TS_GW_SWAP_16(x) swab16((x))

/* =========================================================== */
/*..__tsSdpMsgSwapBSDH -- SDP header endian byte swapping    */
static __inline__ tINT32 __tsSdpMsgSwapBSDH
(
 tSDP_MSG_BSDH header
)
{
  TS_CHECK_NULL(header, -EINVAL);

  header->recv_bufs = TS_GW_SWAP_16(header->recv_bufs);
  header->size      = TS_GW_SWAP_32(header->size);
  header->seq_num   = TS_GW_SWAP_32(header->seq_num);
  header->seq_ack   = TS_GW_SWAP_32(header->seq_ack);

  return 0;
} /* __tsSdpMsgSwapBSDH */

/* =========================================================== */
/*..__tsSdpMsgSwapHH -- SDP header endian byte swapping      */
static __inline__ tINT32 __tsSdpMsgSwapHH
(
 tSDP_MSG_HH header
)
{
  TS_CHECK_NULL(header, -EINVAL);

  header->r_rcv_size     = TS_GW_SWAP_32(header->r_rcv_size);
  header->l_rcv_size     = TS_GW_SWAP_32(header->l_rcv_size);
  header->port           = TS_GW_SWAP_16(header->port);
  header->src.ipv6.addr0 = TS_GW_SWAP_32(header->src.ipv6.addr0);
  header->src.ipv6.addr1 = TS_GW_SWAP_32(header->src.ipv6.addr1);
  header->src.ipv6.addr2 = TS_GW_SWAP_32(header->src.ipv6.addr2);
  header->src.ipv6.addr3 = TS_GW_SWAP_32(header->src.ipv6.addr3);
  header->dst.ipv6.addr0 = TS_GW_SWAP_32(header->dst.ipv6.addr0);
  header->dst.ipv6.addr1 = TS_GW_SWAP_32(header->dst.ipv6.addr1);
  header->dst.ipv6.addr2 = TS_GW_SWAP_32(header->dst.ipv6.addr2);
  header->dst.ipv6.addr3 = TS_GW_SWAP_32(header->dst.ipv6.addr3);

  return 0;
} /* __tsSdpMsgSwap */

/* =========================================================== */
/*..__tsSdpMsgSwapHAH -- SDP header endian byte swapping     */
static __inline__ tINT32 __tsSdpMsgSwapHAH
(
 tSDP_MSG_HAH header
)
{
  TS_CHECK_NULL(header, -EINVAL);

  header->l_rcv_size = TS_GW_SWAP_32(header->l_rcv_size);

  return 0;
} /* __tsSdpMsgSwapHAH */

/* =========================================================== */
/*..__tsSdpMsgSwapSRCAH -- SDP header endian byte swapping   */
static __inline__ tINT32 __tsSdpMsgSwapSRCAH
(
 tSDP_MSG_SRCAH header
)
{
  TS_CHECK_NULL(header, -EINVAL);

  header->size  = TS_GW_SWAP_32(header->size);
#ifdef _TS_SDP_MS_APRIL_ERROR_COMPAT
  /* they're sent in little endian byte order. :-( */
#else
  header->r_key = TS_GW_SWAP_32(header->r_key);
  header->addr  = TS_GW_SWAP_64(header->addr);
#endif
  return 0;
} /* __tsSdpMsgSwapSRCAH */

/* =========================================================== */
/*..__tsSdpMsgSwapSNKAH -- SDP header endian byte swapping   */
static __inline__ tINT32 __tsSdpMsgSwapSNKAH
(
 tSDP_MSG_SNKAH header
)
{
  TS_CHECK_NULL(header, -EINVAL);

  header->size     = TS_GW_SWAP_32(header->size);
  header->r_key    = TS_GW_SWAP_32(header->r_key);
  header->addr     = TS_GW_SWAP_64(header->addr);
  header->non_disc = TS_GW_SWAP_32(header->non_disc);

  return 0;
} /* __tsSdpMsgSwapSNKAH */

/* =========================================================== */
/*..__tsSdpMsgSwapRWCH -- SDP header endian byte swapping    */
static __inline__ tINT32 __tsSdpMsgSwapRWCH
(
 tSDP_MSG_RWCH header
)
{
  TS_CHECK_NULL(header, -EINVAL);

  header->size = TS_GW_SWAP_32(header->size);

  return 0;
} /* __tsSdpMsgSwapRWCH */

/* =========================================================== */
/*..__tsSdpMsgSwapRRCH -- SDP header endian byte swapping    */
static __inline__ tINT32 __tsSdpMsgSwapRRCH
(
 tSDP_MSG_RRCH header
)
{
  TS_CHECK_NULL(header, -EINVAL);

  header->size = TS_GW_SWAP_32(header->size);

  return 0;
} /* __tsSdpMsgSwapRRCH */

/* =========================================================== */
/*..__tsSdpMsgSwapMCH -- SDP header endian byte swapping     */
static __inline__ tINT32 __tsSdpMsgSwapMCH
(
 tSDP_MSG_MCH header
)
{
  return 0;
} /* __tsSdpMsgSwap */

/* =========================================================== */
/*..__tsSdpMsgSwapCRBH -- SDP header endian byte swapping    */
static __inline__ tINT32 __tsSdpMsgSwapCRBH
(
 tSDP_MSG_CRBH header
)
{
  TS_CHECK_NULL(header, -EINVAL);

  header->size = TS_GW_SWAP_32(header->size);

  return 0;
} /* __tsSdpMsgSwapCRBH */

/* =========================================================== */
/*..__tsSdpMsgSwapCRBAH -- SDP header endian byte swapping   */
static __inline__ tINT32 __tsSdpMsgSwapCRBAH
(
 tSDP_MSG_CRBAH header
)
{
  TS_CHECK_NULL(header, -EINVAL);

  header->size = TS_GW_SWAP_32(header->size);

  return 0;
} /* __tsSdpMsgSwapCRBAH */

/* =========================================================== */
/*..__tsSdpMsgSwapSCH -- SDP header endian byte swapping     */
static __inline__ tINT32 __tsSdpMsgSwapSCH
(
 tSDP_MSG_SCH header
)
{
  TS_CHECK_NULL(header, -EINVAL);

  header->service_id = TS_GW_SWAP_64(header->service_id);

  return 0;
} /* __tsSdpMsgSwapSCH */

#define _tsSdpMsgHostToWireBSDH  __tsSdpMsgSwapBSDH
#define _tsSdpMsgWireToHostBSDH  __tsSdpMsgSwapBSDH
#define _tsSdpMsgHostToWireHH    __tsSdpMsgSwapHH
#define _tsSdpMsgWireToHostHH    __tsSdpMsgSwapHH
#define _tsSdpMsgHostToWireHAH   __tsSdpMsgSwapHAH
#define _tsSdpMsgWireToHostHAH   __tsSdpMsgSwapHAH
#define _tsSdpMsgHostToWireSRCAH __tsSdpMsgSwapSRCAH
#define _tsSdpMsgWireToHostSRCAH __tsSdpMsgSwapSRCAH
#define _tsSdpMsgHostToWireSNKAH __tsSdpMsgSwapSNKAH
#define _tsSdpMsgWireToHostSNKAH __tsSdpMsgSwapSNKAH
#define _tsSdpMsgHostToWireRWCH  __tsSdpMsgSwapRWCH
#define _tsSdpMsgWireToHostRWCH  __tsSdpMsgSwapRWCH
#define _tsSdpMsgHostToWireRRCH  __tsSdpMsgSwapRRCH
#define _tsSdpMsgWireToHostRRCH  __tsSdpMsgSwapRRCH
#define _tsSdpMsgHostToWireMCH   __tsSdpMsgSwapMCH
#define _tsSdpMsgWireToHostMCH   __tsSdpMsgSwapMCH
#define _tsSdpMsgHostToWireCRBH  __tsSdpMsgSwapCRBH
#define _tsSdpMsgWireToHostCRBH  __tsSdpMsgSwapCRBH
#define _tsSdpMsgHostToWireCRBAH __tsSdpMsgSwapCRBAH
#define _tsSdpMsgWireToHostCRBAH __tsSdpMsgSwapCRBAH
#define _tsSdpMsgHostToWireSCH   __tsSdpMsgSwapSCH
#define _tsSdpMsgWireToHostSCH   __tsSdpMsgSwapSCH

#else /* big endian */

#if !defined(__BIG_ENDIAN)
#warning "assuming big endian architecture, but it's not defined!!"
#endif

#ifdef _TS_SDP_MS_APRIL_ERROR_COMPAT
/* =========================================================== */
/*..__tsSdpMsgSwapSRCAH -- SDP header endian byte swapping   */
static __inline__ tINT32 __tsSdpMsgSwapSRCAH
(
 tSDP_MSG_SRCAH header
)
{
  TS_CHECK_NULL(header, -EINVAL);
  /*
   * they're sendt in little endian byte order. :-(
   */
  header->r_key = swab32(header->r_key);
  header->addr  = swab64(header->addr);

  return 0;
} /* __tsSdpMsgSwapSRCAH */

#define _tsSdpMsgHostToWireSRCAH __tsSdpMsgSwapSRCAH
#define _tsSdpMsgWireToHostSRCAH __tsSdpMsgSwapSRCAH
#else
#define _tsSdpMsgHostToWireSRCAH(x) (0)
#define _tsSdpMsgWireToHostSRCAH(x) (0)
#endif

#define _tsSdpMsgHostToWireBSDH(x)  (0)
#define _tsSdpMsgWireToHostBSDH(x)  (0)
#define _tsSdpMsgHostToWireHH(x)    (0)
#define _tsSdpMsgWireToHostHH(x)    (0)
#define _tsSdpMsgHostToWireHAH(x)   (0)
#define _tsSdpMsgWireToHostHAH(x)   (0)
#define _tsSdpMsgHostToWireSNKAH(x) (0)
#define _tsSdpMsgWireToHostSNKAH(x) (0)
#define _tsSdpMsgHostToWireRWCH(x)  (0)
#define _tsSdpMsgWireToHostRWCH(x)  (0)
#define _tsSdpMsgHostToWireRRCH(x)  (0)
#define _tsSdpMsgWireToHostRRCH(x)  (0)
#define _tsSdpMsgHostToWireMCH(x)   (0)
#define _tsSdpMsgWireToHostMCH(x)   (0)
#define _tsSdpMsgHostToWireCRBH(x)  (0)
#define _tsSdpMsgWireToHostCRBH(x)  (0)
#define _tsSdpMsgHostToWireCRBAH(x) (0)
#define _tsSdpMsgWireToHostCRBAH(x) (0)
#define _tsSdpMsgHostToWireSCH(x)   (0)
#define _tsSdpMsgWireToHostSCH(x)   (0)

#endif
/* ------------------------------------------------------------------------ */
/* Miscellaneous message related informtation                               */
/* ------------------------------------------------------------------------ */
/*
 * Event handling function, demultiplexed base on Message ID
 */
typedef tINT32 (* tGW_SDP_EVENT_CB_FUNC)(tSDP_CONN  conn,
					 tSDP_BUFF  buff);
/*
 * Connection messages
 */
struct tSDP_MSG_HELLO_STRUCT {
  tSDP_MSG_BSDH_STRUCT  bsdh;    /* base sockets direct header */
  tSDP_MSG_HH_STRUCT    hh;      /* hello message header */
} __attribute__ ((packed)); /* tSDP_MSG_HELLO_STRUCT */

struct tSDP_MSG_HELLO_ACK_STRUCT {
  tSDP_MSG_BSDH_STRUCT  bsdh;    /* base sockets direct header */
  tSDP_MSG_HAH_STRUCT   hah;     /* hello ack message header */
} __attribute__ ((packed)); /* tSDP_MSG_HELLO_ACK_STRUCT */

#endif /* _TS_SDP_MSGS_H */
