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

  $Id: ipoib_proc.c 32 2004-04-09 03:57:42Z roland $
*/

#include "ipoib.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/ctype.h>
#include <linux/module.h>

#include <asm/uaccess.h>
#include <asm/semaphore.h>

/*
 * ARP proc file stuff
 */

static const char ipoib_arp_proc_entry_name[] = "ipoib_arp_%s";
/*
   we have a static variable to hold the device pointer between when
   the /proc file is opened and the seq_file start function is
   called.  (This is a kludge to get around the fact that we don't get
   to pass user data to the seq_file start function)
*/
static DECLARE_MUTEX(proc_arp_mutex);
static struct net_device *proc_arp_device;

/* =============================================================== */
/*.._tsIpoibSeqStart -- seq file handling                          */
static void *_tsIpoibSeqStart(
                              struct seq_file *file,
                              loff_t *pos
                              ) {
  tTS_IPOIB_ARP_ITERATOR iter = tsIpoibArpIteratorInit(proc_arp_device);
  loff_t n = *pos;

  while (n--) {
    if (tsIpoibArpIteratorNext(iter)) {
      tsIpoibArpIteratorFree(iter);
      return NULL;
    }
  }

  return iter;
}

/* =============================================================== */
/*.._tsIpoibSeqNext -- seq file handling                           */
static void *_tsIpoibSeqNext(
                             struct seq_file *file,
                             void *iter_ptr,
                             loff_t *pos
                             ) {
  tTS_IPOIB_ARP_ITERATOR iter = iter_ptr;

  (*pos)++;

  if (tsIpoibArpIteratorNext(iter)) {
    tsIpoibArpIteratorFree(iter);
    return NULL;
  }

  return iter;
}

/* =============================================================== */
/*.._tsIpoibSeqStop -- seq file handling                           */
static void _tsIpoibSeqStop(
                            struct seq_file *file,
                            void *iter_ptr
                            ) {
  /* nothing for now */
}

/* =============================================================== */
/*.._tsIpoibSeqShow -- seq file handling                           */
static int _tsIpoibSeqShow(
                           struct seq_file *file,
                           void *iter_ptr
                           ) {
  tTS_IPOIB_ARP_ITERATOR iter = iter_ptr;
  uint8_t hash[TS_IPOIB_ADDRESS_HASH_BYTES];
  char gid_buf[sizeof "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"];
  tTS_IB_GID gid;
  tTS_IB_QPN qpn;
  int i, n;
  unsigned long created, last_verify;
  unsigned int queuelen, complete;

  if (iter) {
    tsIpoibArpIteratorRead(iter, hash, gid, &qpn, &created, &last_verify, &queuelen, &complete);

    for (i = 0; i < TS_IPOIB_ADDRESS_HASH_BYTES; ++i) {
      seq_printf(file, "%02x", hash[i]);
      if (i < TS_IPOIB_ADDRESS_HASH_BYTES - 1) {
        seq_putc(file, ':');
      } else {
        seq_printf(file, "  ");
      }
    }

    for (n = 0, i = 0; i < sizeof (tTS_IB_GID) / 2; ++i) {
      n += sprintf(gid_buf + n, "%x", be16_to_cpu(((uint16_t *) gid)[i]));
      if (i < sizeof (tTS_IB_GID) / 2 - 1) {
        gid_buf[n++] = ':';
      }
    }
  }

  seq_printf(file, "GID: %*s", -(1 + (int) sizeof gid_buf), gid_buf);
  seq_printf(file, "QP#: 0x%06x", qpn);

  seq_printf(file, " created: %10ld last_verify: %10ld queuelen: %4d complete: %d\n",
             created, last_verify, queuelen, complete);

  return 0;
}

static struct seq_operations ipoib_seq_operations = {
  .start = _tsIpoibSeqStart,
  .next  = _tsIpoibSeqNext,
  .stop  = _tsIpoibSeqStop,
  .show  = _tsIpoibSeqShow
};

