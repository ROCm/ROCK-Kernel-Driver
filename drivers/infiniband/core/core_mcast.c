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

  $Id: core_mcast.c 32 2004-04-09 03:57:42Z roland $
*/

#include "core_priv.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>

int ib_multicast_attach(tTS_IB_LID       multicast_lid,
                        tTS_IB_GID       multicast_gid,
                        tTS_IB_QP_HANDLE qp_handle)
{
	struct ib_qp *qp = qp_handle;

	TS_IB_CHECK_MAGIC(qp, QP);

	return qp->device->multicast_attach(qp, multicast_lid, multicast_gid);
}

int ib_multicast_detach(tTS_IB_LID       multicast_lid,
                        tTS_IB_GID       multicast_gid,
                        tTS_IB_QP_HANDLE qp_handle)
{
	struct ib_qp *qp = qp_handle;

	TS_IB_CHECK_MAGIC(qp, QP);

	return qp->device->multicast_detach(qp, multicast_lid, multicast_gid);
}

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
