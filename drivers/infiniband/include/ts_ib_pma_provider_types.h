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

  $Id: ts_ib_pma_provider_types.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_IB_PMA_PROVIDER_TYPES_H
#define _TS_IB_PMA_PROVIDER_TYPES_H

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

/* type definitions */

/* Flags for possible services provided. */
typedef enum {
  TS_IB_PMA_PROVIDER_MAD        = 1 << 0,
  TS_IB_PMA_PROVIDER_FUNCTION   = 1 << 1,
  TS_IB_PMA_PROVIDER_CA         = 1 << 2,
  TS_IB_PMA_PROVIDER_SWITCH     = 1 << 3,
} tTS_IB_PMA_PROVIDER_FLAGS;

typedef enum {
  TS_IB_PMA_FAILURE     = 0,        // Operation was unable to be performed.
  TS_IB_PMA_SUCCESS     = 1 << 0,   // Operation was performed.
  TS_IB_PMA_INVAL_FIELD = 1 << 1    // Operation contained at least 1 bad field.
} tTS_IB_PMA_RESULT;

/* Possible counters selected for setting in the set PortCounters operation */
typedef enum {
  TS_IB_PMA_PORT_COUNTERS_SELECT_SYMBOL_ERROR                 = 1 <<  0,
  TS_IB_PMA_PORT_COUNTERS_SELECT_LINK_ERR_RECOVERY            = 1 <<  1,
  TS_IB_PMA_PORT_COUNTERS_SELECT_LINK_DOWNED                  = 1 <<  2,
  TS_IB_PMA_PORT_COUNTERS_SELECT_RECV_ERRS                    = 1 <<  3,
  TS_IB_PMA_PORT_COUNTERS_SELECT_RECV_REMOTE_PHY_ERRS         = 1 <<  4,
  TS_IB_PMA_PORT_COUNTERS_SELECT_RECV_SWITCH_RELAY_ERRS       = 1 <<  5,
  TS_IB_PMA_PORT_COUNTERS_SELECT_XMIT_DISCARDS                = 1 <<  6,
  TS_IB_PMA_PORT_COUNTERS_SELECT_XMIT_CONSTRAIN_ERRS          = 1 <<  7,
  TS_IB_PMA_PORT_COUNTERS_SELECT_RECV_CONSTRAIN_ERRS          = 1 <<  8,
  TS_IB_PMA_PORT_COUNTERS_SELECT_LOCAL_LINK_INTEGRITY_ERRS    = 1 <<  9,
  TS_IB_PMA_PORT_COUNTERS_SELECT_EXCESSIVE_BUF_OVERFLOW_ERRS  = 1 << 10,
  TS_IB_PMA_PORT_COUNTERS_SELECT_VL15_DROPPED                 = 1 << 11,
  TS_IB_PMA_PORT_COUNTERS_SELECT_PORT_XMIT_DATA               = 1 << 12,
  TS_IB_PMA_PORT_COUNTERS_SELECT_PORT_RECV_DATA               = 1 << 13,
  TS_IB_PMA_PORT_COUNTERS_SELECT_PORT_XMIT_PKTS               = 1 << 14,
  TS_IB_PMA_PORT_COUNTERS_SELECT_PORT_RECV_PKTS               = 1 << 15
} tTS_IB_PMA_PORT_COUNTERS_SELECT;



typedef struct tTS_IB_PMA_PROVIDER_STRUCT tTS_IB_PMA_PROVIDER_STRUCT,
  *tTS_IB_PMA_PROVIDER;

/*
 * Function type declarations.
 */
typedef ib_mad_process_func tTS_IB_PMA_MAD_FUNCTION;

typedef tTS_IB_PMA_RESULT (*tTS_IB_PMA_CLASS_PORT_INFO_QUERY_FUNCTION)(
                                        tTS_IB_PMA_PROVIDER       provider,
                                        tTS_IB_PORT               port,
                                        tTS_IB_PM_CLASS_PORT_INFO class_pinfo);
