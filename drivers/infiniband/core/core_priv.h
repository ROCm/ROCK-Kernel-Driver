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

  $Id: core_priv.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _CORE_PRIV_H
#define _CORE_PRIV_H

#ifndef __KERNEL__
#  define __KERNEL__
#endif
#ifndef MODULE
#  define MODULE
#endif

#ifndef W2K_OS
#  include <linux/config.h>
#endif
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#  define MODVERSIONS
#endif

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  ifndef W2K_OS
#    include <linux/modversions.h>
#  endif
#endif

#include "ts_ib_core.h"
#include "ts_ib_provider.h"

#include "ts_kernel_thread.h"
#include "ts_kernel_seq_lock.h"

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

enum {
  IB_PORT_CAP_SM,
  IB_PORT_CAP_SNMP_TUN,
  IB_PORT_CAP_DEV_MGMT,
  IB_PORT_CAP_VEND_CLASS,
  IB_PORT_CAP_NUM
};

struct ib_device_private {
	int                     start_port;
	int                     end_port;
	tTS_IB_GUID             node_guid;
	struct ib_port_data    *port_data;

	struct list_head        async_handler_list;
	spinlock_t              async_handler_lock;

	tTS_KERNEL_QUEUE_THREAD completion_thread;
	tTS_KERNEL_QUEUE_THREAD async_thread;

	struct ib_core_proc    *proc;
};

struct ib_port_data {
	spinlock_t                 port_cap_lock;
	int                        port_cap_count[IB_PORT_CAP_NUM];

	tTS_SEQ_LOCK_STRUCT        lock;
	struct ib_port_properties  properties;
	struct ib_sm_path          sm_path;
	struct ib_port_lid         port_lid;
	int                        gid_table_alloc_length;
	int                        pkey_table_alloc_length;
	tTS_IB_GID                *gid_table;
	tTS_IB_PKEY               *pkey_table;
};

int  ib_cache_setup(struct ib_device *device);
void ib_cache_cleanup(struct ib_device *device);
void ib_cache_update(struct ib_device *device, tTS_IB_PORT port);
int  ib_proc_setup(struct ib_device *device, int is_switch);
void ib_proc_cleanup(struct ib_device *device);
int  ib_create_proc_dir(void);
void ib_remove_proc_dir(void);
void ib_completion_thread(struct list_head *entry, void *device_ptr);
void ib_async_thread(struct list_head *entry, void *device_ptr);

#endif /* _CORE_PRIV_H */

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