/* =============================================================== */
/*.._tsIpoibArpProcDeviceOpen -- proc file handling                */
static int _tsIpoibArpProcDeviceOpen(
                                     struct inode *inode,
                                     struct file *file
                                     ) {
  struct proc_dir_entry *pde = PDE(inode);

  if (down_interruptible(&proc_arp_mutex)) {
    return -ERESTARTSYS;
  }

  proc_arp_device = pde->data;
  {
    struct net_device *dev = proc_arp_device;
    TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_GEN,
             "%s: opening proc file",
             dev->name);
  }

  return seq_open(file, &ipoib_seq_operations);
}

/*
  _tsIpoibAsciiToGid is adapted from BSD's inet_pton6, which was
  originally written by Paul Vixie
*/

/* =============================================================== */
/*.._tsIpoibAsciiToGid -- read GID from string                     */
static int _tsIpoibAsciiToGid(
                              const char *src,
                              tTS_IB_GID dst
                              ) {
  static const char xdigits[] = "0123456789abcdef";
  unsigned char *tp, *endp, *colonp;
  const char *curtok;
  int ch, saw_xdigit;
  unsigned int val;

  memset((tp = dst), 0, sizeof (tTS_IB_GID));
  endp = tp + sizeof (tTS_IB_GID);
  colonp = NULL;

  /* Leading :: requires some special handling. */
  if (*src == ':' && *++src != ':') {
    return 0;
  }

  curtok     = src;
  saw_xdigit = 0;
  val        = 0;

  while ((ch = *src++) != '\0') {
    const char *pch;

    pch = strchr(xdigits, tolower(ch));

    if (pch) {
      val <<= 4;
      val |= (pch - xdigits);
      if (val > 0xffff) {
        return 0;
      }
      saw_xdigit = 1;
      continue;
    }

    if (ch == ':') {
      curtok = src;

      if (!saw_xdigit) {
        if (colonp) {
          return 0;
        }
        colonp = tp;
        continue;
      } else if (*src == '\0') {
        return 0;
      }

      if (tp + 2 > endp) {
        return 0;
      }

      *tp++ = (u_char) (val >> 8) & 0xff;
      *tp++ = (u_char) val & 0xff;
      saw_xdigit = 0;
      val = 0;
      continue;
    }

    return 0;
  }

  if (saw_xdigit) {
    if (tp + 2 > endp) {
      return 0;
    }
    *tp++ = (u_char) (val >> 8) & 0xff;
    *tp++ = (u_char) val & 0xff;
  }

  if (colonp) {
    memmove(endp - (tp - colonp), colonp, tp - colonp);
    memset(colonp, 0, tp - colonp);
    tp = endp;
  }

  if (tp != endp) {
    return 0;
  }

  return 1;
}

/* =============================================================== */
/*.._tsIpoibArpProcDeviceWrite -- proc file handling               */
static ssize_t _tsIpoibArpProcDeviceWrite(
                                          struct file *file,
                                          const char *buffer,
                                          size_t count,
                                          loff_t *pos
                                          ) {
  tTS_IPOIB_ARP_ENTRY entry;
  char kernel_buf[256];
  char gid_buf[sizeof "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"];
  tTS_IB_GID gid;
  tTS_IB_QPN qpn;

  count = min(count, sizeof kernel_buf);

  if (copy_from_user(kernel_buf, buffer, count)) {
    return -EFAULT;
  }
  kernel_buf[count - 1] = '\0';

  if (sscanf(kernel_buf, "%39s %i", gid_buf, &qpn) != 2) {
    return -EINVAL;
  }

  if (!_tsIpoibAsciiToGid(gid_buf, gid)) {
    return -EINVAL;
  }

  if (qpn > 0xffffff) {
    return -EINVAL;
  }

  entry = tsIpoibDeviceArpAdd(proc_arp_device, gid, qpn);
  if (entry) {
    tsIpoibArpEntryPut(entry);
  }

  return count;
}

