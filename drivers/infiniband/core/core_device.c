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

  $Id: core_device.c 32 2004-04-09 03:57:42Z roland $
*/

#include "core_priv.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/init.h>

#include <asm/semaphore.h>

static LIST_HEAD(device_list);
static LIST_HEAD(notifier_list);
static DECLARE_MUTEX(device_lock);

static int ib_device_check_mandatory(struct ib_device *device)
{
#define IB_MANDATORY_FUNC(x) { offsetof(tTS_IB_DEVICE_STRUCT, x), #x }
	static const struct {
		size_t offset;
		char  *name;
	} mandatory_table[] = {
		IB_MANDATORY_FUNC(device_query),
		IB_MANDATORY_FUNC(port_query),
		IB_MANDATORY_FUNC(pkey_query),
		IB_MANDATORY_FUNC(gid_query),
		IB_MANDATORY_FUNC(pd_create),
		IB_MANDATORY_FUNC(pd_destroy),
		IB_MANDATORY_FUNC(address_create),
		IB_MANDATORY_FUNC(address_destroy),
		IB_MANDATORY_FUNC(special_qp_create),
		IB_MANDATORY_FUNC(qp_modify),
		IB_MANDATORY_FUNC(qp_destroy),
		IB_MANDATORY_FUNC(send_post),
		IB_MANDATORY_FUNC(receive_post),
		IB_MANDATORY_FUNC(cq_create),
		IB_MANDATORY_FUNC(cq_destroy),
		IB_MANDATORY_FUNC(cq_poll),
		IB_MANDATORY_FUNC(cq_arm),
		IB_MANDATORY_FUNC(mr_register_physical),
		IB_MANDATORY_FUNC(mr_deregister)
	};
	int i;

	for (i = 0; i < sizeof mandatory_table / sizeof mandatory_table[0]; ++i) {
		if (!*(void **) ((void *) device + mandatory_table[i].offset)) {
			TS_REPORT_WARN(MOD_KERNEL_IB,
				       "Device %s is missing mandatory function %s",
				       device->name, mandatory_table[i].name);
			return -EINVAL;
		}
	}

	return 0;
}

int ib_device_register(struct ib_device *device)
{
	struct ib_device_private   *priv;
	struct ib_device_properties prop;
	int                         ret;
	int                         p;

	if (ib_device_check_mandatory(device)) {
		return -EINVAL;
	}

	priv = kmalloc(sizeof *priv, GFP_KERNEL);
	if (!priv) {
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "Couldn't allocate private struct for %s",
			       device->name);
		return -ENOMEM;
	}

	*priv = (struct ib_device_private) { 0 };

	ret = device->device_query(device, &prop);
	if (ret) {
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "device_query failed for %s",
			       device->name);
		return ret;
	}

	memcpy(priv->node_guid, prop.node_guid, sizeof (tTS_IB_GUID));

	if (prop.is_switch) {
		priv->start_port = priv->end_port = 0;

		if (!device->switch_ops) {
			TS_REPORT_WARN(MOD_KERNEL_IB,
				       "Device %s reports itself as a switch but has no switch_ops",
				       device->name);
		}
	} else {
		priv->start_port = 1;
		priv->end_port   = prop.num_port;
		
		if (device->switch_ops) {
			TS_REPORT_WARN(MOD_KERNEL_IB,
				       "Device %s reports itself as a CA but has switch_ops",
				       device->name);
		}
	}

	priv->port_data = kmalloc((priv->end_port + 1) * sizeof (struct ib_port_data),
				  GFP_KERNEL);
	if (!priv->port_data) {
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "Couldn't allocate port info for %s",
			       device->name);
		goto out_free;
	}

	for (p = priv->start_port; p <= priv->end_port; ++p) {
		spin_lock_init(&priv->port_data[p].port_cap_lock);
		memset(priv->port_data[p].port_cap_count, 0, IB_PORT_CAP_NUM * sizeof (int));
	}

	device->core = priv;

	INIT_LIST_HEAD(&priv->async_handler_list);
	spin_lock_init(&priv->async_handler_lock);

	if (ib_cache_setup(device)) {
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "Couldn't create device info cache for %s",
			       device->name);
		goto out_free_port;
	}

	if (tsKernelQueueThreadStart("ts_ib_completion",
				     ib_completion_thread,
				     device,
				     &priv->completion_thread)) {
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "Couldn't start completion thread for %s",
			       device->name);
		goto out_free_cache;
	}

	if (tsKernelQueueThreadStart("ts_ib_async",
				     ib_async_thread,
				     device,
				     &priv->async_thread)) {
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "Couldn't start async thread for %s",
			       device->name);
		goto out_stop_comp;
	}

	if (ib_proc_setup(device, prop.is_switch)) {
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "Couldn't create /proc dir for %s",
			       device->name);
		goto out_stop_async;
	}

	TS_IB_SET_MAGIC(device, DEVICE);

	down(&device_lock);
	list_add_tail(&device->core_list, &device_list);
	{
		struct list_head          *ptr;
		struct ib_device_notifier *notifier;

		list_for_each(ptr, &notifier_list) {
			notifier = list_entry(ptr, struct ib_device_notifier, list);
			notifier->notifier(notifier, device, IB_DEVICE_NOTIFIER_ADD);
		}
	}
	up(&device_lock);
	
	return 0;

 out_stop_async:
	tsKernelQueueThreadStop(priv->async_thread);

 out_stop_comp:
	tsKernelQueueThreadStop(priv->completion_thread);

 out_free_cache:
	ib_cache_cleanup(device);

 out_free_port:
	kfree(priv->port_data);

 out_free:
	kfree(priv);
	return -ENOMEM;
}


