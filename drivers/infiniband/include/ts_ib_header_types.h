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

  $Id: ts_ib_header_types.h,v 1.3 2004/02/25 00:32:32 roland Exp $
*/

#ifndef _TS_IB_HEADER_TYPES_H
#define _TS_IB_HEADER_TYPES_H

#include "ts_ib_core_types.h"

typedef struct tTS_IB_LRH_STRUCT  tTS_IB_LRH_STRUCT,  *tTS_IB_LRH;
typedef struct tTS_IB_GRH_STRUCT  tTS_IB_GRH_STRUCT,  *tTS_IB_GRH;
typedef struct tTS_IB_BTH_STRUCT  tTS_IB_BTH_STRUCT,  *tTS_IB_BTH;
typedef struct tTS_IB_DETH_STRUCT tTS_IB_DETH_STRUCT, *tTS_IB_DETH;

typedef struct tTS_IB_UD_HEADER_STRUCT tTS_IB_UD_HEADER_STRUCT,
  *tTS_IB_UD_HEADER;

enum {
  TS_IB_LRH_BYTES  = 8,
  TS_IB_GRH_BYTES  = 40,
  TS_IB_BTH_BYTES  = 12,
  TS_IB_DETH_BYTES = 8
};


/*
  This macro cleans up the definitions of constants for BTH opcodes.
  It is used to define constants such as TS_IB_OPCODE_UD_SEND_ONLY,
  which becomes TS_IB_OPCODE_UD + TS_IB_OPCODE_SEND_ONLY, and this
  gives the correct value.

  In short, user code should use the constants defined using the macro
  rather than worrying about adding together other constants.
*/
#define TS_IB_OPCODE(transport, op) \
  TS_IB_OPCODE_ ## transport ## _ ## op = \
    TS_IB_OPCODE_ ## transport + TS_IB_OPCODE_ ## op

typedef enum {
  /* transport types -- just used to define real constants */
  TS_IB_OPCODE_RC                                = 0x00,
  TS_IB_OPCODE_UC                                = 0x20,
  TS_IB_OPCODE_RD                                = 0x40,
  TS_IB_OPCODE_UD                                = 0x60,

  /* operations -- just used to define real constants */
  TS_IB_OPCODE_SEND_FIRST                        = 0x00,
  TS_IB_OPCODE_SEND_MIDDLE                       = 0x01,
  TS_IB_OPCODE_SEND_LAST                         = 0x02,
  TS_IB_OPCODE_SEND_LAST_WITH_IMMEDIATE          = 0x03,
  TS_IB_OPCODE_SEND_ONLY                         = 0x04,
  TS_IB_OPCODE_SEND_ONLY_WITH_IMMEDIATE          = 0x05,
  TS_IB_OPCODE_RDMA_WRITE_FIRST                  = 0x06,
  TS_IB_OPCODE_RDMA_WRITE_MIDDLE                 = 0x07,
  TS_IB_OPCODE_RDMA_WRITE_LAST                   = 0x08,
  TS_IB_OPCODE_RDMA_WRITE_LAST_WITH_IMMEDIATE    = 0x09,
  TS_IB_OPCODE_RDMA_WRITE_ONLY                   = 0x0a,
  TS_IB_OPCODE_RDMA_WRITE_ONLY_WITH_IMMEDIATE    = 0x0b,
  TS_IB_OPCODE_RDMA_READ_REQUEST                 = 0x0c,
  TS_IB_OPCODE_RDMA_READ_RESPONSE_FIRST          = 0x0d,
  TS_IB_OPCODE_RDMA_READ_RESPONSE_MIDDLE         = 0x0e,
  TS_IB_OPCODE_RDMA_READ_RESPONSE_LAST           = 0x0f,
  TS_IB_OPCODE_RDMA_READ_RESPONSE_ONLY           = 0x10,
  TS_IB_OPCODE_ACKNOWLEDGE                       = 0x11,
  TS_IB_OPCODE_ATOMIC_ACKNOWLEDGE                = 0x12,
  TS_IB_OPCODE_COMPARE_SWAP                      = 0x13,
  TS_IB_OPCODE_FETCH_ADD                         = 0x14,

  /* real constants follow -- see comment about above TS_IB_OPCODE()
     macro for more details */

  /* RC */
  TS_IB_OPCODE(RC, SEND_FIRST),
  TS_IB_OPCODE(RC, SEND_MIDDLE),
  TS_IB_OPCODE(RC, SEND_LAST),
  TS_IB_OPCODE(RC, SEND_LAST_WITH_IMMEDIATE),
  TS_IB_OPCODE(RC, SEND_ONLY),
  TS_IB_OPCODE(RC, SEND_ONLY_WITH_IMMEDIATE),
  TS_IB_OPCODE(RC, RDMA_WRITE_FIRST),
  TS_IB_OPCODE(RC, RDMA_WRITE_MIDDLE),
  TS_IB_OPCODE(RC, RDMA_WRITE_LAST),
  TS_IB_OPCODE(RC, RDMA_WRITE_LAST_WITH_IMMEDIATE),
  TS_IB_OPCODE(RC, RDMA_WRITE_ONLY),
  TS_IB_OPCODE(RC, RDMA_WRITE_ONLY_WITH_IMMEDIATE),
  TS_IB_OPCODE(RC, RDMA_READ_REQUEST),
  TS_IB_OPCODE(RC, RDMA_READ_RESPONSE_FIRST),
  TS_IB_OPCODE(RC, RDMA_READ_RESPONSE_MIDDLE),
  TS_IB_OPCODE(RC, RDMA_READ_RESPONSE_LAST),
  TS_IB_OPCODE(RC, RDMA_READ_RESPONSE_ONLY),
  TS_IB_OPCODE(RC, ACKNOWLEDGE),
  TS_IB_OPCODE(RC, ATOMIC_ACKNOWLEDGE),
  TS_IB_OPCODE(RC, COMPARE_SWAP),
  TS_IB_OPCODE(RC, FETCH_ADD),

  /* UC */
  TS_IB_OPCODE(UC, SEND_FIRST),
  TS_IB_OPCODE(UC, SEND_MIDDLE),
  TS_IB_OPCODE(UC, SEND_LAST),
  TS_IB_OPCODE(UC, SEND_LAST_WITH_IMMEDIATE),
  TS_IB_OPCODE(UC, SEND_ONLY),
  TS_IB_OPCODE(UC, SEND_ONLY_WITH_IMMEDIATE),
  TS_IB_OPCODE(UC, RDMA_WRITE_FIRST),
  TS_IB_OPCODE(UC, RDMA_WRITE_MIDDLE),
  TS_IB_OPCODE(UC, RDMA_WRITE_LAST),
  TS_IB_OPCODE(UC, RDMA_WRITE_LAST_WITH_IMMEDIATE),
  TS_IB_OPCODE(UC, RDMA_WRITE_ONLY),
  TS_IB_OPCODE(UC, RDMA_WRITE_ONLY_WITH_IMMEDIATE),

  /* RD */
  TS_IB_OPCODE(RD, SEND_FIRST),
  TS_IB_OPCODE(RD, SEND_MIDDLE),
  TS_IB_OPCODE(RD, SEND_LAST),
  TS_IB_OPCODE(RD, SEND_LAST_WITH_IMMEDIATE),
  TS_IB_OPCODE(RD, SEND_ONLY),
  TS_IB_OPCODE(RD, SEND_ONLY_WITH_IMMEDIATE),
  TS_IB_OPCODE(RD, RDMA_WRITE_FIRST),
  TS_IB_OPCODE(RD, RDMA_WRITE_MIDDLE),
  TS_IB_OPCODE(RD, RDMA_WRITE_LAST),
  TS_IB_OPCODE(RD, RDMA_WRITE_LAST_WITH_IMMEDIATE),
  TS_IB_OPCODE(RD, RDMA_WRITE_ONLY),
  TS_IB_OPCODE(RD, RDMA_WRITE_ONLY_WITH_IMMEDIATE),
  TS_IB_OPCODE(RD, RDMA_READ_REQUEST),
  TS_IB_OPCODE(RD, RDMA_READ_RESPONSE_FIRST),
  TS_IB_OPCODE(RD, RDMA_READ_RESPONSE_MIDDLE),
  TS_IB_OPCODE(RD, RDMA_READ_RESPONSE_LAST),
  TS_IB_OPCODE(RD, RDMA_READ_RESPONSE_ONLY),
  TS_IB_OPCODE(RD, ACKNOWLEDGE),
  TS_IB_OPCODE(RD, ATOMIC_ACKNOWLEDGE),
  TS_IB_OPCODE(RD, COMPARE_SWAP),
  TS_IB_OPCODE(RD, FETCH_ADD),

  /* UD */
  TS_IB_OPCODE(UD, SEND_ONLY),
  TS_IB_OPCODE(UD, SEND_ONLY_WITH_IMMEDIATE)
} tTS_IB_BTH_OPCODE;

