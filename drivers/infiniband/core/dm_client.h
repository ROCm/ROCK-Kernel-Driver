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

  $Id: dm_client.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _DM_CLIENT_H
#define _DM_CLIENT_H

#ifndef __KERNEL__
#  define __KERNEL__
#endif
#ifndef MODULE
#  define MODULE
#endif

#ifndef W2K_OS
#include <linux/config.h>
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#  define MODVERSIONS
#endif
#endif

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include <linux/modversions.h>
#endif

#ifdef W2K_OS // Vipul
#include <ntddk.h>
#endif
#include "ts_ib_dm_client.h"
#include "ts_ib_mad_types.h"

enum {
  TS_IB_DM_CLASS_VERSION = 1
};

extern int use_port_info_tbl;

void tsIbDmClientMadInit(tTS_IB_MAD  packet,
                         tTS_IB_DEVICE_HANDLE device,
                         tTS_IB_PORT port,
                         tTS_IB_LID dst_lid,
                         tTS_IB_QPN dst_qpn,
                         uint32_t r_method,
                         uint16_t attribute_id,
                         uint32_t attribute_modifier);

int tsIbDmClientQueryInit(void);

void tsIbDmClientQueryCleanup(void);

void tsIbDmAsyncNotifyHandler(tTS_IB_MAD packet,
                              void *arg);

#endif /* _DM_CLIENT_H */