int ib_device_deregister(struct ib_device *device)
{
	struct ib_device_private *priv;

	TS_IB_CHECK_MAGIC(device, DEVICE);

	priv = device->core;

	if (tsKernelQueueThreadStop(priv->completion_thread)) {
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "tsKernelThreadStop failed for %s completion thread",
			       device->name);
	}

	if (tsKernelQueueThreadStop(priv->async_thread)) {
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "tsKernelThreadStop failed for %s completion thread",
			       device->name);
	}

	ib_proc_cleanup(device);
	ib_cache_cleanup(device);

	down(&device_lock);
	list_del(&device->core_list);
	{
		struct list_head          *ptr;
		struct ib_device_notifier *notifier;

		list_for_each_prev(ptr, &notifier_list) {
			notifier = list_entry(ptr, struct ib_device_notifier, list);
			notifier->notifier(notifier, device, IB_DEVICE_NOTIFIER_REMOVE);
		}
	}
	up(&device_lock);

	TS_IB_CLEAR_MAGIC(device);
	kfree(priv->port_data);
	kfree(priv);

	return 0;
}

tTS_IB_DEVICE_HANDLE ib_device_get_by_name(const char *name)
{
	struct list_head *ptr;
	struct ib_device *device;

	down(&device_lock);
	list_for_each(ptr, &device_list) {
		device = list_entry(ptr, tTS_IB_DEVICE_STRUCT, core_list);
		if (!strcmp(device->name, name))
			goto out;
	}
	device = TS_IB_HANDLE_INVALID;

 out:
	up(&device_lock);
	return device;
}

tTS_IB_DEVICE_HANDLE ib_device_get_by_index(int index)
{
	struct list_head *ptr;
	struct ib_device *device;

	if (index < 0)
		return TS_IB_HANDLE_INVALID;

	down(&device_lock);
	list_for_each(ptr, &device_list) {
		device = list_entry(ptr, tTS_IB_DEVICE_STRUCT, core_list);
		if (!index)
			goto out;
		--index;
	}

	device = TS_IB_HANDLE_INVALID;
 out:
	up(&device_lock);
	return device;
}

int ib_device_notifier_register(struct ib_device_notifier *notifier)
{
	struct list_head *ptr;
	struct ib_device *device;

	down(&device_lock);
	list_add_tail(&notifier->list, &notifier_list);
	list_for_each(ptr, &device_list) {
		device = list_entry(ptr, tTS_IB_DEVICE_STRUCT, core_list);
		notifier->notifier(notifier, device, IB_DEVICE_NOTIFIER_ADD);
	}
	up(&device_lock);

	return 0;
}

int ib_device_notifier_deregister(struct ib_device_notifier *notifier)
{
	down(&device_lock);
	list_del(&notifier->list);
	up(&device_lock);

	return 0;
}

int ib_device_properties_get(tTS_IB_DEVICE_HANDLE         device_handle,
			     struct ib_device_properties *properties)
{
	struct ib_device *device = device_handle;
	TS_IB_CHECK_MAGIC(device, DEVICE);

	return device->device_query ? device->device_query(device, properties) : -ENOSYS;
}

int ib_device_properties_set(tTS_IB_DEVICE_HANDLE      device_handle,
			     struct ib_device_changes *properties)
{
	struct ib_device *device = device_handle;
	TS_IB_CHECK_MAGIC(device, DEVICE);

	return device->device_modify ? device->device_modify(device, properties) : -ENOSYS;
}

int ib_port_properties_get(tTS_IB_DEVICE_HANDLE       device_handle,
			   tTS_IB_PORT                port,
			   struct ib_port_properties *properties)
{
	struct ib_device *device = device_handle;

	TS_IB_CHECK_MAGIC(device, DEVICE);

