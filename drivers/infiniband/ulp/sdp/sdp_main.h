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

  $Id: sdp_main.h 35 2004-04-09 05:34:32Z roland $
*/

#ifndef _TS_SDP_MAIN_H
#define _TS_SDP_MAIN_H
/*
 * error compatability to MS .NET SDP
 */
#if 0
#define _TS_SDP_MS_APRIL_ERROR_COMPAT
#endif
/*
 * print debug information in the main data path
 */
#if 0
#define _TS_SDP_DATA_PATH_DEBUG
#endif
/*
 * Perform entire protocol except for the user/kernel data copy.
 */
#if 0
#define _TS_SDP_DATA_PATH_NULL
#endif
/*
 * Force AIO patch support, should be handled by the makefile.
 */
#if 0
#define _TS_SDP_AIO_SUPPORT
#endif
/*
 * Force sendpage support, should be handled by the makefile.
 */
#if 0
#define _TS_SDP_SENDPAGE_SUPPORT
#endif
/*
 * turn off debuggable pool naming
 */
#if 0
#define _TS_SDP_DEBUG_POOL_NAME
#endif
/*
 * Mellanox A0 bug work around. SE amd UNSIG bits set, the event
 * gets a signal.
 */
#if 1
#define _TS_SDP_SE_UNSIG_BUG_WORKAROUND
#endif
/*
 * keep per connection statistics
 */
#if 0
#define _TS_SDP_CONN_STATS_REC
#endif
/*
 * keep state transition statistics.
 */
#if 0
#define _TS_SDP_CONN_STATE_REC
#endif
/*
 * topspin generic includes
 */
#include <ib_legacy_types.h>
#include <trace_codes.h>
#include <trace_masks.h>
#include <ts_kernel_trace.h>
#include <ts_kernel_services.h>
#include <ts_kernel_timer.h>
/*
 * kernel includes
 */
#include <asm/atomic.h>
#include <asm/byteorder.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/errno.h>
/* TODO: fix correctly (Scott). */
#ifndef ECANCELED
#define ECANCELED 125
#endif
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/net.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/socket.h>
#include <linux/ctype.h>
#include <linux/netdevice.h>
#include <linux/ip.h>
/*
 * RH AS 2.1 2.4.9-e.3 kernel has min and max in sock.h.
 * See ts_kernel_services.h for our definition.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,10)
#undef min
#undef max
#endif
#include <net/sock.h>
#include <net/route.h>
#include <net/dst.h>
#include <net/ip.h>
/*
 * topspin IB includes
 */
#include <ts_ib_core.h>
#include <ts_ib_cm.h>
#include <ts_ib_sa_client.h>
#include <ipoib_proto.h>
#include <ip2pr_export.h>
#include "sdp_sock.h"
#include "sdp_inet.h"
/*
 * public includes
 */
#include "sdp_types.h"
#include "sdp_buff.h"
#include "sdp_proc.h"
#include "sdp_proto.h"
#include "sdp_conn.h"
#include "sdp_dev.h"
#include "sdp_msgs.h"
#include "sdp_advt.h"
#include "sdp_iocb.h"
#include "sdp_os.h"
/*
 * gateway private includes
 */
#include "sdp_buff_p.h"

#endif /* _TS_SDP_MAIN_H */
