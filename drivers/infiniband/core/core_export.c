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

  $Id: core_export.c,v 1.7 2004/02/25 00:35:16 roland Exp $
*/

#include <linux/config.h>
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#  define MODVERSIONS
#endif

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include <linux/modversions.h>
#endif

#include "ts_ib_core.h"
#include "ts_ib_provider.h"

#define __NO_VERSION__
#include <linux/module.h>

EXPORT_SYMBOL(tsIbDeviceRegister);
EXPORT_SYMBOL(tsIbDeviceDeregister);
EXPORT_SYMBOL(tsIbDeviceGetByName);
EXPORT_SYMBOL(tsIbDeviceGetByIndex);
EXPORT_SYMBOL(tsIbDevicePropertiesGet);
EXPORT_SYMBOL(tsIbPortPropertiesGet);
EXPORT_SYMBOL(tsIbPortPropertiesSet);
EXPORT_SYMBOL(tsIbPkeyEntryGet);
EXPORT_SYMBOL(tsIbGidEntryGet);

EXPORT_SYMBOL(tsIbPdCreate);
EXPORT_SYMBOL(tsIbPdDestroy);

EXPORT_SYMBOL(tsIbAddressCreate);
EXPORT_SYMBOL(tsIbAddressQuery);
EXPORT_SYMBOL(tsIbAddressDestroy);

EXPORT_SYMBOL(tsIbQpCreate);
EXPORT_SYMBOL(tsIbSpecialQpCreate);
EXPORT_SYMBOL(tsIbQpModify);
EXPORT_SYMBOL(tsIbQpQuery);
EXPORT_SYMBOL(tsIbQpQueryQpn);
EXPORT_SYMBOL(tsIbQpDestroy);
EXPORT_SYMBOL(tsIbSend);
EXPORT_SYMBOL(tsIbReceive);

EXPORT_SYMBOL(tsIbCqCreate);
EXPORT_SYMBOL(tsIbCqDestroy);
EXPORT_SYMBOL(tsIbCqResize);
EXPORT_SYMBOL(tsIbCqPoll);
EXPORT_SYMBOL(tsIbCqRequestNotification);
EXPORT_SYMBOL(tsIbCompletionEventDispatch);

EXPORT_SYMBOL(tsIbMemoryRegister);
EXPORT_SYMBOL(tsIbMemoryRegisterPhysical);
EXPORT_SYMBOL(tsIbMemoryDeregister);

EXPORT_SYMBOL(tsIbFmrPoolCreate);
EXPORT_SYMBOL(tsIbFmrPoolDestroy);
EXPORT_SYMBOL(tsIbFmrRegisterPhysical);
EXPORT_SYMBOL(tsIbFmrDeregister);

EXPORT_SYMBOL(tsIbMulticastAttach);
EXPORT_SYMBOL(tsIbMulticastDetach);

EXPORT_SYMBOL(tsIbAsyncEventHandlerRegister);
EXPORT_SYMBOL(tsIbAsyncEventHandlerDeregister);

EXPORT_SYMBOL(tsIbCachedNodeGuidGet);
EXPORT_SYMBOL(tsIbCachedPortPropertiesGet);
EXPORT_SYMBOL(tsIbCachedSmPathGet);
EXPORT_SYMBOL(tsIbCachedLidGet);
EXPORT_SYMBOL(tsIbCachedGidGet);
EXPORT_SYMBOL(tsIbCachedGidFind);
EXPORT_SYMBOL(tsIbCachedPkeyGet);
EXPORT_SYMBOL(tsIbCachedPkeyFind);

EXPORT_SYMBOL(tsIbAsyncEventDispatch);
