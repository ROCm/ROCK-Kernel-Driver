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

  $Id: useraccess_main.c 32 2004-04-09 03:57:42Z roland $
*/

#include "useraccess.h"

#include "ts_ib_core.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/slab.h>
#include <linux/init.h>
#include <linux/errno.h>
#if defined(TS_KERNEL_2_6)
#include <linux/cdev.h>
#endif

MODULE_AUTHOR("Roland Dreier");
MODULE_DESCRIPTION("kernel IB userspace access");
MODULE_LICENSE("Dual BSD/GPL");

enum {
  TS_USERACCESS_NUM_DEVICE = 16
};

#if defined(TS_KERNEL_2_6)
static dev_t          useraccess_devnum;
static struct cdev    useraccess_cdev;
#else
static int            useraccess_major;
static devfs_handle_t useraccess_devfs_dir;
#endif

static tTS_IB_USERACCESS_DEVICE_STRUCT useraccess_dev_list[TS_USERACCESS_NUM_DEVICE];
static const char     TS_USERACCESS_NAME[] = "ts_ib_useraccess";

/* =============================================================== */
/*.._tsIbUserOpen -                                                */
static int _tsIbUserOpen(
                         TS_FILE_OP_PARAMS(inode, filp)
                         ) {
  tTS_IB_USERACCESS_DEVICE dev;
  tTS_IB_USERACCESS_PRIVATE priv;

  int dev_num;

  if (filp->private_data) {
    dev = (tTS_IB_USERACCESS_DEVICE) filp->private_data;
  } else {
    dev_num = MINOR(inode->i_rdev);

    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "device number %d", dev_num);

    /* not using devfs, use minor number */
    dev = &useraccess_dev_list[dev_num];
  }

  if (dev->ib_device == TS_IB_HANDLE_INVALID) {
    return -ENODEV;
  }

  priv = kmalloc(sizeof *priv, GFP_KERNEL);
  if (!priv) {
    return -ENOMEM;
  }

  priv->device               = dev;
  memset(priv->port_cap_count, 0,
         (TS_USERACCESS_MAX_PORTS_PER_DEVICE + 1) * TS_IB_PORT_CAP_NUM * sizeof (int));
  priv->max_mad_queue_length = TS_USERACCESS_DEFAULT_QUEUE_LENGTH;
  priv->mad_queue_length     = 0;
  INIT_LIST_HEAD(&priv->mad_list);
  init_waitqueue_head(&priv->mad_wait);
  sema_init(&priv->mad_sem, 1);

  filp->private_data = priv;

  return 0;
}

/* =============================================================== */
/*.._tsIbUserClose -                                               */
static int _tsIbUserClose(
                          TS_FILE_OP_PARAMS(inode, filp)
                          ) {
  tTS_IB_USERACCESS_PRIVATE priv = TS_IB_USER_PRIV_FROM_FILE(filp);
  tTS_IB_PORT_PROPERTIES_SET_STRUCT prop = { 0 };
  int port;

  for (port = 0; port <= TS_USERACCESS_MAX_PORTS_PER_DEVICE; ++port) {
    /* Undo any port capability changes from this process */
    prop.valid_fields = 0;

    if (priv->port_cap_count[port][TS_IB_PORT_CAP_SM]) {
      prop.valid_fields |= TS_IB_PORT_IS_SM;
    }

    if (priv->port_cap_count[port][TS_IB_PORT_CAP_SNMP_TUN]) {
      prop.valid_fields |= TS_IB_PORT_IS_SNMP_TUNNELING_SUPPORTED;
    }

    if (priv->port_cap_count[port][TS_IB_PORT_CAP_DEV_MGMT]) {
      prop.valid_fields |= TS_IB_PORT_IS_DEVICE_MANAGEMENT_SUPPORTED;
    }

    if (priv->port_cap_count[port][TS_IB_PORT_CAP_VEND_CLASS]) {
      prop.valid_fields |= TS_IB_PORT_IS_VENDOR_CLASS_SUPPORTED;
    }

    if (prop.valid_fields) {
      tsIbPortPropertiesSet(priv->device->ib_device, port, &prop);
    }
  }

  tsIbUserFilterClear(priv);
  kfree(priv);

  return 0;
}

