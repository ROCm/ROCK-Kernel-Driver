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

  $Id: core_priv.h,v 1.10 2004/02/25 00:35:17 roland Exp $
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

typedef struct tTS_IB_DEVICE_PRIVATE_STRUCT tTS_IB_DEVICE_PRIVATE_STRUCT,
  *tTS_IB_DEVICE_PRIVATE;
typedef struct tTS_IB_PORT_DATA_STRUCT tTS_IB_PORT_DATA_STRUCT,
  *tTS_IB_PORT_DATA;
typedef struct tTS_IB_CORE_PROC_STRUCT tTS_IB_CORE_PROC_STRUCT,
  *tTS_IB_CORE_PROC;

typedef enum {
  TS_IB_PORT_CAP_SM,
  TS_IB_PORT_CAP_SNMP_TUN,
  TS_IB_PORT_CAP_DEV_MGMT,
  TS_IB_PORT_CAP_VEND_CLASS,
  TS_IB_PORT_CAP_NUM
} tTS_IB_PORT_CAP_BIT;

struct tTS_IB_DEVICE_PRIVATE_STRUCT {
  int                     start_port;
  int                     end_port;
  tTS_IB_GUID             node_guid;
  tTS_IB_PORT_DATA        port_data;

  struct list_head        async_handler_list;
  spinlock_t              async_handler_lock;

  tTS_KERNEL_QUEUE_THREAD completion_thread;
  tTS_KERNEL_QUEUE_THREAD async_thread;

  tTS_IB_CORE_PROC        proc;
};

struct tTS_IB_PORT_DATA_STRUCT {
  spinlock_t                    port_cap_lock;
  int                           port_cap_count[TS_IB_PORT_CAP_NUM];

  tTS_SEQ_LOCK_STRUCT           lock;
  tTS_IB_PORT_PROPERTIES_STRUCT properties;
  tTS_IB_SM_PATH_STRUCT         sm_path;
  tTS_IB_PORT_LID_STRUCT        port_lid;
  int                           gid_table_alloc_length;
  int                           pkey_table_alloc_length;
  tTS_IB_GID                   *gid_table;
  tTS_IB_PKEY                  *pkey_table;
};

int tsIbCacheSetup(
                   tTS_IB_DEVICE device
                   );

void tsIbCacheCleanup(
                      tTS_IB_DEVICE device
                      );

void tsIbCacheUpdate(
                     tTS_IB_DEVICE device,
                     tTS_IB_PORT   port
                     );

int tsIbProcSetup(
                  tTS_IB_DEVICE device,
                  int           is_switch
                  );

void tsIbProcCleanup(
                     tTS_IB_DEVICE device
                     );

int tsIbCreateProcDir(
                      void
                      );

void tsIbRemoveProcDir(
                       void
                       );

void tsIbCompletionThread(
                          struct list_head *entry,
                          void *device_ptr
                          );

void tsIbAsyncThread(
                     struct list_head *entry,
                     void *device_ptr
                     );

int tsIbDeviceInitModule(
                         void
                         );

#endif /* _CORE_PRIV_H */
