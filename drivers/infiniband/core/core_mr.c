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

  $Id: core_mr.c 32 2004-04-09 03:57:42Z roland $
*/

#include "core_priv.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/slab.h>

int ib_memory_register(tTS_IB_PD_HANDLE     pd_handle,
                       void                *start_address,
                       uint64_t             buffer_size,
                       tTS_IB_MEMORY_ACCESS access,
                       tTS_IB_MR_HANDLE    *mr_handle,
                       tTS_IB_LKEY         *lkey,
                       tTS_IB_RKEY         *rkey)
{
	struct ib_pd *pd = pd_handle;
	struct ib_mr *mr;
	int           ret;

	TS_IB_CHECK_MAGIC(pd, PD);

	if (!pd->device->mr_register) {
		return -ENOSYS;
	}

	mr = kmalloc(sizeof *mr, GFP_KERNEL);
	if (!mr) {
		return -ENOMEM;
	}

	ret = pd->device->mr_register(pd, start_address, buffer_size, access, mr);

	if (!ret) {
		TS_IB_SET_MAGIC(mr, MR);
		mr->device = pd->device;
		*mr_handle = mr;
		*lkey      = mr->lkey;
		*rkey      = mr->rkey;
	} else {
		kfree(mr);
	}

	return ret;
}

int ib_memory_register_physical(tTS_IB_PD_HANDLE           pd_handle,
				struct ib_physical_buffer *buffer_list,
				int                    	  list_len,
				uint64_t               	 *io_virtual_address,
				uint64_t               	  buffer_size,
				uint64_t               	  iova_offset,
				tTS_IB_MEMORY_ACCESS   	  access,
				tTS_IB_MR_HANDLE       	 *mr_handle,
				tTS_IB_LKEY            	 *lkey,
				tTS_IB_RKEY            	 *rkey)
{
	struct ib_pd *pd = pd_handle;
	struct ib_mr *mr;
	int           ret;

	TS_IB_CHECK_MAGIC(pd, PD);

	if (!pd->device->mr_register_physical) {
		return -ENOSYS;
	}

	mr = kmalloc(sizeof *mr, GFP_KERNEL);
	if (!mr) {
		return -ENOMEM;
	}

	ret = pd->device->mr_register_physical(pd,
					       buffer_list,
					       list_len,
					       io_virtual_address,
					       buffer_size,
					       iova_offset,
					       access,
					       mr);

	if (!ret) {
		TS_IB_SET_MAGIC(mr, MR);
		mr->device = pd->device;
		*mr_handle = mr;
		*lkey      = mr->lkey;
		*rkey      = mr->rkey;
	} else {
		kfree(mr);
	}

	return ret;
}

int ib_memory_deregister(tTS_IB_MR_HANDLE mr_handle)
{
	struct ib_mr *mr = mr_handle;
	int           ret;

	TS_IB_CHECK_MAGIC(mr, MR);

	if (!mr->device->mr_deregister) {
		return -ENOSYS;
	}

	ret = mr->device->mr_deregister(mr);
	if (!ret) {
		TS_IB_CLEAR_MAGIC(mr);
		kfree(mr);
	}

	return ret;
}

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
