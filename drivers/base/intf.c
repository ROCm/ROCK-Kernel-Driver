/*
 * intf.c - class-specific interface management
 */

#define DEBUG 1

#include <linux/device.h>
#include <linux/module.h>
#include "base.h"


#define to_intf(node) container_of(node,struct device_interface,node)

int interface_register(struct device_interface * intf)
{
	struct device_class * cls = intf->devclass;

	if (cls) {
		pr_debug("register interface '%s' with class '%s\n",
			 intf->name,cls->name);
		intf_make_dir(intf);
		spin_lock(&device_lock);
		list_add_tail(&intf->node,&cls->intf_list);
		spin_unlock(&device_lock);
		return 0;
	}
	return -EINVAL;
}

void interface_unregister(struct device_interface * intf)
{
	pr_debug("unregistering interface '%s' from class '%s'\n",
		 intf->name,intf->devclass->name);
	spin_lock(&device_lock);
	list_del_init(&intf->node);
	spin_unlock(&device_lock);

	intf_remove_dir(intf);
}

int interface_add(struct device_class * cls, struct device * dev)
{
	struct list_head * node;
	int error = 0;

	pr_debug("adding '%s' to %s class interfaces\n",dev->name,cls->name);

	list_for_each(node,&cls->intf_list) {
		struct device_interface * intf = to_intf(node);
		if (intf->add_device) {
			error = intf->add_device(dev);
			if (error)
				pr_debug("%s:%s: adding '%s' failed: %d\n",
					 cls->name,intf->name,dev->name,error);
		}
		
	}
	return 0;
}

void interface_remove(struct device_class * cls, struct device * dev)
{
	struct list_head * node;
	struct list_head * next;

	pr_debug("remove '%s' from %s class interfaces: ",dev->name,cls->name);

	spin_lock(&device_lock);
	list_for_each_safe(node,next,&dev->intf_list) {
		struct intf_data * intf_data = container_of(node,struct intf_data,node);
		list_del_init(&intf_data->node);
		spin_unlock(&device_lock);

		intf_dev_unlink(intf_data);
		pr_debug("%s ",intf_data->intf->name);
		if (intf_data->intf->remove_device)
			intf_data->intf->remove_device(intf_data);

		spin_lock(&device_lock);
	}
	spin_unlock(&device_lock);
	pr_debug("\n");
}

int interface_add_data(struct intf_data * data)
{
	spin_lock(&device_lock);
	list_add_tail(&data->node,&data->dev->intf_list);
	data->intf_num = ++data->intf->devnum;
	spin_unlock(&device_lock);
	intf_dev_link(data);
	return 0;
}

EXPORT_SYMBOL(interface_register);
EXPORT_SYMBOL(interface_unregister);