typedef tTS_IB_PMA_RESULT (*tTS_IB_PMA_CLASS_PORT_INFO_MODIFY_FUNCTION)(
                                        tTS_IB_PMA_PROVIDER       provider,
                                        tTS_IB_PORT               port,
                                        tTS_IB_PM_CLASS_PORT_INFO class_pinfo);

typedef tTS_IB_PMA_RESULT (*tTS_IB_PMA_PORT_SAMPLE_CONTROL_QUERY_FUNCTION)(
                                        tTS_IB_PMA_PROVIDER            provider,
                                        tTS_IB_PORT                    port,
                                        tTS_IB_PM_PORT_SAMPLES_CONTROL control);
typedef tTS_IB_PMA_RESULT (*tTS_IB_PMA_PORT_SAMPLE_CONTROL_MODIFY_FUNCTION)(
                                        tTS_IB_PMA_PROVIDER            provider,
                                        tTS_IB_PORT                    port,
                                        tTS_IB_PM_PORT_SAMPLES_CONTROL control);

typedef tTS_IB_PMA_RESULT (*tTS_IB_PMA_PORT_SAMPLE_RESULT_QUERY_FUNCTION)(
                                        tTS_IB_PMA_PROVIDER            provider,
                                        tTS_IB_PORT                    port,
                                        tTS_IB_PM_PORT_SAMPLES_RESULT  control);
typedef tTS_IB_PMA_RESULT (*tTS_IB_PMA_PORT_SAMPLE_RESULT_MODIFY_FUNCTION)(
                                        tTS_IB_PMA_PROVIDER            provider,
                                        tTS_IB_PORT                    port,
                                        tTS_IB_PM_PORT_SAMPLES_RESULT  control);

typedef tTS_IB_PMA_RESULT (*tTS_IB_PMA_PORT_COUNTERS_QUERY_FUNCTION)(
                                        tTS_IB_PMA_PROVIDER          provider,
                                        tTS_IB_PORT                  port,
                                        tTS_IB_PORT                  port_sel,
                                        tTS_IB_PM_PORT_COUNTERS      control);
typedef tTS_IB_PMA_RESULT (*tTS_IB_PMA_PORT_COUNTERS_MODIFY_FUNCTION)(
                                        tTS_IB_PMA_PROVIDER           provider,
                                        tTS_IB_PORT                   port,
                                        tTS_IB_PM_PORT_COUNTERS       control);

/*
 * structures
 */


/* The provider structure that a device-specific PMA needs to fill in. */
struct tTS_IB_PMA_PROVIDER_STRUCT {
  TS_IB_DECLARE_MAGIC

  tTS_IB_DEVICE                                  device;
  tTS_IB_PMA_PROVIDER_FLAGS                      flags;
  void                                          *pma;  // Generic PMA use
  void                                          *priv; // Provider's private use

  tTS_IB_PMA_MAD_FUNCTION                        mad_handler;
  tTS_IB_PMA_CLASS_PORT_INFO_QUERY_FUNCTION      class_port_info_query;
  tTS_IB_PMA_CLASS_PORT_INFO_MODIFY_FUNCTION     class_port_info_modify;
  tTS_IB_PMA_PORT_SAMPLE_CONTROL_QUERY_FUNCTION  port_sample_control_query;
  tTS_IB_PMA_PORT_SAMPLE_CONTROL_MODIFY_FUNCTION port_sample_control_modify;
  tTS_IB_PMA_PORT_SAMPLE_RESULT_QUERY_FUNCTION   port_sample_result_query;
  tTS_IB_PMA_PORT_SAMPLE_RESULT_MODIFY_FUNCTION  port_sample_result_modify;
  tTS_IB_PMA_PORT_COUNTERS_QUERY_FUNCTION        port_counters_query;
  tTS_IB_PMA_PORT_COUNTERS_MODIFY_FUNCTION       port_counters_modify;
};

#endif /* _TS_IB_PMA_PROVIDER_TYPES_H */
