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

  $Id: ts_ib_mad_smi_types.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_IB_MAD_SMI_TYPES_H
#define _TS_IB_MAD_SMI_TYPES_H

#include "ts_ib_mad_types.h"

/* Convert the MAD payload to an SMP structure (LID-routed) */
#define TS_IB_MAD_SMP_PAYLOAD(mad) ((tTS_IB_MAD_PAYLOAD_SMP)(mad)->payload)
/* Directed route payload */
#define TS_IB_MAD_SMP_DR_PAYLOAD(mad) \
  ((tTS_IB_MAD_PAYLOAD_SMP_DR)(mad)->payload)

/* Macro to get the SMP DATA field from a MAD payload
 * (Same for LID or Directed Route SMPs.)
 */
#define TS_IB_MAD_SMP_DATA(mad) \
  (((tTS_IB_MAD_PAYLOAD_SMP)(mad)->payload)->smp_data)

/*
 * Typedefs
 */

/* 14.2.5 -- SMI Attributes */
enum tTS_IB_SMI_MGT_ATTRIBUTE_ID {
  TS_IB_SMI_MGT_NOTICE      = 0x0002,
  TS_IB_SMI_MGT_NODE_DESC   = 0x0010,
  TS_IB_SMI_MGT_NODE_INFO   = 0x0011,
  TS_IB_SMI_MGT_SWITCH_INFO = 0x0012,
  TS_IB_SMI_MGT_GUID_INFO   = 0x0014,
  TS_IB_SMI_MGT_PORT_INFO   = 0x0015,
  TS_IB_SMI_MGT_PKEY_TABLE  = 0x0016,
  TS_IB_SMI_MGT_SL_VL_MAP   = 0x0017,
  TS_IB_SMI_MGT_VL_ARB      = 0x0018,
  TS_IB_SMI_MGT_LINEAR_FWD  = 0x0019,
  TS_IB_SMI_MGT_RANDOM_FWD  = 0x001A,
  TS_IB_SMI_MGT_MCAST_FWD   = 0x001B,
  TS_IB_SMI_MGT_SMINFO      = 0x0020,
  TS_IB_SMI_MGT_VENDOR_DIAG = 0x0030,
  TS_IB_SMI_MGT_LED_INFO    = 0x0031
};

typedef struct tTS_IB_MAD_PAYLOAD_SMP_STRUCT tTS_IB_MAD_PAYLOAD_SMP_STRUCT,
  *tTS_IB_MAD_PAYLOAD_SMP;

typedef struct tTS_IB_MAD_PAYLOAD_SMP_DR_STRUCT
  tTS_IB_MAD_PAYLOAD_SMP_DR_STRUCT, *tTS_IB_MAD_PAYLOAD_SMP_DR;

/*
 * The overall structure of the mad payload for all SMP packets.
 */

struct tTS_IB_MAD_PAYLOAD_SMP_STRUCT {
  tTS_IB_MKEY     mkey;
  uint8_t         reserved1[32];
  uint8_t         smp_data[64];
  uint8_t         reserved2[128];
} __attribute__((packed));

struct tTS_IB_MAD_PAYLOAD_SMP_DR_STRUCT {
  tTS_IB_MKEY     mkey;
  tTS_IB_LID      dr_slid;
  tTS_IB_LID      dr_dlid;
  uint8_t         reserved1[28];
  uint8_t         smp_data[64];
  uint8_t         initial_path[64];
  uint8_t         return_path[64];
} __attribute__((packed));

#endif /* _TS_IB_MAD_SMI_TYPES_H */
