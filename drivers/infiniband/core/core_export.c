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

  $Id: core_export.c 32 2004-04-09 03:57:42Z roland $
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

EXPORT_SYMBOL(ib_device_register);
EXPORT_SYMBOL(ib_device_deregister);
EXPORT_SYMBOL(ib_device_get_by_name);
EXPORT_SYMBOL(ib_device_get_by_index);
EXPORT_SYMBOL(ib_device_notifier_register);
EXPORT_SYMBOL(ib_device_notifier_deregister);
EXPORT_SYMBOL(ib_device_properties_get);
EXPORT_SYMBOL(ib_port_properties_get);
EXPORT_SYMBOL(ib_port_properties_set);
EXPORT_SYMBOL(ib_pkey_entry_get);
EXPORT_SYMBOL(ib_gid_entry_get);

EXPORT_SYMBOL(ib_pd_create);
EXPORT_SYMBOL(ib_pd_destroy);

EXPORT_SYMBOL(ib_address_create);
EXPORT_SYMBOL(ib_address_query);
EXPORT_SYMBOL(ib_address_destroy);

EXPORT_SYMBOL(ib_qp_create);
EXPORT_SYMBOL(ib_special_qp_create);
EXPORT_SYMBOL(ib_qp_modify);
EXPORT_SYMBOL(ib_qp_query);
EXPORT_SYMBOL(ib_qp_query_qpn);
EXPORT_SYMBOL(ib_qp_destroy);
EXPORT_SYMBOL(ib_send);
EXPORT_SYMBOL(ib_receive);

EXPORT_SYMBOL(ib_cq_create);
EXPORT_SYMBOL(ib_cq_destroy);
EXPORT_SYMBOL(ib_cq_resize);
EXPORT_SYMBOL(ib_cq_poll);
EXPORT_SYMBOL(ib_cq_request_notification);
EXPORT_SYMBOL(ib_completion_event_dispatch);

EXPORT_SYMBOL(ib_memory_register);
EXPORT_SYMBOL(ib_memory_register_physical);
EXPORT_SYMBOL(ib_memory_deregister);

EXPORT_SYMBOL(ib_fmr_pool_create);
EXPORT_SYMBOL(ib_fmr_pool_destroy);
EXPORT_SYMBOL(ib_fmr_register_physical);
EXPORT_SYMBOL(ib_fmr_deregister);

EXPORT_SYMBOL(ib_multicast_attach);
EXPORT_SYMBOL(ib_multicast_detach);

EXPORT_SYMBOL(ib_async_event_handler_register);
EXPORT_SYMBOL(ib_async_event_handler_deregister);

EXPORT_SYMBOL(ib_cached_node_guid_get);
EXPORT_SYMBOL(ib_cached_port_properties_get);
EXPORT_SYMBOL(ib_cached_sm_path_get);
EXPORT_SYMBOL(ib_cached_lid_get);
EXPORT_SYMBOL(ib_cached_gid_get);
EXPORT_SYMBOL(ib_cached_gid_find);
EXPORT_SYMBOL(ib_cached_pkey_get);
EXPORT_SYMBOL(ib_cached_pkey_find);

EXPORT_SYMBOL(ib_async_event_dispatch);
