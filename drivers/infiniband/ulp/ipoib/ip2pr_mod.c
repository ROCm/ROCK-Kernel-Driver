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

  $Id: ip2pr_mod.c 32 2004-04-09 03:57:42Z roland $
*/

#include "ip2pr_priv.h"

MODULE_AUTHOR("Raghava Kondapalli");
MODULE_DESCRIPTION("IB path record lookup module");
MODULE_LICENSE("Dual BSD/GPL");

extern tINT32 tsIp2prLinkAddrInit(
  void
  );
extern tINT32 tsIp2prLinkAddrCleanup(
  void
  );
extern tINT32 _tsIp2prUserLookup(
  unsigned long arg
  );
extern tINT32 _tsGid2prUserLookup(
  unsigned long arg
  );
extern tINT32 tsIp2prProcFsInit(
  void
  );
extern tINT32 tsIp2prProcFsCleanup(
  void
  );
extern tINT32 tsIp2prSrcGidInit(
  void
  );
extern tINT32 tsIp2prSrcGidCleanup(
  void
  );

static int ip2pr_major_number = 240;
static int _tsIp2prOpen(struct inode *inode, struct file *fp);
static int _tsIp2prClose(struct inode *inode, struct file *fp);
static int _tsIp2prIoctl(struct inode *inode, struct file *fp, unsigned int cmd,
		         unsigned long arg);

static struct file_operations ip2pr_fops = {
  .owner   = THIS_MODULE,
  .ioctl   =_tsIp2prIoctl,
  .open    =_tsIp2prOpen,
  .release = _tsIp2prClose,
};
/* ========================================================================= */
/*..tsIp2prOpen -- Driver Open Entry Point */
static int _tsIp2prOpen
(
 struct inode *inode,
 struct file *fp
)
{
    TS_ENTER(MOD_IP2PR);
    return 0;
}

/* ========================================================================= */
/*..tsIp2prClose -- Driver Close Entry Point */
static int _tsIp2prClose
(
 struct inode *inode,
 struct file *fp
)
{
    TS_ENTER(MOD_IP2PR);
    return 0;
}

/* ========================================================================= */
/*..tsIp2prIoctl -- Driver Ioctl Entry Point */
static int _tsIp2prIoctl
(
 struct inode *inode,
 struct file *fp,
 unsigned int cmd,
 unsigned long arg
)
{
  int   result;

  if (_IOC_TYPE(cmd) != IP2PR_IOC_MAGIC) {
    return (-EINVAL);
  }

  switch (cmd) {
    case IP2PR_IOC_LOOKUP_REQ:
      result = _tsIp2prUserLookup(arg);
      break;
    case GID2PR_IOC_LOOKUP_REQ:
      result = _tsGid2prUserLookup(arg);
      break;
    default:
      result = -EINVAL;
  }

  return (result);
}

/* --------------------------------------------------------------------- */
/*                                                                       */
/* Path Record lookup host module load/unload functions                  */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*..prlookup_init -- initialize the PathRecord Lookup host module */
int __init tsIp2prDriverInitModule
(
 void
)
{
  tINT32        result = 0;

  TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_INOUT,
	   "INIT: Path Record Lookup module load.");

  result = register_chrdev(ip2pr_major_number, IP2PR_DEVNAME, &ip2pr_fops);
  if (0 > result) {
    TS_REPORT_FATAL(MOD_IP2PR, "Device registration failed");
    return (result);
  }
  if (ip2pr_major_number == 0)
    ip2pr_major_number = result;

  result = tsIp2prProcFsInit();
  if (0 > result) {
    TS_REPORT_FATAL(MOD_IP2PR, "Init: Error creating proc entries");
    unregister_chrdev(ip2pr_major_number, IP2PR_DEVNAME);
    return (result);
  }

  result = tsIp2prLinkAddrInit();
  if (0 > result) {
    TS_REPORT_FATAL(MOD_IP2PR, "Device resource allocation failed");
    (void)tsIp2prProcFsCleanup();
    unregister_chrdev(ip2pr_major_number, IP2PR_DEVNAME);
    return (result);
  }

  result = tsIp2prSrcGidInit();
  if (0 > result) {
    TS_REPORT_FATAL(MOD_IP2PR, "Gid resource allocation failed");
    (void)tsIp2prLinkAddrCleanup();
    (void)tsIp2prProcFsCleanup();
    unregister_chrdev(ip2pr_major_number, IP2PR_DEVNAME);
    return (result);
  }

  return (result);
}

static void __exit tsIp2prDriverCleanupModule
(
 void
)
{
  TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_INOUT,
	   "INIT: Path Record Lookup module load.");

  if (unregister_chrdev(ip2pr_major_number, IP2PR_DEVNAME) != 0) {
    TS_REPORT_WARN(MOD_UDAPL, "Cannot unregister device");
  }

  /*
   * Src Gid Cleanup
   */
  (void)tsIp2prSrcGidCleanup();
  /*
   * link level addressing services.
   */
  (void)tsIp2prLinkAddrCleanup();

  /*
   * proc tables
   */
  (void)tsIp2prProcFsCleanup();
}

module_init(tsIp2prDriverInitModule);
module_exit(tsIp2prDriverCleanupModule);
