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

  $Id: ts_ib_mad_types.h 58 2004-04-16 02:09:40Z roland $
*/

#ifndef _TS_IB_MAD_TYPES_H
#define _TS_IB_MAD_TYPES_H

#if defined(__KERNEL__)
#ifndef W2K_OS
#  include <linux/types.h>
#  include <linux/list.h>
#endif
#else
#ifndef W2K_OS
#  include <stdint.h>
#endif
#  include <stddef.h>             /* for size_t */
#endif

#ifdef W2K_OS
#include <all/common/include/w2k.h>
#endif

#include "ts_ib_core_types.h"

/* type definitions */

typedef void *tTS_IB_MAD_FILTER_HANDLE;

typedef struct ib_mad tTS_IB_MAD_STRUCT, *tTS_IB_MAD;
typedef struct ib_mad_filter tTS_IB_MAD_FILTER_STRUCT,
	*tTS_IB_MAD_FILTER;

/* enum definitions */

#define TS_IB_GSI_WELL_KNOWN_QKEY 0x80010000UL
#define TS_IB_MAD_FILTER_NAME_MAX 32

#define TS_IB_MAD_DR_DIRECTION_RETURN  0x8000

#define TS_IB_MAD_DR_RETURNING(mad)                         \
  ((mad)->status & cpu_to_be16(TS_IB_MAD_DR_DIRECTION_RETURN))
#define TS_IB_MAD_DR_OUTGOING(mad) (!(TS_IB_MAD_DR_RETURNING(mad)))

/* 13.4.4 */
typedef enum ib_mgmt_class {
	TS_IB_MGMT_CLASS_SUBN_LID_ROUTED     = 0x01,
	TS_IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE = 0x81,
	TS_IB_MGMT_CLASS_SUBN_ADM            = 0x03,
	TS_IB_MGMT_CLASS_PERF                = 0x04,
	TS_IB_MGMT_CLASS_BM                  = 0x05,
	TS_IB_MGMT_CLASS_DEV_MGT             = 0x06,
	TS_IB_MGMT_CLASS_COMM_MGT            = 0x07,
	TS_IB_MGMT_CLASS_SNMP                = 0x08,
	TS_IB_MGMT_CLASS_VENDOR_TOPSPIN      = 0x30
} tTS_IB_MGMT_CLASS;

/* 13.4.5 */
typedef enum ib_mgmt_method {
	TS_IB_MGMT_METHOD_GET             = 0x01,
	TS_IB_MGMT_METHOD_SET             = 0x02,
	TS_IB_MGMT_METHOD_GET_RESPONSE    = 0x81,
	TS_IB_MGMT_METHOD_SEND            = 0x03,
	TS_IB_MGMT_METHOD_TRAP            = 0x05,
	TS_IB_MGMT_METHOD_REPORT          = 0x06,
	TS_IB_MGMT_METHOD_REPORT_RESPONSE = 0x86,
	TS_IB_MGMT_METHOD_TRAP_REPRESS    = 0x07
} tTS_IB_MGMT_METHOD;

typedef enum ib_mad_filter_mask {
	TS_IB_MAD_FILTER_DEVICE       = 1 << 0,
	TS_IB_MAD_FILTER_PORT         = 1 << 1,
	TS_IB_MAD_FILTER_QPN          = 1 << 2,
	TS_IB_MAD_FILTER_MGMT_CLASS   = 1 << 3,
	TS_IB_MAD_FILTER_R_METHOD     = 1 << 4,
	TS_IB_MAD_FILTER_ATTRIBUTE_ID = 1 << 5,
	TS_IB_MAD_FILTER_DIRECTION    = 1 << 6
} tTS_IB_MAD_FILTER_MASK;

typedef enum ib_mad_direction {
	TS_IB_MAD_DIRECTION_IN,
	TS_IB_MAD_DIRECTION_OUT
} tTS_IB_MAD_DIRECTION;

typedef enum ib_mad_result {
	TS_IB_MAD_RESULT_FAILURE      = 0,        // (!SUCCESS is the important flag)
	TS_IB_MAD_RESULT_SUCCESS      = 1 << 0,   // MAD was successfully processed
	TS_IB_MAD_RESULT_REPLY        = 1 << 1,   // Reply packet needs to be sent
	TS_IB_MAD_RESULT_CONSUMED     = 1 << 2    // Packet consumed: stop processing
} tTS_IB_MAD_RESULT;

/* function types */

typedef void (*tTS_IB_MAD_COMPLETION_FUNCTION)(int result,
                                               void *arg);

typedef void (*tTS_IB_MAD_DISPATCH_FUNCTION)(tTS_IB_MAD mad,
                                             void *arg);

/* structs */

struct ib_mad {
	uint8_t                        format_version     __attribute__((packed));
	uint8_t                        mgmt_class         __attribute__((packed));
	uint8_t                        class_version      __attribute__((packed));
	uint8_t                        r_method           __attribute__((packed));
	uint16_t                       status             __attribute__((packed));
	union {
		struct {
			uint8_t        hop_pointer        __attribute__((packed));
			uint8_t        hop_count          __attribute__((packed));
		}                      directed           __attribute__((packed));
		struct {
			uint16_t       class_specific     __attribute__((packed));
		}                      lid                __attribute__((packed));
	}                              route              __attribute__((packed));
	uint64_t                       transaction_id     __attribute__((packed));
	uint16_t                       attribute_id       __attribute__((packed));
	uint16_t                       reserved           __attribute__((packed));
	uint32_t                       attribute_modifier __attribute__((packed));
	uint8_t                        payload[232]       __attribute__((packed));

	tTS_IB_DEVICE_HANDLE           device;
	tTS_IB_PORT                    port;
	tTS_IB_SL                      sl;
	int                            pkey_index;
	tTS_IB_LID                     slid;
	tTS_IB_LID                     dlid;
	tTS_IB_QPN                     sqpn;
	tTS_IB_QPN                     dqpn;
	int                            use_grh;
	tTS_IB_MAD_COMPLETION_FUNCTION completion_func;
	void                          *arg;

	/*
	  struct list_head _must_ be the _last_ element of the structure so
	  that we can copy between userspace and the kernel
	*/
#ifdef __KERNEL__
	struct list_head               list;
#endif
};

struct ib_mad_filter {
	tTS_IB_DEVICE_HANDLE     device;
	tTS_IB_PORT              port;
	tTS_IB_QPN               qpn;
	uint8_t                  mgmt_class;
	uint8_t                  r_method;
	uint16_t                 attribute_id;
	tTS_IB_MAD_DIRECTION     direction;
	tTS_IB_MAD_FILTER_MASK   mask;
	char                     name[TS_IB_MAD_FILTER_NAME_MAX];
};

#endif /* _TS_IB_MAD_TYPES_H */

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
