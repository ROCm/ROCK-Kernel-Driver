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

  $Id: useraccess.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _USERACCESS_H
#define _USERACCESS_H

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

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#ifndef W2K_OS
#  include <linux/modversions.h>
#endif
#endif

#include <linux/list.h>
#include <linux/fs.h>
#include <linux/poll.h>
#if !defined(TS_KERNEL_2_6)
#include <linux/devfs_fs_kernel.h>
#endif

#include "ts_ib_useraccess.h"
#include "ts_ib_core_types.h"

#include "useraccess_file_compat.h"

enum {
  TS_USERACCESS_DEFAULT_QUEUE_LENGTH = 512,
  TS_USERACCESS_MAX_PORTS_PER_DEVICE = 2,
  TS_USER_MAD_SIZE                   = sizeof (tTS_IB_MAD_STRUCT) - sizeof (struct list_head)
};

typedef enum {
  TS_IB_PORT_CAP_SM,
  TS_IB_PORT_CAP_SNMP_TUN,
  TS_IB_PORT_CAP_DEV_MGMT,
  TS_IB_PORT_CAP_VEND_CLASS,
  TS_IB_PORT_CAP_NUM
} tTS_IB_PORT_CAP_BIT;

typedef struct tTS_IB_USERACCESS_DEVICE_STRUCT tTS_IB_USERACCESS_DEVICE_STRUCT,
  *tTS_IB_USERACCESS_DEVICE;
typedef struct tTS_IB_USERACCESS_PRIVATE_STRUCT tTS_IB_USERACCESS_PRIVATE_STRUCT,
  *tTS_IB_USERACCESS_PRIVATE;

struct tTS_IB_USERACCESS_DEVICE_STRUCT {
  tTS_IB_DEVICE_HANDLE ib_device;
#if !defined(TS_KERNEL_2_6)
  devfs_handle_t       devfs_handle;
#endif
};

struct tTS_IB_USERACCESS_PRIVATE_STRUCT {
  tTS_IB_USERACCESS_DEVICE device;
  int                      port_cap_count[TS_USERACCESS_MAX_PORTS_PER_DEVICE + 1][TS_IB_PORT_CAP_NUM];
  int                      max_mad_queue_length;
  int                      mad_queue_length;
  struct list_head         mad_list;
  wait_queue_head_t        mad_wait;
  struct semaphore         mad_sem;
};

ssize_t tsIbUserRead(
                     TS_READ_PARAMS(filp, buf, count, pos)
                     );

ssize_t tsIbUserWrite(
                      TS_WRITE_PARAMS(filp, buf, count, pos)
                      );

unsigned int tsIbUserPoll(
                          struct file *filp,
                          poll_table *wait
                          );

int tsIbUserFilterAdd(
                      tTS_IB_USERACCESS_PRIVATE priv,
                      tTS_IB_USER_MAD_FILTER filter
                      );

int tsIbUserFilterDel(
                      tTS_IB_USERACCESS_PRIVATE priv,
                      tTS_IB_USER_MAD_FILTER_HANDLE handle
                      );

void tsIbUserFilterClear(
                         tTS_IB_USERACCESS_PRIVATE priv
                         );

int tsIbUserIoctl(
                  TS_FILE_OP_PARAMS(inode, filp),
                  unsigned int cmd,
                  unsigned long arg
                  );

#endif /* _USERACCESS_H */
