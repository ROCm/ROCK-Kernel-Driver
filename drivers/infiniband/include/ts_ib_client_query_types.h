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

  $Id: ts_ib_client_query_types.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_IB_CLIENT_QUERY_TYPES_H
#define _TS_IB_CLIENT_QUERY_TYPES_H

#if defined(__KERNEL__)
#  ifndef W2K_OS
#    include <linux/types.h>
#    include "ts_kernel_uintptr.h"
#  endif
#else
#  ifndef W2K_OS
#    include <stdint.h>
#  endif
#  include <stddef.h>             /* for size_t */
#endif

#ifdef W2K_OS
#include <all/common/include/w2k.h>
#endif

#include "ts_ib_core_types.h"

typedef struct tTS_IB_COMMON_ATTRIB_CPI_STRUCT tTS_IB_COMMON_ATTRIB_CPI_STRUCT,
  *tTS_IB_COMMON_ATTRIB_CPI;
typedef struct tTS_IB_COMMON_ATTRIB_NOTICE_STRUCT tTS_IB_COMMON_ATTRIB_NOTICE_STRUCT,
  *tTS_IB_COMMON_ATTRIB_NOTICE;
typedef struct tTS_IB_COMMON_ATTRIB_INFORM_STRUCT tTS_IB_COMMON_ATTRIB_INFORM_STRUCT,
  *tTS_IB_COMMON_ATTRIB_INFORM;
typedef struct tTS_IB_COMMON_ATTRIB_SERVICE_STRUCT tTS_IB_COMMON_ATTRIB_SERVICE_STRUCT,
  *tTS_IB_COMMON_ATTRIB_SERVICE;

enum {
  TS_IB_COMMON_NOTICE_GENERIC       = 1,
  TS_IB_COMMON_INFORM_INFO_GENERIC  = 1
};

enum {
  TS_IB_COMMON_INFORM_UNSUBSCRIBE   = 0,
  TS_IB_COMMON_INFORM_SUBSCRIBE     = 1
};

enum {
  TS_IB_COMMON_NOTICE_TYPE_FATAL    = 0x0,
  TS_IB_COMMON_NOTICE_TYPE_URGENT   = 0x1,
  TS_IB_COMMON_NOTICE_TYPE_SECURITY = 0x2,
  TS_IB_COMMON_NOTICE_TYPE_SM       = 0x3,
  TS_IB_COMMON_NOTICE_TYPE_INFO     = 0x4,
  TS_IB_COMMON_NOTICE_TYPE_ALL      = 0xFFFF
};

enum {
  TS_IB_COMMON_TRAP_PRODUCER_TYPE_CA       = 0x1,
  TS_IB_COMMON_TRAP_PRODUCER_TYPE_SW       = 0x2,
  TS_IB_COMMON_TRAP_PRODUCER_TYPE_ROUTER   = 0x3,
  TS_IB_COMMON_TRAP_PRODUCER_TYPE_SM       = 0x4
};

enum {
  TS_IB_GENERIC_TRAP_NUM_IN_SVC                     = 64,
  TS_IB_GENERIC_TRAP_NUM_OUT_OF_SVC                 = 65,
  TS_IB_GENERIC_TRAP_NUM_CREATE_MC_GRP              = 66,
  TS_IB_GENERIC_TRAP_NUM_DELETE_MC_GRP              = 67,
  TS_IB_GENERIC_TRAP_NUM_PORT_CHANGE_STATE          = 128,
  TS_IB_GENERIC_TRAP_NUM_INTEGRITY_TRESHOLD_REACHED = 129,
  TS_IB_GENERIC_TRAP_NUM_EXCESSIVE_BUFFER_OVERRUN   = 130,
  TS_IB_GENERIC_TRAP_NUM_FLOW_CTRL_WATCHDOG_EXPIRED = 131,
  TS_IB_GENERIC_TRAP_NUM_BAD_M_KEY                  = 256,
  TS_IB_GENERIC_TRAP_NUM_BAD_P_KEY                  = 257,
  TS_IB_GENERIC_TRAP_NUM_BAD_Q_KEY                  = 258,
  TS_IB_GENERIC_TRAP_NUM_ALL                        = 0xFFFF
};

/* common attribute */
struct tTS_IB_COMMON_ATTRIB_CPI_STRUCT {
  uint8_t         base_version;
  uint8_t         class_version;
  uint16_t        capability_mask;
  uint32_t        resp_time;
  tTS_IB_GID      redirect_gid;
  uint32_t        redirect_tc;
  uint32_t        redirect_sl;
  uint32_t        redirect_fl;
  tTS_IB_LID      redirect_lid;
  tTS_IB_PKEY     redirect_pkey;
  tTS_IB_QPN      redirect_qpn;
  tTS_IB_QKEY     redirect_qkey;
  tTS_IB_GID      trap_gid;
  uint32_t        trap_tc;
  uint32_t        trap_sl;
  uint32_t        trap_fl;
  tTS_IB_LID      trap_lid;
  tTS_IB_PKEY     trap_pkey;
  uint32_t        trap_hop_limit;
  uint32_t        trap_qp;
  tTS_IB_QKEY     trap_qkey;
};

struct tTS_IB_COMMON_ATTRIB_NOTICE_STRUCT {
  union {
    struct {
      uint32_t    type;
      uint32_t    producer_type;
      uint16_t    trap_num;
      tTS_IB_LID  issuer_lid;
    } generic;
    struct {
      uint32_t    type;
      uint32_t    vendor_id;
      uint16_t    device_type;
      tTS_IB_LID  issuer_lid;
    } vendor;
  } define;
  uint16_t        toggle;
  uint16_t        count;
  union {
    uint8_t       data[54];
    struct {
      uint8_t     reserved[6];
      tTS_IB_GID  gid;
      uint8_t     padding[32];
    } sm_trap;
  } detail;
  tTS_IB_GID      issuer_gid;
};

struct tTS_IB_COMMON_ATTRIB_INFORM_STRUCT {
  tTS_IB_GID      gid;
  tTS_IB_LID      lid_range_begin;
  tTS_IB_LID      lid_range_end;
  uint16_t        reserved;
  uint8_t         is_generic;
  uint8_t         subscribe;
  uint16_t        type;
  union {
    struct {
      uint16_t    trap_num;
      uint32_t    resp_time;
      uint32_t    producer_type;
    } generic;
    struct {
      uint16_t    device_id;
      uint32_t    resp_time;
      uint32_t    vendor_id;
    } vendor;
  } define;
};

struct tTS_IB_COMMON_ATTRIB_SERVICE_STRUCT {
  uint64_t        service_id;
  tTS_IB_GID      service_gid;
  tTS_IB_PKEY     service_pkey;

  uint16_t        reserved;

  uint32_t        service_lease;

  uint8_t         service_key[16];

  uint8_t         service_name[64];

  uint8_t         service_data8[16];
  uint16_t        service_data16[8];
  uint32_t        service_data32[4];
  uint64_t        service_data64[2];
};

#endif /*_TS_IB_CLIENT_QUERY_TYPES_H */
