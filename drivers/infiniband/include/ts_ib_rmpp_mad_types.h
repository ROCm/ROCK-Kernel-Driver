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

  $Id: ts_ib_rmpp_mad_types.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_IB_RMPP_MAD_TYPES_H
#define _TS_IB_RMPP_MAD_TYPES_H

#include "ts_ib_core_types.h"

#ifdef __KERNEL__
/* Use for linked lists of MAD packets in kernel driver */
#ifndef W2K_OS
#  include <linux/list.h>
#else
#  include <os_dep/win/linux/list.h>
#endif
#endif

/* constants */
enum {
  TS_IB_CLIENT_RMPP_SUPPORTED_VERSION = 1
};

enum {
  TS_IB_CLIENT_RMPP_PAYLOAD_LENGTH = 220
};

/*
  type definitions
*/
typedef struct tTS_IB_CLIENT_RMPP_MAD_STRUCT tTS_IB_CLIENT_RMPP_MAD_STRUCT,
  *tTS_IB_CLIENT_RMPP_MAD;

typedef enum {
  TS_IB_CLIENT_RMPP_TYPE_DATA  = 1,
  TS_IB_CLIENT_RMPP_TYPE_ACK   = 2,
  TS_IB_CLIENT_RMPP_TYPE_STOP  = 3,
  TS_IB_CLIENT_RMPP_TYPE_ABORT = 4
} tTS_IB_CLIENT_RMPP_TYPE;

typedef enum {
  TS_IB_CLIENT_RMPP_FLAG_NON_ACTIVE = 0x0,
  TS_IB_CLIENT_RMPP_FLAG_ACTIVE     = 0x1,
  TS_IB_CLIENT_RMPP_FLAG_DATA_FIRST = 0x2,
  TS_IB_CLIENT_RMPP_FLAG_DATA_LAST  = 0x4
} tTS_IB_CLIENT_RMPP_FLAG;

typedef enum {
  TS_IB_CLIENT_RMPP_STATUS_NORMAL                            = 0,   /* data, ack */
  TS_IB_CLIENT_RMPP_STATUS_RESOURCE_EXHAUST                  = 1,   /* stop */
  TS_IB_CLIENT_RMPP_STATUS_TRANSACTION_TIME_EXCEED           = 118, /* abort */
  TS_IB_CLIENT_RMPP_STATUS_INCONSISTENT_LAST_AND_PAYLOAD_LEN = 119, /* abort */
  TS_IB_CLIENT_RMPP_STATUS_INCONSISTENT_FIRST_AND_SEG_NUM    = 120, /* abort */
  TS_IB_CLIENT_RMPP_STATUS_BAD_RMPP_TYPE                     = 121, /* abort */
  TS_IB_CLIENT_RMPP_STATUS_NEW_WINDOW_NUM_TOO_SMALL          = 122, /* abort */
  TS_IB_CLIENT_RMPP_STATUS_SEG_NUM_TOO_BIG                   = 123, /* abort */
  TS_IB_CLIENT_RMPP_STATUS_ILLEGAL_STATUS                    = 124, /* abort */
  TS_IB_CLIENT_RMPP_STATUS_UNSUPPORTED_VERSION               = 125, /* abort */
  TS_IB_CLIENT_RMPP_STATUS_TOO_MANY_RETRIES                  = 126, /* abort */
  TS_IB_CLIENT_RMPP_STATUS_UNSPECIFIED                       = 127, /* abort */
} tTS_IB_CLIENT_RMPP_STATUS;

typedef struct tTS_IB_CLIENT_RMPP_RCV_STRUCT tTS_IB_CLIENT_RMPP_RCV_STRUCT,
  *tTS_IB_CLIENT_RMPP_RCV;

/* structure definitions */
struct tTS_IB_CLIENT_RMPP_MAD_STRUCT {
  uint8_t                                 standard_mad_header[24];
  uint8_t                                 version;
  uint8_t                                 type;
  uint8_t                                 resp_time__flags;
  uint8_t                                 status;
  union {
    struct {
      uint32_t                            segment_number;
      uint32_t                            payload_length;
    } data;
    struct {
      uint32_t                            segment_number;
      uint32_t                            new_window_last;
    } ack;
    struct {
      uint32_t                            reserved1;
      uint32_t                            reserved2;
    } abort_stop;
  } specific;
  uint8_t                                 payload[TS_IB_CLIENT_RMPP_PAYLOAD_LENGTH];
} __attribute__((packed));


#endif /* _TS_IB_RMPP_MAD_TYPES_H */