typedef enum {
  TS_IB_LNH_RAW        = 0,
  TS_IB_LNH_IP         = 1,
  TS_IB_LNH_IBA_LOCAL  = 2,
  TS_IB_LNH_IBA_GLOBAL = 3
} tTS_IB_LRH_LNH;

struct tTS_IB_LRH_STRUCT {
  uint8_t        virtual_lane;
  uint8_t        link_version;
  uint8_t        service_level;
  tTS_IB_LRH_LNH link_next_header;
  tTS_IB_LID     destination_lid;
  uint16_t       packet_length;
  tTS_IB_LID     source_lid;
};

struct tTS_IB_GRH_STRUCT {
  uint8_t    ip_version;
  uint8_t    traffic_class;
  uint32_t   flow_label;
  uint16_t   payload_length;
  uint8_t    next_header;
  uint8_t    hop_limit;
  tTS_IB_GID source_gid;
  tTS_IB_GID destination_gid;
};

struct tTS_IB_BTH_STRUCT {
  tTS_IB_BTH_OPCODE opcode;
  uint8_t           solicited_event;
  uint8_t           mig_req;
  uint8_t           pad_count;
  uint8_t           transport_header_version;
  tTS_IB_PKEY       pkey;
  tTS_IB_QPN        destination_qpn;
  uint8_t           ack_req;
  tTS_IB_PSN        psn;
};

struct tTS_IB_DETH_STRUCT {
  tTS_IB_QKEY qkey;
  tTS_IB_QPN  source_qpn;
};

struct tTS_IB_UD_HEADER_STRUCT {
  tTS_IB_LRH_STRUCT  lrh;
  int                grh_present;
  tTS_IB_GRH_STRUCT  grh;
  tTS_IB_BTH_STRUCT  bth;
  tTS_IB_DETH_STRUCT deth;
  int                immediate_present;
  uint32_t           immediate_data;
};

#endif /* _TS_IB_HEADER_TYPES_H */