/* =============================================================== */
/*.._tsIpoibArpProcDeviceRelease -- proc file handling             */
static int _tsIpoibArpProcDeviceRelease(
                                        struct inode *inode,
                                        struct file *file
                                        ) {
  up(&proc_arp_mutex);

  return seq_release(inode, file);
}

static struct file_operations ipoib_arp_proc_device_operations = {
  .open    = _tsIpoibArpProcDeviceOpen,
  .read    = seq_read,
  .write   = _tsIpoibArpProcDeviceWrite,
  .llseek  = seq_lseek,
  .release = _tsIpoibArpProcDeviceRelease,
};

/*
 * Multicast proc stuff
 */

static const char ipoib_mcast_proc_entry_name[] = "ipoib_mcast_%s";
/*
   we have a static variable to hold the device pointer between when
   the /proc file is opened and the seq_file start function is
   called.  (This is a kludge to get around the fact that we don't get
   to pass user data to the seq_file start function)
*/
static DECLARE_MUTEX(proc_mcast_mutex);
static struct net_device *proc_mcast_device;

/* =============================================================== */
/*.._tsIpoibMulticastSeqStart -- seq file handling                 */
static void *_tsIpoibMulticastSeqStart(
                                       struct seq_file *file,
                                       loff_t *pos
                                       ) {
  tTS_IPOIB_MULTICAST_ITERATOR iter = tsIpoibMulticastIteratorInit(proc_mcast_device);
  loff_t n = *pos;

  while (n--) {
    if (tsIpoibMulticastIteratorNext(iter)) {
      tsIpoibMulticastIteratorFree(iter);
      return NULL;
    }
  }

  return iter;
}

/* =============================================================== */
/*.._tsIpoibMulticastSeqNext -- seq file handling                  */
static void *_tsIpoibMulticastSeqNext(
                                      struct seq_file *file,
                                      void *iter_ptr,
                                      loff_t *pos
                                      ) {
  tTS_IPOIB_MULTICAST_ITERATOR iter = iter_ptr;

  (*pos)++;

  if (tsIpoibMulticastIteratorNext(iter)) {
    tsIpoibMulticastIteratorFree(iter);
    return NULL;
  }

  return iter;
}

/* =============================================================== */
/*.._tsIpoibMulticastSeqStop -- seq file handling                  */
static void _tsIpoibMulticastSeqStop(
                                     struct seq_file *file,
                                     void *iter_ptr
                                     ) {
  /* nothing for now */
}

/* =============================================================== */
/*.._tsIpoibMulticastSeqShow -- seq file handling                  */
static int _tsIpoibMulticastSeqShow(
                                    struct seq_file *file,
                                    void *iter_ptr
                                    ) {
  tTS_IPOIB_MULTICAST_ITERATOR iter = iter_ptr;
  char gid_buf[sizeof "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"];
  tTS_IB_GID mgid;
  int i, n;
  unsigned long created;
  unsigned int queuelen, complete, send_only;

  if (iter) {
    tsIpoibMulticastIteratorRead(iter, mgid, &created, &queuelen, &complete, &send_only);

    for (n = 0, i = 0; i < sizeof (tTS_IB_GID) / 2; ++i) {
      n += sprintf(gid_buf + n, "%x", be16_to_cpu(((uint16_t *) mgid)[i]));
      if (i < sizeof (tTS_IB_GID) / 2 - 1) {
        gid_buf[n++] = ':';
      }
    }
  }

  seq_printf(file, "GID: %*s", -(1 + (int) sizeof gid_buf), gid_buf);

  seq_printf(file, " created: %10ld queuelen: %4d complete: %d send_only: %d\n",
             created, queuelen, complete, send_only);

  return 0;
}

static struct seq_operations ipoib_mcast_seq_operations = {
  .start = _tsIpoibMulticastSeqStart,
  .next  = _tsIpoibMulticastSeqNext,
  .stop  = _tsIpoibMulticastSeqStop,
  .show  = _tsIpoibMulticastSeqShow
};