	return device->port_query ? device->port_query(device, port, properties) : -ENOSYS;
}

int ib_port_properties_set(tTS_IB_DEVICE_HANDLE    device_handle,
			   tTS_IB_PORT             port,
			   struct ib_port_changes *properties)
{
	struct ib_device *            device = device_handle;
	struct ib_device_private *priv;
	struct ib_port_changes    prop_set;
	unsigned long             flags;

	TS_IB_CHECK_MAGIC(device, DEVICE);

	priv = device->core;

	if (port < priv->start_port || port > priv->end_port) {
		return -EINVAL;
	}

	prop_set = *properties;

	spin_lock_irqsave(&priv->port_data[port].port_cap_lock, flags);

	if (properties->valid_fields & TS_IB_PORT_IS_SM) {
		priv->port_data[port].port_cap_count[IB_PORT_CAP_SM] +=
			2 * !!properties->is_sm - 1;
		if (priv->port_data[port].port_cap_count[IB_PORT_CAP_SM] < 0) {
			TS_REPORT_WARN(MOD_KERNEL_IB,
				       "'is SM' cap count decremented below 0");
			priv->port_data[port].port_cap_count[IB_PORT_CAP_SM] = 0;
		}
		prop_set.is_sm =
			!!priv->port_data[port].port_cap_count[IB_PORT_CAP_SM];
	}

	if (properties->valid_fields & TS_IB_PORT_IS_SNMP_TUNNELING_SUPPORTED) {
		priv->port_data[port].port_cap_count[IB_PORT_CAP_SNMP_TUN] +=
			2 * !!properties->is_snmp_tunneling_supported - 1;
		if (priv->port_data[port].port_cap_count[IB_PORT_CAP_SNMP_TUN] < 0) {
			TS_REPORT_WARN(MOD_KERNEL_IB,
				       "'is SNMP tunneling supported' cap count decremented below 0");
			priv->port_data[port].port_cap_count[IB_PORT_CAP_SNMP_TUN] = 0;
		}
		prop_set.is_snmp_tunneling_supported =
			!!priv->port_data[port].port_cap_count[IB_PORT_CAP_SNMP_TUN];
	}

	if (properties->valid_fields & TS_IB_PORT_IS_DEVICE_MANAGEMENT_SUPPORTED) {
		priv->port_data[port].port_cap_count[IB_PORT_CAP_DEV_MGMT] +=
			2 * !!properties->is_device_management_supported - 1;
		if (priv->port_data[port].port_cap_count[IB_PORT_CAP_DEV_MGMT] < 0) {
			TS_REPORT_WARN(MOD_KERNEL_IB,
				       "'is device management supported' cap count decremented below 0");
			priv->port_data[port].port_cap_count[IB_PORT_CAP_DEV_MGMT] = 0;
		}
		prop_set.is_device_management_supported =
			!!priv->port_data[port].port_cap_count[IB_PORT_CAP_DEV_MGMT];
	}

	if (properties->valid_fields & TS_IB_PORT_IS_VENDOR_CLASS_SUPPORTED) {
		priv->port_data[port].port_cap_count[IB_PORT_CAP_VEND_CLASS] +=
			2 * !!properties->is_vendor_class_supported - 1;
		if (priv->port_data[port].port_cap_count[IB_PORT_CAP_VEND_CLASS] < 0) {
			TS_REPORT_WARN(MOD_KERNEL_IB,
				       "'is vendor class supported' cap count decremented below 0");
			priv->port_data[port].port_cap_count[IB_PORT_CAP_VEND_CLASS] = 0;
		}
		prop_set.is_vendor_class_supported =
			!!priv->port_data[port].port_cap_count[IB_PORT_CAP_VEND_CLASS];
	}

	spin_unlock_irqrestore(&priv->port_data[port].port_cap_lock, flags);

	return device->port_modify ? device->port_modify(device, port, &prop_set) : -ENOSYS;
}

int ib_pkey_entry_get(tTS_IB_DEVICE_HANDLE device_handle,
		      tTS_IB_PORT          port,
		      int                  index,
		      tTS_IB_PKEY         *pkey)
{
	struct ib_device *device = device_handle;

	TS_IB_CHECK_MAGIC(device, DEVICE);

	return device->pkey_query ? device->pkey_query(device, port, index, pkey) : -ENOSYS;
}

int ib_gid_entry_get(tTS_IB_DEVICE_HANDLE device_handle,
		     tTS_IB_PORT          port,
		     int                  index,
		     tTS_IB_GID           gid)
{
	struct ib_device *device = device_handle;

	TS_IB_CHECK_MAGIC(device, DEVICE);

	return device->gid_query ? device->gid_query(device, port, index, gid) : -ENOSYS;
}

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