static struct file_operations useraccess_fops = {
  .owner   = THIS_MODULE,
  .read    = tsIbUserRead,
  .write   = tsIbUserWrite,
  .poll    = tsIbUserPoll,
  .ioctl   = tsIbUserIoctl,
  .open    = _tsIbUserOpen,
  .release = _tsIbUserClose
};

#if defined(TS_KERNEL_2_6)

/* Create devices for kernel 2.4 */
static int __init _tsIbUserCreateDevices(
                                         void
                                         ) {
  int ret;

  ret = alloc_chrdev_region(&useraccess_devnum,
                            0,
                            TS_USERACCESS_NUM_DEVICE,
                            (char *) TS_USERACCESS_NAME);
  if (ret) {
    TS_REPORT_FATAL(MOD_KERNEL_IB,
                    "Couldn't allocate device numbers for useraccess module");
    return ret;
  }

  cdev_init(&useraccess_cdev, &useraccess_fops);
  useraccess_cdev.owner = THIS_MODULE;
  kobject_set_name(&useraccess_cdev.kobj, TS_USERACCESS_NAME);
  ret = cdev_add(&useraccess_cdev,
                 useraccess_devnum,
                 TS_USERACCESS_NUM_DEVICE);
  if (ret) {
    kobject_put(&useraccess_cdev.kobj);
    unregister_chrdev_region(useraccess_devnum, TS_USERACCESS_NUM_DEVICE);
  }

  return ret;
}

#else /* TS_KERNEL_2_6 */

/* Create devices for kernel 2.4 */
static int __init _tsIbUserCreateDevices(
                                         void
                                         ) {
  useraccess_major = devfs_register_chrdev(0,
                                           TS_USERACCESS_NAME,
                                           &useraccess_fops);
  if (useraccess_major < 0) {
    TS_REPORT_FATAL(MOD_KERNEL_IB,
                    "Failed to register device");
    return useraccess_major;
  }

  TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
           "TS IB user access major %d",
           useraccess_major);

  useraccess_devfs_dir = devfs_mk_dir(NULL, "ts_ua", NULL);

  {
    int i;
    char name[4];

    for (i = 0; i < TS_USERACCESS_NUM_DEVICE; ++i) {
      snprintf(name, sizeof name, "%02d", i);

      useraccess_dev_list[i].devfs_handle =
        devfs_register(useraccess_devfs_dir,
                       name,
                       DEVFS_FL_DEFAULT,
                       useraccess_major,
                       i,
                       S_IFCHR | S_IRUSR | S_IWUSR,
                       &useraccess_fops,
                       &useraccess_dev_list[i]);

      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "TS IB add using major %d, minor %d",
               useraccess_major, i);

    }
  }

  return 0;
}

#endif /* TS_KERNEL_2_6 */

static int __init _tsIbUserInitModule(
                                      void
                                      ) {
  int i;

  for (i = 0; i < TS_USERACCESS_NUM_DEVICE; ++i) {
    useraccess_dev_list[i].ib_device = tsIbDeviceGetByIndex(i);
  }

  return _tsIbUserCreateDevices();
}

#if defined(TS_KERNEL_2_6)

/* Delete devices for kernel 2.6 */
static void __exit _tsIbUserDeleteDevices(
                                          void
                                          ) {
  unregister_chrdev_region(useraccess_devnum, TS_USERACCESS_NUM_DEVICE);
  cdev_del(&useraccess_cdev);
}

#else /* TS_KERNEL_2_6 */

/* Delete devices for kernel 2.4 */
static void __exit _tsIbUserDeleteDevices(
                                          void
                                          ) {
  devfs_unregister_chrdev(useraccess_major, TS_USERACCESS_NAME);

  {
    int i;

    for (i = 0; i < TS_USERACCESS_NUM_DEVICE; ++i) {
      if (useraccess_dev_list[i].devfs_handle) {
        devfs_unregister(useraccess_dev_list[i].devfs_handle);
      }
    }
  }

  devfs_unregister(useraccess_devfs_dir);
}

#endif /* TS_KERNEL_2_6 */

static void __exit _tsIbUserCleanupModule(
                                          void
                                          ) {
  TS_REPORT_CLEANUP(MOD_KERNEL_IB,
                    "Unloading IB userspace access");

  _tsIbUserDeleteDevices();

  TS_REPORT_CLEANUP(MOD_KERNEL_IB,
                    "IB userspace access unloaded");
}

module_init(_tsIbUserInitModule);
module_exit(_tsIbUserCleanupModule);
