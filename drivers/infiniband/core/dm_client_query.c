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

  $Id: dm_client_query.c 32 2004-04-09 03:57:42Z roland $
*/

#include "dm_client.h"

#include "ts_ib_mad.h"
#ifndef W2K_OS // Vipul
#include "ts_ib_client_query.h"
#endif

#include "ts_kernel_trace.h"
#include "ts_kernel_hash.h"
#include "ts_kernel_timer.h"

#ifndef W2K_OS
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#else
#include <os_dep/win/linux/list.h>
#include <os_dep/win/linux/spinlock.h>
#endif

void tsIbDmClientMadInit(tTS_IB_MAD  packet,
                         tTS_IB_DEVICE_HANDLE device,
                         tTS_IB_PORT port,
                         tTS_IB_LID dst_lid,
                         tTS_IB_QPN dst_qpn,
                         uint32_t r_method,
                         uint16_t attribute_id,
                         uint32_t attribute_modifier){
  memset(packet, 0, sizeof *packet);

  packet->format_version     = 1;
  packet->mgmt_class         = TS_IB_MGMT_CLASS_DEV_MGT;
  packet->class_version      = TS_IB_DM_CLASS_VERSION;
  packet->transaction_id     = tsIbClientAllocTid();

  packet->device = device;
  packet->pkey_index = 0;
  packet->port = port;
  packet->slid = 0xffff;
  packet->dlid = dst_lid;
  packet->sl   = 0;
  packet->sqpn = TS_IB_GSI_QP;
  packet->dqpn = dst_qpn;
#ifdef W2K_OS // Vipul
  packet->r_method = (uint8_t)r_method;
#else
  packet->r_method = r_method;
#endif
  packet->attribute_id = cpu_to_be16(attribute_id);
  packet->attribute_modifier = cpu_to_be32(attribute_modifier);

  packet->completion_func = NULL;

}

int tsIbDmClientQueryInit(void)
{
  return 0;
}

void tsIbDmClientQueryCleanup(void)
{
  /* XXX cancel all queries and delete timers */
}
