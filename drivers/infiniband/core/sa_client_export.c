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

  $Id: sa_client_export.c 32 2004-04-09 03:57:42Z roland $
*/

#include <linux/config.h>
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#  define MODVERSIONS
#endif

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include <linux/modversions.h>
#endif

#include "ts_ib_sa_client.h"

#define __NO_VERSION__
#include <linux/module.h>

EXPORT_SYMBOL(tsIbPathRecordRequest);
EXPORT_SYMBOL(tsIbMulticastGroupJoin);
EXPORT_SYMBOL(tsIbMulticastGroupLeave);
EXPORT_SYMBOL(tsIbPortInfoQuery);
EXPORT_SYMBOL(tsIbPortInfoTblQuery);
EXPORT_SYMBOL(tsIbSetInServiceNoticeHandler);
EXPORT_SYMBOL(tsIbSetOutofServiceNoticeHandler);
EXPORT_SYMBOL(tsIbServiceSet);
EXPORT_SYMBOL(tsIbAtsServiceSet);
EXPORT_SYMBOL(tsIbAtsServiceGetGid);
EXPORT_SYMBOL(tsIbAtsServiceGetIp);
EXPORT_SYMBOL(tsIbMulticastGroupTableQuery);
EXPORT_SYMBOL(tsIbNodeInfoQuery);
