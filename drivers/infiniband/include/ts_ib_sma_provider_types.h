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

  $Id: ts_ib_sma_provider_types.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_IB_SMA_PROVIDER_TYPES_H
#define _TS_IB_SMA_PROVIDER_TYPES_H

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

#include "ts_ib_magic.h"
#include "ts_ib_core_types.h"
#include "ts_ib_mad_types.h"
#include "ts_ib_provider_types.h"
#include "ts_ib_mad_smi_types.h"

/* type definitions */

/* PortInfo:CapabilityMask Flags. */
typedef enum {
  TS_IB_SMA_CAP_MASK_IS_SM                     = 1 << 1,
  TS_IB_SMA_CAP_MASK_NOTICE                    = 1 << 2,
  TS_IB_SMA_CAP_MASK_TRAP                      = 1 << 3,
  TS_IB_SMA_CAP_MASK_AUTO_MIGRATION            = 1 << 5,
  TS_IB_SMA_CAP_MASK_SL_VL_MAP                 = 1 << 6,
  TS_IB_SMA_CAP_MASK_M_KEY_NVRAM               = 1 << 7,
  TS_IB_SMA_CAP_MASK_P_KEY_NVRAM               = 1 << 8,
  TS_IB_SMA_CAP_MASK_LED_INFO                  = 1 << 9,
  TS_IB_SMA_CAP_MASK_SM_DISABLED               = 1 << 10,
  TS_IB_SMA_CAP_MASK_SYS_IMAGE_GUID            = 1 << 11,
  TS_IB_SMA_CAP_MASK_P_KEY_SW_EXTERN_PORT_TRAP = 1 << 12,
  TS_IB_SMA_CAP_MASK_CM                        = 1 << 16,
  TS_IB_SMA_CAP_MASK_SNMP_TUNNEL               = 1 << 17,
  TS_IB_SMA_CAP_MASK_REINIT                    = 1 << 18,
  TS_IB_SMA_CAP_MASK_DEV_MGMT                  = 1 << 19,
  TS_IB_SMA_CAP_MASK_VENDOR_CLASS              = 1 << 20,
  TS_IB_SMA_CAP_MASK_DR_NOTICE                 = 1 << 21,
  TS_IB_SMA_CAP_MASK_CAP_MASK_NOTICE           = 1 << 22,
  TS_IB_SMA_CAP_MASK_BOOT_MGMT                 = 1 << 23
} tTS_IB_SMA_CAP_MASK;

/* Flags for possible services provided. */
typedef enum {
  TS_IB_SMA_PROVIDER_MAD        = 1 << 0,
  TS_IB_SMA_PROVIDER_FUNCTION   = 1 << 1,
  TS_IB_SMA_PROVIDER_CA         = 1 << 2,
  TS_IB_SMA_PROVIDER_SWITCH     = 1 << 3,
  TS_IB_SMA_PROVIDER_SL_VL_MAP  = 1 << 4,
  TS_IB_SMA_PROVIDER_VL_ARB     = 1 << 5,
  TS_IB_SMA_PROVIDER_LINEAR_FWD = 1 << 6,
  TS_IB_SMA_PROVIDER_RANDOM_FWD = 1 << 7,
  TS_IB_SMA_PROVIDER_MCAST_FWD  = 1 << 8,
  TS_IB_SMA_PROVIDER_LED_INFO   = 1 << 9
} tTS_IB_SMA_PROVIDER_FLAGS;

/* A return code for the provider functions */
typedef enum {
  TS_IB_SMA_SUCCESS,        // Everything is good.
  TS_IB_SMA_FAILURE,        // Major local failure
  TS_IB_SMA_INVAL_VER,      // Base/Class version had invalid value
  TS_IB_SMA_INVAL_METHOD,   // Invalid method
  TS_IB_SMA_INVAL_ATTRIB,   // Invalid attribute number for the method
  TS_IB_SMA_INVAL_A_MOD,    // Invalid attribute modifier
  TS_IB_SMA_INVAL_A_FIELD,  // Invalid field in the attribute data
  TS_IB_SMA_INVAL_M_KEY,    // M_Key match failed.
  TS_IB_SMA_IGNORE,         // SMA should ignore packet, but not an error.
} tTS_IB_SMA_RESULT;

/* PHY LINK states needed by the link port-state state-machine */
typedef enum {
  TS_IB_SMA_PHY_LINK_DOWN,
  TS_IB_SMA_PHY_LINK_UP
} tTS_IB_SMA_PHY_LINK_STATE;

