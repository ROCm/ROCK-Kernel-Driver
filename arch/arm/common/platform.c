#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/init.h>

int __init platform_add_device(struct platform_device *dev)
{
	int i;

	for (i = 0; i < dev->num_resources; i++) {
		struct resource *r = &dev->resource[i];

		r->name = dev->dev.bus_id;

		if (r->flags & IORESOURCE_MEM &&
		    request_resource(&iomem_resource, r)) {
			printk(KERN_ERR
			       "%s%d: failed to claim resource %d\n",
			       dev->name, dev->id, i);
			break;
		}
	}
	if (i == dev->num_resources)
		platform_device_register(dev);
	return 0;
}

int __init platform_add_devices(struct platform_device **devs, int num)
{
	int i;

	for (i = 0; i < num; i++)
		platform_add_device(devs[i]);

	return 0;
}
