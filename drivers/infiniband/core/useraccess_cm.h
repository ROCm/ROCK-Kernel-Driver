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

  $Id: useraccess_cm.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _USERACCESS_CM_H
#define _USERACCESS_CM_H

#ifndef __KERNEL__
#  define __KERNEL__
#endif
#ifndef MODULE
#  define MODULE
#endif

#ifndef W2K_OS
#include <linux/config.h>
#endif
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#  define MODVERSIONS
#endif

#ifdef W2K_OS
#include "useraccess_ioctl_w2k.h"
#include "useraccess_main_w2k.h"
#endif

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#ifndef W2K_OS
#  include <linux/modversions.h>
#endif
#endif

#ifndef W2K_OS
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/poll.h>
#if !defined(TS_KERNEL_2_6)
#include <linux/devfs_fs_kernel.h>
#endif
#else // W2K_OS
#include <os_dep/win/linux/list.h>
#endif

#include "ts_ib_core.h"
#include "ts_ib_cm.h"
#include "ts_ib_useraccess_cm.h"

typedef struct tTS_IB_USERACCESS_CM_DEVICE_STRUCT tTS_IB_USERACCESS_CM_DEVICE_STRUCT,
  *tTS_IB_USERACCESS_CM_DEVICE;
typedef struct tTS_IB_USERACCESS_CM_PRIVATE_STRUCT tTS_IB_USERACCESS_CM_PRIVATE_STRUCT,
  *tTS_IB_USERACCESS_CM_PRIVATE;

struct tTS_IB_USERACCESS_CM_DEVICE_STRUCT {
  tTS_IB_DEVICE_HANDLE ib_device;
  tTS_IB_PD_HANDLE     pd;
#if !defined(TS_KERNEL_2_6)
  devfs_handle_t       devfs_handle;
#endif
};

struct tTS_IB_USERACCESS_CM_PRIVATE_STRUCT {
  tTS_IB_USERACCESS_CM_DEVICE device;

  struct list_head         cm_comp_list;
  wait_queue_head_t        cm_comp_wait;
  spinlock_t               cm_comp_lock;

  struct list_head         cm_conn_list;
#ifndef W2K_OS
  struct semaphore         cm_conn_sem;
#else
  KSEMAPHORE      cm_conn_sem;
  KSEMAPHORE      irp_sem;
  PIRP            pIrp;
#endif
};

/*
 * We use this to keep track of connections an application owns and destroy
 * then when the fd is closed
 */
struct tTS_IB_CM_USER_CONNECTION {
  struct list_head          list;

  atomic_t                  refcnt;

  tTS_IB_USERACCESS_CM_PRIVATE priv;
  tTS_IB_CM_COMM_ID         comm_id;
  tTS_IB_LISTEN_HANDLE      listen_handle;
  unsigned long             cm_arg;

  VAPI_hca_hndl_t           v_device;

  tTS_IB_QP_HANDLE          qp;
  VAPI_k_qp_hndl_t          vk_qp;
};

typedef struct tTS_IB_CM_USER_CONNECTION tTS_IB_CM_USER_CONNECTION_STRUCT,
  *tTS_IB_CM_USER_CONNECTION;

#ifndef W2K_OS
int tsIbCmUserIoctl(
                    struct inode *inode,
                    struct file *filp,
                    unsigned int cmd,
                    unsigned long arg
                    );
#else
int tsIbCmUserIoctl(
                    PFILE_OBJECT pFileObject,
                    unsigned  int  cmd,
                    unsigned long  arg
                    );
#endif

/* User Mode CM Filter Routines */
int tsIbCmUserGetServiceId(
                           unsigned long arg
                           );

int tsIbCmUserConnect(
                      tTS_IB_USERACCESS_CM_PRIVATE priv,
                      unsigned long arg
                      );

int tsIbCmUserListen(
                     tTS_IB_USERACCESS_CM_PRIVATE priv,
                     unsigned long arg
                     );

int tsIbCmUserAccept(
                     tTS_IB_USERACCESS_CM_PRIVATE priv,
                     unsigned long arg
                     );

int tsIbCmUserDropConsumer(
                           tTS_IB_USERACCESS_CM_PRIVATE priv,
                           unsigned long arg
                           );

int tsIbCmUserGetCompletion(
                            tTS_IB_USERACCESS_CM_PRIVATE priv,
                            unsigned long arg
                            );

int tsIbCmUserClose(
                    tTS_IB_USERACCESS_CM_PRIVATE priv
                    );

#endif /* _USERACCESS_CM_H */