/* =============================================================== */
/*.._tsIpoibMulticastProcDeviceOpen -- proc file handling          */
static int _tsIpoibMulticastProcDeviceOpen(
                                           struct inode *inode,
                                           struct file *file
                                           ) {
  struct proc_dir_entry *pde = PDE(inode);

  if (down_interruptible(&proc_mcast_mutex)) {
    return -ERESTARTSYS;
  }

  proc_mcast_device = pde->data;
  {
    struct net_device *dev = proc_mcast_device;
    TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_GEN,
             "%s: opening proc file",
             dev->name);
  }

  return seq_open(file, &ipoib_mcast_seq_operations);
}

/* =============================================================== */
/*.._tsIpoibMulticastProcDeviceRelease -- proc file handling       */
static int _tsIpoibMulticastProcDeviceRelease(
                                              struct inode *inode,
                                              struct file *file
                                              ) {
  up(&proc_mcast_mutex);

  return seq_release(inode, file);
}

static struct file_operations ipoib_mcast_proc_device_operations = {
  .open    = _tsIpoibMulticastProcDeviceOpen,
  .read    = seq_read,
  .llseek  = seq_lseek,
  .release = _tsIpoibMulticastProcDeviceRelease,
};

/* =============================================================== */
/*..tsIpoibDeviceProcInit -- set up ipoib_arp in /proc             */
int tsIpoibDeviceProcInit(
                          struct net_device *dev
                          ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  char name[sizeof ipoib_arp_proc_entry_name + sizeof dev->name];

  snprintf(name, sizeof name - 1, ipoib_arp_proc_entry_name, dev->name);
  priv->arp_proc_entry = create_proc_entry(name,
                                           S_IRUGO | S_IWUGO,
                                           tsKernelProcDirGet());

  if (!priv->arp_proc_entry) {
    TS_REPORT_FATAL(MOD_IB_NET,
                    "%s: Can't create %s in proc directory",
                    dev->name, name);
    return -ENOMEM;
  }

  priv->arp_proc_entry->proc_fops = &ipoib_arp_proc_device_operations;
  priv->arp_proc_entry->data      = dev;

  snprintf(name, sizeof name - 1, ipoib_mcast_proc_entry_name, dev->name);
  priv->mcast_proc_entry = create_proc_entry(name,
                                             S_IRUGO,
                                             tsKernelProcDirGet());
  if (!priv->mcast_proc_entry) {
    TS_REPORT_FATAL(MOD_IB_NET,
                    "%s: Can't create %s in proc directory",
                    dev->name, name);
    /* FIXME: Delete ARP proc entry */
    return -ENOMEM;
  }

  priv->mcast_proc_entry->proc_fops = &ipoib_mcast_proc_device_operations;
  priv->mcast_proc_entry->data      = dev;

  return 0;
}

/* =============================================================== */
/*..tsIpoibDeviceProcCleanup -- unregister /proc file              */
void tsIpoibDeviceProcCleanup(
                              struct net_device *dev
                              ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  char name[sizeof ipoib_arp_proc_entry_name + sizeof dev->name];

  if (priv->arp_proc_entry) {
    snprintf(name, sizeof name - 1, ipoib_arp_proc_entry_name, dev->name);

    TS_REPORT_CLEANUP(MOD_IB_NET,
                      "%s: removing proc file %s",
                      dev->name, name);

    remove_proc_entry(name, tsKernelProcDirGet());
  }

  if (priv->mcast_proc_entry) {
    snprintf(name, sizeof name - 1, ipoib_mcast_proc_entry_name, dev->name);

    TS_REPORT_CLEANUP(MOD_IB_NET,
                      "%s: removing proc file %s",
                      dev->name, name);

    remove_proc_entry(name, tsKernelProcDirGet());
  }
}

#ifndef TS_HOST_DRIVER
/*
 * chassid proc file stuff
 */

struct proc_dir_entry *mcast_chassisid_proc_entry;

