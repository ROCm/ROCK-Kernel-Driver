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

  $Id: sa_client_query.c 32 2004-04-09 03:57:42Z roland $
*/

#include "sa_client.h"

#include "ts_ib_core.h"
#include "ts_ib_mad.h"
#include "ts_ib_client_query.h"

#include "ts_kernel_trace.h"
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

void tsIbSaClientMadInit(
                         tTS_IB_MAD           packet,
                         tTS_IB_DEVICE_HANDLE device,
                         tTS_IB_PORT          port
                         ) {
  tTS_IB_SM_PATH_STRUCT sm_path;

  tsIbCachedSmPathGet(device, port, &sm_path);

  memset(packet, 0, sizeof *packet);

  packet->format_version     = 1;
  packet->mgmt_class         = TS_IB_MGMT_CLASS_SUBN_ADM;
  packet->class_version      = TS_IB_SA_CLASS_VERSION;
  packet->transaction_id     = tsIbClientAllocTid();
  packet->attribute_modifier = 0xffffffff;

  packet->device     = device;
  packet->port       = port;
  packet->pkey_index = 0;
  packet->slid       = 0xffff;
  packet->dlid       = sm_path.sm_lid;
  packet->sl         = sm_path.sm_sl;
  packet->sqpn       = TS_IB_GSI_QP;
  packet->dqpn       = TS_IB_GSI_QP;

  packet->completion_func = NULL;
}

int tsIbSaClientQueryInit(
                          void
                          ) {
  return 0;
}

void tsIbSaClientQueryCleanup(
                              void
                              ) {
  /* XXX cancel all queries and delete timers */
}
