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

  $Id: ts_ib_sma_provider.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_IB_SMA_PROVIDER_H
#define _TS_IB_SMA_PROVIDER_H

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include "ts_kernel_version.h"
#  include TS_VER_FILE(../core,core_export.ver)
#endif

#include "ts_ib_sma_provider_types.h"

int tsIbSmaDeviceRegister(tTS_IB_SMA_PROVIDER sma_dev);
int tsIbSmaDeviceDeregister(tTS_IB_SMA_PROVIDER sma_dev);

tTS_IB_MAD_RESULT tsIbSmaMadProcess(tTS_IB_SMA_PROVIDER sma_dev,
                                    int                 ignore_mkey,
                                    tTS_IB_MAD          in_mad,
                                    tTS_IB_MAD          out_mad
                                   );

int tsIbSmaTrapSend(void *trap);

/* Check to see if a requested port_state transition is valid.
 * Return True (!0) if the transition is valid.
 */
int tsIbSmaPortStateValid(tTS_IB_PORT_STATE  old_state,
                          tTS_IB_PORT_STATE  new_state
                         );

/* tsIbSmaPortInfoValid
 *
 * Check an incoming PortInfo set SMP to see if it contains any invalid
 * fields according to the "Performance Management MAD Status" document
 * Revision 1.1 Sept 15, 2003.
 *
 * Parameters:
 *  local_pinfo:  the current PortInfo data structure for the port
 *    smp_pinfo:  the incoming PortInfo data from the SMP.
 *
 * Returns:
 *   TS_IB_SMA_SUCCESS:         SMP is fine.
 *   TS_IB_SMA_INVAL_A_FIELD:   There is at least one invalid attribute field.
 */
tTS_IB_SMA_RESULT tsIbSmaPortInfoValid(tTS_IB_SMP_PORT_INFO  local_pinfo,
                                       tTS_IB_SMP_PORT_INFO  smp_pinfo
                                      );

/* Routine to update the port state given the old port state, a requested
 * new port state, and the PHY_LINK state.
 */
tTS_IB_SMA_PORT_UPDATE_RESULT
tsIbSmaPortStateUpdate(tTS_IB_PORT_STATE         *state_p,
                       tTS_IB_PORT_STATE          request_state,
                       tTS_IB_SMA_PHY_LINK_STATE  phy_link
                      );

/*
 * Per-port state maintained by the generic SMA code.  This data can be
 * maintained without reference to the hardware.
 *
 * The following fields in the PORT_INFO data structure are maintained by the
 * generic SMA:
 *
 * tTS_IB_MKEY     m_key;
 * tTS_IB_LID      master_sm_lid;
 * uint16_t        diag_code;
 * uint16_t        m_key_lease_period;
 * uint8_t         m_key_protect_bits;
 * uint8_t         master_sm_sl;
 * uint8_t         init_type;
 * uint8_t         init_type_reply;
 * uint16_t        m_key_violations;
 * uint8_t         subnet_timeout;
 * uint8_t         resp_time_value;
 */

/*
 * tsIbSmaPortInfoInit
 *
 * Given a PortInfo data structure, init the generic SMA fields.
 */
void tsIbSmaPortInfoInit(tTS_IB_SMP_PORT_INFO port_info);

/*
 * tsIbSmaPortInfoQuery
 *
 * The generic SMA maintains part of the PortInfo state for each device.
 * This function handles filling in the appropriate generic state.
 *
 * Parameters:
 *  local_pinfo : port info data structure of which this code maintains fields.
 *  port        : the port the SMP came in on.
 *  smp_pinfo   : the outgoing port info to be filled in
 *  mkey_match  : the result of the M_Key check on the incoming mad.
 *
 * Return:
 *  None: always succeeds.
 */
void tsIbSmaPortInfoQuery(tTS_IB_SMA_PROVIDER    provider,
                          tTS_IB_SMP_PORT_INFO   local_pinfo,
                          tTS_IB_PORT            port,
                          tTS_IB_SMP_PORT_INFO   smp_pinfo,
                          tTS_IB_SMA_M_KEY_MATCH mkey_match);

/*
 * tsIbSmaPortInfoModify
 *
 * The generic SMA maintains part of the PortInfo state for each device.
 * This function handles setting the appropriate generic state as well
 * as calling the device-specific provider to modify its info.
 *
 * Parameters:
 *  provider  : the device-specific sma provider
 *  port      : the port the SMP came in on.
 *  port_info : the full port info data structure to be filled in
 *
 * Return:
 *  a success or failure indication.
 */
void tsIbSmaPortInfoModify(tTS_IB_SMA_PROVIDER  provider,
                           tTS_IB_SMP_PORT_INFO local_pinfo,
                           tTS_IB_PORT          port,
                           tTS_IB_SMP_PORT_INFO port_info);

// A simulated device provider for example and testing.
extern tTS_IB_SMA_PROVIDER_STRUCT ts_ib_sma_sim_provider;

#endif /* _TS_IB_SMA_PROVIDER_H */
