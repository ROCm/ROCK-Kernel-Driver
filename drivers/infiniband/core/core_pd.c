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

  $Id: core_pd.c 32 2004-04-09 03:57:42Z roland $
*/

#include "core_priv.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/slab.h>

int ib_pd_create(tTS_IB_DEVICE_HANDLE device_handle,
                 void                *device_specific,
                 tTS_IB_PD_HANDLE    *pd_handle)
{
	struct ib_device *device = device_handle;
	struct ib_pd     *pd;
	int               ret;

	TS_IB_CHECK_MAGIC(device, DEVICE);

	if (!try_module_get(device->owner))
		return -ENODEV;

	if (!device->pd_create)
		return -ENOSYS;

	pd = kmalloc(sizeof *pd, GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	ret = device->pd_create(device, device_specific, pd);

	if (!ret) {
		TS_IB_SET_MAGIC(pd, PD);
		pd->device = device;
		*pd_handle = pd;
	} else {
		kfree(pd);
	}

	return ret;
}

int ib_pd_destroy(tTS_IB_PD_HANDLE pd_handle)
{
	struct ib_pd *pd = pd_handle;
	int           ret;

	TS_IB_CHECK_MAGIC(pd, PD);

	if (!pd->device->pd_destroy)
		return -ENOSYS;

	ret = pd->device->pd_destroy(pd);
	if (!ret) {
		module_put(pd->device->owner);
		TS_IB_CLEAR_MAGIC(pd);
		kfree(pd);
	}

	return ret;
}

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