extern unsigned long long mcast_chassisid;

/* =============================================================== */
/*.._tsIpoibChassisIdProcLseek -- proc file handling               */
static loff_t _tsIpoibChassisIdProcLseek(
                                         struct file *file,
                                         loff_t off,
                                         int whence
                                         ) {
  char buffer[sizeof "01234567890123456789\n"];
  loff_t new;

  snprintf(buffer, sizeof buffer, "%Lx\n", mcast_chassisid);

  switch (whence) {
  case 0:
    new = off;
    break;
  case 1:
    new = file->f_pos + off;
    break;
  case 2:
  default:
    return -EINVAL;
  }
  if (new < 0 || new > strlen(buffer))
    return -EINVAL;
  return (file->f_pos = new);
}

/* =============================================================== */
/*.._tsIpoibChassisIdProcRead -- proc file handling                */
static ssize_t _tsIpoibChassisIdProcRead(
                                         struct file *file,
                                         char *buf,
                                         size_t nbytes,
                                         loff_t *ppos
                                         ) {
  char buffer[sizeof "01234567890123456789\n"];
  unsigned int pos;
  unsigned int size;

  snprintf(buffer, sizeof buffer, "%Lx\n", mcast_chassisid);

  pos = *ppos;
  size = strlen(buffer);
  if (pos >= size)
    return 0;
  if (nbytes >= size)
    nbytes = size;
  if (pos + nbytes > size)
    nbytes = size - pos;

  if (!access_ok(VERIFY_WRITE, buf, nbytes))
    return -EINVAL;

  copy_to_user(buf, buffer + pos, nbytes);

  *ppos += nbytes;

  return nbytes;
}

/* =============================================================== */
/*.._tsIpoibChassisIdProcWrite -- proc file handling               */
static ssize_t _tsIpoibChassisIdProcWrite(
                                          struct file *file,
                                          const char *buffer,
                                          size_t count,
                                          loff_t *pos
                                          ) {
  char kernel_buf[256];

  count = min(count, sizeof kernel_buf);

  if (copy_from_user(kernel_buf, buffer, count)) {
    return -EFAULT;
  }
  kernel_buf[count - 1] = '\0';

  if (sscanf(kernel_buf, "%Lx", &mcast_chassisid) != 1) {
    return -EINVAL;
  }

  return count;
}

static struct file_operations ipoib_chassisid_proc_operations = {
  .owner   = THIS_MODULE,
  .llseek  = _tsIpoibChassisIdProcLseek,
  .read    = _tsIpoibChassisIdProcRead,
  .write   = _tsIpoibChassisIdProcWrite,
};
#endif

/* =============================================================== */
/*..tsIpoibProcInit -- set up ipoib_mcast_chassisid in /proc       */
int tsIpoibProcInit(
                    void
                    ) {
#ifndef TS_HOST_DRIVER
  mcast_chassisid_proc_entry = create_proc_entry("ipoib_mcast_chassisid",
                                       S_IRUGO | S_IWUGO,
                                       tsKernelProcDirGet());

  if (!mcast_chassisid_proc_entry) {
    TS_REPORT_FATAL(MOD_IB_NET,
                    "Can't create ipoib_mcast_chassisid in proc directory\n");
    return -ENOMEM;
  }

  mcast_chassisid_proc_entry->proc_fops = &ipoib_chassisid_proc_operations;
#endif

  return 0;
}

/* =============================================================== */
/*..tsIpoibProcCleanup -- unregister /proc file                    */
void tsIpoibProcCleanup(
                        void
                        ) {
#ifndef TS_HOST_DRIVER
  if (mcast_chassisid_proc_entry) {
    TS_REPORT_CLEANUP(MOD_IB_NET,
                      "removing proc file ipoib_mcast_chassisid");

    remove_proc_entry("ipoib_mcast_chassisid", tsKernelProcDirGet());
  } else {
    TS_REPORT_WARN(MOD_IB_NET,
                   "no proc file to remove");
  }
#endif
}