/* Return value from the link port-state update routine */
typedef enum {
  TS_IB_SMA_PORT_UPDATE_INV   = 1 << 0,   // Requested transition was invalid.
  TS_IB_SMA_PORT_UPDATE_RESET = 1 << 1    // Port went through reset transition
} tTS_IB_SMA_PORT_UPDATE_RESULT;

/* A listing of the possible outcomes of checking an SMP's M_Key.
 * NOTE: order is important.  There are cases where we need to take the less
 *       permissive of two M_KEY checks, and that's done using a numerical
 *       comparison.
 */
typedef enum {
  TS_IB_SMA_M_KEY_FULL,         // Full privledges
  TS_IB_SMA_M_KEY_GET_OKAY,     // SubnGet operations are okay
  TS_IB_SMA_M_KEY_GET_NO_MKEY,  // Same, but don't tell M_Key in PortInfo
  TS_IB_SMA_M_KEY_FAIL          // No operations allowed.
} tTS_IB_SMA_M_KEY_MATCH;

typedef struct tTS_IB_SMA_PROVIDER_STRUCT tTS_IB_SMA_PROVIDER_STRUCT,
  *tTS_IB_SMA_PROVIDER;

typedef struct tTS_IB_SMA_DEV_PORT_INFO_STRUCT tTS_IB_SMA_DEV_PORT_INFO_STRUCT,
  *tTS_IB_SMA_DEV_PORT_INFO;

/*
 * Function type declarations.
 */
typedef ib_mad_process_func tTS_IB_SMA_MAD_FUNCTION;

typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_NODE_DESC_QUERY_FUNCTION)(
                                        tTS_IB_SMA_PROVIDER      provider,
                                        tTS_IB_SMP_NODE_DESC     node_desc);
typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_NODE_DESC_MODIFY_FUNCTION)(
                                        tTS_IB_SMA_PROVIDER       provider,
                                        tTS_IB_SMP_NODE_DESC      node_desc);

typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_NODE_INFO_QUERY_FUNCTION)(
                                        tTS_IB_SMA_PROVIDER      provider,
                                        tTS_IB_PORT              port,
                                        tTS_IB_SMP_NODE_INFO     node_info);
typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_NODE_INFO_MODIFY_FUNCTION)(
                                        tTS_IB_SMA_PROVIDER       provider,
                                        tTS_IB_PORT               port,
                                        tTS_IB_SMP_NODE_INFO      node_info);

typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_SWITCH_INFO_QUERY_FUNCTION)(
                                        tTS_IB_SMA_PROVIDER        provider,
                                        tTS_IB_SMP_SWITCH_INFO     switch_info);
typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_SWITCH_INFO_MODIFY_FUNCTION)(
                                        tTS_IB_SMA_PROVIDER         provider,
                                        tTS_IB_SMP_SWITCH_INFO      switch_info);

typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_GUID_INFO_QUERY_FUNCTION)(
                                        tTS_IB_SMA_PROVIDER      provider,
                                        tTS_IB_PORT              port,
                                        int                      guid_index,
                                        tTS_IB_SMP_GUID_INFO     guid_info);
typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_GUID_INFO_MODIFY_FUNCTION)(
                                        tTS_IB_SMA_PROVIDER       provider,
                                        tTS_IB_PORT               port,
                                        int                       guid_index,
                                        tTS_IB_SMP_GUID_INFO      guid_info);

typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_PORT_INFO_QUERY_FUNCTION)(
                                        tTS_IB_SMA_PROVIDER      provider,
                                        tTS_IB_PORT              port,
                                        tTS_IB_SMP_PORT_INFO     port_info,
                                        tTS_IB_SMA_M_KEY_MATCH   mkey_match,
                                        int                      cache_ok
                                       );
typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_PORT_INFO_MODIFY_FUNCTION)(
                                        tTS_IB_SMA_PROVIDER       provider,
                                        tTS_IB_PORT               port,
                                        tTS_IB_SMP_PORT_INFO      port_info);

typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_PKEY_QUERY_FUNCTION)(
                                    tTS_IB_SMA_PROVIDER     provider,
                                    tTS_IB_PORT             port,
                                    uint32_t                pkey_index,
                                    tTS_IB_SMP_PKEY_TABLE   pkey_table);
typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_PKEY_MODIFY_FUNCTION)(
                                    tTS_IB_SMA_PROVIDER      provider,
                                    tTS_IB_PORT              port,
                                    uint32_t                 pkey_index,
                                    tTS_IB_SMP_PKEY_TABLE    pkey_table);

typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_SL_VL_MAP_QUERY_FUNCTION)(
                                        tTS_IB_SMA_PROVIDER      provider,
                                        tTS_IB_PORT              in_port,
                                        tTS_IB_PORT              out_port,
                                        tTS_IB_SMP_SL_VL_MAP     sl_vl_map);
typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_SL_VL_MAP_MODIFY_FUNCTION)(
                                        tTS_IB_SMA_PROVIDER       provider,
                                        tTS_IB_PORT               in_port,
                                        tTS_IB_PORT               out_port,
                                        tTS_IB_SMP_SL_VL_MAP      sl_vl_map);

typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_VL_ARB_QUERY_FUNCTION)(
                                    tTS_IB_SMA_PROVIDER       provider,
                                    int                       table_id,
                                    tTS_IB_PORT               port,
                                    tTS_IB_SMP_VL_ARB_TABLE   vl_arb);
typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_VL_ARB_MODIFY_FUNCTION)(
                                    tTS_IB_SMA_PROVIDER        provider,
                                    int                        table_id,
                                    tTS_IB_PORT                port,
                                    tTS_IB_SMP_VL_ARB_TABLE    vl_arb);

typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_LINEAR_FWD_QUERY_FUNCTION)(
                                    tTS_IB_SMA_PROVIDER           provider,
                                    int                           offset,
                                    tTS_IB_SMP_LINEAR_FWD_TABLE   fwd_table);
typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_LINEAR_FWD_MODIFY_FUNCTION)(
                                    tTS_IB_SMA_PROVIDER            provider,
                                    int                            offset,
                                    tTS_IB_SMP_LINEAR_FWD_TABLE    fwd_table);

typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_RANDOM_FWD_QUERY_FUNCTION)(
                                    tTS_IB_SMA_PROVIDER           provider,
                                    int                           offset,
                                    tTS_IB_SMP_RANDOM_FWD_TABLE   fwd_table);
typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_RANDOM_FWD_MODIFY_FUNCTION)(
                                    tTS_IB_SMA_PROVIDER            provider,
                                    int                            offset,
                                    tTS_IB_SMP_RANDOM_FWD_TABLE    fwd_table);

/* For the multicast forwarding table entries, the attribute modifier
 * provides 2 interesting values:
 *   port_base is the lowest port to which the 16-bit masks in the table apply.
 *             this must be a whole number multiple of 16.
 *   mlid_base is the number of the first multicast LID to which the
 *               entries in the table apply.
 */
typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_MCAST_FWD_QUERY_FUNCTION)(
                                    tTS_IB_SMA_PROVIDER          provider,
                                    tTS_IB_PORT                  port_base,
                                    uint16_t                     mlid_base,
                                    tTS_IB_SMP_MCAST_FWD_TABLE   fwd_table);
typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_MCAST_FWD_MODIFY_FUNCTION)(
                                    tTS_IB_SMA_PROVIDER           provider,
                                    tTS_IB_PORT                   port_base,
                                    uint16_t                      mlid_base,
                                    tTS_IB_SMP_MCAST_FWD_TABLE    fwd_table);

typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_VENDOR_DIAG_QUERY_FUNCTION)(
                                        tTS_IB_SMA_PROVIDER        provider,
                                        uint16_t                   index,
                                        tTS_IB_SMP_VENDOR_DIAG     diag_info);
typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_VENDOR_DIAG_MODIFY_FUNCTION)(
                                        tTS_IB_SMA_PROVIDER         provider,
                                        uint16_t                    index,
                                        tTS_IB_SMP_VENDOR_DIAG      diag_info);

typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_LED_INFO_QUERY_FUNCTION)(
                                        tTS_IB_SMA_PROVIDER     provider,
                                        tTS_IB_SMP_LED_INFO     led_info);
typedef tTS_IB_SMA_RESULT (*tTS_IB_SMA_LED_INFO_MODIFY_FUNCTION)(
                                        tTS_IB_SMA_PROVIDER      provider,
                                        tTS_IB_SMP_LED_INFO      led_info);

/*
 * structures
 */

/*
 * The part of the SMP Port Info structure that needs to be maintained
 * by the device-specific SMA.
 */
