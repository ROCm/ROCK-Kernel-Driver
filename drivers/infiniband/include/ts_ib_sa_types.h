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

  $Id: ts_ib_sa_types.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_IB_SA_TYPES_H
#define _TS_IB_SA_TYPES_H

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

#include "ts_ib_client_query_types.h"

typedef uint8_t  tTS_IB_LINK_WIDTH;

typedef struct tTS_IB_MULTICAST_MEMBER_STRUCT tTS_IB_MULTICAST_MEMBER_STRUCT,
  *tTS_IB_MULTICAST_MEMBER;
typedef struct tTS_IB_NODE_INFO_STRUCT tTS_IB_NODE_INFO_STRUCT,
  *tTS_IB_NODE_INFO;
typedef struct tTS_IB_PORT_INFO_STRUCT tTS_IB_PORT_INFO_STRUCT,
  *tTS_IB_PORT_INFO;

typedef enum {
  TS_IB_NODE_TYPE_CA     = 1,
  TS_IB_NODE_TYPE_SW     = 2,
  TS_IB_NODE_TYPE_ROUTER = 3
} tTS_IB_NODE_TYPE;

typedef enum {
  TS_IB_LINK_SPEED_NOP  = 0,
  TS_IB_LINK_SPEED_2GB5 = 1
} tTS_IB_LINK_SPEED;

typedef enum {
  TS_IB_PHY_NOP             = 0,
  TS_IB_PHY_SLEEP           = 1,
  TS_IB_PHY_POLLING         = 2,
  TS_IB_PHY_DISABLED        = 3,
  TS_IB_PHY_CONF_TRAINING   = 4,
  TS_IB_PHY_LINK_UP         = 5,
  TS_IB_PHY_LINK_ERROR_REC0 = 6
} tTS_IB_PHY_STATE;

typedef enum {
  TS_IB_VL_0    = 1,
  TS_IB_VL_0_1  = 2,
  TS_IB_VL_0_3  = 3,
  TS_IB_VL_0_7  = 4,
  TS_IB_VL_0_14 = 5
} tTS_IB_VL_CAP;

struct tTS_IB_MULTICAST_MEMBER_STRUCT {
  tTS_IB_GID  mgid;
  tTS_IB_QKEY qkey;
  tTS_IB_LID  mlid;
  tTS_IB_MTU  mtu;
  uint8_t     tclass;
  tTS_IB_PKEY pkey;
  tTS_IB_RATE rate;
  uint8_t     packet_life;
  tTS_IB_SL   sl;
  uint32_t    flowlabel;
  uint8_t     hoplmt;
};

struct tTS_IB_NODE_INFO_STRUCT {
  uint8_t          base_version;
  uint8_t          class_version;
  tTS_IB_NODE_TYPE node_type;
  uint8_t          num_ports;
  tTS_IB_GUID      system_image_guid;
  tTS_IB_GUID      node_guid;
  tTS_IB_GUID      port_guid;
  uint16_t         partition_cap;
  uint16_t         dev_id;
  uint32_t         dev_rev;
  tTS_IB_PORT      local_port_num;
  uint32_t         vendor_id;
};

struct tTS_IB_PORT_INFO_STRUCT {
  tTS_IB_MKEY       mkey;
  uint8_t           gid_prefix[8];
  tTS_IB_LID        lid;
  tTS_IB_LID        master_sm_lid;
  uint32_t          capability_mask;
  uint16_t          diag_code;
  uint16_t          mkey_lease_period;
  tTS_IB_PORT       local_port_num;
  tTS_IB_LINK_WIDTH link_width_enabled;
  tTS_IB_LINK_WIDTH link_width_supported;
  tTS_IB_LINK_WIDTH link_width_active;
  tTS_IB_LINK_SPEED link_speed_supported;
  tTS_IB_PORT_STATE port_state;
  tTS_IB_PHY_STATE  phy_state;
  tTS_IB_PHY_STATE  down_default_state;
  uint8_t           mkey_protect;
  uint8_t           lmc;
  tTS_IB_LINK_SPEED link_speed_active;
  tTS_IB_LINK_SPEED link_speed_enabled;
  tTS_IB_MTU        neighbor_mtu;
  tTS_IB_SL         master_sm_sl;
  tTS_IB_VL_CAP     vl_cap;
  uint8_t           vl_high_limit;
  uint8_t           vl_arb_high_cap;
  uint8_t           vl_arb_low_cap;
  tTS_IB_MTU        mtu_cap;
  uint8_t           vl_stall_count;
  uint8_t           hoq_life;
  tTS_IB_VL_CAP     operational_vl;
  uint8_t           partition_enforcement_inbound;
  uint8_t           partition_enforcement_outbound;
  uint8_t           filter_raw_inbound;
  uint8_t           filter_raw_outbound;
  uint16_t          mkey_violations;
  uint16_t          pkey_violations;
  uint16_t          qkey_violations;
  uint8_t           guid_cap;
  uint8_t           subnet_timeout;
  uint8_t           resp_time_val;
  uint8_t           local_phy_errs;
  uint8_t           overrun_errs;
};

/*
 * Accessor macros for port info capability mask
 */
#define TS_IB_PORT_CAP_SM      1         /* SM  */
#define TS_IB_PORT_CAP_NOTICE  2         /* Notice supported */
#define TS_IB_PORT_CAP_TRAP    3         /* Trap supported */
#define TS_IB_PORT_CAP_RESET   4         /* Reset supported */
#define TS_IB_PORT_CAP_APM     5         /* Alternate path migration */
#define TS_IB_PORT_CAP_SLMAP   6         /* SL mapping supported */
#define TS_IB_PORT_CAP_MKEY    7         /* Mkey in NVRAM */
#define TS_IB_PORT_CAP_PKEY    8         /* Pkey in NVRAM */
#define TS_IB_PORT_CAP_LED     9         /* LED info supported */
#define TS_IB_PORT_CAP_NOSM   10         /* SM is disabled */
#define TS_IB_PORT_CAP_CM     16         /* Communication manager suport */
#define TS_IB_PORT_CAP_SNMP   17         /* SNMP tunneling is supported */
#define TS_IB_PORT_CAP_DM     19         /* Device managment is supported */
#define TS_IB_PORT_CAP_VC     20         /* Vendor Class supported */
#define TS_IB_PORT_CAP_VALID  0x001B07FE /* valid capability fields */

#define TS_IB_PORT_CAP_GET(pi, value) \
        (((pi)->capability_mask & (0x1UL << value)) >> value)
#define TS_IB_PORT_CAP_SET(pi, value) \
        ((pi)->capability_mask = (pi)->capability_mask | (0x1UL << value))
#define TS_IB_PORT_CAP_CLR(pi, value) \
        ((pi)->capability_mask = (pi)->capability_mask & (~(0x1UL << value)))

#endif /* _TS_IB_SA_TYPES_H */
