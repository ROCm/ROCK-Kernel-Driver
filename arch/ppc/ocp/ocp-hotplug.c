#include <asm/ocp.h>
#include <linux/module.h>
#include <linux/kmod.h>		/* for hotplug_path */

#ifndef FALSE
#define FALSE	(0)
#define TRUE	(!FALSE)
#endif

static void
run_sbin_hotplug(struct ocp_device *pdev, int insert)
{
	int i;
	char *argv[3], *envp[8];
	char id[20], sub_id[24], bus_id[24], class_id[20];

	if (!hotplug_path[0])
		return;
#if 0
	sprintf(class_id, "PCI_CLASS=%04X", pdev->class);
	sprintf(id, "PCI_ID=%04X:%04X", pdev->vendor, pdev->device);
	sprintf(sub_id, "PCI_SUBSYS_ID=%04X:%04X", pdev->subsystem_vendor, pdev->subsystem_device);
	sprintf(bus_id, "PCI_SLOT_NAME=%s", pdev->slot_name);
#endif
	i = 0;
	argv[i++] = hotplug_path;
	argv[i++] = "ocp";
	argv[i] = 0;

	i = 0;
	/* minimal command environment */
	envp[i++] = "HOME=/";
	envp[i++] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";
	
	/* other stuff we want to pass to /sbin/hotplug */
	envp[i++] = class_id;
	envp[i++] = id;
	envp[i++] = sub_id;
	envp[i++] = bus_id;
	if (insert)
		envp[i++] = "ACTION=add";
	else
		envp[i++] = "ACTION=remove";
	envp[i] = 0;

	call_usermodehelper (argv [0], argv, envp);
}

/**
 * ocp_insert_device - insert a hotplug device
 * @dev: the device to insert
 * @bus: where to insert it
 *
 * Add a new device to the device lists and notify userspace (/sbin/hotplug).
 */
void
ocp_insert_device(struct ocp_device *dev, struct ocp_bus *bus)
{
	list_add_tail(&dev->bus_list, &bus->devices);
	list_add_tail(&dev->global_list, &ocp_devices);
	/* notify userspace of new hotplug device */
	run_sbin_hotplug(dev, TRUE);
}

#if 0
static void
ocp_free_resources(struct ocp_device *dev)
{
	int i;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		struct resource *res = dev->resource + i;
		if (res->parent)
			release_resource(res);
	}
}
#endif

/**
 * ocp_remove_device - remove a hotplug device
 * @dev: the device to remove
 *
 * Delete the device structure from the device lists and 
 * notify userspace (/sbin/hotplug).
 */
void
ocp_remove_device(struct ocp_device *dev)
{
	put_device(&dev->dev);
	list_del(&dev->bus_list);
	list_del(&dev->global_list);
//	ocp_free_resources(dev);

	/* notify userspace of hotplug device removal */
	run_sbin_hotplug(dev, FALSE);
}

EXPORT_SYMBOL(ocp_insert_device);
EXPORT_SYMBOL(ocp_remove_device);