struct tTS_IB_SMA_DEV_PORT_INFO_STRUCT {
  uint8_t           gid_prefix[8];
  tTS_IB_LID        lid;
  uint32_t          capability_mask;
  uint8_t           link_width_enabled;
  uint8_t           link_width_supported;
  uint8_t           link_width_active;
  uint8_t           link_speed_supported;
  tTS_IB_PORT_STATE port_state;
  uint8_t           port_physical_state;
  uint8_t           link_down_default_state;
  uint8_t           lmc;
  uint8_t           link_speed_active;
  uint8_t           link_speed_enabled;
  uint8_t           neighbor_mtu;
  uint8_t           vl_cap;
  uint8_t           vl_high_limit;
  uint8_t           vl_arb_hi_cap;
  uint8_t           vl_arb_low_cap;
  uint8_t           mtu_cap;
  uint8_t           vl_stall_count;
  uint8_t           hoq_life;
  uint8_t           operational_vls;
  uint8_t           partition_enf_inbound;
  uint8_t           partition_enf_outbound;
  uint8_t           filter_raw_inbound;
  uint8_t           filter_raw_outbound;
  uint16_t          p_key_violations;
  uint16_t          q_key_violations;
  uint8_t           guid_cap;
  uint8_t           local_phy_errors;
  uint8_t           overrun_errors;
};

/* The provider structure that a device-specific SMA needs to fill in. */
struct tTS_IB_SMA_PROVIDER_STRUCT {
  TS_IB_DECLARE_MAGIC

  tTS_IB_DEVICE                          device;
  tTS_IB_SMA_PROVIDER_FLAGS              flags;
  void                                  *sma;   // Generic SMA use
  void                                  *priv;  // SMA provider private use

  tTS_IB_SMA_MAD_FUNCTION                mad_handler;
  tTS_IB_SMA_NODE_DESC_QUERY_FUNCTION    node_desc_query;
  tTS_IB_SMA_NODE_DESC_MODIFY_FUNCTION   node_desc_modify;
  tTS_IB_SMA_NODE_INFO_QUERY_FUNCTION    node_info_query;
  tTS_IB_SMA_NODE_INFO_MODIFY_FUNCTION   node_info_modify;
  tTS_IB_SMA_SWITCH_INFO_QUERY_FUNCTION  switch_info_query;
  tTS_IB_SMA_SWITCH_INFO_MODIFY_FUNCTION switch_info_modify;
  tTS_IB_SMA_GUID_INFO_QUERY_FUNCTION    guid_info_query;
  tTS_IB_SMA_GUID_INFO_MODIFY_FUNCTION   guid_info_modify;
  tTS_IB_SMA_PORT_INFO_QUERY_FUNCTION    port_info_query;
  tTS_IB_SMA_PORT_INFO_MODIFY_FUNCTION   port_info_modify;
  tTS_IB_SMA_PKEY_QUERY_FUNCTION         pkey_query;
  tTS_IB_SMA_PKEY_MODIFY_FUNCTION        pkey_modify;
  tTS_IB_SMA_SL_VL_MAP_QUERY_FUNCTION    sl_vl_map_query;
  tTS_IB_SMA_SL_VL_MAP_MODIFY_FUNCTION   sl_vl_map_modify;
  tTS_IB_SMA_VL_ARB_QUERY_FUNCTION       vl_arb_query;
  tTS_IB_SMA_VL_ARB_MODIFY_FUNCTION      vl_arb_modify;
  tTS_IB_SMA_LINEAR_FWD_QUERY_FUNCTION   linear_fwd_query;
  tTS_IB_SMA_LINEAR_FWD_MODIFY_FUNCTION  linear_fwd_modify;
  tTS_IB_SMA_RANDOM_FWD_QUERY_FUNCTION   random_fwd_query;
  tTS_IB_SMA_RANDOM_FWD_MODIFY_FUNCTION  random_fwd_modify;
  tTS_IB_SMA_MCAST_FWD_QUERY_FUNCTION    mcast_fwd_query;
  tTS_IB_SMA_MCAST_FWD_MODIFY_FUNCTION   mcast_fwd_modify;
  tTS_IB_SMA_VENDOR_DIAG_QUERY_FUNCTION  vendor_diag_query;
  tTS_IB_SMA_VENDOR_DIAG_MODIFY_FUNCTION vendor_diag_modify;
  tTS_IB_SMA_LED_INFO_QUERY_FUNCTION     led_info_query;
  tTS_IB_SMA_LED_INFO_MODIFY_FUNCTION    led_info_modify;
};

#endif /* _TS_IB_SMA_PROVIDER_TYPES_H */
