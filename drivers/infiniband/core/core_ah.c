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

  $Id: core_ah.c 32 2004-04-09 03:57:42Z roland $
*/

#include "core_priv.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/slab.h>

int ib_address_create(tTS_IB_PD_HANDLE          pd_handle,
                      struct ib_address_vector *address_vector,
                      tTS_IB_ADDRESS_HANDLE    *address_handle)
{
	struct ib_pd      *pd;
	struct ib_address *address;
	int                ret;

	TS_IB_CHECK_MAGIC(pd_handle, PD);
	pd = pd_handle;

	if (!pd->device->address_create) {
		return -ENOSYS;
	}

	address = kmalloc(sizeof *address, GFP_KERNEL);
	if (!address) {
		return -ENOMEM;
	}

	ret = pd->device->address_create(pd, address_vector, address);

	if (!ret) {
		TS_IB_SET_MAGIC(address, ADDRESS);
		address->device = pd->device;
		*address_handle = address;
	} else {
		kfree(address);
	}

	return ret;
}

int ib_address_query(tTS_IB_ADDRESS_HANDLE     address_handle,
		     struct ib_address_vector *address_vector)
{
	struct ib_address *address = address_handle;

	TS_IB_CHECK_MAGIC(address, ADDRESS);

	return address->device->address_query ?
		address->device->address_query(address, address_vector) : -ENOSYS;
}

int ib_address_destroy(tTS_IB_ADDRESS_HANDLE address_handle)
{
	struct ib_address *address = address_handle;
	int                ret;

	TS_IB_CHECK_MAGIC(address, ADDRESS);

	if (!address->device->address_destroy) {
		return -ENOSYS;
	}

	ret = address->device->address_destroy(address);
	if (!ret) {
		TS_IB_CLEAR_MAGIC(address);
		kfree(address);
	}

	return ret;
}

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
