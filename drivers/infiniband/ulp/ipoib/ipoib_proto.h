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

  $Id: ipoib_proto.h 35 2004-04-09 05:34:32Z roland $
*/

#ifndef _TS_IPOIB_PROTO_H
#define _TS_IPOIB_PROTO_H

#include <linux/config.h>
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#  define MODVERSIONS
#endif

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include "ts_kernel_version.h"
#  include TS_VER_FILE(.,ipoib_export.ver)
#endif

#include <linux/netdevice.h>
#include <ib_legacy_types.h>
#include <ts_ib_core_types.h>

/* ------------------------------------------------------------------------- */
/* Public constants                                                          */
/* ------------------------------------------------------------------------- */
/* ------------------------------------------------------------------------- */
/* Public functions                                                          */
/* ------------------------------------------------------------------------- */
int tsIpoibDeviceArpGetGid(
                           struct net_device *dev,
                           tUINT8            *hash,
                           tTS_IB_GID         gid
                           );

int tsIpoibDeviceHandle(
                        struct net_device    *dev,
                        tTS_IB_DEVICE_HANDLE *ca,
                        tTS_IB_PORT          *port,
                        tTS_IB_GID           gid,
                        tTS_IB_PKEY          *pkey
                        );

#endif /* _TS_IPOIB_PROTO_H */
